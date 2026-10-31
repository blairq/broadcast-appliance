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
#include "ajustes/Ajustes.hpp"
#include "dispositivos/Dispositivos.hpp"
#include "dispositivos/Drivers.hpp"
#include "dispositivos/SondaReal.hpp"
#include "hw/Antibucle.hpp"
#include "platform/Maquina.hpp"
#include "platform/Hilo.hpp"
#include "platform/Paths.hpp"
#include "dispositivos/Onvif.hpp"
#include "plugins/Plugins.hpp"
#include "ptz/ControlCamaras.hpp"
#include "red/Red.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/OperatorMessages.hpp"
#include "video/Camaras.hpp"
#include "video/CatalogoGst.hpp"
#include "ui/VideoViewModel.hpp"
#if defined(BA_HAVE_NDI)
#include "emision/NdiSource.hpp"
#endif

#include <QAudioDevice>
#include <QAuthenticator>
#include <QCameraDevice>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>
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
    if (nombre.empty()) nombre = e.value("fabricante", QCoreApplication::translate("dispositivos", "Equipo").toStdString());
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
/// Las cámaras y los micrófonos de la máquina, cada uno con su id ESTABLE (el
/// mismo que guarda una entrada: uniqueID de AVFoundation, ruta del dispositivo,
/// UID de Core Audio, id de WASAPI/Pulse), nunca el índice: el nombre que le pone
/// el operador queda pegado a ese equipo aunque se lo reenchufe en otro puerto.
///
/// Corre en OTRO HILO: enumerar carga los proveedores de dispositivos de
/// GStreamer y uno de terceros puede tardar segundos o colgarse (la interfaz
/// llegó a quedar trabada 12,8 s). Lo que está en cuarentena no se carga (rango
/// NONE: dispositivos/Drivers).
QVariantList camarasYMicrofonos(AppRuntime& rt) {
    video::iniciarGStreamer();
    QVariantList eq;
    for (const auto& c : video::camarasDelEquipo(rt.engine()))
        eq << QVariantMap{{"clave", "video:" + qs(c.value)},
                          {"tipo", "camara_local"},
                          {"nombre", qs(c.name)},
                          {"valor", qs(c.value)},
                          {"detalle", QCoreApplication::translate("dispositivos", "Cámara o capturadora · %1").arg(qs(c.value))},
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
                          {"detalle",
                           pred       ? QCoreApplication::translate("dispositivos", "La que tenga elegida el sistema (la sigue si cambia)")
                           : loopback ? QCoreApplication::translate("dispositivos", "Lo que se reproduce por esa salida (no es un micrófono) · %1")
                                            .arg(qs(m.value))
                                      : QCoreApplication::translate("dispositivos", "Micrófono o entrada de audio · %1").arg(qs(m.value))},
                          {"datos", QVariantMap{{"id", qs(m.value)},
                                                {"pipeline", qs(audio::pipelineDeMicrofono(m.value))}}}};
    }
    return eq;
}

/// Las salidas de audio: las mismas que ofrece el mezclador para escuchar y para
/// las salidas (QMediaDevices, que ya las tiene: es barato y va en el hilo de la interfaz).
QVariantList salidasDeAudio() {
    QVariantList eq;
    for (const QAudioDevice& s : QMediaDevices::audioOutputs()) {
        const QString id = QString::fromLatin1(s.id());
        eq << QVariantMap{{"clave", "salida:" + id},
                          {"tipo", "salida_audio"},
                          {"nombre", s.description()},
                          {"valor", id},
                          {"detalle",
                           QCoreApplication::translate("dispositivos", "Salida de audio (placa de sonido, auriculares, HDMI) · %1").arg(id)},
                          {"datos", QVariantMap{{"id", id}, {"predeterminada", s.isDefault()}}}};
    }
    return eq;
}

/// Lo que el sistema dice que hay (los ids que Qt ya tiene, sin enumerar nada):
/// para saber si un aviso de «cambiaron los dispositivos» cambió algo de verdad.
std::string firmaDelSistema() {
    QStringList ids;
    for (const QAudioDevice& a : QMediaDevices::audioInputs()) ids << "i:" + QString::fromLatin1(a.id());
    for (const QAudioDevice& a : QMediaDevices::audioOutputs()) ids << "o:" + QString::fromLatin1(a.id());
    for (const QCameraDevice& c : QMediaDevices::videoInputs()) ids << "v:" + QString::fromLatin1(c.id());
    ids.sort();
    return ids.join('|').toStdString();
}

