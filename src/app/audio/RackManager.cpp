// ============================================================================
// src/app/audio/RackManager.cpp — ver RackManager.hpp
// ============================================================================

#include "audio/RackManager.hpp"
#include "audio/GrupoDeSalida.hpp"
#include "platform/Paths.hpp"
#include "dispositivos/Conocidos.hpp"

#include "audio/GrabadorWav.hpp"
#include "audio/GstRack.hpp"
#include "emision/InputSpecs.hpp"

#include <mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QThread>

#include <atomic>

#include "emision/BroadcastEngine.hpp"
#include "emision/FuentesApp.hpp"
#include "comun/HoraMonotonica.hpp"
#include "Motor.hpp"
#include "audio/AnilloAudio.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace ba::app::audio {

namespace fs = std::filesystem;

namespace {

BroadcastEngine* motorApp() { return fuentes::motorApp(); }

bool existe(const std::string& f) {
    BroadcastEngine* e = motorApp();
    return e != nullptr && e->sourceExists(f);
}

/// ¿Tiene audio? (una cámara de GStreamer con `audio` apagado, no).
bool conAudio(const std::string& f) {
    BroadcastEngine* e = motorApp();
    if (e == nullptr) return false;
    const auto v = e->audioSources();
    return std::find(v.begin(), v.end(), f) != v.end();
}

nlohmann::json ajustesDe(const std::string& f) { return fuentes::ajustesDe(f); }

/// El tipo lógico (inputs::tipoLogico): un micrófono o un invitado por GStreamer
/// se agrupan y usan las plantillas por tipo.
std::string tipoLogicoDe(const std::string& f) {
    BroadcastEngine* e = motorApp();
    return e != nullptr ? inputs::tipoLogico(e->sourceTypeId(f), ajustesDe(f)) : std::string();
}

/// Las salidas de audio del equipo: (id estable, nombre).
std::vector<std::pair<std::string, std::string>> salidasDeAudio() {
    std::vector<std::pair<std::string, std::string>> v;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        // Con el nombre que le puso el operador (Dispositivos y equipos), si le puso.
        v.emplace_back(QString::fromLatin1(d.id()).toStdString(),
                       dispositivos::Conocidos::nombreParaMostrar("local:salida:" + QString::fromLatin1(d.id()).toStdString(),
                                                                  d.description().toStdString()));
    return v;
}
} // namespace
using json = nlohmann::json;

// --- Servicios del hilo principal con Qt ----------------------------------------------

class QtHostServices : public HostServices {
   public:
    explicit QtHostServices(QObject* owner) : owner_(owner) {}
    ~QtHostServices() override {
        for (auto& [id, t] : timers_)
            delete t;
        for (auto& [fd, ns] : fds_)
            for (auto* n : ns)
                delete n;
    }
    void runOnMain(std::function<void()> fn) override {
        QMetaObject::invokeMethod(owner_, [fn]() { fn(); }, Qt::QueuedConnection);
    }
    // Los timers viven en el hilo principal aunque se pidan desde otro: un
    // rack puede crearse en otro hilo, y un
    // QTimer nacido ahí (sin bucle de eventos) nunca dispara; la interfaz de
    // LSP, que se dibuja y atiende el mouse en su timer, quedaba congelada.
    int addTimer(int periodMs, std::function<void()> fn) override {
        const int id = ++next_;
        enMain([this, id, periodMs, fn]() {
            auto* t = new QTimer();
            t->setInterval(periodMs);
            QObject::connect(t, &QTimer::timeout, owner_, [fn]() { fn(); });
            t->start();
            timers_[id] = t;
        });
        return id;
    }
    void removeTimer(int id) override {
        enMain([this, id]() {
            auto it = timers_.find(id);
            if (it == timers_.end()) return;
            it->second->stop();
            it->second->deleteLater();
            timers_.erase(it);
        });
    }
    bool addFd(int fd, uint32_t flags, std::function<void(uint32_t)> fn) override {
#ifdef _WIN32
        (void)fd;
        (void)flags;
        (void)fn;
        return false;
#else
        removeFd(fd);
        // Como los timers: los QSocketNotifier, en el hilo principal.
        enMain([this, fd, flags, fn]() {
            auto& list = fds_[fd];
            if (flags & 1u) {
                auto* n = new QSocketNotifier(fd, QSocketNotifier::Read);
                QObject::connect(n, &QSocketNotifier::activated, owner_, [fn]() { fn(1u); });
                list.push_back(n);
            }
            if (flags & 2u) {
                auto* n = new QSocketNotifier(fd, QSocketNotifier::Write);
                QObject::connect(n, &QSocketNotifier::activated, owner_, [fn]() { fn(2u); });
                list.push_back(n);
            }
        });
        return true;
#endif
    }
    void removeFd(int fd) override {
        enMain([this, fd]() {
            auto it = fds_.find(fd);
            if (it == fds_.end()) return;
            for (auto* n : it->second) {
                n->setEnabled(false);
                n->deleteLater();
            }
            fds_.erase(it);
        });
    }

    void stateDirty() override;
    void latencyChanged() override {
        QMetaObject::invokeMethod(owner_, []() { AudioRack::rehacerTodos(); }, Qt::QueuedConnection);
    }

   private:
    /// Ya, si es el hilo principal; si no, encolado en él (en orden).
    void enMain(std::function<void()> fn) {
        if (QThread::currentThread() == owner_->thread())
            fn();
        else
            QMetaObject::invokeMethod(owner_, std::move(fn), Qt::QueuedConnection);
    }
    QObject* owner_;
    std::atomic<int> next_{0};
    std::map<int, QTimer*> timers_;
    std::map<int, std::vector<QSocketNotifier*>> fds_;
};

// --- El rack de una fuente ------------------------------------------------------------

namespace {
RackManager* g_mgr = nullptr;
} // namespace

void QtHostServices::stateDirty() {
    QMetaObject::invokeMethod(
        owner_,
        []() {
            if (g_mgr != nullptr) g_mgr->markDirty();
        },
        Qt::QueuedConnection);
}

struct RackManager::Filter : gstrack::Anfitrion {
    /// Lo procesa el elemento `barack` de la cadena de audio de la entrada.
    bool gst = true;
    std::string source;
    std::unique_ptr<AudioRack> rack;
    /// Escucha: lo que junta el nodo «salida» va a este anillo, que un
    /// QAudioSink saca por los auriculares del operador (nunca a la mezcla).
    std::shared_ptr<anillo::Ring> escucha;
    std::unique_ptr<anillo::AlDispositivo> escuchaIo;
    std::unique_ptr<QAudioSink> escuchaSink;
    // Grabación de la tarjeta de salida: el hilo de audio toma una copia del
    // puntero bajo este mutex (muy corto) y le agrega lo que llegó.
    std::mutex grabMu;
    std::shared_ptr<GrabadorWav> grab;

    /// Hilo de audio (el de GStreamer): el rack, la escucha, la
    /// grabación de la tarjeta de salida y la voz a texto.
    void procesarCon(float* const* planos, int canales, uint32_t frames, uint32_t sr, uint64_t ts);
    void procesar(float* const* planos, int canales, uint32_t frames) override {
        procesarCon(planos, canales, frames, 48000, ba::app::ahoraNs());
    }
};

namespace {
std::mutex g_textoLock;
RackManager::TextoSink g_texto;
} // namespace

