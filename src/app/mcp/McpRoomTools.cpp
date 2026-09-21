// ============================================================================
// src/app/mcp/McpRoomTools.cpp — Sala de videoconferencia y modo oradores
//
// La sala vive en el Proceso B; estas herramientas usan la MISMA API de
// control que la vista Qt y la consola web (ConferenceClient). El estado se lee
// del snapshot vigente (llega por eventos, sin pedirlo) y las acciones son
// POST/DELETE a B.
// ============================================================================

#include <QBuffer>
#include <QJsonDocument>
#include <QPainter>
#include <QUrl>

#include <algorithm>
#include <cctype>
#include <memory>

#include "AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
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
            "Lobby de admision (true = cada invitado espera que lo hagas pasar) y sala cerrada (nadie "
            "nuevo puede entrar).";
        t.inputSchema = {
            {"type", "object"},
            {"properties", {{"lobby", {{"type", "boolean"}}}, {"locked", {{"type", "boolean"}}}}}};
        t.run = [post](const json& a, Done done) {
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
    // Acciones sobre una persona.
    struct Act {
        const char* name;
        const char* title;
        const char* action;
        const char* desc;
        bool destructive;
    };
    static const Act kActs[] = {
        {"participant_admit", "Admitir", "admit",
         "Hace pasar a alguien del lobby (en modo oradores, a la fila; si no, a un lugar del programa).",
         false},
        {"participant_reject", "Rechazar", "reject", "Rechaza a alguien del lobby (se le avisa y se corta).",
         true},
        {"participant_kick", "Sacar de la sala", "kick",
         "Saca a alguien de la sala (se le avisa y se corta).", true},
        {"participant_requeue", "Volver a la fila", "requeue",
         "Orador que ya hablo: vuelve al final de la fila.", false}};
    for (const Act& act : kActs) {
        Tool t;
        t.name = act.name;
        t.title = act.title;
        t.category = std::string(act.action) == "requeue" ? "oradores" : "sala";
        t.destructive = act.destructive;
        t.description = act.desc;
        t.inputSchema = {
            {"type", "object"}, {"properties", {{"participant", personProp}}}, {"required", {"participant"}}};
        const std::string action = act.action;
        t.run = [withPerson, partPath, post, action](const json& a, Done done) {
            json p;
            if (!withPerson(a, "participant", p, done)) return;
            post(partPath(p, action.c_str()), json::object(), "Hecho: " + action + " " + p.value("name", ""),
                 done);
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
        t.title = "Hablar en privado (lobby)";
        t.category = "sala";
        t.description =
            "Con alguien del lobby: on=true le llega tu voz y vos lo escuchas por monitoreo (nadie mas); "
            "on=false corta.";
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
               {{"type", "string"}, {"enum", {"mute-mic", "mute-cam", "stop-screen", "lower-hand"}}}}}},
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
            "Enlace para entrar a la sala (target local = misma red, remote = dominio publico) con su "
            "QR como imagen; con withPin el PIN va incluido. Con board=true, el del tablero de oradores.";
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
            client->get(path, [done](bool ok, const QJsonObject& r) {
                const json j = fromQt(r);
                if (!ok || !j.value("ok", false))
                    return done(
                        ToolResult::error(j.value("message", std::string("sin enlace: ") + errorText(j))));
                ToolResult res;
                res.text(j.value("url", ""));
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
        t.description = "HTTPS, tunel WireGuard, IPs locales, enlaces y puertos WebRTC del gateway.";
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
        t.description = "Pone al aire al siguiente de la fila (en modo 'uno a la vez', el que estaba sale).";
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
