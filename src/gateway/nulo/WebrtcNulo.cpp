// ============================================================================
// src/gateway/nulo/WebrtcNulo.cpp — Webrtc.hpp sin libwebrtc (el backend NULL)
//
// La otra implementación de Webrtc.hpp (la de verdad es webrtc/Webrtc.cpp). Cuál va en cada
// programa lo decide qué biblioteca enlaza (src/gateway/CMakeLists.txt): los tests de lógica
// del gateway enlazan ba_gateway_nucleo + ba_gateway_nulo; ba-gateway, ba_gateway_nucleo +
// ba_gateway_webrtc (o este, en un build sin el SDK).
//
// Es una máquina de estados que completa la señalización (offer -> answer) sin RTP, para
// probar el servidor sin el SDK: onRemoteAudio/onRemoteVideo no se disparan, no hay
// espectadores ni retorno compartido, CuadroSaliente no convierte (solo lleva el tamaño y
// cuenta igual).
// ============================================================================

#include "Webrtc.hpp"
#include "EstadoCapas.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::gateway {

using json = nlohmann::json;

struct WebrtcSession::Impl {
    int slot = 0;
    SessionCallbacks cb;
    bool configured = false;
    bool connected = false;
    bool closed = false;
    std::atomic<uint64_t> localAudioBlocks{0};
    std::atomic<uint64_t> localVideoFrames{0};
    EstadoCapas capas;
    std::atomic<int> topeRetorno{0};

    bool configure(const SessionCallbacks& c) {
        cb = c;
        configured = true;
        capas.reiniciar();
        topeRetorno = 0;
        // configure() se llama de nuevo en cada reconexion: se limpia el estado
        // para que la sesion no quede muerta tras el primer cierre.
        closed = false;
        connected = false;
        localAudioBlocks.store(0);
        localVideoFrames.store(0);
        return true;
    }
    bool setRemoteOffer(const std::string&) {
        if (!configured || closed) return false;
        const bool first = !connected;
        connected = true;
        // Answer minimo coherente (el backend real lo genera libwebrtc).
        if (cb.onLocalAnswer) {
            cb.onLocalAnswer("v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
                             "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\na=rtpmap:111 opus/48000/2\r\n"
                             "m=video 9 UDP/TLS/RTP/SAVPF 96\r\na=rtpmap:96 VP8/90000\r\n");
        }
        if (first && cb.onStateChange) cb.onStateChange(true);
        return true;
    }
    bool addRemoteIce(const std::string&) { return configured && !closed; }
    void pushLocalAudio(const float*, size_t n, int) {
        if (n > 0 && connected) localAudioBlocks.fetch_add(1, std::memory_order_relaxed);
    }
    void pushLocalVideo() {
        if (connected) localVideoFrames.fetch_add(1, std::memory_order_relaxed);
    }
    void close() {
        if (closed) return;
        closed = true;
        const bool was = connected;
        connected = false;
        if (was && cb.onStateChange) cb.onStateChange(false);
    }
    std::string estadisticas() { return "{}"; }
};

// ============================================================================
// CuadroSaliente (F2-13: una conversión por cuadro, no una por sesión)
// ============================================================================

namespace {
std::atomic<uint64_t> g_conversiones{0};
std::atomic<uint64_t> g_bytesConvertidos{0};
} // namespace

struct CuadroSaliente::Impl {
    int ancho = 0, alto = 0;
};

struct FlujoRetorno::Impl {
};

struct AudioCompartido::Impl {
};

uint64_t AudioCompartido::codificar(const float* pcm, size_t n, int rate) const {
    (void)pcm;
    (void)n;
    (void)rate;
    return 0;
}

CuadroSaliente FlujoRetorno::emitir(const CuadroSaliente& c) const {
    return c;
}

CuadroSaliente CuadroSaliente::desdeBgra(const uint8_t* bgra, int ancho, int alto, int stride) {
    CuadroSaliente c;
    if (bgra == nullptr || ancho <= 0 || alto <= 0) return c;
    auto impl = std::make_shared<Impl>();
    impl->ancho = ancho;
    impl->alto = alto;
    (void)stride;
    g_conversiones.fetch_add(1, std::memory_order_relaxed);
    const uint64_t croma = static_cast<uint64_t>((ancho + 1) / 2) * static_cast<uint64_t>((alto + 1) / 2);
    g_bytesConvertidos.fetch_add(static_cast<uint64_t>(ancho) * static_cast<uint64_t>(alto) + 2 * croma,
                                 std::memory_order_relaxed);
    c.impl_ = std::move(impl);
    return c;
}