void RackManager::Filter::procesarCon(float* const* planos, int canales, uint32_t frames, uint32_t sr, uint64_t ts) {
    rack->process(planos, canales, frames);
    (void)ts;
    // Escucha: lo que juntó el nodo "monitor" va a su anillo, que suena por la
    // salida elegida (no vuelve a la mezcla).
    const uint32_t mf = rack->monitorFrames();
    if (mf > 0)
        if (std::shared_ptr<anillo::Ring> e = std::atomic_load(&escucha)) e->push(rack->monitorPlane(0), rack->monitorPlane(1), mf);
    if (mf > 0) {
        std::shared_ptr<GrabadorWav> g;
        {
            std::lock_guard<std::mutex> l(grabMu);
            g = grab;
        }
        if (g) g->agregar(rack->monitorPlane(0), rack->monitorPlane(1), mf);
    }
    // Voz a texto: lo que juntó cada nodo va al reconocedor (copia y vuelve).
    std::lock_guard<std::mutex> l(g_textoLock);
    if (g_texto)
        rack->textosCapturados([sr](const std::string& flujo, int modo, const float* m, uint32_t n) {
            g_texto(flujo, modo, m, n, sr);
        });
}

RackManager::RackManager(fs::path dataRoot, QObject* parent) : QObject(parent), root_(std::move(dataRoot)) {
    g_mgr = this;
    // Viven lo que el proceso: el último rack (y sus plugins, que devuelven
    // sus timers) puede soltarse después que el manager.
    static QtHostServices* services = new QtHostServices(QCoreApplication::instance());
    services_ = services;
    // `barack`: el rack en la cadena de audio de las entradas de GStreamer.
    gstrack::registrarEnSegundoPlano();
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(800);
    connect(&saveTimer_, &QTimer::timeout, this, &RackManager::saveNow);
    revision_.setInterval(2000);
    connect(&revision_, &QTimer::timeout, this, [this]() {
        revisarRacksGst();
        montarPendientes();
        sincronizarGrabaciones();
        normalizarFlujos();
        sincronizarEntradas();
    });
    revision_.start();
}

RackManager::~RackManager() {
    saveNow();
    revision_.stop();
    tomas_.clear(); // cada Toma suelta la del motor
    // detach saca de filters_: se recorre una copia.
    std::vector<Filter*> todos;
    for (auto& [name, f] : filters_) todos.push_back(f);
    for (Filter* f : todos) detach(f);
    filters_.clear();
    for (auto& [name, f] : gstFilters_)
        gstrack::retirar(name);
    gstFilters_.clear();
    g_mgr = nullptr;
}

HostServices* RackManager::services() const { return services_; }

double RackManager::sampleRate() const { return 48000.0; }

std::shared_ptr<ClapLibrary> RackManager::library(std::string* error) {
    if (!libTried_) {
        libTried_ = true;
        std::vector<fs::path> candidates;
        if (const char* env = std::getenv("BA_LSP_CLAP")) candidates.emplace_back(env);
        // Viene con el programa: del bundle; y de la raíz de datos (copia sombra, layout viejo).
        candidates.push_back(ba::platform::bundleRoot() / "plugins" / "lsp" / "lsp-plugins.clap");
        candidates.push_back(root_ / "plugins" / "lsp" / "lsp-plugins.clap");
#ifdef BA_DEV_LSP_CLAP
        candidates.emplace_back(BA_DEV_LSP_CLAP); // arbol de build (desarrollo)
#endif
        for (const auto& p : candidates) {
            std::error_code ec;
            if (!fs::exists(p, ec)) continue;
            lib_ = ClapLibrary::open(p.string(), &libError_);
            if (lib_) break;
        }
        if (!lib_ && libError_.empty()) libError_ = "no estan los plugins LSP (plugins/lsp/lsp-plugins.clap)";
        // Los racks que se crearon antes (sin biblioteca) no tienen modulos: da igual.
    }
    if (!lib_ && error != nullptr) *error = libError_;
    return lib_;
}

void RackManager::attach(Filter* f) {
    filters_[f->source] = f;
    syncMonitors();
}

void RackManager::detach(Filter* f) {
    // Su fuente se va (la borraron, o la app se está cerrando y las entradas
    // virtuales se destruyen antes del último guardado): el rack queda como
    // pendiente, para volver cuando la fuente reaparezca. Antes se perdía.
    if (f->rack && filters_.contains(f->source) && filters_[f->source] == f) {
        if (!pending_.is_object()) pending_ = json::object();
        pending_[f->source] = f->rack->save();
    }
    {
        std::shared_ptr<GrabadorWav> g;
        {
            std::lock_guard<std::mutex> l(f->grabMu);
            g.swap(f->grab);
        }
        if (g) g->terminar();
    }
    for (auto it = filters_.begin(); it != filters_.end(); ++it)
        if (it->second == f) {
            filters_.erase(it);
            break;
        }
    // La escucha se cierra (el hilo de audio ya no le escribe).
    std::atomic_store(&f->escucha, std::shared_ptr<anillo::Ring>());
    if (f->escuchaSink) f->escuchaSink->stop();
    f->escuchaSink.reset();
    f->escuchaIo.reset();
}

// --- Salidas de audio del equipo ------------------------------------------------

json RackManager::outputDevices() const {
    struct Item {
        std::string id, name;
    };
    std::vector<Item> items;
    for (const auto& [id, nombre] : salidasDeAudio()) items.push_back({id, nombre});
    std::vector<std::string> order;
    std::map<std::string, json> byGroup;
    std::map<std::string, std::string> label;
    for (const auto& it : items) {
        // Por id, no por nombre: renombrar una salida no la saca de su placa.
        const std::string g = grupoDeSalida(it.id);
        if (byGroup.find(g) == byGroup.end()) {
            order.push_back(g);
            byGroup[g] = json::array();
            label[g] = it.name; // nombre de la primera salida de esa placa
        }
        byGroup[g].push_back({{"id", it.id}, {"nombre", it.name}});
    }
    json out = json::array();
    for (const auto& g : order)
        out.push_back({{"dispositivo", label[g]}, {"clave", g}, {"salidas", byGroup[g]}});
    return out;
}

std::string RackManager::setOutput(const std::string& source, const std::string& device) {
    auto it = filters_.find(source);
    if (it == filters_.end()) return "esa fuente no tiene rack";
    std::string nombre;
    bool hay = false;
    for (const auto& [id, n] : salidasDeAudio())
        if (id == device) {
            nombre = n;
            hay = true;
        }
    if (!hay) return "esa salida de audio ya no esta";
    monitorId_ = device;
    monitorName_ = nombre;
    if (BroadcastEngine* e = motorApp(); e != nullptr && e->motor() != nullptr) e->motor()->escuchaDispositivo(device);
    it->second->rack->addOutput(device, nombre);
    syncMonitors();
    saveMonitor();
    markDirty();
    emit changed();
    return {};
}

std::string RackManager::setAuriculares(const std::string& device) {
    std::string nombre;
    if (!device.empty()) {
        bool hay = false;
        for (const auto& [id, n] : salidasDeAudio())
            if (id == device) {
                nombre = n;
                hay = true;
            }
        if (!hay) return "esa salida de audio ya no esta";
    }
    monitorId_ = device;
    monitorName_ = nombre;
    // La escucha del motor (PFL, voces) sale por el mismo lado.
    if (BroadcastEngine* e = motorApp(); e != nullptr && e->motor() != nullptr) e->motor()->escuchaDispositivo(device);
    syncMonitors();
    saveMonitor();
    emit changed();
    return {};
}

void RackManager::syncMonitors() {
    // Suena el rack que tiene nodo de salida (y un dispositivo elegido): un
    // QAudioSink por rack hacia esa salida (el sistema los suma).
    QAudioDevice dev;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        if (QString::fromLatin1(d.id()).toStdString() == monitorId_) dev = d;
    for (auto& [name, f] : filters_) {
        const bool quiero = !dev.isNull() && f->rack && f->rack->hasOutput();
        const bool tiene = static_cast<bool>(f->escuchaSink);
        if (quiero == tiene && (!tiene || dispositivoEscucha_ == monitorId_)) continue;
        std::atomic_store(&f->escucha, std::shared_ptr<anillo::Ring>());
        if (f->escuchaSink) f->escuchaSink->stop();
        f->escuchaSink.reset();
        f->escuchaIo.reset();
        if (!quiero) continue;
        QAudioFormat fmt;
        fmt.setSampleRate(48000);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Float);
        if (!dev.isFormatSupported(fmt)) continue;
        auto ring = std::make_shared<anillo::Ring>();
        f->escuchaIo = std::make_unique<anillo::AlDispositivo>(ring);
        f->escuchaSink = std::make_unique<QAudioSink>(dev, fmt);
        f->escuchaSink->setBufferSize(static_cast<qsizetype>(anillo::kColchon * 2 * sizeof(float) * 2));
        f->escuchaSink->start(f->escuchaIo.get());
        std::atomic_store(&f->escucha, ring);
    }
    dispositivoEscucha_ = monitorId_;
}

