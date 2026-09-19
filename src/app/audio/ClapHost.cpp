// ============================================================================
// src/app/audio/ClapHost.cpp — ver ClapHost.hpp
// ============================================================================

#include "audio/ClapHost.hpp"

#include <clap/clap.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

#include "audio/ClapEditor.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ba::app::audio {

namespace {

thread_local bool tl_audioThread = false;

void* openLib(const std::string& path, std::string* error) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
    HMODULE h = LoadLibraryW(w.c_str());
    if (h == nullptr && error != nullptr)
        *error = "LoadLibrary fallo (" + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(h);
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr && error != nullptr) *error = dlerror();
    return h;
#endif
}

void* libSymbol(void* h, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(h), name));
#else
    return dlsym(h, name);
#endif
}

void closeLib(void* h) {
#ifdef _WIN32
    FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    dlclose(h);
#endif
}

void passthrough(float* const* const* in, float* const* const* out, uint32_t inCh, uint32_t outCh,
                 uint32_t frames) {
    for (uint32_t c = 0; c < outCh; ++c) {
        if (c < inCh && in != nullptr && in[0] != nullptr)
            std::memcpy(out[0][c], in[0][c], frames * sizeof(float));
        else
            std::memset(out[0][c], 0, frames * sizeof(float));
    }
}

} // namespace

