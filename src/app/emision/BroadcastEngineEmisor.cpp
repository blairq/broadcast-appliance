// ============================================================================
// src/app/emision/BroadcastEngineEmisor.cpp — las salidas que codifica un emisor aparte
//
// Parte de BroadcastEngine (ver EL EMISOR APARTE en BroadcastEngine.hpp y
// docs/arquitectura/04-plan-unificado.md, F3-01). Para una salida con emisor, lo que
// startStreamingYa, startExtraYa, startRecordingYa, setReplayYa y startCleanFeedYa
// armarían acá (las mismas configuraciones de motor::Transmision, motor::Grabacion y
// motor::Repeticion) se escribe en su pedido; el `ba-emisor` lo aplica, y lo que hace
// se lee de su estado para la foto de las salidas.
//
// Su video le llega por canales propios (CanalEmisor): el programa de la salida en
// NV12 y del tamaño de la salida (lo que toma el codificador, sin el tope del retorno
// al gateway ni otra conversión), y la señal limpia mientras se la pide. Las pistas
// 2-6 de su grabación, por un anillo cada una (Motor::pistasAlEmisor).
//
// Todo en el hilo de salidas, como lo que reemplaza; los canales también desde la
// interfaz (al arrancar y al crear o borrar una salida) y sus cuadros desde el motor.
// ============================================================================

#include "emision/BroadcastEngine.hpp"

#include "Motor.hpp"
#include "Pedido.hpp"
#include "Salidas.hpp"
#include "Vistas.hpp"
#include "i18n/I18n.hpp"
#include "ipc/CanalesSalida.hpp"
#include "ipc/HealthBlock.hpp"
#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace ba::app {

using json = nlohmann::json;

/// El canal de video de una salida a su emisor: el escritor y la vista que lo alimenta.
struct BroadcastEngine::CanalEmisor {
    std::mutex mu; ///< tx, el tamaño y las filas (el motor manda los cuadros desde su hilo)
    ba::ipc::SpoutSender tx;
    int ancho = 0, alto = 0;
    std::uint64_t cuadros = 0;
    std::vector<std::uint8_t> filas;
    std::string clave; ///< la vista del motor («programa…@emisor», «limpio…@emisor»)
    bool pedida = false;
};

namespace {

std::int64_t ahoraMsMono() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::int64_t ahoraMsReloj() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::filesystem::path runtime() { return ba::platform::dataRoot() / "Runtime"; }

/// Lo último que escribe del archivo: su estado dice cada segundo qué hace; uno de
/// hace más de 5 s es de un emisor que no está (caído, o el launcher lo está relanzando).
bool fresco(const json& e) {
    return e.is_object() && !e.value("cerrado", false) && ahoraMsReloj() - e.value("hora_ms", std::int64_t{0}) < 5000;
}

json objeto(const json& e, const char* k) {
    const auto it = e.find(k);
    return it != e.end() && it->is_object() ? *it : json::object();
}

std::vector<std::string> archivosDe(const json& g) {
    std::vector<std::string> out;
    const auto it = g.find("archivos");
    if (it != g.end() && it->is_array())
        for (const auto& a : *it)
            if (a.is_string()) out.push_back(a.get<std::string>());
    return out;
}

/// Un cuadro NV12 de la vista al canal: los dos planos seguidos y sin relleno (lo que
/// espera el canal). Si la vista ya los trae así, sin copiar acá: la única copia es al canal.
void enviarNv12(ba::ipc::SpoutSender& tx, std::vector<std::uint8_t>& filas, std::uint64_t cuadro, const motor::Cuadro& q) {
    const std::size_t w = static_cast<std::size_t>(q.ancho), h = static_cast<std::size_t>(q.alto), y = w * h;
    const std::size_t total = ba::ipc::bytesDeCuadro(q.ancho, q.alto, ba::ipc::FormatoCanal::NV12);
    const std::uint8_t* px = q.bgra.data();
    if (static_cast<std::size_t>(q.stride) != w || static_cast<std::size_t>(q.strideUv) != w || q.desdeUv != y) {
        filas.resize(total);
        for (std::size_t r = 0; r < h; ++r) std::memcpy(filas.data() + w * r, px + static_cast<std::size_t>(q.stride) * r, w);
        for (std::size_t r = 0; r < h / 2; ++r)
            std::memcpy(filas.data() + y + w * r, px + q.desdeUv + static_cast<std::size_t>(q.strideUv) * r, w);
        px = filas.data();
    }
    ba::ipc::FrameMeta meta;
    meta.frameIndex = cuadro;
    meta.qpc = ba::ipc::HealthBlock::nowQpc();
    meta.contenidoQpc = q.contenidoQpc;
    tx.send(px, total, meta);
}

} // namespace

