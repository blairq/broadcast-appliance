// ============================================================================
// src/app/emision/EmisionSalidas.cpp — ver EmisionSalidas.hpp
// ============================================================================

#include "emision/EmisionSalidas.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <set>
#include <tuple>

#include "Motor.hpp"
#include "Pedido.hpp"
#include "Salidas.hpp"
#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"
#include "emision/EmisorAparte.hpp"
#include "emision/EscenasYFuentes.hpp"
#include "emision/MezclaApp.hpp"
#include "emision/VigiaDisco.hpp"
#include "i18n/I18n.hpp"

namespace ba::app {

using json = nlohmann::json;

namespace {

// Lo que hizo el último startStreaming DE CADA HILO (como lastError).
thread_local BroadcastEngine::Inicio t_inicio = BroadcastEngine::Inicio::Iniciado;

std::string enmascarar(const std::string& server, const std::string& key) {
    std::string s = server;
    if (!key.empty()) s += " (clave …" + (key.size() > 4 ? key.substr(key.size() - 4) : key) + ")";
    return s;
}

/// La dirección completa para el motor: en RTMP la clave va al final; en SRT, como passphrase.
std::string urlConClave(const std::string& server, const std::string& key) {
    if (key.empty()) return server;
    if (server.rfind("srt://", 0) == 0)
        return server + (server.find('?') == std::string::npos ? "?" : "&") + "passphrase=" + key;
    return server + (server.empty() || server.back() == '/' ? "" : "/") + key;
}

std::string marcaDeTiempo() {
    return std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

/// Lo que leen las salidas del programa de una salida: su video y su audio (pista 1
/// y las pedidas: las pistas 2-6 de ESA salida, con su mezcla; ver Motor).
motor::Fuentes fuentesDelPrograma(motor::Motor& m, const std::string& salida, const std::vector<int>& pistasExtra,
                                  bool limpio = false) {
    motor::Fuentes f;
    f.video = m.productorDeVideo(limpio ? "limpio" : "programa", &f.latVideoNs, salida);
    std::int64_t lat = 0;
    f.audios.push_back(m.productorDeBus("programa", &lat, salida));
    f.latAudioNs = lat;
    for (int p : pistasExtra) {
        std::int64_t l = 0;
        const std::string prod = m.productorDeBus("pista" + std::to_string(p), &l, salida);
        if (!prod.empty()) {
            f.audios.push_back(prod);
            f.latAudioNs = std::max(f.latAudioNs, l);
        }
    }
    return f;
}

/// La transmisión a `url` con la codificación de `enc` (la principal y los destinos extra:
/// con el mismo perfil comparten el codificador de la salida).
motor::Transmision::Config configTransmision(const std::string& url, const EncoderConfig& enc, const VideoConfig& video,
                                             std::vector<std::string> codificadores) {
    motor::Transmision::Config c;
    c.url = url;
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, video.fpsNum / std::max(1, video.fpsDen));
    c.codificadores = std::move(codificadores);
    c.control = motor::controlTasaDe(enc.rateControl);
    return c;
}

/// Los bitrates de la grabación del programa: «archivo» graba al triple que la transmisión.
std::pair<int, int> kbpsVideoAudio(const RecordConfig& rec, const EncoderConfig& enc) {
    const bool archivo = rec.calidad == "archivo";
    return {archivo ? enc.videoBitrateKbps * 3 : enc.videoBitrateKbps, std::max(enc.audioBitrateKbps, archivo ? 256 : 160)};
}

/// Sale algo de la foto de una salida (ver fotoDe).
bool saleAlgo(const json& f, bool contarGrabacion) {
    if (f.value("principal", json::object()).value("andando", false)) return true;
    const json extras = f.value("extras", json::object()); // con nombre: .items() de un temporal lo usa destruido
    for (const auto& [id, x] : extras.items())
        if (x.value("andando", false)) return true;
    // La señal limpia se transmite (tiene destino) o se graba.
    const json limpio = f.value("limpio", json::object());
    if (limpio.value("activa", false) && (contarGrabacion || !limpio.value("destino", std::string()).empty()))
        return true;
    return contarGrabacion && f.value("grabacion", json::object()).value("activa", false);
}

} // namespace

// Acá Grabacion, Transmision, Repeticion y Pedido ya están completos.
SalidaEmision::SalidaEmision() = default;
SalidaEmision::~SalidaEmision() = default;
GrabacionAparte::GrabacionAparte() = default;
GrabacionAparte::GrabacionAparte(GrabacionAparte&&) noexcept = default;
GrabacionAparte& GrabacionAparte::operator=(GrabacionAparte&&) noexcept = default;
GrabacionAparte::~GrabacionAparte() = default;

struct EmisionSalidas::EstadoRemux {
    std::mutex mu;
    std::string archivo, destino, error;
    std::atomic<float> pct{0};
    std::atomic<bool> andando{false}, ok{false};
};

EmisionSalidas::EmisionSalidas(BroadcastEngine& m) : m_(m) {}

EmisionSalidas::~EmisionSalidas() = default;

// ============================================================================
// El hilo de salidas (docs/planes/PLAN-MOTOR-SIN-ESPERAS.md, S4)
// ============================================================================

void EmisionSalidas::arrancar() {
    {
        std::lock_guard<std::mutex> l(colaM_);
        seguir_ = true;
    }
    hilo_ = std::thread([this]() { bucle(); });
}

void EmisionSalidas::enSalidas(const std::string& que, std::function<std::string()> f) {
    {
        std::lock_guard<std::mutex> l(colaM_);
        cola_.emplace_back(que, std::move(f));
    }
    colaCv_.notify_one();
}

void EmisionSalidas::alta(const std::shared_ptr<SalidaEmision>& s) {
    if (!andando()) {
        emisiones_.push_back(s);
        return;
    }
    enSalidas("alta", [this, s]() {
        emisiones_.push_back(s);
        return std::string();
    });
}

void EmisionSalidas::baja(const std::shared_ptr<SalidaAire>& s) {
    // Lo pedido de ella, en nada (un «no arrancó» que llegue tarde ya no la encuentra).
    s->stream.poner(false, ordenes_);
    s->grabacion.poner(false, ordenes_);
    s->limpio.poner(false, ordenes_);
    s->extras.clear();
    // Lo que sale de ella se para en el hilo de salidas, después de lo que ya estaba encolado,
    // y ahí también se suelta su canal de la señal limpia al emisor (es de ese hilo).
    const std::shared_ptr<SalidaEmision> e = s->emision;
    const auto sacar = [this, e]() {
        m_.emisor_->soltarCanal(e->canalLimpioEmisor, true);
        emisiones_.erase(std::remove(emisiones_.begin(), emisiones_.end(), e), emisiones_.end());
    };
    if (!andando()) return sacar();
    enSalidas("borrarSalida", [this, e, sacar]() {
        pararTodoYa(*e);
        sacar();
        return std::string();
    });
}

void EmisionSalidas::quedarseConLaPorOmision() {
    emisiones_.erase(std::remove_if(emisiones_.begin(), emisiones_.end(), [](const auto& e) { return !e->porOmision(); }),
                     emisiones_.end());
}

void EmisionSalidas::atenderBuzon() {
    for (const MensajeSalida& m : buzon_.tomar()) {
        if (m.tipo == MensajeSalida::Tipo::AparteNoArranco) {
            const auto it = individualesPedidas_.find(m.id);
            if (it != individualesPedidas_.end() && it->second == m.orden) individualesPedidas_.erase(it);
            continue;
        }
        const auto it = m_.salidas_.find(m.salida);
        if (it == m_.salidas_.end()) continue; // se borró mientras tanto
        SalidaAire& s = *it->second;
        // Lo que no arrancó deja de estar pedido, si sigue siendo esa orden.
        switch (m.tipo) {
            case MensajeSalida::Tipo::NoTransmite: s.stream.noArranco(m.orden); break;
            case MensajeSalida::Tipo::NoGraba: s.grabacion.noArranco(m.orden); break;
            case MensajeSalida::Tipo::NoLimpia: s.limpio.noArranco(m.orden); break;
            case MensajeSalida::Tipo::ExtraNoArranco:
                if (const auto x = s.extras.find(m.id); x != s.extras.end() && x->second == m.orden) s.extras.erase(x);
                break;
            case MensajeSalida::Tipo::ExtraAdoptado:
                if (s.extras.count(m.id) == 0) s.extras[m.id] = ++ordenes_;
                break;
            case MensajeSalida::Tipo::LimpioAdoptado:
                if (!s.limpio.si) s.limpio.poner(true, ordenes_);
                break;
            case MensajeSalida::Tipo::Aac: m_.mezcla_->alAac(s, m.kbps); break;
            case MensajeSalida::Tipo::AparteNoArranco: break;
        }
    }
}

void EmisionSalidas::contestarComoLaAnterior(const std::shared_ptr<SalidaEmision>& s, const std::string& que,
                                             AlResultado alTerminar) {
    if (!alTerminar) return;
    enSalidas("como_la_anterior", [s, que, alTerminar = std::move(alTerminar)]() {
        // Acá ya corrió la orden de antes (la cola es en orden): lo que arrancó, si sigue.
        const bool sigue = que == "grabar" ? s->grabacionViva : s->transmisionViva;
        std::string fallo = que == "grabar" ? s->falloGrabacion : s->falloTransmision;
        if (!sigue && fallo.empty()) fallo = ba::i18n::traducir("emision", "se paró antes de arrancar");
        alTerminar({s->id, que, sigue, sigue ? std::string() : fallo, sigue && que == "grabar" ? s->recordPath : std::string()});
        return std::string(); // la falla, si hubo, ya la contó la orden de antes
    });
}

void EmisionSalidas::bucle() {
    for (;;) {
        std::deque<std::pair<std::string, std::function<std::string()>>> ordenes;
        bool seguir = true;
        {
            std::unique_lock<std::mutex> l(colaM_);
            colaCv_.wait_for(l, std::chrono::milliseconds(100), [this]() { return !cola_.empty() || !seguir_; });
            ordenes.swap(cola_);
            seguir = seguir_;
        }
        for (auto& [que, f] : ordenes) {
            const std::string e = f();
            if (!e.empty()) m_.avisarFallo(e);
        }
        if (!seguir && ordenes.empty()) break;
        tic();
        publicarFoto();
    }
    publicarFoto();
}

void EmisionSalidas::detener() {
    if (!hilo_.joinable()) return;
    // Lo que quede andando se para EN el hilo de salidas (después de lo encolado).
    enSalidas("apagar", [this]() {
        for (auto& [n, r] : sourceRecs_)
            if (r.grab) r.grab->parar();
        sourceRecs_.clear();
        for (const auto& s : emisiones_) pararTodoYa(*s);
        return std::string();
    });
    {
        std::lock_guard<std::mutex> l(colaM_);
        seguir_ = false;
    }
    colaCv_.notify_one();
    hilo_.join();
    // El hilo paró: todo es del dueño. Lo que contó (el AAC) se atiende; lo pedido, en nada.
    atenderBuzon();
    for (const auto& s : m_.lasSalidas()) {
        s->stream.poner(false, ordenes_);
        s->grabacion.poner(false, ordenes_);
        s->limpio.poner(false, ordenes_);
        s->extras.clear();
    }
    for (const auto& e : emisiones_) e->grabacionViva = e->transmisionViva = false;
    individualesPedidas_.clear();
}

void EmisionSalidas::pararTodoYa(SalidaEmision& s) {
    if (s.clean) {
        if (s.clean->tx) s.clean->tx->parar();
        if (s.clean->grab) s.clean->grab->parar();
        s.clean.reset();
    }
    for (auto& [id, x] : s.extras)
        if (x.tx) x.tx->parar();
    s.extras.clear();
    if (s.stream) s.stream->parar();
    s.stream.reset();
    s.transmisionViva = false;
    // Lo que sale por el emisor aparte no se para acá: apagar o rearmar el motor no
    // corta el aire (lo para el operador, o el cierre ordenado del launcher).
    if (!s.emisor) {
        stopRecordingYa(s);
        s.grabacionViva = false;
    }
    s.replay.reset();
}

// Una sola atención: cada salida (y cada codificador que lee, dentro de su tic) se
// rearma sola acá, cada 100 ms: la transmisión reconecta, la grabación sigue en otra
// parte, el codificador que falló pasa a la lista negra y se sigue con otro.
void EmisionSalidas::tic() {
    for (const auto& s : emisiones_) {
        if (s->emisor) m_.emisor_->leer(*s); // lo que hace su emisor (cada medio segundo)
        if (s->stream) s->stream->tic();
        for (auto& [id, x] : s->extras)
            if (x.tx) x.tx->tic();
        if (s->rec) s->rec->tic();
        if (s->recCopia) s->recCopia->tic();
        if (s->replay) s->replay->rep->tic();
        if (s->clean) {
            if (s->clean->grab) s->clean->grab->tic();
            if (s->clean->tx) s->clean->tx->tic();
        }
    }
    for (auto& [n, r] : sourceRecs_) {
        if (r.grab) r.grab->tic();
        if (r.tx) r.tx->tic();
    }
    m_.disco_->vigilar();
}

// ============================================================================
// La foto de las salidas
// ============================================================================

nlohmann::json EmisionSalidas::fotoSalidas() const {
    std::lock_guard<std::mutex> l(fotoSalidasM_);
    return fotoSalidas_;
}

json EmisionSalidas::fotoDe(const SalidaEmision& s) const {
    json f = json::object();
    f["principal"] = s.stream ? s.stream->estado() : json::object();
    json ex = json::object();
    for (const auto& [id, x] : s.extras) {
        json st = x.tx ? x.tx->estado() : json::object();
        st["destino"] = enmascarar(x.server, x.key);
        st["salida"] = s.id;
        ex[id] = std::move(st);
    }
    f["extras"] = std::move(ex);
    // El archivo en curso: el último tramo (con la grabación partida, o seguida en otra parte o carpeta).
    std::string archivo = s.recordPath, copia = s.recordCopyPath;
    if (s.rec)
        if (const auto a = s.rec->archivos(); !a.empty()) archivo = a.back();
    if (s.recCopia)
        if (const auto a = s.recCopia->archivos(); !a.empty()) copia = a.back();
    const json eg = s.rec ? s.rec->estado() : json::object();
    f["grabacion"] = {{"activa", eg.value("grabando", false)},
                      {"caida", eg.value("caida", false)}, // pedida y cortada: se está rearmando
                      {"estado", eg},
                      {"archivo", archivo},
                      {"copia", copia},
                      {"copia_activa", s.recCopia && s.recCopia->grabando()}};
    // Perdió un codificador en el aire: sigue con otro (la lista negra de su video).
    f["codificador_respaldo"] = !s.fuenteVideo.empty() && !motor::fabricasCaidas(s.fuenteVideo).empty();
    std::string ultimo;
    if (s.replay) {
        std::lock_guard<std::mutex> l(s.replay->mu);
        ultimo = s.replay->ultimo;
    }
    f["replay"] = {{"activa", s.replay && s.replay->rep->andando()},
                   {"segundos", s.replay ? s.replay->rep->segundos() : 0},
                   {"ultimo", ultimo}};
    if (s.clean) {
        const json st = s.clean->tx ? s.clean->tx->estado() : json::object();
        f["limpio"] = {{"activa", s.clean->tx ? st.value("andando", false) : (s.clean->grab && s.clean->grab->grabando())},
                       {"reconectando", st.value("reconectando", false)},
                       {"destino", s.clean->destino},
                       {"archivo", s.clean->path},
                       {"cuadros", st.value("cuadros", 0)},
                       {"perdidos", st.value("perdidos", 0)}};
    } else {
        f["limpio"] = {{"activa", false}};
    }
    if (s.emisor) m_.emisor_->fotoDel(s, f); // su transmisión y su grabación, en el otro proceso
    return f;
}

json EmisionSalidas::fotoDeSalida(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return json::object();
    return fotoSalidas().value("salidas", json::object()).value(s->id, json::object());
}

void EmisionSalidas::publicarFoto() {
    // Con algo al aire, la latencia del estudio no baja sola (bajarla mueve la línea de tiempo
    // de todas las salidas): solo sube si hace falta; sin nada al aire, se acomoda sola.
    {
        bool aire = false;
        for (const auto& s : emisiones_) {
            // La grabación cortada que se está rearmando también es «al aire».
            aire = aire || (s->stream && s->stream->estado().value("andando", false)) || (s->rec && (s->rec->grabando() || s->rec->caida()));
            aire = aire || (s->emisor && m_.emisor_->aireEn(*s));
            for (const auto& [id, x] : s->extras) aire = aire || (x.tx && x.tx->estado().value("andando", false));
        }
        alAire_ = aire;
        if (m_.motor_) m_.motor_->permitirBajarLatencia(!aire);
    }
    // Cada salida en `salidas`; afuera, lo de siempre: el de la por omisión (y los
    // destinos extra de todas, que tienen ids únicos y dicen de qué salida son).
    json porSalida = json::object(), extras = json::object();
    bool grabaAlguna = false;
    for (const auto& s : emisiones_) {
        json fs = fotoDe(*s);
        for (const auto& [id, x] : fs["extras"].items()) extras[id] = x;
        grabaAlguna = grabaAlguna || fs["grabacion"].value("activa", false);
        porSalida[s->id] = std::move(fs);
    }
    json f = porSalida.value(std::string(BroadcastEngine::kSalidaPorOmision), json::object());
    f["extras"] = std::move(extras);
    f["salidas"] = std::move(porSalida);
    json ind = json::array();
    for (const auto& [n, r] : sourceRecs_)
        ind.push_back({{"nombre", n}, {"path", r.path}, {"audio_propio", r.audioPropio}, {"grabando", r.grab && r.grab->grabando()},
                       {"bytes", r.grab ? r.grab->estado().value("bytes", std::uint64_t{0}) : std::uint64_t{0}},
                       {"pausada", r.grab && r.grab->pausada()}});
    f["individuales"] = std::move(ind);
    f["codificaciones"] = motor::codificacionesAbiertas(); // las sesiones de codificador abiertas, de todas las salidas
    m_.disco_->medirRestante();
    f["disco"] = m_.disco_->estado(grabaAlguna);
    std::lock_guard<std::mutex> l(fotoSalidasM_);
    fotoSalidas_ = std::move(f);
}

bool EmisionSalidas::haySalida(bool contarGrabacion) const {
    const json todas = fotoSalidas().value("salidas", json::object());
    for (const auto& [id, f] : todas.items())
        if (saleAlgo(f, contarGrabacion)) return true;
    return false;
}

bool EmisionSalidas::haySalidaEn(const std::string& salida, bool contarGrabacion) const {
    return saleAlgo(fotoDeSalida(salida), contarGrabacion);
}

bool EmisionSalidas::outputsActive() {
    // Lo pedido (las salidas son del hilo de salidas: ver enSalidas), en todas las salidas.
    atenderBuzon();
    for (const auto& [id, s] : m_.salidas_)
        if (s->stream.si || s->grabacion.si || !s->extras.empty()) return true;
    return false;
}

// ============================================================================
// Transmisión y destinos
// ============================================================================

BroadcastEngine::Inicio EmisionSalidas::ultimoInicio() const { return t_inicio; }

std::string EmisionSalidas::destinoActivo(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return {};
    return enmascarar(s->activeServer, s->activeKey);
}

void EmisionSalidas::anotarAac(SalidaEmision& s, int kbps) {
    const int antes = s.kbpsAac;
    if (kbps <= 0 || (antes > 0 && antes <= kbps)) return;
    s.kbpsAac = kbps;
    // El techo del loudness lo pone el dueño (MezclaApp::alAac).
    contarAlDueno({MensajeSalida::Tipo::Aac, s.id, {}, 0, kbps});
}

bool EmisionSalidas::startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente,
                                    const std::string& salida, AlResultado alTerminar) {
    atenderBuzon();
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "startStreaming sin startup"));
        t_inicio = Inicio::Error;
        return false;
    }
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) {
        t_inicio = Inicio::Error;
        return false;
    }
    const bool estaba = s->stream.si;
    if (estaba) {
        const bool mismo = (stream.server.empty() || stream.server == s->activeServer) && (stream.key.empty() || stream.key == s->activeKey);
        if (mismo) {
            t_inicio = Inicio::YaIgual;
            contestarComoLaAnterior(s->emision, "transmitir", std::move(alTerminar));
            return true;
        }
        if (!cambiarEnCaliente) {
            t_inicio = Inicio::YaOtroDestino;
            m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya está transmitiendo a %1; para cambiar a %2 hay que pedirlo explícitamente (cambiar en caliente)"), {destinoActivo(s->id), enmascarar(stream.server, stream.key)}));
            return false;
        }
        stopStreaming(s->id);
    }
    t_inicio = Inicio::Error;
    if (stream.server.empty()) {
        m_.setError(ba::i18n::traducir("emision", "startStreaming sin servidor"));
        return false;
    }
    const std::uint64_t orden = s->stream.poner(true, ordenes_);
    s->activeServer = stream.server;
    s->activeKey = stream.key;
    t_inicio = Inicio::Iniciado;
    enSalidas("startStreaming", [this, e = s->emision, stream, enc, orden, alTerminar = std::move(alTerminar)]() {
        std::string err = startStreamingYa(*e, stream, enc);
        e->falloTransmision = err;
        e->transmisionViva = err.empty();
        if (!err.empty()) contarAlDueno({MensajeSalida::Tipo::NoTransmite, e->id, {}, orden});
        if (alTerminar) alTerminar({e->id, "transmitir", err.empty(), err, {}});
        return err;
    });
    return true;
}

