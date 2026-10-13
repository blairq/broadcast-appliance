// src/app/sala/ControlSala.cpp — la sala en sí (ver el .hpp)

#include "sala/ControlSala.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/AppSettings.hpp"
#include "sala/ReglasSala.hpp"
#include "ui/ConferenceClient.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <filesystem>
#include <system_error>

namespace ba::app::sala {

namespace {

std::string recortado(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

/// El archivo compartido `id`, como lo da la sala (vacío si ya no está).
QJsonObject archivoCompartido(const ConferenceClient& sala, const std::string& id) {
    const QString cual = QString::fromStdString(id);
    for (const QJsonValue& v : sala.snapshot().value("shares").toArray())
        if (v.toObject().value("id").toString() == cual) return v.toObject();
    return {};
}

std::string enRuta(const std::string& id) {
    return QUrl::toPercentEncoding(QString::fromStdString(id)).toStdString();
}

} // namespace

ControlSala::ControlSala(AppRuntime& rt, ConferenceClient& sala) : rt_(rt), sala_(sala) {
    // La sala arranca después que el programa (y se reinicia sola): cada vez
    // que conecta se le dice si el asistente atiende el lobby. Con el asistente
    // atendiendo, la pantalla de ingreso del invitado no le pide los datos.
    alConectar_ = QObject::connect(&sala_, &ConferenceClient::onlineChanged, &sala_, [this](bool on) {
        if (on) pedir(sala_, "POST", "/api/v1/room", {{"intake", atencionAutomatica()}}, {});
    });
}

ControlSala::~ControlSala() { QObject::disconnect(alConectar_); }

std::string ControlSala::entrada(std::optional<bool> lobby, std::optional<bool> cerrada, Listo listo) {
    nlohmann::json b = nlohmann::json::object();
    if (lobby) b["lobby"] = *lobby;
    if (cerrada) b["locked"] = *cerrada;
    if (b.empty()) return "no hay nada para cambiar";
    pedir(sala_, "POST", "/api/v1/room", b, std::move(listo));
    return {};
}

bool ControlSala::atencionAutomatica() const { return rt_.gateway().autoIntake; }

void ControlSala::setAtencionAutomatica(bool on) {
    // Es una decisión del evento, no un botón que haya que volver a apretar:
    // queda guardada y sigue al arrancar.
    rt_.gateway().autoIntake = on;
    rt_.save();
    pedir(sala_, "POST", "/api/v1/room", {{"intake", on}}, {});
}

void ControlSala::cambiarPin(const std::string& pin, Listo listo) {
    nlohmann::json b = nlohmann::json::object();
    if (const std::string p = recortado(pin); !p.empty()) b["pin"] = p;
    pedir(sala_, "POST", "/api/v1/pin", b, std::move(listo));
}

void ControlSala::verBloqueosPin(Listo listo) { pedir(sala_, "GET", "/api/v1/pin/bloqueos", {}, std::move(listo)); }

void ControlSala::desbloquearPin(const std::string& ip, Listo listo) {
    nlohmann::json b = nlohmann::json::object();
    if (const std::string i = recortado(ip); !i.empty()) b["ip"] = i;
    pedir(sala_, "POST", "/api/v1/pin/desbloquear", b, std::move(listo));
}

std::string ControlSala::chatear(const std::string& para, const std::string& texto, Listo listo) {
    if (recortado(texto).empty()) return "el mensaje está vacío";
    pedir(sala_, "POST", "/api/v1/chat", {{"to", para.empty() ? std::string("all") : para}, {"text", texto}},
          std::move(listo));
    return {};
}

void ControlSala::borrarArchivo(const std::string& id, Listo listo) {
    pedir(sala_, "DELETE", "/api/v1/shares/" + enRuta(id), {}, std::move(listo));
}

void ControlSala::verArchivo(const std::string& id, Contenido listo) {
    const QJsonObject a = archivoCompartido(sala_, id);
    if (a.isEmpty()) {
        if (listo) listo("Ese archivo ya no está.", nlohmann::json::object(), {});
        return;
    }
    nlohmann::json datos =
        nlohmann::json::parse(QJsonDocument(a).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    if (!datos.is_object()) datos = nlohmann::json::object();
    if (a.value("kind").toString() != "image") {
        if (listo) listo({}, datos, {});
        return;
    }
    sala_.getRaw(QString::fromStdString("/api/v1/shares/" + enRuta(id) + "/content"),
                 [listo, datos](bool ok, const QByteArray& bytes) {
                     if (!listo) return;
                     if (!ok || bytes.isEmpty()) return listo("No se pudo leer el archivo.", datos, {});
                     listo({}, datos, bytes);
                 });
}

ControlSala::Tomado ControlSala::tomarArchivo(const std::string& id) {
    namespace fs = std::filesystem;
    Tomado t;
    const QJsonObject a = archivoCompartido(sala_, id);
    if (a.isEmpty()) {
        t.error = "Ese archivo ya no está.";
        return t;
    }
    // Copia propia: la carpeta de la sala se limpia al reiniciarla, y la
    // escena no debe depender de eso.
    const QString shown = a.value("name").toString();
    t.nombre = shown.toStdString();
    const fs::path src(a.value("path").toString().toStdU16String());
    const fs::path mediaDir = rt_.dataRoot() / "Media";
    std::error_code ec;
    fs::create_directories(mediaDir, ec);
    QString stem = QString::fromStdU16String(fs::path(shown.toStdU16String()).stem().u16string());
    stem.replace(QRegularExpression("[^\\w\\- ]"), "_");
    if (stem.isEmpty()) stem = "archivo";
    fs::path dst = mediaDir / (stem.toStdU16String() + src.extension().u16string());
    for (int n = 2; fs::exists(dst, ec); ++n)
        dst = mediaDir / ((stem + QString(" (%1)").arg(n)).toStdU16String() + src.extension().u16string());
    fs::copy_file(src, dst, ec);
    if (ec) {
        t.error = "No se pudo copiar el archivo: " + ec.message();
        return t;
    }

    const QString kind = a.value("kind").toString();
    const QString file = QString::fromStdU16String(dst.u16string());
    SourceSpec spec;
    QJsonObject settings;
    if (kind == "image") {
        spec.id = "image_source";
        settings.insert("file", file);
    } else {
        spec.id = "ffmpeg_source";
        settings.insert("local_file", file);
        settings.insert("is_local_file", true);
        settings.insert("looping", kind == "video");
        settings.insert("restart_on_activate", true);
    }
    spec.settingsJson = QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString();
    t.escena = rt_.engine().currentScene();
    const QString base = a.value("participantName").toString() + " — " + shown;
    QString name = base;
    for (int n = 2; rt_.engine().sourceExists(name.toStdString()); ++n)
        name = base + QString(" (%1)").arg(n);
    spec.name = name.toStdString();
    if (!rt_.addSource(t.escena, spec)) {
        t.error = "No se pudo agregar la fuente: " + rt_.lastError();
        return t;
    }
    rt_.save();
    t.fuente = spec.name;
    if (alTomarArchivo_) alTomarArchivo_();
    return t;
}

void ControlSala::codigoConsolaWeb(Listo listo) {
    pedir(sala_, "POST", "/api/v1/login-code", nlohmann::json::object(), std::move(listo));
}

void ControlSala::verProxy(Listo listo) {
    pedir(sala_, "GET", "/api/v1/proxy", {}, [this, listo](const std::string& err, const nlohmann::json& r) {
        nlohmann::json out = r.is_object() ? r : nlohmann::json::object();
        out["configuracion"] = rt_.gateway().proxy;
        if (listo) listo(err, out);
    });
}

void ControlSala::renovarProxy(const std::string& host, Listo listo) {
    pedir(sala_, "POST", "/api/v1/proxy/renovar", {{"host", host}}, std::move(listo));
}

std::string ControlSala::configurarProxy(const nlohmann::json& cambios, Reiniciada reiniciada) {
    nlohmann::json nueva;
    if (std::string e = mezclarProxy(rt_.gateway().proxy, cambios, nueva); !e.empty()) return e;
    rt_.gateway().proxy = nueva;
    if (!rt_.save()) return rt_.lastError();
    reiniciar(std::move(reiniciada));
    return {};
}

void ControlSala::verRed(Listo listo) { pedir(sala_, "GET", "/api/v1/network", {}, std::move(listo)); }

void ControlSala::publicarRotulos(const nlohmann::json& estado, Listo listo) {
    pedir(sala_, "POST", "/api/v1/rotulos", {{"estado", estado}, {"tomar", true}}, std::move(listo));
}

void ControlSala::publicarEventoWeb(const nlohmann::json& estado) {
    pedir(sala_, "POST", "/api/v1/evento-web", estado, {});
}

void ControlSala::acreditaciones(Listo listo) { pedir(sala_, "GET", "/api/v1/acreditaciones", {}, std::move(listo)); }

void ControlSala::nivelLog(Listo listo) { pedir(sala_, "GET", "/api/v1/log-nivel", {}, std::move(listo)); }

void ControlSala::setNivelLog(const std::string& nivel, Listo listo) {
    pedir(sala_, "POST", "/api/v1/log-nivel", {{"nivel", nivel}}, std::move(listo));
}

std::string ControlSala::accesoDirecto(const std::string& endpoint, const std::string& direccionPublica,
                                       const std::string& interfaz, Listo listo, Reiniciada reiniciada) {
    const std::string ep = recortado(endpoint);
    if (ep.empty()) return "Falta la direccion del servidor (host:puerto)";
    const std::string iface = recortado(interfaz);
    pedir(sala_, "POST", "/api/v1/network/server-config", {{"endpoint", ep}, {"interface", iface.empty() ? "eth0" : iface}},
          [this, ep, direccionPublica, listo, reiniciada](const std::string& err, const nlohmann::json& r) {
              if (!err.empty()) {
                  if (listo) listo("El gateway no pudo generar la configuracion: " + err, r);
                  return;
              }
              // Este equipo queda apuntando a ese servidor, con el túnel prendido.
              GatewaySettings& g = rt_.gateway();
              g.wgEnabled = true;
              g.wgEndpoint = r.value("endpoint", std::string());
              g.wgServerPublicKey = r.value("serverPublicKey", std::string());
              std::string pub = recortado(direccionPublica);
              if (pub.empty()) pub = ep.substr(0, ep.find(':')); // por defecto, el mismo host
              g.wgPublicAddress = pub;
              rt_.save();
              if (listo) listo({}, r);
              reiniciar(reiniciada);
          });
    return {};
}

void ControlSala::reiniciar(Reiniciada reiniciada) {
    // `listo` de reiniciarSalaEnElFondo corre en otro hilo: se vuelve al de la
    // interfaz antes de avisar.
    rt_.reiniciarSalaEnElFondo([reiniciada](const std::string& e) {
        if (!reiniciada) return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [reiniciada, e]() { reiniciada(e); },
                                  Qt::QueuedConnection);
    });
}

} // namespace ba::app::sala
