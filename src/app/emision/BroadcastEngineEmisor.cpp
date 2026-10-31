// ============================================================================
// src/app/emision/BroadcastEngineEmisor.cpp — las salidas que codifica un emisor aparte
//
// Parte de BroadcastEngine (ver EL EMISOR APARTE en BroadcastEngine.hpp y
// docs/arquitectura/04-plan-unificado.md, F3-01). Para una salida con emisor, lo que
// startStreamingYa/startRecordingYa armarían acá (las mismas configuraciones de
// motor::Transmision y motor::Grabacion) se escribe en su pedido; el `ba-emisor` lo
// aplica, y lo que hace se lee de su estado para la foto de las salidas.
//
// Todo en el hilo de salidas, como lo que reemplaza.
// ============================================================================

#include "emision/BroadcastEngine.hpp"

#include "Pedido.hpp"
#include "Salidas.hpp"
#include "i18n/I18n.hpp"
#include "ipc/CanalesSalida.hpp"
#include "persist/Escritor.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace ba::app {

using json = nlohmann::json;

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

void BroadcastEngine::leerEmisor(Salida& s) {
    const std::int64_t ahora = ahoraMsMono();
    if (s.pedidoEmisor && ahora - s.estadoEmisorMs < 500) return;
    s.estadoEmisorMs = ahora;
    // Lo pedido antes (por esta corrida o la anterior, si la app vuelve tras caerse).
    if (!s.pedidoEmisor) {
        s.pedidoEmisor = std::make_unique<emisor::Pedido>();
        emisor::leer(emisor::rutaPedido(runtime(), s.id), *s.pedidoEmisor);
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
    if (!emisor::escribir(emisor::rutaPedido(runtime(), s.id), p, &err))
        return ba::i18n::llenar(ba::i18n::traducir("emision", "no se pudo pedir al emisor de «%1»: %2"), {s.id, err});
    *s.pedidoEmisor = std::move(p);
    return {};
}

void BroadcastEngine::fotoDelEmisor(const Salida& s, json& f) const {
    const json& e = s.estadoEmisor;
    const emisor::Pedido* p = s.pedidoEmisor.get();
    const bool vivo = fresco(e);
    // Lo que muestra el estado es lo pedido recién cuando el emisor ya aplicó ese pedido;
    // antes (o con el emisor caído: el launcher lo relanza y retoma), lo pedido.
    const bool aplicado = vivo && p != nullptr && e.value("seq", std::uint64_t{0}) >= p->seq;
    if (p != nullptr && p->transmision) {
        json t = aplicado ? objeto(e, "transmision") : json::object();
        if (t.empty()) t = {{"conectada", false}, {"reconectando", false}, {"kbps", 0.0}, {"bytes", 0}, {"cuadros", 0}, {"perdidos", 0}};
        t["andando"] = true; // pedida (aunque esté reconectando), como Transmision::estado
        f["principal"] = std::move(t);
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
    f["emisor"] = {{"vivo", vivo},
                   {"placa", e.value("placa", false)},
                   {"placas", e.value("placas", 0)},
                   {"edad_entrada", e.value("edad_entrada", json())},
                   {"error", e.value("error", std::string())},
                   {"pid", e.value("pid", 0)}};
}

bool BroadcastEngine::aireEnEmisor(const Salida& s) const { return s.pedidoEmisor && s.pedidoEmisor->hayAire(); }

} // namespace ba::app