std::string EmisionSalidas::startStreamingYa(SalidaEmision& s, const StreamConfig& stream, const EncoderConfig& enc) {
    anotarAac(s, enc.audioBitrateKbps);
    if (s.emisor) {
        // Al emisor aparte: la misma configuración. Si ya transmite a ese destino (la app
        // vuelve tras caerse), sigue esa: la conexión no se toca.
        const motor::Transmision::Config c =
            configTransmision(urlConClave(stream.server, stream.key), enc, m_.video_, BroadcastEngine::videoEncoderCandidates(enc));
        m_.emisor_->leer(s);
        const bool adopta = m_.emisor_->laTiene(s, "transmision");
        return m_.emisor_->pedir(s, [&](emisor::Pedido& p) {
            if (!(adopta && p.transmision && p.transmision->url == c.url)) p.transmision = c;
        });
    }
    auto tx = std::make_unique<motor::Transmision>(m_.motor_->reloj(), configTransmision(urlConClave(stream.server, stream.key), enc, m_.video_,
                                                                                         BroadcastEngine::videoEncoderCandidates(enc)));
    const motor::Fuentes f = fuentesDelPrograma(*m_.motor_, s.id, {});
    s.fuenteVideo = f.video;
    const std::string e = tx->iniciar(f);
    if (!e.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo iniciar la transmisión: %1"), {e});
    s.stream = std::move(tx);
    return {};
}

void EmisionSalidas::stopStreaming(const std::string& salida) {
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return;
    s->stream.poner(false, ordenes_);
    enSalidas("stopStreaming", [this, e = s->emision]() {
        if (e->stream) e->stream->parar();
        e->stream.reset();
        e->transmisionViva = false;
        if (e->emisor) return m_.emisor_->pedir(*e, [](emisor::Pedido& p) { p.transmision.reset(); });
        return std::string();
    });
}

bool EmisionSalidas::startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                                const std::string& salida, const std::string& serverRespaldo) {
    atenderBuzon();
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    if (server.empty()) {
        m_.setError(ba::i18n::traducir("emision", "el destino no tiene servidor"));
        return false;
    }
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return false;
    // El id de un destino es único entre todas las salidas: si ya sale (por la que sea), está.
    for (const auto& [sid, o] : m_.salidas_)
        if (o->extras.count(id) > 0) return true;
    const std::uint64_t orden = ++ordenes_;
    s->extras[id] = orden;
    enSalidas("startExtra", [this, e = s->emision, id, server, key, enc, serverRespaldo, orden]() {
        std::string err = startExtraYa(*e, id, server, key, enc, serverRespaldo);
        if (!err.empty()) contarAlDueno({MensajeSalida::Tipo::ExtraNoArranco, e->id, id, orden});
        return err;
    });
    return true;
}

