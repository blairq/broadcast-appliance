// src/app/audio/EquipoAudio.cpp — ver EquipoAudio.hpp

#include "audio/EquipoAudio.hpp"

#include "audio/AnilloAudio.hpp"
#include "audio/GstVoz.hpp"

#include "emision/FuentesApp.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QPointer>
#include <QThread>

#if defined(__APPLE__)
#include <CoreAudio/CoreAudio.h>
#endif
#include "comun/HoraMonotonica.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <mutex>
#include <thread>

namespace ba::app::audio {

namespace {

constexpr const char* kTipo = "ba_entrada_fisica";
constexpr int kMaxCanales = 32;

/// Un poco adelante de la captura: lo que la entrada del motor necesita para no tirarlo por viejo.
constexpr std::uint64_t kAdelanteNs = 20'000'000ULL;

QAudioFormat formato(int canales) {
    QAudioFormat f;
    f.setSampleRate(48000);
    f.setChannelCount(canales);
    f.setSampleFormat(QAudioFormat::Float);
    return f;
}

std::string idDe(const QAudioDevice& d) { return QString::fromLatin1(d.id()).toStdString(); }

#if defined(__APPLE__)
/// Los canales físicos según CoreAudio (Qt en la Mac informa los que puede
/// convertir: 16 para las bocinas de la notebook, que tienen 2).
int canalesCoreAudio(const std::string& uid, bool entrada) {
    CFStringRef cfUid = CFStringCreateWithCString(nullptr, uid.c_str(), kCFStringEncodingUTF8);
    if (cfUid == nullptr) return 0;
    AudioDeviceID dev = kAudioObjectUnknown;
    AudioValueTranslation tr{&cfUid, sizeof(cfUid), &dev, sizeof(dev)};
    AudioObjectPropertyAddress a{kAudioHardwarePropertyDeviceForUID, kAudioObjectPropertyScopeGlobal,
                                 kAudioObjectPropertyElementMain};
    UInt32 tam = sizeof(tr);
    const OSStatus st = AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &tam, &tr);
    CFRelease(cfUid);
    if (st != noErr || dev == kAudioObjectUnknown) return 0;
    AudioObjectPropertyAddress c{kAudioDevicePropertyStreamConfiguration,
                                 entrada ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                 kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyDataSize(dev, &c, 0, nullptr, &tam) != noErr || tam == 0) return 0;
    std::vector<std::uint8_t> buf(tam);
    auto* lista = reinterpret_cast<AudioBufferList*>(buf.data());
    if (AudioObjectGetPropertyData(dev, &c, 0, nullptr, &tam, lista) != noErr) return 0;
    int n = 0;
    for (UInt32 i = 0; i < lista->mNumberBuffers; ++i)
        n += static_cast<int>(lista->mBuffers[i].mNumberChannels);
    return n;
}
#endif

#if defined(__APPLE__)
/// Lo que ya contestó CoreAudio. La consulta va en un hilo aparte: si el
/// servicio de audio del sistema (coreaudiod) no responde, se queda colgada
/// ahí y no en el arranque de la app (con la consulta en el hilo principal,
/// el launcher la daba por colgada y la reiniciaba en bucle).
struct CacheCanales {
    std::mutex mu;
    std::map<std::string, int> valor;
    std::set<std::string> pidiendo;
};
CacheCanales& cacheCanales() {
    static CacheCanales c;
    return c;
}
#endif

/// Cuántos canales tiene de verdad el dispositivo. En la Mac, si CoreAudio
/// todavía no contestó, 2 por ahora; al saberlo se avisa a `equipo`, que
/// rearma ese dispositivo.
int canalesDe(const QAudioDevice& d, bool entrada, EquipoAudio* equipo = nullptr) {
#if defined(__APPLE__)
    const std::string uid = idDe(d);
    const std::string clave = uid + (entrada ? "|e" : "|s");
    CacheCanales& c = cacheCanales();
    {
        std::lock_guard<std::mutex> l(c.mu);
        if (const auto it = c.valor.find(clave); it != c.valor.end())
            return it->second > 0 ? std::clamp(it->second, 1, kMaxCanales) : 2;
        if (!c.pidiendo.contains(clave)) {
            c.pidiendo.insert(clave);
            QPointer<EquipoAudio> avisar(equipo);
            std::thread([uid, clave, entrada, avisar]() {
                const int n = canalesCoreAudio(uid, entrada);
                {
                    CacheCanales& cc = cacheCanales();
                    std::lock_guard<std::mutex> l2(cc.mu);
                    cc.valor[clave] = n;
                    cc.pidiendo.erase(clave);
                }
                if (avisar)
                    QMetaObject::invokeMethod(
                        avisar.data(),
                        [avisar]() {
                            if (avisar) avisar->canalesSabidos();
                        },
                        Qt::QueuedConnection);
            }).detach();
        }
    }
    return 2;
#else
    (void)entrada;
    (void)equipo;
    // Fuera de la Mac, el formato preferido es el del dispositivo (WASAPI: el del mezclador).
    const int pref = d.preferredFormat().channelCount();
    return std::clamp(pref > 0 ? pref : 2, 1, kMaxCanales);
#endif
}

/// Los dispositivos de entrada que ya son una fuente de la app (el micrófono
/// de la Mac en Audio): no se repiten como entrada física.
std::function<std::set<std::string>()> g_reservados;

std::set<std::string> yaUsados() {
    std::set<std::string> out = g_reservados ? g_reservados() : std::set<std::string>{};
    // Los micrófonos de la app (su dispositivo, por id estable).
    for (const auto& [n, tipo] : fuentes::listar()) {
        const nlohmann::json a = fuentes::ajustesDe(n);
        for (const char* k : {"microfono", "device_id"})
            if (!a.value(k, std::string()).empty()) out.insert(a.value(k, std::string()));
    }
    return out;
}

} // namespace