int CuadroSaliente::ancho() const { return impl_ ? impl_->ancho : 0; }
int CuadroSaliente::alto() const { return impl_ ? impl_->alto : 0; }
uint64_t CuadroSaliente::conversiones() { return g_conversiones.load(std::memory_order_relaxed); }
uint64_t CuadroSaliente::bytesConvertidos() { return g_bytesConvertidos.load(std::memory_order_relaxed); }

// ============================================================================
// WebrtcSession (API comun)
// ============================================================================

WebrtcSession::WebrtcSession(int slot, int, int, int, int) : impl_(new Impl()) { impl_->slot = slot; }
WebrtcSession::WebrtcSession() : impl_(new Impl()) {}
WebrtcSession::~WebrtcSession() {
    if (impl_ != nullptr) {
        impl_->close();
        delete impl_;
    }
}
WebrtcSession::WebrtcSession(WebrtcSession&& o) noexcept : impl_(o.impl_) { o.impl_ = nullptr; }
WebrtcSession& WebrtcSession::operator=(WebrtcSession&& o) noexcept {
    if (this != &o) {
        if (impl_ != nullptr) {
            impl_->close();
            delete impl_;
        }
        impl_ = o.impl_;
        o.impl_ = nullptr;
    }
    return *this;
}

bool WebrtcSession::configure(const SessionCallbacks& cb, void* factory) {
    (void)factory;
    return impl_ != nullptr && impl_->configure(cb);
}
bool WebrtcSession::setRemoteOffer(const std::string& sdp) {
    return impl_ != nullptr && impl_->setRemoteOffer(sdp);
}
bool WebrtcSession::addRemoteIce(const std::string& c) { return impl_ != nullptr && impl_->addRemoteIce(c); }
void WebrtcSession::setCapas(const std::string& midAlta, const std::string& midBaja) {
    if (impl_ != nullptr) impl_->capas.ponerMids(midAlta, midBaja);
}
void WebrtcSession::pedirCapa(Capa c) {
    if (impl_ == nullptr) return;
    CapasActivas a;
    if (impl_->capas.pedir(c, a) && impl_->cb.onCapas) impl_->cb.onCapas(a.alta, a.baja);
}
void WebrtcSession::setFundidoMs(int ms) {
    if (impl_ != nullptr) impl_->capas.setFundidoMs(ms);
}
bool WebrtcSession::conCapas() const { return impl_ != nullptr && impl_->capas.con(); }
Capa WebrtcSession::capaSale() const { return impl_ != nullptr ? impl_->capas.sale() : Capa::Nada; }
Capa WebrtcSession::capaPedida() const { return impl_ != nullptr ? impl_->capas.pedida() : Capa::Nada; }
CapasActivas WebrtcSession::capasActivas() const {
    return impl_ != nullptr ? impl_->capas.activas() : CapasActivas{};
}
void WebrtcSession::pushLocalAudio(const float* s, size_t n, int rate) {
    if (impl_ != nullptr) impl_->pushLocalAudio(s, n, rate);
}
void WebrtcSession::pushLocalAudio(const float* s, size_t n, int rate, const AudioCompartido& grupo, uint64_t bloque) {
    if (impl_ == nullptr) return;
    (void)grupo;
    (void)bloque;
    impl_->pushLocalAudio(s, n, rate);
}
void WebrtcSession::pushLocalVideo(const uint8_t* p, size_t bytes, int w, int h) {
    if (impl_ == nullptr || p == nullptr || w <= 0 || h <= 0 || bytes < static_cast<size_t>(w) * h * 4u) return;
    if (!impl_->connected) return; // sin nadie del otro lado no se convierte nada
    pushLocalVideo(CuadroSaliente::desdeBgra(p, w, h));
}
void WebrtcSession::pushLocalVideo(const CuadroSaliente& c) {
    if (impl_ == nullptr || c.vacio()) return;
    impl_->pushLocalVideo();
}
bool WebrtcSession::connected() const { return impl_ != nullptr && impl_->connected; }
void WebrtcSession::close() {
    if (impl_ != nullptr) impl_->close();
}
int WebrtcSession::slot() const { return impl_ != nullptr ? impl_->slot : 0; }
void WebrtcSession::setTopeRetorno(int alto) {
    if (impl_ == nullptr) return;
    impl_->topeRetorno.store(std::max(0, alto), std::memory_order_relaxed);
}
int WebrtcSession::topeRetorno() const {
    if (impl_ == nullptr) return 0;
    return impl_->topeRetorno.load(std::memory_order_relaxed);
}
std::string WebrtcSession::estadisticas() {
    if (impl_ == nullptr) return "{}";
    std::string s = impl_->estadisticas();
    const bool conRetorno = false;
    if (!impl_->capas.con() && !conRetorno) return s;
    json j = json::parse(s, nullptr, false);
    if (!j.is_object()) j = json::object();
    if (impl_->capas.con()) {
        // Con dos capas: cuál sale y cuál pidió la sala (para ver que lo que se pide llega).
        j["capa"] = nombreCapa(impl_->capas.sale());
        j["capa_pedida"] = nombreCapa(impl_->capas.pedida());
    }
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}
uint64_t WebrtcSession::localAudioBlocks() const {
    return impl_ != nullptr ? impl_->localAudioBlocks.load() : 0;
}
uint64_t WebrtcSession::localVideoFrames() const {
    return impl_ != nullptr ? impl_->localVideoFrames.load() : 0;
}