std::string EmisionSalidas::startExtraYa(SalidaEmision& s, const std::string& id, const std::string& server, const std::string& key,
                                         const EncoderConfig& enc, const std::string& serverRespaldo) {
    auto it = s.extras.find(id);
    if (it != s.extras.end() && it->second.tx && it->second.tx->andando()) return {};
    if (it != s.extras.end()) {
        if (it->second.tx) it->second.tx->parar();
        s.extras.erase(it);
    }
    anotarAac(s, enc.audioBitrateKbps);
    motor::Transmision::Config c = configTransmision(urlConClave(server, key), enc, m_.video_, BroadcastEngine::videoEncoderCandidates(enc));
    // El punto de ingest de respaldo, con la misma clave: otra rama del mismo codificador.
    if (!serverRespaldo.empty()) c.urlRespaldo = urlConClave(serverRespaldo, key);
    if (s.emisor) {
        emisor::Pedido d;
        d.extras[id] = c;
        return m_.emisor_->extraAl(s, id, &d); // lo transmite su emisor
    }
    SalidaEmision::Extra x;
    x.server = server;
    x.key = key;
    x.serverRespaldo = serverRespaldo;
    x.tx = std::make_unique<motor::Transmision>(m_.motor_->reloj(), c);
    const motor::Fuentes f = fuentesDelPrograma(*m_.motor_, s.id, {});
    s.fuenteVideo = f.video;
    const std::string e = x.tx->iniciar(f);
    if (!e.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "el destino «%1» no arrancó: %2"), {id, e});
    s.extras[id] = std::move(x);
    return {};
}