const std::set<std::string>& BroadcastEngine::salidasConEmisor() {
    // El launcher lo fija al lanzar A (Supervisor::armarHijos): no cambia en la corrida.
    static const std::set<std::string> ids = []() {
        std::set<std::string> out;
        const char* v = std::getenv("BA_EMISOR");
        std::stringstream ss(v != nullptr ? v : "");
        for (std::string id; std::getline(ss, id, ',');)
            if (ba::ipc::idDeSalidaValido(id)) out.insert(id);
        return out;
    }();
    return ids;
}

// --- Sus canales de video -------------------------------------------------------------

bool BroadcastEngine::publicarCanalEmisor(Salida& s, bool limpio) {
    std::shared_ptr<CanalEmisor>& c = limpio ? s.canalLimpioEmisor : s.canalEmisor;
    if (!c) c = std::make_shared<CanalEmisor>();
    // Del tamaño de la salida (par: NV12 tiene el plano de color a la mitad).
    ba::ipc::SlotConfig cfg{std::max(2, video_.outputWidth) & ~1, std::max(2, video_.outputHeight) & ~1};
    cfg.formato = ba::ipc::FormatoCanal::NV12;
    if (video_.fpsNum > 0 && video_.fpsDen > 0)
        cfg.periodoNs = static_cast<std::uint64_t>(video_.fpsDen) * 1'000'000'000ULL / static_cast<std::uint64_t>(video_.fpsNum);
    {
        std::lock_guard<std::mutex> l(c->mu);
        // Como el retorno: un canal no cambia de tamaño; con otro lienzo se rehace y el
        // emisor se vuelve a suscribir solo (otra instancia).
        if (c->tx.published() && (c->ancho != cfg.width || c->alto != cfg.height)) c->tx = ba::ipc::SpoutSender();
        if (!c->tx.published()) {
            const std::string nombre = limpio ? ba::ipc::canalVideoLimpioEmisor(s.id) : ba::ipc::canalVideoEmisor(s.id);
            const std::wstring w(nombre.begin(), nombre.end());
            if (!c->tx.publish(w.c_str(), cfg)) {
                setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo publicar el canal del emisor de «%1»"), {s.id}));
                return false;
            }
            c->ancho = cfg.width;
            c->alto = cfg.height;
        }
    }
    if (!motor_ || c->pedida) return true;
    const std::string sufijo = s.porOmision() ? std::string() : ":" + s.id;
    c->clave = (limpio ? "limpio" : "programa") + sufijo + "@emisor";
    if (const std::string e = motor_->verPedir(c->clave, cfg.width, cfg.height, "NV12"); !e.empty()) {
        setError(e);
        return false;
    }
    // El canal va en el aviso: vive mientras el motor pueda llamarlo.
    motor_->verAvisar(c->clave, [c](const motor::Cuadro& q) {
        std::lock_guard<std::mutex> l(c->mu);
        if (c->tx.published() && q.nv12 && q.ancho == c->ancho && q.alto == c->alto) enviarNv12(c->tx, c->filas, ++c->cuadros, q);
    });
    c->pedida = true;
    return true;
}

