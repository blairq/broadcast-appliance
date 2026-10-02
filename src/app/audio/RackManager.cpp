// ============================================================================
// src/app/audio/RackManager.cpp — ver RackManager.hpp
// ============================================================================

#include "platform/Paths.hpp"
#include "audio/RackManager.hpp"

#include "audio/GrabadorWav.hpp"

#include <mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QThread>

#include <atomic>

#include <obs.h>
#include <util/platform.h>

#include <cstdlib>
#include <fstream>
#include <vector>

namespace ba::app::audio {

namespace fs = std::filesystem;
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
    // Los timers viven en el hilo principal aunque se pidan desde otro: los
    // racks que OBS restaura de la escena se crean en un hilo de OBS, y un
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

// --- Filtro de OBS ------------------------------------------------------------------------

namespace {
RackManager* g_mgr = nullptr;
constexpr const char* kFilterId = "ba_audio_rack";
constexpr const char* kFilterName = "Rack de audio";
// Fuente privada por rack que lleva el nodo de escucha a los auriculares del
// operador: MONITOR_ONLY, nunca entra en la mezcla del programa.
constexpr const char* kTapId = "ba_rack_escucha";
} // namespace

void QtHostServices::stateDirty() {
    QMetaObject::invokeMethod(
        owner_,
        []() {
            if (g_mgr != nullptr) g_mgr->markDirty();
        },
        Qt::QueuedConnection);
}

struct RackManager::Filter {
    obs_source_t* self = nullptr;
    std::string source;
    std::unique_ptr<AudioRack> rack;
    obs_source_t* tap = nullptr; // salida de escucha (puede ser nullptr)
    // Grabación de la tarjeta de salida: el hilo de audio toma una copia del
    // puntero bajo este mutex (muy corto) y le agrega lo que llegó.
    std::mutex grabMu;
    std::shared_ptr<GrabadorWav> grab;
};

namespace {

const char* filterName(void*) { return "Rack de audio (LSP)"; }

void* filterCreate(obs_data_t*, obs_source_t* self) {
    auto* f = new RackManager::Filter();
    f->self = self;
    std::shared_ptr<ClapLibrary> lib = g_mgr != nullptr ? g_mgr->library() : nullptr;
    // Con los servicios del proceso (timers, ventanas): sin ellos los plugins de
    // un rack restaurado de la escena no tienen interfaz que funcione.
    f->rack = std::make_unique<AudioRack>(lib, g_mgr != nullptr ? g_mgr->services() : nullptr,
                                          g_mgr != nullptr ? g_mgr->sampleRate() : 48000);
    return f;
}

void filterDestroy(void* data) {
    auto* f = static_cast<RackManager::Filter*>(data);
    if (g_mgr != nullptr) g_mgr->detach(f);
    // Los plugins se destruyen en el hilo principal (lo pide CLAP).
    if (QCoreApplication::instance() != nullptr &&
        QThread::currentThread() != QCoreApplication::instance()->thread())
        QMetaObject::invokeMethod(QCoreApplication::instance(), [f]() { delete f; }, Qt::QueuedConnection);
    else
        delete f;
}

std::mutex g_textoLock;
RackManager::TextoSink g_texto;

obs_audio_data* filterAudio(void* data, obs_audio_data* audio) {
    auto* f = static_cast<RackManager::Filter*>(data);
    const int ch = static_cast<int>(audio_output_get_channels(obs_get_audio()));
    f->rack->process(reinterpret_cast<float* const*>(audio->data), ch, audio->frames);
    // Escucha: lo que junto el nodo "monitor" se entrega a su fuente privada,
    // que libobs reproduce en la salida elegida (no vuelve a la mezcla).
    const uint32_t mf = f->rack->monitorFrames();
    if (f->tap != nullptr && mf > 0) {
        obs_source_audio out{};
        out.data[0] = reinterpret_cast<const uint8_t*>(f->rack->monitorPlane(0));
        out.data[1] = reinterpret_cast<const uint8_t*>(f->rack->monitorPlane(1));
        out.frames = mf;
        out.speakers = SPEAKERS_STEREO;
        out.format = AUDIO_FORMAT_FLOAT_PLANAR;
        out.samples_per_sec = audio_output_get_sample_rate(obs_get_audio());
        out.timestamp = audio->timestamp;
        obs_source_output_audio(f->tap, &out);
    }
    if (mf > 0) {
        std::shared_ptr<GrabadorWav> g;
        {
            std::lock_guard<std::mutex> l(f->grabMu);
            g = f->grab;
        }
        if (g) g->agregar(f->rack->monitorPlane(0), f->rack->monitorPlane(1), mf);
    }
    // Voz a texto: lo que juntó cada nodo va al reconocedor (copia y vuelve).
    {
        std::lock_guard<std::mutex> l(g_textoLock);
        if (g_texto) {
            const uint32_t sr = audio_output_get_sample_rate(obs_get_audio());
            f->rack->textosCapturados([sr](const std::string& flujo, int modo, const float* m, uint32_t n) {
                g_texto(flujo, modo, m, n, sr);
            });
        }
    }
    return audio;
}

const char* tapName(void*) { return "Escucha del rack"; }
void* tapCreate(obs_data_t*, obs_source_t* self) { return self; }
void tapDestroy(void*) {}

} // namespace

RackManager::RackManager(fs::path dataRoot, QObject* parent) : QObject(parent), root_(std::move(dataRoot)) {
    g_mgr = this;
    // Viven lo que el proceso: OBS puede destruir un filtro (y sus plugins,
    // que devuelven sus timers) despues que el manager.
    static QtHostServices* services = new QtHostServices(QCoreApplication::instance());
    services_ = services;
    static bool registered = false;
    if (!registered) {
        obs_source_info info{};
        info.id = kFilterId;
        info.type = OBS_SOURCE_TYPE_FILTER;
        info.output_flags = OBS_SOURCE_AUDIO;
        info.get_name = filterName;
        info.create = filterCreate;
        info.destroy = filterDestroy;
        info.filter_audio = filterAudio;
        obs_register_source(&info);

        obs_source_info tap{};
        tap.id = kTapId;
        tap.type = OBS_SOURCE_TYPE_INPUT;
        tap.output_flags = OBS_SOURCE_AUDIO;
        tap.get_name = tapName;
        tap.create = tapCreate;
        tap.destroy = tapDestroy;
        obs_register_source(&tap);
        registered = true;
    }
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(800);
    connect(&saveTimer_, &QTimer::timeout, this, &RackManager::saveNow);
    revision_.setInterval(2000);
    connect(&revision_, &QTimer::timeout, this, [this]() {
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
    tomas_.clear(); // cada Toma saca su callback
    // Los filtros siguen vivos en OBS; que no apunten al manager.
    for (auto& [name, f] : filters_)
        (void)f;
    filters_.clear();
    g_mgr = nullptr;
}

HostServices* RackManager::services() const { return services_; }

double RackManager::sampleRate() const {
    audio_t* a = obs_get_audio();
    return a != nullptr ? static_cast<double>(audio_output_get_sample_rate(a)) : 48000.0;
}

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
    if (f->tap == nullptr) {
        const std::string name = "ba-escucha-" + f->source;
        f->tap = obs_source_create_private(kTapId, name.c_str(), nullptr);
        if (f->tap != nullptr) {
            // Solo a los auriculares del operador: jamas a la mezcla.
            obs_source_set_monitoring_type(f->tap, OBS_MONITORING_TYPE_MONITOR_ONLY);
            obs_source_set_muted(f->tap, monitorId_.empty());
        }
    }
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
    if (f->tap != nullptr) {
        obs_source_release(f->tap);
        f->tap = nullptr;
    }
}

// --- Salidas de audio del equipo ------------------------------------------------

namespace {
/// Placa a la que pertenece una salida, para agrupar la lista. En PulseAudio el
/// id es `alsa_output.<placa>.<perfil>__<puerto>__sink`: la placa es el tramo
/// que sigue al prefijo. Si no se reconoce, la salida queda en su propio grupo.
std::string deviceGroupOf(const std::string& id, const std::string& name) {
    const std::size_t dot = id.find('.');
    if (dot != std::string::npos) {
        const std::size_t end = id.find('.', dot + 1);
        if (end != std::string::npos) return id.substr(dot + 1, end - dot - 1);
    }
    return name.empty() ? std::string("Otras salidas") : name;
}
} // namespace

json RackManager::outputDevices() const {
    struct Item {
        std::string id, name;
    };
    std::vector<Item> items;
    obs_enum_audio_monitoring_devices(
        [](void* p, const char* name, const char* id) {
            static_cast<std::vector<Item>*>(p)->push_back(
                {id != nullptr ? id : "", name != nullptr ? name : ""});
            return true;
        },
        &items);
    std::vector<std::string> order;
    std::map<std::string, json> byGroup;
    std::map<std::string, std::string> label;
    for (const auto& it : items) {
        const std::string g = deviceGroupOf(it.id, it.name);
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
    struct Ctx {
        const std::string& id;
        std::string name;
        bool found = false;
    } ctx{device, {}, false};
    obs_enum_audio_monitoring_devices(
        [](void* p, const char* n, const char* i) {
            auto* c = static_cast<Ctx*>(p);
            if (i != nullptr && c->id == i) {
                c->name = n != nullptr ? n : "";
                c->found = true;
                return false;
            }
            return true;
        },
        &ctx);
    if (!ctx.found) return "esa salida de audio ya no esta";
    if (!obs_set_audio_monitoring_device(ctx.name.c_str(), device.c_str()))
        return "no se pudo abrir la salida de audio";
    monitorId_ = device;
    monitorName_ = ctx.name;
    it->second->rack->addOutput(device, ctx.name);
    syncMonitors();
    saveMonitor();
    markDirty();
    emit changed();
    return {};
}

std::string RackManager::setAuriculares(const std::string& device) {
    std::string nombre;
    if (!device.empty()) {
        struct Ctx {
            const std::string& id;
            std::string name;
            bool found = false;
        } ctx{device, {}, false};
        obs_enum_audio_monitoring_devices(
            [](void* p, const char* n, const char* i) {
                auto* c = static_cast<Ctx*>(p);
                if (i != nullptr && c->id == i) {
                    c->name = n != nullptr ? n : "";
                    c->found = true;
                    return false;
                }
                return true;
            },
            &ctx);
        if (!ctx.found) return "esa salida de audio ya no esta";
        nombre = ctx.name;
    }
    // "default" es la salida del sistema para libobs.
    if (!obs_set_audio_monitoring_device(device.empty() ? "Default" : nombre.c_str(),
                                         device.empty() ? "default" : device.c_str()))
        return "no se pudo abrir la salida de audio";
    monitorId_ = device;
    monitorName_ = nombre;
    syncMonitors();
    saveMonitor();
    emit changed();
    return {};
}

void RackManager::syncMonitors() {
    // Suena el rack que tiene nodo de salida (y un dispositivo elegido).
    for (auto& [name, f] : filters_)
        if (f->tap != nullptr) obs_source_set_muted(f->tap, !f->rack->hasOutput() || monitorId_.empty());
}

void RackManager::saveMonitor() const {
    const fs::path file = root_ / "Config" / "audio-escucha.json";
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    if (out) out << json{{"id", monitorId_}, {"nombre", monitorName_}}.dump(2);
}

void RackManager::restoreMonitor() {
    std::ifstream in(root_ / "Config" / "audio-escucha.json", std::ios::binary);
    if (!in) return;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    const std::string id = j.value("id", "");
    const std::string name = j.value("nombre", "");
    if (id.empty()) return;
    // El dispositivo puede no estar (se desenchufo): se queda sin escucha.
    if (obs_set_audio_monitoring_device(name.c_str(), id.c_str())) {
        monitorId_ = id;
        monitorName_ = name;
    }
    syncMonitors();
}

json RackManager::audioSources() const {
    struct Ctx {
        const RackManager* self;
        json list = json::array();
    } ctx{this};
    obs_enum_sources(
        [](void* p, obs_source_t* s) {
            auto* c = static_cast<Ctx*>(p);
            if ((obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) == 0) return true;
            const std::string name = obs_source_get_name(s);
            const std::string type = obs_source_get_id(s);
            // Grupo para el arbol de la interfaz: por donde entra el audio.
            std::string group = "Otras fuentes";
            if (type == "ba_voz_asistente") {
                group = "Voces (texto a audio)"; // la del sistema, la del teleprompter…
            } else if (type == "ba_entrada_fisica") {
                group = "Entradas del equipo (placas)";
            } else if (type == "ba_guest_audio") {
                group = "Invitados";
            } else if (type.find("_input_capture") != std::string::npos) {
                group = "Micrófonos y líneas del equipo";
            } else if (type.find("_output_capture") != std::string::npos) {
                group = "Audio del equipo (lo que suena)";
            } else if (type == "ffmpeg_source" || type == "vlc_source" || type == "ba_ndi_input") {
                group = "Medios y red";
            }
            json j{{"nombre", name}, {"tipo", type}, {"grupo", group}, {"rack", false}};
            auto it = c->self->filters_.find(name);
            if (it != c->self->filters_.end()) {
                j["rack"] = true;
                int mods = 0;
                for (const auto& id : it->second->rack->nodeIds())
                    if (it->second->rack->kindOf(id) == "plugin") ++mods;
                j["modulos"] = mods;
                j["salida"] = it->second->rack->meters("out");
            }
            c->list.push_back(std::move(j));
            return true;
        },
        &ctx);
    return ctx.list;
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

bool RackManager::flujoLibre(const std::string& flujo, const std::string& salvoRack, const std::string& salvoNodo) const {
    for (const auto& [nombre, f] : filters_)
        if (f->rack)
            for (const auto& t : f->rack->textos())
                if (t.flujo == flujo && !(nombre == salvoRack && t.id == salvoNodo)) return false;
    return true;
}

std::string RackManager::addTexto(const std::string& source, const std::string& modo, const std::string& flujo,
                                  std::string* error) {
    AudioRack* r = rack(source, true, error);
    if (r == nullptr) return {};
    std::string nombre = flujo;
    if (nombre.empty()) {
        nombre = source;
        for (int i = 2; !flujoLibre(nombre, {}, {}); ++i) nombre = source + " " + std::to_string(i);
    } else if (!flujoLibre(nombre, {}, {})) {
        if (error != nullptr) *error = "ya hay una salida de texto «" + nombre + "»";
        return {};
    }
    const std::string id = r->addTexto(modo, nombre);
    markDirty();
    emit changed();
    return id;
}

std::string RackManager::setTextoOpciones(const std::string& source, const std::string& nodo, const json& cambios) {
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
        for (auto it = cambios["salidas"].begin(); it != cambios["salidas"].end(); ++it) actuales[it.key()] = it.value();
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
    for (const auto& t : r->textos()) r->remove(t.id, &e);
    markDirty();
    emit changed();
}

RackManager::json RackManager::textos() const {
    json out = json::array();
    for (const auto& [nombre, f] : filters_)
        if (f != nullptr && f->rack)
            for (const auto& t : f->rack->textos())
                out.push_back({{"flujo", t.flujo}, {"fuente", nombre}, {"nodo", t.id}, {"modo", t.modo},
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
            for (int i = 2; !flujoLibre(n, nombre, t.id); ++i) n = (t.flujo.empty() ? nombre : t.flujo) + " " + std::to_string(i);
            f->rack->setTextoFlujo(t.id, n);
            algo = true;
        }
    }
    if (algo) {
        saveTimer_.start();
        emit changed();
    }
}

AudioRack* RackManager::rack(const std::string& source, bool create, std::string* error) {
    auto it = filters_.find(source);
    if (it != filters_.end()) return it->second->rack.get();
    obs_source_t* src = obs_get_source_by_name(source.c_str());
    if (src == nullptr) {
        if (error != nullptr) *error = "no existe la fuente '" + source + "'";
        return nullptr;
    }
    AudioRack* out = nullptr;
    if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) == 0) {
        if (error != nullptr) *error = "'" + source + "' no tiene audio";
    } else if (!create) {
        if (error != nullptr) *error = "'" + source + "' no tiene rack";
    } else {
        obs_source_t* filter = obs_source_create_private(kFilterId, kFilterName, nullptr);
        auto* f = filter != nullptr ? static_cast<Filter*>(obs_obj_get_data(filter)) : nullptr;
        if (f == nullptr) {
            if (error != nullptr) *error = "no se pudo crear el filtro del rack";
        } else {
            // La biblioteca pudo abrirse despues de crear el filtro: el rack la toma.
            f->source = source;
            if (pending_.contains(source)) {
                std::string e;
                f->rack = std::make_unique<AudioRack>(library(), services_, sampleRate());
                f->rack->load(pending_[source], &e);
            } else {
                f->rack = std::make_unique<AudioRack>(library(), services_, sampleRate());
                // Sin rack guardado, se mira si hay una plantilla marcada para
                // este tipo de fuente: es lo que hace que un invitado que se
                // conecta ya suene bien sin que nadie le arme nada.
                const std::string plantilla = plantillaDeTipo(obs_source_get_id(src));
                if (!plantilla.empty()) {
                    std::string e;
                    f->rack->load(plantillas_[plantilla].value("rack", json::object()), &e);
                }
            }
            obs_source_filter_add(src, filter);
            attach(f);
            out = f->rack.get();
            markDirty();
        }
        if (filter != nullptr) obs_source_release(filter); // la fuente lo retiene
    }
    obs_source_release(src);
    if (out != nullptr) emit changed();
    return out;
}

bool RackManager::removeRack(const std::string& source) {
    auto it = filters_.find(source);
    if (it == filters_.end()) return false;
    obs_source_t* src = obs_get_source_by_name(source.c_str());
    obs_source_t* filter = it->second->self;
    filters_.erase(it);
    pending_.erase(source);
    if (src != nullptr) {
        obs_source_filter_remove(src, filter);
        obs_source_release(src);
    }
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
        out << plantillas_.dump(2);
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
        out << all.dump(2);
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
    for (auto it = all.begin(); it != all.end(); ++it) {
        obs_source_t* src = obs_get_source_by_name(it.key().c_str());
        if (src == nullptr) continue;
        obs_source_release(src);
        rack(it.key(), true);
    }
}

// --- Tarjetas de entrada (el audio de otra fuente) ---------------------------------

struct RackManager::Toma {
    std::string fuente;
    obs_source_t* src = nullptr;
    std::shared_ptr<anillo::Ring> ring = std::make_shared<anillo::Ring>();

    static void alAudio(void* p, obs_source_t*, const ::audio_data* data, bool muted) {
        auto* t = static_cast<Toma*>(p);
        if (data == nullptr || data->frames == 0 || data->data[0] == nullptr) return;
        const auto* l = reinterpret_cast<const float*>(data->data[0]);
        const auto* r = data->data[1] != nullptr ? reinterpret_cast<const float*>(data->data[1]) : l;
        const std::uint32_t sr = audio_output_get_sample_rate(obs_get_audio());
        if (muted) {
            std::vector<float> z(data->frames, 0.0f);
            t->ring->pushEn(z.data(), z.data(), data->frames, data->timestamp, os_gettime_ns(), sr);
        } else {
            t->ring->pushEn(l, r, data->frames, data->timestamp, os_gettime_ns(), sr);
        }
    }
    ~Toma() {
        // Si libobs ya se apagó (cierre temprano), la fuente ya no existe.
        if (src != nullptr && obs_initialized()) {
            obs_source_remove_audio_capture_callback(src, &Toma::alAudio, this);
            obs_source_release(src);
        }
    }
};

std::string RackManager::validarEntrada(const std::string& source, const std::string& fuente) const {
    if (fuente.empty()) return "falta de qué fuente traer el audio";
    if (fuente == source) return "una fuente no puede traer su propio audio (sería un lazo)";
    obs_source_t* src = obs_get_source_by_name(fuente.c_str());
    if (src == nullptr) return "no existe la fuente '" + fuente + "'";
    const bool audio = (obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) != 0;
    obs_source_release(src);
    if (!audio) return "'" + fuente + "' no tiene audio";
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
                return "'" + fuente + "' ya trae el audio de '" + source + "' (directa o indirectamente): sería un lazo";
            pendientes.push_back(origen);
        }
    }
    return {};
}