void EmisionSalidas::stopExtra(const std::string& id) {
    for (const auto& [sid, s] : m_.salidas_) s->extras.erase(id);
    enSalidas("stopExtra", [this, id]() {
        for (const auto& s : emisiones_) {
            if (s->emisor) m_.emisor_->extraAl(*s, id, nullptr);
            auto it = s->extras.find(id);
            if (it == s->extras.end()) continue;
            if (it->second.tx) it->second.tx->parar();
            s->extras.erase(it);
        }
        return std::string();
    });
}

void EmisionSalidas::stopExtras() {
    atenderBuzon();
    std::set<std::string> ids;
    for (const auto& [sid, s] : m_.salidas_)
        for (const auto& [id, orden] : s->extras) ids.insert(id);
    for (const auto& id : ids) stopExtra(id);
}

nlohmann::json EmisionSalidas::extrasEstado() const {
    json out = json::object();
    const json ex = fotoSalidas().value("extras", json::object());
    for (const auto& [id, st] : ex.items())
        out[id] = {{"activo", st.value("andando", false)},
                   {"reconectando", st.value("reconectando", false)},
                   {"perdidos", st.value("perdidos", 0)},
                   {"cuadros", st.value("cuadros", 0)},
                   {"bytes", st.value("bytes", std::uint64_t{0})},
                   {"kbps", st.value("kbps", 0.0)},
                   {"destino", st.value("destino", std::string())},
                   {"salida", st.value("salida", std::string(BroadcastEngine::kSalidaPorOmision))},
                   {"ingest_respaldo", st.value("respaldo", json::object())}}; // el punto de respaldo, si tiene
    return out;
}

