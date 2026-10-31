// ============================================================================
// src/app/ui/SettingsModel.cpp — ver SettingsModel.hpp
// ============================================================================

#include "arranque/AjustesMotor.hpp"
#include "emision/BroadcastEngine.hpp"
#include "ui/SettingsModel.hpp"
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <QHostInfo>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

#include "arranque/AppRuntime.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "platform/IdiomaSistema.hpp"
#include "ajustes/Registro.hpp"
#include "sala/ConfigSala.hpp"
#include "control/TemasWeb.hpp"
#include "ipc/LugaresSala.hpp"

namespace ba::app {

using json = nlohmann::json;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

/// Un texto del formulario en el idioma del operador. Se llama `traducir` (con el
/// contexto primero) para que lupdate lo encuentre: busca ese nombre
/// (tools/i18n/actualizar.py, translate+=traducir).
QString traducir(const char* contexto, const char* texto) { return qs(ba::i18n::traducir(contexto, texto)); }

/// Las secciones (su nombre es la CLAVE: la página las compara y el MCP las busca por
/// nombre). Se traducen solo al mostrarlas (seccionVisible).
constexpr const char* kSecciones[] = {
    QT_TRANSLATE_NOOP("ajustes", "Video"),
    QT_TRANSLATE_NOOP("ajustes", "Cámaras y entradas"),
    QT_TRANSLATE_NOOP("ajustes", "Transmisión"),
    QT_TRANSLATE_NOOP("ajustes", "Grabación"),
    QT_TRANSLATE_NOOP("ajustes", "Codificación"),
    QT_TRANSLATE_NOOP("ajustes", "Invitados"),
    QT_TRANSLATE_NOOP("ajustes", "Conferencia"),
    QT_TRANSLATE_NOOP("ajustes", "Sesión remota"),
    QT_TRANSLATE_NOOP("ajustes", "Pantallas"),
    QT_TRANSLATE_NOOP("ajustes", "Red de la conferencia"),
    QT_TRANSLATE_NOOP("ajustes", "Cloudflare"),
    QT_TRANSLATE_NOOP("ajustes", "Túnel propio (WireGuard)"),
    QT_TRANSLATE_NOOP("ajustes", "Pinggy"),
    QT_TRANSLATE_NOOP("ajustes", "Asistente (MCP)"),
    QT_TRANSLATE_NOOP("ajustes", "Modelos locales"),
    QT_TRANSLATE_NOOP("ajustes", "Apariencia"),
    QT_TRANSLATE_NOOP("ajustes", "Sistema y datos"),
    QT_TRANSLATE_NOOP("ajustes", "Audio"),
    QT_TRANSLATE_NOOP("ajustes", "Botonera y superficies"),
    QT_TRANSLATE_NOOP("ajustes", "Caras"),
    QT_TRANSLATE_NOOP("ajustes", "Dispositivos y drivers"),
    QT_TRANSLATE_NOOP("ajustes", "Página del evento"),
    QT_TRANSLATE_NOOP("ajustes", "Plugins y descargas"),
    QT_TRANSLATE_NOOP("ajustes", "Teleprompter"),
    QT_TRANSLATE_NOOP("ajustes", "Tótems de registro"),
    QT_TRANSLATE_NOOP("ajustes", "Traducción"),
    QT_TRANSLATE_NOOP("ajustes", "Voz del sistema"),
    QT_TRANSLATE_NOOP("ajustes", "Control del sistema"), // propia de la página (SettingsPage.qml)
};

QVariantList choices(std::initializer_list<std::pair<const char*, QString>> items) {
    QVariantList out;
    for (const auto& [v, l] : items) {
        out << QVariantMap{{"value", QString(v)}, {"label", l}};
    }
    return out;
}

/// Los idiomas que hay (ba::i18n), cada uno con su nombre en su idioma y, si le falta
/// traducción, cuánto tiene. "" = el del sistema.
QVariantList opcionesIdioma() {
    namespace i18n = ba::i18n;
    const std::string sistema = i18n::elegirDelSistema(ba::platform::idiomasDelSistema());
    QVariantList out{QVariantMap{
        {"value", QString()},
        {"label", qs(i18n::llenar(i18n::traducir("ajustes", "Automático (el del sistema: %1)"),
                                  {i18n::perfil(sistema).nombre}))}}};
    for (const auto& p : i18n::idiomas()) {
        std::string label = p.nombre;
        if (const double f = i18n::cobertura(p.codigo).fraccion(); f < 1.0)
            label += i18n::llenar(i18n::traducir("ajustes", " · %1 % traducido"),
                                  {std::to_string(static_cast<int>(f * 100))});
        out << QVariantMap{{"value", qs(p.codigo)}, {"label", qs(label)}};
    }
    return out;
}

/// «Idioma», y también en inglés si no es el inglés: quien lo cambió sin querer a uno
/// que no lee lo encuentra igual.
QString etiquetaIdioma() {
    const QString t = qs(ba::i18n::traducir("ajustes", "Idioma"));
    return t == "Language" ? t : t + " · Language";
}

/// Dónde escucha el gateway: esta máquina, toda la red local, o UNA interfaz
/// (una de las IPv4 que tiene la máquina ahora; la guardada aunque no esté).
QVariantList opcionesTemaWeb() {
    QVariantList out;
    for (const auto& t : control::temasWeb())
        out << QVariantMap{{"value", t.id}, {"label", QCoreApplication::translate("control-web", t.nombre)}};
    return out;
}

QVariantList opcionesBind(const std::string& actual) {
    QVariantList out = choices({{"127.0.0.1", traducir("ajustes", "Solo esta máquina (vía túnel)")},
                                {"0.0.0.0", traducir("ajustes", "Red local, todas las interfaces (también sin túnel)")}});
    bool esta = actual.empty() || actual == "127.0.0.1" || actual == "0.0.0.0";
    for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLinkLocal()) continue;
            const QString ip = a.toString();
            esta = esta || ip.toStdString() == actual;
            out << QVariantMap{{"value", ip}, {"label", traducir("ajustes", "Solo %1 (%2)").arg(ip, i.humanReadableName())}};
        }
    }
    if (!esta) out << QVariantMap{{"value", qs(actual)}, {"label", traducir("ajustes", "%1 (esta máquina ya no la tiene)").arg(qs(actual))}};
    return out;
}

/// Lo que va en el QR y el enlace: automática, las IPv4 de la máquina (con su
/// interfaz) o el nombre del equipo. También se escribe a mano.
QVariantList opcionesUrlHost(const std::string& actual) {
    QVariantList out = choices({{"", traducir("ajustes", "Automática (la de la red)")}});
    for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLinkLocal()) continue;
            out << QVariantMap{{"value", a.toString()}, {"label", a.toString() + " (" + i.humanReadableName() + ")"}};
        }
    }
    const QString equipo = QHostInfo::localHostName();
    if (!equipo.isEmpty()) out << QVariantMap{{"value", equipo}, {"label", traducir("ajustes", "%1 (el nombre del equipo, si la red lo resuelve)").arg(equipo)}};
    (void)actual;
    return out;
}

} // namespace

