// ============================================================================
// src/app/audio/RackManager.cpp — ver RackManager.hpp
// ============================================================================

#include "audio/RackManager.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QSocketNotifier>
#include <QThread>

#include <obs.h>

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
    int addTimer(int periodMs, std::function<void()> fn) override {
        auto* t = new QTimer();
        t->setInterval(periodMs);
        QObject::connect(t, &QTimer::timeout, owner_, [fn]() { fn(); });
        t->start();
        const int id = ++next_;
        timers_[id] = t;
        return id;
    }
    void removeTimer(int id) override {
        auto it = timers_.find(id);
        if (it == timers_.end()) return;
        it->second->stop();
        it->second->deleteLater();
        timers_.erase(it);
    }
    bool addFd(int fd, uint32_t flags, std::function<void(uint32_t)> fn) override {
#ifdef _WIN32
        (void)fd;
        (void)flags;
        (void)fn;
        return false;
#else
        removeFd(fd);
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
        return true;
#endif
    }
    void removeFd(int fd) override {
        auto it = fds_.find(fd);
        if (it == fds_.end()) return;
        for (auto* n : it->second) {
            n->setEnabled(false);
            n->deleteLater();
        }
        fds_.erase(it);
    }

    void stateDirty() override;

   private:
    QObject* owner_;
    int next_ = 0;
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
};

namespace {

const char* filterName(void*) { return "Rack de audio (LSP)"; }

void* filterCreate(obs_data_t*, obs_source_t* self) {
    auto* f = new RackManager::Filter();
    f->self = self;
    std::shared_ptr<ClapLibrary> lib = g_mgr != nullptr ? g_mgr->library() : nullptr;
    f->rack = std::make_unique<AudioRack>(lib, nullptr, g_mgr != nullptr ? g_mgr->sampleRate() : 48000);
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
}

RackManager::~RackManager() {
    saveNow();
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

// --- Escucha (salida de audio del sistema) ------------------------------------------------

json RackManager::monitorDevices() const {
    json list = json::array();
    list.push_back({{"id", ""}, {"nombre", "Sin escucha aparte (solo el programa)"}});
    obs_enum_audio_monitoring_devices(
        [](void* p, const char* name, const char* id) {
            static_cast<json*>(p)->push_back(
                {{"id", id != nullptr ? id : ""}, {"nombre", name != nullptr ? name : ""}});
            return true;
        },
        &list);
    return list;
}

std::string RackManager::setMonitorDevice(const std::string& id) {
    std::string name;
    if (!id.empty()) {
        struct Ctx {
            const std::string& id;
            std::string name;
            bool found = false;
        } ctx{id, {}, false};
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
        name = ctx.name;
        if (!obs_set_audio_monitoring_device(name.c_str(), id.c_str()))
            return "no se pudo abrir la salida de audio";
    }
    monitorId_ = id;
    monitorName_ = name;
    syncMonitors();
    saveMonitor();
    emit changed();
    return {};
}

void RackManager::syncMonitors() {
    for (auto& [name, f] : filters_)
        if (f->tap != nullptr) obs_source_set_muted(f->tap, monitorId_.empty());
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
            if (type == "ba_guest_audio") {
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

void RackManager::markDirty() { saveTimer_.start(); }

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

void RackManager::restore() {
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

} // namespace ba::app::audio