nlohmann::json EmisionSalidas::saludSalidas() const {
    const auto de = [](const json& st) {
        return json{{"activo", st.value("andando", false)},
                    {"reconectando", st.value("reconectando", false)},
                    {"bytes", st.value("bytes", std::uint64_t{0})},
                    {"perdidos", st.value("perdidos", 0)},
                    {"cuadros", st.value("cuadros", 0)},
                    {"congestion", 0.0}};
    };
    const json f = fotoSalidas();
    const json extras = f.value("extras", json::object()); // nunca .items() de un temporal
    json ex = json::object();
    for (const auto& [id, st] : extras.items()) ex[id] = de(st);
    return {{"principal", de(f.value("principal", json::object()))}, {"extras", ex}, {"saltados_encoder", 0}, {"lag_render", 0}};
}

// ============================================================================
// Grabación
// ============================================================================

bool EmisionSalidas::startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida,
                                    AlResultado alTerminar) {
    atenderBuzon();
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "startRecording sin startup"));
        return false;
    }
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return false;
    if (s->grabacion.si) {
        contestarComoLaAnterior(s->emision, "grabar", std::move(alTerminar));
        return true;
    }
    const std::uint64_t orden = s->grabacion.poner(true, ordenes_);
    s->lastRec = rec;
    s->lastRecValido = true;
    const std::string pedido = rec.container.empty() ? std::string("mkv") : rec.container;
    // MP4/MOV no sobreviven a un corte: se graba en MKV y se pasa al terminar.
    const std::string remuxA = pedido == "mp4" || pedido == "mov" ? pedido : std::string();
    // Las pistas 2-6 (la Matriz): las de esta salida, con su mezcla.
    std::vector<int> pistas;
    s->pistasGrabacionMask = 0;
    for (int p : rec.pistas)
        if (p >= 2 && p <= 6 && std::find(pistas.begin(), pistas.end(), p) == pistas.end()) {
            pistas.push_back(p);
            s->pistasGrabacionMask |= 1 << (p - 1);
        }
    std::sort(pistas.begin(), pistas.end());
    // Las de las demás salidas llevan su id: dos salidas grabando a la vez no se pisan.
    const std::string nombre = s->porOmision() ? "ba-" + marcaDeTiempo() : "ba-" + s->id + "-" + marcaDeTiempo();
    enSalidas("startRecording", [this, e = s->emision, rec, enc, pistas, nombre, remuxA, orden,
                                 remuxAlTerminar = rec.remuxAlTerminar, alTerminar = std::move(alTerminar)]() {
        e->remuxSal = remuxA; // lo que mira stopRecordingYa, en este hilo
        e->remuxAlTerminarSal = remuxAlTerminar;
        std::string err = startRecordingYa(*e, rec, enc, pistas, nombre);
        e->falloGrabacion = err;
        e->grabacionViva = err.empty();
        if (!err.empty()) contarAlDueno({MensajeSalida::Tipo::NoGraba, e->id, {}, orden});
        if (alTerminar) alTerminar({e->id, "grabar", err.empty(), err, err.empty() ? e->recordPath : std::string()});
        return err;
    });
    return true;
}

int EmisionSalidas::kbpsGrabacion(const RecordConfig& rec, const EncoderConfig& enc, bool conPistas) {
    const auto [video, audio] = kbpsVideoAudio(rec, enc);
    // Las pistas 2-6 suman su audio (el emisor aparte todavía no las graba).
    std::set<int> pistas;
    for (int p : rec.pistas)
        if (conPistas && p >= 2 && p <= 6) pistas.insert(p);
    return video + audio * static_cast<int>(1 + pistas.size());
}

std::string EmisionSalidas::startRecordingYa(SalidaEmision& s, const RecordConfig& rec, const EncoderConfig& enc,
                                             const std::vector<int>& pistas, const std::string& nombre) {
    if (s.rec && s.rec->grabando()) return {};
    motor::Grabacion::Config c;
    c.carpeta = rec.directory;
    c.nombre = nombre;
    // En tramos de 15 min de fábrica: un archivo dañado no se lleva toda la grabación (0 = uno solo).
    c.tramoSeg = ajustes::entero("record.tramoMin", 15) * 60;
    std::tie(c.kbps, c.kbpsAudio) = kbpsVideoAudio(rec, enc);
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, m_.video_.fpsNum / std::max(1, m_.video_.fpsDen));
    c.codificadores = BroadcastEngine::videoEncoderCandidates(enc);
    c.colchonSeg = std::max(5, rec.colchonSeg); // lo que más importa: el disco trabado sin perder nada (Configuración → Grabación)
    // «como_stream»: el mismo perfil que la transmisión (comparten el codificador; calidad
    // constante no sirve para la red y la transmisión la toma como VBR). «archivo»: calidad
    // constante (CRF) con el triple de bitrate como tope, su propio perfil.
    if (rec.calidad == "archivo") {
        c.control = motor::ControlTasa::Calidad;
        c.crf = rec.calidadCrf;
    } else {
        c.control = motor::controlTasaDe(enc.rateControl);
        if (c.control == motor::ControlTasa::Calidad) c.control = motor::ControlTasa::VBR;
    }
    c.carpetaRespaldo = rec.copiaDir; // si el disco de la grabación falla, la parte que sigue va a la de la copia
    // La copia en otro disco: lo mismo ya codificado, en otra carpeta (con
    // «record.copiaCodificadorPropio», un encode aparte).
    motor::Grabacion::Config cc = c;
    cc.carpeta = rec.copiaDir;
    cc.carpetaRespaldo.clear();
    cc.codificadorPropio = ajustes::siNo("record.copiaCodificadorPropio", false);
    if (s.emisor) {
        // Al emisor aparte, con las pistas 2-6 (cada una por su anillo: el motor las
        // manda) y a qué pasarla al cerrar en orden. Si ya graba (la app vuelve tras
        // caerse), sigue en ese archivo con lo que tenía.
        m_.emisor_->leer(s);
        const bool adopta = m_.emisor_->laTiene(s, "grabacion");
        const std::string e = m_.emisor_->pedir(s, [&](emisor::Pedido& p) {
            if (adopta && p.grabacion) return;
            p.grabacion = c;
            if (rec.copiaDir.empty())
                p.copia.reset();
            else
                p.copia = cc;
            p.pistas = pistas;
            p.remux = s.remuxAlTerminarSal ? s.remuxSal : std::string();
        });
        if (!e.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo iniciar la grabación: %1"), {e});
        const std::vector<int>& van = s.pedidoEmisor->pistas;
        if (const std::string ep = m_.motor_->pistasAlEmisor(s.id, std::set<int>(van.begin(), van.end())); !ep.empty())
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "grabacion", 0, ep);
        s.kbpsRec = kbpsGrabacion(rec, enc, s.porOmision());
        const auto& g = *s.pedidoEmisor->grabacion;
        s.recordPath = (std::filesystem::path(g.carpeta) / (g.nombre + "-00000.mkv")).string();
        s.recordCopyPath = s.pedidoEmisor->copia
                               ? (std::filesystem::path(s.pedidoEmisor->copia->carpeta) / (s.pedidoEmisor->copia->nombre + "-00000.mkv")).string()
                               : std::string();
        return {};
    }
    auto g = std::make_unique<motor::Grabacion>(m_.motor_->reloj(), c);
    const motor::Fuentes f = fuentesDelPrograma(*m_.motor_, s.id, pistas);
    s.fuenteVideo = f.video;
    const std::string e = g->iniciar(f);
    if (!e.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo iniciar la grabación: %1"), {e});
    s.rec = std::move(g);
    s.kbpsRec = kbpsGrabacion(rec, enc, true);
    s.recordPath = (std::filesystem::path(rec.directory) / (c.nombre + "-00000.mkv")).string();
    // La copia en otro disco: lo mismo ya codificado (si no arranca, la principal sigue). Con
    // «record.copiaCodificadorPropio», un encode aparte (cubre también un codificador que se traba).
    s.recordCopyPath.clear();
    if (!rec.copiaDir.empty()) {
        auto copia = std::make_unique<motor::Grabacion>(m_.motor_->reloj(), cc);
        if (copia->iniciar(f).empty()) {
            s.recCopia = std::move(copia);
            s.recordCopyPath = (std::filesystem::path(rec.copiaDir) / (cc.nombre + "-00000.mkv")).string();
        } else {
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "grabacion", 0, ba::i18n::llenar(ba::i18n::traducir("grabacion", "la copia en %1 no arrancó"), {rec.copiaDir}));
        }
    }
    return {};
}