SettingsModel::SettingsModel(AppRuntime& rt, QObject* parent)
    : QAbstractListModel(parent), rt_(rt), idiomaAlArrancar_(rt.launcher().idioma) {
    // Codificadores: los alias que se ofrecen (cuál hay de verdad en esta máquina lo
    // decide el motor al armar cada salida: motor/Salidas.cpp, elegirCodificador).
    QVariantList encoders = choices({{"auto", traducir("ajustes", "Automatico (hardware si hay)")},
                                     {"vaapi", "VAAPI (AMD / Intel, Linux)"},
                                     {"nvenc", "NVENC (NVIDIA)"},
                                     {"qsv", "Quick Sync (Intel)"},
                                     {"amf", "AMF (AMD, Windows)"},
                                     {"videotoolbox", "VideoToolbox (macOS)"},
                                     {"x264", traducir("ajustes", "x264 (software)")}});

    // clang-format off
    fields_ = {
        // --- Video -----------------------------------------------------------------
        {"video.width", "Video", traducir("ajustes", "Ancho del lienzo"), "int", {}, 320, 7680, "px", traducir("ajustes", "Resolución de composición."), "motor", 1920},
        {"video.height", "Video", traducir("ajustes", "Alto del lienzo"), "int", {}, 240, 4320, "px", "", "motor", 1080},
        {"video.outputWidth", "Video", traducir("ajustes", "Ancho de salida"), "int", {}, 320, 7680, "px", traducir("ajustes", "Resolución que se codifica (se escala desde el lienzo)."), "motor", 1920},
        {"video.outputHeight", "Video", traducir("ajustes", "Alto de salida"), "int", {}, 240, 4320, "px", "", "motor", 1080},
        {"video.fps", "Video", traducir("ajustes", "Cuadros por segundo"), "choice", choices({{"24000/1001","23,976"},{"24","24"},{"25","25"},{"30000/1001","29,97"},{"30","30"},{"50","50"},{"60000/1001","59,94"},{"60","60"}}), 0, 0, "fps", traducir("ajustes", "29,97 / 59,94 / 23,976: las cadencias NTSC (America, Japón)."), "motor", "30"},
        {"video.formato", "Video", traducir("ajustes", "Formato de salida"), "choice", choices({{"nv12", traducir("ajustes", "NV12 (8 bits 4:2:0, el de siempre)")},{"i420", traducir("ajustes", "I420 (8 bits 4:2:0)")},{"i444", traducir("ajustes", "I444 (8 bits 4:4:4)")},{"p010", traducir("ajustes", "P010 (10 bits 4:2:0, HDR)")},{"i010", traducir("ajustes", "I010 (10 bits 4:2:0)")}}), 0, 0, "", traducir("ajustes", "Lo que reciben los codificadores. HDR necesita 10 bits (y un codificador que lo acepte: HEVC/AV1)."), "motor", "nv12"},
        {"video.color", "Video", traducir("ajustes", "Espacio de color"), "choice", choices({{"709","Rec. 709 (HD)"},{"601","Rec. 601 (SD)"},{"srgb","sRGB"},{"2100pq","Rec. 2100 PQ (HDR)"},{"2100hlg","Rec. 2100 HLG (HDR)"}}), 0, 0, "", traducir("ajustes", "Las entradas que se normalizan se llevan a este espacio de color."), "motor", "709"},
        {"video.rango", "Video", traducir("ajustes", "Rango"), "choice", choices({{"limitado", traducir("ajustes", "Limitado (16-235, broadcast)")},{"completo", traducir("ajustes", "Completo (0-255)")}}), 0, 0, "", "", "motor", "limitado"},
        {"transition.type", "Video", traducir("ajustes", "Transición"), "choice", choices({{"cut", traducir("ajustes", "Corte")},{"fade", traducir("ajustes", "Fundido")},{"swipe", traducir("ajustes", "Barrido")},{"slide", traducir("ajustes", "Deslizar")},{"fade_to_color", traducir("ajustes", "Fundido a color")}}), 0, 0, "", "", "live", "fade"},
        {"transition.ms", "Video", traducir("ajustes", "Duración de la transición"), "int", {}, 0, 5000, "ms", "", "live", 300},
        // --- Cámaras y entradas ----------------------------------------------------
        {"mejora.activa", "Cámaras y entradas", traducir("ajustes", "Mejorar imagen de las cámaras"), "bool", {}, 0, 0, "", traducir("ajustes", "Limpia el ruido, pega el color a los bordes (el LED del micrófono, la corbata) y afila, en la GPU y sin demora. Cada entrada puede seguir esto, forzarlo o apagarlo en Cámaras PTZ."), "live", false},
        {"mejora.ruido", "Cámaras y entradas", traducir("ajustes", "Mejorar imagen: ruido"), "int", {}, 0, 100, "%", traducir("ajustes", "Cuánto se mezcla con el cuadro anterior donde no hay movimiento."), "live", 60},
        {"mejora.color", "Cámaras y entradas", traducir("ajustes", "Mejorar imagen: color pegado al brillo"), "int", {}, 0, 100, "%", traducir("ajustes", "Saca el color corrido alrededor de los bordes."), "live", 70},
        {"mejora.nitidez", "Cámaras y entradas", traducir("ajustes", "Mejorar imagen: nitidez"), "int", {}, 0, 100, "%", traducir("ajustes", "Enfoque adaptativo (sin halos)."), "live", 50},
        {"video.legalizar", "Cámaras y entradas", traducir("ajustes", "Legalizar para TV"), "choice", choices({{"no", traducir("ajustes", "No")},{"pal", traducir("ajustes", "Sí, PAL (+133 %)")},{"ntsc", traducir("ajustes", "Sí, estricto (+120 %)")},{"rango", traducir("ajustes", "Solo el rango (luma y croma)")}}), 0, 0, "", traducir("ajustes", "Pasa cada entrada por el legalizador (balegal, en GStreamer): luma y croma en rango legal y, si se pasa la gama compuesta, menos saturación en ese punto. Para entregas a un canal."), "live", "no"},
        {"entradas.sinSenal", "Cámaras y entradas", traducir("ajustes", "Placa «sin señal» después de"), "int", {}, 200, 60000, "ms", traducir("ajustes", "Una entrada que deja de mandar cuadros muestra la placa «sin señal» pasado este tiempo. Más corto, se ve antes que se cortó; más largo, aguanta cortes chicos de la red sin parpadear. Rige al armar cada entrada."), "live", 3000},
        {"entradas.rearmarSinDatos", "Cámaras y entradas", traducir("ajustes", "Rearmar si no llegan datos"), "int", {}, 3, 600, "s", traducir("ajustes", "Una entrada que no da error pero tampoco manda nada se desarma y se vuelve a armar pasado este tiempo."), "live", 10},
        {"entradas.reintentoMax", "Cámaras y entradas", traducir("ajustes", "Espera máxima entre reintentos"), "int", {}, 200, 120000, "ms", traducir("ajustes", "Una entrada que no arma se reintenta a los 0,2, 0,5, 1 y 2 s y después cada vez más espaciado, hasta este tope. Con una cámara apagada mucho rato, un tope más alto no la martilla."), "live", 2000},
        {"entradas.placaReposo", "Cámaras y entradas", traducir("ajustes", "Placa de entradas sin uso"), "int", {}, 5, 3600, "s", traducir("ajustes", "Una entrada que ninguna escena usa desarma su placa («sin señal») pasado este tiempo, para no gastar hilos. Rearmarla es una negociación de la GPU: con poco tiempo, cada cambio de escena la rearma."), "live", 120},
        // --- Transmisión -----------------------------------------------------------
        {"stream.server", "Transmisión", traducir("ajustes", "Servidor RTMP"), "text", {}, 0, 0, "", traducir("ajustes", "p. ej. rtmp://a.rtmp.youtube.com/live2"), "live", ""},
        {"stream.keyEnv", "Transmisión", traducir("ajustes", "Variable con la clave"), "text", {}, 0, 0, "", traducir("ajustes", "La clave no se guarda en disco: se lee de esta variable de entorno o se escribe al transmitir."), "live", "BA_STREAM_KEY"},
        {"stream.reintentoMax", "Transmisión", traducir("ajustes", "Espera máxima entre reconexiones"), "int", {}, 1, 300, "s", traducir("ajustes", "Si se corta la transmisión, se reconecta a los 1, 2, 4, 8… s hasta este tope."), "live", 10},
        {"stream.onStart", "Transmisión", traducir("ajustes", "Transmitir al arrancar"), "bool", {}, 0, 0, "", "", "live", false},
        // --- Grabación -------------------------------------------------------------
        {"record.dir", "Grabación", traducir("ajustes", "Carpeta"), "path", {}, 0, 0, "", traducir("ajustes", "Relativa al bundle o absoluta."), "live", "Recordings"},
        {"record.container", "Grabación", traducir("ajustes", "Formato"), "choice", choices({{"mkv", traducir("ajustes", "MKV (sobrevive a un corte de energía)")},{"mp4","MP4"},{"mov","MOV"},{"flv","FLV"}}), 0, 0, "", traducir("ajustes", "MP4 y MOV se graban en MKV (un corte no los arruina) y se pasan al formato al terminar."), "live", "mkv"},
        {"record.remux", "Grabación", traducir("ajustes", "Pasar a MP4/MOV al terminar"), "bool", {}, 0, 0, "", traducir("ajustes", "Con MP4 o MOV elegidos: al detener, el MKV se pasa al formato sin recodificar y se borra si salió bien."), "live", true},
        {"record.onStart", "Grabación", traducir("ajustes", "Grabar al arrancar"), "bool", {}, 0, 0, "", traducir("ajustes", "Respaldo local automático."), "live", false},
        {"record.calidad", "Grabación", traducir("ajustes", "Calidad del archivo"), "choice", choices({{"como_stream", traducir("ajustes", "Como la transmisión (no cuesta otro encode)")},{"archivo", traducir("ajustes", "Calidad de archivo (para editar; un encode más)")}}), 0, 0, "", traducir("ajustes", "«Calidad de archivo» usa su propio codificador a calidad constante: el archivo sale mejor que lo que se transmite."), "live", "como_stream"},
        {"record.crf", "Grabación", traducir("ajustes", "Calidad constante (CRF)"), "int", {}, 10, 35, "", traducir("ajustes", "Con «Calidad de archivo»: 14 casi sin pérdida, 18 muy buena, 23 normal, 28 chico."), "live", 18},
        {"record.pistas", "Grabación", traducir("ajustes", "Pistas de audio extra"), "text", {}, 0, 0, "", traducir("ajustes", "Números de pista de la Matriz (2 a 6) separados por coma: cada una se graba aparte con lo que le cruces en la Matriz. La 1 (el programa) va siempre."), "live", ""},
        {"record.copiaDir", "Grabación", traducir("ajustes", "Copia en otro disco"), "path", {}, 0, 0, "", traducir("ajustes", "Una segunda grabación igual, en otra carpeta (otro disco). Vacío = sin copia."), "live", ""},
        {"record.colchon", "Grabación", traducir("ajustes", "Colchón si el disco se traba"), "int", {}, 5, 600, "s", traducir("ajustes", "Lo que la grabación del programa junta en memoria mientras el disco no escribe (un disco USB que se reinicia, por ejemplo). Más segundos, más memoria (a 6 Mbps, un minuto son ~45 MB; hay un tope de 512 MB)."), "live", 60},
        {"disco.prioridad", "Grabación", traducir("ajustes", "Si el disco no da abasto"), "choice", choices({{"preguntar", traducir("ajustes", "Preguntar si le doy el disco a la grabación del programa")},{"auto", traducir("ajustes", "Dárselo solo (pausa ISO, repetición, copia y registro)")},{"nunca", traducir("ajustes", "No hacer nada (solo avisar)")}}), 0, 0, "", traducir("ajustes", "Cuando lo grabado espera para entrar al disco: se puede pausar todo lo demás que escribe para que la grabación del programa no pierda nada. Todo sigue solo cuando el disco vuelve a andar bien."), "live", "preguntar"},
        {"disco.apuros", "Grabación", traducir("ajustes", "El disco está en apuros con"), "int", {}, 1, 60, "s", traducir("ajustes", "Segundos que lo grabado espera para entrar al disco (o el registro trabado escribiendo) hasta avisar y, según lo de arriba, darle el disco al programa."), "live", 3},
        {"disco.recuperado", "Grabación", traducir("ajustes", "Vuelve a la normalidad después de"), "int", {}, 5, 3600, "s", traducir("ajustes", "Segundos con el disco al día hasta que se retoma lo pausado."), "live", 30},
        {"record.replay", "Grabación", traducir("ajustes", "Repetición: últimos segundos"), "int", {}, 0, 600, "s", traducir("ajustes", "Guarda en memoria los últimos N segundos del programa para el botón «Repetir». 0 = apagada."), "live", 0},
        // --- Codificación (transmisión y grabación) --------------------------------
        {"encoder.video", "Codificación", traducir("ajustes", "Codificador de video"), "choice", encoders, 0, 0, "", traducir("ajustes", "Si el elegido no está o falla al arrancar, se usa el siguiente disponible y al final x264."), "live", "auto"},
        {"encoder.bitrate", "Codificación", traducir("ajustes", "Bitrate de video"), "int", {}, 500, 50000, "kbps", "", "live", 6000},
        {"encoder.rateControl", "Codificación", traducir("ajustes", "Control de tasa"), "choice", choices({{"CBR", traducir("ajustes", "CBR (constante, para transmitir)")},{"VBR", traducir("ajustes", "VBR (variable)")},{"CQP", traducir("ajustes", "CQP (calidad fija, para grabar)")}}), 0, 0, "", "", "live", "CBR"},
        {"encoder.keyint", "Codificación", traducir("ajustes", "Intervalo de keyframe"), "int", {}, 1, 10, "s", traducir("ajustes", "Las plataformas de streaming piden 2 s."), "live", 2},
        {"encoder.preset", "Codificación", traducir("ajustes", "Preset de x264"), "choice", choices({{"ultrafast","ultrafast"},{"superfast","superfast"},{"veryfast","veryfast"},{"faster","faster"},{"fast","fast"},{"medium","medium"}}), 0, 0, "", traducir("ajustes", "Solo aplica a x264: más lento = mejor calidad y más CPU."), "live", "veryfast"},
        {"encoder.audioBitrate", "Codificación", traducir("ajustes", "Bitrate de audio"), "choice", choices({{"96","96"},{"128","128"},{"160","160"},{"192","192"},{"256","256"},{"320","320"}}), 0, 0, "kbps", "", "live", "160"},
        // --- Invitados -------------------------------------------------------------
        {"guests.count", "Invitados", traducir("ajustes", "Cantidad de invitados"), "int", {}, 0, ba::ipc::kMaxInvitados, "",
         traducir("ajustes", "Lugares en el programa. Cada invitado es una conexión de video que hay que decodificar: mirá el panel «Uso de la conferencia» antes de subirlo mucho. Cambia en caliente (hasta %1), sin cortar a nadie.").arg(ba::ipc::kMaxInvitados),
         "live", 2},
        {"guests.reserva", "Invitados", traducir("ajustes", "Lugares reservados"), "int", {}, 1, 16, "", traducir("ajustes", "Los lugares que la conferencia deja listos al arrancar (sus canales de video y audio). Ya no limita el cupo: los que faltan se crean cuando el cupo sube. Cambiarlo reinicia el gateway."), "gateway", 4},
        {"guests.video", "Invitados", traducir("ajustes", "Video de invitados"), "bool", {}, 0, 0, "", "", "restart", true},
        {"guests.audio", "Invitados", traducir("ajustes", "Audio de invitados"), "bool", {}, 0, 0, "", "", "restart", true},
        {"guests.audioPreset", "Invitados", traducir("ajustes", "Procesamiento de voz"), "choice", choices({{"podium", traducir("ajustes", "Podio")},{"lavalier", traducir("ajustes", "Corbatero")},{"handheld", traducir("ajustes", "De mano")},{"", traducir("ajustes", "Sin procesar")}}), 0, 0, "", traducir("ajustes", "Channel strip nativo: HPF, EQ, de-esser, gate, compresor y limitador."), "restart", "podium"},
        {"gateway.sinVideoPlaca", "Invitados", traducir("ajustes", "Placa si un invitado no manda video"), "int", {}, 200, 30000, "ms", traducir("ajustes", "El lugar de un invitado que deja de mandar video muestra su placa pasado este tiempo."), "gateway", 1000},
        {"gateway.videoW", "Invitados", traducir("ajustes", "Video del invitado: ancho"), "int", {}, 320, 3840, "px", "", "gateway", 1280},
        {"gateway.videoH", "Invitados", traducir("ajustes", "Video del invitado: alto"), "int", {}, 180, 2160, "px", "", "gateway", 720},
        {"gateway.returnW", "Invitados", traducir("ajustes", "Retorno al invitado: ancho"), "int", {}, 320, 1920, "px", "", "gateway", 640},
        {"gateway.returnH", "Invitados", traducir("ajustes", "Retorno al invitado: alto"), "int", {}, 180, 1080, "px", "", "gateway", 360},
        // --- Conferencia -----------------------------------------------------------
        {"gateway.room", "Conferencia", traducir("ajustes", "Nombre de la conferencia"), "text", {}, 0, 0, "", traducir("ajustes", "Nombre de la conferencia a la que entran los invitados."), "gateway", "sala1"},
        {"room.lobby", "Conferencia", traducir("ajustes", "Lobby de admisión"), "bool", {}, 0, 0, "", traducir("ajustes", "Cada invitado espera a que lo hagas pasar (y mientras espera solo te ve a vos)."), "live", true},
        {"room.lobbySlots", "Conferencia", traducir("ajustes", "Previews del lobby"), "int", {}, 0, 8, "", traducir("ajustes", "Cuántos de los que esperan pueden mostrarte su cámara a la vez."), "gateway", 4},
        {"room.maxBackstage", "Conferencia", traducir("ajustes", "Fila de oradores"), "int", {}, 0, 32, "", traducir("ajustes", "Modo oradores: cuántos en la fila pueden ver el programa a la vez (cada uno usa una sesión de video)."), "gateway", 12},
        {"room.backstagePreviews", "Conferencia", traducir("ajustes", "Previews de la fila"), "int", {}, 0, 8, "", traducir("ajustes", "Modo oradores: cuántos de la fila (los primeros) te muestran su cámara en la app."), "gateway", 6},
        {"room.vistasRemotas", "Sesión remota", traducir("ajustes", "Integrantes mirando a la vez"), "int", {}, 1, 64, "", traducir("ajustes", "Sesión remota: cuántos integrantes pueden mirar un canal a la vez en su celular (cada uno es una conexión de video)."), "gateway", 16},
        {"gateway.programaW", "Pantallas", traducir("ajustes", "Retorno a las pantallas: ancho"), "int", {}, 320, 3840, "px", traducir("ajustes", "Resolución del programa que va a las pantallas de la sala y a los celulares."), "gateway", 1280},
        {"gateway.programaH", "Pantallas", traducir("ajustes", "Retorno a las pantallas: alto"), "int", {}, 180, 2160, "px", "", "gateway", 720},
        {"room.pinIntentos", "Conferencia", traducir("ajustes", "PIN: intentos por dirección"), "int", {}, 1, 100, "", traducir("ajustes", "PIN mal tantas veces desde una misma dirección: se la bloquea un rato."), "live", 5},
        {"room.pinBloqueo", "Conferencia", traducir("ajustes", "PIN: bloqueo"), "int", {}, 5, 86400, "s", traducir("ajustes", "Cuánto queda bloqueada esa dirección."), "live", 60},
        {"room.maxUploadMB", "Conferencia", traducir("ajustes", "Tope por archivo compartido"), "int", {}, 1, 20480, "MB", "", "live", 2048},
        // --- Red de la conferencia -------------------------------------------------
        {"gateway.port", "Red de la conferencia", traducir("ajustes", "Puerto de la conferencia"), "int", {}, 1024, 65535, "", "", "gateway", 8787},
        {"gateway.bind", "Red de la conferencia", traducir("ajustes", "Acceso (dónde escucha)"), "choice", opcionesBind(rt.gateway().bindAddress), 0, 0, "", traducir("ajustes", "Red local expone el gateway a la LAN; una IP sola, solo a esa interfaz (la página y la conexión de los invitados). El PIN y el límite de intentos siguen protegiendo. Al aplicar se reinicia solo la conferencia."), "gateway", "127.0.0.1"},
        {"gateway.urlHost", "Red de la conferencia", traducir("ajustes", "Dirección para el QR y el enlace"), "combo", opcionesUrlHost(rt.gateway().urlHost), 0, 0, "", traducir("ajustes", "Lo que va en el QR de la conferencia y en el enlace para los invitados de la red local: elegila de la lista (las IP de esta máquina) o escribila (una IP o un nombre). Vacía, la elige sola (la de la interfaz de «Acceso», o la de la red). Si «Acceso» está en «Solo esta máquina», pasa a «Red local» para que se pueda entrar. Al aplicar se reinicia solo la conferencia."), "gateway", ""},
        {"direct.https", "Red de la conferencia", traducir("ajustes", "HTTPS propio"), "bool", {}, 0, 0, "", traducir("ajustes", "Los navegadores solo dan cámara y micrófono en paginas seguras: sin HTTPS, un invitado por la red local o por IP pública no puede usarlos."), "gateway", true},
        {"direct.httpsPort", "Red de la conferencia", traducir("ajustes", "Puerto HTTPS"), "int", {}, 1024, 65535, "", traducir("ajustes", "Un certificado propio evita el aviso del navegador: deja cert.pem + key.pem en Credenciales/ y al reiniciar se importan (cifrados)."), "gateway", 8789},
        {"direct.rtcMin", "Red de la conferencia", traducir("ajustes", "WebRTC: primer puerto UDP"), "int", {}, 1024, 65000, "", traducir("ajustes", "Rango fijo para el firewall y la redirección del servidor."), "gateway", 40000},
        {"direct.rtcMax", "Red de la conferencia", traducir("ajustes", "WebRTC: último puerto UDP"), "int", {}, 1024, 65535, "", "", "gateway", 40099},
        {"room.adminPort", "Red de la conferencia", traducir("ajustes", "Puerto de la API de control"), "int", {}, 1024, 65535, "", traducir("ajustes", "Solo en 127.0.0.1: la usa esta app."), "gateway", 8788},
        // --- Acceso remoto ---------------------------------------------------------
        {"gateway.tunnel", "Cloudflare", traducir("ajustes", "Túnel de Cloudflare"), "bool", {}, 0, 0, "", traducir("ajustes", "Saca la conferencia a internet por un túnel de Cloudflare (cloudflared). Necesita el token del túnel (acá arriba)."), "gateway", false},
        {"gateway.tunnelHost", "Cloudflare", traducir("ajustes", "Dominio público (Cloudflare)"), "text", {}, 0, 0, "", traducir("ajustes", "El hostname que apunta al túnel en el panel de Cloudflare (p. ej. sala.midominio.com). Lo usa el QR remoto."), "gateway", ""},
        {"wg.enabled", "Túnel propio (WireGuard)", traducir("ajustes", "Túnel WireGuard"), "bool", {}, 0, 0, "", traducir("ajustes", "Conecta a un servidor propio con IP pública: los invitados entran directo, sin Cloudflare. Sin permisos de administrador ni drivers."), "gateway", false},
        {"wg.endpoint", "Túnel propio (WireGuard)", traducir("ajustes", "Servidor WireGuard"), "text", {}, 0, 0, "", traducir("ajustes", "host:puerto (p. ej. vps.midominio.com:51820)."), "gateway", ""},
        {"wg.serverKey", "Túnel propio (WireGuard)", traducir("ajustes", "Clave pública del servidor"), "text", {}, 0, 0, "", traducir("ajustes", "La que muestra 'wg show' en el servidor (base64)."), "gateway", ""},
        {"wg.address", "Túnel propio (WireGuard)", traducir("ajustes", "Dirección en el túnel"), "text", {}, 0, 0, "", traducir("ajustes", "La que el servidor le asigna a este equipo (AllowedIPs)."), "gateway", "10.99.0.2/24"},
        {"wg.publicAddress", "Túnel propio (WireGuard)", traducir("ajustes", "IP o dominio público"), "text", {}, 0, 0, "", traducir("ajustes", "Por donde entran los invitados; también es la que se anuncia para WebRTC."), "gateway", ""},
        {"gateway.metricsPort", "Cloudflare", traducir("ajustes", "Puerto de métricas del túnel"), "int", {}, 1024, 65535, "", traducir("ajustes", "Lo usa cloudflared en esta máquina (127.0.0.1) para informar cómo anda el túnel."), "gateway", 9099},
        {"wg.httpPort", "Túnel propio (WireGuard)", traducir("ajustes", "WireGuard: puerto web que redirige el servidor"), "int", {}, 1, 65535, "", "", "gateway", 80},
        {"wg.httpsPort", "Túnel propio (WireGuard)", traducir("ajustes", "WireGuard: puerto seguro que redirige el servidor"), "int", {}, 1, 65535, "", "", "gateway", 443},
        {"wg.keepalive", "Túnel propio (WireGuard)", traducir("ajustes", "Keepalive"), "int", {}, 0, 120, "s", traducir("ajustes", "Mantiene abierto el NAT del lado de este equipo."), "gateway", 25},
        {"pinggy.activo", "Pinggy", traducir("ajustes", "Túnel de pinggy"), "bool", {}, 0, 0, "", traducir("ajustes", "Saca todas las páginas de la conferencia (invitados, sesión remota, público, botonera) por una sola dirección pública de pinggy.io, sin abrir puertos ni tener IP pública. Necesita el token de túnel guardado acá abajo. La API de control y el MCP nunca salen por el túnel."), "gateway", false},
        {"pinggy.region", "Pinggy", traducir("ajustes", "Región del servidor"), "choice", choices({{"auto", traducir("ajustes", "Automática (la más cercana)")},{"us", traducir("ajustes", "Estados Unidos")},{"eu", traducir("ajustes", "Europa")},{"ap", traducir("ajustes", "Asia-Pacífico")}}), 0, 0, "", traducir("ajustes", "Por dónde pasa el tráfico. La automática elige la de menos demora; si se corta seguido, probá con Estados Unidos."), "gateway", "auto"},
        {"pinggy.dominioTipo", "Pinggy", traducir("ajustes", "Dirección pública"), "choice", choices({{"azar", traducir("ajustes", "La que dé pinggy (al azar, cambia cada vez)")},{"pinggy", traducir("ajustes", "Subdominio fijo de pinggy")},{"propio", traducir("ajustes", "Dominio propio")}}), 0, 0, "", traducir("ajustes", "Al azar: anda sin configurar nada, pero el enlace (y el QR) cambia cada vez que se conecta. Subdominio fijo (plan Pro): el que pinggy le asigna al token en dashboard.pinggy.io → Domains. Dominio propio (plan Pro): un nombre de la organización (p. ej. sala.miorganismo.gob.ar) cargado en el panel de pinggy y apuntado con un CNAME."), "gateway", "azar"},
        {"pinggy.host", "Pinggy", traducir("ajustes", "Subdominio o dominio"), "text", {}, 0, 0, "", traducir("ajustes", "Tal cual figura en el panel de pinggy: p. ej. salaconcejo.a.pinggy.link (subdominio fijo) o sala.miorganismo.gob.ar (dominio propio). Sin https:// ni barras."), "gateway", ""},
        {"pinggy.cnameDestino", "Pinggy", traducir("ajustes", "Destino del CNAME"), "text", {}, 0, 0, "", traducir("ajustes", "Lo muestra el panel de pinggy al cargar el dominio propio, del estilo <id>.a.pinggy.link. Es a donde tiene que apuntar el registro CNAME del dominio (las instrucciones para el que administra el DNS están acá abajo)."), "gateway", ""},
        {"pinggy.turn", "Pinggy", traducir("ajustes", "TURN por el túnel (video de los invitados de afuera)"), "bool", {}, 0, 0, "", traducir("ajustes", "El audio y el video de los invitados que entran por internet viajan por el túnel (un servidor TURN propio dentro de este equipo). Apagado, solo pasan las páginas: el video de los de afuera puede no conectar."), "gateway", true},
        {"pinggy.turnTcpPuerto", "Pinggy", traducir("ajustes", "TURN: puerto TCP fijo"), "int", {}, 0, 65535, "", traducir("ajustes", "Con plan Pro se puede reservar un puerto TCP fijo en pinggy. 0 = el que dé pinggy."), "gateway", 0},
        {"pinggy.turnUdp", "Pinggy", traducir("ajustes", "TURN también por UDP"), "bool", {}, 0, 0, "", traducir("ajustes", "Menos demora que por TCP, si la red del invitado lo deja pasar. Necesita un puerto UDP de pinggy (Pro)."), "gateway", false},
        {"pinggy.turnUdpPuerto", "Pinggy", traducir("ajustes", "TURN: puerto UDP fijo"), "int", {}, 0, 65535, "", traducir("ajustes", "0 = el que dé pinggy."), "gateway", 0},
        {"pinggy.listaBlanca", "Pinggy", traducir("ajustes", "Solo desde estas direcciones"), "text", {}, 0, 0, "", traducir("ajustes", "IPs o redes separadas por coma (p. ej. 200.45.10.0/24, 181.1.2.3). Vacío: desde cualquier lado (el PIN sigue protegiendo)."), "gateway", ""},
        {"pinggy.puertoTunel", "Pinggy", traducir("ajustes", "Puerto local del túnel"), "int", {}, 1024, 65535, "", traducir("ajustes", "Lo que entra por pinggy llega a este puerto de esta máquina (solo 127.0.0.1) y se marca como «de afuera». Cambialo solo si otro programa lo usa."), "gateway", 8786},
        // --- Asistente de IA (MCP) -------------------------------------------------
        {"mcp.enabled", "Asistente (MCP)", traducir("ajustes", "Servidor MCP"), "bool", {}, 0, 0, "", traducir("ajustes", "Permite que un asistente de IA (Claude u otro cliente MCP) maneje y vea toda la app. Solo en esta máquina y con token (cifrado en Credenciales/; bin/64bit/ba-mcp --token lo muestra)."), "restart", true},
        {"mcp.port", "Asistente (MCP)", traducir("ajustes", "Puerto MCP"), "int", {}, 1024, 65535, "", traducir("ajustes", "http://127.0.0.1:<puerto>/mcp. Para clientes que lanzan un proceso: bin/64bit/ba-mcp."), "restart", 8790},
        {"mcp.approval", "Asistente (MCP)", traducir("ajustes", "Aprobar acciones del asistente"), "choice", choices({{"invitados", traducir("ajustes", "Si leyó mensajes/datos de invitados o del público (recomendado)")},{"siempre", traducir("ajustes", "Siempre")},{"nunca", traducir("ajustes", "Nunca: aprobación automática (bajo tu responsabilidad)")}}), 0, 0, "", traducir("ajustes", "Salvaguarda determinista: cuando el asistente leyo algo escrito por invitados o por el publico (chat, nombres, archivos, su video), toda accion que modifica algo espera tu click de aprobacion en la barra de abajo. Asi un invitado no puede hacer que el asistente cambie nada. Con «Nunca» se aprueba todo solo: un invitado podria escribir en el chat algo que el asistente termine haciendo. Solo se cambia desde aca (el asistente no puede)."), "live", "invitados"},
        {"mcp.aprobacionMin", "Asistente (MCP)", traducir("ajustes", "Una aprobación espera hasta"), "int", {}, 1, 240, "min", traducir("ajustes", "Lo que el asistente pide aprobar se rechaza solo si nadie contesta en este tiempo."), "live", 10},
        // --- Modelos locales (el chat sin internet) ----------------------------------
        {"modelo.contexto", "Modelos locales", traducir("ajustes", "Memoria de la charla (contexto)"), "int", {}, 2048, 262144, traducir("ajustes", "tokens"), traducir("ajustes", "Contexto del modelo local de chat (pestaña Asistente, «modelo local»). Más, recuerda más de la charla y usa más memoria de la placa. Rige al volver a arrancarlo."), "live", 32768},
        {"modelo.capasGpu", "Modelos locales", traducir("ajustes", "Capas en la GPU"), "int", {}, 0, 999, "", traducir("ajustes", "999 = todas (lo más rápido). Si la placa se queda sin memoria, bajalo; 0 = solo la CPU. Rige al volver a arrancarlo."), "live", 999},
        // --- Apariencia ------------------------------------------------------------
        {"ui.previewWidth", "Apariencia", traducir("ajustes", "Ancho de las vistas previas"), "int", {}, 320, 3840, "px", traducir("ajustes", "El tamaño con que se dibujan las vistas previas del programa y las escenas. Más grande se ve mejor y usa más la placa."), "restart", 960},
        {"ui.theme", "Apariencia", traducir("ajustes", "Tema"), "choice", choices({{"dark", traducir("ajustes", "Oscuro")},{"light", traducir("ajustes", "Claro")}}), 0, 0, "", "", "live", "dark"},
        {"web.tema", "Apariencia", traducir("ajustes", "Tema de las interfaces web"), "choice", opcionesTemaWeb(), 0, 0, "", traducir("ajustes", "El de todas las páginas de control (dashboard, botonera, presidencia, rótulos, corrección): cambia en todos los aparatos sin recargar. También en Servicios → Templates."), "live", "neuro-oscuro"},
        // --- Sistema y datos -------------------------------------------------------
        {"launcher.idioma", "Idioma", etiquetaIdioma(), "choice", opcionesIdioma(), 0, 0, "", traducir("ajustes", "El del programa: la interfaz, los avisos, el registro y el asistente. El de cada evento (lo que ven y escuchan el público y los invitados) se elige en el evento. Cambia al reiniciar el programa."), "restart", QString()},
        {"launcher.shadow", "Sistema y datos", traducir("ajustes", "Copia sombra"), "bool", {}, 0, 0, "", traducir("ajustes", "Ejecutar desde una copia en Cache/ del propio bundle (permite actualizar el bundle en caliente)."), "restart", false},
        {"launcher.cuelgue", "Sistema y datos", traducir("ajustes", "Colgado si no responde"), "int", {}, 2, 120, "s", traducir("ajustes", "Si el programa (o la conferencia) deja de latir este tiempo, se guarda un volcado y se reinicia solo. Más corto, vuelve antes; más largo, aguanta trabas largas sin reiniciar. Rige al volver a abrir el programa."), "restart", 5},
        {"launcher.graciaArranque", "Sistema y datos", traducir("ajustes", "Gracia al arrancar"), "int", {}, 5, 600, "s", traducir("ajustes", "Recién abierto, el programa todavía no late: no se lo vigila durante este tiempo."), "restart", 20},
        {"launcher.maxCaidas", "Sistema y datos", traducir("ajustes", "Caídas antes del modo seguro"), "int", {}, 1, 100, "", traducir("ajustes", "Si se cae más veces que esto en la ventana de abajo, arranca en modo seguro (sin restaurar lo que estaba andando)."), "restart", 5},
        {"launcher.ventanaCaidas", "Sistema y datos", traducir("ajustes", "Ventana de caídas"), "int", {}, 1, 120, "min", "", "restart", 2},
        {"launcher.reiniciosSinPreguntar", "Sistema y datos", traducir("ajustes", "Reinicios sin preguntar"), "int", {}, 0, 100, "", traducir("ajustes", "Con el diálogo de recuperación prendido: cuántas fallas seguidas se reinician solas antes de preguntar."), "restart", 2},
        {"launcher.recoveryUi", "Sistema y datos", traducir("ajustes", "Diálogo de recuperación"), "bool", {}, 0, 0, "", traducir("ajustes", "Si está apagado, el supervisor reinicia solo sin preguntar."), "restart", true},
    };
    // clang-format on
    // Los avanzados (antes valores fijos en el código): al final, cada uno en su sección,
    // del registro central (ajustes/Registro.hpp). Su tabla marca los textos
    // (QT_TRANSLATE_NOOP): se traducen acá. Lo que no se marcó (unidades, siglas) no pasa
    // por la traducción.
    const auto deTabla = [](const QString& t) {
        return t.isEmpty() ? t : traducir("ajustes", t.toUtf8().constData());
    };
    static const QRegularExpression palabra("[a-záéíóúñ]{5}");
    const auto marcado = [](const QString& t) { return t.contains(palabra); };
    const auto u = [](const char* t) { return QString::fromUtf8(t); };
    for (const ba::ajustes::Definicion& a : ba::ajustes::registro()) {
        QVariantList opciones;
        for (const auto& [v, l] : a.opciones)
            opciones << QVariantMap{{"value", u(v)}, {"label", u(v) != u(l) && marcado(u(l)) ? deTabla(u(l)) : u(l)}};
        const QString sufijo = marcado(u(a.sufijo)) ? deTabla(u(a.sufijo)) : u(a.sufijo);
        const QVariant def = a.defecto.is_boolean()  ? QVariant(a.defecto.get<bool>())
                             : a.defecto.is_string() ? QVariant(qs(a.defecto.get<std::string>()))
                             : std::string(a.tipo) == "float" ? QVariant(a.defecto.get<double>())
                                                              : QVariant(a.defecto.get<int>());
        Field f{u(a.clave), u(a.seccion), deTabla(u(a.etiqueta)), u(a.tipo), opciones, a.min, a.max,
                sufijo, deTabla(u(a.ayuda)), u(a.alcance), def};
        f.avanzado = true;
        f.destino = static_cast<int>(a.destino);
        fields_.push_back(std::move(f));
    }
    load();
    // Otro formulario (el del MCP, el de la GUI) aplicó la configuración: este,
    // si no tiene cambios a medio hacer, muestra lo vigente.
    observador_ = rt_.observar([this](AppRuntime::Cambio c) {
        if (c != AppRuntime::Cambio::Ajustes || aplicando_) return;
        QMetaObject::invokeMethod(this, [this]() { traerDeAfuera(); }, Qt::QueuedConnection);
    });
}

