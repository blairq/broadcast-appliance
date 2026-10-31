// ============================================================================
// src/app/mcp/McpRoomTools.cpp — Sala de videoconferencia y modo oradores
//
// La sala vive en el Proceso B. Las acciones son las MISMAS operaciones del
// modelo que usa la vista Qt (src/app/sala/: ControlSala, ParticipantesSala,
// OradoresSala, EnlacesSala), y el error que se devuelve es el que contestó la
// sala. El estado se lee del snapshot vigente (llega por eventos, sin pedirlo).
// ============================================================================

#include <QBuffer>
#include <QDateTime>
#include <QJsonDocument>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <memory>
#include <set>

#include "i18n/I18n.hpp"
#include "arranque/AppRuntime.hpp"
#include "sala/CupoSala.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "sala/OradoresSala.hpp"
#include "sala/ParticipantesSala.hpp"
#include "sala/ReglasSala.hpp"
#include <QMetaObject>
#include <QCoreApplication>
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "net/AccesoRemoto.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/AtencionViewModel.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

// --- Utilidades compartidas -------------------------------------------------------

json roomSnapshot(StudioViewModel& vm) {
    auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
    const QByteArray raw = QJsonDocument(conf->client().snapshot()).toJson(QJsonDocument::Compact);
    json s = json::parse(raw.toStdString(), nullptr, false);
    return s.is_object() ? s : json::object();
}