void EmisionSalidas::stopRecording(const std::string& salida) {
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return;
    // Con la orden que la pidió (diag/Correlacion): quién paró una grabación se sabe.
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "grabación de la salida «%1»: se para (pedido)"), {s->id}));
    s->grabacion.poner(false, ordenes_);
    enSalidas("stopRecording", [this, e = s->emision]() {
        stopRecordingYa(*e);
        e->grabacionViva = false;
        return std::string();
    });
}

void EmisionSalidas::stopRecordingYa(SalidaEmision& s) {
    if (s.emisor) {
        m_.emisor_->leer(s);
        if (!s.pedidoEmisor->grabacion && !s.pedidoEmisor->copia && !m_.emisor_->laTiene(s, "grabacion")) return;
        if (const std::string e = m_.emisor_->pedir(s, [](emisor::Pedido& p) {
                p.grabacion.reset();
                p.copia.reset();
                p.pistas.clear();
                p.remux.clear(); // la pasa la app, acá abajo
            });
            !e.empty()) {
            m_.avisarFallo(e);
            return;
        }
        if (m_.motor_) m_.motor_->pistasAlEmisor(s.id, {});
        // Como parar() acá: se espera que el emisor cierre el archivo (su estado sin
        // grabación con este pedido aplicado), para pasarlo a MP4/MOV si se pidió.
        const std::uint64_t seq = s.pedidoEmisor->seq;
        std::vector<std::string> archivos;
        for (int i = 0; i < 100; ++i) {
            s.estadoEmisorMs = 0;
            m_.emisor_->leer(s);
            const json g = s.estadoEmisor.value("grabacion", json());
            if (g.is_object() && g.contains("archivos") && g["archivos"].is_array() && !g["archivos"].empty())
                archivos = g["archivos"].get<std::vector<std::string>>();
            if (s.estadoEmisor.value("seq", std::uint64_t{0}) >= seq && !g.is_object()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!archivos.empty()) s.recordPath = archivos.back();
        if (!s.remuxSal.empty() && s.remuxAlTerminarSal && !archivos.empty()) remuxVarios(archivos, s.remuxSal, true);
        return;
    }
    if (s.recCopia) {
        s.recCopia->parar();
        s.recCopia.reset();
    }
    if (!s.rec) return;
    const bool estaba = s.rec->grabando();
    s.rec->parar(); // espera el cierre del archivo: por eso en este hilo
    const auto archivos = s.rec->archivos();
    if (!archivos.empty()) s.recordPath = archivos.back();
    s.rec.reset();
    // Con la grabación partida en tramos (Configuración → avanzado), se pasan todos.
    if (estaba && !s.remuxSal.empty() && s.remuxAlTerminarSal && !archivos.empty()) remuxVarios(archivos, s.remuxSal, true);
}

std::string EmisionSalidas::archivoGrabacion(const std::string& salida) const {
    return fotoDeSalida(salida).value("grabacion", json::object()).value("archivo", std::string());
}

nlohmann::json EmisionSalidas::grabacionEstado(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return json::object();
    json pistas = json::array({1});
    for (int i = 1; i < 6; ++i)
        if (s->pistasGrabacionMask & (1 << i)) pistas.push_back(i + 1);
    const json g = fotoDeSalida(s->id).value("grabacion", json::object());
    const json st = g.value("estado", json::object());
    return {{"activa", g.value("activa", false)},
            {"caida", g.value("caida", false)},
            {"parte", st.value("parte", 1)},
            {"cortes", st.value("cortes", 0)},
            {"archivo", g.value("archivo", std::string())},
            {"copia", g.value("copia", std::string())},
            {"copia_activa", g.value("copia_activa", false)},
            {"calidad", s->lastRecValido ? s->lastRec.calidad : std::string("como_stream")},
            {"encoder", st.value("codificador", std::string())},
            {"pistas", pistas},
            {"encoder_respaldo_activo", fotoDeSalida(s->id).value("codificador_respaldo", false)}};
}

bool EmisionSalidas::remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen) {
    if (archivos.empty()) return false;
    const std::string archivo = archivos.front();
    std::lock_guard<std::mutex> lr(remuxM_); // lo lanza también el hilo de salidas (al parar)
    if (remux_ && remux_->andando) {
        m_.setError(ba::i18n::traducir("emision", "ya hay un remux andando"));
        return false;
    }
    auto st = std::make_shared<EstadoRemux>();
    const std::string destino = std::filesystem::path(archivo).replace_extension("." + formato).string();
    st->archivo = archivo;
    st->destino = destino;
    st->andando = true;
    remux_ = st;
    std::thread([st, archivos, formato, borrarOrigen]() {
        bool todos = true;
        for (std::size_t i = 0; i < archivos.size(); ++i) {
            const std::string& origen = archivos[i];
            const std::string dest = std::filesystem::path(origen).replace_extension("." + formato).string();
            {
                std::lock_guard<std::mutex> l(st->mu);
                st->archivo = origen;
                st->destino = dest;
            }
            const double base = static_cast<double>(i) / static_cast<double>(archivos.size());
            // Verifica el nuevo antes de borrar el MKV (lo mismo hace el emisor aparte al cerrar).
            const std::string e = motor::remuxarYVerificar(origen, formato, borrarOrigen, [&](double p) {
                st->pct = static_cast<float>((base + p / static_cast<double>(archivos.size())) * 100.0);
            });
            if (!e.empty()) {
                std::lock_guard<std::mutex> l(st->mu);
                st->error = e;
                todos = false;
            }
        }
        st->ok = todos;
        st->pct = 100;
        st->andando = false;
    }).detach();
    return true;
}