bool ParamInfo::readOnly() const { return (flags & CLAP_PARAM_IS_READONLY) != 0; }
bool ParamInfo::stepped() const { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
bool ParamInfo::hidden() const { return (flags & CLAP_PARAM_IS_HIDDEN) != 0; }
bool ParamInfo::isEnum() const { return (flags & CLAP_PARAM_IS_ENUM) != 0; }

// --- Biblioteca -----------------------------------------------------------------------

std::shared_ptr<ClapLibrary> ClapLibrary::open(const std::string& path, std::string* error) {
    std::shared_ptr<ClapLibrary> lib(new ClapLibrary());
    lib->path_ = path;
    lib->handle_ = openLib(path, error);
    if (lib->handle_ == nullptr) return nullptr;
    lib->entry_ = static_cast<const clap_plugin_entry_t*>(libSymbol(lib->handle_, "clap_entry"));
    if (lib->entry_ == nullptr || !clap_version_is_compatible(lib->entry_->clap_version)) {
        if (error != nullptr) *error = "no es un plugin CLAP compatible";
        return nullptr;
    }
    if (!lib->entry_->init(path.c_str())) {
        lib->entry_ = nullptr; // sin deinit: init fallo
        if (error != nullptr) *error = "el plugin no se pudo inicializar";
        return nullptr;
    }
    lib->factory_ =
        static_cast<const clap_plugin_factory_t*>(lib->entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (lib->factory_ == nullptr) {
        if (error != nullptr) *error = "el .clap no trae fabrica de plugins";
        return nullptr;
    }
    const uint32_t n = lib->factory_->get_plugin_count(lib->factory_);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_plugin_descriptor_t* d = lib->factory_->get_plugin_descriptor(lib->factory_, i);
        if (d == nullptr || d->id == nullptr) continue;
        PluginDesc p;
        p.id = d->id;
        p.name = d->name != nullptr ? d->name : d->id;
        p.vendor = d->vendor != nullptr ? d->vendor : "";
        p.version = d->version != nullptr ? d->version : "";
        p.description = d->description != nullptr ? d->description : "";
        for (const char* const* f = d->features; f != nullptr && *f != nullptr; ++f)
            p.features.emplace_back(*f);
        lib->plugins_.push_back(std::move(p));
    }
    return lib;
}

ClapLibrary::~ClapLibrary() {
    if (entry_ != nullptr) entry_->deinit();
    if (handle_ != nullptr) closeLib(handle_);
}

const PluginDesc* ClapLibrary::find(const std::string& id) const {
    for (const auto& p : plugins_)
        if (p.id == id) return &p;
    return nullptr;
}

// --- Host (lo que el plugin ve de nosotros) ---------------------------------------------

struct ClapInstance::Host {
    clap_host_t host{};
    ClapInstance* inst = nullptr;
    std::thread::id mainThread;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::mutex timersLock;
    std::map<clap_id, int> timers; // id CLAP -> id de HostServices
    clap_id nextTimer = 1;

    static Host* from(const clap_host_t* h) { return static_cast<Host*>(h->host_data); }

    // Correr en el principal sin tocar una instancia ya destruida.
    void onMain(std::function<void(ClapInstance&)> fn) {
        if (inst->services_ == nullptr) return;
        std::weak_ptr<bool> w = alive;
        ClapInstance* i = inst;
        inst->services_->runOnMain([w, i, fn]() {
            if (w.lock()) fn(*i);
        });
    }

    // --- extensiones -----------------------------------------------------------------
    static void log(const clap_host_t*, clap_log_severity sev, const char* msg) {
        if (sev >= CLAP_LOG_WARNING) std::fprintf(stderr, "[clap] %s\n", msg != nullptr ? msg : "");
    }
    static bool isMain(const clap_host_t* h) { return std::this_thread::get_id() == from(h)->mainThread; }
    static bool isAudio(const clap_host_t*) { return tl_audioThread; }

    static void paramsRescan(const clap_host_t*, clap_param_rescan_flags) {}
    static void paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
    static void paramsRequestFlush(const clap_host_t* h) {
        from(h)->onMain([](ClapInstance& i) { i.flushParamsOnMain(); });
    }
    static void stateDirty(const clap_host_t* h) {
        if (from(h)->inst->services_ != nullptr) from(h)->inst->services_->stateDirty();
    }
    static void latencyChanged(const clap_host_t*) {}
    static bool portsFlagSupported(const clap_host_t*, uint32_t) { return false; }
    static void portsRescan(const clap_host_t*, uint32_t) {}

    static bool registerTimer(const clap_host_t* h, uint32_t periodMs, clap_id* timerId) {
        Host* self = from(h);
        if (self->inst->services_ == nullptr || timerId == nullptr) return false;
        std::lock_guard<std::mutex> lk(self->timersLock);
        const clap_id id = self->nextTimer++;
        ClapInstance* inst = self->inst;
        std::weak_ptr<bool> w = self->alive;
        const int sid =
            inst->services_->addTimer(static_cast<int>(std::max<uint32_t>(periodMs, 10)), [w, inst, id]() {
                if (!w.lock() || inst->plugin_ == nullptr) return;
                auto* ext = static_cast<const clap_plugin_timer_support_t*>(
                    inst->plugin_->get_extension(inst->plugin_, CLAP_EXT_TIMER_SUPPORT));
                if (ext != nullptr) ext->on_timer(inst->plugin_, id);
            });
        self->timers[id] = sid;
        *timerId = id;
        return true;
    }
    static bool unregisterTimer(const clap_host_t* h, clap_id timerId) {
        Host* self = from(h);
        std::lock_guard<std::mutex> lk(self->timersLock);
        auto it = self->timers.find(timerId);
        if (it == self->timers.end()) return false;
        self->inst->services_->removeTimer(it->second);
        self->timers.erase(it);
        return true;
    }
    static bool registerFd(const clap_host_t* h, int fd, clap_posix_fd_flags_t flags) {
        Host* self = from(h);
        if (self->inst->services_ == nullptr) return false;
        ClapInstance* inst = self->inst;
        std::weak_ptr<bool> w = self->alive;
        return inst->services_->addFd(fd, flags, [w, inst, fd](uint32_t got) {
            if (!w.lock() || inst->plugin_ == nullptr) return;
            auto* ext = static_cast<const clap_plugin_posix_fd_support_t*>(
                inst->plugin_->get_extension(inst->plugin_, CLAP_EXT_POSIX_FD_SUPPORT));
            if (ext != nullptr) ext->on_fd(inst->plugin_, fd, got);
        });
    }
    static bool modifyFd(const clap_host_t* h, int fd, clap_posix_fd_flags_t flags) {
        unregisterFd(h, fd);
        return registerFd(h, fd, flags);
    }
    static bool unregisterFd(const clap_host_t* h, int fd) {
        Host* self = from(h);
        if (self->inst->services_ == nullptr) return false;
        self->inst->services_->removeFd(fd);
        return true;
    }

    static void guiResizeHints(const clap_host_t*) {}
    static bool guiRequestResize(const clap_host_t* h, uint32_t w, uint32_t hh) {
        from(h)->onMain([w, hh](ClapInstance& i) {
            if (i.editor_) i.editor_->resize(w, hh);
        });
        return true;
    }
    static bool guiRequestShow(const clap_host_t*) { return true; }
    static bool guiRequestHide(const clap_host_t*) { return true; }
    static void guiClosed(const clap_host_t* h, bool) {
        from(h)->onMain([](ClapInstance& i) { i.deliverGuiClosed(); });
    }

    static const void* getExtension(const clap_host_t*, const char* id) {
        static const clap_host_log_t kLog{log};
        static const clap_host_thread_check_t kThread{isMain, isAudio};
        static const clap_host_params_t kParams{paramsRescan, paramsClear, paramsRequestFlush};
        static const clap_host_state_t kState{stateDirty};
        static const clap_host_latency_t kLatency{latencyChanged};
        static const clap_host_audio_ports_t kPorts{portsFlagSupported, portsRescan};
        static const clap_host_timer_support_t kTimer{registerTimer, unregisterTimer};
#ifndef _WIN32
        static const clap_host_posix_fd_support_t kFd{registerFd, modifyFd, unregisterFd};
#endif
        static const clap_host_gui_t kGui{guiResizeHints, guiRequestResize, guiRequestShow, guiRequestHide,
                                          guiClosed};
        if (std::strcmp(id, CLAP_EXT_LOG) == 0) return &kLog;
        if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) return &kThread;
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
        if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
        if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatency;
        if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kPorts;
        if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimer;
#ifndef _WIN32
        if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) return &kFd;
#endif
        if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kGui;
        return nullptr;
    }
    static void requestRestart(const clap_host_t* h) {
        from(h)->onMain([](ClapInstance& i) {
            if (!i.active_) return;
            const double sr = i.sampleRate_;
            const uint32_t mf = i.maxFrames_;
            i.deactivate();
            i.activate(sr, mf);
        });
    }
    static void requestProcess(const clap_host_t*) {}
    static void requestCallback(const clap_host_t* h) {
        from(h)->onMain([](ClapInstance& i) {
            if (i.plugin_ != nullptr) i.plugin_->on_main_thread(i.plugin_);
        });
    }
};