SettingsModel::~SettingsModel() { rt_.dejarDeObservar(observador_); }

QVariantMap SettingsModel::current() const {
    QVariantMap m = currentBasicos();
    // Los avanzados: lo guardado en su archivo ("ajustes") o su valor de siempre.
    const AppConfig& c = rt_.config();
    const json* donde[3] = {&c.ajustes, &const_cast<AppRuntime&>(rt_).gateway().ajustes,
                            &const_cast<AppRuntime&>(rt_).launcher().ajustes};
    for (const Field& f : fields_) {
        if (!f.avanzado) continue;
        const json& j = *donde[std::clamp(f.destino, 0, 2)];
        const auto it = j.find(f.key.toStdString());
        if (it == j.end()) m[f.key] = f.def;
        else if (it->is_string()) m[f.key] = QString::fromStdString(it->get<std::string>());
        else if (it->is_boolean()) m[f.key] = it->get<bool>();
        else if (f.kind == "float") m[f.key] = it->get<double>();
        else if (it->is_number()) m[f.key] = static_cast<int>(it->get<double>());
        else m[f.key] = f.def;
    }
    return m;
}

bool SettingsModel::idiomaPendiente() const {
    // Cambió desde que arrancó (no «es otro que el que se usa»: BA_IDIOMA puede pisarlo).
    return const_cast<AppRuntime&>(rt_).launcher().idioma != idiomaAlArrancar_;
}