void RackManager::saveMonitor() const {
    const fs::path file = root_ / "Config" / "audio-escucha.json";
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // Aparte y reemplazar: un corte a mitad no deja el archivo trunco.
    QSaveFile out(QString::fromStdString(file.string()));
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(QByteArray::fromStdString(
        json{{"id", monitorId_}, {"nombre", monitorName_}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
    out.commit();
}

void RackManager::restoreMonitor() {
    std::ifstream in(root_ / "Config" / "audio-escucha.json", std::ios::binary);
    if (!in) return;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    const std::string id = j.value("id", "");
    const std::string name = j.value("nombre", "");
    if (id.empty()) return;
    // El dispositivo puede no estar (se desenchufó): se queda sin escucha.
    for (const auto& [i, n] : salidasDeAudio())
        if (i == id) {
            monitorId_ = id;
            monitorName_ = name.empty() ? n : name;
        }
    if (!monitorId_.empty())
        if (BroadcastEngine* e = motorApp(); e != nullptr && e->motor() != nullptr) e->motor()->escuchaDispositivo(monitorId_);
    syncMonitors();
}

json RackManager::audioSources() const {
    json list = json::array();
    BroadcastEngine* e = motorApp();
    if (e == nullptr) return list;
    for (const std::string& name : e->audioSources()) {
        const std::string type = tipoLogicoDe(name);
        // Grupo para el arbol de la interfaz: por donde entra el audio.
        std::string group = "Otras fuentes";
        if (type == "ba_voz_asistente") {
            group = "Voces (texto a audio)"; // la del sistema, la del teleprompter…
        } else if (type == "ba_entrada_fisica") {
            group = "Entradas del equipo (placas)";
        } else if (type == "ba_entrada_virtual") {
            group = "Entradas virtuales (matriz)";
        } else if (type == "ba_guest_audio") {
            // Los remotos de la asamblea, aparte de los invitados de la conferencia.
            group = inputs::grupoDeGente(type, json::parse(e->sourceSettingsJson(name), nullptr, false));
        } else if (type.find("_input_capture") != std::string::npos) {
            group = "Micrófonos y líneas del equipo";
        } else if (type.find("_output_capture") != std::string::npos) {
            group = "Audio del equipo (lo que suena)";
        } else if (type == "ffmpeg_source" || type == "ba_gst_medio" || type == "vlc_source" || type == "ba_ndi_input") {
            group = "Medios y red";
        }
        json j{{"nombre", name}, {"tipo", type}, {"grupo", group}, {"rack", false}};
        auto it = filters_.find(name);
        if (it != filters_.end()) {
            j["rack"] = true;
            j["motor"] = "gstreamer"; // en la cadena de GStreamer de la entrada
            j["procesando"] = gstrack::procesando(name);
            int mods = 0;
            for (const auto& id : it->second->rack->nodeIds())
                if (it->second->rack->kindOf(id) == "plugin") ++mods;
            j["modulos"] = mods;
            j["salida"] = it->second->rack->meters("out");
        }
        list.push_back(std::move(j));
    }
    return list;
}

// --- Vista general (Estudio) ----------------------------------------------------

namespace {
/// De dónde viene el audio, en palabras (para la tarjeta de origen).
std::string describirOrigen(const std::string& tipo, const nlohmann::json& s) {
    if (tipo == "ba_gst_medio")
        return "Archivo · " + std::filesystem::path(s.is_object() ? s.value("local_file", std::string()) : "").filename().string() +
               (s.is_object() && s.value("looping", false) ? " · repite" : "");
    if (tipo != "ba_gst_fuente") return tipo;
    if (!s.is_object()) return "GStreamer";
    if (s.contains("microfono")) return "Micrófono · " + s.value("pipeline_audio", std::string());
    if (s.contains("anillo"))
        return std::string(s.value("anillo", std::string()) == "remoto" ? "Remoto con la palabra" : "Invitado") + " · " +
               s.value("pipeline_audio", std::string());
    if (!s.value("uri", std::string()).empty()) return "Red · " + inputs::redactUrl(s.value("uri", std::string()));
    if (s.contains("ba_plugin")) return "Plugin · " + s.value("ba_plugin", std::string());
    if (!s.value("pipeline_audio", std::string()).empty()) return "Pipeline · " + s.value("pipeline_audio", std::string());
    return "GStreamer · audio del pipeline";
}

nlohmann::json puertos(const nlohmann::json& nombres, const char* tipo = "audio") {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& n : nombres) out.push_back({{"id", n}, {"tipo", tipo}});
    return out;
}

/// Alto de una tarjeta del Patchbay (cabecera + puertos + cuerpo).
double altoTarjeta(const nlohmann::json& n) {
    const double filas = std::max<double>({1.0, static_cast<double>(n.value("entradas", nlohmann::json::array()).size()),
                                           static_cast<double>(n.value("salidas", nlohmann::json::array()).size())});
    return 34.0 + 24.0 * filas + 40.0;
}
} // namespace

json RackManager::estudio() const {
    constexpr double kPaso = 240.0;  // de una columna a la otra
    constexpr double kTitulo = 30.0; // lo que ocupa el nombre de la franja
    constexpr double kEntre = 24.0;  // entre franjas
    json nodos = json::array(), cables = json::array(), franjas = json::array();
    std::vector<std::pair<std::string, std::string>> entradas; // (tarjeta «entrada», fuente que trae)
    std::map<std::string, std::vector<std::string>> mesas;      // mesa → tarjetas baautomixer
    double y = 0;
    for (const auto& f : audioSources()) {
        const std::string nombre = f.value("nombre", std::string());
        BroadcastEngine* eng = motorApp();
        if (eng == nullptr || !eng->sourceExists(nombre)) continue;
        const std::string tipo = eng->sourceTypeId(nombre);
        const json ajustes = ajustesDe(nombre);
        const float vol = eng->sourceVolume(nombre);
        const bool mudo = eng->sourceMuted(nombre);

        const std::string p = nombre + "|";
        const std::string motor = "gstreamer"; // en el motor, toda entrada es de GStreamer
        const double y0 = y + kTitulo;
        double altoFranja = 0;
        const auto poner = [&](json n, double x, double dy) {
            n["x"] = x;
            n["y"] = y0 + dy;
            n["grupo"] = nombre;
            n["fuente"] = nombre;
            altoFranja = std::max(altoFranja, dy + altoTarjeta(n));
            nodos.push_back(std::move(n));
        };
        // El rack: se dibuja como está armado en su página, con su «in» donde va
        // el origen y su «out» donde empieza lo que sigue (filtros y mezcla).
        double origenY = 0, x = kPaso, etapasY = 0;
        std::vector<json> cablesRack;
        json rackNodos = json::array();
        auto it = filters_.find(nombre);
        if (it != filters_.end() && it->second->rack) {
            const json r = it->second->rack->describe();
            double minY = 1e9, inX = 0, inY = 0, outX = kPaso, outY = 0;
            for (const auto& n : r["nodos"]) {
                minY = std::min(minY, n.value("y", 0.0));
                if (n["id"] == "in") inX = n.value("x", 0.0), inY = n.value("y", 0.0);
                if (n["id"] == "out") outX = n.value("x", 0.0), outY = n.value("y", 0.0);
            }
            origenY = inY - minY;
            x = std::max(kPaso, outX - inX);
            etapasY = outY - minY;
            for (const auto& n : r["nodos"]) {
                const std::string id = n.value("id", std::string());
                if (id == "in" || id == "out") continue;
                const std::string k = n.value("tipo", std::string());
                json t{{"id", p + "r:" + id},
                       {"tipo", k},
                       {"etiqueta", n.value("etiqueta", id)},
                       {"entradas", puertos(n.value("entradas", json::array()))},
                       {"salidas", puertos(n.value("salidas", json::array()))},
                       {"rack", id}};
                std::string resumen = k == "plugin" ? n.value("nombre", std::string()) : "";
                if (n.value("bypass", false)) resumen += " · anulado";
                if (n.value("bypass_auto", false)) resumen += " · anulado por el vigía";
                if (k == "entrada") {
                    resumen = "de «" + n.value("fuente", std::string()) + "»";
                    t["entradas"].push_back({{"id", "desde"}, {"tipo", "audio"}});
                    entradas.push_back({p + "r:" + id, n.value("fuente", std::string())});
                }
                if (k == "texto") resumen = "voz a texto · " + n.value("modo", std::string());
                if (!resumen.empty()) t["resumen"] = resumen;
                t["@x"] = n.value("x", 0.0) - inX;
                t["@y"] = n.value("y", 0.0) - minY;
                rackNodos.push_back(t);
            }
            for (const auto& c : r["cables"]) {
                const std::string a = c.value("desde", std::string()), b = c.value("hacia", std::string());
                cablesRack.push_back({{"desde", a == "in" ? p + "origen" : p + "r:" + a},
                                      {"salida", a == "in" ? std::string("out") : c.value("salida", std::string())},
                                      {"hacia", b == "out" ? std::string("@sigue") : p + "r:" + b},
                                      {"entrada", b == "out" ? std::string("in") : c.value("entrada", std::string())}});
            }
        }
        poner({{"id", p + "origen"},
               {"tipo", motor == "gstreamer" ? "origen-gst" : "origen-obs"},
               {"etiqueta", nombre},
               {"resumen", describirOrigen(tipo, ajustes)},
               {"motor", motor},
               {"entradas", json::array()},
               {"salidas", puertos(json::array({"out"}))}},
              0, origenY);
        for (auto& t : rackNodos) {
            const double tx = t["@x"], ty = t["@y"];
            t.erase("@x");
            t.erase("@y");
            poner(t, tx, ty);
        }

        // Los filtros de su cadena de GStreamer (después del rack) y la mezcla.
        std::vector<std::string> etapas;
        const json fa = json::parse(ajustes.is_object() ? ajustes.value("filtros_audio", std::string()) : std::string(),
                                    nullptr, false);
        int i = 0;
        for (const auto& fl : fa.is_array() ? fa : json::array()) {
            const std::string el = fl.value("elemento", std::string());
            const json pr = fl.value("propiedades", json::object());
            const std::string id = p + "f:" + std::to_string(i++);
            json t{{"id", id},
                   {"tipo", el == "baautomixer" ? "automixer" : "gst"},
                   {"etiqueta", el == "baautomixer" ? std::string("Automixer") : el},
                   {"entradas", puertos(json::array({"in"}))},
                   {"salidas", puertos(json::array({"out"}))}};
            if (el == "baautomixer") {
                const std::string g = pr.value("grupo", std::string("mesa"));
                t["resumen"] = "mesa «" + g + "»";
                t["entradas"].push_back({{"id", "mesa"}, {"tipo", "control"}});
                mesas[g].push_back(id);
            } else if (el == "baplugin") {
                t["resumen"] = "Python · " + pr.value("plugin", std::string());
            }
            etapas.push_back(id);
            poner(t, x, etapasY);
            x += kPaso;
        }
        char vtxt[64];
        std::snprintf(vtxt, sizeof vtxt, "%.1f dB", vol > 0.0f ? 20.0 * std::log10(vol) : -100.0);
        poner({{"id", p + "mezcla"},
               {"tipo", "mezcla"},
               {"etiqueta", "Mezcla"},
               {"resumen", std::string(vtxt) + (mudo ? " · silenciada" : "")},
               {"entradas", puertos(json::array({"in"}))},
               {"salidas", puertos(json::array({"out"}))}},
              x, etapasY);
        etapas.push_back(p + "mezcla");

        // Cables: el «out» del rack va a la primera etapa; sin rack, el origen va
        // directo; y las etapas en fila.
        for (auto& c : cablesRack) {
            if (c["hacia"] == "@sigue") c["hacia"] = etapas.front();
            cables.push_back(c);
        }
        if (cablesRack.empty())
            cables.push_back({{"desde", p + "origen"}, {"salida", "out"}, {"hacia", etapas.front()}, {"entrada", "in"}});
        for (size_t k = 1; k < etapas.size(); ++k)
            cables.push_back({{"desde", etapas[k - 1]}, {"salida", "out"}, {"hacia", etapas[k]}, {"entrada", "in"}});

        franjas.push_back({{"titulo", nombre + "  ·  GStreamer"},
                           {"y", y},
                           {"alto", kTitulo + altoFranja + 10},
                           {"motor", motor}});
        y += kTitulo + altoFranja + kEntre;
    }
    // Las tarjetas «Entrada»: un cable desde lo que sale de la otra fuente.
    for (const auto& [tarjeta, fuente] : entradas)
        cables.push_back({{"desde", fuente + "|mezcla"}, {"salida", "out"}, {"hacia", tarjeta}, {"entrada", "desde"}});
    // Las mesas del automixer: una tarjeta por mesa, con un cable de control a cada micrófono.
    if (!mesas.empty()) {
        const double y0 = y + kTitulo;
        double alto = 0, x = 0;
        for (const auto& [g, ids] : mesas) {
            json t{{"id", "mesa|" + g},
                   {"tipo", "mesa"},
                   {"etiqueta", "Mesa «" + g + "»"},
                   {"resumen", std::to_string(ids.size()) + " micrófonos"},
                   {"entradas", json::array()},
                   {"salidas", json::array({json{{"id", "ctl"}, {"tipo", "control"}}})},
                   {"x", x},
                   {"y", y0},
                   {"grupo", "Automixer"}};
            alto = std::max(alto, altoTarjeta(t));
            nodos.push_back(t);
            for (const auto& id : ids)
                cables.push_back({{"desde", "mesa|" + g}, {"salida", "ctl"}, {"hacia", id}, {"entrada", "mesa"}});
            x += kPaso;
        }
        franjas.push_back({{"titulo", "Automixer"}, {"y", y}, {"alto", kTitulo + alto + 10}, {"motor", "gstreamer"}});
    }
    return json{{"nodos", nodos}, {"cables", cables}, {"franjas", franjas}};
}

void RackManager::setTextoSink(TextoSink sink) {
    std::lock_guard<std::mutex> l(g_textoLock);
    g_texto = std::move(sink);
}

std::string RackManager::setTexto(const std::string& source, const std::string& modo) {
    std::string e;
    AudioRack* r = rack(source, true, &e);
    if (r == nullptr) return e;
    const auto ts = r->textos();
    if (!ts.empty()) {
        r->setTextoModo(ts.front().id, modo);
    } else if (addTexto(source, modo, {}, &e).empty()) {
        return e;
    }
    markDirty();
    emit changed();
    return {};
}

bool RackManager::flujoLibre(const std::string& flujo, const std::string& salvoRack,
                             const std::string& salvoNodo) const {
    for (const auto& [nombre, f] : filters_)
        if (f->rack)
            for (const auto& t : f->rack->textos())
                if (t.flujo == flujo && !(nombre == salvoRack && t.id == salvoNodo)) return false;
    return true;
}

std::string RackManager::addTexto(const std::string& source, const std::string& modo,
                                  const std::string& flujo, std::string* error) {
    // El nombre, antes de crear el rack de la fuente (con el nombre ocupado, quedaba un
    // rack nuevo colgado de la fuente, y guardado).
    std::string nombre = flujo;
    if (nombre.empty()) {
        nombre = source;
        for (int i = 2; !flujoLibre(nombre, {}, {}); ++i)
            nombre = source + " " + std::to_string(i);
    } else if (!flujoLibre(nombre, {}, {})) {
        if (error != nullptr) *error = "ya hay una salida de texto «" + nombre + "»";
        return {};
    }
    AudioRack* r = rack(source, true, error);
    if (r == nullptr) return {};
    const std::string id = r->addTexto(modo, nombre);
    markDirty();
    emit changed();
    return id;
}

std::string RackManager::setTextoOpciones(const std::string& source, const std::string& nodo,
                                          const json& cambios) {
    std::string e;
    AudioRack* r = rack(source, false, &e);
    if (r == nullptr) return e;
    if (r->kindOf(nodo) != "texto") return "'" + nodo + "' no es una voz a texto";
    if (cambios.contains("flujo")) {
        const std::string f = cambios.value("flujo", "");
        if (f.empty()) return "la salida de texto necesita un nombre";
        if (!flujoLibre(f, source, nodo)) return "ya hay una salida de texto «" + f + "»";
        r->setTextoFlujo(nodo, f);
    }
    if (cambios.contains("modo")) r->setTextoModo(nodo, cambios.value("modo", "dictado"));
    if (cambios.contains("salidas")) {
        json actuales = json::object();
        for (const auto& t : r->textos())
            if (t.id == nodo) actuales = t.salidas.is_object() ? t.salidas : json::object();
        for (auto it = cambios["salidas"].begin(); it != cambios["salidas"].end(); ++it)
            actuales[it.key()] = it.value();
        r->setTextoSalidas(nodo, actuales);
    }
    markDirty();
    emit changed();
    return {};
}

void RackManager::quitarTextos(const std::string& source) {
    AudioRack* r = rack(source, false);
    if (r == nullptr) return;
    std::string e;
    for (const auto& t : r->textos())
        r->remove(t.id, &e);
    markDirty();
    emit changed();
}

RackManager::json RackManager::textos() const {
    json out = json::array();
    for (const auto& [nombre, f] : filters_)
        if (f != nullptr && f->rack)
            for (const auto& t : f->rack->textos())
                out.push_back({{"flujo", t.flujo},
                               {"fuente", nombre},
                               {"nodo", t.id},
                               {"modo", t.modo},
                               {"salidas", t.salidas.is_object() ? t.salidas : json::object()}});
    return out;
}

RackManager::json RackManager::textoDe(const std::string& flujo) const {
    for (const auto& t : textos())
        if (t.value("flujo", "") == flujo) return t;
    return nullptr;
}

RackManager::json RackManager::fuentesConTexto() const {
    json out = json::array();
    for (const auto& t : textos())
        out.push_back({{"fuente", t["fuente"]}, {"modo", t["modo"]}, {"flujo", t["flujo"]}});
    return out;
}

void RackManager::normalizarFlujos() {
    bool algo = false;
    for (const auto& [nombre, f] : filters_) {
        if (!f->rack) continue;
        for (const auto& t : f->rack->textos()) {
            if (!t.flujo.empty() && flujoLibre(t.flujo, nombre, t.id)) continue;
            std::string n = t.flujo.empty() ? nombre : t.flujo;
            for (int i = 2; !flujoLibre(n, nombre, t.id); ++i)
                n = (t.flujo.empty() ? nombre : t.flujo) + " " + std::to_string(i);
            f->rack->setTextoFlujo(t.id, n);
            algo = true;
        }
    }
    if (algo) {
        saveTimer_.start();
        emit changed();
    }
}

std::vector<std::string> RackManager::tomarSaltados() {
    std::vector<std::string> out;
    for (auto& [fuente, f] : filters_)
        if (f != nullptr && f->rack)
            for (const auto& m : f->rack->tomarSaltados()) out.push_back(fuente + ": " + m);
    return out;
}

AudioRack* RackManager::rack(const std::string& source, bool create, std::string* error) {
    auto it = filters_.find(source);
    if (it != filters_.end()) return it->second->rack.get();
    if (!existe(source)) {
        if (error != nullptr) *error = "no existe la fuente '" + source + "'";
        return nullptr;
    }
    // El rack guardado de la fuente o, si no tiene, la plantilla marcada para
    // su tipo: es lo que hace que un invitado que se conecta ya suene bien sin
    // que nadie le arme nada.
    const auto cargar = [&](AudioRack& r) {
        std::string e;
        if (pending_.contains(source)) {
            r.load(pending_[source], &e);
        } else {
            const std::string plantilla = plantillaDeTipo(tipoLogicoDe(source));
            if (!plantilla.empty()) r.load(plantillas_[plantilla].value("rack", json::object()), &e);
        }
    };
    if (!conAudio(source)) {
        if (error != nullptr) *error = "'" + source + "' no tiene audio";
        return nullptr;
    }
    if (!create) {
        if (error != nullptr) *error = "'" + source + "' no tiene rack";
        return nullptr;
    }
    // El rack corre en la cadena de audio de la entrada (`barack`).
    std::shared_ptr<Filter> f(new Filter(), [](Filter* x) {
        // Los plugins se destruyen en el hilo principal (lo pide CLAP); la
        // última referencia puede soltarla el hilo de audio de GStreamer.
        if (QCoreApplication::instance() != nullptr &&
            QThread::currentThread() != QCoreApplication::instance()->thread())
            QMetaObject::invokeMethod(QCoreApplication::instance(), [x]() { delete x; }, Qt::QueuedConnection);
        else
            delete x;
    });
    f->source = source;
    f->rack = std::make_unique<AudioRack>(library(), services_, 48000.0);
    cargar(*f->rack);
    gstFilters_[source] = f;
    attach(f.get());
    gstrack::publicar(source, f);
    ponerRackEnFuente(source, source);
    markDirty();
    emit changed();
    return f->rack.get();
}

bool RackManager::removeRack(const std::string& source) {
    auto it = filters_.find(source);
    if (it == filters_.end()) return false;
    Filter* f = it->second;
    filters_.erase(it);
    pending_.erase(source);
    gstrack::retirar(source);
    detach(f); // escucha y grabación (ya no está en filters_: no queda pendiente)
    if (existe(source)) ponerRackEnFuente(source, "");
    gstFilters_.erase(source);
    markDirty();
    emit changed();
    return true;
}

// --- Plantillas -------------------------------------------------------------

namespace {
constexpr const char* kPlantillasFile = "audio-plantillas.json";
}

void RackManager::cargarPlantillas() {
    std::ifstream in(root_ / "Config" / kPlantillasFile, std::ios::binary);
    if (!in) return;
    const json j = json::parse(in, nullptr, false);
    // Un archivo roto no tiene por que impedir que el audio ande: se ignora y
    // el operador se entera cuando la lista aparece vacia.
    if (j.is_object()) plantillas_ = j;
}

void RackManager::guardarPlantillas() const {
    const fs::path file = root_ / "Config" / kPlantillasFile;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return;
        out << plantillas_.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    fs::rename(tmp, file, ec);
}

json RackManager::plantillas() const {
    json out = json::array();
    for (auto it = plantillas_.begin(); it != plantillas_.end(); ++it) {
        const json& p = it.value();
        const json& r = p.value("rack", json::object());
        out.push_back(json{{"nombre", it.key()},
                           {"modulos", r.contains("nodos") ? r["nodos"].size() : 0},
                           {"paraTipo", p.value("paraTipo", std::string())}});
    }
    return out;
}

std::string RackManager::guardarPlantilla(const std::string& source, const std::string& nombre) {
    if (nombre.empty()) return "la plantilla necesita un nombre";
    AudioRack* r = rack(source, false);
    if (r == nullptr) return "'" + source + "' no tiene rack que guardar";
    // Se conserva el tipo predeterminado si la plantilla ya existia: guardar
    // encima es actualizarla, no volverla a empezar.
    const std::string tipo =
        plantillas_.contains(nombre) ? plantillas_[nombre].value("paraTipo", std::string()) : std::string();
    plantillas_[nombre] = json{{"rack", r->save()}, {"paraTipo", tipo}};
    guardarPlantillas();
    emit changed();
    return {};
}

std::string RackManager::aplicarPlantilla(const std::string& nombre, const std::string& source) {
    if (!plantillas_.contains(nombre)) return "no existe la plantilla '" + nombre + "'";
    std::string err;
    AudioRack* r = rack(source, true, &err);
    if (r == nullptr) return err.empty() ? "no se pudo montar el rack" : err;
    if (!r->load(plantillas_[nombre].value("rack", json::object()), &err))
        return "la plantilla no se pudo aplicar: " + err;
    markDirty();
    emit changed();
    return {};
}

std::string RackManager::borrarPlantilla(const std::string& nombre) {
    if (!plantillas_.contains(nombre)) return "no existe la plantilla '" + nombre + "'";
    plantillas_.erase(nombre);
    guardarPlantillas();
    emit changed();
    return {};
}

std::string RackManager::plantillaPorDefecto(const std::string& nombre, const std::string& tipo) {
    if (!plantillas_.contains(nombre)) return "no existe la plantilla '" + nombre + "'";
    // Una sola por tipo: si no, cual toma una fuente nueva seria una moneda.
    if (!tipo.empty()) {
        for (auto it = plantillas_.begin(); it != plantillas_.end(); ++it)
            if (it.key() != nombre && it.value().value("paraTipo", std::string()) == tipo)
                it.value()["paraTipo"] = "";
    }
    plantillas_[nombre]["paraTipo"] = tipo;
    guardarPlantillas();
    emit changed();
    return {};
}

std::string RackManager::plantillaDeTipo(const std::string& tipo) const {
    if (tipo.empty()) return {};
    for (auto it = plantillas_.begin(); it != plantillas_.end(); ++it)
        if (it.value().value("paraTipo", std::string()) == tipo) return it.key();
    return {};
}

void RackManager::markDirty() {
    saveTimer_.start();
    sincronizarGrabaciones();
    normalizarFlujos();
    sincronizarEntradas();
    emit editado();
}

void RackManager::saveNow() {
    json all = pending_.is_object() ? pending_ : json::object();
    for (const auto& [name, f] : filters_)
        all[name] = f->rack->save();
    const fs::path file = root_ / "Config" / "audio-racks.json";
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) return;
        out << all.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    fs::rename(tmp, file, ec);
}

std::string RackManager::restablecerInterfazLsp(bool* ok) {
    if (ok != nullptr) *ok = false;
    // Primero las ventanas: LSP guarda su configuración al tocarla, y una
    // abierta podría volver a escribir lo que se restablece.
    int cerradas = 0;
    for (auto& [src, f] : filters_)
        if (f != nullptr && f->rack) cerradas += f->rack->cerrarEditores();

            // Donde la busca LSP (lsp-runtime-lib, system::get_user_config_path).
#ifdef _WIN32
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty()) base = QDir::homePath() + "/.config";
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) base = QDir::homePath() + "/.config";
#endif
    const QString ruta = base + "/lsp-plugins/lsp-plugins.cfg";
    QFile f(ruta);
    if (!f.exists()) {
        if (ok != nullptr) *ok = true;
        return cerradas > 0
                   ? "Ventanas LSP cerradas; LSP todavía no tenía configuración propia (ya está de fábrica)."
                   : "LSP todavía no tenía configuración propia: ya está de fábrica.";
    }
    if (!f.open(QIODevice::ReadOnly)) return "No se pudo leer " + ruta.toStdString();
    const QString antes = QString::fromUtf8(f.readAll());
    f.close();

    // Lo que hace que una ventana no entre en la pantalla (o no se vea):
    static const std::vector<std::pair<QRegularExpression, QString>> reglas = {
        {QRegularExpression("^ui_scaling\\s*=.*$", QRegularExpression::MultilineOption),
         "ui_scaling = 100.00000"},
        {QRegularExpression("^ui_scaling_host\\s*=.*$", QRegularExpression::MultilineOption),
         "ui_scaling_host = true"},
        {QRegularExpression("^font_scaling\\s*=.*$", QRegularExpression::MultilineOption),
         "font_scaling = 100.00000"},
        // El zoom por plugin («<plugin>_ui_scaling»): 0 = usar el general.
        {QRegularExpression("^(\\w+)_ui_scaling\\s*=.*$", QRegularExpression::MultilineOption),
         "\\1_ui_scaling = 0.00000"},
    };
    QString despues = antes;
    for (const auto& [re, valor] : reglas)
        despues.replace(re, valor);
    if (despues == antes) {
        if (ok != nullptr) *ok = true;
        return std::string(cerradas > 0 ? "Ventanas LSP cerradas. " : "") +
               "La configuración de LSP ya estaba de fábrica (zoom 100 %, escala del host).";
    }

    // Una copia, por si había algo que valía la pena.
    const QString copia = ruta + ".respaldo-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    if (!QFile::copy(ruta, copia)) return "No se pudo hacer la copia de " + ruta.toStdString();
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return "No se pudo escribir " + ruta.toStdString();
    f.write(despues.toUtf8());
    f.close();
    if (ok != nullptr) *ok = true;
    return std::string(cerradas > 0 ? "Ventanas LSP cerradas. " : "") +
           "Interfaz LSP restablecida: zoom 100 %, escala del host, letra 100 %. Copia del archivo "
           "anterior: " +
           copia.toStdString();
}

