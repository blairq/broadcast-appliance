// ============================================================================
// src/gateway/AdminApi.cpp — API de control de la sala (operador)
//
// Proposito:
//   La UNICA forma de operar la conferencia. De esta API cuelga la vista Qt de
//   la app (que es la que la expone al resto: el MCP y el dashboard web /control/);
//   ninguna vista tiene logica propia de sala.
//
//   Escucha SIEMPRE en 127.0.0.1 (puerto `adminPort`, 8788 por defecto).
//
// Autenticacion: `Authorization: Bearer <token>` (la app Qt; el token lo publica
//   B en Runtime/gateway-admin.json (dentro del bundle) con permisos solo-usuario).
//   No hay cookie ni página propia: la vieja consola web (/admin/) se reemplazó por
//   el dashboard /control/, que habla con la app (no con esta API).
//
// Rutas (JSON; errores {ok:false, error}):
//   GET    /api/v1/room                    estado completo (snapshot)
//   GET    /api/v1/events                  SSE: "room" (snapshot) y "chat"
//   GET    /api/v1/network                 HTTPS, tunel WireGuard, IPs y enlaces
//   GET    /api/v1/prompter                estado del teleprompter (+ la clave y quien esta conectado)
//   POST   /api/v1/prompter                {texto?, titulo?, panico?, ...comandos}
//   GET    /api/v1/prompter-link?target=local|remote&vista=control|orador&conClave=0|1  enlace + QR
//   GET    /api/v1/cast-link?target=local|remote&nombre=  enlace del programa en vivo para una pantalla
//   GET    /api/v1/carruseles              los publicados
//   GET    /api/v1/superficies             que paginas estan VIVAS (latido, ver Superficies.hpp)
//   POST   /api/v1/carruseles              publica uno entero (o {borrar: "<id>"})
//   GET    /api/v1/rotulos                estado de los rotulos (+ la clave)
//   POST   /api/v1/rotulos                {estado?, tomar?} publica y se lleva los encargos
//   GET    /api/v1/rotulos-link?target=local|remote&conClave=0|1  enlace + QR para el celular
//   GET    /api/v1/qr?target=local|remote[&pin=1]  URL de ingreso (+PIN) + matriz del QR
//   POST   /api/v1/network/server-config   {endpoint, publicAddress?} -> wg0.conf del servidor
//   POST   /api/v1/room                    {lobby?, locked?, intake?}
//   POST   /api/v1/pin                     {pin?}  (vacio = generar)
//   POST   /api/v1/participants/<id>/admit | reject | kick
//   POST   /api/v1/participants/<id>/request   {what}
//   POST   /api/v1/participants/<id>/talk      {on}  (lobby: en privado; adentro: IFB)
//   POST   /api/v1/participants/<id>/atiende   {on}  (lobby: lo atiende el asistente hablando)
//   POST   /api/v1/participants/<id>/prueba    {ms}  prueba técnica: eco y red (bloquea ms)
//   POST   /api/v1/participants/<id>/onair     {on}  modo oradores: al aire / fuera
//   POST   /api/v1/participants/<id>/move      {index}  lugar en la fila (0 = primero)
//   POST   /api/v1/participants/<id>/time      {sec}  tiempo propio para hablar
//   POST   /api/v1/participants/<id>/requeue   vuelve al final de la fila
//   POST   /api/v1/participants/<id>/presenter  {on} presentador fijo
//   POST   /api/v1/participants/<id>/profile   {name?, info?, contact?}
//   GET    /api/v1/participants/<id>/avatar    foto de la tarjeta (JPEG)
//   POST   /api/v1/speakers                {enabled?, defaultSec?, carousel?, graceSec?,
//                                           multi?, defaultSlot?}
//   POST   /api/v1/speakers/next           al aire el siguiente de la fila
//   GET    /api/v1/board-link?target=local|remote  enlace + QR del tablero (tablet)
//   GET    /api/v1/sesion/origen?target=local|remote  origen para los links de la sesión remota
//   …/api/v1/sesion/*                       la sesión remota (ver Sesion.hpp)
//   POST   /api/v1/view                    {participant, kind?:"camera"|"screen", sdp}
//                                          -> {viewer, sdp}: video en vivo (relay de B)
//   POST   /api/v1/view/<id>/keyframe      la vista se congelo
//   DELETE /api/v1/view/<id>
//   POST   /api/v1/chat                    {text, to?}
//   GET    /api/v1/shares/<id>/content     el archivo (vista previa)
//   DELETE /api/v1/shares/<id>
// ============================================================================

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

#include "HttpIo.hpp"
#include "proxy/GestorProxy.hpp"
#include "Log.hpp"
#include "qrcodegen.hpp"
#include "Room.hpp"
#include "security/CredentialStore.hpp"
#include "security/SecretsStore.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "MediaPump.hpp"

#include "diag/Registro.hpp"
#include "ajustes/Ajustes.hpp"