QVariantMap SettingsModel::currentBasicos() const {
    const AppConfig& c = rt_.config();
    const GatewaySettings& g = const_cast<AppRuntime&>(rt_).gateway();
    const LauncherSettings& l = const_cast<AppRuntime&>(rt_).launcher();
    return {
        {"video.width", c.video.baseWidth},
        {"video.height", c.video.baseHeight},
        {"video.outputWidth", c.video.outputWidth},
        {"video.outputHeight", c.video.outputHeight},
        {"video.fps", qs(textoDeFps(c.video.fpsNum, c.video.fpsDen))},
        {"video.formato", qs(c.video.formato)},
        {"video.color", qs(c.video.color)},
        {"video.rango", qs(c.video.rango)},
        {"video.legalizar", qs(c.legalizar)},
        {"entradas.placaReposo", c.placaReposoS},
        {"entradas.sinSenal", c.sinSenalMs},
        {"entradas.rearmarSinDatos", c.rearmarSinDatosS},
        {"entradas.reintentoMax", c.reintentoEntradaMaxMs},
        {"stream.reintentoMax", c.reintentoTransmisionMaxS},
        {"record.colchon", c.rec.colchonSeg},
        {"disco.prioridad", QString::fromStdString(const_cast<AppRuntime&>(rt_).engine().modoDisco())},
        {"disco.apuros", static_cast<int>(c.discoApurosS)},
        {"disco.recuperado", c.discoRecuperadoS},
        {"mcp.aprobacionMin", c.mcpAprobacionMin},
        {"modelo.contexto", c.modeloContexto},
        {"modelo.capasGpu", c.modeloCapasGpu},
        {"gateway.sinVideoPlaca", g.sinVideoPlacaMs},
        {"room.vistasRemotas", g.vistasRemotas},
        {"gateway.programaW", g.programVideoW},
        {"gateway.programaH", g.programVideoH},
        {"room.pinIntentos", g.pinIntentos},
        {"room.pinBloqueo", g.pinBloqueoS},
        {"launcher.cuelgue", l.cuelgueS},
        {"launcher.graciaArranque", l.graciaArranqueS},
        {"launcher.maxCaidas", l.maxCaidas},
        {"launcher.ventanaCaidas", l.ventanaCaidasMin},
        {"launcher.reiniciosSinPreguntar", l.reiniciosSinPreguntar},
        {"mejora.activa", c.mejora.activa},
        {"mejora.ruido", c.mejora.ruido},
        {"mejora.color", c.mejora.color},
        {"mejora.nitidez", c.mejora.nitidez},
        {"encoder.video", qs(c.encoder.videoEncoder)},
        {"encoder.bitrate", c.encoder.videoBitrateKbps},
        {"encoder.rateControl", qs(c.encoder.rateControl)},
        {"encoder.keyint", c.encoder.keyintSec},
        {"encoder.preset", qs(c.encoder.preset)},
        {"encoder.audioBitrate", QString::number(c.encoder.audioBitrateKbps)},
        {"record.dir", qs(c.rec.directory)},
        {"record.container", qs(c.rec.container)},
        {"record.remux", c.rec.remuxAlTerminar},
        {"record.onStart", c.record},
        {"record.calidad", qs(c.rec.calidad)},
        {"record.crf", c.rec.calidadCrf},
        {"record.pistas", [&]() {
             QStringList l;
             for (int p : c.rec.pistas) l << QString::number(p);
             return l.join(",");
         }()},
        {"record.copiaDir", qs(c.rec.copiaDir)},
        {"record.replay", c.replaySegundos},
        {"stream.server", qs(c.streamCfg.server)},
        {"stream.keyEnv", qs(c.streamKeyEnv)},
        {"stream.onStart", c.stream},
        {"guests.count", c.guests},
        {"guests.reserva", const_cast<AppRuntime&>(rt_).gateway().guestsReserva},
        {"guests.video", c.guestVideo},
        {"guests.audio", c.guestAudio},
        {"guests.audioPreset", qs(c.guestAudioPreset)},
        {"transition.type", qs(c.transition.type)},
        {"transition.ms", c.transition.durationMs},
        {"gateway.room", qs(g.room)},
        {"gateway.port", g.port},
        {"gateway.bind", qs(g.bindAddress)},
        {"gateway.urlHost", qs(g.urlHost)},
        {"gateway.tunnel", g.tunnel},
        {"gateway.tunnelHost", qs(g.tunnelHostname)},
        {"gateway.videoW", g.videoW},
        {"gateway.videoH", g.videoH},
        {"gateway.returnW", g.guestVideoW},
        {"gateway.returnH", g.guestVideoH},
        {"room.lobby", g.lobby},
        {"room.lobbySlots", g.lobbySlots},
        {"room.maxBackstage", g.maxBackstage},
        {"room.backstagePreviews", g.backstagePreviews},
        {"room.maxUploadMB", g.maxUploadMB},
        {"room.adminPort", g.adminPort},
        {"direct.https", g.https},
        {"direct.httpsPort", g.httpsPort},
        {"direct.rtcMin", g.rtcPortMin},
        {"direct.rtcMax", g.rtcPortMax},
        {"wg.enabled", g.wgEnabled},
        {"wg.endpoint", qs(g.wgEndpoint)},
        {"wg.serverKey", qs(g.wgServerPublicKey)},
        {"wg.address", qs(g.wgAddress)},
        {"wg.publicAddress", qs(g.wgPublicAddress)},
        {"wg.keepalive", g.wgKeepalive},
        {"gateway.metricsPort", g.metricsPort},
        {"wg.httpPort", g.wgHttpPort},
        {"wg.httpsPort", g.wgHttpsPort},
        {"pinggy.activo", g.pinggyActivo},
        {"pinggy.region", qs(g.pinggyRegion)},
        {"pinggy.dominioTipo", qs(g.pinggyDominioTipo)},
        {"pinggy.host", qs(g.pinggyHost)},
        {"pinggy.cnameDestino", qs(g.pinggyCnameDestino)},
        {"pinggy.turn", g.pinggyTurn},
        {"pinggy.turnTcpPuerto", g.pinggyTurnTcpPuerto},
        {"pinggy.turnUdp", g.pinggyTurnUdp},
        {"pinggy.turnUdpPuerto", g.pinggyTurnUdpPuerto},
        {"pinggy.listaBlanca", qs(g.pinggyListaBlanca)},
        {"pinggy.puertoTunel", g.pinggyPuertoTunel},
        {"launcher.shadow", l.shadowCopy},
        {"launcher.recoveryUi", l.recoveryUi},
        {"launcher.idioma", qs(l.idioma)},
        {"ui.theme", qs(c.theme)},
        {"web.tema", qs(c.temaWeb)},
        {"ui.previewWidth", c.previewWidth},
        {"mcp.enabled", c.mcpEnabled},
        {"mcp.port", c.mcpPort},
        {"mcp.approval", QString::fromStdString(c.mcpApproval)},
    };
}