void RackManager::restore() {
    cargarPlantillas();
    restoreMonitor();
    std::ifstream in(root_ / "Config" / "audio-racks.json", std::ios::binary);
    if (!in) return;
    const json all = json::parse(in, nullptr, false);
    if (!all.is_object()) return;
    pending_ = all;
    for (auto it = all.begin(); it != all.end(); ++it)
        if (existe(it.key())) rack(it.key(), true);
}

// --- Tarjetas de entrada (el audio de otra fuente) ---------------------------------

struct RackManager::Toma {
    std::string fuente;
    int toma = 0; ///< la del motor (escucharEntrada)
    std::shared_ptr<anillo::Ring> ring = std::make_shared<anillo::Ring>();
    ~Toma() {
        BroadcastEngine* e = motorApp();
        if (toma != 0 && e != nullptr && e->motor() != nullptr) e->motor()->dejarDeEscucharEntrada(toma);
    }
};

std::string RackManager::validarEntrada(const std::string& source, const std::string& fuente) const {
    if (fuente.empty()) return "falta de qué fuente traer el audio";
    if (fuente == source) return "una fuente no puede traer su propio audio (sería un lazo)";
    if (!existe(fuente)) return "no existe la fuente '" + fuente + "'";
    if (!conAudio(fuente)) return "'" + fuente + "' no tiene audio";
    // Lazo entre racks: si `fuente` (o lo que trae su rack, y así) ya trae el
    // audio de `source`, el sonido daría vueltas (con eco, por la demora).
    std::set<std::string> vistas;
    std::vector<std::string> pendientes{fuente};
    while (!pendientes.empty()) {
        const std::string f = pendientes.back();
        pendientes.pop_back();
        if (!vistas.insert(f).second) continue;
        auto it = filters_.find(f);
        if (it == filters_.end() || !it->second->rack) continue;
        for (const auto& [nodo, origen] : it->second->rack->entradas()) {
            if (origen == source)
                return "'" + fuente + "' ya trae el audio de '" + source +
                       "' (directa o indirectamente): sería un lazo";
            pendientes.push_back(origen);
        }
    }
    return {};
}