nlohmann::json EmisionSalidas::remuxEstado() const {
    std::shared_ptr<EstadoRemux> r;
    {
        std::lock_guard<std::mutex> lr(remuxM_);
        r = remux_;
    }
    if (!r) return json::object();
    std::lock_guard<std::mutex> l(r->mu);
    return {{"archivo", r->archivo}, {"destino", r->destino}, {"pct", r->pct.load()},
            {"andando", r->andando.load()}, {"ok", r->ok.load()}, {"error", r->error}};
}

bool EmisionSalidas::encoderRespaldoActivo() const {
    const json todas = fotoSalidas().value("salidas", json::object());
    for (const auto& [id, f] : todas.items())
        if (f.value("codificador_respaldo", false)) return true;
    return false;
}

void EmisionSalidas::simularDiscoTrabado(int ms) {
    enSalidas("simularDiscoTrabado", [this, ms]() {
        for (const auto& s : emisiones_)
            if (s->porOmision() && s->rec) s->rec->simularDiscoTrabado(ms);
        return std::string();
    });
}

// --- Repetición -----------------------------------------------------------------------------

bool EmisionSalidas::setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida) {
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return false;
    enSalidas("setReplay", [this, e = s->emision, segundos, dir, enc]() { return setReplayYa(*e, segundos, dir, enc); });
    return true;
}

std::string EmisionSalidas::setReplayYa(SalidaEmision& s, int segundos, const std::string& dir, const EncoderConfig& enc) {
    s.replay.reset();
    if (s.emisor) return m_.emisor_->replayAl(s, segundos, dir, enc.videoBitrateKbps); // la graba su emisor
    if (segundos <= 0) return {};
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto r = std::make_unique<SalidaEmision::Repeticion>();
    r->dir = dir;
    r->rep = std::make_unique<motor::Repeticion>(m_.motor_->reloj(), (std::filesystem::path(dir) / ".tramos").string(), segundos,
                                                 enc.videoBitrateKbps);
    const std::string e = r->rep->iniciar(fuentesDelPrograma(*m_.motor_, s.id, {}));
    if (!e.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no arrancó la repetición: %1"), {e});
    if (m_.disco_->prioridad()) r->rep->pausar(true);
    s.replay = std::move(r);
    return {};
}

bool EmisionSalidas::guardarReplay(const std::string& salida) {
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return false;
    if (!fotoDeSalida(s->id).value("replay", json::object()).value("activa", false)) {
        m_.setError(ba::i18n::traducir("emision", "la repetición está apagada (replay_ajustes con segundos)"));
        return false;
    }
    enSalidas("guardarReplay", [this, e = s->emision]() {
        const std::string pre = e->porOmision() ? std::string("repeticion-") : "repeticion-" + e->id + "-";
        if (e->emisor) return m_.emisor_->guardarReplayEn(*e, pre + marcaDeTiempo() + ".mkv");
        if (!e->replay || !e->replay->rep->andando() || e->replay->guardando) return std::string();
        SalidaEmision::Repeticion* r = e->replay.get();
        r->guardando = true;
        const std::string destino = (std::filesystem::path(r->dir) / (pre + marcaDeTiempo() + ".mkv")).string();
        std::thread([r, destino]() {
            const std::string err = r->rep->guardar(destino);
            {
                std::lock_guard<std::mutex> l(r->mu);
                if (err.empty()) r->ultimo = destino;
            }
            if (!err.empty()) ba::diag::registro().linea(ba::diag::Nivel::Aviso, "repeticion", 0, ba::i18n::llenar(ba::i18n::traducir("repeticion", "no se guardó: %1"), {err}));
            r->guardando = false;
        }).detach();
        return std::string();
    });
    return true;
}

std::string EmisionSalidas::ultimoReplay(const std::string& salida) const {
    return fotoDeSalida(salida).value("replay", json::object()).value("ultimo", std::string());
}

nlohmann::json EmisionSalidas::replayEstado(const std::string& salida) const {
    const json r = fotoDeSalida(salida).value("replay", json::object());
    return {{"activa", r.value("activa", false)}, {"segundos", r.value("segundos", 0)}, {"ultimo", r.value("ultimo", std::string())}};
}

// ============================================================================
// Grabación por entrada y señal limpia
// ============================================================================

bool EmisionSalidas::startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc) {
    atenderBuzon();
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "startSourceRecording sin startup"));
        return false;
    }
    if (!m_.escenas_->sourceExists(source)) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente \"%1\""), {source}));
        return false;
    }
    if (individualesPedidas_.count(source) > 0) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya se esta grabando \"%1\" por separado"), {source}));
        return false;
    }
    if (static_cast<int>(individualesPedidas_.size()) >= BroadcastEngine::kMaxIndividuales) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya hay %1 entradas grabandose por separado, que es el tope"), {std::to_string(BroadcastEngine::kMaxIndividuales)}));
        return false;
    }
    const std::uint64_t orden = ++ordenes_;
    individualesPedidas_[source] = orden;
    enSalidas("startSourceRecording", [this, source, dir, enc, orden]() {
        std::string err = startSourceRecordingYa(source, dir, enc);
        if (!err.empty()) contarAlDueno({MensajeSalida::Tipo::AparteNoArranco, {}, source, orden});
        return err;
    });
    return true;
}

