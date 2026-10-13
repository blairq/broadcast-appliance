// src/app/audio/LazoMedicion.cpp — ver el .hpp
#include "audio/LazoMedicion.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QMediaDevices>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ba::app::audio {

namespace {

QAudioDevice buscarDispositivo(bool salida, const QString& id) {
    const auto lista = salida ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();
    for (const auto& d : lista)
        if (QString::fromUtf8(d.id()) == id || d.description() == id) return d;
    return {};
}

// El formato: la frecuencia pedida, en float (o 16 bits si el dispositivo no da float).
QAudioFormat formatoPara(const QAudioDevice& d, int frecuencia) {
    QAudioFormat f;
    f.setSampleRate(frecuencia);
    f.setChannelCount(std::clamp(d.preferredFormat().channelCount(), 1, 8));
    f.setSampleFormat(QAudioFormat::Float);
    if (d.isFormatSupported(f)) return f;
    f.setSampleFormat(QAudioFormat::Int16);
    if (d.isFormatSupported(f)) return f;
    return {};
}

// Con los dispositivos de verdad.
class LazoReal : public QObject, public LazoMedicion {
   public:
    LazoReal(QString salida, int canalSalida, QString entrada, int canalEntrada)
        : salida_(std::move(salida)), entrada_(std::move(entrada)), canalS_(canalSalida), canalE_(canalEntrada) {}
    ~LazoReal() override { parar(); }

    QString arrancar(const std::vector<float>& estimulo, int frecuencia, Listo listo) override {
        const QAudioDevice ds = buscarDispositivo(true, salida_), de = buscarDispositivo(false, entrada_);
        if (ds.isNull()) return "no está la salida «" + salida_ + "»";
        if (de.isNull()) return "no está la entrada «" + entrada_ + "»";
        const QAudioFormat fs = formatoPara(ds, frecuencia), fe = formatoPara(de, frecuencia);
        if (!fs.isValid()) return "la salida no acepta " + QString::number(frecuencia) + " Hz";
        if (!fe.isValid()) return "la entrada no acepta " + QString::number(frecuencia) + " Hz";
        if (canalS_ >= fs.channelCount()) return "la salida tiene " + QString::number(fs.channelCount()) + " canales";
        if (canalE_ >= fe.channelCount()) return "la entrada tiene " + QString::number(fe.channelCount()) + " canales";
        listo_ = std::move(listo);
        fe_ = fe;
        previo_ = static_cast<size_t>(frecuencia / 4);
        captura_.clear();
        captura_.reserve(estimulo.size() + static_cast<size_t>(frecuencia) * 2);

        // Lo que se emite: el barrido en el canal elegido (o en todos, -1), silencio en el resto.
        const int ch = fs.channelCount();
        const int bps = fs.bytesPerSample();
        datos_.resize(static_cast<qsizetype>(estimulo.size()) * ch * bps);
        datos_.fill(0);
        for (size_t i = 0; i < estimulo.size(); ++i)
            for (int c = 0; c < ch; ++c) {
                if (canalS_ >= 0 && c != canalS_) continue;
                char* p = datos_.data() + (static_cast<qsizetype>(i) * ch + c) * bps;
                if (fs.sampleFormat() == QAudioFormat::Float) {
                    std::memcpy(p, &estimulo[i], 4);
                } else {
                    const auto v = static_cast<int16_t>(std::lround(std::clamp(estimulo[i], -1.0f, 1.0f) * 32767.0f));
                    std::memcpy(p, &v, 2);
                }
            }
        buffer_ = std::make_unique<QBuffer>(&datos_);
        buffer_->open(QIODevice::ReadOnly);

        fuente_ = std::make_unique<QAudioSource>(de, fe);
        fuente_->setBufferSize(fe.bytesForDuration(100000));
        entradaIo_ = fuente_->start();
        if (entradaIo_ == nullptr) {
            parar();
            return "no se pudo abrir la entrada";
        }
        QObject::connect(entradaIo_, &QIODevice::readyRead, this, [this] { leer(); });
        sumidero_ = std::make_unique<QAudioSink>(ds, fs);
        sumidero_->setBufferSize(fs.bytesForDuration(100000));
        QObject::connect(sumidero_.get(), &QAudioSink::stateChanged, this, [this](QAudio::State s) {
            // Se vació: un rato más de grabación (la cola de la sala y el retardo de la placa).
            if (s == QAudio::IdleState && !terminando_) {
                terminando_ = true;
                QTimer::singleShot(800, this, [this] { terminar({}); });
            }
        });
        // La grabación arranca primero (y esos 250 ms se descartan al final): lo que
        // tarde la salida queda adentro como retardo del directo.
        QTimer::singleShot(250, this, [this] {
            if (sumidero_) sumidero_->start(buffer_.get());
        });
        const int tope = static_cast<int>(estimulo.size() * 1000 / static_cast<size_t>(frecuencia)) + 6000;
        QTimer::singleShot(tope, this, [this] {
            if (listo_) terminar("la salida no terminó de sonar (¿dispositivo trabado?)");
        });
        return {};
    }