// --- Instancia ---------------------------------------------------------------------------

std::unique_ptr<ClapInstance> ClapInstance::create(std::shared_ptr<ClapLibrary> lib,
                                                   const std::string& pluginId, HostServices* services,
                                                   std::string* error) {
    if (!lib) return nullptr;
    const PluginDesc* d = lib->find(pluginId);
    if (d == nullptr) {
        if (error != nullptr) *error = "no existe el plugin " + pluginId;
        return nullptr;
    }
    std::unique_ptr<ClapInstance> inst(new ClapInstance());
    inst->lib_ = lib;
    inst->desc_ = *d;
    inst->services_ = services;
    inst->host_ = std::make_unique<Host>();
    Host& h = *inst->host_;
    h.inst = inst.get();
    h.mainThread = std::this_thread::get_id();
    h.host.clap_version = CLAP_VERSION;
    h.host.host_data = &h;
    h.host.name = "Broadcast Appliance";
    h.host.vendor = "Legis Studio";
    h.host.url = "";
    h.host.version = "2.0";
    h.host.get_extension = Host::getExtension;
    h.host.request_restart = Host::requestRestart;
    h.host.request_process = Host::requestProcess;
    h.host.request_callback = Host::requestCallback;
    inst->plugin_ = lib->factory()->create_plugin(lib->factory(), &h.host, pluginId.c_str());
    if (inst->plugin_ == nullptr || !inst->plugin_->init(inst->plugin_)) {
        if (inst->plugin_ != nullptr) inst->plugin_->destroy(inst->plugin_);
        inst->plugin_ = nullptr;
        if (error != nullptr) *error = "no se pudo crear " + pluginId;
        return nullptr;
    }
    // Puertos de audio.
    auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        inst->plugin_->get_extension(inst->plugin_, CLAP_EXT_AUDIO_PORTS));
    for (bool isIn : {true, false}) {
        auto& list = isIn ? inst->inputs_ : inst->outputs_;
        const uint32_t n = ports != nullptr ? ports->count(inst->plugin_, isIn) : 0;
        for (uint32_t i = 0; i < n && i < 8; ++i) {
            clap_audio_port_info_t pi{};
            if (!ports->get(inst->plugin_, i, isIn, &pi)) continue;
            list.push_back({pi.name, pi.channel_count, (pi.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0});
        }
    }
    return inst;
}