void SettingsModel::load() {
    beginResetModel();
    saved_ = current();
    draft_ = saved_;
    endResetModel();
    emit dirtyChanged();
    emit draftChanged();
}

int SettingsModel::rowCount(const QModelIndex&) const { return static_cast<int>(fields_.size()); }

QVariant SettingsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const Field& f = fields_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case KeyRole: return f.key;
        case SectionRole: return f.section;
        case LabelRole: return f.label;
        case KindRole: return f.kind;
        case ValueRole: return draft_.value(f.key, f.def);
        case OptionsRole: return f.options;
        case MinRole: return f.min;
        case MaxRole: return f.max;
        case SuffixRole: return f.suffix;
        case HelpRole: return f.help;
        case ScopeRole: return f.scope;
        case AvanzadoRole: return f.avanzado;
        case ModifiedRole: return draft_.value(f.key) != saved_.value(f.key);
        default: return {};
    }
}

QHash<int, QByteArray> SettingsModel::roleNames() const {
    return {{KeyRole, "key"},     {SectionRole, "section"}, {LabelRole, "label"},
            {KindRole, "kind"},   {ValueRole, "value"},     {OptionsRole, "options"},
            {MinRole, "minimum"}, {MaxRole, "maximum"},     {SuffixRole, "suffix"},
            {HelpRole, "help"},   {ScopeRole, "scope"},     {ModifiedRole, "modified"},
            {AvanzadoRole, "avanzado"}};
}

