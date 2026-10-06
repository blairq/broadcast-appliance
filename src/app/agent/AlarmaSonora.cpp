// SPDX-License-Identifier: GPL-2.0-or-later
#include "agent/AlarmaSonora.hpp"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QSoundEffect>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <numbers>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

namespace ba::app::agent {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

/// Un WAV mono de 16 bits a 48 kHz con tonos: [{frecuencia, ms}] (0 Hz = silencio).
void escribirWav(const fs::path& ruta, const std::vector<std::pair<double, int>>& tramos) {
    constexpr int kTasa = 48000;
    std::vector<std::int16_t> m;
    for (const auto& [f, ms] : tramos) {
        const int n = kTasa * ms / 1000;
        for (int i = 0; i < n; ++i) {
            // Entrada y salida suaves (5 ms) para que no haga clic.
            const double env = std::min({1.0, i / 240.0, (n - i) / 240.0});
            const double v = f > 0 ? std::sin(2 * std::numbers::pi * f * i / kTasa) * 0.6 * env : 0.0;
            m.push_back(static_cast<std::int16_t>(v * 32767));
        }
    }
    std::ofstream o(ruta, std::ios::binary);
    const auto u32 = [&o](std::uint32_t x) { o.write(reinterpret_cast<const char*>(&x), 4); };
    const auto u16 = [&o](std::uint16_t x) { o.write(reinterpret_cast<const char*>(&x), 2); };
    const std::uint32_t datos = static_cast<std::uint32_t>(m.size() * 2);
    o.write("RIFF", 4);
    u32(36 + datos);
    o.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(kTasa);
    u32(kTasa * 2);
    u16(2);
    u16(16);
    o.write("data", 4);
    u32(datos);
    o.write(reinterpret_cast<const char*>(m.data()), datos);
}

} // namespace

AlarmaSonora::AlarmaSonora(fs::path datos, QObject* parent) : QObject(parent), datos_(std::move(datos)) {
    std::error_code ec;
    fs::create_directories(datos_ / "Runtime", ec);
    const fs::path a = datos_ / "Runtime" / "alarma-aviso.wav", c = datos_ / "Runtime" / "alarma-critica.wav";
    escribirWav(a, {{880, 140}, {0, 80}, {1175, 180}});
    escribirWav(c, {{988, 220}, {0, 60}, {740, 220}, {0, 500}});
    {
        std::ifstream f(datos_ / "Config" / "alarmas.json");
        const json j = json::parse(f, nullptr, false);
        if (j.is_object()) {
            volumen_ = std::clamp(j.value("volumen", volumen_), 0.0, 1.0);
            dispositivo_ = QString::fromStdString(j.value("dispositivo", std::string()));
        }
    }
    aviso_ = std::make_unique<QSoundEffect>();
    critico_ = std::make_unique<QSoundEffect>();
    aviso_->setSource(QUrl::fromLocalFile(QString::fromStdWString(a.wstring())));
    critico_->setSource(QUrl::fromLocalFile(QString::fromStdWString(c.wstring())));
    critico_->setLoopCount(QSoundEffect::Infinite);
    aplicar();
}

AlarmaSonora::~AlarmaSonora() = default;

void AlarmaSonora::aplicar() {
    for (QSoundEffect* s : {aviso_.get(), critico_.get()}) {
        s->setVolume(static_cast<float>(volumen_));
        if (!dispositivo_.isEmpty())
            for (const QAudioDevice& d : QMediaDevices::audioOutputs())
                if (d.description() == dispositivo_) s->setAudioDevice(d);
    }
}

void AlarmaSonora::sonar(const QString& nivel) {
    if (nivel == "critico") {
        if (!critico_->isPlaying()) critico_->play();
    } else if (nivel == "aviso") {
        aviso_->play();
    }
}

void AlarmaSonora::criticaActiva(bool hay) {
    if (!hay && critico_->isPlaying()) critico_->stop();
}

QVariantMap AlarmaSonora::ajustesUi() const { return {{"volumen", volumen_}, {"dispositivo", dispositivo_}}; }

QString AlarmaSonora::ponerAjustesUi(const QVariantMap& a) {
    if (a.contains("volumen")) volumen_ = std::clamp(a.value("volumen").toDouble(), 0.0, 1.0);
    if (a.contains("dispositivo")) dispositivo_ = a.value("dispositivo").toString();
    aplicar();
    guardar();
    return {};
}

QStringList AlarmaSonora::dispositivosUi() const {
    QStringList out{""};
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) out << d.description();
    return out;
}

void AlarmaSonora::guardar() const {
    std::error_code ec;
    fs::create_directories(datos_ / "Config", ec);
    std::ofstream o(datos_ / "Config" / "alarmas.json");
    o << json{{"volumen", volumen_}, {"dispositivo", dispositivo_.toStdString()}}.dump(1);
}

} // namespace ba::app::agent