ClapInstance::~ClapInstance() {
    if (plugin_ == nullptr) return;
    closeEditor();
    deactivate(true); // quien lo destruye ya lo saco del audio
    *host_->alive = false;
    {
        std::lock_guard<std::mutex> lk(host_->timersLock);
        for (auto& [id, sid] : host_->timers)
            if (services_ != nullptr) services_->removeTimer(sid);
        host_->timers.clear();
    }
    plugin_->destroy(plugin_);
    plugin_ = nullptr;
}

bool ClapInstance::activate(double sampleRate, uint32_t maxFrames) {
    if (plugin_ == nullptr) return false;
    if (active_) return true;
    sampleRate_ = sampleRate;
    maxFrames_ = maxFrames;
    active_ = plugin_->activate(plugin_, sampleRate, 1, maxFrames);
    return active_;
}

void ClapInstance::deactivate(bool audioDetached) {
    if (plugin_ == nullptr || !active_) return;
    // Detener el procesamiento en el hilo de audio (lo pide CLAP). Si el audio
    // no esta corriendo (fuente callada), se detiene aca: nadie mas lo toca.
    stopRequested_ = true;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(audioDetached ? 0 : 300);
    while (state_.load() == 1 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    int expected = 1;
    if (state_.compare_exchange_strong(expected, 3)) {
        plugin_->stop_processing(plugin_);
        state_ = 0;
    }
    plugin_->deactivate(plugin_);
    active_ = false;
    stopRequested_ = false;
}

std::vector<ParamInfo> ClapInstance::params() const {
    std::vector<ParamInfo> out;
    auto* ext = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    if (ext == nullptr) return out;
    const uint32_t n = ext->count(plugin_);
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        clap_param_info_t pi{};
        if (!ext->get_info(plugin_, i, &pi)) continue;
        ParamInfo p;
        p.id = pi.id;
        p.name = pi.name;
        p.module = pi.module;
        p.min = pi.min_value;
        p.max = pi.max_value;
        p.def = pi.default_value;
        p.flags = pi.flags;
        out.push_back(std::move(p));
    }
    return out;
}

bool ClapInstance::getValue(uint32_t id, double& value) const {
    auto* ext = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    return ext != nullptr && ext->get_value(plugin_, id, &value);
}

std::string ClapInstance::valueText(uint32_t id, double value) const {
    auto* ext = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    char buf[128] = {0};
    if (ext != nullptr && ext->value_to_text(plugin_, id, value, buf, sizeof(buf))) return buf;
    return std::to_string(value);
}

bool ClapInstance::textToValue(uint32_t id, const std::string& text, double& value) const {
    auto* ext = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    return ext != nullptr && ext->text_to_value(plugin_, id, text.c_str(), &value);
}