// ============================================================================
// WebrtcHub
// ============================================================================

WebrtcHub::WebrtcHub(int guests, int videoW, int videoH, int audioRate, int audioChannels) : guests_(guests) {
    sessions_.resize(static_cast<size_t>(guests) + 1);
    for (int s = 1; s <= guests; ++s)
        sessions_[static_cast<size_t>(s)] = WebrtcSession(s, videoW, videoH, audioRate, audioChannels);
}

WebrtcHub::~WebrtcHub() {
    closeAll();
}

void WebrtcHub::setNetwork(uint16_t minPort, uint16_t maxPort, bool allowLoopback, const std::vector<std::string>& ignorar) {
    (void)minPort;
    (void)maxPort;
    (void)allowLoopback;
    (void)ignorar;
}

std::vector<turn::Bucle*> WebrtcHub::buclesRed() const {
    std::vector<turn::Bucle*> v;
    return v;
}

memoria::Conmutador* WebrtcHub::conmutador() const {
    return nullptr;
}

void WebrtcHub::conectarTunel(net::IpStack* pila) {
    (void)pila;
}

nlohmann::json WebrtcHub::capasEstado() const {
    return nlohmann::json::object();
}

WebrtcSession* WebrtcHub::session(int idx) {
    if (idx < 1 || idx > guests_) return nullptr;
    return &sessions_[static_cast<size_t>(idx)];
}

FlujoRetorno WebrtcHub::flujoRetorno(const std::string& nombre) {
    FlujoRetorno out;
    (void)nombre;
    return out;
}

AudioCompartido WebrtcHub::audioCompartido(const std::string& nombre) {
    AudioCompartido out;
    (void)nombre;
    return out;
}

nlohmann::json WebrtcHub::audioCompartidoEstado() const {
    return nlohmann::json::array();
}

nlohmann::json WebrtcHub::retornoCompartido() const {
    nlohmann::json out = nlohmann::json::array();
    return out;
}

nlohmann::json WebrtcHub::codecsVideo() const {
    return nlohmann::json::object();
}

void WebrtcHub::closeAll() {
    for (auto& s : sessions_)
        s.close();
}

// ============================================================================
// Espectadores (relay de video codificado)
// ============================================================================

// Backend NULL: sin RTP no hay nada que reenviar.
int WebrtcHub::openViewer(int, bool, const std::string&, std::string&) { return 0; }
void WebrtcHub::closeViewer(int) {}
void WebrtcHub::closeViewersOf(int) {}
void WebrtcHub::requestKeyFrame(int) {}
void WebrtcHub::reapViewers() {}
WebrtcHub::Trafico WebrtcHub::trafico() const { return {}; }

int WebrtcHub::viewerCount() const { return 0; }
int WebrtcHub::espectadoresDe(int) const { return 0; }
WebrtcHub::EntradaCamara WebrtcHub::entradaCamara(int) const { return {}; }

const char* nombreBackendRtc() { return "NULL"; }

} // namespace ba::gateway