void setDispositivosReservados(std::function<std::set<std::string>()> f) { g_reservados = std::move(f); }


// --- El hilo de los dispositivos ------------------------------------------------------
//
// Abrir o cerrar un dispositivo (QAudioSink, QAudioSource) habla con CoreAudio
// (o WASAPI, o PulseAudio) y, si el audio del sistema anda trabado, se queda
// esperando. En el hilo principal eso colgaba la app (el launcher la daba por
// muerta). Todo lo que toca un dispositivo va a este hilo; el principal solo
// pide y sigue. El hilo no se cierra nunca (si quedó trabado, la app igual
// puede salir).
namespace {
QObject* trabajador() {
    static QObject* t = []() {
        auto* hilo = new QThread();
        hilo->setObjectName(QStringLiteral("ba-equipo-audio"));
        hilo->start();
        auto* o = new QObject();
        o->moveToThread(hilo);
        return o;
    }();
    return t;
}
} // namespace

// --- Salidas -------------------------------------------------------------------------

struct EquipoAudio::Salida {
    QAudioDevice dev;
    std::string id, nombre;
    int canales = 2;
    std::vector<std::shared_ptr<anillo::Ring>> pares;
    /// Lo que vive en el hilo de los dispositivos.
    struct Hilo {
        std::unique_ptr<anillo::AlDispositivoMulti> io;
        std::unique_ptr<QAudioSink> sink;
        std::atomic<bool> abierta{false};
        std::mutex mu;
        std::string error; ///< por qué no se abrió (bajo mu)
    };
    std::shared_ptr<Hilo> h = std::make_shared<Hilo>();
    bool pedida = false;

    bool abierta() const { return h->abierta.load(); }
    void abrir() {
        if (pedida) return;
        pedida = true;
        QMetaObject::invokeMethod(
            trabajador(),
            [hh = h, d = dev, p = pares, c = canales, n = nombre]() {
                if (hh->sink) return;
                const auto poner = [&](const std::string& e) {
                    std::lock_guard<std::mutex> l(hh->mu);
                    hh->error = e;
                };
                if (!d.isFormatSupported(formato(c))) {
                    std::printf("ba-app: la salida '%s' no acepta %d canales a 48 kHz\n", n.c_str(), c);
                    poner("no acepta " + std::to_string(c) + " canales a 48 kHz (float)");
                    return;
                }
                hh->io = std::make_unique<anillo::AlDispositivoMulti>(p, c);
                hh->sink = std::make_unique<QAudioSink>(d, formato(c));
                hh->sink->setBufferSize(static_cast<qsizetype>(anillo::kColchon * c * sizeof(float) * 2));
                hh->sink->start(hh->io.get());
                if (hh->sink->error() != QAudio::NoError) {
                    poner("no se pudo abrir (error de Qt " + std::to_string(static_cast<int>(hh->sink->error())) + ")");
                    hh->sink.reset();
                    hh->io.reset();
                    return;
                }
                poner("");
                hh->abierta = true;
            },
            Qt::QueuedConnection);
    }
    void cerrar() {
        if (!pedida) return;
        pedida = false;
        QMetaObject::invokeMethod(
            trabajador(),
            [hh = h]() {
                if (hh->sink) hh->sink->stop();
                hh->sink.reset();
                hh->io.reset();
                hh->abierta = false;
            },
            Qt::QueuedConnection);
    }
};