std::int64_t ahoraMs() {
    static QElapsedTimer t = [] {
        QElapsedTimer x;
        x.start();
        return x;
    }();
    return t.elapsed();
}

/// La búsqueda de cámaras y micrófonos: una a la vez, en otro hilo, con tiempo
/// límite, y sin entrar en el círculo «enumerar → el sistema avisa → enumerar».
struct BusquedaLocal {
    bool enCurso = false;
    bool trabada = false; ///< la última no volvió a tiempo (su hilo sigue adentro de un driver)
    int vuelta = 0;
    bool primera = false; ///< ya se hizo la del arranque
    std::vector<Listo> esperan;
    hw::Antibucle antibucle;
    std::function<void()> rearmar;
};
} // namespace

void registrarTodo(Dispositivos& d, const Dependencias& dep) {
    QPointer<red::Red> red = dep.red;
    QPointer<plugins::Plugins> pl = dep.plugins;
    QPointer<VideoViewModel> video = dep.video;
    QPointer<ptz::ControlCamaras> camaras = dep.camaras;
    QPointer<OperatorMessages> mensajes = dep.mensajes;
    QPointer<InputsViewModel> entradas = dep.entradas;
    QPointer<Dispositivos> yo(&d);

    // --- Detectores ---------------------------------------------------------------------------
    if (red) {
        d.registrar(Detector{
            "red", QCoreApplication::translate("dispositivos", "Red local"),
            QCoreApplication::translate("dispositivos", "Cámaras IP, grabadores y equipos de video de la subred: ONVIF, puertos, MAC/fabricante, URLs "
            "RTSP "
            "típicas probadas (unos 10 s)."),
            [red](Listo listo) {
                if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner de red no está disponible")}});
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
        // Los drivers de la máquina (dispositivos/Drivers): qué anduvo, qué está en
        // cuarentena, y la prueba de lo nuevo en un proceso hijo (ba-app --sonda-drivers).
        Drivers::Opciones od;
        od.archivo = ba::platform::dataRoot() / "Config" / "drivers.json";
        od.huella = ba::platform::huellaDeMaquina();
        od.etiqueta = ba::platform::modeloDeMaquina();
        od.umbral = ba::ajustes::entero("drivers.fallasCuarentena", 4);
        od.sondeo.programa = QCoreApplication::applicationFilePath();
        od.sondeo.argsBase = {sonda::kBandera};
        od.sondeo.limiteMs = ba::ajustes::sAMs("drivers.tiempoLimiteS", 8);
        // El hijo ve los rangos verdaderos (sin los NONE de la cuarentena) y no rehace el
        // registro de GStreamer (lo hace la app; dos a la vez se pisan).
        QProcessEnvironment entorno = QProcessEnvironment::systemEnvironment();
        entorno.remove("GST_PLUGIN_FEATURE_RANK");
        entorno.insert("GST_REGISTRY_UPDATE", "no");
        od.sondeo.entorno = entorno;
        od.ponerRango = [](const std::string& proveedor, int rango) { sonda::ponerRangoDeProveedor(proveedor, rango); };
        auto* drivers = new Drivers(std::move(od), &d);
        d.setDrivers(drivers);
        QPointer<Drivers> drv(drivers);
        if (mensajes)
            QObject::connect(drivers, &Drivers::cuarentenaNueva, &d,
                             [mensajes](const QString&, const QString& nombre, const QString& motivo) {
                                 if (!mensajes) return;
                                 mensajes->post({{"kind", "message"},
                                                 {"priority", "info"},
                                                 {"title", QCoreApplication::translate("dispositivos", "Driver en cuarentena: %1")
                                                               .arg(nombre)
                                                               .toStdString()},
                                                 {"text", QCoreApplication::translate("dispositivos", "%1. No se vuelve a cargar solo; "
                                                                                                      "«Probar de nuevo» en Dispositivos "
                                                                                                      "y equipos → Drivers.")
                                                              .arg(motivo)
                                                              .toStdString()}});
                             });

        auto est = std::make_shared<BusquedaLocal>();
        est->antibucle.setTiempos({800, 2000, ba::ajustes::entero("dispositivos.minimoEntreBusquedasMs", 10000)});
        d.registrar(Detector{
            "local", QCoreApplication::translate("dispositivos", "Cámaras, micrófonos y placas de sonido de esta máquina"),
            QCoreApplication::translate("dispositivos", "Las cámaras y capturadoras, los micrófonos y entradas de audio y las salidas de las placas de "
            "sonido, con su id estable. Se vuelve a buscar solo cuando se enchufa o se desenchufa algo."),
            [rt, est, drv](Listo listo) {
                if (est->trabada)
                    return listo({{"error", QCoreApplication::translate("dispositivos", "la búsqueda anterior sigue trabada adentro de un driver: se lo está "
                                            "probando aparte (Dispositivos y equipos → Drivers)")}});
                est->esperan.push_back(std::move(listo));
                if (est->enCurso) return; // contesta la que ya está andando
                est->enCurso = true;
                const int vuelta = ++est->vuelta;
                est->antibucle.empieza(ahoraMs(), firmaDelSistema());
                std::thread([rt, est, vuelta]() {
                    ba::platform::nombrarHilo("equipos-buscar");
                    QVariantList eq = camarasYMicrofonos(*rt);
                    QMetaObject::invokeMethod(
                        qApp,
                        [est, vuelta, eq]() mutable {
                            if (vuelta != est->vuelta) {
                                est->trabada = false; // volvió, tarde: ya se la había dado por trabada
                                return;
                            }
                            est->enCurso = false;
                            est->antibucle.termina(ahoraMs(), firmaDelSistema());
                            eq += salidasDeAudio();
                            std::vector<Listo> esperan;
                            esperan.swap(est->esperan);
                            for (auto& l : esperan) l({{"equipos", eq}});
                            if (est->rearmar) est->rearmar();
                        },
                        Qt::QueuedConnection);
                }).detach();
                const int limite = ba::ajustes::sAMs("dispositivos.busquedaLimiteS", 15);
                QTimer::singleShot(limite, qApp, [est, vuelta, drv, limite]() {
                    if (!est->enCurso || vuelta != est->vuelta) return;
                    // No volvió: un proveedor se colgó adentro de su hilo. La app sigue; lo
                    // que esperaba la lista se entera, y los proveedores se prueban aparte
                    // (en el hijo, con tiempo límite) para saber cuál fue y ponerlo en cuarentena.
                    est->enCurso = false;
                    est->trabada = true;
                    ++est->vuelta;
                    est->antibucle.termina(ahoraMs(), firmaDelSistema());
                    std::vector<Listo> esperan;
                    esperan.swap(est->esperan);
                    const QString err = QString(QCoreApplication::translate("dispositivos", "la búsqueda de cámaras y micrófonos no terminó en %1 s: se prueban "
                                                "los drivers aparte"))
                                            .arg(limite / 1000);
                    for (auto& l : esperan) l({{"error", err}});
                    if (drv) drv->sospechar(err);
                    if (est->rearmar) est->rearmar();
                });
            }});
        // Enchufar o desenchufar algo se ve solo (conectado / ausente), sin tocar
        // «Buscar»: el sistema avisa y se busca de nuevo, con hw::Antibucle en el medio
        // (espera corta porque un USB llega en varios avisos; los avisos que provoca la
        // búsqueda misma no cuentan; nunca más de una cada 10 s).
        auto* md = new QMediaDevices(&d);
        auto* reloj = new QTimer(&d);
        reloj->setSingleShot(true);
        est->rearmar = [est = std::weak_ptr<BusquedaLocal>(est), reloj]() {
            auto e = est.lock();
            if (!e || e->antibucle.enumerando()) return; // al terminar se rearma
            const std::int64_t f = e->antibucle.falta(ahoraMs());
            if (f >= 0)
                reloj->start(static_cast<int>(std::min<std::int64_t>(f, 600000)));
            else
                reloj->stop();
        };
        QObject::connect(reloj, &QTimer::timeout, &d, [yo, est]() {
            // Antes de la primera (que espera a los drivers) los avisos no disparan nada: en
            // una máquina nueva, probar un ASIO en el hijo mueve los dispositivos de Windows.
            if (!yo || !est->primera) return;
            if (est->antibucle.vence(ahoraMs(), firmaDelSistema()))
                yo->escanear("local", [](QVariantMap) {});
            else
                est->rearmar();
        });
        auto aviso = [est]() {
            est->antibucle.aviso(ahoraMs(), firmaDelSistema());
            est->rearmar();
        };
        QObject::connect(md, &QMediaDevices::audioInputsChanged, &d, aviso);
        QObject::connect(md, &QMediaDevices::audioOutputsChanged, &d, aviso);
        QObject::connect(md, &QMediaDevices::videoInputsChanged, &d, aviso);
        // Una búsqueda al arrancar (fuera del arranque mismo): las entradas, el
        // mezclador y esta página muestran los nombres y lo que falta desde el
        // principio. Espera a los drivers: en una máquina conocida es un momento
        // (lo de cuarentena ya arrancó afuera y solo se mira si hay algo nuevo); en
        // una nueva, que se prueben todos aparte antes de cargarlos acá. Con un tope,
        // por si la prueba se demora.
        auto primera = [yo, est]() {
            if (!yo || est->primera) return;
            est->primera = true;
            yo->escanear("local", [](QVariantMap) {});
        };
        QObject::connect(drivers, &Drivers::listoArranque, &d, primera);
        QTimer::singleShot(ba::ajustes::sAMs("drivers.esperaPrimeraBusquedaS", 90), &d, primera);
        QTimer::singleShot(1500, drivers, [drv]() {
            if (drv) drv->arrancar();
        });
    }
#if defined(BA_HAVE_NDI)
    d.registrar(Detector{"ndi", QCoreApplication::translate("dispositivos", "NDI® en la red"), QCoreApplication::translate("dispositivos", "Los emisores NDI que se anuncian en la red (unos 4 s)."),
                         [](Listo listo) {
                             std::thread([listo]() {
                                 ba::platform::nombrarHilo("ndi-buscar");
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
            "usb", QCoreApplication::translate("dispositivos", "USB, HID y serie"),
            QCoreApplication::translate("dispositivos", "Los equipos conectados por USB (vid:pid, marca, si son cámaras UVC), las interfaces HID y los "
            "puertos "
            "serie: por dónde se le habla a un equipo raro (la primera vez baja pyusb/hidapi/pyserial)."),
            [pl](Listo listo) {
                if (!pl) return listo({{"error", QCoreApplication::translate("dispositivos", "los plugins no están disponibles")}});
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
                                                    (u.value("uvc", false)
                                                         ? " · " + QCoreApplication::translate("dispositivos", "cámara UVC").toStdString()
                                                         : ""))},
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
                   QCoreApplication::translate("dispositivos", "Pedir usuario y clave"),
                   QCoreApplication::translate("dispositivos", "Muestra al operador el formulario seguro de la barra (se guarda cifrada; nadie la ve)."),
                   {},
                   true,
                   {},
                   [mensajes, red](const QVariantMap& e, const QVariantMap&, Listo listo) {
                       if (!mensajes || !red) return listo({{"error", QCoreApplication::translate("dispositivos", "no hay barra del operador")}});
                       const QString ip = ipDe(e);
                       const json eq = red->equipo(ip.toStdString());
                       const int n = mensajes->post(
                           {{"kind", "credential"},
                            {"platform", "camara:" + ip.toStdString()},
                            {"priority", "info"},
                            {"title", QCoreApplication::translate("dispositivos", "Usuario y clave de %1")
                                          .arg(e.value("nombre").toString())
                                          .toStdString()},
                            {"text", QCoreApplication::translate("dispositivos", "Se guarda cifrado en este equipo.").toStdString()},
                            {"fields",
                             json::array(
                                 {{{"key", "usuario"},
                                   {"label", QCoreApplication::translate("dispositivos", "Usuario").toStdString()},
                                   {"secret", false},
                                   {"placeholder", eq.value("usuario_de_fabrica", std::string("admin"))}},
                                  {{"key", "clave"},
                                   {"label", QCoreApplication::translate("dispositivos", "Clave").toStdString()},
                                   {"secret", true}}})}});
                       listo({{"texto", QString(QCoreApplication::translate("dispositivos", "Formulario %1 en la barra del operador.")).arg(n)}});
                   }});
        d.registrar(
            "camara_red",
            Accion{"clave",
                   QCoreApplication::translate("dispositivos", "Cargar usuario y clave"),
                   QCoreApplication::translate("dispositivos", "Guarda el usuario y la clave de la cámara (cifrados). Solo desde la interfaz: el "
                   "asistente usa «pedir_clave»."),
                   {Parametro{"usuario", QCoreApplication::translate("dispositivos", "Usuario"), "texto", {}, true},
                    Parametro{"clave", QCoreApplication::translate("dispositivos", "Clave"), "clave", {}, true}},
                   true,
                   {},
                   [red](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                       if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
                       const QString err = red->guardarCredenciales(ipDe(e), a.value("usuario").toString(),
                                                                    a.value("clave").toString());
                       listo(err.isEmpty()
                                 ? QVariantMap{{"texto",
                                                QCoreApplication::translate("dispositivos", "Guardada (cifrada). Ahora «Ficha» o «Funciones ONVIF».")}}
                                 : QVariantMap{{"error", err}});
                   }});
        d.registrar("camara_red",
                    Accion{"ficha",
                           QCoreApplication::translate("dispositivos", "Ficha y plan"),
                           QCoreApplication::translate("dispositivos", "ONVIF completo (equipo, perfiles, PTZ, imagen) + análisis del stream con "
                           "GStreamer + cómo conectarla."),
                           {},
                           false,
                           {},
                           [red](const QVariantMap& e, const QVariantMap&, Listo listo) {
                               if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
                               red->ficha(ipDe(e).toStdString(), [listo](json f) {
                                   if (f.contains("error") && !f.value("onvif", false) && !f.contains("plan"))
                                       return listo({{"error", QString::fromStdString(
                                                                   f.value("error", std::string()))},
                                                     {"datos", aV(f)}});
                                   listo({{"texto", QCoreApplication::translate("dispositivos", "Ficha lista")}, {"datos", aV(f)}});
                               });
                           }});
        d.registrar("camara_red", Accion{"funciones_onvif",
                                         QCoreApplication::translate("dispositivos", "Funciones ONVIF"),
                                         QCoreApplication::translate("dispositivos", "Todo lo que la cámara dice que puede, por protocolo: ejes PTZ y "
                                         "cómo se mueven (continuo, absoluto, relativo), "
                                         "límites, foco, home, presets, comandos auxiliares, imagen; y su "
                                         "posición actual. Con las respuestas crudas."),
                                         {},
                                         false,
                                         {},
                                         [red, yo](const QVariantMap& e, const QVariantMap&, Listo listo) {
                                             if (!red)
                                                 return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
                                             QString u, c;
                                             red->clave(ipDe(e).toStdString(), u, c);
                                             auto* cli = new ClienteOnvif(ipDe(e), u, c, yo);
                                             cli->funciones([cli, listo](QVariantMap r) {
                                                 cli->deleteLater();
                                                 if (r.contains("error")) return listo(r);
                                                 listo({{"texto", QCoreApplication::translate("dispositivos", "Funciones mapeadas")}, {"datos", r}});
                                             });
                                         }});
        d.registrar(
            "camara_red",
            Accion{"onvif",
                   QCoreApplication::translate("dispositivos", "Llamar una operación ONVIF"),
                   QCoreApplication::translate("dispositivos", "Para explorar: manda `cuerpo` (el XML del Body, p. ej. "
                   "<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>) "
                   "al `servicio`, con la clave guardada. {perfil}, {config_ptz} y {fuente} se reemplazan "
                   "por los de la cámara. Devuelve la respuesta aplanada."),
                   {Parametro{"servicio",
                              QCoreApplication::translate("dispositivos", "Servicio"),
                              "opcion",
                              {"ptz", "media", "imaging", "device"},
                              true,
                              {},
                              "ptz"},
                    Parametro{"cuerpo", QCoreApplication::translate("dispositivos", "Cuerpo (XML del Body)"), "texto", {}, true}},
                   true,
                   {},
                   [red, yo](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                       if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
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
                                                    : QCoreApplication::translate("dispositivos", "La cámara contestó: %1")
                                                          .arg(r.value("falla").toString())},
                                      {"datos", r}});
                           });
                   }});
        d.registrar(
            "camara_red",
            Accion{"http",
                   QCoreApplication::translate("dispositivos", "Pedido HTTP a la cámara"),
                   QCoreApplication::translate("dispositivos", "Para la API propia de la marca (Axis VAPIX: /axis-cgi/com/ptz.cgi?query=limits; "
                   "Hikvision ISAPI: "
                   "/ISAPI/PTZCtrl/channels/1/status…), con el usuario y la clave guardados (Basic o "
                   "Digest). Devuelve el texto."),
                   {Parametro{"ruta", QCoreApplication::translate("dispositivos", "Ruta"), "texto", {}, true, "/axis-cgi/com/ptz.cgi?query=position"},
                    Parametro{"metodo", QCoreApplication::translate("dispositivos", "Método"), "opcion", {"GET", "POST", "PUT"}, false, {}, "GET"},
                    Parametro{"cuerpo", QCoreApplication::translate("dispositivos", "Cuerpo (POST/PUT)")}},
                   true,
                   {},
                   [red, yo](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                       if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
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
                           QCoreApplication::translate("dispositivos", "Agregar como entrada"),
                           QCoreApplication::translate("dispositivos", "La conecta al estudio sincronizada (colchón, reloj), normalizada si hace falta y "
                           "con su PTZ ONVIF si tiene."),
                           {Parametro{"escena",
                                      QCoreApplication::translate("dispositivos", "Escena"),
                                      "texto",
                                      {},
                                      true,
                                      QCoreApplication::translate("dispositivos", "dónde ponerla (no la que está al aire sin querer)")},
                            Parametro{"nombre", QCoreApplication::translate("dispositivos", "Nombre")}},
                           true,
                           {},
                           [red](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                               if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
                               json op{{"escena", a.value("escena").toString().toStdString()}};
                               if (!a.value("nombre").toString().isEmpty())
                                   op["nombre"] = a.value("nombre").toString().toStdString();
                               red->agregar(ipDe(e).toStdString(), op, [listo](const std::string& err, const json& h) {
                                   listo(err.empty() ? QVariantMap{{"texto", QCoreApplication::translate("dispositivos", "Agregada")},
                                                                   {"datos", aV(h)}}
                                                     : QVariantMap{{"error", QString::fromStdString(err)}});
                               });
                           }});
        d.registrar(
            "camara_red",
            Accion{
                "activar_ptz",
                QCoreApplication::translate("dispositivos", "Activar su PTZ real"),
                QCoreApplication::translate("dispositivos", "Con la clave guardada: la consulta por ONVIF y configura el motor de la entrada que ya la "
                "muestra."),
                {Parametro{
                    "entrada", QCoreApplication::translate("dispositivos", "Entrada"), "texto", {}, false, QCoreApplication::translate("dispositivos", "vacío: la que ya toma video de esta cámara")}},
                true,
                [red](const QVariantMap& e) { return red && !red->entradasDe(ipDe(e)).isEmpty(); },
                [red](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                    if (!red) return listo({{"error", QCoreApplication::translate("dispositivos", "el escáner no está disponible")}});
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
                           QCoreApplication::translate("dispositivos", "Agregar como entrada de video"),
                           QCoreApplication::translate("dispositivos", "La suma al estudio como entrada (sincronizada y normalizada), con el nombre que le "
                           "pusiste y su formato preferido si lo configuraste."),
                           {Parametro{"escena", QCoreApplication::translate("dispositivos", "Escena"), "texto", {}, true}, Parametro{"nombre", QCoreApplication::translate("dispositivos", "Nombre")}},
                           true,
                           {},
                           [entradas](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                               if (!entradas) return listo({{"error", QCoreApplication::translate("dispositivos", "las entradas no están disponibles")}});
                               const QString nombre = a.value("nombre").toString().isEmpty()
                                                          ? e.value("nombre").toString()
                                                          : a.value("nombre").toString();
                               const auto alTerminar = [listo, nombre](const QString& err) {
                                   listo(err.isEmpty() ? QVariantMap{{"texto", QCoreApplication::translate("dispositivos", "Entrada «%1» agregada")
                                                                                   .arg(nombre)}}
                                                       : QVariantMap{{"error", err}});
                               };
                               const QString err = entradas->addHardware(e.value("valor").toString(), nombre,
                                                                         a.value("escena").toString(), {}, alTerminar);
                               if (!err.isEmpty()) alTerminar(err);
                           }});
        d.registrar("microfono",
                    Accion{"agregar",
                           QCoreApplication::translate("dispositivos", "Agregar como entrada de audio"),
                           QCoreApplication::translate("dispositivos", "Lo suma como entrada de audio (suena en todas las escenas; su procesamiento, en Audio)."),
                           {Parametro{"nombre", QCoreApplication::translate("dispositivos", "Nombre")}},
                           true,
                           {},
                           [entradas](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                               if (!entradas) return listo({{"error", QCoreApplication::translate("dispositivos", "las entradas no están disponibles")}});
                               const auto alTerminar = [listo](const QString& err) {
                                   listo(err.isEmpty() ? QVariantMap{{"texto", QCoreApplication::translate("dispositivos", "Entrada de audio agregada")}}
                                                       : QVariantMap{{"error", err}});
                               };
                               const QString err =
                                   entradas->addAudioInput(e.value("valor").toString(), a.value("nombre").toString(), alTerminar);
                               if (!err.isEmpty()) alTerminar(err);
                           }});
        d.registrar("ndi", Accion{"agregar",
                                  QCoreApplication::translate("dispositivos", "Agregar como entrada de video"),
                                  QCoreApplication::translate("dispositivos", "Recibe este emisor NDI como entrada."),
                                  {Parametro{"escena", QCoreApplication::translate("dispositivos", "Escena"), "texto", {}, true}, Parametro{"nombre", QCoreApplication::translate("dispositivos", "Nombre")}},
                                  true,
                                  {},
                                  [entradas](const QVariantMap& e, const QVariantMap& a, Listo listo) {
                                      if (!entradas)
                                          return listo({{"error", QCoreApplication::translate("dispositivos", "las entradas no están disponibles")}});
                                      const QString nombre = a.value("nombre").toString().isEmpty()
                                                                 ? e.value("nombre").toString()
                                                                 : a.value("nombre").toString();
                                      const auto alTerminar = [listo](const QString& err) {
                                          listo(err.isEmpty() ? QVariantMap{{"texto", QCoreApplication::translate("dispositivos", "Entrada NDI agregada")}}
                                                              : QVariantMap{{"error", err}});
                                      };
                                      const QString err = entradas->addNdi(e.value("valor").toString(), nombre,
                                                                           a.value("escena").toString(), alTerminar);
                                      if (!err.isEmpty()) alTerminar(err);
                                  }});
    }
    (void)video;

    // --- USB ----------------------------------------------------------------------------------------
    if (pl) {
        d.registrar(
            "usb",
            Accion{"explorar_uvc",
                   QCoreApplication::translate("dispositivos", "Explorar sus controles UVC"),
                   QCoreApplication::translate("dispositivos", "Si es una cámara UVC: su terminal de cámara (PTZ estándar y rangos) y sus unidades de "
                   "extensión con los selectores que "
                   "contestan (lo propio de la marca). Es el primer paso para un plugin de PTZ."),
                   {},
                   false,
                   [](const QVariantMap& e) { return e.value("datos").toMap().value("uvc").toBool(); },
                   [pl](const QVariantMap& e, const QVariantMap&, Listo listo) {
                       if (!pl) return listo({{"error", QCoreApplication::translate("dispositivos", "los plugins no están disponibles")}});
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
                               listo({{"texto", QCoreApplication::translate("dispositivos", "Controles UVC")}, {"datos", aV(r)}});
                           });
                   }});
    }
    (void)camaras;
    // Las entradas eligen y muestran desde este registro (no buscan por su cuenta).
    if (entradas) entradas->setDispositivos(&d);
}

} // namespace ba::app::dispositivos