std::string RackManager::addEntrada(const std::string& source, const std::string& fuente,
                                    std::string* error) {
    const std::string e = validarEntrada(source, fuente);
    if (!e.empty()) {
        if (error != nullptr) *error = e;
        return {};
    }
    AudioRack* r = rack(source, true, error);
    if (r == nullptr) return {};
    const std::string id = r->addEntrada(fuente);
    markDirty();
    emit changed();
    return id;
}

namespace {
struct AnalisisLsp {
    const char* id;
    const char* plugin;
    const char* nombre;
    const char* para;
};
// Los plugins de medición de LSP (vienen en el bundle entero: tools/deps/lsp-*.sh).
constexpr AnalisisLsp kAnalisisLsp[] = {
    {"espectro", "in.lsp-plug.spectrum_analyzer_x2", "Spectrum Analyzer",
     "espectro en vivo (FFT) de L y R, con retención y espectrograma"},
    {"fase", "in.lsp-plug.phase_detector", "Phase Detector", "fase y retardo entre L y R (dos micrófonos, polaridad)"},
    {"osciloscopio", "in.lsp-plug.oscilloscope_x2", "Oscilloscope", "la forma de onda de L y R (recortes, DC, XY)"},
    {"latencia", "in.lsp-plug.latency_meter", "Latency Meter", "la latencia de ida y vuelta de un camino de audio"},
    {"perfil", "in.lsp-plug.profiler_stereo", "Profiler",
     "respuesta impulsiva, respuesta en frecuencia y RT60 con un barrido (suena)"},
};
} // namespace