// --- Entradas ------------------------------------------------------------------------

struct EquipoAudio::Entrada {
    QAudioDevice dev;
    std::string id, nombre;
    int canales = 1;
    std::vector<std::string> fuentes; ///< una por canal (el nombre de su entrada en el motor)
    /// Lo que vive en el hilo de los dispositivos.
    struct Hilo {
        std::unique_ptr<QAudioSource> src;
        QIODevice* io = nullptr;
        std::vector<float> uno;
        std::vector<std::string> fuentes;
        int canales = 1;
        std::atomic<bool> abierta{false};
        std::atomic<std::uint64_t> leidos{0}; ///< cuadros que llegaron del dispositivo
    };
    std::shared_ptr<Hilo> h = std::make_shared<Hilo>();
    bool pedida = false;

    bool abierta() const { return h->abierta.load(); }
    void abrir(QObject*) {
        if (pedida) return;
        pedida = true;
        QMetaObject::invokeMethod(
            trabajador(),
            [hh = h, d = dev, c = canales, n = nombre, refs = fuentes]() {
                if (hh->src) return;
                hh->fuentes = refs;
                hh->canales = c;
                if (!d.isFormatSupported(formato(c))) {
                    std::printf("ba-app: la entrada '%s' no da %d canales a 48 kHz\n", n.c_str(), c);
                    return;
                }
                hh->src = std::make_unique<QAudioSource>(d, formato(c));
                hh->io = hh->src->start();
                if (hh->io == nullptr) {
                    hh->src.reset();
                    return;
                }
                QObject::connect(hh->io, &QIODevice::readyRead, trabajador(),
                                 [w = std::weak_ptr<Hilo>(hh)]() {
                                     if (auto x = w.lock()) leer(*x);
                                 });
                hh->abierta = true;
            },
            Qt::QueuedConnection);
    }
    void cerrar() {
        if (!pedida) return;
        pedida = false;
        QMetaObject::invokeMethod(
            trabajador(),
            [hh = h]() {
                if (hh->src) hh->src->stop();
                hh->src.reset();
                hh->io = nullptr;
                hh->abierta = false;
                hh->fuentes.clear();
            },
            Qt::QueuedConnection);
    }
    /// Lo que llegó, separado por canal: cada uno a la cola de su entrada
    /// (`bavoz`), en el hilo de los dispositivos.
    static void leer(Hilo& x) {
        if (x.io == nullptr) return;
        const QByteArray b = x.io->readAll();
        const int canales = x.canales;
        const auto frames =
            static_cast<std::uint32_t>(b.size() / static_cast<qsizetype>(canales * sizeof(float)));
        if (frames == 0) return;
        x.leidos += frames;
        const auto* m = reinterpret_cast<const float*>(b.constData());
        x.uno.resize(frames);
        const std::uint64_t ts = ba::app::ahoraNs() + kAdelanteNs;
        for (int c = 0; c < canales && c < static_cast<int>(x.fuentes.size()); ++c) {
            for (std::uint32_t i = 0; i < frames; ++i)
                x.uno[i] = m[static_cast<std::size_t>(i) * canales + c];
            gstvoz::encolar(x.fuentes[static_cast<std::size_t>(c)], x.uno.data(), frames, 48000, ts);
        }
    }
    void crearFuentes() {
        gstvoz::registrarElemento(); // bavoz, antes de armar las entradas
        for (int c = 0; c < canales; ++c) {
            const std::string n = canales == 1 ? nombre : nombre + " · Entrada " + std::to_string(c + 1);
            // Con ese nombre hay OTRA cosa (una entrada de audio de la app): no se la toma.
            const auto todas = fuentes::listar();
            const bool existe = std::any_of(todas.begin(), todas.end(), [&](const auto& p) { return p.first == n; });
            if (existe && !fuentes::ajustesDe(n).value("fisica", false)) {
                std::printf("ba-app: la entrada física '%s' choca con otra fuente; no se crea\n", n.c_str());
                continue;
            }
            std::string nom;
            for (char ch : n) {
                if (ch == '\\' || ch == '"') nom += '\\';
                nom += ch;
            }
            // Una entrada global del motor que lee la cola de este canal; de
            // fábrica sin pistas (no sale al aire sola).
            const nlohmann::json aj{{"pipeline", ""},
                                    {"pipeline_audio", "bavoz voz=\"" + nom + "\""},
                                    {"audio", true},
                                    {"normalizar", false},
                                    {"estudio", false},
                                    {"fisica", true},
                                    {"voz", n}};
            if (fuentes::crearGlobal(n, "ba_gst_fuente", aj, false, {})) fuentes.push_back(n);
        }
    }
    void quitarFuentes() {
        cerrar();
        for (const auto& n : fuentes) {
            gstvoz::vaciar(n);
            fuentes::quitarGlobal(n);
        }
        fuentes.clear();
    }
};

