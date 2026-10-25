// ============================================================================
// src/app/dispositivos/Registro.cpp — qué se busca y qué se le puede hacer
//
// Acá se enchufan los detectores y las acciones de cada tipo de equipo. Para
// sumar una acción: un `Accion{...}` más con sus parámetros. El asistente
// (equipo_accion) y la interfaz (página Dispositivos y equipos) la ven solos.
// ============================================================================

#include "dispositivos/Registro.hpp"

#include "arranque/AppRuntime.hpp"
#include "audio/EntradasAudio.hpp"
#include "audio/UnificarEntradas.hpp"
#include "dispositivos/Dispositivos.hpp"
#include "dispositivos/Onvif.hpp"
#include "plugins/Plugins.hpp"
#include "ptz/ControlCamaras.hpp"
#include "red/Red.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "video/Camaras.hpp"
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"
#if defined(BA_HAVE_NDI)
#include "emision/NdiSource.hpp"
#endif

#include <QAudioDevice>
#include <QAuthenticator>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <thread>

namespace ba::app::dispositivos {

namespace {
using json = nlohmann::json;

QVariant aV(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .toVariant()
        .toMap()
        .value("v");
}
json aJ(const QVariant& v) {
    return json::parse(
        QJsonDocument::fromVariant(QVariantMap{{"v", v}}).toJson(QJsonDocument::Compact).toStdString(),
        nullptr, false)["v"];
}
QString ipDe(const QVariantMap& e) { return e.value("datos").toMap().value("ip").toString(); }
QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Un equipo de la red (del escaneo de red::Red) como equipo del catálogo.
QVariantMap equipoDeRed(const json& e) {
    const std::string ip = e.value("ip", std::string());
    const bool camara = !e.value("onvif", json::object()).empty() ||
                        !e.value("rtsp", json::array()).empty() ||
                        e.value("marca", json::object()).value("tipo", std::string()) == "camara";
    std::string nombre = e.value("onvif", json::object()).value("nombre", std::string());
    if (nombre.empty()) nombre = e.value("marca", json::object()).value("nombre", std::string());
    if (nombre.empty()) nombre = e.value("fabricante", std::string("Equipo"));
    QString detalle = QString::fromStdString(ip);
    if (!e.value("mac", std::string()).empty())
        detalle += " · " + QString::fromStdString(e.value("mac", std::string()));
    if (!e.value("fabricante", std::string()).empty())
        detalle += " · " + QString::fromStdString(e.value("fabricante", std::string()));
    if (!e.value("siguiente_paso", std::string()).empty())
        detalle += " — " + QString::fromStdString(e.value("siguiente_paso", std::string()));
    return {{"clave", QString::fromStdString(ip)},
            {"valor", QString::fromStdString(ip)},
            {"tipo", camara ? "camara_red" : "equipo_red"},
            {"nombre", QString::fromStdString(nombre + " " + ip)},
            {"detalle", detalle},
            {"datos", aV(e)}};
}
/// Las cámaras, los micrófonos y las salidas de audio de la máquina, cada uno
/// con su id ESTABLE (el mismo que guarda una entrada: uniqueID de AVFoundation,
/// ruta del dispositivo, UID de Core Audio, id de WASAPI/Pulse), nunca el índice:
/// el nombre que le pone el operador queda pegado a ese equipo aunque se lo
/// reenchufe en otro puerto.
QVariantList localesDe(AppRuntime& rt) {
    video::iniciarGStreamer();
    QVariantList eq;
    for (const auto& c : video::camarasDelEquipo(rt.engine()))
        eq << QVariantMap{{"clave", "video:" + qs(c.value)},
                          {"tipo", "camara_local"},
                          {"nombre", qs(c.name)},
                          {"valor", qs(c.value)},
                          {"detalle", "Cámara o capturadora · " + qs(c.value)},
                          {"datos", QVariantMap{{"id", qs(c.value)}}}};
    for (const auto& m : audio::entradasDeAudio(rt.engine())) {
        const bool pred = m.value == "default";
        // Lo que suena en una salida (loopback, Windows): se ofrece como entrada,
        // pero no es un micrófono y se dice.
        const bool loopback = m.value.rfind(audio::kPrefijoLoopback, 0) == 0;
        eq << QVariantMap{{"clave", "audio:" + qs(m.value)},
                          {"tipo", "microfono"},
                          {"nombre", qs(m.name)},
                          {"valor", qs(m.value)},
                          {"detalle", pred       ? QStringLiteral("La que tenga elegida el sistema (la sigue si cambia)")
                                      : loopback ? "Lo que se reproduce por esa salida (no es un micrófono) · " + qs(m.value)
                                                 : "Micrófono o entrada de audio · " + qs(m.value)},
                          {"datos", QVariantMap{{"id", qs(m.value)},
                                                {"pipeline", qs(audio::pipelineDeMicrofono(m.value))}}}};
    }
    // Las mismas que ofrece el mezclador para escuchar y para las salidas (QMediaDevices).
    for (const QAudioDevice& s : QMediaDevices::audioOutputs()) {
        const QString id = QString::fromLatin1(s.id());
        eq << QVariantMap{{"clave", "salida:" + id},
                          {"tipo", "salida_audio"},
                          {"nombre", s.description()},
                          {"valor", id},
                          {"detalle", "Salida de audio (placa de sonido, auriculares, HDMI) · " + id},
                          {"datos", QVariantMap{{"id", id}, {"predeterminada", s.isDefault()}}}};
    }
    return eq;
}
} // namespace

void registrarTodo(Dispositivos& d, const Dependencias& dep) {
    QPointer<red::Red> red = dep.red;
    QPointer<plugins::Plugins> pl = dep.plugins;
    QPointer<video::VideoGrafos> video = dep.video;
    QPointer<ptz::ControlCamaras> camaras = dep.camaras;
    QPointer<OperatorMessages> mensajes = dep.mensajes;
    QPointer<InputsViewModel> entradas = dep.entradas;
    QPointer<Dispositivos> yo(&d);

    // --- Detectores ---------------------------------------------------------------------------
    if (red) {
        d.registrar(Detector{
            "red", "Red local",
            "Cámaras IP, grabadores y equipos de video de la subred: ONVIF, puertos, MAC/fabricante, URLs "
            "RTSP "
            "típicas probadas (unos 10 s).",
            [red](Listo listo) {
                if (!red) return listo({{"error", "el escáner de red no está disponible"}});
                red->escanear([listo](json r) {
                    if (r.contains("error"))
                        return listo({{"error", QString::fromStdString(r.value("error", std::string()))}});
                    QVariantList eq;
                    for (const auto& e : r.value("equipos", json::array()))
                        eq << equipoDeRed(e);
                    listo({{"equipos", eq}});
                });
            }});
        // Lo del último escaneo (si la app ya escaneó), sin volver a escanear.
        for (const QVariant& v : red->equiposQml())
            d.anotar("red", equipoDeRed(aJ(v)));
    }
    if (dep.rt) {
        AppRuntime* rt = dep.rt;
        d.registrar(Detector{
            "local", "Cámaras, micrófonos y placas de sonido de esta máquina",
            "Las cámaras y capturadoras, los micrófonos y entradas de audio y las salidas de las placas de "
            "sonido, con su id estable. Se vuelve a buscar solo cuando se enchufa o se desenchufa algo.",
            [rt](Listo listo) { listo({{"equipos", localesDe(*rt)}}); }});
        // Enchufar o desenchufar algo se ve solo (conectado / ausente), sin tocar
        // «Buscar»: el sistema avisa y se busca de nuevo (con una espera corta,
        // porque un equipo USB llega en varios avisos seguidos).
        auto* md = new QMediaDevices(&d);
        auto* espera = new QTimer(&d);
        espera->setSingleShot(true);
        espera->setInterval(800);
        QObject::connect(espera, &QTimer::timeout, &d, [yo]() {
            if (yo) yo->escanear("local", [](QVariantMap) {});
        });
        QObject::connect(md, &QMediaDevices::audioInputsChanged, espera, qOverload<>(&QTimer::start));
        QObject::connect(md, &QMediaDevices::audioOutputsChanged, espera, qOverload<>(&QTimer::start));
        QObject::connect(md, &QMediaDevices::videoInputsChanged, espera, qOverload<>(&QTimer::start));
        // Una búsqueda al arrancar (fuera del arranque mismo): las entradas, el
        // mezclador y esta página muestran los nombres y lo que falta desde el principio.
        QTimer::singleShot(2000, &d, [yo]() {
            if (yo) yo->escanear("local", [](QVariantMap) {});
        });
    }
#if defined(BA_HAVE_NDI)
    d.registrar(Detector{"ndi", "NDI® en la red", "Los emisores NDI que se anuncian en la red (unos 4 s).",
                         [](Listo listo) {
                             std::thread([listo]() {
                                 const auto found = ndi::discover(4000);
                                 QVariantList eq;
                                 for (const auto& f : found)
                                     eq << QVariantMap{{"clave", qs(f.name)},
                                                       {"tipo", "ndi"},
                                                       {"nombre", qs(f.name)},
                                                       {"valor", qs(f.name)},
                                                       {"detalle", QString("NDI · %1:%2").arg(qs(f.host)).arg(f.port)},
                                                       {"datos", QVariantMap{{"host", qs(f.host)}, {"puerto", f.port}}}};
                                 QMetaObject::invokeMethod(
                                     qApp, [eq, listo]() { listo({{"equipos", eq}}); }, Qt::QueuedConnection);
                             }).detach();
                         }});
#endif
    if (pl) {
        d.registrar(Detector{
            "usb", "USB, HID y serie",
            "Los equipos conectados por USB (vid:pid, marca, si son cámaras UVC), las interfaces HID y los "
            "puertos "
            "serie: por dónde se le habla a un equipo raro (la primera vez baja pyusb/hidapi/pyserial).",
            [pl](Listo listo) {
                if (!pl) return listo({{"error", "los plugins no están disponibles"}});
                pl->dispositivosPython(json::array(), [listo](json r) {
                    QVariantList eq;
                    for (const auto& u : r.value("usb", json::array()))
                        eq << QVariantMap{
                            {"clave", QString::fromStdString(u.value("vid", "") + ":" + u.value("pid", "") +
                                                             ":" + std::to_string(u.value("bus", 0)) + "." +
                                                             std::to_string(u.value("direccion", 0)))},
                            {"tipo", "usb"},
                            {"nombre",
                             QString::fromStdString(u.value("fabricante", "") + " " + u.value("producto", ""))
                                 .trimmed()},
                            {"detalle",
                             QString::fromStdString("USB " + u.value("vid", "") + ":" + u.value("pid", "") +
                                                    (u.value("uvc", false) ? " · cámara UVC" : ""))},
                            {"datos", aV(u)}};
                    for (const auto& h : r.value("hid", json::array()))
                        eq << QVariantMap{{"clave", QString::fromStdString(h.value("ruta", std::string()))},
                                          {"tipo", "hid"},
                                          {"nombre", QString::fromStdString(h.value("fabricante", "") + " " +
                                                                            h.value("producto", ""))
                                                         .trimmed()},
                                          {"detalle", QString::fromStdString("HID " + h.value("vid", "") +
                                                                             ":" + h.value("pid", ""))},
                                          {"datos", aV(h)}};
                    for (const auto& p : r.value("serie", json::array()))
                        eq << QVariantMap{
                            {"clave", QString::fromStdString(p.value("puerto", std::string()))},
                            {"tipo", "serie"},
                            {"nombre", QString::fromStdString(p.value("puerto", std::string()))},
                            {"detalle", QString::fromStdString(p.value("descripcion", std::string()))},
                            {"datos", aV(p)}};
                    QVariantMap out{{"equipos", eq}};
                    if (r.contains("usb_error"))
                        out["aviso"] = QString::fromStdString(r.value("usb_error", std::string()));
                    listo(out);
                });
            }});
    }

    // --- Cámaras de la red ----------------------------------------------------------------------
    if (red) {
        d.registrar(
            "camara_red",
            Accion{"pedir_clave",
                   "Pedir usuario y clave",
                   "Muestra al operador el formulario seguro de la barra (se guarda cifrada; nadie la ve).",
                   {},
                   true,
                   {},
                   [mensajes, red](const QVariantMap& e, const QVariantMap&, Listo listo) {
                       if (!mensajes || !red) return listo({{"error", "no hay barra del operador"}});
                       const QString ip = ipDe(e);
                       const json eq = red->equipo(ip.toStdString());
                       const int n = mensajes->post(
                           {{"kind", "credential"},
                            {"platform", "camara:" + ip.toStdString()},
                            {"priority", "info"},
                            {"title", "Usuario y clave de " + e.value("nombre").toString().toStdString()},
                            {"text", "Se guarda cifrado en este equipo."},
                            {"fields",
                             json::array(
                                 {{{"key", "usuario"},
                                   {"label", "Usuario"},
                                   {"secret", false},
                                   {"placeholder", eq.value("usuario_de_fabrica", std::string("admin"))}},
                                  {{"key", "clave"}, {"label", "Clave"}, {"secret", true}}})}});
                       listo({{"texto", QString("Formulario %1 en la barra del operador.").arg(n)}});
                   }});
        d.registrar(
            "camara_red",
            Accion{"clave",
                   "Cargar usuario y clave",
                   "Guarda el usuario y la clave de la cámara (cifrados). Solo desde la interfaz: el "
                   "asistente usa «pedir_clave».",
                   {Parametro{"usuario", "Usuario", "texto", {}, true},
                    Parametro{"clave", "Clave", "clave", {}, true}},
                   true,
                   {},
                   [red](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                       if (!red) return listo({{"error", "el escáner no está disponible"}});
                       const QString err = red->guardarCredenciales(ipDe(e), a.value("usuario").toString(),
                                                                    a.value("clave").toString());
                       listo(err.isEmpty()
                                 ? QVariantMap{{"texto",
                                                "Guardada (cifrada). Ahora «Ficha» o «Funciones ONVIF»."}}
                                 : QVariantMap{{"error", err}});
                   }});
        d.registrar("camara_red",
                    Accion{"ficha",
                           "Ficha y plan",
                           "ONVIF completo (equipo, perfiles, PTZ, imagen) + análisis del stream con "
                           "GStreamer + cómo conectarla.",
                           {},
                           false,
                           {},
                           [red](const QVariantMap& e, const QVariantMap&, Listo listo) {
                               if (!red) return listo({{"error", "el escáner no está disponible"}});
                               red->ficha(ipDe(e).toStdString(), [listo](json f) {
                                   if (f.contains("error") && !f.value("onvif", false) && !f.contains("plan"))
                                       return listo({{"error", QString::fromStdString(
                                                                   f.value("error", std::string()))},
                                                     {"datos", aV(f)}});
                                   listo({{"texto", "Ficha lista"}, {"datos", aV(f)}});
                               });
                           }});
        d.registrar("camara_red", Accion{"funciones_onvif",
                                         "Funciones ONVIF",
                                         "Todo lo que la cámara dice que puede, por protocolo: ejes PTZ y "
                                         "cómo se mueven (continuo, absoluto, relativo), "
                                         "límites, foco, home, presets, comandos auxiliares, imagen; y su "
                                         "posición actual. Con las respuestas crudas.",
                                         {},
                                         false,
                                         {},
                                         [red, yo](const QVariantMap& e, const QVariantMap&, Listo listo) {
                                             if (!red)
                                                 return listo({{"error", "el escáner no está disponible"}});
                                             QString u, c;
                                             red->clave(ipDe(e).toStdString(), u, c);
                                             auto* cli = new ClienteOnvif(ipDe(e), u, c, yo);
                                             cli->funciones([cli, listo](QVariantMap r) {
                                                 cli->deleteLater();
                                                 if (r.contains("error")) return listo(r);
                                                 listo({{"texto", "Funciones mapeadas"}, {"datos", r}});
                                             });
                                         }});
        d.registrar(
            "camara_red",
            Accion{"onvif",
                   "Llamar una operación ONVIF",
                   "Para explorar: manda `cuerpo` (el XML del Body, p. ej. "
                   "<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>) "
                   "al `servicio`, con la clave guardada. {perfil}, {config_ptz} y {fuente} se reemplazan "
                   "por los de la cámara. Devuelve la respuesta aplanada.",
                   {Parametro{"servicio",
                              "Servicio",
                              "opcion",
                              {"ptz", "media", "imaging", "device"},
                              true,
                              {},
                              "ptz"},
                    Parametro{"cuerpo", "Cuerpo (XML del Body)", "texto", {}, true}},
                   true,
                   {},
                   [red, yo](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                       if (!red) return listo({{"error", "el escáner no está disponible"}});
                       QString u, c;
                       red->clave(ipDe(e).toStdString(), u, c);
                       auto* cli = new ClienteOnvif(ipDe(e), u, c, yo);
                       cli->llamar(
                           a.value("servicio", "ptz").toString(), a.value("cuerpo").toString(),
                           [cli, listo](QVariantMap r) {
                               cli->deleteLater();
                               if (r.contains("error")) return listo(r);
                               listo({{"texto", r.value("ok").toBool()
                                                    ? "OK"
                                                    : "La cámara contestó: " + r.value("falla").toString()},
                                      {"datos", r}});
                           });
                   }});
        d.registrar(
            "camara_red",
            Accion{"http",
                   "Pedido HTTP a la cámara",
                   "Para la API propia de la marca (Axis VAPIX: /axis-cgi/com/ptz.cgi?query=limits; "
                   "Hikvision ISAPI: "
                   "/ISAPI/PTZCtrl/channels/1/status…), con el usuario y la clave guardados (Basic o "
                   "Digest). Devuelve el texto.",
                   {Parametro{"ruta", "Ruta", "texto", {}, true, "/axis-cgi/com/ptz.cgi?query=position"},
                    Parametro{"metodo", "Método", "opcion", {"GET", "POST", "PUT"}, false, {}, "GET"},
                    Parametro{"cuerpo", "Cuerpo (POST/PUT)"}},
                   true,
                   {},
                   [red, yo](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                       if (!red) return listo({{"error", "el escáner no está disponible"}});
                       QString u, c;
                       red->clave(ipDe(e).toStdString(), u, c);
                       auto* nam = new QNetworkAccessManager(yo);
                       QObject::connect(nam, &QNetworkAccessManager::authenticationRequired, nam,
                                        [u, c](QNetworkReply*, QAuthenticator* au) {
                                            au->setUser(u);
                                            au->setPassword(c);
                                        });
                       QNetworkRequest pedido{QUrl("http://" + ipDe(e) + a.value("ruta").toString())};
                       pedido.setTransferTimeout(8000);
                       const QString metodo = a.value("metodo", "GET").toString();
                       QNetworkReply* r = metodo == "GET"
                                              ? nam->get(pedido)
                                              : nam->sendCustomRequest(pedido, metodo.toUtf8(),
                                                                       a.value("cuerpo").toString().toUtf8());
                       QObject::connect(r, &QNetworkReply::finished, nam, [r, nam, listo]() {
                           const int cod = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                           const QString cuerpo = QString::fromUtf8(r->readAll().left(20000));
                           r->deleteLater();
                           nam->deleteLater();
                           QVariantMap res{{"texto", QString("HTTP %1").arg(cod)},
                                           {"datos", QVariantMap{{"codigo", cod}, {"cuerpo", cuerpo}}}};
                           if (cod == 0 || cod >= 400)
                               res["error"] = QString("HTTP %1 %2").arg(cod).arg(cuerpo.left(300));
                           listo(res);
                       });
                   }});
        d.registrar("camara_red",
                    Accion{"agregar",
                           "Agregar como entrada",
                           "La conecta al estudio sincronizada (colchón, reloj), normalizada si hace falta y "
                           "con su PTZ ONVIF si tiene.",
                           {Parametro{"escena",
                                      "Escena",
                                      "texto",
                                      {},
                                      true,
                                      "dónde ponerla (no la que está al aire sin querer)"},
                            Parametro{"nombre", "Nombre"}},
                           true,
                           {},
                           [red](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                               if (!red) return listo({{"error", "el escáner no está disponible"}});
                               json h;
                               json op{{"escena", a.value("escena").toString().toStdString()}};
                               if (!a.value("nombre").toString().isEmpty())
                                   op["nombre"] = a.value("nombre").toString().toStdString();
                               const std::string err = red->agregar(ipDe(e).toStdString(), op, &h);
                               listo(err.empty() ? QVariantMap{{"texto", "Agregada"}, {"datos", aV(h)}}
                                                 : QVariantMap{{"error", QString::fromStdString(err)}});
                           }});
        d.registrar(
            "camara_red",
            Accion{
                "activar_ptz",
                "Activar su PTZ real",
                "Con la clave guardada: la consulta por ONVIF y configura el motor de la entrada que ya la "
                "muestra.",
                {Parametro{
                    "entrada", "Entrada", "texto", {}, false, "vacío: la que ya toma video de esta cámara"}},
                true,
                [red](const QVariantMap& e) { return red && !red->entradasDe(ipDe(e)).isEmpty(); },
                [red](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                    if (!red) return listo({{"error", "el escáner no está disponible"}});
                    QString entrada = a.value("entrada").toString();
                    if (entrada.isEmpty()) {
                        const QVariantList ya = red->entradasDe(ipDe(e));
                        if (!ya.isEmpty()) entrada = ya.first().toMap().value("entrada").toString();
                    }
                    red->activarPtz(ipDe(e).toStdString(), entrada, [listo](bool ok, QString texto) {
                        listo(ok ? QVariantMap{{"texto", texto}} : QVariantMap{{"error", texto}});
                    });
                }});
    }

    // --- Cámaras, micrófonos y NDI de esta máquina ------------------------------------------------
    // Los arma la página de entradas (InputsViewModel): una sola manera de crear
    // una entrada, la use la interfaz, el asistente o esta acción.
    if (entradas) {
        d.registrar("camara_local",
                    Accion{"agregar",
                           "Agregar como entrada de video",
                           "La suma al estudio como entrada (sincronizada y normalizada), con el nombre que le "
                           "pusiste y su formato preferido si lo configuraste.",
                           {Parametro{"escena", "Escena", "texto", {}, true}, Parametro{"nombre", "Nombre"}},
                           true,
                           {},
                           [entradas](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                               if (!entradas) return listo({{"error", "las entradas no están disponibles"}});
                               const QString nombre = a.value("nombre").toString().isEmpty()
                                                          ? e.value("nombre").toString()
                                                          : a.value("nombre").toString();
                               const QString err = entradas->addHardware(e.value("valor").toString(), nombre,
                                                                         a.value("escena").toString(), {});
                               listo(err.isEmpty() ? QVariantMap{{"texto", "Entrada «" + nombre + "» agregada"}}
                                                   : QVariantMap{{"error", err}});
                           }});
        d.registrar("microfono",
                    Accion{"agregar",
                           "Agregar como entrada de audio",
                           "Lo suma como entrada de audio (suena en todas las escenas; su procesamiento, en Audio).",
                           {Parametro{"nombre", "Nombre"}},
                           true,
                           {},
                           [entradas](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                               if (!entradas) return listo({{"error", "las entradas no están disponibles"}});
                               const QString err =
                                   entradas->addAudioInput(e.value("valor").toString(), a.value("nombre").toString());
                               listo(err.isEmpty() ? QVariantMap{{"texto", "Entrada de audio agregada"}}
                                                   : QVariantMap{{"error", err}});
                           }});
        d.registrar("ndi", Accion{"agregar",
                                  "Agregar como entrada de video",
                                  "Recibe este emisor NDI como entrada.",
                                  {Parametro{"escena", "Escena", "texto", {}, true}, Parametro{"nombre", "Nombre"}},
                                  true,
                                  {},
                                  [entradas](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                                      if (!entradas)
                                          return listo({{"error", "las entradas no están disponibles"}});
                                      const QString nombre = a.value("nombre").toString().isEmpty()
                                                                 ? e.value("nombre").toString()
                                                                 : a.value("nombre").toString();
                                      const QString err = entradas->addNdi(e.value("valor").toString(), nombre,
                                                                           a.value("escena").toString());
                                      listo(err.isEmpty() ? QVariantMap{{"texto", "Entrada NDI agregada"}}
                                                          : QVariantMap{{"error", err}});
                                  }});
    }
    (void)video;

    // --- USB ----------------------------------------------------------------------------------------
    if (pl) {
        d.registrar(
            "usb",
            Accion{"explorar_uvc",
                   "Explorar sus controles UVC",
                   "Si es una cámara UVC: su terminal de cámara (PTZ estándar y rangos) y sus unidades de "
                   "extensión con los selectores que "
                   "contestan (lo propio de la marca). Es el primer paso para un plugin de PTZ.",
                   {},
                   false,
                   [](const QVariantMap& e) { return e.value("datos").toMap().value("uvc").toBool(); },
                   [pl](const QVariantMap& e, const QVariantMap&, Listo listo) {
                       if (!pl) return listo({{"error", "los plugins no están disponibles"}});
                       const QVariantMap u = e.value("datos").toMap();
                       const QString codigo = R"PY(
import json
from ba_plugin import dispositivos as d
try:
    c = d.UVC.abrir(sys.argv[1], sys.argv[2])
    info = c.info()
    info["extensiones_detalle"] = {str(x["unidad"]): c.xu_explorar(x["unidad"], 24) for x in c.unidades()}
    print(json.dumps(info, default=str))
except Exception as e:
    print(json.dumps({"error": f"{type(e).__name__}: {e}"}))
)PY";
                       pl->correrPython(
                           codigo, {u.value("vid").toString(), u.value("pid").toString()}, [listo](json r) {
                               if (r.contains("error"))
                                   return listo(
                                       {{"error", QString::fromStdString(r.value("error", std::string()))}});
                               listo({{"texto", "Controles UVC"}, {"datos", aV(r)}});
                           });
                   }});
    }
    (void)camaras;
    // Las entradas eligen y muestran desde este registro (no buscan por su cuenta).
    if (entradas) entradas->setDispositivos(&d);
}

} // namespace ba::app::dispositivos