json RackManager::analisisLsp() {
    json out = json::array();
    for (const auto& a : kAnalisisLsp) out.push_back({{"id", a.id}, {"nombre", a.nombre}, {"para", a.para}});
    return out;
}

std::string RackManager::abrirAnalisisLsp(const std::string& source, const std::string& cual) {
    const AnalisisLsp* a = nullptr;
    for (const auto& x : kAnalisisLsp)
        if (cual == x.id) a = &x;
    if (a == nullptr) return "no conozco el análisis «" + cual + "» (espectro, fase, osciloscopio, latencia, perfil)";
    std::string e;
    if (library(&e) == nullptr) return e.empty() ? "no están los plugins LSP" : e;
    AudioRack* r = rack(source, true, &e);
    if (r == nullptr) return e.empty() ? "esa fuente no tiene audio" : e;
    std::string nodo;
    for (const std::string& id : r->nodeIds())
        if (ClapInstance* c = r->plugin(id); c != nullptr && c->desc().id == a->plugin) nodo = id;
    if (nodo.empty()) {
        nodo = r->addPlugin(a->plugin, a->nombre, "", &e, a->id);
        if (nodo.empty()) return e.empty() ? std::string("no se pudo agregar ") + a->nombre : e;
        // Mide al costado: lo que sale de él (los pulsos del Latency Meter, el barrido
        // del Profiler) no va al aire.
        r->enParalelo(nodo);
        markDirty();
        emit changed();
    }
    ClapInstance* c = r->plugin(nodo);
    if (c == nullptr) return "no quedó el módulo";
    if (!c->openEditor(std::string(a->nombre) + " — " + source, &e)) return e.empty() ? "no abrió su interfaz" : e;
    return {};
}

