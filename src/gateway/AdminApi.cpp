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
//   GET    /metrics                        las métricas de B (texto de Prometheus, F4-01)
//   GET    /api/v1/room                    estado completo (snapshot)
//   GET    /api/v1/events                  SSE: "room" (snapshot) y "chat"
//   GET    /api/v1/network                 HTTPS, tunel WireGuard, IPs y enlaces
//   GET    /api/v1/prompter?p=<id>         estado de un teleprompter (+ la clave y quien esta conectado; sin p, el principal)
//   POST   /api/v1/prompter                {prompter?, texto?, titulo?, panico?, ...comandos} (prompter "*": a todos)
//   GET    /api/v1/prompters               los teleprompters: [{id, titulo, palabras, corriendo, oradores, controles}]
//   POST   /api/v1/prompters               {quitar: id}
//   GET    /api/v1/prompter-link?target=local|remote&vista=control|orador&conClave=0|1&p=<id>  enlace + QR
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
//   GET    /api/v1/prueba-sala             la prueba de sala (PruebaSala.hpp), con la credencial
//   POST   /api/v1/prueba-sala             {duracionS, directa?} la abre/renueva · {directa} el interruptor
//   DELETE /api/v1/prueba-sala · POST /api/v1/prueba-sala/cerrar {motivo}  la termina (echa a los de prueba)
//   POST   /api/v1/claves                  {prompter?, correccion?, rotulos?, publico?, tablero?} (del evento)
//   PUT    /api/v1/config/ajustes          {valores: {clave: valor}}  los avanzados «live» (en caliente)
//   PUT    /api/v1/config/sala             {lobby?, pinIntentos?, pinBloqueoS?, maxUploadMB?}  (en caliente)
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
//   …/api/v1/registro/*                     el registro de personas por tótem (ver Registro.hpp)
//   GET|POST /api/v1/grillas                la grilla de personas: lo que pinta cada BA.Grilla.<k> (ver Grillas.hpp)
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
#include <set>
#include <thread>
#include <system_error>

#include "Canal.hpp"
#include "HttpIo.hpp"
#include "diag/Correlacion.hpp"
#include "diag/Metricas.hpp"
#include "proxy/GestorProxy.hpp"
#include "Log.hpp"
#include "qrcodegen.hpp"
#include "Room.hpp"
#include "security/Comparar.hpp"
#include "security/CredentialStore.hpp"
#include "security/SecretsStore.hpp"
#include "ShareStore.hpp"
#include "SignalingServer.hpp"
#include "Canal.hpp"
#include "MediaPump.hpp"
#include "Grillas.hpp"
#include "ipc/CanalesSalida.hpp"
#include "ipc/ProtocoloSala.hpp"

#include "diag/Registro.hpp"
#include "ajustes/Ajustes.hpp"
#include "ajustes/Registro.hpp"
#include "i18n/I18n.hpp"

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;

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