    void cancelar() override {
        listo_ = nullptr;
        parar();
    }

   private:
    void leer() {
        if (entradaIo_ == nullptr) return;
        const QByteArray b = entradaIo_->readAll();
        const int ch = fe_.channelCount(), bps = fe_.bytesPerSample();
        const qsizetype frames = b.size() / (ch * bps);
        for (qsizetype i = 0; i < frames; ++i) {
            const char* p = b.constData() + (i * ch + canalE_) * bps;
            float v;
            if (fe_.sampleFormat() == QAudioFormat::Float) {
                std::memcpy(&v, p, 4);
            } else {
                int16_t s;
                std::memcpy(&s, p, 2);
                v = static_cast<float>(s) / 32768.0f;
            }
            captura_.push_back(v);
        }
    }
    void terminar(const QString& error) {
        leer();
        parar();
        if (auto l = std::move(listo_)) {
            listo_ = nullptr;
            if (captura_.size() > previo_) captura_.erase(captura_.begin(), captura_.begin() + static_cast<std::ptrdiff_t>(previo_));
            l(error.isEmpty() ? std::move(captura_) : std::vector<float>{}, error);
        }
    }
    void parar() {
        if (sumidero_) sumidero_->stop();
        if (fuente_) fuente_->stop();
        entradaIo_ = nullptr;
        sumidero_.reset();
        fuente_.reset();
        buffer_.reset();
        terminando_ = false;
    }

    QString salida_, entrada_;
    int canalS_ = 0, canalE_ = 0;
    Listo listo_;
    QAudioFormat fe_;
    QByteArray datos_;
    std::unique_ptr<QBuffer> buffer_;
    std::unique_ptr<QAudioSink> sumidero_;
    std::unique_ptr<QAudioSource> fuente_;
    QIODevice* entradaIo_ = nullptr;
    std::vector<float> captura_;
    bool terminando_ = false;
    size_t previo_ = 0;
};

class LazoSim : public QObject, public LazoMedicion {
   public:
    LazoSim(nlohmann::json sala, nlohmann::json pa, nlohmann::json mic)
        : sala_(std::move(sala)), pa_(std::move(pa)), mic_(std::move(mic)) {}
    QString arrancar(const std::vector<float>& estimulo, int frecuencia, Listo listo) override {
        listo_ = std::move(listo);
        std::vector<float> x = estimulo;
        QTimer::singleShot(0, this, [this, x = std::move(x), frecuencia] {
            // Como la de verdad: la grabación sigue 800 ms después.
            std::vector<float> y = simularSala(sala_, pa_, mic_, x, frecuencia);
            y.resize(y.size() + static_cast<size_t>(frecuencia * 0.8), 0.0f);
            if (auto l = std::move(listo_)) l(std::move(y), {});
        });
        return {};
    }
    void cancelar() override { listo_ = nullptr; }

   private:
    nlohmann::json sala_, pa_, mic_;
    Listo listo_;
};

} // namespace

QVariantList dispositivosAudio(bool salidas, bool conSimulada) {
    QVariantList r;
    const auto lista = salidas ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();
    for (const auto& d : lista)
        r.append(QVariantMap{{"id", QString::fromUtf8(d.id())},
                             {"nombre", d.description()},
                             {"canales", std::clamp(d.preferredFormat().channelCount(), 1, 8)}});
    if (conSimulada)
        r.append(QVariantMap{{"id", QString(kSalaSimulada)}, {"nombre", "Sala simulada (no suena)"}, {"canales", 2}});
    return r;
}

std::unique_ptr<LazoMedicion> lazoReal(const QString& salida, int canalSalida, const QString& entrada, int canalEntrada) {
    return std::make_unique<LazoReal>(salida, canalSalida, entrada, canalEntrada);
}

std::unique_ptr<LazoMedicion> lazoSimulado(const nlohmann::json& sala, const nlohmann::json& parlante,
                                           const nlohmann::json& microfono) {
    return std::make_unique<LazoSim>(sala, parlante, microfono);
}

} // namespace ba::app::audio