std::string RackManager::setEntradaFuente(const std::string& source, const std::string& nodo,
                                          const std::string& fuente) {
    std::string e = validarEntrada(source, fuente);
    if (!e.empty()) return e;
    AudioRack* r = rack(source, false, &e);
    if (r == nullptr) return e;
    if (!r->setEntradaFuente(nodo, fuente)) return "'" + nodo + "' no es una tarjeta de entrada";
    markDirty();
    emit changed();
    return {};
}

std::set<std::string> RackManager::fuentesDeEntradas() const {
    std::set<std::string> out;
    for (const auto& [nombre, f] : filters_)
        if (f->rack)
            for (const auto& [nodo, fuente] : f->rack->entradas())
                if (!fuente.empty()) out.insert(fuente);
    return out;
}

std::set<std::string> RackManager::fuentesConRack() const {
    std::set<std::string> out;
    for (const auto& [nombre, f] : filters_) {
        if (!f->rack) continue;
        for (const auto& id : f->rack->nodeIds())
            if (id != "in" && id != "out") {
                out.insert(nombre);
                break;
            }
    }
    return out;
}

void RackManager::sincronizarEntradas() {
    BroadcastEngine* eng = motorApp();
    if (eng == nullptr || eng->motor() == nullptr) return;
    std::set<std::string> quiero;
    for (const auto& [nombre, f] : filters_) {
        if (!f->rack) continue;
        for (const auto& [nodo, fuente] : f->rack->entradas()) {
            const std::string clave = nombre + "\n" + nodo;
            // Una toma vieja de otra fuente (cambió la tarjeta) se rehace.
            auto it = tomas_.find(clave);
            if (it != tomas_.end() && (it->second->fuente != fuente || !eng->sourceExists(it->second->fuente) ||
                                       !eng->motor()->tomaViva(it->second->toma))) {
                f->rack->setEntradaAnillo(nodo, nullptr);
                tomas_.erase(it);
                it = tomas_.end();
            }
            if (fuente.empty() || !validarEntrada(nombre, fuente).empty()) {
                f->rack->setEntradaAnillo(nodo, nullptr);
                continue;
            }
            quiero.insert(clave);
            if (it == tomas_.end()) {
                if (!eng->sourceExists(fuente)) continue; // todavía no existe: en la próxima vuelta
                auto t = std::make_unique<Toma>();
                t->fuente = fuente;
                // El audio de la fuente después de su cadena, al anillo de la tarjeta.
                std::weak_ptr<anillo::Ring> w = t->ring;
                t->toma = eng->motor()->escucharEntrada(fuente, [w](const float* d, std::size_t cuadros) {
                    auto r = w.lock();
                    if (!r || cuadros == 0) return;
                    thread_local std::vector<float> l, rr;
                    l.resize(cuadros);
                    rr.resize(cuadros);
                    for (std::size_t i = 0; i < cuadros; ++i) {
                        l[i] = d[2 * i];
                        rr[i] = d[2 * i + 1];
                    }
                    r->push(l.data(), rr.data(), static_cast<std::uint32_t>(cuadros));
                });
                if (t->toma == 0) continue;
                it = tomas_.emplace(clave, std::move(t)).first;
            }
            f->rack->setEntradaAnillo(nodo, it->second->ring);
        }
    }
    // Texto a voz: cada tarjeta con su anillo (lo llena VocesRack).
    std::set<std::string> hablasVivas;
    for (const auto& [nombre, f] : filters_) {
        if (!f->rack) continue;
        for (const auto& h : f->rack->hablas()) {
            const std::string nodo = h.value("id", "");
            const std::string clave = nombre + "\n" + nodo;
            hablasVivas.insert(clave);
            auto& ring = anillosHabla_[clave];
            if (!ring) ring = std::make_shared<anillo::Ring>();
            f->rack->setEntradaAnillo(nodo, ring);
        }
    }
    for (auto it = anillosHabla_.begin(); it != anillosHabla_.end();)
        it = hablasVivas.contains(it->first) ? std::next(it) : anillosHabla_.erase(it);
    for (auto it = tomas_.begin(); it != tomas_.end();) {
        if (quiero.contains(it->first)) {
            ++it;
            continue;
        }
        // El rack o la tarjeta ya no están: se saca la toma (si el rack sigue,
        // su tarjeta ya no apunta al anillo).
        const std::string rackNombre = it->first.substr(0, it->first.find('\n'));
        const std::string nodo = it->first.substr(it->first.find('\n') + 1);
        if (auto f = filters_.find(rackNombre); f != filters_.end() && f->second->rack)
            f->second->rack->setEntradaAnillo(nodo, nullptr);
        it = tomas_.erase(it);
    }
}

void RackManager::ponerRackEnFuente(const std::string& fuente, const std::string& rack) {
    // Solo el ajuste `rack` de la entrada (se mezcla con los demás): la cadena
    // se rearma con (o sin) `barack` al frente del audio. Por la app: queda
    // también en la configuración guardada.
    fuentes::cambiarAjustes(fuente, json{{"rack", rack}});
}