namespace ba::gateway {

using json = nlohmann::json;

namespace {

/// La fecha local de hoy (AAAA-MM-DD): el PIN de la sala cambia una vez por día.
std::string hoyLocal() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

constexpr const char* kPinDelDia = "sala-pin-del-dia";

/// El reloj de pared, para fechar lo que entra por aca.
inline std::int64_t nowMsAdmin() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t nowMsA() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Comparacion en tiempo constante (no filtra el prefijo correcto por timing).
bool sameSecret(const std::string& a, const std::string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

void sendJson(long long fd, int code, const json& j) {
    http::sendResponse(fd, code, "application/json", j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

void sendErr(long long fd, int code, const char* error) {
    sendJson(fd, code, json{{"ok", false}, {"error", error}});
}

/// Room::Err -> respuesta HTTP.
void sendRoomResult(long long fd, int err) {
    switch (err) {
        case Room::kOk: sendJson(fd, 200, json{{"ok", true}}); return;
        case Room::kNotFound: sendErr(fd, 404, "not_found"); return;
        case Room::kFull: sendErr(fd, 409, "full"); return;
        case Room::kEmpty: sendErr(fd, 400, "empty"); return;
        default: sendErr(fd, 409, "state"); return;
    }
}

} // namespace

bool SignalingServer::adminAuthorized(const std::string& headers, const std::string& method) const {
    const std::string auth = http::getHeader(headers, "Authorization");
    (void)method;
    return auth.rfind("Bearer ", 0) == 0 && sameSecret(auth.substr(7), cfg_.adminToken);
}

std::string SignalingServer::changePin(const std::string& pin) {
    std::string p = pin;
    if (p.empty()) {
        std::string hex;
        if (!ba::security::randomSecretHex(16, hex)) return {};
        // 6 digitos a partir de 32 bits aleatorios (sesgo del modulo ~0.02%).
        const unsigned long v = std::stoul(hex.substr(0, 8), nullptr, 16) % 1000000UL;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%06lu", v);
        p = buf;
    }
    auth_.setPin(p);
    const json snap = room_.snapshot();
    room_.setHostInfo(p, snap.value("guestUrl", ""));
    guardarPinDeHoy(p); // los reinicios de la sala de hoy siguen con este
    log_.info("control: PIN de la sala cambiado");
    return p;
}

std::string SignalingServer::pinDeHoy() {
    std::string datos;
    if (!ba::security::CredentialStore::bundle().read(kPinDelDia, datos)) return {};
    const json j = json::parse(datos, nullptr, false);
    if (!j.is_object() || j.value("fecha", "") != hoyLocal()) return {};
    const std::string pin = j.value("pin", "");
    return pin.size() >= 4 && pin.size() <= 32 ? pin : std::string();
}

void SignalingServer::guardarPinDeHoy(const std::string& pin) {
    if (pin.empty()) return;
    ba::security::CredentialStore::bundle().write(kPinDelDia, json{{"fecha", hoyLocal()}, {"pin", pin}}.dump());
}

void SignalingServer::handleAdmin(long long fd, const std::string& method, const std::string& path,
                                  const std::string& query, const std::string& headers,
                                  const std::string& body) {
    // --- Sin autenticacion: la salud ------------------------------------------
    if (path == "/healthz") {
        http::sendResponse(fd, 200, "text/plain", "ok\n");
        return;
    }
    if (path == "/" && method == "GET") {
        http::sendResponse(fd, 200, "text/plain", "API de control de la sala (solo para la app).\n");
        return;
    }

    // --- Todo lo demas pide credenciales ------------------------------------
    if (!adminAuthorized(headers, method)) {
        sendErr(fd, 401, "auth");
        return;
    }
    // La botonera (superficie de control): la app le pasa credenciales y estado.
    if (botonera_ && botonera_->admin(fd, method, path, body)) return;
    // El control web (dashboard /control/): credenciales, aprobaciones y lo que se empuja.
    if (controlWeb_ && controlWeb_->admin(fd, method, path, body)) return;
    // La sesión remota: el origen para armar los links personales (la app agrega
    // /sesion/#i=…&t=…) y después credenciales, mensajes y fotos de la cámara.
    if (path == "/api/v1/sesion/origen" && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        const std::string servicio = q.count("servicio") ? http::urlDecode(q.at("servicio")) : "sesion";
        json out = origenDe(servicio, target);
        if (out.contains("origen")) {
            const std::string origin = out["origen"].get<std::string>();
            out = json{{"ok", true}, {"target", target}, {"origen", origin},
                       {"https", origin.rfind("https://", 0) == 0}};
        } else {
            out["ok"] = false;
        }
        sendJson(fd, 200, out);
        return;
    }
    // El programa en vivo para la consola de la presidencia: la misma página de los
    // televisores (/cast/), con su token; solo la ruta (la consola la abre en su origen).
    if (path == "/api/v1/sesion/cast" && method == "GET") {
        const std::string token = auth_.issue(cfg_.room, room_.snapshot().value("pin", ""));
        if (token.empty()) {
            sendJson(fd, 200, json{{"ok", false}, {"error", "sin token"}});
            return;
        }
        sendJson(fd, 200, json{{"ok", true}, {"url", "/cast/?token=" + token + "&nombre=Presidencia"}});
        return;
    }
    if (sesion_ && sesion_->admin(fd, method, path, query, body)) return;

    const json req = body.empty() ? json::object() : json::parse(body, nullptr, false);
    if (!req.is_object()) {
        sendErr(fd, 400, "json");
        return;
    }

    if (path == "/api/v1/room") {
        if (method == "GET") {
            sendJson(fd, 200, room_.snapshot());
            return;
        }
        if (method == "POST") {
            if (req.contains("lobby") && req["lobby"].is_boolean())
                room_.setLobby(req["lobby"].get<bool>(), nowMsA());
            if (req.contains("locked") && req["locked"].is_boolean())
                room_.setLocked(req["locked"].get<bool>());
            if (req.contains("intake") && req["intake"].is_boolean())
                room_.setIntake(req["intake"].get<bool>());
            if (req.contains("cupo") && req["cupo"].is_number_integer()) {
                const int n = req["cupo"].get<int>();
                // Los canales de los lugares nuevos, ANTES de que la sala pueda
                // admitir a alguien ahí (sin lobby entra solo): sin reiniciar.
                if (pump_ != nullptr && n >= 1 && n <= kMaxInvitados) pump_->ampliar(n);
                const int r = room_.setCupo(n);
                if (r != Room::kOk) {
                    sendJson(fd, 409, json{{"ok", false},
                                           {"error", r == Room::kFull ? "hay invitados en lugares que se irían"
                                                                      : "el cupo va de 1 a " + std::to_string(kMaxInvitados)}});
                    return;
                }
            }
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
    }
    if (path == "/api/v1/proxy") {
        sendJson(fd, 200, proxy_ != nullptr ? proxy_->estado() : json{{"activo", false}, {"motivo", "apagado en la configuración"}});
        return;
    }
    if (path == "/api/v1/proxy/renovar" && method == "POST") {
        if (proxy_ == nullptr) {
            sendJson(fd, 409, json{{"ok", false}, {"error", "el proxy está apagado"}});
            return;
        }
        proxy_->renovar(req.value("host", std::string()));
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }
    if (path == "/api/v1/network/server-config" && method == "POST") {
        if (!serverConfig_) {
            sendErr(fd, 404, "route");
            return;
        }
        const json out = serverConfig_(req);
        sendJson(fd, out.value("ok", false) ? 200 : 400, out);
        return;
    }
    if (path == "/api/v1/qr" && method == "GET") {
        // QR de ingreso al lobby: la URL la decide main (red local o dominio
        // de Cloudflare); aca solo se codifica. Cada vista lo dibuja con la
        // matriz (filas de '0'/'1'), sin depender de ninguna biblioteca.
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = origenDe("invitados", target);
        if (!out.contains("url")) {
            // 200 con ok=false: falta configuracion, no es una falla de la
            // API; la vista muestra `message` en el lugar del QR.
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        // La sala va en el fragmento (#room=): la app del invitado la precarga y el
        // fragmento nunca viaja al servidor.
        std::string url = out["url"].get<std::string>() + "#room=" + cfg_.room;
        out["url"] = url;
        out.erase("origen");
        // Con pin=1 el PIN viaja en el FRAGMENTO del enlace (no llega a ningun
        // servidor ni a sus logs; la app del invitado lo toma y lo borra de la
        // barra de direcciones). Quien tenga el QR entra sin tipear el PIN.
        const bool withPin = q.count("pin") && q.at("pin") == "1";
        if (withPin) {
            const std::string pin = room_.snapshot().value("pin", "");
            if (!pin.empty()) url += (url.find('#') == std::string::npos ? "#pin=" : "&pin=") + pin;
            out["url"] = url;
        }
        out["withPin"] = withPin;
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        out["ok"] = true;
        out["target"] = target;
        out["size"] = qr.getSize();
        out["modules"] = rows;
        sendJson(fd, 200, out);
        return;
    }
    if (path == "/api/v1/view" && method == "POST") {
        apiView(fd, body, false);
        return;
    }
    static const std::string kView = "/api/v1/view/";
    if (path.rfind(kView, 0) == 0) {
        apiViewAction(fd, method, path.substr(kView.size()), false);
        return;
    }
    if (path == "/api/v1/speakers" && method == "POST") {
        sendRoomResult(fd, room_.setSpeakerConfig(req, nowMsA()));
        return;
    }
    if (path == "/api/v1/speakers/next" && method == "POST") {
        sendRoomResult(fd, room_.nextSpeaker(nowMsA()));
        return;
    }
    if (path == "/api/v1/board-link" && method == "GET") {
        // Mismo origen que el ingreso de invitados (red local o dominio), con
        // la clave del tablero en el FRAGMENTO (no viaja en la URL pedida).
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = origenDe("tablero", target);
        if (!out.contains("origen")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["origen"].get<std::string>();
        const std::string url = origin + "/board/#k=" + boardToken();
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        sendJson(
            fd, 200,
            json{{"ok", true}, {"target", target}, {"url", url}, {"size", qr.getSize()}, {"modules", rows}});
        return;
    }
    // --- Teleprompter -------------------------------------------------------
    if (path == "/api/v1/prompter") {
        // A ve siempre la clave del control: la necesita para armar el enlace
        // que le manda al operador. Por la superficie de invitados no sale.
        const auto conClave = [this](json e) {
            e["clave"] = cfg_.prompterKey;
            e["clientes"] = prompter_.clientes();
            return e;
        };
        if (method == "GET") {
            sendJson(fd, 200, conClave(prompter_.estado()));
            return;
        }
        if (method == "POST") {
            // A comanda sin clave: ya paso por el token de la API de control.
            if (req.contains("texto") && req["texto"].is_string()) {
                prompter_.cargar(req.value("titulo", std::string()), req["texto"].get<std::string>());
            }
            if (req.value("panico", false)) prompter_.pararEnSeco();
            prompter_.comandar(req);
            sendJson(fd, 200, conClave(prompter_.estado()));
            return;
        }
    }
    // --- Rotulos ------------------------------------------------------------
    // El buzon de dos direcciones con la app: A publica lo que hay y se lleva
    // lo que encargaron desde los celulares. Ver Rotulador.hpp.
    if (path == "/api/v1/rotulos") {
        if (method == "GET") {
            json e = rotulador_.estado();
            e["clave"] = cfg_.rotulosKey;
            sendJson(fd, 200, e);
            return;
        }
        if (method == "POST") {
            if (req.contains("estado")) rotulador_.publicar(req["estado"]);
            json out{{"ok", true}};
            // `tomar` en el mismo pedido: A publica y se lleva los encargos de
            // una, que es un viaje por segundo en vez de dos.
            if (req.value("tomar", false)) out["encargos"] = rotulador_.tomarEncargos();
            sendJson(fd, 200, out);
            return;
        }
    }
    if (path == "/api/v1/rotulos-link" && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = origenDe("rotulos", target);
        if (!out.contains("origen")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["origen"].get<std::string>();
        // La clave va en el FRAGMENTO: no viaja en la peticion ni queda en los
        // registros de ningun servidor por el que pase el enlace. `conClave=0`
        // la deja afuera: el QR entra a la pagina y la clave se la dice el
        // operador aparte, que es lo que se quiere si el QR va a quedar pegado
        // en una pared.
        const bool conClave = !q.count("conClave") || http::urlDecode(q.at("conClave")) != "0";
        const std::string url = origin + "/rotulos/" + (conClave ? "#k=" + cfg_.rotulosKey : std::string());
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        sendJson(fd, 200,
                 json{{"ok", true},
                      {"target", target},
                      {"url", url},
                      {"conClave", conClave},
                      {"clave", cfg_.rotulosKey},
                      {"size", qr.getSize()},
                      {"modules", rows}});
        return;
    }

    // --- Las preguntas del publico -------------------------------------------
    if (path == "/api/v1/preguntas") {
        if (method == "GET") {
            sendJson(fd, 200,
                     json{{"ok", true},
                          {"preguntas", preguntas_.lista()},
                          {"recuento", preguntas_.recuento()},
                          {"proxima", preguntas_.proxima()},
                          {"clave", cfg_.publicoKey}});
            return;
        }
        if (method == "POST") {
            const std::string accion = req.value("accion", std::string());
            const std::string id = req.value("id", std::string());
            bool ok = false;
            if (accion == "elegir")
                ok = preguntas_.elegir(id);
            else if (accion == "des_elegir")
                ok = preguntas_.desElegir(id);
            else if (accion == "al_aire")
                ok = preguntas_.alAire(id);
            else if (accion == "responder")
                ok = preguntas_.responder(id);
            else if (accion == "descartar")
                ok = preguntas_.descartar(id);
            else if (accion == "limpiar") {
                preguntas_.limpiar();
                ok = true;
            } else if (accion == "vaciar") {
                preguntas_.vaciar();
                ok = true;
            } else if (accion == "preguntar") {
                // Para meter en la cola lo que llego por otro lado (el chat de
                // la sala, o el de YouTube): una sola cola para todos, que es
                // de lo que se trata.
                ok = !preguntas_
                          .preguntar(req.value("nombre", std::string()), req.value("texto", std::string()),
                                     req.value("deDonde", std::string("sala")), std::string(), nowMsAdmin())
                          .empty();
            } else {
                sendErr(fd, 400, "accion");
                return;
            }
            sendJson(fd, 200,
                     json{{"ok", ok},
                          {"preguntas", preguntas_.lista()},
                          {"recuento", preguntas_.recuento()},
                          {"proxima", preguntas_.proxima()}});
            return;
        }
    }

    // A viene a vaciar el buzon de los que escanearon su invitacion.
    if (path == "/api/v1/acreditaciones" && method == "GET") {
        json cs = json::array();
        for (const std::string& c : tomarAcreditaciones())
            cs.push_back(c);
        sendJson(fd, 200, json{{"ok", true}, {"codigos", cs}});
        return;
    }

    // Lo que la pagina del publico muestra arriba y de fondo. Lo manda A: es
    // el unico que sabe que evento esta abierto.
    if (path == "/api/v1/publico-info" && method == "POST") {
        if (req.contains("info")) setPublicoInfo(req["info"]);
        if (req.contains("fondo")) {
            // Base64 porque viaja en JSON. Vacio = sacar el fondo, que tiene
            // que poder hacerse sin reiniciar nada.
            const std::string b64 = req["fondo"].is_string() ? req["fondo"].get<std::string>() : "";
            setPublicoFondo(b64.empty() ? std::string() : http::deBase64(b64),
                            req.value("fondoTipo", std::string("image/jpeg")));
        }
        sendJson(fd, 200, json{{"ok", true}, {"info", publicoInfo()}});
        return;
    }

    // La página del evento en vivo: la manda A con el evento abierto (y cada vez que
    // el guion avanza). {activo, dir, estado}.
    if (path == "/api/v1/evento-web" && method == "POST") {
        setEventoWeb(req);
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }

    // --- Los bloqueos por PIN mal (por IP y el global) ------------------------
    if (path == "/api/v1/pin/bloqueos" && method == "GET") {
        sendJson(fd, 200, json{{"ok", true}, {"bloqueos", bloqueosPin()}});
        return;
    }
    if (path == "/api/v1/pin/desbloquear" && method == "POST") {
        const std::string ip = req.value("ip", std::string());
        const int n = desbloquearPin(ip);
        sendJson(fd, 200, json{{"ok", true}, {"desbloqueadas", n}, {"bloqueos", bloqueosPin()}});
        return;
    }

    // --- La urna, del lado del operador --------------------------------------
    if (path == "/api/v1/votacion") {
        if (method == "GET") {
            const auto q = http::parseQuery(query);
            json out{{"ok", true}, {"votacion", votacion_.estado()}};
            // El historial solo cuando se pide: en una asamblea larga son
            // muchas, y quien mira «como va» no las quiere todas.
            if (q.count("historial")) out["historial"] = votacion_.historial();
            sendJson(fd, 200, out);
            return;
        }
        if (method == "POST") {
            const std::string accion = req.value("accion", std::string());
            if (accion == "cerrar") {
                const bool ok = votacion_.cerrar();
                sendJson(fd, 200, json{{"ok", ok}, {"votacion", votacion_.estado()}});
                return;
            }
            if (accion == "olvidar_historial") {
                votacion_.olvidarHistorial();
                sendJson(fd, 200, json{{"ok", true}});
                return;
            }
            if (accion == "en_vivo") {
                votacion_.setMostrarEnVivo(req.value("si", false));
                sendJson(fd, 200, json{{"ok", true}, {"votacion", votacion_.estado()}});
                return;
            }
            if (accion != "abrir") {
                sendErr(fd, 400, "accion");
                return;
            }
            std::vector<std::string> opciones;
            if (req.contains("opciones") && req["opciones"].is_array())
                for (const json& o : req["opciones"])
                    if (o.is_string()) opciones.push_back(o.get<std::string>());
            ba::gateway::Votacion::Modo modo = ba::gateway::Votacion::Modo::Encuesta;
            ba::gateway::Votacion::modoDeNombre(req.value("modo", std::string("encuesta")), modo);
            // Los habilitados los manda A: es el unico que tiene el padron. B
            // solo sabe que codigos son validos, ni siquiera de quienes son mas
            // que el nombre para poder decir quien falta votar.
            std::map<std::string, std::string> habilitados;
            if (req.contains("habilitados") && req["habilitados"].is_array())
                for (const json& h : req["habilitados"])
                    if (h.is_object() && h.contains("codigo"))
                        habilitados[h["codigo"].get<std::string>()] = h.value("nombre", std::string());
            const bool ok = votacion_.abrir(req.value("pregunta", std::string()), opciones, modo,
                                            req.value("secreta", false), habilitados, req.value("podian", 0),
                                            nowMsAdmin());
            if (!ok) {
                sendErr(fd, 400, "hace falta una pregunta y al menos dos opciones");
                return;
            }
            votacion_.setMostrarEnVivo(req.value("enVivo", false));
            sendJson(fd, 200, json{{"ok", true}, {"votacion", votacion_.estado()}});
            return;
        }
    }

    if (path == "/api/v1/publico-link" && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = origenDe("publico", target);
        if (!out.contains("origen")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["origen"].get<std::string>();
        // Con la clave adentro casi siempre: este QR se proyecta en una
        // pantalla o se imprime en el programa, y pedirle a doscientas personas
        // que tipeen una clave es perder a la mitad.
        const bool conClave = !q.count("conClave") || http::urlDecode(q.at("conClave")) != "0";
        const std::string url = origin + "/publico/" + (conClave ? "#k=" + cfg_.publicoKey : std::string());
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        sendJson(fd, 200,
                 json{{"ok", true},
                      {"target", target},
                      {"url", url},
                      {"conClave", conClave},
                      {"clave", cfg_.publicoKey},
                      {"size", qr.getSize()},
                      {"modules", rows}});
        return;
    }
    // Lo que no es letra, numero o guion va porcentado: el nombre lo escribe
    // una persona ("TV del hall") y va en una URL.
    const auto porcentar = [](const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        std::string o;
        for (const unsigned char c : s) {
            if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
                o.push_back(static_cast<char>(c));
            } else {
                o.push_back('%');
                o.push_back(hex[c >> 4]);
                o.push_back(hex[c & 0xf]);
            }
        }
        return o;
    };
    // EMPUJAR: cambiarle a una superficie lo que muestra, SIN volver a
    // castearla. Es la mitad que le faltaba al latido: una dice como esta, la
    // otra la maneja. {a: "<id>"|"" (todas), tipo?: solo las de ese tipo,
    // url?: que abrir, recargar?: que se recargue sola}.
    if (path == "/api/v1/superficies/mostrar" && method == "POST") {
        std::vector<std::string> destinos;
        if (req.contains("a") && req["a"].is_string() && !req["a"].get<std::string>().empty())
            destinos.push_back("superficie:" + req["a"].get<std::string>());
        else
            destinos = superficies_.conexionesDe(req.value("tipo", ""));
        json orden{{"type", req.value("recargar", false) ? "recargar" : "mostrar"}};
        if (req.contains("url")) orden["url"] = req["url"];
        for (const std::string& pid : destinos)
            sendToGuest(pid, orden);
        sendJson(fd, 200, json{{"ok", true}, {"a", destinos.size()}});
        return;
    }
    // EL TALLY lo publica A en cada cambio de escena. El gateway no sabe de
    // escenas ni quiere saber: guarda lo que le dan y lo sirve en
    // /pantalla/tally.json.
    if (path == "/api/v1/tally" && method == "POST") {
        {
            std::lock_guard<std::mutex> lock(tallyLock_);
            tally_ = req.is_object() ? req : json::object();
        }
        // Y se les avisa a las pantallas de tally que hay algo nuevo, para que
        // no tengan que preguntar cada medio segundo: un tally que llega tarde
        // es peor que no tenerlo, porque alguien se confia.
        for (const std::string& pid : superficies_.conexionesDe("tally"))
            sendToGuest(pid, json{{"type", "tally"}});
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }
    if (path == "/api/v1/superficies" && method == "GET") {
        sendJson(fd, 200, json{{"ok", true}, {"superficies", superficies_.vivas()}});
        return;
    }
    if (path == "/api/v1/carruseles") {
        if (method == "GET") {
            sendJson(fd, 200, json{{"ok", true}, {"carruseles", carruseles_.todos()}});
            return;
        }
        if (method == "POST") {
            if (req.contains("borrar")) {
                sendJson(fd, 200, json{{"ok", carruseles_.borrar(req["borrar"].get<std::string>())}});
                return;
            }
            if (!carruseles_.publicar(req)) {
                sendErr(fd, 400, "carrusel");
                return;
            }
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
    }

    // El enlace del PROGRAMA EN VIVO para una pantalla. Acuña un token de
    // ingreso (la pantalla entra como un invitado que no publica nada) y arma
    // la URL de /cast/. Va por HTTP a proposito: un televisor no tiene donde
    // aceptar nuestro certificado propio, asi que el HTTPS lo rechazaria.
    if (path == "/api/v1/cast-link" && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        const std::string nombre = q.count("nombre") ? http::urlDecode(q.at("nombre")) : "Pantalla";
        // `canal`: una ESCENA en vez del programa (BA.Escena.<k>, la que la app
        // puso en ese canal). La pantalla lo repite al entrar por /ws.
        std::string canal;
        if (q.count("canal")) {
            canal = http::urlDecode(q.at("canal"));
            if (canal.empty() || canal.size() > 2 ||
                !std::all_of(canal.begin(), canal.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                sendJson(fd, 200, json{{"ok", false}, {"message", "canal de escena invalido"}});
                return;
            }
        }
        if (room_.snapshot().value("locked", false)) {
            sendJson(fd, 200,
                     json{{"ok", false},
                          {"message",
                           "La sala esta cerrada: abrila para poder mandarle el programa a una pantalla."}});
            return;
        }
        const std::string token = auth_.issue(cfg_.room, room_.snapshot().value("pin", ""));
        if (token.empty()) {
            sendJson(
                fd, 200,
                json{{"ok", false}, {"message", "No pude sacar un permiso de ingreso para la pantalla."}});
            return;
        }
        json out = origenDe("pantalla", target);
        if (!out.contains("origen")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["origen"].get<std::string>();
        // Un televisor no tiene dónde aceptar un certificado propio: el servicio
        // «pantalla» ya es la puerta HTTP de la sala (Direcciones.hpp).
        sendJson(fd, 200,
                 json{{"ok", true},
                      {"url", origin + "/cast/?token=" + token + "&nombre=" + porcentar(nombre) +
                                  (canal.empty() ? std::string() : "&canal=" + canal)},
                      {"token", token}});
        return;
    }
    // T44: la app le pasa las frases que esperan; y pide el enlace del puesto (con QR).
    if (path == "/api/v1/correccion/pendientes" && method == "POST") {
        {
            std::lock_guard<std::mutex> l(correccionMu_);
            correccionPend_ = req.value("pendientes", json::array());
            correccionMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }
    if (path == "/api/v1/correccion-link" && method == "GET") {
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        json out = origenDe("correccion", target);
        if (!out.contains("origen")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        const std::string aviso = out.value("warning", std::string());
        const std::string origin = out["origen"].get<std::string>();
        const std::string url = origin + "/correccion/#k=" + cfg_.correccionKey;
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x) row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        json resp{{"ok", true}, {"url", url}, {"size", qr.getSize()}, {"modules", rows}};
        if (!aviso.empty()) resp["message"] = aviso;
        sendJson(fd, 200, resp);
        return;
    }
    if (path == "/api/v1/prompter-link" && method == "GET") {
        // El enlace del control, con la clave en el FRAGMENTO para que no
        // viaje en la URL pedida ni quede en los registros del servidor.
        const auto q = http::parseQuery(query);
        const std::string target = q.count("target") ? http::urlDecode(q.at("target")) : "local";
        const bool control = !q.count("vista") || http::urlDecode(q.at("vista")) == "control";
        // La clave puede ir DENTRO del enlace (se entra sin tipear) o no: un QR
        // pegado en una pared no deberia llevarla.
        const bool conClave = !q.count("conClave") || http::urlDecode(q.at("conClave")) != "0";
        json out = origenDe("prompter", target);
        if (!out.contains("origen")) {
            out["ok"] = false;
            sendJson(fd, 200, out);
            return;
        }
        std::string origin = out["origen"].get<std::string>();
        // La tablet del orador no lleva clave nunca: solo lee.
        const std::string url = origin + "/prompter/" +
                                (control ? (conClave ? "#control&k=" + cfg_.prompterKey : "#control") : "");
        const qrcodegen::QrCode qr =
            qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        json rows = json::array();
        for (int y = 0; y < qr.getSize(); ++y) {
            std::string row;
            for (int x = 0; x < qr.getSize(); ++x)
                row.push_back(qr.getModule(x, y) ? '1' : '0');
            rows.push_back(row);
        }
        sendJson(fd, 200,
                 json{{"ok", true},
                      {"vista", control ? "control" : "orador"},
                      {"conClave", control && conClave},
                      {"clave", cfg_.prompterKey},
                      {"url", url},
                      {"size", qr.getSize()},
                      {"modules", rows}});
        return;
    }

    // El nivel de registro de B, en vivo. Sin esto solo se podia fijar al
    // arrancar (BA_LOG_NIVEL), o sea que para ver mas detalle de algo que esta
    // pasando AHORA habia que reiniciar el gateway y perder lo que se queria
    // mirar.
    if (path == "/api/v1/log-nivel") {
        auto& reg = ba::diag::registro();
        if (method == "GET") {
            sendJson(fd, 200,
                     json{{"nivel", ba::diag::nombreDeNivel(reg.nivel())},
                          {"descartadas", reg.descartadas()},
                          {"registradas", reg.cuenta()}});
            return;
        }
        if (method == "POST") {
            ba::diag::Nivel n{};
            if (!req.contains("nivel") || !req["nivel"].is_string() ||
                !ba::diag::nivelDeNombre(req["nivel"].get<std::string>(), n)) {
                sendJson(fd, 400, json{{"ok", false}, {"error", "nivel desconocido"}});
                return;
            }
            reg.setNivel(n);
            // Queda anotado en el propio registro: si manana aparece un log
            // lleno de trazas, tiene que decir quien lo puso asi.
            reg.linea(ba::diag::Nivel::Info, "registro", 0,
                      std::string("nivel cambiado a ") + ba::diag::nombreDeNivel(n) +
                          " desde la API de control");
            sendJson(fd, 200, json{{"ok", true}, {"nivel", ba::diag::nombreDeNivel(n)}});
            return;
        }
    }
    // Acceso remoto (pinggy; docs/planes/PINGGY.md). La lee la app (Configuración
    // → Acceso remoto y el MCP). La forma de "pinggy" es fija: sin proveedor, apagado.
    if ((path == "/api/remoto" || path == "/api/v1/remoto") && method == "GET") {
        json out = remotoStatus_ ? remotoStatus_() : json::object();
        if (!out.is_object()) out = json::object();
        if (!out.contains("pinggy"))
            out["pinggy"] = json{{"activo", false},
                                 {"estado", "apagado"},
                                 {"urls", json::array()},
                                 {"urlPublica", ""},
                                 {"error", ""},
                                 {"turn", {{"publico", ""}, {"tipo", ""}, {"udp", ""}}}};
        sendJson(fd, 200, out);
        return;
    }
    if (path == "/api/v1/direcciones" && method == "GET") {
        sendJson(fd, 200, direcciones());
        return;
    }
    if (path == "/api/v1/tema" && method == "POST") {
        if (!setTemaWeb(req.value("tema", std::string()))) {
            sendErr(fd, 400, "tema");
            return;
        }
        sendJson(fd, 200, json{{"ok", true}, {"tema", temaWeb()}});
        return;
    }
    if (path == "/api/v1/network" && method == "GET") {
        sendJson(fd, 200, networkStatus_ ? networkStatus_() : json::object());
        return;
    }
    if (path == "/api/v1/events" && method == "GET") {
        adminEvents(fd);
        return;
    }
    if (path == "/api/v1/pin" && method == "POST") {
        const std::string pin = req.value("pin", "");
        if (!pin.empty() && (pin.size() < 4 || pin.size() > 32)) {
            sendErr(fd, 400, "pin");
            return;
        }
        // {"si_cambio_el_dia": true}: solo si el PIN vigente no es el de hoy
        // (al empezar una sesión en vivo: el mismo día, el mismo PIN).
        if (pin.empty() && req.value("si_cambio_el_dia", false)) {
            const std::string vigente = room_.snapshot().value("pin", "");
            if (!vigente.empty() && vigente == pinDeHoy()) {
                sendJson(fd, 200, json{{"ok", true}, {"pin", vigente}, {"cambio", false}});
                return;
            }
        }
        const std::string now = changePin(pin);
        if (now.empty()) {
            sendErr(fd, 500, "entropy");
            return;
        }
        sendJson(fd, 200, json{{"ok", true}, {"pin", now}, {"cambio", true}});
        return;
    }
    if (path == "/api/v1/chat" && method == "POST") {
        sendRoomResult(fd, room_.chatFromHost(req.value("to", "all"), req.value("text", ""), nowMsA()));
        return;
    }
    // /api/v1/participants/<id>/<accion>
    static const std::string kPart = "/api/v1/participants/";
    if (path.rfind(kPart, 0) == 0 && method == "GET") {
        const std::string rest = path.substr(kPart.size());
        const size_t slash = rest.find('/');
        if (slash != std::string::npos && rest.substr(slash + 1) == "avatar") {
            sendAvatar(fd, rest.substr(0, slash));
            return;
        }
    }
    if (path.rfind(kPart, 0) == 0 && method == "POST") {
        const std::string rest = path.substr(kPart.size());
        const size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            sendErr(fd, 404, "route");
            return;
        }
        const std::string id = rest.substr(0, slash);
        const std::string action = rest.substr(slash + 1);
        if (action == "admit") {
            sendRoomResult(fd, room_.admit(id, nowMsA()));
        } else if (action == "reject") {
            sendRoomResult(fd, room_.reject(id));
        } else if (action == "kick") {
            sendRoomResult(fd, room_.kick(id));
        } else if (action == "talk") {
            sendRoomResult(fd, room_.setTalk(id, req.value("on", true)));
        } else if (action == "atiende") {
            // La atención hablada del lobby (la app la prende y la apaga).
            sendRoomResult(fd, room_.setAtiende(id, req.value("on", true)));
        } else if (action == "prueba") {
            // Prueba técnica (T33): eco por su micrófono y estado de la red. Tarda lo
            // que dura (2 a 8 s): este pedido queda esperando, la sala sigue.
            int sesion = 0;
            {
                std::lock_guard<std::mutex> l(sessLock_);
                const auto it = sessionOf_.find(id);
                if (it != sessionOf_.end()) sesion = it->second;
            }
            if (pump_ == nullptr || sesion == 0) {
                sendJson(fd, 409, json{{"ok", false}, {"error", "ese participante no está mandando medios"}});
            } else {
                const json r = json::parse(pump_->pruebaTecnica(sesion, req.value("ms", 4000)), nullptr, false);
                sendJson(fd, r.is_object() && !r.contains("error") ? 200 : 409, r);
            }
        } else if (action == "request") {
            sendRoomResult(fd, room_.request(id, req.value("what", "")));
        } else if (action == "onair") {
            sendRoomResult(fd, room_.setOnAir(id, req.value("on", true), nowMsA()));
        } else if (action == "move") {
            sendRoomResult(fd, room_.moveSpeaker(id, req.value("index", 0)));
        } else if (action == "time") {
            sendRoomResult(fd, room_.setAllotted(id, req.value("sec", 0)));
        } else if (action == "requeue") {
            sendRoomResult(fd, room_.requeue(id));
        } else if (action == "presenter") {
            sendRoomResult(fd, room_.setPresenter(id, req.value("on", true), nowMsA()));
        } else if (action == "profile") {
            sendRoomResult(fd, room_.setProfile(id, req));
        } else {
            sendErr(fd, 404, "route");
        }
        return;
    }

    // /api/v1/shares/<id>[/content]
    static const std::string kShares = "/api/v1/shares/";
    if (path.rfind(kShares, 0) == 0) {
        const std::string rest = path.substr(kShares.size());
        const size_t slash = rest.find('/');
        const std::string id = rest.substr(0, slash);
        if (method == "GET" && slash != std::string::npos && rest.substr(slash + 1) == "content") {
            adminShareContent(fd, id);
            return;
        }
        if (method == "DELETE" && slash == std::string::npos) {
            std::string file;
            if (!room_.removeShare(id, &file)) {
                sendErr(fd, 404, "not_found");
                return;
            }
            std::error_code ec;
            std::filesystem::remove(pathFromUtf8(file), ec);
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
    }
    sendErr(fd, 404, "route");
}

void SignalingServer::adminShareContent(long long fd, const std::string& id) {
    Room::Share s;
    if (!room_.share(id, s)) {
        sendErr(fd, 404, "not_found");
        return;
    }
    std::ifstream f(pathFromUtf8(s.path), std::ios::binary);
    if (!f) {
        sendErr(fd, 404, "file");
        return;
    }
    // Cabecera y cuerpo en streaming (un video puede pesar cientos de MB).
    std::string head = "HTTP/1.1 200 OK\r\nContent-Type: " + s.mime +
                       "\r\nContent-Length: " + std::to_string(s.bytes) +
                       "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                       "Content-Security-Policy: sandbox\r\nConnection: close\r\n\r\n";
    if (!http::sendAll(fd, head.data(), head.size())) return;
    std::vector<char> buf(256 * 1024);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n <= 0) break;
        if (!http::sendAll(fd, buf.data(), static_cast<size_t>(n))) return;
    }
}

void SignalingServer::adminEvents(long long fd) {
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                             "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n";
    if (!http::sendAll(fd, head.data(), head.size())) return;
    // Los recv solo ocurren despues de waitReadable; el timeout acota los
    // ENVIOS: un operador colgado no debe trabar a la sala que le escribe.
    http::setSocketTimeout(fd, 5000);

    auto conn = std::make_shared<SseConn>();
    conn->fd = fd;
    {
        // Primero el estado completo; despues, cambios. Se registra con el
        // envio del snapshot bajo su lock para que ningun evento se adelante.
        std::lock_guard<std::mutex> sendLock(conn->sendLock);
        {
            std::lock_guard<std::mutex> lock(sseLock_);
            sse_.push_back(conn);
        }
        const std::string first = "retry: 2000\n\nevent: room\ndata: " + room_.snapshot().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n\n";
        http::sendAll(fd, first.data(), first.size());
    }

    // El cliente no manda nada: se espera su cierre, con un latido cada 15 s
    // que ademas detecta conexiones muertas (el envio falla).
    while (!stop_.load(std::memory_order_relaxed)) {
        const int r = http::waitReadable(fd, 15000);
        if (r < 0) break;
        if (r > 0) {
            uint8_t sink[256];
            if (http::recvSome(fd, sink, sizeof(sink)) <= 0) break;
            continue;
        }
        std::lock_guard<std::mutex> lock(conn->sendLock);
        if (!http::sendAll(fd, ": ping\n\n", 8)) break;
    }
    {
        std::lock_guard<std::mutex> lock(sseLock_);
        sse_.erase(std::remove(sse_.begin(), sse_.end(), conn), sse_.end());
    }
    std::lock_guard<std::mutex> lock(conn->sendLock);
    conn->fd = -1;
}

void SignalingServer::broadcastToHosts(const std::string& event, const json& data) {
    std::vector<std::shared_ptr<SseConn>> conns;
    {
        std::lock_guard<std::mutex> lock(sseLock_);
        conns = sse_;
    }
    if (conns.empty()) return;
    const std::string msg = "event: " + event + "\ndata: " + data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n\n";
    for (const auto& c : conns) {
        std::lock_guard<std::mutex> lock(c->sendLock);
        if (c->fd >= 0 && !http::sendAll(c->fd, msg.data(), msg.size())) {
            http::shutdownFd(c->fd); // su thread lo da de baja
        }
    }
}

} // namespace ba::gateway