bool ClapInstance::setValue(uint32_t id, double value) {
    const uint32_t head = qHead_.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1) % kQueue;
    if (next == qTail_.load(std::memory_order_acquire)) return false; // llena
    queue_[head] = {id, value};
    qHead_.store(next, std::memory_order_release);
    if (state_.load() != 1) flushParamsOnMain();
    return true;
}

namespace {

// Lista de eventos de entrada (param value) armada en la pila.
struct EventList {
    const clap_event_param_value_t* events;
    uint32_t count;
};
uint32_t evSize(const clap_input_events_t* l) { return static_cast<const EventList*>(l->ctx)->count; }
const clap_event_header_t* evGet(const clap_input_events_t* l, uint32_t i) {
    return &static_cast<const EventList*>(l->ctx)->events[i].header;
}
bool evPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

} // namespace

void ClapInstance::flushParamsOnMain() {
    if (plugin_ == nullptr) return;
    auto* ext = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    if (ext == nullptr) return;
    int expected = 0;
    if (!state_.compare_exchange_strong(expected, 2)) return; // el audio los entrega
    clap_event_param_value_t evs[kQueue];
    uint32_t n = 0;
    uint32_t tail = qTail_.load(std::memory_order_relaxed);
    while (tail != qHead_.load(std::memory_order_acquire) && n < kQueue) {
        auto& e = evs[n++];
        e = {};
        e.header = {sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = queue_[tail].id;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = queue_[tail].value;
        tail = (tail + 1) % kQueue;
    }
    qTail_.store(tail, std::memory_order_release);
    EventList list{evs, n};
    clap_input_events_t in{&list, evSize, evGet};
    clap_output_events_t out{nullptr, evPush};
    ext->flush(plugin_, &in, &out);
    state_ = 0;
}

std::vector<uint8_t> ClapInstance::saveState() const {
    std::vector<uint8_t> data;
    auto* ext = static_cast<const clap_plugin_state_t*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
    if (ext == nullptr) return data;
    clap_ostream_t os{&data, [](const clap_ostream_t* s, const void* buf, uint64_t size) -> int64_t {
                          auto* v = static_cast<std::vector<uint8_t>*>(s->ctx);
                          const auto* b = static_cast<const uint8_t*>(buf);
                          v->insert(v->end(), b, b + size);
                          return static_cast<int64_t>(size);
                      }};
    if (!ext->save(plugin_, &os)) data.clear();
    return data;
}

bool ClapInstance::loadState(const std::vector<uint8_t>& data) {
    auto* ext = static_cast<const clap_plugin_state_t*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
    if (ext == nullptr || data.empty()) return false;
    struct Reader {
        const std::vector<uint8_t>* v;
        size_t pos;
    } r{&data, 0};
    clap_istream_t is{&r, [](const clap_istream_t* s, void* buf, uint64_t size) -> int64_t {
                          auto* rd = static_cast<Reader*>(s->ctx);
                          const size_t n =
                              std::min<size_t>(static_cast<size_t>(size), rd->v->size() - rd->pos);
                          std::memcpy(buf, rd->v->data() + rd->pos, n);
                          rd->pos += n;
                          return static_cast<int64_t>(n);
                      }};
    return ext->load(plugin_, &is);
}

// --- Audio -----------------------------------------------------------------------------

void ClapInstance::process(float* const* const* in, float* const* const* out, uint32_t frames) {
    // Hilo de audio mientras dura la llamada (la medicion fuera de linea la
    // hace el principal: despues vuelve a ser "principal").
    struct AudioScope {
        bool prev = tl_audioThread;
        AudioScope() { tl_audioThread = true; }
        ~AudioScope() { tl_audioThread = prev; }
    } scope;
    const uint32_t inCh = inputs_.empty() ? 0 : inputs_[0].channels;
    const uint32_t outCh = outputs_.empty() ? 0 : outputs_[0].channels;
    if (plugin_ == nullptr || !active_ || outputs_.empty()) {
        if (!outputs_.empty()) passthrough(in, out, inCh, outCh, frames);
        return;
    }
    if (stopRequested_.load()) {
        int expected = 1;
        if (state_.compare_exchange_strong(expected, 3)) {
            plugin_->stop_processing(plugin_);
            state_ = 0;
        }
        passthrough(in, out, inCh, outCh, frames);
        return;
    }
    int s = state_.load();
    if (s == 0) {
        if (!state_.compare_exchange_strong(s, 1) || !plugin_->start_processing(plugin_)) {
            if (state_.load() == 1) state_ = 0;
            passthrough(in, out, inCh, outCh, frames);
            return;
        }
    } else if (s != 1) {
        passthrough(in, out, inCh, outCh, frames); // flush en curso en el principal
        return;
    }

    clap_event_param_value_t evs[kQueue];
    uint32_t n = 0;
    uint32_t tail = qTail_.load(std::memory_order_relaxed);
    while (tail != qHead_.load(std::memory_order_acquire) && n < kQueue) {
        auto& e = evs[n++];
        e = {};
        e.header = {sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = queue_[tail].id;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        e.value = queue_[tail].value;
        tail = (tail + 1) % kQueue;
    }
    qTail_.store(tail, std::memory_order_release);
    EventList list{evs, n};
    clap_input_events_t inEv{&list, evSize, evGet};
    clap_output_events_t outEv{nullptr, evPush};

    clap_audio_buffer_t ib[8]{};
    clap_audio_buffer_t ob[8]{};
    for (size_t p = 0; p < inputs_.size(); ++p) {
        ib[p].data32 = const_cast<float**>(in[p]);
        ib[p].channel_count = inputs_[p].channels;
    }
    for (size_t p = 0; p < outputs_.size(); ++p) {
        ob[p].data32 = const_cast<float**>(out[p]);
        ob[p].channel_count = outputs_[p].channels;
    }
    clap_process_t proc{};
    proc.steady_time = steadyTime_;
    proc.frames_count = frames;
    proc.transport = nullptr;
    proc.audio_inputs = ib;
    proc.audio_outputs = ob;
    proc.audio_inputs_count = static_cast<uint32_t>(inputs_.size());
    proc.audio_outputs_count = static_cast<uint32_t>(outputs_.size());
    proc.in_events = &inEv;
    proc.out_events = &outEv;
    if (plugin_->process(plugin_, &proc) == CLAP_PROCESS_ERROR) passthrough(in, out, inCh, outCh, frames);
    steadyTime_ += frames;
}

void ClapInstance::audioThreadIdle() {
    tl_audioThread = true;
    if (!stopRequested_.load()) return;
    int expected = 1;
    if (state_.compare_exchange_strong(expected, 3)) {
        plugin_->stop_processing(plugin_);
        state_ = 0;
    }
}

// --- Editor ------------------------------------------------------------------------------

bool ClapInstance::hasEditor() const {
    auto* gui = static_cast<const clap_plugin_gui_t*>(plugin_->get_extension(plugin_, CLAP_EXT_GUI));
    if (gui == nullptr) return false;
#ifdef _WIN32
    return gui->is_api_supported(plugin_, CLAP_WINDOW_API_WIN32, false);
#else
    return gui->is_api_supported(plugin_, CLAP_WINDOW_API_X11, false);
#endif
}

bool ClapInstance::openEditor(const std::string& title, std::string* error) {
    if (editor_) {
        editor_->raise();
        return true;
    }
    editor_ = ClapEditor::open(*this, title, error);
    return editor_ != nullptr;
}

void ClapInstance::closeEditor() { editor_.reset(); }

bool ClapInstance::editorOpen() const { return editor_ != nullptr; }

void ClapInstance::deliverGuiClosed() { editor_.reset(); }

} // namespace ba::app::audio
