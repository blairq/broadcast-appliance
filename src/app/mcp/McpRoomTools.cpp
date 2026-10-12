// ============================================================================
// src/app/mcp/McpRoomTools.cpp — Sala de videoconferencia y modo oradores
//
// La sala vive en el Proceso B; estas herramientas usan la MISMA API de
// control que la vista Qt y la consola web (ConferenceClient). El estado se lee
// del snapshot vigente (llega por eventos, sin pedirlo) y las acciones son
// POST/DELETE a B.
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

#include "arranque/AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "net/AccesoRemoto.hpp"
#include "ui/EventoViewModel.hpp"
#include "ui/SettingsModel.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/ConferenceViewModel.hpp"
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
    err = prefix.size() > 1 ? "'" + query + "' coincide con varias personas: " + names + ". Usa el id."
                            : "No hay nadie '" + query + "' en la sala." +
                                  (names.empty() ? " La sala esta vacia." : " Estan: " + names + ".");
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

json fromQt(const QJsonObject& o) { return toJson(o); }

std::string errorText(const json& reply) {
    static const std::map<std::string, std::string> kText{{"full", "no hay lugar libre (sala llena)"},
                                                          {"not_found", "ya no esta en la sala"},
                                                          {"state", "no aplica en su estado actual"},
                                                          {"empty", "no hay nadie mas en la fila"},
                                                          {"offline", "la sala (gateway) no responde"}};
    const std::string e = reply.value("error", std::string("offline"));
    const auto it = kText.find(e);
    return it != kText.end() ? it->second : e;
}

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
        "sala", "Sala de videoconferencia",
        "Invitados por WebRTC: lobby, admitir, hablar, pedir cosas, chat, archivos, enlaces y QR, red.");
    core_.addCategory("oradores", "Modo oradores",
                      "Fila de oradores con reloj: al aire por turno, orden, tiempos, carrusel.");

    StudioViewModel& vm = vm_;
    AppRuntime& rt = rt_;
    auto* conf = static_cast<ConferenceViewModel*>(vm.conference());
    ConferenceClient* client = &conf->client();

    // POST generico a B con el resultado como texto.
    const auto post = [client](const QString& path, const json& body, const std::string& okText, Done done) {
        client->post(path, toQt(body), [okText, done](bool ok, const QJsonObject& reply) {
            done(ok ? ToolResult::ok(okText)
                    : ToolResult::error("La sala respondio: " + errorText(fromQt(reply))));
        });
    };
    const auto withPerson = [&vm](const json& a, const char* field, json& p, Done& done) {
        std::string err;
        if (!resolveParticipant(roomSnapshot(vm), a.value(field, std::string()), p, err)) {
            done(ToolResult::error(err));
            return false;
        }
        return true;
    };
    const auto partPath = [](const json& p, const char* action) {
        return "/api/v1/participants/" +
               QString::fromUtf8(QUrl::toPercentEncoding(QString::fromStdString(p["id"]))) + "/" + action;
    };
    const json personProp = {{"type", "string"},
                             {"description", "Nombre (o prefijo unico) o id de la persona."}};

    {
        Tool t;
        t.name = "room_status";
        t.title = "Estado de la sala";
        t.category = "sala";
        t.readOnly = true;
        t.description =
            "Todo lo de la sala: nombre, PIN, lobby/cerrada, enlace, cada persona (estado, lugar, "
            "microfono, camara, pantalla, mano levantada, conexion, info y contacto, turno de orador), "
            "archivos compartidos, chat reciente y config de oradores.";
        t.run = [&vm, conf](const json&, Done done) {
            if (!conf->online()) return done(ToolResult::error("La sala (gateway) no esta conectada."));
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
        t.title = "Lobby / sala cerrada";
        t.category = "sala";
        t.description =
            "Lobby de admision (true = cada invitado espera que lo hagas pasar), sala cerrada (nadie "
            "nuevo puede entrar) y `cupo`: cuántos invitados en el programa (1 a 16), SIN reiniciar la sala: los "
            "lugares nuevos se crean andando (no saca a nadie: si un lugar que se iría está ocupado, no lo baja).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"lobby", {{"type", "boolean"}}},
              {"locked", {{"type", "boolean"}}},
              {"cupo", {{"type", "integer"}, {"minimum", 1}, {"maximum", 16}}}}}};
        t.run = [post, &vm = vm_](const json& a, Done done) {
            if (a.contains("cupo") && !a.contains("lobby") && !a.contains("locked")) {
                const int n = a.value("cupo", 2);
                const QString e = vm.cupo(n, [done, n](const QString& err) {
                    if (!err.isEmpty()) done(ToolResult::error("La sala respondio: " + err.toStdString()));
                    else done(ToolResult::ok("Cupo: " + std::to_string(n) + " invitados, sin reiniciar la sala."));
                });
                if (!e.isEmpty()) done(ToolResult::error(e.toStdString()));
                return;
            }
            if (a.contains("cupo")) {
                const QString e = vm.cupoUi(a.value("cupo", 2));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
            }
            json b = json::object();
            if (a.contains("lobby")) b["lobby"] = a["lobby"];
            if (a.contains("locked")) b["locked"] = a["locked"];
            post("/api/v1/room", b, "Sala actualizada.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_pin";
        t.title = "Cambiar el PIN";
        t.category = "sala";
        t.description =
            "PIN nuevo para entrar a la sala (sin `pin`: se genera uno). El anterior deja de servir.";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"pin", {{"type", "string"}, {"description", "4 a 32 caracteres."}}}}}};
        t.run = [client](const json& a, Done done) {
            json b = json::object();
            if (a.contains("pin")) b["pin"] = a["pin"];
            client->post("/api/v1/pin", toQt(b), [done](bool ok, const QJsonObject& r) {
                const json j = fromQt(r);
                done(ok ? ToolResult::ok("PIN nuevo: " + j.value("pin", ""))
                        : ToolResult::error(errorText(j)));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_pin_bloqueos";
        t.title = "Bloqueos por PIN mal";
        t.category = "sala";
        t.description =
            "Quién quedó bloqueado por poner mal el PIN y cómo destrabarlo. `accion`: «ver» (de fábrica) o "
            "«desbloquear» (con `ip`, solo esa; sin `ip`, todas y también el bloqueo global).";
        t.details =
            "Hay dos bloqueos. POR IP: tras `intentosPorIp` PIN mal desde la misma IP (de fábrica 5), esa IP no "
            "puede entrar por `bloqueoS` segundos (Configuración → Sala). GLOBAL: de último recurso, si hay "
            "muchísimos fallos entre todos (un ataque repartido) se cierra la entrada para todos un rato. "
            "Por internet la IP es la real (la pasa ba-web desde el túnel). Desbloquear no cambia el PIN: si "
            "alguien lo está adivinando, mejor cambiarlo (room_pin). Usalo cuando un invitado legítimo se "
            "equivocó varias veces y no puede entrar, o cuando nadie puede entrar (global).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"accion", {{"type", "string"}, {"enum", {"ver", "desbloquear"}}}},
                           {"ip", {{"type", "string"}, {"description", "Solo esta IP (al desbloquear)."}}}}}};
        t.examples = {R"({})", R"({"accion": "desbloquear"})", R"({"accion": "desbloquear", "ip": "190.12.34.56"})"};
        t.run = [client](const json& a, Done done) {
            const auto mostrar = [done](const json& r, const std::string& antes) {
                const json b = r.value("bloqueos", json::object());
                std::string txt = antes;
                const json g = b.value("global", json::object());
                if (g.value("bloqueado", false))
                    txt += "BLOQUEO GLOBAL activo: nadie puede entrar por " + std::to_string(g.value("quedanS", 0)) +
                           " s más (" + std::to_string(g.value("fallos", 0)) + " fallos entre todos).\n";
                int n = 0;
                for (const auto& ip : b.value("ips", json::array()))
                    if (ip.value("bloqueada", false)) {
                        txt += "- " + ip.value("ip", std::string()) + ": bloqueada " +
                               std::to_string(ip.value("quedanS", 0)) + " s más\n";
                        ++n;
                    }
                if (!n && !g.value("bloqueado", false)) txt += "No hay nadie bloqueado.";
                done(ToolResult::ok(txt).data(b));
            };
            if (a.value("accion", std::string("ver")) == "desbloquear") {
                json b = json::object();
                if (a.contains("ip") && a["ip"].is_string() && !a["ip"].get<std::string>().empty()) b["ip"] = a["ip"];
                client->post("/api/v1/pin/desbloquear", toQt(b), [done, mostrar, b](bool ok, const QJsonObject& r) {
                    const json j = fromQt(r);
                    if (!ok) return done(ToolResult::error("La sala respondio: " + errorText(j)));
                    mostrar(j, b.contains("ip") ? "Desbloqueada " + b["ip"].get<std::string>() + ".\n"
                                                : "Desbloqueadas " + std::to_string(j.value("desbloqueadas", 0)) +
                                                      " IP y el bloqueo global.\n");
                });
                return;
            }
            client->get("/api/v1/pin/bloqueos", [done, mostrar](bool ok, const QJsonObject& r) {
                if (!ok) return done(ToolResult::error("La sala (gateway) no respondió."));
                mostrar(fromQt(r), "");
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
        t.title = "Volver a la fila";
        t.category = "oradores";
        t.description = "Orador que ya hablo: vuelve al final de la fila.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.examples = {R"({"participant": "Marta"})"};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "requeue"), json::object(), "Vuelve a la fila.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_admit";
        t.title = "Hacer pasar a alguien del lobby";
        t.category = "sala";
        t.description = "Hace PASAR a alguien que espera en el lobby: entra a la sala. Admite a UNA persona "
                        "que espera: deja el lobby y entra a la sala, con su camara y su "
                        "microfono.";
        t.details = "Es el final del camino de la atencion en el lobby: `rostro_lobby` / `chat_send` para "
                    "saber quien es, `participant_profile` para dejarle la ficha, `operator_ask` para que "
                    "el operador decida, y esto cuando dijo que si.\n\n"
                    "Si no hay lugar libre en la sala (`room_status` -> `slotsFree`) el gateway la deja "
                    "esperando: admitir no crea lugares.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.examples = {R"({"participant": "Jorge"})"};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "admit"), json::object(), "Adentro.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_reject";
        t.title = "No dejar pasar a alguien del lobby";
        t.category = "sala";
        t.destructive = true;
        t.description = "Le niega la entrada a quien espera: se le cierra la sesion y sale del lobby.";
        t.details = "Para el que no tiene que estar. Si solo querés que espere un rato mas, no uses "
                    "esto: dejala en el lobby y avisale por `chat_send`.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "reject"), json::object(), "Afuera.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_kick";
        t.title = "Sacar a alguien de la sala";
        t.category = "sala";
        t.destructive = true;
        t.description = "Saca a alguien que YA esta adentro. Si esta al aire, sale al aire que se fue.";
        t.details = "Mira `room_status` antes: si esa persona esta en una escena al aire, sacarla se "
                    "ve en la transmision. Pasar el programa a otra escena primero es gratis; "
                    "deshacer esto no.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "kick"), json::object(), "Listo.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_admit_all";
        t.title = "Admitir a todos";
        t.category = "sala";
        t.description = "Hace pasar a todos los que esperan en el lobby, en orden de llegada.";
        t.run = [&vm, post](const json&, Done done) {
            int n = 0;
            for (const auto& p : roomSnapshot(vm).value("participants", json::array())) {
                if (p.value("state", "") != "waiting") continue;
                post("/api/v1/participants/" + QString::fromStdString(p["id"]) + "/admit", json::object(), "",
                     [](ToolResult) {});
                ++n;
            }
            done(ToolResult::ok(std::to_string(n) + " persona(s) admitidas (si habia lugar)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_talk";
        t.title = "Hablarle a un invitado (lobby en privado, adentro IFB)";
        t.category = "sala";
        t.description =
            "Con alguien del lobby: on=true le llega tu voz y vos lo escuchas por monitoreo (nadie mas). "
            "Con alguien de ADENTRO (programa o fila de oradores) es IFB: le llega tu voz mezclada con su "
            "retorno (el programa baja), sin salir al aire. on=false corta.";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"participant", personProp}, {"on", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "talk"), json{{"on", a.value("on", true)}}, "Listo.", done);
        };
        core_.addTool(std::move(t));
    }
    // --- El proxy por nombre (T61) ------------------------------------------------
    {
        Tool t;
        t.name = "proxy_estado";
        t.title = "Proxy por nombre: servicios y certificados";
        t.category = "sala";
        t.readOnly = true;
        t.description = "El proxy propio (443/80): cada servicio con su nombre (sesion.<dominio>, botonera.<dominio>…), "
                        "a qué va, si es solo de la red local, y su certificado (acme = Let's Encrypt, casa = "
                        "autofirmado) con la fecha en que vence (segundos desde 1970). Más el estado de ACME: último "
                        "error y si está pidiendo ahora.";
        t.run = [client, &rt](const json&, Done done) {
            client->get("/api/v1/proxy", [done, &rt](bool ok, const QJsonObject& r) {
                json out = fromQt(r);
                out["configuracion"] = rt.gateway().proxy;
                if (!ok) return done(ToolResult::error("la sala no contestó"));
                done(ToolResult().data(out));
            });
        };
        core_.addTool(std::move(t));
    }
    // --- Acceso remoto por pinggy (docs/planes/PINGGY.md) ---------------------------
    if (AccesoRemoto* ar = vm.accesoRemoto(); ar != nullptr) {
        const QPointer<AccesoRemoto> acc = ar;
        Tool t;
        t.name = "acceso_remoto_estado";
        t.title = "Acceso remoto por pinggy: estado y direcciones";
        t.category = "sala";
        t.readOnly = true;
        t.openWorld = true;
        t.description =
            "El túnel de pinggy.io por el que entran los de afuera: `pinggy.estado` (apagado, conectando, listo, "
            "reconectando, error), la dirección pública (`pinggy.urlPublica`) y `enlaces` (invitados, sesión remota, "
            "público, botonera), el último `pinggy.error` y el TURN (`pinggy.turn`). `configuracion`: activo, región, "
            "tipo de dirección (azar | pinggy = subdominio fijo | propio = dominio propio), host y destino del CNAME. "
            "`token_tunel` dice solo si está guardado o falta: el token lo carga el operador en Configuración → Acceso "
            "remoto, vos nunca lo pedís ni lo ves. Con dominio propio trae `instrucciones_dns` para pasarle al que "
            "administra el DNS. Para cambiar la configuración: settings_set con las claves pinggy.* (reinicia la sala). "
            "`disponible` false = la sala no está andando o es una versión que no informa el túnel. "
            "SIN TOKEN (gratis): dirección al azar que cambia cada vez, 60 minutos por túnel y pinggy le muestra a cada "
            "navegador un aviso que hay que aceptar. CON TOKEN (Pro): sin límite ni aviso, dirección fija (subdominio de "
            "pinggy o dominio propio con CNAME), puertos fijos; un token por equipo.";
        t.run = [acc](const json&, Done done) {
            if (!acc) return done(ToolResult::error("no disponible"));
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
                done(ToolResult::error("pinggy/DNS no contestó en 30 s"));
            });
            terminar(); // por si ya terminó
        };
        {
            Tool t;
            t.name = "acceso_remoto_clave";
            t.title = "Acceso remoto: guardar o borrar una clave (pinggy o Cloudflare)";
            t.category = "sala";
            t.idempotent = true;
            t.description =
                "Guarda CIFRADA en la bóveda (Credenciales/) la clave que te pasó el operador, o la borra. "
                "`tipo`: \"token\" (el token del PLAN de pinggy: abre el túnel; el del comando …@pro.pinggy.io), \"api_key\" (la "
                "API key de pinggy: control de la cuenta por API, con acceso_remoto_panel; NO abre túneles) o "
                "\"cloudflare\" (el token del túnel de Cloudflare, Configuración → Cloudflare). Nunca se devuelve ni se escribe "
                "en el registro: la respuesta dice solo «guardada». Con pinggy lo normal es guardar SOLO la api_key y "
                "seguir con acceso_remoto_configurar_con_api_key (trae el token sola); el token a mano es solo si no "
                "hay API key. Si no sabés cuál es, guardala como api_key y probá acceso_remoto_panel (si la acepta, es "
                "la API key). El token rige al reiniciar la sala. Guía: help {\"topic\": \"acceso_remoto\"}.";
            t.inputSchema = {
                {"type", "object"},
                {"properties",
                 {{"tipo", {{"type", "string"}, {"enum", {"token", "api_key", "cloudflare"}}}},
                  {"clave", {{"type", "string"}, {"description", "la clave (no hace falta para borrar)"}}},
                  {"borrar", {{"type", "boolean"}, {"default", false}}}}},
                {"required", {"tipo"}}};
            t.run = [acc](const json& a, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
                const std::string tipo = a.value("tipo", std::string());
                const bool borrar = a.value("borrar", false);
                const QString clave = QString::fromStdString(a.value("clave", std::string())).trimmed();
                QString e;
                if (tipo == "token") e = borrar ? acc->borrarToken() : acc->guardarToken(clave);
                else if (tipo == "api_key") e = borrar ? acc->borrarApiKey() : acc->guardarApiKey(clave);
                else if (tipo == "cloudflare") e = borrar ? acc->borrarTokenCloudflare() : acc->guardarTokenCloudflare(clave);
                else return done(ToolResult::error("tipo: \"token\", \"api_key\" o \"cloudflare\""));
                if (!e.isEmpty()) return done(ToolResult::error(e.toStdString()));
                done(ToolResult::ok(std::string(tipo == "token" ? "Token de túnel de pinggy " : tipo == "cloudflare" ? "Token de Cloudflare " : "API key ") +
                                    (borrar ? "borrado/a." : "guardado/a, cifrado/a en Credenciales/.") +
                                    (tipo != "api_key" && !borrar ? " Rige al reiniciar la sala (gateway_restart o "
                                                                  "aplicar Configuración)."
                                                                : "") +
                                    (acc->clavesIguales()
                                         ? " OJO: el token de túnel y la API key son la MISMA clave. Son dos cosas distintas del "
                                           "panel de pinggy: el token (el del comando del túnel, …@pro.pinggy.io) abre el túnel; la "
                                           "API key (API keys) solo sirve para consultar la cuenta. Con la API key como token el "
                                           "túnel no abre."
                                         : "")));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_configurar";
            t.title = "Acceso remoto: configurar pinggy";
            t.category = "sala";
            t.description =
                "Prende o apaga el túnel de pinggy y lo configura (lo mismo que Configuración → Pinggy). Solo cambia lo que mandes. Reinicia la sala (corta a los invitados un momento). "
                "Para armarlo de cero con la API key (token, subdominio y puerto de la cuenta) usá acceso_remoto_configurar_con_api_key. "
                "`direccion`: \"azar\" (la que dé pinggy), \"pinggy\" (subdominio fijo del token) o \"propio\" (dominio "
                "propio: va con `host` y `cname_destino`).";
            t.inputSchema = {
                {"type", "object"},
                {"properties",
                 {{"activo", {{"type", "boolean"}}},
                  {"region", {{"type", "string"}, {"enum", {"auto", "us", "eu", "ap"}}}},
                  {"direccion", {{"type", "string"}, {"enum", {"azar", "pinggy", "propio"}}}},
                  {"host", {{"type", "string"}, {"description", "subdominio fijo o dominio propio"}}},
                  {"cname_destino", {{"type", "string"}, {"description", "lo que muestra el panel de pinggy"}}},
                  {"turn", {{"type", "boolean"}, {"description", "video de los invitados de afuera por el túnel"}}},
                  {"turn_udp", {{"type", "boolean"}}},
                  {"turn_tcp_puerto", {{"type", "integer"}}},
                  {"turn_udp_puerto", {{"type", "integer"}}},
                  {"lista_blanca", {{"type", "string"}, {"description", "IPs o redes separadas por coma; vacío = todos"}}}}}};
            t.examples = {R"({"activo": true, "direccion": "propio", "host": "sala.miorganismo.gob.ar", "cname_destino": "abc.a.pinggy.link"})"};
            SettingsModel* settings = settings_.get();
            t.run = [settings, &vm](const json& a, Done done) {
                if (auto* ev = static_cast<EventoViewModel*>(vm.evento()); ev != nullptr && ev->enVivo())
                    return done(ToolResult::error("El evento está corriendo: el túnel no se toca hasta que termine "
                                                  "(reinicia la sala). Si hace falta, lo hace el operador desde la app."));
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
                        return done(ToolResult::error(std::string("esta versión no tiene ") + clave));
                    }
                    settings->setValue(clave, jsonToQv(a[arg]));
                    ++n;
                }
                if (n == 0) return done(ToolResult::error("no mandaste nada para cambiar"));
                const QString summary = settings->apply();
                auto* ui = static_cast<SettingsModel*>(vm.settings());
                if (!ui->dirty()) ui->revert();
                vm.refreshModels();
                done(summary.contains("EN VIVO") || summary.contains("ERROR") ? ToolResult::error(summary.toStdString())
                                                                              : ToolResult::ok(summary.toStdString()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_dns_instrucciones";
            t.title = "Acceso remoto: instrucciones para el DNS";
            t.category = "sala";
            t.readOnly = true;
            t.description = "El texto para mandarle al que administra el DNS (registro CNAME, pasos en el panel de "
                            "pinggy, comodín, dominio raíz). Sin argumentos, con el dominio y el destino configurados.";
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"host", {{"type", "string"}}}, {"cname_destino", {{"type", "string"}}}}}};
            t.run = [acc, &rt = rt_](const json& a, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
                const QString host = QString::fromStdString(a.value("host", rt.gateway().pinggyHost));
                const QString dest = QString::fromStdString(a.value("cname_destino", rt.gateway().pinggyCnameDestino));
                if (host.isEmpty()) return done(ToolResult::error("falta el dominio (host)"));
                done(ToolResult::ok(acc->instruccionesDns(host, dest).toStdString()));
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_dns_verificar";
            t.title = "Acceso remoto: verificar el DNS del dominio propio";
            t.category = "sala";
            t.readOnly = true;
            t.openWorld = true;
            t.description = "Resuelve el CNAME del dominio y dice si apunta al destino de pinggy (estado: ok, otro, "
                            "mal, error). Sin argumentos, con lo configurado.";
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"host", {{"type", "string"}}}, {"cname_destino", {{"type", "string"}}}}}};
            t.run = [acc, esperar, &rt = rt_](const json& a, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
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
            t.title = "Acceso remoto: probar si pinggy da esa dirección";
            t.category = "sala";
            t.readOnly = true;
            t.openWorld = true;
            t.description =
                "Prueba un subdominio fijo o un dominio propio con el token guardado: abre un túnel de prueba de unos "
                "segundos (sin desplazar a otro) y se fija si pinggy da esa dirección. ok = asignada al token y libre; "
                "mal = no asignada (pinggy dio otra al azar) o en uso por otro túnel (con la API key también mira los "
                "túneles activos de la cuenta); error = falta el token, el túnel de este equipo está prendido, etc. "
                "Sin argumentos, con lo configurado.";
            t.inputSchema = {{"type", "object"},
                             {"properties",
                              {{"direccion", {{"type", "string"}, {"enum", {"pinggy", "propio"}}}},
                               {"host", {{"type", "string"}}}}}};
            t.run = [acc, esperar, &rt = rt_](const json& a, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
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
            t.title = "Acceso remoto: armar pinggy solo con la API key";
            t.category = "sala";
            t.openWorld = true;
            t.description = "Con SOLO la API key guardada (acceso_remoto_clave tipo api_key) arma todo pinggy: le pide a "
                            "la cuenta el token del túnel (lo guarda cifrado, nunca se muestra), su subdominio fijo y su "
                            "puerto TCP fijo, y prende el túnel con eso (reinicia la sala). Con `subdominio` (solo el "
                            "nombre, p. ej. \"sala-concejo\") primero lo cambia en la cuenta: queda "
                            "sala-concejo.a.pinggy.link; si pinggy no lo da (ocupado), avisa. No hace falta entrar al "
                            "panel de pinggy.";
            t.inputSchema = {{"type", "object"},
                             {"properties", {{"subdominio", {{"type", "string"}, {"description", "opcional: el nombre que se quiere"}}}}}};
            t.run = [acc, esperar, &vm](const json& a, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
                if (auto* ev = static_cast<EventoViewModel*>(vm.evento()); ev != nullptr && ev->enVivo())
                    return done(ToolResult::error("El evento está corriendo: el túnel no se toca hasta que termine (reinicia la sala)."));
                acc->configurarConApiKey(QString::fromStdString(a.value("subdominio", std::string())));
                esperar(&AccesoRemoto::cuentaChanged, [acc]() { return acc ? acc->cuenta() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_cortar_tuneles";
            t.title = "Acceso remoto: cortar los túneles activos de la cuenta de pinggy";
            t.category = "sala";
            t.destructive = true;
            t.openWorld = true;
            t.description = "Con la API key, corta todos los túneles activos de la cuenta de pinggy (p. ej. otro equipo "
                            "que quedó con el mismo token). El de este equipo, si está prendido, vuelve a conectarse solo.";
            t.run = [acc, esperar](const json&, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
                acc->cortarTunelesDeLaCuenta();
                esperar(&AccesoRemoto::cuentaChanged, [acc]() { return acc ? acc->cuenta() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
        {
            Tool t;
            t.name = "acceso_remoto_panel";
            t.title = "Acceso remoto: túneles activos en la cuenta de pinggy";
            t.category = "sala";
            t.readOnly = true;
            t.openWorld = true;
            t.description = "Consulta el panel de pinggy con la API key guardada (acceso_remoto_clave tipo api_key): "
                            "qué túneles tiene activos la cuenta, con qué dominio y puertos. Los secretos que vengan "
                            "en la respuesta salen tapados.";
            t.run = [acc, esperar](const json&, Done done) {
                if (!acc) return done(ToolResult::error("no disponible"));
                acc->verEnPinggy();
                esperar(&AccesoRemoto::panelChanged, [acc]() { return acc ? acc->panel() : QVariantMap{}; }, done);
            };
            core_.addTool(std::move(t));
        }
    }
    {
        Tool t;
        t.name = "proxy_renovar";
        t.title = "Proxy: pedir los certificados ya";
        t.category = "sala";
        t.description = "Pide ya a Let's Encrypt el certificado de `host` (o de todos), aunque no esté por vencer. "
                        "Tarda; el resultado se ve en proxy_estado (acme.ultimo_error).";
        t.inputSchema = {{"type", "object"}, {"properties", {{"host", {{"type", "string"}}}}}};
        t.run = [post](const json& a, Done done) {
            post("/api/v1/proxy/renovar", json{{"host", a.value("host", std::string())}}, "Pedido: mirá proxy_estado en un rato.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "proxy_configurar";
        t.title = "Proxy: dominio, puertos, servicios y ACME";
        t.category = "sala";
        t.description =
            "Configura el proxy por nombre y REINICIA la sala (corta a los invitados): `activo`, `dominio` (los "
            "servicios quedan en <nombre>.<dominio>: hay que apuntar esos nombres a esta máquina en el DNS), "
            "`puertoTls` (443) y `puertoHttp` (80; en Linux los de menos de 1024 piden permiso), `servicios` "
            "([{nombre, destino, ruta, soloLan}]; sin esto, los de siempre: sala, sesion, presidencia, botonera, "
            "prompter, tablero) y `acme` ({activo, email, pruebas}: Let's Encrypt; `pruebas` usa su entorno de "
            "prueba, sin límites). Lo que no se pasa queda como estaba.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"activo", {{"type", "boolean"}}},
                           {"dominio", {{"type", "string"}}},
                           {"puertoTls", {{"type", "integer"}}},
                           {"puertoHttp", {{"type", "integer"}}},
                           {"servicios", {{"type", "array"}}},
                           {"acme", {{"type", "object"}}}}}};
        t.run = [&rt](const json& a, Done done) {
            json& p = rt.gateway().proxy;
            for (const char* k : {"activo", "dominio", "puertoTls", "puertoHttp", "servicios"})
                if (a.contains(k)) p[k] = a[k];
            if (a.contains("acme") && a["acme"].is_object())
                for (const auto& [k, v] : a["acme"].items()) p["acme"][k] = v;
            if (p.value("activo", false) && p.value("dominio", std::string()).empty())
                return done(ToolResult::error("falta `dominio`"));
            if (!rt.save()) return done(ToolResult::error(rt.lastError()));
            const bool ok = rt.restartGateway();
            done(ToolResult().data(json{{"proxy", p}, {"sala_reiniciada", ok}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "invitado_prueba";
        t.title = "Prueba técnica de un invitado: eco y red";
        t.category = "sala";
        t.description =
            "Con alguien de ADENTRO (programa o fila de oradores): le manda un tono de 1 kHz cortado por su "
            "retorno durante 4 s y mide cuánto vuelve por su micrófono (`eco_db`; más de 10 dB = se escucha el "
            "parlante: auriculares), y lee su red (rtt, pérdida, jitter, semáforo). Mientras dura, el invitado "
            "escucha el tono y el programa más bajo: avisale antes. Devuelve `recomendacion` en castellano.";
        t.inputSchema = {{"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, partPath, client](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            client->post(
                partPath(p, "prueba"), QJsonObject{{"ms", 4000}},
                [done](bool ok, const QJsonObject& reply) {
                    json r = fromQt(reply);
                    r["recomendacion"] = ConferenceViewModel::recomendacionPrueba(reply).toStdString();
                    if (!ok) return done(ToolResult::error(r.value("error", std::string("la sala no pudo hacer la prueba"))));
                    done(ToolResult().data(r));
                },
                15000);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_request";
        t.title = "Pedirle algo a un invitado";
        t.category = "sala";
        t.description =
            "Le pide al navegador del invitado: silenciar el microfono, apagar la camara, dejar de "
            "compartir pantalla o bajar la mano.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"participant", personProp},
              {"what",
               {{"type", "string"},
                {"enum",
                 {"mute-mic", "mute-cam", "unmute-mic", "unmute-cam", "stop-screen", "lower-hand"}}}}}},
            {"required", {"participant", "what"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "request"), json{{"what", a["what"]}}, "Pedido enviado.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_calidad";
        t.title = "Bajar o devolver la calidad de la camara de un invitado";
        t.category = "sala";
        t.description =
            "Cuando la red de un invitado no da (room_status: `red.semaforo` ambar o rojo, perdida, jitter, RTT), "
            "le pide a su navegador que mande menos: `maximo` 720p (techo 1,5 Mbps) o 360p (0,5 Mbps); 1080p "
            "devuelve la calidad completa. No corta nada: baja resolucion y bitrate.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participant", personProp},
                           {"maximo", {{"type", "string"}, {"enum", {"1080p", "720p", "360p"}}}}}},
                         {"required", {"participant", "maximo"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const std::string m = a.value("maximo", std::string("720p"));
            post(partPath(p, "request"), json{{"what", m == "1080p" ? "calidad-max" : "calidad-" + m}},
                 "Pedido enviado: el navegador del invitado ajusta su camara.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "participant_profile";
        t.title = "Datos de una persona";
        t.category = "sala";
        t.description =
            "Cambia nombre, informacion y/o contacto de una persona (los ve la produccion y el tablero).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participant", personProp},
                           {"name", {{"type", "string"}}},
                           {"info", {{"type", "string"}}},
                           {"contact", {{"type", "string"}}}}},
                         {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            json b = json::object();
            for (const char* k : {"name", "info", "contact"})
                if (a.contains(k)) b[k] = a[k];
            post(partPath(p, "profile"), b, "Datos actualizados.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "chat_send";
        t.title = "Escribir en el chat";
        t.category = "sala";
        t.description = "Mensaje del operador a toda la sala o a una persona.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"text", {{"type", "string"}}},
                           {"to", {{"type", "string"}, {"description", "'all' (defecto) o una persona."}}}}},
                         {"required", {"text"}}};
        t.run = [&vm, post](const json& a, Done done) {
            std::string to = a.value("to", std::string("all"));
            if (to != "all") {
                json p;
                std::string err;
                if (!resolveParticipant(roomSnapshot(vm), to, p, err)) return done(ToolResult::error(err));
                to = p["id"];
            }
            post("/api/v1/chat", json{{"text", a["text"]}, {"to", to}}, "Enviado.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "chat_history";
        t.title = "Chat reciente";
        t.category = "sala";
        t.readOnly = true;
        t.description = "Ultimos mensajes del chat (de quien, a quien, texto, hora).";
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
        // chat_wait. Sin esto tendria que consultar chat_history cada tanto,
        // que es caro (cada consulta es un turno) y torpe.
        Tool t;
        t.name = "chat_wait";
        t.title = "Esperar que alguien escriba";
        t.category = "sala";
        t.readOnly = true;
        t.description =
            "Espera hasta `timeoutSec` (max. 120) a que `participant` escriba en el chat de la sala y "
            "devuelve "
            "sus mensajes nuevos (`timedOut` true y lista vacia si no escribio). Para atender a alguien: una "
            "pregunta con chat_send, un chat_wait, y asi.";
        t.inputSchema = {
            {"type", "object"},
            {"required", {"participant"}},
            {"properties",
             {{"participant", {{"type", "string"}, {"description", "id o nombre"}}},
              {"timeoutSec", {{"type", "integer"}, {"minimum", 1}, {"maximum", 120}, {"default", 60}}}}}};
        t.run = [conf, &vm](const json& a, Done done) {
            json p;
            std::string err;
            if (!resolveParticipant(roomSnapshot(vm), a["participant"].get<std::string>(), p, err)) {
                done(ToolResult::error(err));
                return;
            }
            const std::string id = p.value("id", "");
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
        t.title = "Archivos compartidos";
        t.category = "sala";
        t.readOnly = true;
        t.description =
            "Archivos que compartieron los invitados (imagen, video, audio) con id, nombre, quien, tamaño.";
        t.run = [&vm](const json&, Done done) {
            done(ToolResult().data(json{{"shares", roomSnapshot(vm).value("shares", json::array())}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "share_view";
        t.title = "Ver un archivo compartido";
        t.category = "sala";
        t.readOnly = true;
        t.description =
            "Muestra una imagen compartida por un invitado (para revisarla antes de ponerla al aire). "
            "Videos y audios: da sus datos.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}}}, {"maxWidth", {{"type", "integer"}, {"default", 1280}}}}},
            {"required", {"id"}}};
        t.run = [&vm, client](const json& a, Done done) {
            json share;
            for (const auto& s : roomSnapshot(vm).value("shares", json::array()))
                if (s.value("id", "") == a["id"].get<std::string>()) share = s;
            if (share.is_null()) return done(ToolResult::error("No existe ese archivo (share_list)."));
            if (share.value("kind", "") != "image") return done(ToolResult().data(share));
            client->getRaw("/api/v1/shares/" + QString::fromStdString(a["id"]) + "/content",
                           [a, share, done](bool ok, const QByteArray& bytes) {
                               QImage img;
                               if (!ok || !img.loadFromData(bytes))
                                   return done(ToolResult::error("No se pudo leer la imagen."));
                               ToolResult r;
                               r.text(share.value("name", "") + " — de " +
                                      share.value("participantName", ""));
                               addImage(r, img, a, 1280);
                               done(r);
                           });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "share_take";
        t.title = "Poner un archivo compartido en la escena";
        t.category = "sala";
        t.description =
            "Copia el archivo compartido a la carpeta de medios y lo agrega a la escena al aire (imagen, "
            "video o audio).";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [conf, &vm](const json& a, Done done) {
            conf->takeShare(QString::fromStdString(a["id"]));
            vm.refreshModels();
            done(ToolResult::ok("Agregado a la escena al aire (si fallo, la app lo avisa en pantalla)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "share_delete";
        t.title = "Borrar archivo compartido";
        t.category = "sala";
        t.destructive = true;
        t.description = "Borra un archivo compartido del gateway.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"id", {{"type", "string"}}}}}, {"required", {"id"}}};
        t.run = [client](const json& a, Done done) {
            client->del("/api/v1/shares/" + QString::fromStdString(a["id"]),
                        [done](bool ok, const QJsonObject& r) {
                            done(ok ? ToolResult::ok("Borrado.") : ToolResult::error(errorText(fromQt(r))));
                        });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "room_link";
        t.title = "Enlace y QR de ingreso (o del tablero)";
        t.category = "sala";
        t.readOnly = true;
        t.description =
            "Enlace para entrar a la sala (target local = misma red; remote = la dirección pública de pinggy, "
            "Cloudflare o WireGuard, para los de afuera) con su QR como imagen; con withPin el PIN va incluido. "
            "Con board=true, el del tablero de oradores. La imagen la ves solo vos: para que el operador vea el QR "
            "y lo pueda copiar, después llamá a qr_mostrar con la URL.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"target", {{"type", "string"}, {"enum", {"local", "remote"}}, {"default", "local"}}},
              {"withPin", {{"type", "boolean"}, {"default", false}}},
              {"board", {{"type", "boolean"}, {"default", false}}}}}};
        t.run = [client](const json& a, Done done) {
            const QString target = QString::fromStdString(a.value("target", std::string("local")));
            const QString path = a.value("board", false) ? "/api/v1/board-link?target=" + target
                                                         : "/api/v1/qr?target=" + target +
                                                               (a.value("withPin", false) ? "&pin=1" : "");
            client->get(path, [done, target](bool ok, const QJsonObject& r) {
                const json j = fromQt(r);
                if (!ok || !j.value("ok", false))
                    return done(
                        ToolResult::error(j.value("message", std::string("sin enlace: ") + errorText(j))));
                ToolResult res;
                const std::string url = j.value("url", "");
                res.text(url + "\n\n(El operador NO ve esta imagen. Para mostrarle el QR y que lo pueda copiar: "
                               "qr_mostrar {\"texto\": \"" + url + "\", \"descripcion\": \"…\"}." +
                         (target == "local" ? std::string(" Es la dirección de la red local: para alguien de afuera, "
                                                          "room_link {\"target\": \"remote\"}.")
                                            : std::string()) + ")");
                const std::string png = qrPng(j.value("modules", json::array()));
                if (!png.empty()) res.image(png, "image/png");
                done(res);
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "network_status";
        t.title = "Red y acceso de invitados";
        t.category = "sala";
        t.readOnly = true;
        t.description = "Como se llega por RED al aparato: HTTPS, tunel WireGuard, IPs locales, "
                        "enlaces y puertos WebRTC del gateway.";
        t.run = [client](const json&, Done done) {
            client->get("/api/v1/network", [done](bool ok, const QJsonObject& r) {
                done(ok ? ToolResult().data(fromQt(r)) : ToolResult::error("La sala no responde."));
            });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "operator_feed";
        t.title = "Como te ve y oye el lobby";
        t.category = "sala";
        t.description =
            "Elige con que fuente de video y de audio (por nombre) te ven y oyen los que esperan en el "
            "lobby. Vacio = placa de espera / sin voz. Sin argumentos lista las opciones.";
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
            done(ToolResult::ok("Listo: el lobby te ve con '" + conf->operatorVideo().toStdString() +
                                "' y te oye con '" + conf->operatorAudio().toStdString() + "'."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "gateway_restart";
        t.title = "Reiniciar la sala (gateway)";
        t.category = "sala";
        t.destructive = true;
        t.description =
            "Reinicia el proceso de la sala: se cortan todos (se reconectan solos en segundos si el "
            "estado se puede recuperar). Solo si algo no responde.";
        t.run = [&rt](const json&, Done done) {
            done(rt.restartGateway() ? ToolResult::ok("Gateway reiniciandose.")
                                     : ToolResult::error(rt.lastError()));
        };
        core_.addTool(std::move(t));
    }

    // --- Oradores ----------------------------------------------------------------
    {
        Tool t;
        t.name = "speakers_status";
        t.title = "Fila de oradores";
        t.category = "oradores";
        t.readOnly = true;
        t.description =
            "Configuracion del modo oradores y la fila en orden: estado (queued/onair/done), tiempo "
            "asignado, cuanto le queda al que esta al aire (negativo = se paso), cuanto uso cada uno, "
            "conexion, info y contacto.";
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
        t.title = "Configurar el modo oradores";
        t.category = "oradores";
        t.description =
            "Activa/desactiva el modo oradores y define: tiempo general por orador, al aire uno a la vez "
            "o varios, slot por defecto, carrusel (manual; chain = al sacar entra el siguiente; time = "
            "rota solo pasado el tiempo + margen) y margen.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"enabled", {{"type", "boolean"}}},
              {"defaultTime",
               {{"type", json::array({"string", "integer"})}, {"description", "Segundos o 'm:ss'."}}},
              {"multi", {{"type", "boolean"}}},
              {"defaultSlot", {{"type", "integer"}, {"minimum", 1}}},
              {"carousel", {{"type", "string"}, {"enum", {"manual", "chain", "time"}}}},
              {"graceSec", {{"type", "integer"}, {"minimum", 0}, {"maximum", 600}}}}}};
        t.run = [post](const json& a, Done done) {
            json b = json::object();
            for (const char* k : {"enabled", "multi", "defaultSlot", "carousel", "graceSec"})
                if (a.contains(k)) b[k] = a[k];
            if (a.contains("defaultTime")) {
                const int s = parseSeconds(a["defaultTime"]);
                if (s <= 0) return done(ToolResult::error("defaultTime invalido (segundos o 'm:ss')."));
                b["defaultSec"] = s;
            }
            post("/api/v1/speakers", b, "Modo oradores actualizado.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_on_air";
        t.title = "Orador al aire / fuera";
        t.category = "oradores";
        t.description =
            "Pone al aire a un orador (on=true, arranca su reloj) o lo saca (on=false: vuelve a la fila "
            "como 'hablo', nunca se lo echa).";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"participant", personProp}, {"on", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const bool on = a.value("on", true);
            post(partPath(p, "onair"), json{{"on", on}},
                 p.value("name", "") + (on ? " al aire." : " fuera del aire."), done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_next";
        t.title = "Siguiente orador";
        t.category = "oradores";
        t.description = "Pasa al orador que SIGUE en la fila. Pone al aire al siguiente de la fila (en modo "
                        "'uno a la vez', el que estaba sale).";
        t.run = [post](const json&, Done done) {
            post("/api/v1/speakers/next", json::object(), "Siguiente al aire.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_move";
        t.title = "Reordenar la fila";
        t.category = "oradores";
        t.description = "Mueve a una persona a una posicion de la fila (1 = primera).";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"participant", personProp}, {"position", {{"type", "integer"}, {"minimum", 1}}}}},
                         {"required", {"participant", "position"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "move"), json{{"index", a["position"].get<int>() - 1}}, "Fila reordenada.",
                 done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_queue";
        t.title = "Mandar a la fila de oradores";
        t.category = "oradores";
        t.description =
            "QUE ES: pone en la FILA de oradores a alguien que ya esta en la sala (o devuelve a la fila a "
            "quien ya hablo). PARA QUE: en modo oradores todos siguen en la sala y la fila es solo el "
            "orden en que se los saca al aire. COMO SE USA: `participant` (nombre o id). Sale del aire "
            "hasta que le toque; `speaker_on_air` o `speaker_next` lo suben. Si era presentador, deja de "
            "serlo.";
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, "requeue"), json::object(), "A la fila de oradores.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_presenter";
        t.title = "Presentador fijo";
        t.category = "oradores";
        t.description =
            "QUE ES: marca a una persona como PRESENTADOR: se queda al aire con su propio lugar mientras "
            "los oradores rotan a su lado. PARA QUE: un conductor remoto fijo con invitados que entran y "
            "salen. COMO SE USA: `participant` (nombre o id) y `on` (true/false). Hay uno solo a la vez; "
            "no entra en la fila y la rotacion no lo saca. Con `on=false` deja de serlo y sale del aire "
            "(despues se lo puede mandar a la fila con `speaker_queue`).";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"participant", personProp}, {"on", {{"type", "boolean"}}}}},
                         {"required", {"participant"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const bool on = a.value("on", true);
            post(partPath(p, "presenter"), json{{"on", on}},
                 on ? "Presentador fijo." : "Ya no es presentador.", done);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "speaker_time";
        t.title = "Tiempo de un orador";
        t.category = "oradores";
        t.description = "Tiempo asignado para hablar de una persona (segundos o 'm:ss'). Se puede cambiar "
                        "estando al aire.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"participant", personProp}, {"time", {{"type", json::array({"string", "integer"})}}}}},
            {"required", {"participant", "time"}}};
        t.run = [withPerson, partPath, post](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            const int s = parseSeconds(a["time"]);
            if (s <= 0) return done(ToolResult::error("Tiempo invalido (segundos o 'm:ss')."));
            post(partPath(p, "time"), json{{"sec", s}}, "Tiempo: " + std::to_string(s) + " s.", done);
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