bool SettingsModel::dirty() const { return draft_ != saved_; }

QStringList SettingsModel::sections() const {
    QStringList out;
    for (const auto& f : fields_) {
        if (!out.contains(f.section)) {
            out << f.section;
        }
    }
    return out;
}

QString SettingsModel::seccionVisible(const QString& seccion) const {
    // La del idioma, también en inglés: la encuentra quien lo cambió sin querer a uno que no lee.
    if (seccion == QLatin1String("Idioma")) return etiquetaIdioma();
    const QByteArray s = seccion.toUtf8();
    for (const char* k : kSecciones)
        if (s == k) return traducir("ajustes", k);
    return seccion;
}

int SettingsModel::rowOfKey(const QString& key) const {
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].key == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SettingsModel::setValue(const QString& key, const QVariant& value) {
    const int row = rowOfKey(key);
    if (row < 0 || draft_.value(key) == value) {
        return;
    }
    const bool wasDirty = dirty();
    draft_[key] = value;
    emit dataChanged(index(row), index(row), {ValueRole, ModifiedRole});
    emit draftChanged();
    if (wasDirty != dirty()) {
        emit dirtyChanged();
    }
}

QVariant SettingsModel::value(const QString& key) const { return draft_.value(key); }

void SettingsModel::traerDeAfuera() {
    // Otro aplicó (el asistente por MCP, «Configurar con la API key»): lo que el operador no
    // estaba editando toma el valor nuevo; lo que sí estaba editando se respeta. Antes, con
    // cualquier cambio a medio hacer, el formulario entero se quedaba con lo viejo (la
    // dirección de pinggy corregida por el asistente no se veía).
    const QVariantMap nuevo = current();
    for (auto it = nuevo.begin(); it != nuevo.end(); ++it) {
        const bool loEditaba = draft_.contains(it.key()) && saved_.contains(it.key()) &&
                               draft_.value(it.key()) != saved_.value(it.key());
        if (!loEditaba) draft_[it.key()] = it.value();
    }
    saved_ = nuevo;
    emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, ModifiedRole});
    emit dirtyChanged();
    emit draftChanged();
}

void SettingsModel::revert() {
    // Lo vigente se relee de la configuración: puede haber cambiado después de la
    // última carga (el cupo en caliente se guarda cuando contesta la sala, otro
    // formulario aplicó). Volver a la foto vieja hacía que un pedido igual a esa
    // foto pareciera «sin cambios» y no se aplicara.
    saved_ = current();
    draft_ = saved_;
    emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, ModifiedRole});
    emit dirtyChanged();
    emit draftChanged();
}

bool SettingsModel::tieneBasicos(const QString& section) const {
    for (const Field& f : fields_)
        if (f.section == section && !f.avanzado) return true;
    return false;
}