std::string EmisionSalidas::startSourceRecordingYa(const std::string& source, const std::string& dir, const EncoderConfig& enc) {
    if (sourceRecs_.count(source) > 0) return {};
    // La foto del motor (no estadoMotor: ese es del dueño).
    const auto foto = m_.motor_->instantanea();
    const json e = foto ? foto->value("entradas", json::object()).value(source, json::object()) : json::object();
    motor::Fuentes f;
    if (e.value("video", false)) {
        f.video = source + ":video";
        f.latVideoNs = m_.motor_->reloj().latencia();
    }
    GrabacionAparte r;
    // Su audio, si tiene; si no, el del programa (referencia para sincronizar al editar).
    r.audioPropio = e.value("audio", false);
    if (r.audioPropio) {
        f.audios.push_back(source + ":audio");
        f.latAudioNs = m_.motor_->reloj().latencia();
    } else {
        std::int64_t l = 0;
        f.audios.push_back(m_.motor_->productorDeBus("programa", &l));
        f.latAudioNs = l;
    }
    std::string limpio;
    for (const char c : source) limpio += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') ? c : '-';
    motor::Grabacion::Config c;
    c.carpeta = dir;
    c.nombre = "entrada-" + limpio + "-" + marcaDeTiempo();
    c.tramoSeg = ajustes::entero("record.tramoMin", 15) * 60;
    c.kbps = enc.videoBitrateKbps;
    c.kbpsAudio = enc.audioBitrateKbps;
    c.gopSeg = std::max(1, enc.keyintSec);
    c.fps = std::max(1, m_.video_.fpsNum / std::max(1, m_.video_.fpsDen));
    // Las sesiones de codificación de la placa quedan para el programa: x264.
    c.codificadores = {"x264enc"};
    c.colchonSeg = std::max(1, ajustes::entero("record.colchonIsoS", 10)); // hasta 8 a la vez: menos memoria (con el disco en apuros se pausan)
    r.grab = std::make_unique<motor::Grabacion>(m_.motor_->reloj(), c);
    const std::string err = r.grab->iniciar(f);
    if (!err.empty()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no arrancó la grabación de \"%1\": %2"), {source, err});
    r.path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
    r.kbps = c.kbps + c.kbpsAudio;
    if (m_.disco_->prioridad()) r.grab->pausar(true); // el disco es del programa: arranca en pausa
    sourceRecs_[source] = std::move(r);
    return {};
}

bool EmisionSalidas::stopSourceRecording(const std::string& source) {
    atenderBuzon();
    if (individualesPedidas_.erase(source) == 0) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no se estaba grabando \"%1\" por separado"), {source}));
        return false;
    }
    enSalidas("stopSourceRecording", [this, source]() {
        const auto it = sourceRecs_.find(source);
        if (it == sourceRecs_.end()) return std::string();
        if (it->second.grab) it->second.grab->parar();
        sourceRecs_.erase(it);
        return std::string();
    });
    return true;
}

void EmisionSalidas::stopAllSourceRecordings() {
    atenderBuzon();
    std::vector<std::string> todas;
    for (const auto& [n, orden] : individualesPedidas_) todas.push_back(n);
    for (const auto& s : todas) stopSourceRecording(s);
}

std::vector<BroadcastEngine::GrabacionIndividual> EmisionSalidas::sourceRecordings() const {
    std::vector<BroadcastEngine::GrabacionIndividual> out;
    const json ind = fotoSalidas().value("individuales", json::array());
    for (const auto& r : ind)
        out.push_back({r.value("nombre", std::string()), r.value("path", std::string()), r.value("audio_propio", false),
                       r.value("grabando", false), r.value("bytes", std::uint64_t{0}), r.value("pausada", false)});
    return out;
}

bool EmisionSalidas::startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                                    const EncoderConfig& enc, const std::string& salida) {
    atenderBuzon();
    if (!m_.started_) {
        m_.setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return false;
    if (s->limpio.si) return true;
    const std::uint64_t orden = s->limpio.poner(true, ordenes_);
    enSalidas("startCleanFeed", [this, e = s->emision, destino, clave, dir, enc, orden]() {
        std::string err = startCleanFeedYa(*e, destino, clave, dir, enc);
        if (!err.empty()) contarAlDueno({MensajeSalida::Tipo::NoLimpia, e->id, {}, orden});
        return err;
    });
    return true;
}

std::string EmisionSalidas::startCleanFeedYa(SalidaEmision& s, const std::string& destino, const std::string& clave,
                                             const std::string& dir, const EncoderConfig& enc) {
    if (s.clean) return {};
    const auto fallo = [](const std::string& e) { return ba::i18n::llenar(ba::i18n::traducir("emision", "no arrancó la señal limpia: %1"), {e}); };
    auto r = std::make_unique<GrabacionAparte>();
    const motor::Fuentes f = fuentesDelPrograma(*m_.motor_, s.id, {}, true); // el programa SIN capas
    const int fps = std::max(1, m_.video_.fpsNum / std::max(1, m_.video_.fpsDen));
    if (!destino.empty()) {
        motor::Transmision::Config c;
        c.url = urlConClave(destino, clave);
        c.kbps = enc.videoBitrateKbps;
        c.kbpsAudio = enc.audioBitrateKbps;
        c.gopSeg = std::max(1, enc.keyintSec);
        c.fps = fps;
        c.codificadores = {"x264enc"};
        if (s.emisor) { // la transmite su emisor (con su canal de la señal limpia)
            emisor::PedidoLimpio l;
            l.transmision = c;
            if (const std::string e = m_.emisor_->limpioAl(s, &l); !e.empty()) return fallo(e);
            return {};
        }
        r->tx = std::make_unique<motor::Transmision>(m_.motor_->reloj(), c);
        const std::string e = r->tx->iniciar(f);
        if (!e.empty()) return fallo(e);
        r->destino = enmascarar(destino, clave);
    } else {
        motor::Grabacion::Config c;
        c.carpeta = dir;
        c.nombre = (s.porOmision() ? std::string("senal-limpia-") : "senal-limpia-" + s.id + "-") + marcaDeTiempo();
        c.tramoSeg = ajustes::entero("record.tramoMin", 15) * 60;
        c.kbps = enc.videoBitrateKbps;
        c.fps = fps;
        c.codificadores = {"x264enc"};
        if (s.emisor) { // la graba su emisor
            emisor::PedidoLimpio l;
            l.grabacion = c;
            if (const std::string e = m_.emisor_->limpioAl(s, &l); !e.empty()) return fallo(e);
            return {};
        }
        r->grab = std::make_unique<motor::Grabacion>(m_.motor_->reloj(), c);
        const std::string e = r->grab->iniciar(f);
        if (!e.empty()) return fallo(e);
        r->path = (std::filesystem::path(dir) / (c.nombre + "-00000.mkv")).string();
        r->destino = r->path;
        r->kbps = c.kbps + c.kbpsAudio;
    }
    s.clean = std::move(r);
    return {};
}

void EmisionSalidas::stopCleanFeed(const std::string& salida) {
    std::shared_ptr<SalidaAire> s = m_.laSalidaCompartida(salida);
    if (!s) return;
    s->limpio.poner(false, ordenes_);
    enSalidas("stopCleanFeed", [this, e = s->emision]() {
        if (e->emisor) return m_.emisor_->limpioAl(*e, nullptr);
        if (!e->clean) return std::string();
        if (e->clean->tx) e->clean->tx->parar();
        if (e->clean->grab) e->clean->grab->parar();
        e->clean.reset();
        return std::string();
    });
}

nlohmann::json EmisionSalidas::cleanFeedEstado(const std::string& salida) const {
    return fotoDeSalida(salida).value("limpio", json{{"activa", false}});
}

} // namespace ba::app