std::string RackManager::addEntrada(const std::string& source, const std::string& fuente, std::string* error) {
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

std::string RackManager::setEntradaFuente(const std::string& source, const std::string& nodo, const std::string& fuente) {
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

void RackManager::sincronizarEntradas() {
    if (!obs_initialized()) return;
    std::set<std::string> quiero;
    for (const auto& [nombre, f] : filters_) {
        if (!f->rack) continue;
        for (const auto& [nodo, fuente] : f->rack->entradas()) {
            const std::string clave = nombre + "\n" + nodo;
            // Una toma vieja de otra fuente (cambió la tarjeta) se rehace.
            auto it = tomas_.find(clave);
            if (it != tomas_.end() && (it->second->fuente != fuente || obs_source_removed(it->second->src))) {
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
                obs_source_t* src = obs_get_source_by_name(fuente.c_str());
                if (src == nullptr) continue; // todavía no existe: en la próxima vuelta
                auto t = std::make_unique<Toma>();
                t->fuente = fuente;
                t->src = src; // la referencia queda en la toma
                obs_source_add_audio_capture_callback(src, &Toma::alAudio, t.get());
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

void RackManager::montarPendientes() {
    if (!obs_initialized() || !pending_.is_object()) return;
    std::vector<std::string> nombres;
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (!filters_.contains(it.key())) nombres.push_back(it.key());
    for (const std::string& n : nombres) {
        obs_source_t* src = obs_get_source_by_name(n.c_str());
        if (src == nullptr) continue;
        obs_source_release(src);
        rack(n, true);
    }
}

// --- Grabar lo que le llega a la tarjeta de salida -----------------------------------

std::string RackManager::setGrabar(const std::string& source, bool grabar) {
    std::string e;
    AudioRack* r = rack(source, false, &e);
    if (r == nullptr) return e;
    if (!r->setGrabar(grabar)) return "'" + source + "' no tiene tarjeta de salida de audio (agregala con «+ Salida»)";
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
            for (char c : nombre) seguro += (std::string("/\\:*?\"<>|").find(c) == std::string::npos && static_cast<unsigned char>(c) >= 32) ? c : '_';
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

std::string RackManager::setHablaOpciones(const std::string& source, const std::string& nodo, const json& cambios) {
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

std::shared_ptr<anillo::Ring> RackManager::anilloHabla(const std::string& source, const std::string& nodo) const {
    auto it = anillosHabla_.find(source + "\n" + nodo);
    return it == anillosHabla_.end() ? nullptr : it->second;
}

} // namespace ba::app::audio