void RackManager::revisarRacksGst() {
    // Una entrada que se fue (la borraron, o se rearmó como fuente nueva): su
    // rack queda pendiente y vuelve cuando aparezca otra con ese nombre.
    std::vector<std::string> idas;
    for (const auto& [nombre, f] : gstFilters_) {
        if (!existe(nombre)) {
            idas.push_back(nombre);
            continue;
        }
        // La misma entrada, pero sin el ajuste (p. ej. le cambiaron los ajustes
        // desde la configuración guardada): se le vuelve a pedir el rack.
        if (ajustesDe(nombre).value("rack", std::string()) != nombre) ponerRackEnFuente(nombre, nombre);
    }
    for (const std::string& n : idas) {
        auto it = gstFilters_.find(n);
        gstrack::retirar(n);
        detach(it->second.get()); // queda en pending_
        gstFilters_.erase(it);
    }
    if (!idas.empty()) emit changed();
}

void RackManager::montarPendientes() {
    if (motorApp() == nullptr || !pending_.is_object()) return;
    std::vector<std::string> nombres;
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (!filters_.contains(it.key())) nombres.push_back(it.key());
    for (const std::string& n : nombres)
        if (existe(n)) rack(n, true);
}

// --- Grabar lo que le llega a la tarjeta de salida -----------------------------------

std::string RackManager::setGrabar(const std::string& source, bool grabar) {
    std::string e;
    AudioRack* r = rack(source, false, &e);
    if (r == nullptr) return e;
    if (!r->setGrabar(grabar))
        return "'" + source + "' no tiene tarjeta de salida de audio (agregala con «+ Salida»)";
    markDirty();
    emit changed();
    return {};
}

RackManager::json RackManager::grabacion(const std::string& source) const {
    auto it = filters_.find(source);
    if (it == filters_.end() || !it->second->rack) return json{{"grabando", false}};
    Filter* f = it->second;
    bool conCable = false;
    for (const auto& c : f->rack->describe().value("cables", json::array()))
        if (c.value("hacia", "") == "salida-audio") conCable = true;
    json j{{"grabar", f->rack->grabar()}, {"conCable", conCable}, {"grabando", false}};
    std::shared_ptr<GrabadorWav> g;
    {
        std::lock_guard<std::mutex> l(f->grabMu);
        g = f->grab;
    }
    if (g) {
        j["grabando"] = g->grabando();
        j["archivo"] = g->archivo().string();
        j["segundos"] = g->segundos();
        j["bytes"] = g->bytes();
    }
    return j;
}

void RackManager::sincronizarGrabaciones() {
    for (auto& [nombre, f] : filters_) {
        if (!f->rack) continue;
        const bool quiere = f->rack->grabar();
        std::shared_ptr<GrabadorWav> actual;
        {
            std::lock_guard<std::mutex> l(f->grabMu);
            actual = f->grab;
        }
        if (quiere && !actual) {
            // Un nombre de archivo sin caracteres raros.
            std::string seguro;
            for (char c : nombre)
                seguro += (std::string("/\\:*?\"<>|").find(c) == std::string::npos &&
                           static_cast<unsigned char>(c) >= 32)
                              ? c
                              : '_';
            const std::string fecha = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss").toStdString();
            auto g = std::make_shared<GrabadorWav>(carpetaGrabaciones() / (seguro + "-" + fecha),
                                                   static_cast<std::uint32_t>(sampleRate()));
            if (!g->empezar().empty()) continue;
            std::lock_guard<std::mutex> l(f->grabMu);
            f->grab = std::move(g);
        } else if (!quiere && actual) {
            {
                std::lock_guard<std::mutex> l(f->grabMu);
                f->grab.reset();
            }
            actual->terminar();
        }
    }
}

// --- Texto a voz ------------------------------------------------------------------------

std::string RackManager::addHabla(const std::string& source, const json& opciones, std::string* error) {
    AudioRack* r = rack(source, true, error);
    if (r == nullptr) return {};
    const std::string id = r->addHabla(opciones);
    markDirty();
    emit changed();
    return id;
}

std::string RackManager::setHablaOpciones(const std::string& source, const std::string& nodo,
                                          const json& cambios) {
    std::string e;
    AudioRack* r = rack(source, false, &e);
    if (r == nullptr) return e;
    if (!r->setHablaOpciones(nodo, cambios)) return "'" + nodo + "' no es una tarjeta de texto a voz";
    markDirty();
    emit changed();
    return {};
}

RackManager::json RackManager::hablas() const {
    json out = json::array();
    for (const auto& [nombre, f] : filters_)
        if (f != nullptr && f->rack)
            for (json h : f->rack->hablas()) {
                h["fuente"] = nombre;
                h["nodo"] = h.value("id", "");
                h.erase("id");
                out.push_back(h);
            }
    return out;
}

// --- Traducir ------------------------------------------------------------------------------

namespace {
bool partirId(const std::string& id, std::string& fuente, std::string& nodo) {
    const auto i = id.rfind('#');
    if (i == std::string::npos || i == 0 || i + 1 == id.size()) return false;
    fuente = id.substr(0, i);
    nodo = id.substr(i + 1);
    return true;
}
} // namespace

std::string RackManager::addTraduccion(const json& t, std::string* error) {
    std::string fuente = t.value("fuente", std::string());
    const std::string desde = t.value("desde", std::string());
    if (fuente.empty() && !desde.empty()) {
        const json tx = textoDe(desde);
        if (tx.is_object()) fuente = tx.value("fuente", std::string());
        if (fuente.empty())
            for (const auto& x : traducciones())
                if (x.value("nombre", std::string()) == desde ||
                    (x.value("nombre", std::string()).empty() &&
                     x.value("desde", std::string()) + " · " + x.value("a", std::string()) == desde))
                    fuente = x.value("fuente", std::string());
    }
    if (fuente.empty()) {
        if (error)
            *error = "no hay una voz a texto que publique «" + desde +
                     "»: decí en qué fuente va la tarjeta (`fuente`)";
        return {};
    }
    AudioRack* r = rack(fuente, true, error);
    if (r == nullptr) return {};
    json o = t;
    o.erase("fuente");
    o.erase("id");
    const std::string nodo = r->addTraduccion(o);
    markDirty();
    emit changed();
    return fuente + "#" + nodo;
}

std::string RackManager::setTraduccionOpciones(const std::string& id, const json& cambios) {
    std::string fuente, nodo, e;
    if (!partirId(id, fuente, nodo)) return "no hay una traducción «" + id + "»";
    AudioRack* r = rack(fuente, false, &e);
    if (r == nullptr) return e;
    if (!r->setTraduccionOpciones(nodo, cambios)) return "no hay una traducción «" + id + "»";
    markDirty();
    emit changed();
    return {};
}

std::string RackManager::quitarTraduccion(const std::string& id) {
    std::string fuente, nodo, e;
    if (!partirId(id, fuente, nodo)) return "no hay una traducción «" + id + "»";
    AudioRack* r = rack(fuente, false, &e);
    if (r == nullptr) return e;
    if (r->kindOf(nodo) != "traducir") return "no hay una traducción «" + id + "»";
    if (!r->remove(nodo, &e)) return e;
    markDirty();
    emit changed();
    return {};
}

RackManager::json RackManager::traducciones() const {
    json out = json::array();
    for (const auto& [nombre, f] : filters_)
        if (f != nullptr && f->rack)
            for (json t : f->rack->traducciones()) {
                t["fuente"] = nombre;
                t["nodo"] = t.value("id", "");
                t["id"] = nombre + "#" + t.value("id", std::string());
                out.push_back(t);
            }
    return out;
}

std::shared_ptr<anillo::Ring> RackManager::anilloHabla(const std::string& source,
                                                       const std::string& nodo) const {
    auto it = anillosHabla_.find(source + "\n" + nodo);
    return it == anillosHabla_.end() ? nullptr : it->second;
}

} // namespace ba::app::audio