// --- EquipoAudio ---------------------------------------------------------------------

EquipoAudio::EquipoAudio(QObject* parent) : QObject(parent) {
    connect(&medios_, &QMediaDevices::audioOutputsChanged, this, [this]() { refrescar(); });
    connect(&medios_, &QMediaDevices::audioInputsChanged, this, [this]() { refrescar(); });
    refrescar();
}

EquipoAudio::~EquipoAudio() {
    for (auto& [id, s] : salidas_)
        s->cerrar();
    for (auto& [id, e] : entradas_)
        e->quitarFuentes();
}

bool EquipoAudio::visible(const std::string& id) const {
    if (!mostrar_) return false;
    const auto it = visibles_.find(id);
    return it == visibles_.end() || it->second;
}

void EquipoAudio::refrescar() {
    // Salidas: las que hay y se ven.
    std::set<std::string> vistas;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) {
        const std::string id = idDe(d);
        if (!visible(id)) continue;
        vistas.insert(id);
        if (salidas_.contains(id)) continue;
        auto s = std::make_unique<Salida>();
        s->dev = d;
        s->id = id;
        s->nombre = d.description().toStdString();
        s->canales = canalesDe(d, false, this);
        for (int p = 0; p < (s->canales + 1) / 2; ++p)
            s->pares.push_back(std::make_shared<anillo::Ring>());
        salidas_[id] = std::move(s);
    }
    for (auto it = salidas_.begin(); it != salidas_.end();) {
        if (vistas.contains(it->first)) {
            ++it;
            continue;
        }
        it->second->cerrar();
        it = salidas_.erase(it);
    }
    // Entradas: las que hay, se ven y no son ya una entrada de la app.
    vistas.clear();
    const std::set<std::string> usados = yaUsados();
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) {
        const std::string id = idDe(d);
        if (!visible(id) || usados.contains(id)) continue;
        vistas.insert(id);
        if (entradas_.contains(id)) continue;
        auto e = std::make_unique<Entrada>();
        e->dev = d;
        e->id = id;
        e->nombre = d.description().toStdString();
        e->canales = canalesDe(d, true, this);
        e->crearFuentes();
        entradas_[id] = std::move(e);
    }
    for (auto it = entradas_.begin(); it != entradas_.end();) {
        if (vistas.contains(it->first)) {
            ++it;
            continue;
        }
        it->second->quitarFuentes();
        it = entradas_.erase(it);
    }
    emit cambio();
}

void EquipoAudio::canalesSabidos() {
    // CoreAudio contestó cuántos canales tiene algún dispositivo: el que
    // quedó armado con otra cantidad se rearma.
    bool algo = false;
    for (auto it = salidas_.begin(); it != salidas_.end();) {
        if (canalesDe(it->second->dev, false) != it->second->canales) {
            it->second->cerrar();
            it = salidas_.erase(it);
            algo = true;
        } else {
            ++it;
        }
    }
    for (auto it = entradas_.begin(); it != entradas_.end();) {
        if (canalesDe(it->second->dev, true) != it->second->canales) {
            it->second->quitarFuentes();
            it = entradas_.erase(it);
            algo = true;
        } else {
            ++it;
        }
    }
    if (algo) refrescar();
}

std::vector<EquipoAudio::Columna> EquipoAudio::columnas() const {
    std::vector<Columna> out;
    for (const auto& [id, s] : salidas_)
        for (int p = 0; p < static_cast<int>(s->pares.size()); ++p) {
            const int c0 = 2 * p + 1, c1 = std::min(2 * p + 2, s->canales);
            const std::string canales =
                s->canales <= 2
                    ? std::string()
                    : (c1 > c0 ? std::to_string(c0) + "-" + std::to_string(c1) : std::to_string(c0));
            out.push_back({"fis:" + id + ":" + std::to_string(p),
                           canales.empty() ? s->nombre : s->nombre + " · " + canales, s->nombre});
        }
    return out;
}