bool SettingsModel::tieneAvanzados(const QString& section) const {
    for (const Field& f : fields_)
        if (f.section == section && f.avanzado) return true;
    return false;
}

QString SettingsModel::alcance(const QString& key) const {
    for (const Field& f : fields_)
        if (f.key == key) return f.scope;
    return {};
}

void SettingsModel::resetSection(const QString& section) {
    for (const auto& f : fields_) {
        if (f.section == section) {
            setValue(f.key, f.def);
        }
    }
}

QString SettingsModel::apply() {
    // En vivo, lo que reinicia el gateway (corta a los invitados) o el programa no se aplica.
    if (enVivo_ && enVivo_()) {
        QStringList cortan;
        for (const Field& f : fields_)
            if ((f.scope == "gateway" || f.scope == "restart" || f.scope == "motor") && draft_.value(f.key) != saved_.value(f.key))
                cortan << f.label;
        if (!cortan.isEmpty()) {
            fallo_ = true;
            return traducir("ajustes", "EN VIVO: no se aplica «%1»: reinicia el gateway, el motor o el programa y corta a los "
                      "invitados o el aire. Volvé atrás esos cambios o esperá a terminar la transmisión.")
                .arg(cortan.join("», «"));
        }
    }
    fallo_ = false;
    // Por prefijo, sin los avanzados (esos van por destino, más abajo: algunos de la
    // app empiezan con «room.» y no tienen por qué reiniciar la sala).
    QSet<QString> avanzados;
    for (const Field& f : fields_)
        if (f.avanzado) avanzados.insert(f.key);
    const auto changed = [&](const QString& prefix) {
        for (auto it = draft_.cbegin(); it != draft_.cend(); ++it) {
            if (it.key().startsWith(prefix) && !avanzados.contains(it.key()) && it.value() != saved_.value(it.key())) {
                return true;
            }
        }
        return false;
    };
    // Qué cambió de lo que reinicia la sala (para decirlo en el aviso).
    const auto cambiadas = [&](const QStringList& prefijos) {
        QStringList out;
        for (auto it = draft_.cbegin(); it != draft_.cend(); ++it) {
            if (avanzados.contains(it.key()) || it.value() == saved_.value(it.key())) continue;
            for (const QString& p : prefijos)
                if (it.key().startsWith(p)) {
                    out << it.key();
                    break;
                }
        }
        return out;
    };
    const auto s = [&](const char* k) { return draft_.value(k).toString().toStdString(); };
    const auto i = [&](const char* k) { return draft_.value(k).toInt(); };
    const auto b = [&](const char* k) { return draft_.value(k).toBool(); };
    QStringList notes;

    AppConfig& c = rt_.config();
    // Video y codificacion: en caliente si no hay salidas activas. Solo los de
    // alcance «motor» lo rearman (antes, por el prefijo «video.», también
    // «Legalizar para TV», que no lo necesita).
    const bool cambioMotor = std::any_of(fields_.cbegin(), fields_.cend(), [&](const Field& f) {
        return f.scope == "motor" && draft_.value(f.key) != saved_.value(f.key);
    });
    if (cambioMotor) {
        VideoConfig v = c.video;
        v.baseWidth = i("video.width");
        v.baseHeight = i("video.height");
        v.outputWidth = i("video.outputWidth");
        v.outputHeight = i("video.outputHeight");
        if (!fpsDeTexto(s("video.fps"), v.fpsNum, v.fpsDen)) v.fpsNum = 30, v.fpsDen = 1;
        v.formato = s("video.formato");
        v.color = s("video.color");
        v.rango = s("video.rango");
        if (rt_.applyVideo(v)) {
            notes << traducir("ajustes", "Video aplicado: el motor se reinició (por unos segundos se cortó lo que sale).");
        } else {
            c.video = v; // se guarda igual: rige al proximo arranque
            notes << traducir("ajustes", "Video: %1 (se aplica al reiniciar).").arg(qs(rt_.lastError()));
        }
    }
    if (changed("mejora.")) {
        c.mejora.activa = b("mejora.activa");
        c.mejora.ruido = i("mejora.ruido");
        c.mejora.color = i("mejora.color");
        c.mejora.nitidez = i("mejora.nitidez");
        notes << (c.mejora.activa ? traducir("ajustes", "Mejorar imagen: prendido en las cámaras que siguen lo general.")
                                  : traducir("ajustes", "Mejorar imagen: apagado en las cámaras que siguen lo general."));
    }
    if (changed("encoder.")) {
        EncoderConfig e = c.encoder;
        e.videoEncoder = s("encoder.video");
        e.videoBitrateKbps = i("encoder.bitrate");
        e.rateControl = s("encoder.rateControl");
        e.keyintSec = i("encoder.keyint");
        e.preset = s("encoder.preset");
        e.audioBitrateKbps = draft_.value("encoder.audioBitrate").toInt();
        if (rt_.applyEncoder(e)) {
            notes << traducir("ajustes", "Codificación aplicada (rige en la próxima salida).");
        } else {
            c.encoder = e;
            notes << traducir("ajustes", "Codificación: se aplica al detener las salidas.");
        }
    }
    const std::filesystem::path recDir = s("record.dir");
    c.rec.directory = (recDir.is_absolute() ? recDir : rt_.dataRoot() / recDir).string();
    c.rec.container = s("record.container");
    c.rec.remuxAlTerminar = b("record.remux");
    c.record = b("record.onStart");
    c.legalizar = s("video.legalizar");
    c.placaReposoS = std::clamp(i("entradas.placaReposo"), 5, 3600);
    // Cámaras y entradas, Transmisión y el disco (Grabación): en caliente.
    c.sinSenalMs = i("entradas.sinSenal");
    c.rearmarSinDatosS = i("entradas.rearmarSinDatos");
    c.reintentoEntradaMaxMs = i("entradas.reintentoMax");
    c.reintentoTransmisionMaxS = i("stream.reintentoMax");
    c.rec.colchonSeg = i("record.colchon");
    c.discoApurosS = i("disco.apuros");
    c.discoRecuperadoS = i("disco.recuperado");
    c.mcpAprobacionMin = i("mcp.aprobacionMin");
    c.modeloContexto = i("modelo.contexto");
    c.modeloCapasGpu = i("modelo.capasGpu");
    aplicarAjustesMotor(c);
    if (draft_.value("disco.prioridad") != saved_.value("disco.prioridad")) rt_.engine().setModoDisco(s("disco.prioridad"));
    c.rec.calidad = s("record.calidad");
    c.rec.calidadCrf = i("record.crf");
    c.rec.pistas.clear();
    for (const QString& p : QString::fromStdString(s("record.pistas")).split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int n = p.trimmed().toInt(&ok);
        if (ok && n >= 2 && n <= 6) c.rec.pistas.push_back(n);
    }
    {
        const std::filesystem::path copia = s("record.copiaDir");
        c.rec.copiaDir = copia.empty() || copia.is_absolute() ? copia.string() : (rt_.dataRoot() / copia).string();
    }
    if (i("record.replay") != c.replaySegundos) {
        c.replaySegundos = i("record.replay");
        notes << QString::fromStdString(rt_.aplicarReplay());
    }
    c.streamCfg.server = s("stream.server");
    c.streamKeyEnv = s("stream.keyEnv");
    c.stream = b("stream.onStart");
    c.guestVideo = b("guests.video");
    c.guestAudio = b("guests.audio");
    c.guestAudioPreset = s("guests.audioPreset");
    c.theme = s("ui.theme");
    if (control::temaWebValido(s("web.tema"))) c.temaWeb = s("web.tema");
    c.previewWidth = std::clamp(i("ui.previewWidth"), 320, 3840);
    c.mcpEnabled = b("mcp.enabled");
    c.mcpPort = i("mcp.port");
    c.mcpApproval = s("mcp.approval");
    if (changed("transition.")) {
        rt_.setTransition(s("transition.type"), i("transition.ms"));
    }

    bool restartB = false;
    // El cupo SIEMPRE en caliente con la sala andando: el gateway crea los canales de
    // los lugares nuevos (hasta 16) y, si al bajarlo hay gente en un lugar que se iría,
    // la sala lo rechaza (aviso) y no saca a nadie. Nunca reinicia la sala por el cupo:
    // eso cortaba a todos (pasó al bajar de 6 a 3 con 6 conectados).
    bool cupoEnVivo = false;
    if (changed("guests.count") && cupoEnCaliente_ && i("guests.count") >= 1 && i("guests.count") <= ba::ipc::kMaxInvitados) {
        const std::string e = cupoEnCaliente_(i("guests.count")); // el resultado llega como aviso
        cupoEnVivo = true;
        if (!e.empty()) notes << traducir("ajustes", "Cupo: %1").arg(QString::fromStdString(e));
    }
    // Los avanzados: cada uno a su archivo ("ajustes"), solo los que no están en su valor de siempre.
    // Los de la sala: los de alcance «live» rigen en caliente (se le mandan por su API,
    // sala::ConfigSala); el resto la reinicia (porQue).
    json ajApp = json::object(), ajSala = json::object(), ajLauncher = json::object();
    json ajSalaEnCaliente = json::object(); // los «live» de la sala: van al gateway andando
    bool ajustesSalaEnCaliente = false, cambiaLauncher = false;
    QStringList porQue;
    for (const Field& f : fields_) {
        if (!f.avanzado) continue;
        const QVariant v = draft_.value(f.key);
        if (v != saved_.value(f.key)) {
            if (f.destino == 1 && f.scope == "live") ajustesSalaEnCaliente = true;
            else if (f.destino == 1) porQue << f.key;
            if (f.destino == 2) cambiaLauncher = true;
        }
        if (v == f.def) continue;
        json jv = f.kind == "float"  ? json(v.toDouble())
                  : f.kind == "int"  ? json(v.toInt())
                  : f.kind == "bool" ? json(v.toBool())
                                     : json(v.toString().toStdString());
        (f.destino == 1 ? ajSala : f.destino == 2 ? ajLauncher : ajApp)[f.key.toStdString()] = jv;
        if (f.destino == 1 && f.scope == "live") ajSalaEnCaliente[f.key.toStdString()] = jv;
    }
    c.ajustes = ajApp;
    ba::ajustes::cargar(c.ajustes); // en caliente: el código los lee al usarlos
    rt_.launcher().ajustes = ajLauncher;
    if (cambiaLauncher) notes << traducir("ajustes", "Los avanzados del arranque rigen al volver a abrir el programa.");
    bool salaEnCaliente = false;
    for (const QString& k : cambiadas({"gateway.", "guests.reserva", "room.", "direct.", "wg.", "pinggy."}) +
                                (cupoEnVivo ? QStringList() : cambiadas({"guests.count"}))) {
        if (alcance(k) == "live") salaEnCaliente = true;
        else porQue << k;
    }
    // Sin la sala para mandárselo, lo que rige en caliente queda para cuando arranque.
    if (configSala_ == nullptr && (salaEnCaliente || ajustesSalaEnCaliente) && porQue.isEmpty())
        notes << traducir("ajustes", "La conferencia toma estos cambios cuando arranque.");
    if (!porQue.isEmpty() || salaEnCaliente || ajustesSalaEnCaliente) {
        GatewaySettings& g = rt_.gateway();
        g.ajustes = ajSala;
        g.room = s("gateway.room");
        g.port = static_cast<std::uint16_t>(i("gateway.port"));
        g.bindAddress = s("gateway.bind");
        g.urlHost = s("gateway.urlHost");
        // Una dirección de la red para el QR con el gateway escuchando solo en esta máquina:
        // nadie podría entrar. Pasa a la red local.
        if (g.bindAddress == "127.0.0.1" && !g.urlHost.empty() && g.urlHost != "localhost" && g.urlHost.rfind("127.", 0) != 0)
            g.bindAddress = "0.0.0.0";
        g.tunnel = b("gateway.tunnel");
        g.tunnelHostname = s("gateway.tunnelHost");
        g.videoW = i("gateway.videoW");
        g.videoH = i("gateway.videoH");
        g.guestVideoW = i("gateway.returnW");
        g.guestVideoH = i("gateway.returnH");
        g.lobby = b("room.lobby");
        g.lobbySlots = i("room.lobbySlots");
        g.maxBackstage = i("room.maxBackstage");
        g.backstagePreviews = i("room.backstagePreviews");
        g.maxUploadMB = i("room.maxUploadMB");
        g.sinVideoPlacaMs = i("gateway.sinVideoPlaca");
        g.vistasRemotas = i("room.vistasRemotas");
        g.programVideoW = i("gateway.programaW");
        g.programVideoH = i("gateway.programaH");
        g.pinIntentos = i("room.pinIntentos");
        g.pinBloqueoS = i("room.pinBloqueo");
        g.adminPort = static_cast<std::uint16_t>(i("room.adminPort"));
        g.https = b("direct.https");
        g.httpsPort = static_cast<std::uint16_t>(i("direct.httpsPort"));
        g.rtcPortMin = static_cast<std::uint16_t>(i("direct.rtcMin"));
        g.rtcPortMax = static_cast<std::uint16_t>(i("direct.rtcMax"));
        g.wgEnabled = b("wg.enabled");
        g.wgEndpoint = s("wg.endpoint");
        g.wgServerPublicKey = s("wg.serverKey");
        g.wgAddress = s("wg.address");
        g.wgPublicAddress = s("wg.publicAddress");
        g.wgKeepalive = i("wg.keepalive");
        g.metricsPort = static_cast<std::uint16_t>(i("gateway.metricsPort"));
        g.wgHttpPort = static_cast<std::uint16_t>(i("wg.httpPort"));
        g.wgHttpsPort = static_cast<std::uint16_t>(i("wg.httpsPort"));
        g.pinggyActivo = b("pinggy.activo");
        g.pinggyRegion = s("pinggy.region");
        g.pinggyDominioTipo = s("pinggy.dominioTipo");
        {
            // Lo que se pega del panel: sin esquema, barras ni espacios, en minúsculas.
            QString h = QString::fromStdString(s("pinggy.host")).trimmed().toLower();
            h.remove(QRegularExpression("^[a-z]+://"));
            h = h.section('/', 0, 0);
            if (h.endsWith('.')) h.chop(1);
            g.pinggyHost = h.toStdString();
            QString d = QString::fromStdString(s("pinggy.cnameDestino")).trimmed().toLower();
            if (d.endsWith('.')) d.chop(1);
            g.pinggyCnameDestino = d.toStdString();
        }
        g.pinggyTurn = b("pinggy.turn");
        g.pinggyTurnTcpPuerto = static_cast<std::uint16_t>(i("pinggy.turnTcpPuerto"));
        g.pinggyTurnUdp = b("pinggy.turnUdp");
        g.pinggyTurnUdpPuerto = static_cast<std::uint16_t>(i("pinggy.turnUdpPuerto"));
        g.pinggyListaBlanca = s("pinggy.listaBlanca");
        g.pinggyPuertoTunel = static_cast<std::uint16_t>(i("pinggy.puertoTunel"));
        if (g.pinggyActivo && g.pinggyDominioTipo != "azar" && g.pinggyHost.empty())
            notes << traducir("ajustes", "Pinggy: falta el subdominio o dominio; mientras tanto se usa la dirección al azar.");
        // Por ajustarInvitados, como el cupo en caliente: crea el audio y las
        // entradas de video de los lugares nuevos (si no, «Invitado 5» no aparecía).
        if (!cupoEnVivo && i("guests.count") != c.guests) rt_.ajustarInvitados(i("guests.count"));
        g.guestsReserva = std::max(i("guests.reserva"), c.guests);
        restartB = !porQue.isEmpty();
    }
    LauncherSettings& l = rt_.launcher();
    l.shadowCopy = b("launcher.shadow");
    l.recoveryUi = b("launcher.recoveryUi");
    l.cuelgueS = i("launcher.cuelgue");
    l.graciaArranqueS = i("launcher.graciaArranque");
    l.maxCaidas = i("launcher.maxCaidas");
    l.ventanaCaidasMin = i("launcher.ventanaCaidas");
    l.reiniciosSinPreguntar = i("launcher.reiniciosSinPreguntar");
    const bool idiomaCambio = l.idioma != s("launcher.idioma");
    l.idioma = s("launcher.idioma");
    if (changed("launcher.") || changed("guests.video") || changed("guests.audio") ||
        changed("guests.audioPreset")) {
        notes << traducir("ajustes", "Algunos cambios rigen desde el proximo arranque.");
    }

    if (!rt_.save()) {
        fallo_ = true;
        notes << traducir("ajustes", "ERROR al guardar: %1").arg(qs(rt_.lastError()));
    } else {
        notes << traducir("ajustes", "Configuracion guardada.");
    }
    if (!restartB && configSala_ != nullptr) {
        // En caliente: el gateway los aplica andando, sin cortar a nadie. Lo que no
        // pudo (la sala no contesta, un valor que rechazó) se avisa: queda guardado y
        // rige cuando la conferencia vuelva a arrancar.
        QPointer<SettingsModel> yo(this);
        const auto alVolver = [yo](const std::string& e) {
            if (yo && !e.empty())
                emit yo->avisoSala(traducir("ajustes", "La conferencia no tomó los cambios en caliente (%1): rigen cuando "
                                                       "vuelva a arrancar.")
                                       .arg(QString::fromStdString(e)));
        };
        if (ajustesSalaEnCaliente) configSala_->aplicar("ajustes", json{{"valores", ajSalaEnCaliente}}, alVolver);
        if (salaEnCaliente) {
            const GatewaySettings& g = rt_.gateway();
            configSala_->aplicar("sala",
                                 json{{"lobby", g.lobby},
                                      {"pinIntentos", g.pinIntentos},
                                      {"pinBloqueoS", g.pinBloqueoS},
                                      {"maxUploadMB", g.maxUploadMB}},
                                 alVolver);
        }
        if (ajustesSalaEnCaliente || salaEnCaliente)
            notes << traducir("ajustes", "La conferencia los toma en caliente, sin reiniciarse.");
    }
    if (restartB) {
        notes << traducir("ajustes", "Se reinicia la conferencia por: %1.").arg(porQue.join(", "));
        // Reiniciarla corta a todos: que se sepa a cuántos.
        if (const int n = configSala_ != nullptr ? configSala_->conectados() : -1; n > 0)
            notes << traducir("ajustes", "Se cortan %1 conectados: vuelven a entrar solos en unos segundos.").arg(n);
        // Aparte: esperar que el gateway se cierre trababa la interfaz unos segundos
        // (al prender o apagar pinggy, por ejemplo). El resultado llega como señal.
        notes << traducir("ajustes", "Reiniciando la conferencia con la configuración nueva…");
        QPointer<SettingsModel> yo(this);
        rt_.reiniciarSalaEnElFondo([yo](const std::string& e) {
            if (!yo) return;
            QMetaObject::invokeMethod(yo.data(), [yo, e]() {
                if (yo) emit yo->salaReiniciada(QString::fromStdString(e));
            }, Qt::QueuedConnection);
        });
    }
    load();
    aplicando_ = true;
    rt_.ajustesAplicados(); // los otros formularios se recargan
    aplicando_ = false;
    if (idiomaCambio) emit idiomaPendienteChanged();
    const QString summary = notes.join(" ");
    emit applied(summary);
    return summary;
}

} // namespace ba::app