/// El QR de `url` en `out` («url», «size» y «modules»: filas de '0'/'1'): cada vista lo
/// dibuja con la matriz, sin depender de ninguna biblioteca.
void conQr(json& out, const std::string& url) {
    const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    json rows = json::array();
    for (int y = 0; y < qr.getSize(); ++y) {
        std::string row;
        for (int x = 0; x < qr.getSize(); ++x) row.push_back(qr.getModule(x, y) ? '1' : '0');
        rows.push_back(row);
    }
    out["url"] = url;
    out["size"] = qr.getSize();
    out["modules"] = rows;
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

void SignalingServer::aplicarConfig(long long fd, const std::string& seccion, const json& req) {
    if (seccion == "ajustes") {
        // Los avanzados de la sala que rigen en caliente (alcance «live» en el registro):
        // los que vienen toman ese valor; los «live» que no vienen vuelven al de siempre.
        // Los que reinician la sala (los lee una vez al arrancar) no se tocan acá.
        const json valores = req.value("valores", json::object());
        if (!valores.is_object()) {
            sendErr(fd, 400, "valores");
            return;
        }
        json rechazados = json::object();
        for (const auto& [k, v] : valores.items()) {
            const ba::ajustes::Definicion* d = ba::ajustes::buscar(k);
            if (d == nullptr || d->destino != ba::ajustes::Destino::Sala)
                rechazados[k] = traducir("gateway", "no es un ajuste de la conferencia");
            else if (std::string(d->alcance) != "live")
                rechazados[k] = traducir("gateway", "rige al reiniciar la conferencia");
            else if (const std::string e = ba::ajustes::validar(k, v); !e.empty())
                rechazados[k] = e;
        }
        // Todo o nada: con uno mal no se aplica ninguno (la app avisa cuál).
        if (!rechazados.empty()) {
            std::string lista;
            for (const auto& [k, e] : rechazados.items()) lista += (lista.empty() ? "" : ", ") + k + ": " + e.get<std::string>();
            sendJson(fd, 409, json{{"ok", false}, {"error", lista}, {"claves", rechazados}});
            return;
        }
        int cambiados = 0;
        for (const ba::ajustes::Definicion& d : ba::ajustes::registro()) {
            if (d.destino != ba::ajustes::Destino::Sala || std::string(d.alcance) != "live") continue;
            const auto it = valores.find(d.clave);
            ba::ajustes::poner(d.clave, it != valores.end() ? *it : json());
            ++cambiados;
        }
        log_.info(llenar(traducir("gateway", "control: ajustes de la conferencia en caliente (%1 en uso)"),
                         {std::to_string(valores.size())}));
        sendJson(fd, 200, json{{"ok", true}, {"enCaliente", cambiados}});
        return;
    }
    if (seccion == "sala") {
        // Lo de Configuración → Conferencia que la sala aplica andando. Rangos como en la página.
        const auto entero = [&](const char* k, int min, int max, int& out) {
            if (!req.contains(k)) return true;
            if (!req[k].is_number_integer() || req[k].get<int>() < min || req[k].get<int>() > max) return false;
            out = req[k].get<int>();
            return true;
        };
        int intentos = -1, bloqueoS = -1, maxMB = -1;
        if (!entero("pinIntentos", 1, 100, intentos) || !entero("pinBloqueoS", 5, 86400, bloqueoS) ||
            !entero("maxUploadMB", 1, 20480, maxMB) || (req.contains("lobby") && !req["lobby"].is_boolean())) {
            sendErr(fd, 400, "rango");
            return;
        }
        if (req.contains("lobby")) room_.setLobby(req["lobby"].get<bool>(), nowMsA());
        {
            std::lock_guard<std::mutex> l(ipAttemptsLock_);
            if (intentos > 0) pinIntentos_ = intentos;
            if (bloqueoS > 0) pinBloqueoS_ = bloqueoS;
        }
        if (maxMB > 0) shares_.ponerMaxBytes(static_cast<std::uint64_t>(maxMB) * 1024ull * 1024ull);
        log_.info(traducir("gateway", "control: configuración de la conferencia en caliente"));
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }
    sendJson(fd, 404, json{{"ok", false}, {"error", llenar(traducir("gateway", "sección desconocida: %1"), {seccion})}});
}

json SignalingServer::enlaceConQr(const std::string& servicio, const std::string& target, const std::string& ruta) const {
    json o = origenDe(servicio, target);
    if (!o.contains("origen")) {
        // 200 con ok=false: falta configuración, no es una falla de la API; la vista
        // muestra `message` en el lugar del QR.
        o["ok"] = false;
        return o;
    }
    json out{{"ok", true}, {"target", target}};
    if (o.contains("warning")) out["message"] = o["warning"];
    conQr(out, o["origen"].get<std::string>() + ruta);
    return out;
}

bool SignalingServer::adminAuthorized(const std::string& headers, const std::string& method) const {
    const std::string auth = http::getHeader(headers, "Authorization");
    (void)method;
    return auth.rfind("Bearer ", 0) == 0 && ba::security::igualesEnTiempoConstante(auth.substr(7), cfg_.adminToken);
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
    log_.info(traducir("gateway", "control: PIN de la conferencia cambiado"));
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

const char* SignalingServer::candadoTrabado() {
    const auto seToma = [](std::mutex& m) {
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (std::chrono::steady_clock::now() < hasta) {
            if (m.try_lock()) {
                m.unlock();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    };
    if (!seToma(connsLock_)) return "conexiones";
    if (!seToma(sessLock_)) return "sesiones";
    if (!seToma(sseLock_)) return "eventos";
    return nullptr;
}

void SignalingServer::handleAdmin(long long fd, const std::string& method, const std::string& path,
                                  const std::string& query, const std::string& headers,
                                  const std::string& body) {
    // La orden de la app que trae esto (F4-02): todo lo que se registre al atenderla lleva su id.
    const ba::diag::correlacion::Ambito orden(http::getHeader(headers, ba::diag::correlacion::kCabecera));
    // --- Sin autenticacion: la salud ------------------------------------------
    // No alcanza con contestar (eso solo prueba que se acepta una conexión y se crea un
    // hilo): los candados de los que vive la sala (las conexiones, las sesiones del hub, los
    // eventos a la app) tienen que poder tomarse. Uno que no se suelta en un segundo es una
    // sala trabada aunque conteste.
    if (path == "/healthz") {
        if (const char* trabado = candadoTrabado()) {
            http::sendResponse(fd, 503, "text/plain", std::string("trabado: ") + trabado + "\n");
            return;
        }
        http::sendResponse(fd, 200, "text/plain", "ok\n");
        return;
    }
    if (path == "/" && method == "GET") {
        http::sendResponse(fd, 200, "text/plain", traducir("gateway", "API de control de la sala (solo para la app).") + "\n");
        return;
    }

    // --- Todo lo demas pide credenciales ------------------------------------
    if (!adminAuthorized(headers, method)) {
        sendErr(fd, 401, "auth");
        return;
    }
    if (!ba::diag::correlacion::actual().empty() && method != "GET") log_.info(method + " " + path); // la orden llegó (F4-02)
    // Las métricas de B en el formato de Prometheus (F4-01).
    if (path == "/metrics" && method == "GET") {
        http::sendResponse(fd, 200, "text/plain; version=0.0.4; charset=utf-8", ba::diag::Metricas::proceso().texto());
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
            sendJson(fd, 200, json{{"ok", false}, {"error", traducir("gateway", "sin token")}});
            return;
        }
        // `salida`: de qué salida es el programa que mira (sin ella, la por omisión).
        const auto q = http::parseQuery(query);
        const std::string salida = q.count("salida") ? http::urlDecode(q.at("salida")) : std::string();
        sendJson(fd, 200, json{{"ok", true},
                               {"url", "/cast/?token=" + token + "&nombre=Presidencia" +
                                           (ba::ipc::idDeSalidaValido(salida) ? "&salida=" + salida : std::string())}});
        return;
    }
    if (sesion_ && sesion_->admin(fd, method, path, query, body)) return;
    // El registro por tótem: la lista de tótems, mensajes a cada pantalla, fotos y cortes.
    if (registro_ && registro_->admin(fd, method, path, query, body)) return;
    // La grilla de personas: lo que pinta cada BA.Grilla.<k> (lo decide la app).
    if (grillas_ && grillas_->admin(fd, method, path, body)) return;

    const json req = body.empty() ? json::object() : json::parse(body, nullptr, false);
    if (!req.is_object()) {
        sendErr(fd, 400, "json");
        return;
    }

    // La configuración en caliente (F3-06): lo que la sala aplica andando, sin
    // reiniciarse ni cortar a nadie. Lo estructural (puertos, TLS…) no está acá.
    static const std::string kConfig = "/api/v1/config/";
    if (path.rfind(kConfig, 0) == 0) {
        if (method != "PUT") {
            sendErr(fd, 405, "method");
            return;
        }
        aplicarConfig(fd, path.substr(kConfig.size()), req);
        return;
    }

    // Las salidas del aparato (docs/planes/PLAN-SALIDAS.md): la app manda la lista entera
    // cada vez que cambia ({lista:[{id, nombre, color, porOmision}]}); se puede leer.
    if (path == "/api/v1/salidas") {
        if (method == "POST") ponerSalidas(req.value("lista", json::array()));
        std::lock_guard<std::mutex> l(salidasMu_);
        sendJson(fd, 200, json{{"ok", true}, {"lista", salidas_}});
        return;
    }
    // Las claves de las superficies son del evento: la app las guarda y las da (ver
    // SignalingServer::Claves). Sin esto, el QR del público impreso moría con la sala.
    if (path == "/api/v1/claves" && method == "POST") {
        if (const std::string mal = ponerClaves(req); !mal.empty()) {
            sendJson(fd, 400, json{{"ok", false}, {"error", "clave"}, {"cual", mal}});
            return;
        }
        log_.info(traducir("gateway", "control: claves de las superficies del evento puestas"));
        sendJson(fd, 200, json{{"ok", true}});
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
                                           {"error", r == Room::kFull ? traducir("gateway", "hay invitados en lugares que se irían")
                                                                      : llenar(traducir("gateway", "el cupo va de 1 a %1"),
                                                                               {std::to_string(kMaxInvitados)})}});
                    return;
                }
            }
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
    }
    // La prueba de sala (PruebaSala.hpp): la abre, la renueva y la cierra la app.
    if (path == "/api/v1/prueba-sala") {
        if (method == "GET") {
            sendJson(fd, 200, estadoPruebaSala());
            return;
        }
        if (method == "POST") {
            std::optional<bool> directa;
            if (req.contains("directa") && req["directa"].is_boolean()) directa = req["directa"].get<bool>();
            // Solo el interruptor (sin duración): la prueba tiene que estar abierta.
            if (!req.contains("duracionS")) {
                if (!directa) {
                    sendErr(fd, 400, "duracionS");
                    return;
                }
                const json r = directaPruebaSala(*directa);
                sendJson(fd, r.value("ok", false) ? 200 : 409, r);
                return;
            }
            const int64_t dur = req.value("duracionS", 0LL) * 1000;
            const json r = abrirPruebaSala(dur, directa);
            sendJson(fd, r.value("ok", false) ? 200 : 400, r);
            return;
        }
        if (method == "DELETE") {
            sendJson(fd, 200, cerrarPruebaSala("app"));
            return;
        }
    }
    if (path == "/api/v1/prueba-sala/cerrar" && method == "POST") {
        sendJson(fd, 200, cerrarPruebaSala(req.value("motivo", std::string("app"))));
        return;
    }
    if (path == "/api/v1/proxy") {
        sendJson(fd, 200, proxy_ != nullptr ? proxy_->estado() : json{{"activo", false}, {"motivo", traducir("gateway", "apagado en la configuración")}});
        return;
    }
    if (path == "/api/v1/proxy/renovar" && method == "POST") {
        if (proxy_ == nullptr) {
            sendJson(fd, 409, json{{"ok", false}, {"error", traducir("gateway", "el proxy está apagado")}});
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
        conQr(out, url);
        out["ok"] = true;
        out["target"] = target;
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
        sendJson(fd, 200,
                 enlaceConQr("tablero", q.count("target") ? http::urlDecode(q.at("target")) : "local",
                             "/board/#k=" + claves().tablero));
        return;
    }
    // --- Teleprompter -------------------------------------------------------
    if (path == "/api/v1/prompters") {
        if (method == "GET") {
            sendJson(fd, 200, json{{"ok", true}, {"prompters", prompters_.lista()}});
            return;
        }
        if (method == "POST") {
            const std::string id = req.value("quitar", std::string());
            if (!prompters_.quitar(id)) return sendErr(fd, 400, "prompter");
            broadcastToHosts("prompter", json{{"prompter", Prompters::idValido(id)}, {"quitado", true}});
            sendJson(fd, 200, json{{"ok", true}, {"prompters", prompters_.lista()}});
            return;
        }
    }
    if (path == "/api/v1/prompter") {
        // Cuál: `p` en la consulta (GET) o `prompter` en el cuerpo (POST); sin eso, el principal.
        const auto q = http::parseQuery(query);
        const std::string id = method == "POST" ? req.value("prompter", std::string())
                                                : (q.count("p") ? http::urlDecode(q.at("p")) : std::string());
        // «*»: a todos (lo del presentador de la línea de tiempo va a cada atril).
        if (method == "POST" && id == "*") {
            prompters_.comandarTodos(req);
            for (const json& p : prompters_.lista()) avisarPrompter(p.value("id", std::string()), false, true);
            sendJson(fd, 200, json{{"ok", true}});
            return;
        }
        const auto prompter = prompters_.de(id);
        if (!prompter) return sendErr(fd, 400, "prompter");
        // A ve siempre la clave del control: la necesita para armar el enlace
        // que le manda al operador. Por la superficie de invitados no sale.
        const auto conClave = [this, &prompter, &id](json e) {
            e["clave"] = prompterKey();
            e["clientes"] = clientesPrompter(Prompters::idValido(id), *prompter);
            e["prompter"] = Prompters::idValido(id);
            return e;
        };
        if (method == "GET") {
            sendJson(fd, 200, conClave(prompter->estado()));
            return;
        }
        if (method == "POST") {
            // A comanda sin clave: ya paso por el token de la API de control.
            if (req.contains("texto") && req["texto"].is_string()) {
                prompter->cargar(req.value("titulo", std::string()), req["texto"].get<std::string>());
            }
            if (req.value("panico", false)) prompter->pararEnSeco();
            prompter->comandar(req);
            avisarPrompter(id, req.contains("texto"), true);
            sendJson(fd, 200, conClave(prompter->estado()));
            return;
        }
    }
    // --- Rotulos ------------------------------------------------------------
    // El buzon de dos direcciones con la app: A publica lo que hay y se lleva
    // lo que encargaron desde los celulares. Ver Rotulador.hpp.
    if (path == "/api/v1/rotulos") {
        if (method == "GET") {
            json e = rotulador_.estado();
            e["clave"] = claves().rotulos;
            sendJson(fd, 200, e);
            return;
        }
        if (method == "POST") {
            if (req.contains("estado")) {
                rotulador_.publicar(req["estado"]);
                empujarASuperficies("rotulos"); // los celulares no preguntan: se les avisa
            }
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
        // La clave va en el FRAGMENTO: no viaja en la peticion ni queda en los
        // registros de ningun servidor por el que pase el enlace. `conClave=0`
        // la deja afuera: el QR entra a la pagina y la clave se la dice el
        // operador aparte, que es lo que se quiere si el QR va a quedar pegado
        // en una pared.
        const bool conClave = !q.count("conClave") || http::urlDecode(q.at("conClave")) != "0";
        const std::string clave = claves().rotulos;
        json out = enlaceConQr("rotulos", q.count("target") ? http::urlDecode(q.at("target")) : "local",
                               "/rotulos/" + (conClave ? "#k=" + clave : std::string()));
        if (out.value("ok", false)) {
            out["conClave"] = conClave;
            out["clave"] = clave;
        }
        sendJson(fd, 200, out);
        return;
    }

    // --- Las preguntas del publico -------------------------------------------
    if (path == "/api/v1/preguntas") {
        if (method == "GET") {
            json out{{"ok", true},
                     {"preguntas", preguntas_.lista()},
                     {"recuento", preguntas_.recuento()},
                     {"proxima", preguntas_.proxima()},
                     {"clave", claves().publico}};
            if (http::parseQuery(query).count("foto")) {
                out["foto"] = preguntas_.foto();
                std::lock_guard<std::mutex> l(publicoM_);
                out["evento"] = eventoPreguntas_;
            }
            sendJson(fd, 200, out);
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
            } else if (accion == "restaurar") {
                // La app (que las guarda en el evento) las devuelve: a una sala que se
                // reinició, o al abrir otro evento (las de ese). Si ya son de ese evento,
                // las de acá son más nuevas: no se tocan (ver la urna, abajo).
                const std::string evento = req.value("evento", std::string());
                std::lock_guard<std::mutex> l(publicoM_);
                if (evento.empty() || evento != eventoPreguntas_) {
                    preguntas_.restaurar(req.value("foto", json::object()));
                    eventoPreguntas_ = evento;
                }
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
            if (ok) avisarPreguntas();
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
            // Lo que la app guarda y de qué evento es (ver sala::UrnaEvento).
            if (q.count("foto")) {
                out["foto"] = votacion_.foto();
                std::lock_guard<std::mutex> l(publicoM_);
                out["evento"] = eventoVotacion_;
            }
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
                avisarVotacion();
                sendJson(fd, 200, json{{"ok", ok}, {"votacion", votacion_.estado()}});
                return;
            }
            if (accion == "olvidar_historial") {
                votacion_.olvidarHistorial();
                avisarVotacion();
                sendJson(fd, 200, json{{"ok", true}});
                return;
            }
            if (accion == "en_vivo") {
                votacion_.setMostrarEnVivo(req.value("si", false));
                avisarVotacion();
                sendJson(fd, 200, json{{"ok", true}, {"votacion", votacion_.estado()}});
                return;
            }
            if (accion == "restaurar") {
                // La urna es de A (ver Votacion.hpp): se la devuelve a una sala que se
                // reinició, o al abrir otro evento (la de ese). Si la de acá ya es de ese
                // evento (se abrió una votación mientras A leía la guardada), es la más
                // nueva y no se pisa: mirar y restaurar van juntos, con publicoM_, como
                // abrir (abajo).
                {
                    const std::string evento = req.value("evento", std::string());
                    std::lock_guard<std::mutex> l(publicoM_);
                    if (evento.empty() || evento != eventoVotacion_) {
                        votacion_.restaurar(req.value("foto", json::object()));
                        eventoVotacion_ = evento;
                    }
                }
                avisarVotacion();
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
            // Abrir y decir de qué evento es, juntos (con publicoM_): un restaurar que llega
            // en el medio no la pisa (ver «restaurar»).
            std::unique_lock<std::mutex> deEvento(publicoM_);
            const bool ok = votacion_.abrir(req.value("pregunta", std::string()), opciones, modo,
                                            req.value("secreta", false), habilitados, req.value("podian", 0),
                                            nowMsAdmin());
            // Las reglas (cuántas opciones, el padrón) son de la app (sala::VotacionSala):
            // esto solo es la urna que no acepta lo que no puede contar.
            if (!ok) {
                deEvento.unlock();
                sendErr(fd, 400, "votacion");
                return;
            }
            votacion_.setMostrarEnVivo(req.value("enVivo", false));
            eventoVotacion_ = req.value("evento", eventoVotacion_);
            deEvento.unlock();
            avisarVotacion();
            sendJson(fd, 200, json{{"ok", true}, {"votacion", votacion_.estado()}});
            return;
        }
    }

    if (path == "/api/v1/publico-link" && method == "GET") {
        const auto q = http::parseQuery(query);
        // Con la clave adentro casi siempre: este QR se proyecta en una
        // pantalla o se imprime en el programa, y pedirle a doscientas personas
        // que tipeen una clave es perder a la mitad.
        const bool conClave = !q.count("conClave") || http::urlDecode(q.at("conClave")) != "0";
        const std::string clave = claves().publico;
        json out = enlaceConQr("publico", q.count("target") ? http::urlDecode(q.at("target")) : "local",
                               "/publico/" + (conClave ? "#k=" + clave : std::string()));
        if (out.value("ok", false)) {
            out["conClave"] = conClave;
            out["clave"] = clave;
        }
        sendJson(fd, 200, out);
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
    // escenas ni quiere saber: guarda lo que le dan, se lo manda a las pantallas
    // de tally por su canal (y a la que entra, ver saludarSuperficie) y lo sirve
    // en /pantalla/tally.json (para quien lo lea de afuera). Si B se reinicia, A
    // se lo vuelve a dar (republicador «tally»).
    if (path == "/api/v1/tally" && method == "POST") {
        {
            std::lock_guard<std::mutex> lock(tallyLock_);
            tally_ = req.is_object() ? req : json::object();
        }
        // Y se les manda a las pantallas de tally, entero: que no tengan que
        // preguntar. Un tally que llega tarde es peor que no tenerlo, porque
        // alguien se confia.
        const json aviso{{"type", "tally"}, {"tally", req}};
        for (const std::string& pid : superficies_.conexionesDe("tally"))
            sendToGuest(pid, aviso);
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }
    // LAS CAPAS DE LA CÁMARA (Capas.hpp): qué entradas de remotos tiene la app al aire y
    // armadas, en cualquier salida ({aire: ["guest:2", "remoto:1"], siguiente: [...]}). Con
    // eso la sala le pide a cada navegador la capa alta o la baja. Si B se reinicia, A se
    // lo vuelve a dar (republicador «capas»).
    if (path == "/api/v1/capas" && method == "POST") {
        const auto lista = [&](const char* k) {
            std::set<std::string> out;
            if (req.is_object() && req.contains(k) && req[k].is_array())
                for (const json& x : req[k])
                    if (x.is_string() && x.get<std::string>().size() <= 32) out.insert(x.get<std::string>());
            return out;
        };
        {
            std::lock_guard<std::mutex> l(capasMu_);
            capasApp_ = true;
            capasAire_ = lista("aire");
            capasSiguiente_ = lista("siguiente");
        }
        revisarCapasTodas();
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
            // Las pantallas de carrusel releen su lista cuando cambia (se les avisa por el
            // canal); antes la releían cada 10 s.
            const auto avisarCarruseles = [this]() {
                for (const std::string& pid : superficies_.conexionesDe("carrusel")) sendToGuest(pid, json{{"type", "carrusel"}});
            };
            if (req.contains("borrar")) {
                const bool ok = carruseles_.borrar(req["borrar"].get<std::string>());
                if (ok) avisarCarruseles();
                sendJson(fd, 200, json{{"ok", ok}});
                return;
            }
            if (!carruseles_.publicar(req)) {
                sendErr(fd, 400, "carrusel");
                return;
            }
            avisarCarruseles();
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
        // puso en ese canal). `salida`: el programa de otra salida (sin ella, la por
        // omisión). La pantalla los repite al entrar por /ws.
        const std::string salida = q.count("salida") ? http::urlDecode(q.at("salida")) : std::string();
        if (!salida.empty() && !ba::ipc::idDeSalidaValido(salida)) {
            sendJson(fd, 200, json{{"ok", false}, {"message", traducir("gateway", "salida inválida")}});
            return;
        }
        std::string canal;
        if (q.count("canal")) {
            canal = http::urlDecode(q.at("canal"));
            if (canal.empty() || canal.size() > 2 ||
                !std::all_of(canal.begin(), canal.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
                sendJson(fd, 200, json{{"ok", false}, {"message", traducir("gateway", "canal de escena inválido")}});
                return;
            }
        }
        if (room_.snapshot().value("locked", false)) {
            sendJson(fd, 200,
                     json{{"ok", false},
                          {"message",
                           traducir("gateway", "La conferencia esta cerrada: abrila para poder mandarle el programa a una "
                                               "pantalla.")}});
            return;
        }
        const std::string token = auth_.issue(cfg_.room, room_.snapshot().value("pin", ""));
        if (token.empty()) {
            sendJson(
                fd, 200,
                json{{"ok", false}, {"message", traducir("gateway", "No pude sacar un permiso de ingreso para la pantalla.")}});
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
                                  (canal.empty() ? std::string() : "&canal=" + canal) +
                                  (salida.empty() || !canal.empty() ? std::string() : "&salida=" + salida)},
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
        empujarASuperficies("correccion"); // el puesto no pregunta: se le avisa
        sendJson(fd, 200, json{{"ok", true}});
        return;
    }
    if (path == "/api/v1/correccion-link" && method == "GET") {
        const auto q = http::parseQuery(query);
        sendJson(fd, 200,
                 enlaceConQr("correccion", q.count("target") ? http::urlDecode(q.at("target")) : "local",
                             "/correccion/#k=" + claves().correccion));
        return;
    }
    if (path == "/api/v1/prompter-link" && method == "GET") {
        // El enlace del control, con la clave en el FRAGMENTO para que no
        // viaje en la URL pedida ni quede en los registros del servidor.
        const auto q = http::parseQuery(query);
        const bool control = !q.count("vista") || http::urlDecode(q.at("vista")) == "control";
        // La clave puede ir DENTRO del enlace (se entra sin tipear) o no: un QR
        // pegado en una pared no deberia llevarla.
        const bool conClave = !q.count("conClave") || http::urlDecode(q.at("conClave")) != "0";
        // Cuál: el principal en /prompter/, los demás en /prompter/<id>/.
        const std::string pid = Prompters::idValido(q.count("p") ? http::urlDecode(q.at("p")) : std::string());
        if (pid.empty()) return sendErr(fd, 400, "prompter");
        const std::string carpeta = pid == Prompters::kPrincipal ? std::string() : pid + "/";
        const std::string clave = claves().prompter;
        // La tablet del orador no lleva clave nunca: solo lee.
        json out = enlaceConQr("prompter", q.count("target") ? http::urlDecode(q.at("target")) : "local",
                               "/prompter/" + carpeta + (control ? (conClave ? "#control&k=" + clave : "#control") : ""));
        if (out.value("ok", false)) {
            out["vista"] = control ? "control" : "orador";
            out["conClave"] = control && conClave;
            out["clave"] = clave;
        }
        sendJson(fd, 200, out);
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
                          {"perdidas", reg.perdidas()},
                          {"registradas", reg.cuenta()}});
            return;
        }
        if (method == "POST") {
            ba::diag::Nivel n{};
            if (!req.contains("nivel") || !req["nivel"].is_string() ||
                !ba::diag::nivelDeNombre(req["nivel"].get<std::string>(), n)) {
                sendJson(fd, 400, json{{"ok", false}, {"error", traducir("gateway", "nivel desconocido")}});
                return;
            }
            reg.setNivel(n);
            // Queda anotado en el propio registro: si manana aparece un log
            // lleno de trazas, tiene que decir quien lo puso asi.
            reg.linea(ba::diag::Nivel::Info, "registro", 0,
                      llenar(traducir("registro", "nivel cambiado a %1 desde la API de control"), {ba::diag::nombreDeNivel(n)}));
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
        avisarTema();
        sendJson(fd, 200, json{{"ok", true}, {"tema", temaWeb()}});
        return;
    }
    // El tema del evento abierto (lo manda la app; ver setTemaEvento).
    if (path == "/api/v1/tema-evento" && method == "POST") {
        json r = setTemaEvento(req);
        r["ok"] = true;
        avisarTema();
        sendJson(fd, 200, r);
        return;
    }
    if (path == "/api/v1/network" && method == "GET") {
        sendJson(fd, 200, networkStatus_ ? networkStatus_() : json::object());
        return;
    }
    if (path == "/api/v1/events" && method == "GET") {
        adminEvents(fd, headers);
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
        } else if (action == "salida") {
            // De qué salida le baja el retorno ("" = la por omisión).
            sendRoomResult(fd, room_.setSalida(id, req.value("salida", std::string())));
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
                sendJson(fd, 409, json{{"ok", false}, {"error", traducir("gateway", "ese participante no está mandando medios")}});
            } else {
                const json r = json::parse(pump_->pruebaTecnica(sesion, req.value("ms", 4000)), nullptr, false);
                sendJson(fd, r.is_object() && !r.contains("error") ? 200 : 409, r);
            }
        } else if (action == "request") {
            const std::string what = req.value("what", "");
            const int r = room_.request(id, what);
            if (r == Room::kOk) aplicarTopeRetorno(id, what); // calidad-*: también su video de retorno
            sendRoomResult(fd, r);
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

namespace {

/// Cuántos eventos de un solo disparo se guardan para reenviar (ver SseConn). Un corte de
/// la app de unos segundos junta pocos. Un REINICIO de la app no se cubre con esto (la app
/// nueva no trae Last-Event-ID): lo que pide una acción es estado con pendientes que la app
/// va a buscar al conectar (solicitudes de emparejamiento, acreditaciones, rótulos,
/// aparatos de la sesión).
constexpr std::size_t kGuardadosSse = 1024;
/// Lo más que se le junta a una conexión sin poder escribirle: pasado esto se la corta.
constexpr std::size_t kTopeColaSse = 8u * 1024u * 1024u;
/// El latido del flujo (la app corta si no oye nada en room.salaMuertaS).
constexpr auto kLatidoSse = std::chrono::seconds(15);

/// Last-Event-ID «<epoch>:<n>» -> (epoch, n). n = 0 si no se entiende.
std::pair<std::string, std::uint64_t> idDeEvento(const std::string& v) {
    const auto dos = v.find(':');
    if (dos == std::string::npos) return {};
    try {
        return {v.substr(0, dos), std::stoull(v.substr(dos + 1))};
    } catch (...) {
        return {};
    }
}

} // namespace

void SignalingServer::avisarTema() {
    // Las páginas con canal (todas las de /ws, de cualquier rol: docs/arquitectura/06-canal-web.md) no
    // preguntan por el tema cada 15 s: se les avisa que lo relean.
    canal::aTodos(json{{"type", "tema"}});
}

void SignalingServer::avisarVotacion() {
    ++publicoVersion_; // lo que ven los teléfonos del público cambió (ver /api/publico/estado)
    empujarASuperficies("votacion"); // la pantalla de la votación
    std::string evento;
    {
        std::lock_guard<std::mutex> l(publicoM_);
        evento = eventoVotacion_;
    }
    broadcastToHosts("votacion", json{{"votacion", votacion_.estado()}, {"foto", votacion_.foto()}, {"evento", evento}});
}

void SignalingServer::avisarPreguntas() {
    ++publicoVersion_;
    std::string evento;
    {
        std::lock_guard<std::mutex> l(publicoM_);
        evento = eventoPreguntas_;
    }
    broadcastToHosts("preguntas", json{{"preguntas", preguntas_.lista()},
                                       {"recuento", preguntas_.recuento()},
                                       {"proxima", preguntas_.proxima()},
                                       {"foto", preguntas_.foto()},
                                       {"evento", evento}});
}

bool SignalingServer::eventoDeEstado(const std::string& event) {
    // Lo que dice CÓMO ESTÁ algo, entero: no va a la cola de un solo disparo (ver estados_).
    static const std::set<std::string> kEstado{"room", "programa", "direcciones", "hablan",    "prompter",
                                               "red",  "web",      "votacion",    "preguntas", "foto",
                                               "prueba-sala"};
    return kEstado.count(event) > 0;
}

std::string SignalingServer::claveDeEstado(const std::string& event, const json& data) {
    if (event == "room") return {}; // es la foto misma
    if (event == "prompter" && data.is_object()) return "prompter/" + data.value("prompter", std::string());
    if (event == "foto" && data.is_object()) return "foto/" + data.value("id", std::string());
    return event;
}

void SignalingServer::encolarSse(SseConn& c, std::uint64_t seq, const std::string& texto) {
    {
        std::lock_guard<std::mutex> l(c.m);
        if (c.cortar || seq <= c.ultimo) return;
        c.cola.push_back(texto);
        c.bytes += texto.size();
        c.ultimo = seq;
        if (c.bytes > kTopeColaSse) c.cortar = true;
    }
    c.cv.notify_one();
}

void SignalingServer::adminEvents(long long fd, const std::string& headers) {
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                             "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n";
    if (!http::sendAll(fd, head.data(), head.size())) return;
    // Los recv solo ocurren despues de waitReadable; el timeout acota los
    // ENVIOS: un operador colgado no debe trabar a la sala que le escribe.
    http::setSocketTimeout(fd, 5000);

    auto conn = std::make_shared<SseConn>();
    conn->fd = fd;
    const auto [epochPedido, desde] = idDeEvento(http::getHeader(headers, "Last-Event-ID"));
    {
        // Primero la foto; después, si la app viene de esta misma sala y la cola corta
        // tiene todo lo que se perdió, los eventos de un solo disparo que no le llegaron;
        // después, lo nuevo. Todo con sseLock_: ningún evento se cuela en el medio.
        std::lock_guard<std::mutex> lock(sseLock_);
        const bool reanudado = !epochPedido.empty() && epochPedido == epoch_ && desde <= sseSeq_ && descartadoHasta_ <= desde;
        json foto = room_.snapshot();
        foto["epoch"] = epoch_;
        foto["protocolo"] = ba::ipc::kProtocoloSala;
        foto["reanudado"] = reanudado;
        conn->cola.push_back("retry: 2000\nid: " + epoch_ + ":" + std::to_string(sseSeq_) + "\nevent: room\ndata: " +
                             foto.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n\n");
        // El último de cada estado, a toda conexión (ver estados_): sin id, no mueven el
        // Last-Event-ID que dejó la foto.
        for (const auto& [clave, texto] : estados_) conn->cola.push_back(texto);
        if (reanudado)
            for (const EventoGuardado& g : guardados_)
                if (g.seq > desde) conn->cola.push_back(g.texto);
        conn->ultimo = sseSeq_;
        sse_.push_back(conn);
    }
    empujarASuperficies("rotulos"); // la app volvió: los celulares de rótulos lo ven

    // Este hilo es el que le escribe a la app (ver SseConn): saca de la cola y escribe;
    // sin nada, un latido cada 15 s que además detecta conexiones muertas. El cliente no
    // manda nada: lo que llega es su cierre.
    auto ultimoEnvio = std::chrono::steady_clock::now();
    while (!stop_.load(std::memory_order_relaxed)) {
        std::deque<std::string> lote;
        bool cortar = false;
        {
            std::unique_lock<std::mutex> l(conn->m);
            conn->cv.wait_for(l, std::chrono::seconds(1), [&]() { return !conn->cola.empty() || conn->cortar; });
            lote.swap(conn->cola);
            conn->bytes = 0;
            cortar = conn->cortar;
        }
        if (cortar) {
            log_.warn(traducir("gateway", "control: la app no lee sus eventos; se le corta el flujo (vuelve a entrar con la foto)"),
                      -1);
            break;
        }
        bool ok = true;
        for (const std::string& m : lote)
            if (!(ok = http::sendAll(fd, m.data(), m.size()))) break;
        if (!ok) break;
        const auto ahora = std::chrono::steady_clock::now();
        if (!lote.empty()) {
            ultimoEnvio = ahora;
        } else if (ahora - ultimoEnvio >= kLatidoSse) {
            if (!http::sendAll(fd, ": ping\n\n", 8)) break;
            ultimoEnvio = ahora;
        }
        const int r = http::waitReadable(fd, 0);
        if (r < 0) break;
        if (r > 0) {
            uint8_t sink[256];
            if (http::recvSome(fd, sink, sizeof(sink)) <= 0) break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(sseLock_);
        sse_.erase(std::remove(sse_.begin(), sse_.end(), conn), sse_.end());
    }
    empujarASuperficies("rotulos"); // y se fue: «la app no responde»
}

void SignalingServer::broadcastToHosts(const std::string& event, const json& data) {
    const std::string cuerpo = data.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const bool estado = eventoDeEstado(event);
    std::lock_guard<std::mutex> lock(sseLock_);
    const std::uint64_t seq = ++sseSeq_;
    std::string msg = "id: " + epoch_ + ":" + std::to_string(seq) + "\nevent: " + event + "\ndata: " + cuerpo + "\n\n";
    for (const auto& c : sse_) encolarSse(*c, seq, msg);
    if (estado) {
        if (const std::string clave = claveDeEstado(event, data); !clave.empty()) {
            if (data.is_object() && data.value("quitado", false)) estados_.erase(clave); // ya no existe: nada que recordar
            else estados_[clave] = "event: " + event + "\ndata: " + cuerpo + "\n\n";
        }
        return;
    }
    guardados_.push_back({seq, std::move(msg)});
    while (guardados_.size() > kGuardadosSse) {
        descartadoHasta_ = guardados_.front().seq;
        guardados_.pop_front();
    }
}

} // namespace ba::gateway