std::shared_ptr<anillo::Ring> EquipoAudio::anilloDe(const std::string& columna) const {
    // "fis:<id>:<par>" (el id puede tener «:»: el par es lo último).
    if (columna.rfind("fis:", 0) != 0) return nullptr;
    const auto dos = columna.rfind(':');
    const std::string id = columna.substr(4, dos - 4);
    const int par = std::atoi(columna.c_str() + dos + 1);
    const auto it = salidas_.find(id);
    if (it == salidas_.end() || par < 0 || par >= static_cast<int>(it->second->pares.size())) return nullptr;
    return it->second->pares[static_cast<std::size_t>(par)];
}

void EquipoAudio::usarColumnas(const std::set<std::string>& usadas) {
    for (auto& [id, s] : salidas_) {
        bool usa = false;
        for (const auto& c : usadas)
            if (c.rfind("fis:" + id + ":", 0) == 0) usa = true;
        if (usa)
            s->abrir();
        else
            s->cerrar();
    }
}

void EquipoAudio::usarEntradas(const std::set<std::string>& usadas) {
    for (auto& [id, e] : entradas_) {
        bool usa = false;
        for (const auto& f : e->fuentes)
            if (usadas.contains(f)) usa = true;
        if (usa)
            e->abrir(this);
        else
            e->cerrar();
    }
}

bool EquipoAudio::esEntradaFisica(const std::string& fuente) const {
    for (const auto& [id, e] : entradas_)
        for (const auto& f : e->fuentes)
            if (fuente == f) return true;
    return false;
}

void EquipoAudio::setMostrar(bool on) {
    if (mostrar_ == on) return;
    mostrar_ = on;
    refrescar();
}

void EquipoAudio::setVisible(const std::string& id, bool v) {
    visibles_[id] = v;
    refrescar();
}

nlohmann::json EquipoAudio::estadoDispositivos() const {
    nlohmann::json sal = nlohmann::json::array(), ent = nlohmann::json::array();
    for (const auto& [id, s] : salidas_) {
        std::string error;
        {
            std::lock_guard<std::mutex> l(s->h->mu);
            error = s->h->error;
        }
        nlohmann::json j{{"id", id}, {"nombre", s->nombre}, {"canales", s->canales}, {"pedida", s->pedida},
                         {"abierta", s->abierta()}, {"error", error}};
        if (s->h->io) {
            j["leidos"] = s->h->io->leidos.load();
            const float p = s->h->io->pico.load();
            j["pico_db"] = p > 1e-5f ? 20.0 * std::log10(p) : -100.0;
        }
        sal.push_back(j);
    }
    for (const auto& [id, e] : entradas_)
        ent.push_back({{"id", id}, {"nombre", e->nombre}, {"canales", e->canales}, {"pedida", e->pedida},
                       {"abierta", e->abierta()}, {"leidos", e->h->leidos.load()}, {"fuentes", e->fuentes}});
    return {{"salidas", sal}, {"entradas", ent}};
}

QVariantList EquipoAudio::dispositivos() const {
    QVariantList out;
    const std::set<std::string> usados = yaUsados();
    const auto agregar = [&](const QAudioDevice& d, const char* tipo) {
        const std::string id = idDe(d);
        const auto it = visibles_.find(id);
        bool abierto = false;
        if (std::string(tipo) == "salida") {
            const auto s = salidas_.find(id);
            abierto = s != salidas_.end() && s->second->abierta();
        } else {
            const auto e = entradas_.find(id);
            abierto = e != entradas_.end() && e->second->abierta();
        }
        out.push_back(QVariantMap{{"id", QString::fromStdString(id)},
                                  {"nombre", d.description()},
                                  {"tipo", tipo},
                                  {"canales", canalesDe(d, std::string(tipo) == "entrada")},
                                  {"visible", it == visibles_.end() || it->second},
                                  {"abierto", abierto},
                                  {"enApp", std::string(tipo) == "entrada" && usados.contains(id)}});
    };
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        agregar(d, "salida");
    for (const QAudioDevice& d : QMediaDevices::audioInputs())
        agregar(d, "entrada");
    return out;
}

nlohmann::json EquipoAudio::guardado() const {
    nlohmann::json v = nlohmann::json::object();
    for (const auto& [id, on] : visibles_)
        v[id] = on;
    return {{"mostrar", mostrar_}, {"visibles", v}};
}

void EquipoAudio::cargar(const nlohmann::json& j) {
    if (!j.is_object()) return;
    mostrar_ = j.value("mostrar", true);
    visibles_.clear();
    for (const auto obj_ = j.value("visibles", nlohmann::json::object()); const auto& [id, on] : obj_.items())
        if (on.is_boolean()) visibles_[id] = on.get<bool>();
    refrescar();
}

} // namespace ba::app::audio