void BroadcastEngine::soltarCanalEmisor(Salida& s, bool limpio, bool cerrar) {
    std::shared_ptr<CanalEmisor>& c = limpio ? s.canalLimpioEmisor : s.canalEmisor;
    if (!c) return;
    if (c->pedida && motor_) {
        motor_->verAvisar(c->clave, nullptr);
        motor_->verSoltar(c->clave);
    }
    c->pedida = false;
    if (!cerrar) return; // publicado: el emisor sigue suscripto y, sin cuadros, en la placa
    {
        std::lock_guard<std::mutex> l(c->mu);
        c->tx = ba::ipc::SpoutSender();
    }
    c.reset();
}

void BroadcastEngine::desengancharEmisores() {
    for (const auto& s : lasSalidas()) {
        if (!s->emisor) continue;
        soltarCanalEmisor(*s, false, false);
        soltarCanalEmisor(*s, true, false);
        s->emisorEnganchado = false; // el motor nuevo los vuelve a enganchar (leerEmisor)
    }
}

// --- El pedido y el estado -------------------------------------------------------------

void BroadcastEngine::leerEmisor(Salida& s) {
    const std::int64_t ahora = ahoraMsMono();
    if (s.pedidoEmisor && s.emisorEnganchado && ahora - s.estadoEmisorMs < 500) return;
    s.estadoEmisorMs = ahora;
    // Lo pedido antes (por esta corrida o la anterior, si la app vuelve tras caerse).
    if (!s.pedidoEmisor) {
        s.pedidoEmisor = std::make_unique<emisor::Pedido>();
        emisor::leer(runtime(), s.id, *s.pedidoEmisor);
    }
    // Lo que el pedido ya tenía y necesita algo de acá (un motor nuevo, o la app que
    // vuelve): el canal de la señal limpia y los puentes de las pistas; y se anota como
    // pedido, así la interfaz lo muestra y lo puede parar.
    if (!s.emisorEnganchado && motor_) {
        s.emisorEnganchado = true;
        const emisor::Pedido& p = *s.pedidoEmisor;
        {
            std::lock_guard<std::mutex> l(pedidosM_);
            for (const auto& [id, c] : p.extras) s.extrasPedidos.insert(id);
            if (p.limpio) s.limpioPedido = true;
        }
        if (p.limpio) publicarCanalEmisor(s, true);
        if (!p.pistas.empty()) motor_->pistasAlEmisor(s.id, std::set<int>(p.pistas.begin(), p.pistas.end()));
    }
    std::string txt;
    if (ba::persist::leerArchivo(emisor::rutaEstado(runtime(), s.id), txt)) {
        json e = json::parse(txt, nullptr, false);
        if (e.is_object()) s.estadoEmisor = std::move(e);
    }
}

bool BroadcastEngine::emisorLaTiene(const Salida& s, const char* que) const {
    const json& e = s.estadoEmisor;
    if (!e.is_object() || e.value("cerrado", false)) return false;
    const json x = objeto(e, que);
    if (x.empty()) return false;
    return std::string(que) == "transmision" ? x.value("andando", false) : (x.value("grabando", false) || x.value("caida", false));
}

std::string BroadcastEngine::pedirAlEmisor(Salida& s, const std::function<void(emisor::Pedido&)>& cambio) {
    s.estadoEmisorMs = 0;
    leerEmisor(s);
    emisor::Pedido p = *s.pedidoEmisor;
    cambio(p);
    // El siguiente de lo que vio el emisor también (si el pedido se perdió, uno con un
    // `seq` viejo podría ser el que cerró en orden y lo ignoraría).
    p.seq = std::max<std::uint64_t>(p.seq, s.estadoEmisor.value("seq_visto", std::uint64_t{0})) + 1;
    p.ancho = video_.outputWidth;
    p.alto = video_.outputHeight;
    p.fpsN = std::max(1, video_.fpsNum);
    p.fpsD = std::max(1, video_.fpsDen);
    p.placaTexto = ba::i18n::traducirEvento("emision", "Volvemos enseguida");
    std::string err;
    // Las claves de las URL van al CredentialStore; el pedido queda sin ellas.
    if (!emisor::escribir(runtime(), s.id, p, &err))
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo pedir al emisor de «%1»: %2"), {s.id, err});
    *s.pedidoEmisor = std::move(p);
    return {};
}

