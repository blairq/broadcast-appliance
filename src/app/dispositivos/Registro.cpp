// ============================================================================
// src/app/dispositivos/Registro.cpp — qué se busca y qué se le puede hacer
//
// Acá se enchufan los detectores y las acciones de cada tipo de equipo. Para
// sumar una acción: un `Accion{...}` más con sus parámetros. El asistente
// (equipo_accion) y la interfaz (página Equipos) la ven solos.
// ============================================================================

#include "dispositivos/Registro.hpp"

#include "dispositivos/Dispositivos.hpp"
#include "dispositivos/Onvif.hpp"
#include "plugins/Plugins.hpp"
#include "ptz/ControlCamaras.hpp"
#include "red/Red.hpp"
#include "ui/OperatorMessages.hpp"
#include "video/VideoGrafos.hpp"

#include <QAuthenticator>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
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
            {"tipo", camara ? "camara_red" : "equipo_red"},
            {"nombre", QString::fromStdString(nombre + " " + ip)},
            {"detalle", detalle},
            {"datos", aV(e)}};
}
} // namespace

void registrarTodo(Dispositivos& d, const Dependencias& dep) {
    QPointer<red::Red> red = dep.red;
    QPointer<plugins::Plugins> pl = dep.plugins;
    QPointer<video::VideoGrafos> video = dep.video;
    QPointer<ptz::ControlCamaras> camaras = dep.camaras;
    QPointer<OperatorMessages> mensajes = dep.mensajes;
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
    if (pl) {
        d.registrar(Detector{
            "local", "Cámaras y micrófonos de esta máquina",
            "Lo que ve GStreamer (webcams, capturadoras, micrófonos), con el pipeline para usarlo de "
            "entrada.",
            [](Listo listo) {
                std::thread([listo]() {
                    const json l = plugins::Plugins::camarasYMicrofonos();
                    QMetaObject::invokeMethod(
                        qApp,
                        [l, listo]() {
                            QVariantList eq;
                            int nv = 0, na = 0;
                            for (const auto& x : l) {
                                const bool video =
                                    x.value("clase", std::string()).find("Video") != std::string::npos;
                                eq << QVariantMap{
                                    {"clave",
                                     QString(video ? "video:%1" : "audio:%1").arg(video ? nv++ : na++)},
                                    {"tipo", video ? "camara_local" : "microfono"},
                                    {"nombre", QString::fromStdString(x.value("nombre", std::string()))},
                                    {"detalle", QString::fromStdString(x.value("gst", std::string()))},
                                    {"datos", aV(x)}};
                            }
                            listo({{"equipos", eq}});
                        },
                        Qt::QueuedConnection);
                }).detach();
            }});
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

    // --- Cámaras de esta máquina -------------------------------------------------------------------
    if (video) {
        d.registrar(
            "camara_local",
            Accion{
                "agregar",
                "Agregar como entrada",
                "La suma al estudio como entrada de GStreamer (con su pipeline), sincronizada y normalizada.",
                {Parametro{"escena", "Escena", "texto", {}, true}, Parametro{"nombre", "Nombre"}},
                true,
                {},
                [video](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                    if (!video) return listo({{"error", "el video no está disponible"}});
                    const QString nombre = a.value("nombre").toString().isEmpty()
                                               ? e.value("nombre").toString()
                                               : a.value("nombre").toString();
                    const std::string err = video->crearEntradaPipeline(
                        nombre.toStdString(), e.value("datos").toMap().value("gst").toString().toStdString(),
                        "", a.value("escena").toString().toStdString(), 100, 1);
                    listo(err.empty() ? QVariantMap{{"texto", "Entrada «" + nombre + "» agregada"}}
                                      : QVariantMap{{"error", QString::fromStdString(err)}});
                }});
    }

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
}

} // namespace ba::app::dispositivos
