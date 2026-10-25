// src/app/tests/test_dispositivos.cpp — el registro de dispositivos de la máquina:
// nombres del operador (persistencia, renombre, choques), ausentes y detectores
// (sin hardware: los detectores son de mentira)

#include "audio/GrupoDeSalida.hpp"
#include "audio/UnificarEntradas.hpp"
#include "dispositivos/Conocidos.hpp"
#include "dispositivos/Dispositivos.hpp"

#include <QCoreApplication>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace ba::app::dispositivos;
namespace fs = std::filesystem;

namespace {
int fallos = 0;
#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (c)                                                                                               \
            std::printf("ok: %s\n", m);                                                                      \
        else {                                                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

fs::path carpetaTemporal() {
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path p = fs::temp_directory_path() / ("ba-test-dispositivos-" + std::to_string(t));
    fs::create_directories(p);
    return p;
}

/// Lo que «encuentra» el detector de mentira en cada búsqueda.
QVariantList hay;

QVariantMap camara(const QString& id, const QString& nombre) {
    return {{"clave", "video:" + id}, {"tipo", "camara_local"}, {"nombre", nombre}, {"valor", id}, {"detalle", "cam"}};
}
QVariantMap mic(const QString& id, const QString& nombre) {
    return {{"clave", "audio:" + id}, {"tipo", "microfono"}, {"nombre", nombre}, {"valor", id}, {"detalle", "mic"}};
}
QVariantMap usb(const QString& id) {
    return {{"clave", id}, {"tipo", "usb"}, {"nombre", "Hub USB"}, {"detalle", "usb"}};
}

void registrarDetector(Dispositivos& d) {
    d.registrar(Detector{"local", "De mentira", "", [](Listo listo) { listo({{"equipos", hay}}); }});
}

QVariantMap buscar(const QVariantList& l, const QString& id) {
    for (const QVariant& v : l)
        if (v.toMap().value("id").toString() == id) return v.toMap();
    return {};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const fs::path dir = carpetaTemporal();
    const fs::path archivo = dir / "Config" / "dispositivos.json";

    // --- Conocidos: el archivo de la máquina ---------------------------------------
    {
        CHECK(seRecuerda("microfono") && seRecuerda("camara_local") && seRecuerda("salida_audio") &&
                  seRecuerda("ndi") && seRecuerda("camara_red"),
              "se recuerdan los de audio y video");
        CHECK(!seRecuerda("usb") && !seRecuerda("hid") && !seRecuerda("serie") && !seRecuerda("equipo_red"),
              "lo USB/HID/serie y los equipos sueltos no se recuerdan");

        Conocidos c(archivo);
        CHECK(c.todos().empty(), "sin archivo, vacío");
        CHECK(c.visto("local:audio:UID-1", "microfono", "MacBook Pro Microphone", "UID-1", "2026-10-25T10:00:00"),
              "uno nuevo se anota (y se guarda)");
        CHECK(fs::exists(archivo), "el archivo se crea (con su carpeta)");
        CHECK(!fs::exists(fs::path(archivo).concat(".tmp")), "no queda el temporal");
        CHECK(!c.visto("local:audio:UID-1", "microfono", "MacBook Pro Microphone", "UID-1", "2026-10-25T11:00:00"),
              "volver a verlo igual no reescribe el archivo");
        CHECK(!c.visto("usb:1234", "usb", "Hub", "x", ""), "un USB no se anota");
        CHECK(c.visto("local:video:CAM-A", "camara_local", "FaceTime HD Camera", "CAM-A", ""), "una cámara");

        CHECK(c.renombrar("local:audio:UID-1", "  Mic del atril  ").empty(), "renombrar");
        CHECK(c.nombreDe("local:audio:UID-1", "x") == "Mic del atril", "el nombre queda sin espacios de más");
        CHECK(c.uno("local:audio:UID-1").tecnico == "MacBook Pro Microphone", "el técnico se conserva");
        CHECK(!c.renombrar("local:video:CAM-A", "mic DEL atril").empty(),
              "dos dispositivos no pueden llamarse igual (sin importar mayúsculas)");
        CHECK(!c.renombrar("no:existe", "algo").empty(), "renombrar uno que no existe da error");
        CHECK(!c.renombrar("local:video:CAM-A", std::string(81, 'x')).empty(), "nombre demasiado largo");
        CHECK(c.renombrar("local:video:CAM-A", "Cámara del fondo").empty(), "otro nombre, con tilde");
        CHECK(c.porValor("camara_local", "CAM-A").id == "local:video:CAM-A", "porValor encuentra por el id del sistema");
        CHECK(c.porValor("microfono", "CAM-A").id.empty(), "porValor respeta el tipo");

        CHECK(c.configurar("local:video:CAM-A", {{"nota", "trípode"}, {"modo", "1920x1080@30"}}).empty(), "configurar");
        CHECK(c.configurar("local:video:CAM-A", {{"modo", nullptr}}).empty(), "una clave en null se borra");
        CHECK(c.uno("local:video:CAM-A").config.value("nota", "") == "trípode" &&
                  !c.uno("local:video:CAM-A").config.contains("modo"),
              "la configuración se mezcla");
        CHECK(!c.configurar("local:video:CAM-A", nlohmann::json::array()).empty(), "la configuración es un objeto");
    }
    {
        // Otra instancia (otro arranque): lee lo guardado.
        Conocidos c(archivo);
        CHECK(c.todos().size() == 2, "se leen los dos");
        CHECK(c.nombreDe("local:audio:UID-1", "") == "Mic del atril", "el nombre sobrevive al reinicio");
        CHECK(c.uno("local:video:CAM-A").mostrar() == "Cámara del fondo", "con tildes (UTF-8)");
        CHECK(c.uno("local:video:CAM-A").config.value("nota", "") == "trípode", "la configuración también");
        CHECK(c.renombrar("local:audio:UID-1", "MacBook Pro Microphone").empty() &&
                  c.uno("local:audio:UID-1").nombre.empty(),
              "ponerle el nombre del sistema es no ponerle nombre");
        CHECK(c.renombrar("local:audio:UID-1", "Mic del atril").empty() && c.renombrar("local:audio:UID-1", "").empty() &&
                  c.nombreDe("local:audio:UID-1", "tec") == "tec",
              "vacío vuelve al del sistema");
        CHECK(c.visto("local:audio:UID-1", "microfono", "Micrófono (nuevo driver)", "UID-1", ""),
              "si el sistema cambia el nombre técnico, se actualiza");
        CHECK(c.olvidar("local:audio:UID-1").empty() && !c.hay("local:audio:UID-1"), "olvidar");
        CHECK(!c.olvidar("local:audio:UID-1").empty(), "olvidar dos veces da error");
        Conocidos::setDeLaMaquina(&c);
        CHECK(Conocidos::nombreParaMostrar("local:video:CAM-A", "tec") == "Cámara del fondo",
              "el nombre de la máquina, para el mezclador");
        Conocidos::setDeLaMaquina(nullptr);
        CHECK(Conocidos::nombreParaMostrar("local:video:CAM-A", "tec") == "tec", "sin registro, el técnico");
    }
    {
        // Un archivo roto no tira abajo nada: arranca vacío.
        const fs::path roto = dir / "roto.json";
        std::ofstream(roto) << "{esto no es json";
        Conocidos c(roto);
        CHECK(c.todos().empty(), "un archivo roto se lee vacío");
    }

    // --- Dispositivos: el registro con nombres y ausentes ------------------------------
    const fs::path archivo2 = dir / "Config" / "dispositivos2.json";
    {
        Dispositivos d(archivo2);
        registrarDetector(d);
        QString recibido;
        d.registrar("microfono", Accion{"probar", "Probar", "", {}, false, {},
                                        [&recibido](const QVariantMap& e, const QVariantMap&, Listo listo) {
                                            recibido = e.value("nombre").toString();
                                            listo({{"texto", "ok"}});
                                        }});
        hay = {mic("UID-1", "MacBook Pro Microphone"), camara("CAM-A", "FaceTime HD Camera"), usb("1:2")};
        int avisos = 0;
        QObject::connect(&d, &Dispositivos::cambio, [&avisos]() { ++avisos; });
        d.escanear("local", [](QVariantMap) {});
        CHECK(avisos <= 3, "una búsqueda avisa pocas veces (no una por equipo)");
        QVariantList l = d.equipos();
        CHECK(l.size() == 3, "los tres encontrados");
        CHECK(l.value(0).toMap().value("tipo").toString() == "camara_local" &&
                  l.value(1).toMap().value("tipo").toString() == "microfono",
              "ordenados: video, audio y después lo demás");
        const QVariantMap m = buscar(l, "local:audio:UID-1");
        CHECK(m.value("conectado").toBool() && m.value("estado").toString() == "conectado", "conectado");
        CHECK(m.value("nombre").toString() == "MacBook Pro Microphone" && !m.value("nombre_propio").toBool(),
              "sin nombre propio, el del sistema");
        CHECK(m.value("valor").toString() == "UID-1", "el valor es el id del sistema (no el índice)");

        CHECK(d.renombrar("local:audio:UID-1", "Mic del atril").isEmpty(), "renombrar desde el registro");
        CHECK(avisos > 0, "renombrar avisa (la vista se entera)");
        const QVariantMap m2 = d.equipo("local:audio:UID-1");
        CHECK(m2.value("nombre").toString() == "Mic del atril" && m2.value("nombre_propio").toBool() &&
                  m2.value("tecnico").toString() == "MacBook Pro Microphone",
              "con el nombre del operador y el técnico al lado");
        CHECK(!d.renombrar("local:usb:1:2", "Mi hub").isEmpty(), "a un USB no se le pone nombre");
        CHECK(d.equipos("", "MacBook").size() == 1, "se busca también por el nombre técnico");
        CHECK(d.porValor("microfono", "UID-1").value("nombre").toString() == "Mic del atril", "porValor (conectado)");

        d.correr("local:audio:UID-1", "probar", {}, [](QVariantMap) {});
        CHECK(recibido == "Mic del atril", "las acciones reciben el nombre del operador");
        CHECK(!d.olvidar("local:audio:UID-1").isEmpty(), "uno conectado no se olvida");
        CHECK(d.configurar("local:video:CAM-A", {{"nota", "fondo"}}).isEmpty() &&
                  d.equipo("local:video:CAM-A").value("config").toMap().value("nota").toString() == "fondo",
              "configurar desde el registro");
    }
    {
        // Otro arranque: antes de buscar no se sabe; después, el micrófono que no está queda AUSENTE.
        Dispositivos d(archivo2);
        registrarDetector(d);
        QVariantMap m = d.equipo("local:audio:UID-1");
        CHECK(!m.isEmpty() && m.value("estado").toString() == "sin_buscar",
              "antes de la primera búsqueda: sin buscar (no ausente)");
        CHECK(m.value("nombre").toString() == "Mic del atril", "pero ya con su nombre");

        hay = {camara("CAM-A", "FaceTime HD Camera")};
        d.escanear("local", [](QVariantMap) {});
        m = d.equipo("local:audio:UID-1");
        CHECK(m.value("estado").toString() == "ausente" && !m.value("conectado").toBool(),
              "el que no aparece queda ausente");
        CHECK(m.value("nombre").toString() == "Mic del atril", "ausente, con el nombre del operador");
        CHECK(m.value("acciones").toList().isEmpty(), "un ausente no ofrece acciones");
        CHECK(!buscar(d.equipos(), "local:audio:UID-1").isEmpty(), "y sigue en la lista");
        CHECK(buscar(d.equipos(), "local:usb:1:2").isEmpty(), "el USB que no está, no (no se recuerda)");
        CHECK(d.porValor("microfono", "UID-1").value("estado").toString() == "ausente",
              "porValor encuentra al ausente (para la entrada que lo usa)");
        QString err;
        d.correr("local:audio:UID-1", "probar", {}, [&err](QVariantMap r) { err = r.value("error").toString(); });
        CHECK(err.contains("no está conectado"), "a un ausente no se le corren acciones");

        // Vuelve (se enchufa): conectado otra vez, con el mismo nombre.
        hay = {camara("CAM-A", "FaceTime HD Camera"), mic("UID-1", "MacBook Pro Microphone")};
        d.escanear("local", [](QVariantMap) {});
        m = d.equipo("local:audio:UID-1");
        CHECK(m.value("conectado").toBool() && m.value("nombre").toString() == "Mic del atril",
              "al volver, conectado y con su nombre");

        hay = {camara("CAM-A", "FaceTime HD Camera")};
        d.escanear("local", [](QVariantMap) {});
        CHECK(d.olvidar("local:audio:UID-1").isEmpty() && d.equipo("local:audio:UID-1").isEmpty(),
              "un ausente se puede olvidar");
    }

    // --- Una entrada de audio por dispositivo físico (lo visto en la PC con Windows) ---
    {
        using namespace ba::app::audio;
        const std::string ep = "{0.0.1.00000000}.{eea76234-94d6-4fbd-bf1a-72ef6097110a}";
        const std::string guid = "{EEA76234-94D6-4FBD-BF1A-72EF6097110A}";
        const std::string monitorDell = "{0.0.0.00000000}.{11111111-2222-3333-4444-555555555555}";
        CHECK(claveFisica(ep) == claveFisica(guid), "el endpoint y el GUID solo (en mayúsculas) son el mismo");
        CHECK(claveFisica("\\\\?\\SWD#MMDEVAPI#" + ep + "#{2eef81be-33fa-4800-9670-1cd474972c3f}") == claveFisica(ep),
              "la ruta de la interfaz también: la clase de audio no cuenta");
        CHECK(esPredeterminadaDelProveedor("{2EEF81BE-33FA-4800-9670-1CD474972C3F}"),
              "«Default Audio Capture Device» es la predeterminada del proveedor");
        CHECK(!esPredeterminadaDelProveedor(ep) && !esPredeterminadaDelProveedor("BuiltInMicrophoneDevice"),
              "un dispositivo de verdad no");
        const auto l = unificarEntradas(
            {{"Micrófono (HD Pro Webcam C910)", guid, "wasapi2src", false},
             {"Micrófono (HD Pro Webcam C910)", ep, "wasapi2src", false},
             {"Micrófono (HD Pro Webcam C910)", guid, "directsoundsrc", false},
             {"Default Audio Capture Device", "{2EEF81BE-33FA-4800-9670-1CD474972C3F}", "wasapi2src", false},
             {"Default Audio Render Device", "{E6327CAD-DCEC-4949-AE8A-991E976A79D2}", "wasapi2src", true},
             {"5 - DELL U2913WM (AMD High Definition Audio Device)", monitorDell, "wasapi2src", true}},
            "wasapi2src");
        CHECK(l.size() == 2, "queda un micrófono y un loopback");
        CHECK(l.size() == 2 && l[0].second == ep, "del micrófono queda el id de endpoint (el que abre wasapi2src)");
        CHECK(l.size() == 2 && l[1].first == "Lo que suena en «5 - DELL U2913WM (AMD High Definition Audio Device)»" &&
                  l[1].second == std::string(kPrefijoLoopback) + monitorDell,
              "el loopback de una salida se nombra como tal");
        // Mac y Linux: nada cambia.
        const auto m = unificarEntradas({{"MacBook Pro Microphone", "BuiltInMicrophoneDevice", "osxaudiosrc", false},
                                         {"default", "default", "osxaudiosrc", false}},
                                        "osxaudiosrc");
        CHECK(m.size() == 1 && m[0].second == "BuiltInMicrophoneDevice", "en la Mac queda igual (sin la default repetida)");

        // Lo que la máquina ya recordaba con el id viejo no aparece como «ausente»
        // al lado del mismo micrófono conectado.
        const fs::path dir2 = carpetaTemporal();
        Dispositivos d2(dir2 / "dispositivos.json");
        registrarDetector(d2);
        hay = {mic(QString::fromStdString(guid), "Micrófono (HD Pro Webcam C910)"),
               mic("{2EEF81BE-33FA-4800-9670-1CD474972C3F}", "Default Audio Capture Device")};
        d2.escanear("local", [](QVariantMap) {});
        hay = {mic(QString::fromStdString(ep), "Micrófono (HD Pro Webcam C910)")};
        d2.escanear("local", [](QVariantMap) {});
        const QVariantList mics = d2.equipos("microfono");
        CHECK(mics.size() == 1, "un solo micrófono: ni el GUID viejo ni la predeterminada del proveedor como ausentes");
        std::error_code ec2;
        fs::remove_all(dir2, ec2);
    }

    // --- El grupo de una salida de audio sale de su id, nunca del nombre ----------
    {
        using ba::app::audio::grupoDeSalida;
        CHECK(grupoDeSalida("alsa_output.pci-0000_00_1f.3.analog-stereo") == "pci-0000_00_1f",
              "PulseAudio: la placa sale del id");
        CHECK(grupoDeSalida("alsa_output.pci-0000_00_1f.3.analog-stereo") ==
                  grupoDeSalida("alsa_output.pci-0000_00_1f.3.hdmi-stereo"),
              "PulseAudio: dos salidas de la misma placa, mismo grupo");
        // Mac: sin puntos. El grupo es el id: renombrar (el nombre ni entra) no lo cambia.
        CHECK(grupoDeSalida("BuiltInSpeakerDevice") == "BuiltInSpeakerDevice", "Core Audio: el id es el grupo");
        CHECK(grupoDeSalida("BuiltInSpeakerDevice") != grupoDeSalida("AppleUSBAudioEngine:Focusrite:1"),
              "Core Audio: dos salidas distintas no se juntan");
        // Windows: el id tiene puntos pero no dice la placa; antes todas caían en el grupo «0».
        CHECK(grupoDeSalida("{0.0.0.00000000}.{aaaa}") != grupoDeSalida("{0.0.0.00000000}.{bbbb}"),
              "WASAPI: cada salida en su grupo (no todas en uno)");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("%s\n", fallos == 0 ? "TODO OK" : "HAY FALLOS");
    return fallos == 0 ? 0 : 1;
}