// --- Lo que delega cada salida ---------------------------------------------------------

std::string BroadcastEngine::extraAlEmisor(Salida& s, const std::string& id, const emisor::Pedido* desde) {
    const motor::Transmision::Config* c = nullptr;
    if (desde != nullptr)
        if (const auto it = desde->extras.find(id); it != desde->extras.end()) c = &it->second;
    leerEmisor(s);
    if (c == nullptr && s.pedidoEmisor->extras.count(id) == 0) return {};
    return pedirAlEmisor(s, [&](emisor::Pedido& p) {
        if (c == nullptr) {
            p.extras.erase(id);
            return;
        }
        // Al mismo destino ya lo tiene (la app que vuelve): sigue esa conexión.
        const auto it = p.extras.find(id);
        if (it != p.extras.end() && it->second.url == c->url && it->second.urlRespaldo == c->urlRespaldo) return;
        p.extras[id] = *c;
    });
}

std::string BroadcastEngine::replayAlEmisor(Salida& s, int segundos, const std::string& dir, int kbps) {
    return pedirAlEmisor(s, [&](emisor::Pedido& p) {
        if (segundos <= 0) {
            p.repeticion.reset();
            return;
        }
        emisor::PedidoRepeticion r = p.repeticion.value_or(emisor::PedidoRepeticion{});
        r.segundos = segundos;
        r.carpeta = dir;
        r.kbps = kbps;
        p.repeticion = r;
    });
}

std::string BroadcastEngine::guardarReplayEnEmisor(Salida& s, const std::string& nombre) {
    leerEmisor(s);
    if (!s.pedidoEmisor->repeticion) return ba::i18n::traducir("emision", "la repetición está apagada (replay_ajustes con segundos)");
    return pedirAlEmisor(s, [&](emisor::Pedido& p) {
        if (!p.repeticion) return;
        ++p.repeticion->guardarSeq;
        p.repeticion->guardarEn = (std::filesystem::path(p.repeticion->carpeta) / nombre).string();
    });
}

std::string BroadcastEngine::limpioAlEmisor(Salida& s, const emisor::PedidoLimpio* l) {
    const motor::Transmision::Config* tx = l != nullptr && l->transmision ? &*l->transmision : nullptr;
    const motor::Grabacion::Config* grab = l != nullptr && l->grabacion ? &*l->grabacion : nullptr;
    leerEmisor(s);
    if (tx == nullptr && grab == nullptr) {
        if (!s.pedidoEmisor->limpio) return {};
        const std::string e = pedirAlEmisor(s, [](emisor::Pedido& p) { p.limpio.reset(); });
        soltarCanalEmisor(s, true, true);
        return e;
    }
    if (!publicarCanalEmisor(s, true)) return lastError();
    return pedirAlEmisor(s, [&](emisor::Pedido& p) {
        // La que ya hace (la app que vuelve): sigue esa.
        if (p.limpio && ((tx != nullptr && p.limpio->transmision && p.limpio->transmision->url == tx->url) ||
                         (grab != nullptr && p.limpio->grabacion)))
            return;
        p.limpio = *l;
    });
}

// --- La foto ---------------------------------------------------------------------------