bool resolveParticipant(const json& snapshot, const std::string& query, json& out, std::string& err) {
    const auto low = [](std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const json people = snapshot.value("participants", json::array());
    const std::string q = low(query);
    std::vector<json> prefix;
    for (const auto& p : people) {
        if (p.value("id", "") == query) {
            out = p;
            return true;
        }
    }
    for (const auto& p : people) {
        if (low(p.value("name", "")) == q) {
            out = p;
            return true;
        }
        if (!q.empty() && low(p.value("name", "")).rfind(q, 0) == 0) prefix.push_back(p);
    }
    if (prefix.size() == 1) {
        out = prefix[0];
        return true;
    }
    std::string names;
    for (const auto& p : people)
        names += (names.empty() ? "" : ", ") + p.value("name", "") + " (" + p.value("id", "") + ")";
    err = prefix.size() > 1 ? ba::i18n::llenar(ba::i18n::traducir("mcp", "'%1' coincide con varias personas: %2. Usa el id."), {query, names})
                            : ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay nadie '%1' en la conferencia."), {query}) + " " +
                                  (names.empty() ? ba::i18n::traducir("mcp", "La conferencia esta vacia.")
                                                 : ba::i18n::llenar(ba::i18n::traducir("mcp", "Estan: %1."), {names}));
    return false;
}

std::string personFeedKey(const json& p) {
    if (p.value("state", "") == "admitted" && p.value("slot", 0) > 0)
        return "guest:" + std::to_string(p.value("slot", 0));
    if (p.value("lobbySlot", 0) > 0) return "lobby:" + std::to_string(p.value("lobbySlot", 0));
    if (p.value("backstageSlot", 0) > 0) return "backstage:" + std::to_string(p.value("backstageSlot", 0));
    return {};
}

namespace {


/// QR (matriz de '0'/'1') -> PNG en base64.
std::string qrPng(const json& modules) {
    const int n = static_cast<int>(modules.size());
    if (n == 0) return {};
    const int cell = 8;
    const int quiet = 4;
    QImage img((n + 2 * quiet) * cell, (n + 2 * quiet) * cell, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int y = 0; y < n; ++y) {
        const std::string row = modules[y].get<std::string>();
        for (int x = 0; x < static_cast<int>(row.size()); ++x)
            if (row[x] == '1') p.fillRect((x + quiet) * cell, (y + quiet) * cell, cell, cell, Qt::black);
    }
    p.end();
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes.toBase64().toStdString();
}

} // namespace

void McpService::registerRoomTools() {
    core_.addCategory(
        "sala", ba::i18n::traducir("mcp", "Conferencia (videollamada con invitados)"),
        ba::i18n::traducir("mcp", "Invitados por WebRTC: lobby, admitir, hablar, pedir cosas, chat, archivos, enlaces y QR, red."));
    core_.addCategory("oradores", ba::i18n::traducir("mcp", "Modo oradores"),
                      ba::i18n::traducir("mcp", "Fila de oradores con reloj: al aire por turno, orden, tiempos, carrusel."));

    StudioViewModel& vm = vm_;
    AppRuntime& rt = rt_;
    auto* conf = static_cast<ConferenceViewModel*>(vm.conference());

    // Las operaciones de la sala (modelo): las mismas que usa la GUI.
    sala::ControlSala& control = conf->control();
    sala::ParticipantesSala& gente = conf->participantes();
    sala::OradoresSala& oradores = conf->oradores();
    sala::EnlacesSala& enlaces = conf->enlaces();
    // El resultado de una operación como texto: el error, el que contestó la sala.
    const auto contestar = [](const std::string& okText, Done done) -> sala::Listo {
        return [okText, done](const std::string& err, const json&) {
            done(err.empty() ? ToolResult::ok(okText)
                             : ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "La conferencia respondio: %1"), {sala::explicarError(err)})));
        };
    };
    const auto withPerson = [&vm](const json& a, const char* field, json& p, Done& done) {
        std::string err;
        if (!resolveParticipant(roomSnapshot(vm), a.value(field, std::string()), p, err)) {
            done(ToolResult::error(err));
            return false;
        }
        return true;
    };
    const json personProp = {{"type", "string"},
                             {"description", QT_TRANSLATE_NOOP("mcp", "Nombre (o prefijo unico) o id de la persona.")}};

    {
        Tool t;
        t.name = "room_status";
        t.title = ba::i18n::traducir("mcp", "Estado de la conferencia");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Todo lo de la conferencia: nombre, PIN, lobby/cerrada, enlace, cada persona (estado, lugar, "
            "microfono, camara, pantalla, mano levantada, conexion, info y contacto, turno de orador), "
            "archivos compartidos, chat reciente y config de oradores.");
        t.run = [&vm, conf](const json&, Done done) {
            if (!conf->online()) return done(ToolResult::error(ba::i18n::traducir("mcp", "La conferencia (gateway) no esta conectada.")));
            json s = roomSnapshot(vm);
            if (s.contains("chat") && s["chat"].size() > 20) {
                json last = json::array();
                for (size_t i = s["chat"].size() - 20; i < s["chat"].size(); ++i)
                    last.push_back(s["chat"][i]);
                s["chat"] = last;
            }
            done(ToolResult().data(s));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_set";
        t.title = ba::i18n::traducir("mcp", "Lobby / conferencia cerrada");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Lobby de admision (true = cada invitado espera que lo hagas pasar), conferencia cerrada (nadie "
            "nuevo puede entrar) y `cupo`: cuántos invitados en el programa (1 a 16), SIN reiniciar la conferencia: los "
            "lugares nuevos se crean andando (no saca a nadie: si un lugar que se iría está ocupado, no lo baja).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"lobby", {{"type", "boolean"}}},
              {"locked", {{"type", "boolean"}}},
              {"cupo", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}}};
        t.run = [&control, contestar, &vm = vm_](const json& a, Done done) {
            // El cupo es una operación del modelo (sala::CupoSala), la misma que usa la GUI.
            if (a.contains("cupo") && !a.contains("lobby") && !a.contains("locked")) {
                const int n = a.value("cupo", 2);
                const std::string e = vm.cupoSala().cambiar(n, [done, n](const std::string& err) {
                    if (!err.empty()) done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "La conferencia respondio: %1"), {err})));
                    else done(ToolResult::ok(ba::i18n::traducir("mcp", "Cupo: %n invitado, sin reiniciar la conferencia.", nullptr, n)));
                });
                if (!e.empty()) done(ToolResult::error(e));
                return;
            }
            if (a.contains("cupo")) {
                const std::string e = vm.cupoSala().cambiar(a.value("cupo", 2));
                if (!e.empty()) return done(ToolResult::error(e));
            }
            const auto si = [&a](const char* k) {
                return a.contains(k) && a[k].is_boolean() ? std::optional<bool>(a[k].get<bool>()) : std::nullopt;
            };
            const std::string e = control.entrada(si("lobby"), si("locked"), contestar(ba::i18n::traducir("mcp", "Conferencia actualizada."), done));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_pin";
        t.title = ba::i18n::traducir("mcp", "Cambiar el PIN");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "PIN nuevo para entrar a la conferencia (sin `pin`: se genera uno). El anterior deja de servir.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"pin", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "4 a 32 caracteres.")}}}}}};
        t.run = [&control](const json& a, Done done) {
            control.cambiarPin(a.value("pin", std::string()), [done](const std::string& err, const json& r) {
                done(err.empty() ? ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "PIN nuevo: %1"), {r.value("pin", std::string())}))
                                 : ToolResult::error(sala::explicarError(err)));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_pin_bloqueos";
        t.title = ba::i18n::traducir("mcp", "Bloqueos por PIN mal");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Quién quedó bloqueado por poner mal el PIN y cómo destrabarlo. `accion`: «ver» (de fábrica) o "
            "«desbloquear» (con `ip`, solo esa; sin `ip`, todas y también el bloqueo global).");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Hay dos bloqueos. POR IP: tras `intentosPorIp` PIN mal desde la misma IP (de fábrica 5), esa IP no "
            "puede entrar por `bloqueoS` segundos (Configuración → Conferencia). GLOBAL: de último recurso, si hay "
            "muchísimos fallos entre todos (un ataque repartido) se cierra la entrada para todos un rato. "
            "Por internet la IP es la real (la pasa ba-web desde el túnel). Desbloquear no cambia el PIN: si "
            "alguien lo está adivinando, mejor cambiarlo (room_pin). Usalo cuando un invitado legítimo se "
            "equivocó varias veces y no puede entrar, o cuando nadie puede entrar (global).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"accion", {{"type", "string"}, {"enum", {"ver", "desbloquear"}}}},
                           {"ip", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Solo esta IP (al desbloquear).")}}}}}};
        t.examples = {R"({})", R"({"accion": "desbloquear"})", R"({"accion": "desbloquear", "ip": "190.12.34.56"})"};
        t.run = [&control](const json& a, Done done) {
            const auto mostrar = [done](const json& r, const std::string& antes) {
                const json b = r.value("bloqueos", json::object());
                std::string txt = antes;
                const json g = b.value("global", json::object());
                if (g.value("bloqueado", false))
                    txt += ba::i18n::llenar(ba::i18n::traducir("mcp", "BLOQUEO GLOBAL activo: nadie puede entrar por %1 s más (%2 fallos entre todos)."),
                                            {std::to_string(g.value("quedanS", 0)), std::to_string(g.value("fallos", 0))}) +
                           "\n";
                int n = 0;
                for (const auto& ip : b.value("ips", json::array()))
                    if (ip.value("bloqueada", false)) {
                        txt += "- " + ba::i18n::llenar(ba::i18n::traducir("mcp", "%1: bloqueada %2 s más"),
                                                        {ip.value("ip", std::string()), std::to_string(ip.value("quedanS", 0))}) +
                               "\n";
                        ++n;
                    }
                if (!n && !g.value("bloqueado", false)) txt += ba::i18n::traducir("mcp", "No hay nadie bloqueado.");
                done(ToolResult::ok(txt).data(b));
            };
            if (a.value("accion", std::string("ver")) == "desbloquear") {
                const std::string ip = a.contains("ip") && a["ip"].is_string() ? a["ip"].get<std::string>() : "";
                control.desbloquearPin(ip, [done, mostrar, ip](const std::string& err, const json& j) {
                    if (!err.empty()) return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "La conferencia respondio: %1"), {sala::explicarError(err)})));
                    mostrar(j, (!ip.empty() ? ba::i18n::llenar(ba::i18n::traducir("mcp", "Desbloqueada %1."), {ip})
                                            : ba::i18n::llenar(ba::i18n::traducir("mcp", "Desbloqueadas %1 IP y el bloqueo global."),
                                                               {std::to_string(j.value("desbloqueadas", 0))})) +
                                   "\n");
                });
                return;
            }
            control.verBloqueosPin([done, mostrar](const std::string& err, const json& r) {
                if (!err.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "La conferencia (gateway) no respondió.")));
                mostrar(r, "");
            });
        };
        core_.addTool(std::move(t));
    }
    // Volver a la fila. ESTABA EN UNA TABLA junto con admitir, rechazar y
    // sacar, y esas tres YA se registran una por una mas abajo, con su titulo,
    // sus detalles y sus ejemplos: se estaban publicando DOS VECES cada una.
    // El modelo las veia repetidas —dos fichas distintas con el mismo nombre—
    // y el listado pesaba de mas. Lo encontro el inventario del MCP
    // (tools/docs/mcp-inventario.py), que cuenta nombres repetidos.
    {
        Tool t;
        t.name = "participant_requeue";
        t.title = ba::i18n::traducir("mcp", "Volver a la fila");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Un orador que ya habló vuelve a la fila de oradores, al final, para hablar otra vez (`participant`: "
                                                 "nombre o id).");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.examples = {R"({"participant": "Marta"})"};
        t.run = [withPerson, &oradores, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            oradores.aLaFila(p["id"], contestar(ba::i18n::traducir("mcp", "Vuelve a la fila."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_admit";
        t.title = ba::i18n::traducir("mcp", "Hacer pasar a alguien del lobby");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Hace PASAR a alguien que espera en el lobby: entra a la conferencia. Admite a UNA persona "
                        "que espera: deja el lobby y entra a la conferencia, con su camara y su "
                        "microfono.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Es el final del camino de la atencion en el lobby: `rostro_lobby` / `chat_send` para "
                    "saber quien es, `participant_profile` para dejarle la ficha, `operator_ask` para que "
                    "el operador decida, y esto cuando dijo que si.\n\n"
                    "Si no hay lugar libre en la conferencia (`room_status` -> `slotsFree`) el gateway la deja "
                    "esperando: admitir no crea lugares.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.examples = {R"({"participant": "Jorge"})"};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            gente.admitir(p["id"], contestar(ba::i18n::traducir("mcp", "Adentro."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_reject";
        t.title = ba::i18n::traducir("mcp", "No dejar pasar a alguien del lobby");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Le niega la entrada a quien espera: se le cierra la sesion y sale del lobby.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Para el que no tiene que estar. Si solo querés que espere un rato mas, no uses "
                    "esto: dejala en el lobby y avisale por `chat_send`.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            gente.rechazar(p["id"], contestar(ba::i18n::traducir("mcp", "Afuera."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_kick";
        t.title = ba::i18n::traducir("mcp", "Sacar a alguien de la conferencia");
        t.category = "sala";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "saca a un invitado de la conferencia");
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Saca a alguien que YA esta adentro. Si esta al aire, sale al aire que se fue.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Mira `room_status` antes: si esa persona esta en una escena al aire, sacarla se "
                    "ve en la transmision. Pasar el programa a otra escena primero es gratis; "
                    "deshacer esto no.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            gente.sacar(p["id"], contestar(ba::i18n::traducir("mcp", "Listo."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_admit_all";
        t.title = ba::i18n::traducir("mcp", "Admitir a todos");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Hace pasar a todos los que esperan en el lobby, en orden de llegada.");
        t.run = [&gente](const json&, Done done) {
            // La misma operación que el botón «Admitir a todos»: de a uno, en
            // orden de llegada, y corta al primer «sala llena».
            gente.admitirATodos([done](int pedidos, int admitidos, const std::string& err) {
                if (pedidos == 0) return done(ToolResult::ok(ba::i18n::traducir("mcp", "No hay nadie esperando en el lobby.")));
                const std::string a = std::to_string(admitidos), de = std::to_string(pedidos);
                if (err.empty()) return done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Pasaron %1 de %2."), {a, de})));
                const std::string txt = ba::i18n::llenar(ba::i18n::traducir("mcp", "Pasaron %1 de %2. La conferencia respondio: %3"),
                                                         {a, de, sala::explicarError(err)});
                done(admitidos > 0 ? ToolResult::ok(txt) : ToolResult::error(txt));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_talk";
        t.title = ba::i18n::traducir("mcp", "Hablarle a un invitado (lobby en privado, adentro IFB)");
        t.category = "sala";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "abre o cierra el micrófono de un invitado");
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Con alguien del lobby: on=true le llega tu voz y vos lo escuchas por monitoreo (nadie mas). "
            "Con alguien de ADENTRO (programa o fila de oradores) es IFB: le llega tu voz mezclada con su "
            "retorno (el programa baja), sin salir al aire. on=false corta.");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"participant", personProp}, {"on", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"participant"}}};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            gente.hablar(p["id"], a.value("on", true), contestar(ba::i18n::traducir("mcp", "Listo."), done));
        };
        core_.addTool(std::move(t));
    }
    // --- El proxy por nombre (T61) ------------------------------------------------
    {
        Tool t;
        t.name = "proxy_estado";
        t.title = ba::i18n::traducir("mcp", "Proxy por nombre: servicios y certificados");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "El proxy propio (443/80): cada servicio con su nombre (sesion.<dominio>, botonera.<dominio>…), "
                        "a qué va, si es solo de la red local, y su certificado (acme = Let's Encrypt, casa = "
                        "autofirmado) con la fecha en que vence (segundos desde 1970). Más el estado de ACME: último "
                        "error y si está pidiendo ahora.");
        t.run = [&control](const json&, Done done) {
            control.verProxy([done](const std::string& err, const json& r) {
                if (!err.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "la conferencia no contestó")));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    // --- Acceso remoto por pinggy (docs/planes/PINGGY.md) ---------------------------
    if (AccesoRemoto* ar = vm.accesoRemoto(); ar != nullptr) {
        const QPointer<AccesoRemoto> acc = ar;
        Tool t;
        t.name = "acceso_remoto_estado";
        t.title = ba::i18n::traducir("mcp", "Acceso remoto por pinggy: estado y direcciones");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.openWorld = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "El túnel de pinggy.io por el que entran los de afuera: `pinggy.estado` (apagado, conectando, listo, "
            "reconectando, error), la dirección pública (`pinggy.urlPublica`) y `enlaces` (invitados, sesión remota, "
            "público, botonera), el último `pinggy.error` y el TURN (`pinggy.turn`). `configuracion`: activo, región, "
            "tipo de dirección (azar | pinggy = subdominio fijo | propio = dominio propio), host y destino del CNAME. "
            "`token_tunel` dice solo si está guardado o falta: el token lo carga el operador en Configuración → Acceso "
            "remoto, vos nunca lo pedís ni lo ves. Con dominio propio trae `instrucciones_dns` para pasarle al que "
            "administra el DNS. Para cambiar la configuración: settings_set con las claves pinggy.* (reinicia la conferencia). "
            "`disponible` false = la conferencia no está andando o es una versión que no informa el túnel. "
            "SIN TOKEN (gratis): dirección al azar que cambia cada vez, 60 minutos por túnel y pinggy le muestra a cada "
            "navegador un aviso que hay que aceptar. CON TOKEN (Pro): sin límite ni aviso, dirección fija (subdominio de "
            "pinggy o dominio propio con CNAME), puertos fijos; un token por equipo.");
        t.run = [acc](const json&, Done done) {
            if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
            acc->consultar([done](json r) { done(ToolResult().data(r)); });
        };
        core_.addTool(std::move(t));
    }
    if (AccesoRemoto* ar = vm.accesoRemoto(); ar != nullptr) {
        const QPointer<AccesoRemoto> acc = ar;
        // Espera a que `campo` (dns/panel) deje de estar «verificando/consultando» (o 30 s).
        const auto esperar = [acc](void (AccesoRemoto::*senal)(), std::function<QVariantMap()> leer, Done done) {
            auto conn = std::make_shared<QMetaObject::Connection>();
            auto listo = std::make_shared<bool>(false);
            auto terminar = [conn, listo, leer, done]() {
                if (*listo) return;
                const QVariantMap m = leer();
                const QString e = m.value("estado").toString();
                if (e == "verificando" || e == "consultando" || e == "trabajando") return;
                *listo = true;
                QObject::disconnect(*conn);
                done(ToolResult().data(json{{"estado", m.value("estado").toString().toStdString()},
                                            {"texto", m.value("texto").toString().toStdString()}}));
            };
            *conn = QObject::connect(acc.data(), senal, acc.data(), terminar);
            QTimer::singleShot(30000, acc.data(), [conn, listo, done]() {
                if (*listo) return;
                *listo = true;
                QObject::disconnect(*conn);
                done(ToolResult::error(ba::i18n::traducir("mcp", "pinggy/DNS no contestó en 30 s")));
            });
            terminar(); // por si ya terminó
        };
        {
            Tool t;
            t.name = "acceso_remoto_clave";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: guardar o borrar una clave (pinggy o Cloudflare)");
            t.category = "sala";
            t.clase = Clase::Escritura;
            t.idempotent = true;
            t.description =
                QT_TRANSLATE_NOOP("mcp", "Guarda CIFRADA en la bóveda (Credenciales/) la clave que te pasó el operador, o la borra. "
                "`tipo`: \"token\" (el token del PLAN de pinggy: abre el túnel; el del comando …@pro.pinggy.io), \"api_key\" (la "
                "API key de pinggy: control de la cuenta por API, con acceso_remoto_panel; NO abre túneles) o "
                "\"cloudflare\" (el token del túnel de Cloudflare, Configuración → Cloudflare). Nunca se devuelve ni se escribe "
                "en el registro: la respuesta dice solo «guardada». Con pinggy lo normal es guardar SOLO la api_key y "
                "seguir con acceso_remoto_configurar_con_api_key (trae el token sola); el token a mano es solo si no "
                "hay API key. Si no sabés cuál es, guardala como api_key y probá acceso_remoto_panel (si la acepta, es "
                "la API key). El token rige al reiniciar la conferencia. Guía: help {\"topic\": \"acceso_remoto\"}.");
            t.inputSchema = {
                {"type", "object"},
                {"properties",
                 {{"tipo", {{"type", "string"}, {"enum", {"token", "api_key", "cloudflare"}}}},
                  {"clave", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "la clave (no hace falta para borrar)")}}},
                  {"borrar", {{"type", "boolean"}, {"default", false}}}}},
                {"required", {"tipo"}}};
            t.run = [acc, &vm](const json& a, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                const std::string tipo = a.value("tipo", std::string());
                const bool borrar = a.value("borrar", false);
                const QString clave = QString::fromStdString(a.value("clave", std::string())).trimmed();
                QString e;
                // Si hay que reiniciar la sala para que el token rija lo decide
                // el modelo, con la misma regla que Configuración → Acceso remoto.
                bool reiniciarAhora = false;
                if (tipo == "token" && !borrar) {
                    const QVariantMap r =
                        acc->guardarTokenTunel(clave, static_cast<SettingsModel*>(vm.settings())->dirty());
                    e = r.value("error").toString();
                    reiniciarAhora = r.value("reiniciarAhora").toBool();
                } else if (tipo == "token") e = acc->borrarToken();
                else if (tipo == "api_key") e = borrar ? acc->borrarApiKey() : acc->guardarApiKey(clave);
                else if (tipo == "cloudflare") e = borrar ? acc->borrarTokenCloudflare() : acc->guardarTokenCloudflare(clave);
                else return done(ToolResult::error(ba::i18n::traducir("mcp", "tipo: \"token\", \"api_key\" o \"cloudflare\"")));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                // Una frase entera por caso (no «Token de…» + «borrado/a»).
                const std::string hecho =
                    tipo == "token"        ? (borrar ? ba::i18n::traducir("mcp", "Token de túnel de pinggy borrado/a.")
                                                     : ba::i18n::traducir("mcp", "Token de túnel de pinggy guardado/a, cifrado/a en Credenciales/."))
                    : tipo == "cloudflare" ? (borrar ? ba::i18n::traducir("mcp", "Token de Cloudflare borrado/a.")
                                                     : ba::i18n::traducir("mcp", "Token de Cloudflare guardado/a, cifrado/a en Credenciales/."))
                                           : (borrar ? ba::i18n::traducir("mcp", "API key borrado/a.")
                                                     : ba::i18n::traducir("mcp", "API key guardado/a, cifrado/a en Credenciales/."));
                done(ToolResult::ok(hecho +
                                    (reiniciarAhora ? " " + ba::i18n::traducir("mcp", "La conferencia lo lee al arrancar y está andando con el túnel "
                                                      "prendido: reiniciala (gateway_restart) para que rija.")
                                     : tipo != "api_key" && !borrar ? " " + ba::i18n::traducir("mcp", "Rige al reiniciar la conferencia (gateway_restart o "
                                                                      "aplicar Configuración).")
                                                                    : std::string()) +
                                    (acc->clavesIguales()
                                         ? " " + ba::i18n::traducir("mcp", "OJO: el token de túnel y la API key son la MISMA clave. Son dos cosas distintas del "
                                           "panel de pinggy: el token (el del comando del túnel, …@pro.pinggy.io) abre el túnel; la "
                                           "API key (API keys) solo sirve para consultar la cuenta. Con la API key como token el "
                                           "túnel no abre.")
                                         : std::string())));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_configurar";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: configurar pinggy");
            t.category = "sala";
            t.clase = Clase::Escritura;
            t.description =
                QT_TRANSLATE_NOOP("mcp", "Prende o apaga el túnel de pinggy y lo configura (lo mismo que Configuración → Pinggy). Solo cambia lo que mandes. Reinicia la conferencia (corta a los invitados un momento). "
                "Para armarlo de cero con la API key (token, subdominio y puerto de la cuenta) usá acceso_remoto_configurar_con_api_key. "
                "`direccion`: \"azar\" (la que dé pinggy), \"pinggy\" (subdominio fijo del token) o \"propio\" (dominio "
                "propio: va con `host` y `cname_destino`).");
            t.inputSchema = {
                {"type", "object"},
                {"properties",
                 {{"activo", {{"type", "boolean"}}},
                  {"region", {{"type", "string"}, {"enum", {"auto", "us", "eu", "ap"}}}},
                  {"direccion", {{"type", "string"}, {"enum", {"azar", "pinggy", "propio"}}}},
                  {"host", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "subdominio fijo o dominio propio")}}},
                  {"cname_destino", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "lo que muestra el panel de pinggy")}}},
                  {"turn", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "video de los invitados de afuera por el túnel")}}},
                  {"turn_udp", {{"type", "boolean"}}},
                  {"turn_tcp_puerto", {{"type", "integer"}}},
                  {"turn_udp_puerto", {{"type", "integer"}}},
                  {"lista_blanca", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "IPs o redes separadas por coma; vacío = todos")}}}}}};
            t.examples = {R"({"activo": true, "direccion": "propio", "host": "sala.miorganismo.gob.ar", "cname_destino": "abc.a.pinggy.link"})"};
            SettingsModel* settings = settings_.get();
            t.run = [settings, &vm](const json& a, Done done) {
                if (vm.alAire())
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "Se está transmitiendo o grabando: el túnel no se toca hasta que termine "
                                                  "(reinicia la conferencia). Si hace falta, lo hace el operador desde la app.")));
                static const std::vector<std::pair<const char*, const char*>> kMapa = {
                    {"activo", "pinggy.activo"},           {"region", "pinggy.region"},
                    {"direccion", "pinggy.dominioTipo"},   {"host", "pinggy.host"},
                    {"cname_destino", "pinggy.cnameDestino"}, {"turn", "pinggy.turn"},
                    {"turn_udp", "pinggy.turnUdp"},        {"turn_tcp_puerto", "pinggy.turnTcpPuerto"},
                    {"turn_udp_puerto", "pinggy.turnUdpPuerto"}, {"lista_blanca", "pinggy.listaBlanca"}};
                settings->revert();
                int n = 0;
                for (const auto& [arg, clave] : kMapa) {
                    if (!a.contains(arg)) continue;
                    if (!settings->value(clave).isValid()) {
                        settings->revert();
                        return done(ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "esta versión no tiene %1"), {clave})));
                    }
                    settings->setValue(clave, jsonToQv(a[arg]));
                    ++n;
                }
                if (n == 0) return done(ToolResult::error(ba::i18n::traducir("mcp", "no mandaste nada para cambiar")));
                const QString summary = settings->apply(); // la vista se recarga sola (aviso «Ajustes»)
                done(settings->fallo() ? ToolResult::error(summary.toStdString()) : ToolResult::ok(summary.toStdString()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_dns_instrucciones";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: instrucciones para el DNS");
            t.category = "sala";
            t.clase = Clase::Lectura;
            t.description = QT_TRANSLATE_NOOP("mcp", "El texto para mandarle al que administra el DNS (registro CNAME, pasos en el panel de "
                            "pinggy, comodín, dominio raíz). Sin argumentos, con el dominio y el destino configurados.");
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"host", {{"type", "string"}}}, {"cname_destino", {{"type", "string"}}}}}};
            t.run = [acc, &rt = rt_](const json& a, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                const QString host = QString::fromStdString(a.value("host", rt.gateway().pinggyHost));
                const QString dest = QString::fromStdString(a.value("cname_destino", rt.gateway().pinggyCnameDestino));
                if (host.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "falta el dominio (host)")));
                done(ToolResult::ok(acc->instruccionesDns(host, dest).toStdString()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_dns_verificar";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: verificar el DNS del dominio propio");
            t.category = "sala";
            t.clase = Clase::Lectura;
            t.openWorld = true;
            t.description = QT_TRANSLATE_NOOP("mcp", "Resuelve el CNAME del dominio y dice si apunta al destino de pinggy (estado: ok, otro, "
                            "mal, error). Sin argumentos, con lo configurado.");
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"host", {{"type", "string"}}}, {"cname_destino", {{"type", "string"}}}}}};
            t.run = [acc, esperar, &rt = rt_](const json& a, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                const QString host = QString::fromStdString(a.value("host", rt.gateway().pinggyHost));
                const QString dest = QString::fromStdString(a.value("cname_destino", rt.gateway().pinggyCnameDestino));
                acc->verificarDns(host, dest);
                esperar(&AccesoRemoto::dnsChanged, [acc]() { return acc ? acc->dns() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_probar_direccion";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: probar si pinggy da esa dirección");
            t.category = "sala";
            t.clase = Clase::Lectura;
            t.openWorld = true;
            t.description =
                QT_TRANSLATE_NOOP("mcp", "Prueba un subdominio fijo o un dominio propio con el token guardado: abre un túnel de prueba de unos "
                "segundos (sin desplazar a otro) y se fija si pinggy da esa dirección. ok = asignada al token y libre; "
                "mal = no asignada (pinggy dio otra al azar) o en uso por otro túnel (con la API key también mira los "
                "túneles activos de la cuenta); error = falta el token, el túnel de este equipo está prendido, etc. "
                "Sin argumentos, con lo configurado.");
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"direccion", {{"type", "string"}, {"enum", {"pinggy", "propio"}}}},
                               {"host", {{"type", "string"}}}}}};
            t.run = [acc, esperar, &rt = rt_](const json& a, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                const QString tipo = QString::fromStdString(a.value("direccion", rt.gateway().pinggyDominioTipo));
                const QString host = QString::fromStdString(a.value("host", rt.gateway().pinggyHost));
                acc->probarDireccion(tipo, host);
                esperar(&AccesoRemoto::pruebaChanged, [acc]() {
                    QVariantMap m = acc ? acc->prueba() : QVariantMap{};
                    if (m.value("estado") == "probando") m["estado"] = "consultando"; // sigue esperando
                    return m;
                }, done);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_configurar_con_api_key";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: armar pinggy solo con la API key");
            t.category = "sala";
            t.clase = Clase::Escritura;
            t.openWorld = true;
            t.description = QT_TRANSLATE_NOOP("mcp", "Con SOLO la API key guardada (acceso_remoto_clave tipo api_key) arma todo pinggy: le pide a "
                            "la cuenta el token del túnel (lo guarda cifrado, nunca se muestra), su subdominio fijo y su "
                            "puerto TCP fijo, y prende el túnel con eso (reinicia la conferencia). Con `subdominio` (solo el "
                            "nombre, solo minúsculas y números, p. ej. \"salaconcejo\") primero lo cambia en la cuenta: queda "
                            "salaconcejo.a.pinggy.link; si pinggy no lo da (ocupado), avisa. No hace falta entrar al "
                            "panel de pinggy.");
            t.inputSchema = {{"type", "object"},
                             {"properties", {{"subdominio", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "opcional: el nombre que se quiere")}}}}}};
            t.run = [acc, esperar, &vm](const json& a, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                if (vm.alAire())
                    return done(ToolResult::error(ba::i18n::traducir("mcp", "Se está transmitiendo o grabando: el túnel no se toca hasta que termine (reinicia la conferencia).")));
                acc->configurarConApiKey(QString::fromStdString(a.value("subdominio", std::string())));
                esperar(&AccesoRemoto::cuentaChanged, [acc]() { return acc ? acc->cuenta() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_conectar";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: conectar o desconectar la conferencia de internet");
            t.category = "sala";
            t.clase = Clase::Escritura;
            t.openWorld = true;
            t.description = QT_TRANSLATE_NOOP("mcp", "Lo mismo que el botón «Conectar VPN» de la tarjeta Invitados: saca la conferencia a internet por "
                            "`via` o la vuelve a dejar solo en la red local (`via`: \"desconectar\"). Sin `via` dice "
                            "cómo está y las formas que hay con lo guardado.");
            t.details = QT_TRANSLATE_NOOP("mcp", "Las formas (solo las que se pueden con lo que hay guardado; la lista la da la llamada sin "
                        "`via`): pinggy (el token de la cuenta, con su dirección), pinggy-cuenta (solo la API key: "
                        "arma todo), cloudflare (el token de Cloudflare), pinggy-gratis (sin cuenta, dirección al "
                        "azar, se corta a los 60 min). Conectar por una apaga la otra. Reinicia la conferencia: los "
                        "invitados se reconectan solos. Con el evento corriendo no se toca.");
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"via", {{"type", "string"},
                                        {"enum", {"pinggy", "pinggy-cuenta", "cloudflare", "pinggy-gratis", "desconectar"}}}}}}};
            t.run = [acc](const json& a, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                const auto aJson = [](const QVariant& v) {
                    return json::parse(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
                };
                if (!a.contains("via"))
                    return done(ToolResult().data({{"conexion", aJson(acc->conexion())}, {"formas", aJson(acc->formasDeConectar())}}));
                const QString via = QString::fromStdString(a["via"].get<std::string>());
                const QString e = via == "desconectar" ? acc->desconectar() : acc->conectar(via);
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                done(ToolResult::ok(via == "desconectar" ? ba::i18n::traducir("mcp", "La conferencia vuelve a ser solo de la red local (se reinicia).")
                                                         : ba::i18n::llenar(ba::i18n::traducir("mcp", "Conectando por %1 (la conferencia se reinicia). "
                                                                                             "Mirá acceso_remoto_estado en unos segundos."),
                                                                           {via.toStdString()})));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_cortar_tuneles";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: cortar los túneles activos de la cuenta de pinggy");
            t.category = "sala";
            t.clase = Clase::Escritura;
            t.destructive = true;
            t.openWorld = true;
            t.description = QT_TRANSLATE_NOOP("mcp", "Con la API key, corta todos los túneles activos de la cuenta de pinggy (p. ej. otro equipo "
                            "que quedó con el mismo token). El de este equipo, si está prendido, vuelve a conectarse solo.");
            t.run = [acc, esperar](const json&, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                acc->cortarTunelesDeLaCuenta();
                esperar(&AccesoRemoto::cuentaChanged, [acc]() { return acc ? acc->cuenta() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_panel";
            t.title = ba::i18n::traducir("mcp", "Acceso remoto: túneles activos en la cuenta de pinggy");
            t.category = "sala";
            t.clase = Clase::Lectura;
            t.openWorld = true;
            t.description = QT_TRANSLATE_NOOP("mcp", "Consulta el panel de pinggy con la API key guardada (acceso_remoto_clave tipo api_key): "
                            "qué túneles tiene activos la cuenta, con qué dominio y puertos. Los secretos que vengan "
                            "en la respuesta salen tapados.");
            t.run = [acc, esperar](const json&, Done done) {
                if (!acc) return done(ToolResult::error(ba::i18n::traducir("mcp", "no disponible")));
                acc->verEnPinggy();
                esperar(&AccesoRemoto::panelChanged, [acc]() { return acc ? acc->panel() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
    }
    {
        Tool t;
        t.name = "proxy_renovar";
        t.title = ba::i18n::traducir("mcp", "Proxy: pedir los certificados ya");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Pide ya a Let's Encrypt el certificado de `host` (o de todos), aunque no esté por vencer. "
                        "Tarda; el resultado se ve en proxy_estado (acme.ultimo_error).");
        t.inputSchema = {{"type", "object"}, {"properties", {{"host", {{"type", "string"}}}}}};
        t.run = [&control, contestar](const json& a, Done done) {
            control.renovarProxy(a.value("host", std::string()), contestar(ba::i18n::traducir("mcp", "Pedido: mirá proxy_estado en un rato."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "proxy_configurar";
        t.title = ba::i18n::traducir("mcp", "Proxy: dominio, puertos, servicios y ACME");
        t.category = "sala";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "reinicia la conferencia (corta a los invitados) con el proxy nuevo");
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Configura el proxy por nombre y REINICIA la conferencia (corta a los invitados): `activo`, `dominio` (los "
            "servicios quedan en <nombre>.<dominio>: hay que apuntar esos nombres a esta máquina en el DNS), "
            "`puertoTls` (443) y `puertoHttp` (80; en Linux los de menos de 1024 piden permiso), `servicios` "
            "([{nombre, destino, ruta, soloLan}]; sin esto, los de siempre: sala, sesion, presidencia, botonera, "
            "prompter, tablero) y `acme` ({activo, email, pruebas}: Let's Encrypt; `pruebas` usa su entorno de "
            "prueba, sin límites). Lo que no se pasa queda como estaba.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"dominio", {{"type", "string"}}},
                           {"puertoTls", {{"type", "integer"}}},
                           {"puertoHttp", {{"type", "integer"}}},
                           {"servicios", {{"type", "array"}}},
                           {"acme", {{"type", "object"}}}}}};
        t.run = [&control, &rt](const json& a, Done done) {
            // La misma operación que Configuración → Proxy: mezcla, guarda y reinicia la sala.
            const std::string e = control.configurarProxy(a, [done, &rt](const std::string& err) {
                json r{{"proxy", rt.gateway().proxy}, {"sala_reiniciada", err.empty()}};
                if (!err.empty()) r["error"] = err;
                done(ToolResult().data(r));
            });
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_prueba";
        t.title = ba::i18n::traducir("mcp", "Prueba técnica de un invitado: eco y red");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Con alguien de ADENTRO (programa o fila de oradores): le manda un tono de 1 kHz cortado por su "
            "retorno durante 4 s y mide cuánto vuelve por su micrófono (`eco_db`; más de 10 dB = se escucha el "
            "parlante: auriculares), y lee su red (rtt, pérdida, jitter, semáforo). Mientras dura, el invitado "
            "escucha el tono y el programa más bajo: avisale antes. Devuelve `recomendacion` en castellano.");
        t.inputSchema = {{"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, &gente](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            gente.prueba(p["id"], [done](const std::string& err, const json& r) {
                if (!err.empty()) return done(ToolResult::error(err));
                done(ToolResult().data(r));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_request";
        t.title = ba::i18n::traducir("mcp", "Pedirle algo a un invitado");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Le pide al navegador del invitado: silenciar el microfono, apagar la camara, dejar de "
            "compartir pantalla o bajar la mano.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"participant", personProp},
              {"what",
               {{"type", "string"},
                {"enum",
                 {"mute-mic", "mute-cam", "unmute-mic", "unmute-cam", "stop-screen", "lower-hand"}}}}}},
            {"required", {"participant", "what"}}};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const std::string e =
                gente.pedirle(p["id"], a.value("what", std::string()), contestar(ba::i18n::traducir("mcp", "Pedido enviado."), done));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_calidad";
        t.title = ba::i18n::traducir("mcp", "Bajar o devolver la calidad de la camara de un invitado");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Cuando la red de un invitado no da (room_status: `red.semaforo` ambar o rojo, perdida, jitter, RTT), "
            "le pide a su navegador que mande menos: `maximo` 720p (techo 1,5 Mbps) o 360p (0,5 Mbps); 1080p "
            "devuelve la calidad completa. No corta nada: baja resolucion y bitrate.");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participant", personProp},
                           {"maximo", {{"type", "string"}, {"enum", {"1080p", "720p", "360p"}}}}}},
                         {"required", {"participant", "maximo"}}};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const std::string m = a.value("maximo", std::string("720p"));
            const std::string e = gente.pedirle(p["id"], m == "1080p" ? "calidad-max" : "calidad-" + m,
                                                contestar(ba::i18n::traducir("mcp", "Pedido enviado: el navegador del invitado ajusta su camara."), done));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_profile";
        t.title = ba::i18n::traducir("mcp", "Datos de una persona");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Cambia nombre, informacion y/o contacto de una persona (los ve la produccion y el tablero).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participant", personProp},
                           {"name", {{"type", "string"}}},
                           {"info", {{"type", "string"}}},
                           {"contact", {{"type", "string"}}}}},
                         {"required", {"participant"}}};
        t.run = [withPerson, &gente, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const auto campo = [&a](const char* k) {
                return a.contains(k) && a[k].is_string() ? std::optional<std::string>(a[k].get<std::string>())
                                                         : std::nullopt;
            };
            gente.perfil(p["id"], campo("name"), campo("info"), campo("contact"), contestar(ba::i18n::traducir("mcp", "Datos actualizados."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "chat_send";
        t.title = ba::i18n::traducir("mcp", "Escribir en el chat");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Mensaje del operador a toda la conferencia o a una persona (también al integrante que espera en "
                        "la sesión remota y se lo atiende hablando: se le dice con la voz del atendedor).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"text", {{"type", "string"}}},
                           {"to", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "'all' (defecto) o una persona.")}}}}},
                         {"required", {"text"}}};
        t.run = [&vm, &control, contestar](const json& a, Done done) {
            std::string to = a.value("to", std::string("all"));
            // El que espera en la sesión remota y lo atiende el asistente hablando no
            // está en la sala ni tiene chat: lo que se le escribe se le DICE, solo a
            // él (docs/funciones/ATENCION.md). Las mismas herramientas que en el lobby.
            if (AtencionViewModel* at = vm.atencionVm(); at != nullptr && at->atiendeHablandoEnAsamblea(QString::fromStdString(to))) {
                const QString e = at->decirAlIntegrante(QString::fromStdString(to), QString::fromStdString(a.value("text", std::string())));
                return done(e.isEmpty() ? ToolResult().text(ba::i18n::traducir("mcp", "Dicho.")) : ToolResult::error(e.toStdString()));
            }
            if (to != "all") {
                json p;
                std::string err;
                if (!resolveParticipant(roomSnapshot(vm), to, p, err)) return done(ToolResult::error(err));
                to = p["id"];
            }
            const std::string e = control.chatear(to, a.value("text", std::string()), contestar(ba::i18n::traducir("mcp", "Enviado."), done));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitados_chat_leer";
        t.title = ba::i18n::traducir("mcp", "Chat reciente");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Lee el chat PRIVADO con los invitados de la conferencia (de quién, a quién, texto, hora); no es el chat del "
            "público.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 30}}}}}};
        t.run = [conf](const json& a, Done done) {
            // Todo lo recibido (el snapshot trae solo parte: el resto llega por eventos).
            const json chat = json::parse(QJsonDocument(static_cast<JsonListModel*>(conf->chat())->items())
                                              .toJson(QJsonDocument::Compact)
                                              .toStdString(),
                                          nullptr, false);
            const size_t n = std::min<size_t>(chat.size(), a.value("limit", 30));
            json out = json::array();
            for (size_t i = chat.size() - n; i < chat.size(); ++i)
                out.push_back(chat[i]);
            ToolResult r;
            r.data(json{{"messages", out}});
            for (const auto& m : out)
                if (!m.value("mine", false)) r.untrusted = true; // escrito por un invitado
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        // Para un agente de un tiro que atiende a alguien: una pregunta, un
        // chat_wait. Sin esto tendria que consultar invitados_chat_leer cada tanto,
        // que es caro (cada consulta es un turno) y torpe.
        Tool t;
        t.name = "chat_wait";
        t.title = ba::i18n::traducir("mcp", "Esperar que alguien escriba");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Espera hasta `timeoutSec` (max. 120) a que `participant` escriba en el chat de la conferencia y "
            "devuelve "
            "sus mensajes nuevos (`timedOut` true y lista vacia si no escribio). Para atender a alguien: una "
            "pregunta con chat_send, un chat_wait, y asi. Si se lo atiende hablando (en el lobby o esperando en "
            "la sesión remota), lo que dice llega acá pasado a texto (`voz: true`).");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"participant"}},
            {"properties",
             {{"participant", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "id o nombre")}}},
              {"timeoutSec", {{"type", "integer"}, {"minimum", 1}, {"maximum", 120}, {"default", 60}}}}}};
        t.run = [conf, &vm](const json& a, Done done) {
            json p;
            std::string err;
            std::string id = a["participant"].get<std::string>();
            // El que espera en la sesión remota atendido hablando: lo que dice entra al
            // mismo hilo (ConferenceViewModel::agregarDichoPorVoz), con su id de integrante.
            AtencionViewModel* at = vm.atencionVm();
            if (at == nullptr || !at->atiendeHablandoEnAsamblea(QString::fromStdString(id))) {
                if (!resolveParticipant(roomSnapshot(vm), id, p, err)) {
                    done(ToolResult::error(err));
                    return;
                }
                id = p.value("id", "");
            }
            auto* model = static_cast<JsonListModel*>(conf->chat());
            const auto leer = [model]() {
                return json::parse(QJsonDocument(model->items()).toJson(QJsonDocument::Compact).toStdString(),
                                   nullptr, false);
            };
            // Lo ya visto se recuerda por uid, no por posicion: la lista se
            // recorta por arriba y las posiciones se mueven.
            auto vistos = std::make_shared<std::set<std::string>>();
            for (const auto& m : leer())
                if (m.is_object()) vistos->insert(m.value("uid", ""));
            const std::int64_t vence = QDateTime::currentMSecsSinceEpoch() + a.value("timeoutSec", 60) * 1000;
            auto* timer = new QTimer(conf);
            timer->setInterval(400);
            QObject::connect(timer, &QTimer::timeout, conf, [timer, leer, vistos, id, vence, done]() {
                json nuevos = json::array();
                for (const auto& m : leer()) {
                    if (!m.is_object() || vistos->count(m.value("uid", "")) != 0) continue;
                    if (m.value("fromId", "") == id && !m.value("mine", false)) nuevos.push_back(m);
                }
                const bool vencio = QDateTime::currentMSecsSinceEpoch() >= vence;
                if (nuevos.empty() && !vencio) return;
                timer->stop();
                timer->deleteLater();
                ToolResult r;
                r.data(json{{"messages", nuevos}, {"timedOut", nuevos.empty()}});
                if (!nuevos.empty()) r.untrusted = true; // escrito por un invitado
                done(r);
            });
            timer->start();
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "share_list";
        t.title = ba::i18n::traducir("mcp", "Archivos compartidos");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Archivos que compartieron los invitados (imagen, video, audio) con id, nombre, quien, tamaño.");
        t.run = [&vm](const json&, Done done) {
            done(ToolResult().data(json{{"shares", roomSnapshot(vm).value("shares", json::array())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "compartida_revisar";
        t.title = ba::i18n::traducir("mcp", "Ver un archivo compartido");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "Muestra una imagen que COMPARTIÓ un invitado, para revisarla antes de ponerla al aire. Videos y "
            "audios: da sus datos.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}}}, {"maxWidth", {{"type", "integer"}, {"default", 1280}}}}},
            {"required", {"id"}}};
        t.run = [&control](const json& a, Done done) {
            control.verArchivo(a["id"].get<std::string>(), [a, done](const std::string& err, const json& share,
                                                                      const QByteArray& bytes) {
                if (!err.empty()) return done(ToolResult::error(err + " " + ba::i18n::traducir("mcp", "share_list dice cuáles hay.")));
                if (bytes.isEmpty()) return done(ToolResult().data(share)); // video o audio: sus datos
                QImage img;
                if (!img.loadFromData(bytes)) return done(ToolResult::error(ba::i18n::traducir("mcp", "No se pudo leer la imagen.")));
                ToolResult r;
                r.text(share.value("name", "") + " — de " + share.value("participantName", ""));
                addImage(r, img, a, 1280);
                done(r);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "share_take";
        t.title = ba::i18n::traducir("mcp", "Poner un archivo compartido en la escena");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Copia el archivo compartido a la carpeta de medios y lo agrega a la escena al aire (imagen, "
            "video o audio).");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [&control](const json& a, Done done) {
            // La misma operación que el botón «Tomar» de la página Sala.
            const sala::ControlSala::Tomado r = control.tomarArchivo(a["id"].get<std::string>());
            if (!r.error.empty()) return done(ToolResult::error(r.error));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» agregado a la escena «%2» como «%3»."), {r.nombre, r.escena, r.fuente})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "share_delete";
        t.title = ba::i18n::traducir("mcp", "Borrar archivo compartido");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.destructive = true;
        t.description = QT_TRANSLATE_NOOP("mcp", "Borra del gateway de la conferencia un archivo que compartió un invitado (`id`, de share_list).");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [&control](const json& a, Done done) {
            control.borrarArchivo(a.value("id", std::string()), [done](const std::string& err, const json&) {
                done(err.empty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Borrado.")) : ToolResult::error(sala::explicarError(err)));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_link";
        t.title = ba::i18n::traducir("mcp", "Enlace y QR de ingreso (o del tablero)");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Enlace para entrar a la conferencia (target local = misma red; remote = la dirección pública de pinggy, "
            "Cloudflare o WireGuard, para los de afuera) con su QR como imagen; con withPin el PIN va incluido. "
            "Con board=true, el del tablero de oradores. La imagen la ves solo vos: para que el operador vea el QR "
            "y lo pueda copiar, después llamá a qr_mostrar con la URL.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"target", {{"type", "string"}, {"enum", {"local", "remote"}}, {"default", "local"}}},
              {"withPin", {{"type", "boolean"}, {"default", false}}},
              {"board", {{"type", "boolean"}, {"default", false}}}}}};
        t.run = [&enlaces](const json& a, Done done) {
            const bool remoto = a.value("target", std::string("local")) == "remote";
            const auto alVolver = [done, remoto](const sala::Enlace& e) {
                if (!e.ok) return done(ToolResult::error(e.mensaje.empty() ? ba::i18n::traducir("mcp", "sin enlace") : e.mensaje));
                ToolResult res;
                const std::string url = e.url;
                res.text(url + "\n\n(" +
                         ba::i18n::llenar(ba::i18n::traducir("mcp", "El operador NO ve esta imagen. Para mostrarle el QR y que lo pueda copiar: "
                                                               "qr_mostrar {\"texto\": \"%1\", \"descripcion\": \"…\"}."),
                                          {url}) +
                         (!remoto ? " " + ba::i18n::traducir("mcp", "Es la dirección de la red local: para alguien de afuera, "
                                                "room_link {\"target\": \"remote\"}.")
                                  : std::string()) + ")");
                res.structured = json{{"url", url}, {"remoto", remoto}};
                const std::string png = qrPng(json(e.filas));
                if (!png.empty()) res.image(png, "image/png");
                done(res);
            };
            if (a.value("board", false))
                enlaces.tablero(remoto, alVolver);
            else
                enlaces.ingreso(remoto, a.value("withPin", false), alVolver);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "network_status";
        t.title = ba::i18n::traducir("mcp", "Red y acceso de invitados");
        t.category = "sala";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Como se llega por RED al aparato: HTTPS, tunel WireGuard, IPs locales, "
                        "enlaces y puertos WebRTC del gateway.");
        t.run = [&control](const json&, Done done) {
            control.verRed([done](const std::string& err, const json& r) {
                done(err.empty() ? ToolResult().data(r) : ToolResult::error(ba::i18n::traducir("mcp", "La conferencia no responde.")));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "operator_feed";
        t.title = ba::i18n::traducir("mcp", "Como te ve y oye el lobby");
        t.category = "sala";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Elige con que fuente de video y de audio (por nombre) te ven y oyen los que esperan en el "
            "lobby. Vacio = placa de espera / sin voz. Sin argumentos lista las opciones.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"video", {{"type", "string"}}}, {"audio", {{"type", "string"}}}}}};
        t.run = [conf](const json& a, Done done) {
            if (!a.contains("video") && !a.contains("audio")) {
                json v = json::array();
                json au = json::array();
                for (const QString& s : conf->videoSourceNames())
                    v.push_back(s.toStdString());
                for (const QString& s : conf->audioSourceNames())
                    au.push_back(s.toStdString());
                return done(ToolResult().data(json{{"video", conf->operatorVideo().toStdString()},
                                                   {"audio", conf->operatorAudio().toStdString()},
                                                   {"videoOptions", v},
                                                   {"audioOptions", au}}));
            }
            conf->setOperatorFeed(
                QString::fromStdString(a.value("video", conf->operatorVideo().toStdString())),
                QString::fromStdString(a.value("audio", conf->operatorAudio().toStdString())));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "Listo: el lobby te ve con '%1' y te oye con '%2'."),
                                                {conf->operatorVideo().toStdString(), conf->operatorAudio().toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "gateway_restart";
        t.title = ba::i18n::traducir("mcp", "Reiniciar la conferencia (gateway)");
        t.category = "sala";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "reinicia el gateway (corta a los invitados)");
        t.destructive = true;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Reinicia el proceso de la conferencia: se cortan todos (se reconectan solos en segundos si el "
            "estado se puede recuperar). Solo si algo no responde.");
        t.run = [&rt](const json&, Done done) {
            rt.reiniciarSalaEnElFondo([done](const std::string& e) {
                QMetaObject::invokeMethod(QCoreApplication::instance(), [done, e]() {
                    done(e.empty() ? ToolResult::ok(ba::i18n::traducir("mcp", "Gateway reiniciandose.")) : ToolResult::error(e));
                }, Qt::QueuedConnection);
            });
        };
        core_.addTool(std::move(t));
    }

    // --- Oradores ----------------------------------------------------------------
    {
        Tool t;
        t.name = "remotos_fila_ver";
        t.title = ba::i18n::traducir("mcp", "Fila de oradores");
        t.category = "oradores";
        t.clase = Clase::Lectura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "Los oradores REMOTOS (por videollamada): la fila en orden, el estado de cada uno y cuánto le queda "
            "al que habla; no son las bancas del recinto.");
        t.run = [&vm](const json&, Done done) {
            const json s = roomSnapshot(vm);
            const double now = s.value("serverNowMs", 0.0);
            std::map<std::string, json> byId;
            for (const auto& p : s.value("participants", json::array()))
                byId[p.value("id", "")] = p;
            json order = json::array();
            int pos = 0;
            for (const auto& id : s.value("order", json::array())) {
                const auto it = byId.find(id.get<std::string>());
                if (it == byId.end()) continue;
                const json& p = it->second;
                json e{{"id", p["id"]},
                       {"name", p["name"]},
                       {"state", p.value("speaker", "")},
                       {"allottedSec", p.value("allottedSec", 0)},
                       {"usedSec", p.value("usedMs", 0.0) / 1000.0},
                       {"connected", p.value("connected", false)},
                       {"info", p.value("info", "")},
                       {"contact", p.value("contact", "")}};
                if (p.value("speaker", "") == "queued") e["position"] = ++pos;
                if (p.value("speaker", "") == "onair") {
                    e["remainingSec"] =
                        p.value("allottedSec", 0) - (now - p.value("onAirSinceMs", 0.0)) / 1000.0;
                    e["slot"] = p.value("slot", 0);
                }
                order.push_back(e);
            }
            done(ToolResult().data(json{{"config", s.value("speakers", json::object())}, {"order", order}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speakers_config";
        t.title = ba::i18n::traducir("mcp", "Configurar el modo oradores");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Activa/desactiva el modo oradores y define: tiempo general por orador, al aire uno a la vez "
            "o varios, slot por defecto, carrusel (manual; chain = al sacar entra el siguiente; time = "
            "rota solo pasado el tiempo + margen) y margen.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"enabled", {{"type", "boolean"}}},
              {"defaultTime",
               {{"type", json::array({"string", "integer"})}, {"description", QT_TRANSLATE_NOOP("mcp", "Segundos o 'm:ss'.")}}},
              {"multi", {{"type", "boolean"}}},
              {"defaultSlot", {{"type", "integer"}, {"minimum", 1}}},
              {"carousel", {{"type", "string"}, {"enum", {"manual", "chain", "time"}}}},
              {"graceSec", {{"type", "integer"}, {"minimum", 0}, {"maximum", 600}}}}}};
        t.run = [&oradores, contestar](const json& a, Done done) {
            json b = json::object();
            for (const char* k : {"enabled", "multi", "defaultSlot", "carousel", "graceSec"})
                if (a.contains(k)) b[k] = a[k];
            if (a.contains("defaultTime")) {
                const int s = parseSeconds(a["defaultTime"]);
                if (s <= 0) return done(ToolResult::error(ba::i18n::traducir("mcp", "defaultTime invalido (segundos o 'm:ss').")));
                b["defaultSec"] = s;
            }
            const std::string e = oradores.configurar(b, contestar(ba::i18n::traducir("mcp", "Modo oradores actualizado."), done));
            if (!e.empty()) done(ToolResult::error(e));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_on_air";
        t.title = ba::i18n::traducir("mcp", "Orador al aire / fuera");
        t.category = "oradores";
        t.clase = Clase::Aire;
        t.porQueAire = QT_TRANSLATE_NOOP("mcp", "pone o saca oradores del aire");
        t.aireSi = AireSi::SacarDelAire;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Pone al aire a un orador (on=true, arranca su reloj) o lo saca (on=false: vuelve a la fila "
            "como 'hablo', nunca se lo echa).");
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"participant", personProp}, {"on", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"participant"}}};
        t.run = [withPerson, &oradores, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const bool on = a.value("on", true);
            oradores.alAire(p["id"], on, contestar(ba::i18n::llenar(on ? ba::i18n::traducir("mcp", "%1 al aire.") : ba::i18n::traducir("mcp", "%1 fuera del aire."), {p.value("name", "")}), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_next";
        t.title = ba::i18n::traducir("mcp", "Siguiente orador");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Pasa al orador que SIGUE en la fila. Pone al aire al siguiente de la fila (en modo "
                        "'uno a la vez', el que estaba sale).");
        t.run = [&oradores, contestar](const json&, Done done) {
            oradores.siguiente(contestar(ba::i18n::traducir("mcp", "Siguiente al aire."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_move";
        t.title = ba::i18n::traducir("mcp", "Reordenar la fila");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Mueve a una persona a una posicion de la fila (1 = primera).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participant", personProp}, {"position", {{"type", "integer"}, {"minimum", 1}}}}},
                         {"required", {"participant", "position"}}};
        t.run = [withPerson, &oradores, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            oradores.mover(p["id"], a.value("position", 1) - 1, contestar(ba::i18n::traducir("mcp", "Fila reordenada."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_queue";
        t.title = ba::i18n::traducir("mcp", "Mandar a la fila de oradores");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "QUE ES: pone en la FILA de oradores a alguien que ya esta en la conferencia (o devuelve a la fila a "
            "quien ya hablo). PARA QUE: en modo oradores todos siguen en la conferencia y la fila es solo el "
            "orden en que se los saca al aire. COMO SE USA: `participant` (nombre o id). Sale del aire "
            "hasta que le toque; `speaker_on_air` o `speaker_next` lo suben. Si era presentador, deja de "
            "serlo.");
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, &oradores, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            oradores.aLaFila(p["id"], contestar(ba::i18n::traducir("mcp", "A la fila de oradores."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_presenter";
        t.title = ba::i18n::traducir("mcp", "Presentador fijo");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "QUE ES: marca a una persona como PRESENTADOR: se queda al aire con su propio lugar mientras "
            "los oradores rotan a su lado. PARA QUE: un conductor remoto fijo con invitados que entran y "
            "salen. COMO SE USA: `participant` (nombre o id) y `on` (true/false). Hay uno solo a la vez; "
            "no entra en la fila y la rotacion no lo saca. Con `on=false` deja de serlo y sale del aire "
            "(despues se lo puede mandar a la fila con `speaker_queue`).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"participant", personProp}, {"on", {{"type", "boolean"}}}}},
                         {"required", {"participant"}}};
        t.run = [withPerson, &oradores, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const bool on = a.value("on", true);
            oradores.presentador(p["id"], on, contestar(on ? ba::i18n::traducir("mcp", "Presentador fijo.") : ba::i18n::traducir("mcp", "Ya no es presentador."), done));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_time";
        t.title = ba::i18n::traducir("mcp", "Tiempo de un orador");
        t.category = "oradores";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Tiempo asignado para hablar de una persona (segundos o 'm:ss'): '+2:00' le suma dos "
                        "minutos, '-30' le saca treinta segundos, '2:30' o '=2:30' lo deja en eso. Se puede "
                        "cambiar estando al aire.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"participant", personProp}, {"time", {{"type", json::array({"string", "integer"})}}}}},
            {"required", {"participant", "time"}}};
        t.run = [withPerson, &oradores, &vm, contestar](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            // «+2:00» / «-30»: sobre lo que ya tiene (el suyo, o el general si no tiene uno);
            // «=2:30» o «2:30»: eso. Antes el signo se perdia y «+2:00» lo DEJABA en dos minutos.
            int s = 0;
            std::string txt = a["time"].is_string() ? a["time"].get<std::string>() : std::string();
            while (!txt.empty() && txt.front() == ' ') txt.erase(0, 1);
            if (!txt.empty() && (txt.front() == '+' || txt.front() == '-')) {
                const int signo = txt.front() == '-' ? -1 : 1;
                int base = p.value("allottedSec", 0);
                if (base <= 0) base = roomSnapshot(vm).value("speakers", json::object()).value("defaultSec", 0);
                s = base + signo * parseSeconds(json(txt.substr(1)));
            } else if (!txt.empty() && txt.front() == '=') {
                s = parseSeconds(json(txt.substr(1)));
            } else {
                s = parseSeconds(a["time"]);
            }
            const std::string e = oradores.tiempo(p["id"], s, contestar(ba::i18n::llenar(ba::i18n::traducir("mcp", "Tiempo: %1 s."), {std::to_string(s)}), done));
            if (!e.empty()) done(ToolResult::error(e + " " + ba::i18n::traducir("mcp", "(segundos o 'm:ss').")));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