void BroadcastEngine::fotoDelEmisor(const Salida& s, json& f) const {
    const json& e = s.estadoEmisor;
    const emisor::Pedido* p = s.pedidoEmisor.get();
    const bool vivo = fresco(e);
    // Lo que muestra el estado es lo pedido recién cuando el emisor ya aplicó ese pedido;
    // antes (o con el emisor caído: el launcher lo relanza y retoma), lo pedido.
    const bool aplicado = vivo && p != nullptr && e.value("seq", std::uint64_t{0}) >= p->seq;
    const json sinConexion = {{"conectada", false}, {"reconectando", false}, {"kbps", 0.0}, {"bytes", 0}, {"cuadros", 0}, {"perdidos", 0}};
    if (p != nullptr && p->transmision) {
        json t = aplicado ? objeto(e, "transmision") : json::object();
        if (t.empty()) t = sinConexion;
        t["andando"] = true; // pedida (aunque esté reconectando), como Transmision::estado
        f["principal"] = std::move(t);
    }
    if (p != nullptr && !p->extras.empty()) {
        const json ex = objeto(e, "extras");
        json out = f.value("extras", json::object());
        for (const auto& [id, c] : p->extras) {
            json t = aplicado ? objeto(ex, id.c_str()) : json::object();
            if (t.empty()) t = sinConexion;
            t["andando"] = true;
            t["destino"] = t.value("url", std::string()); // la URL sin la clave (Transmision::estado)
            t["salida"] = s.id;
            out[id] = std::move(t);
        }
        f["extras"] = std::move(out);
    }
    if (p != nullptr && (p->grabacion || p->copia)) {
        const json g = aplicado ? objeto(e, "grabacion") : json::object();
        const json c = aplicado ? objeto(e, "copia") : json::object();
        const auto ultimo = [](const json& x, const std::string& sino) {
            const auto a = archivosDe(x);
            return a.empty() ? sino : a.back();
        };
        f["grabacion"] = {{"activa", aplicado ? g.value("grabando", false) : true},
                          {"caida", aplicado && g.value("caida", false)},
                          {"estado", g},
                          {"archivo", ultimo(g, s.recordPath)},
                          {"copia", ultimo(c, s.recordCopyPath)},
                          {"copia_activa", aplicado ? c.value("grabando", false) : static_cast<bool>(p->copia)}};
    }
    if (p != nullptr && p->repeticion) {
        const json r = aplicado ? objeto(e, "replay") : json::object();
        f["replay"] = {{"activa", aplicado ? r.value("activa", false) : true},
                       {"segundos", p->repeticion->segundos},
                       {"ultimo", r.value("ultimo", std::string())},
                       {"guardando", r.value("guardando", false)}};
    }
    if (p != nullptr && p->limpio) {
        const json l = aplicado ? objeto(e, "limpio") : json::object();
        const bool aRed = static_cast<bool>(p->limpio->transmision);
        const json st = objeto(l, aRed ? "transmision" : "grabacion");
        const auto archivos = archivosDe(st);
        f["limpio"] = {{"activa", !aplicado || (aRed ? st.value("andando", false) : st.value("grabando", false))},
                       {"reconectando", st.value("reconectando", false)},
                       {"destino", aRed ? st.value("url", std::string()) : (archivos.empty() ? std::string() : archivos.back())},
                       {"archivo", archivos.empty() ? std::string() : archivos.back()},
                       {"cuadros", st.value("cuadros", 0)},
                       {"perdidos", st.value("perdidos", 0)},
                       {"placa", l.value("placa", false)}};
    }
    f["emisor"] = {{"vivo", vivo},
                   {"placa", e.value("placa", false)},
                   {"placas", e.value("placas", 0)},
                   {"edad_entrada", e.value("edad_entrada", json())},
                   {"error", e.value("error", std::string())},
                   {"pid", e.value("pid", 0)},
                   {"remux_pendiente", e.value("remux_pendiente", json())}};
}

bool BroadcastEngine::aireEnEmisor(const Salida& s) const { return s.pedidoEmisor && s.pedidoEmisor->hayAire(); }

} // namespace ba::app
