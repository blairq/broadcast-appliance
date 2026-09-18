// ============================================================================
// src/app/ui/SettingsModel.cpp — ver SettingsModel.hpp
// ============================================================================

#include "ui/SettingsModel.hpp"

#include <QVariantMap>

#include "AppRuntime.hpp"

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QVariantList choices(std::initializer_list<std::pair<const char*, const char*>> items) {
    QVariantList out;
    for (const auto& [v, l] : items) {
        out << QVariantMap{{"value", QString(v)}, {"label", QString(l)}};
    }
    return out;
}

} // namespace

SettingsModel::SettingsModel(AppRuntime& rt, QObject* parent) : QAbstractListModel(parent), rt_(rt) {
    // Codificadores: los que esta libobs tiene de verdad + los alias.
    QVariantList encoders = choices({{"auto", "Automatico (hardware si hay)"},
                                     {"vaapi", "VAAPI (AMD / Intel, Linux)"},
                                     {"nvenc", "NVENC (NVIDIA)"},
                                     {"qsv", "Quick Sync (Intel)"},
                                     {"amf", "AMF (AMD, Windows)"},
                                     {"videotoolbox", "VideoToolbox (macOS)"},
                                     {"x264", "x264 (software)"}});

    // clang-format off
    fields_ = {
        // --- Video -----------------------------------------------------------
        {"video.width", "Video", "Ancho del lienzo", "int", {}, 320, 7680, "px", "Resolucion de composicion.", "live", 1920},
        {"video.height", "Video", "Alto del lienzo", "int", {}, 240, 4320, "px", "", "live", 1080},
        {"video.outputWidth", "Video", "Ancho de salida", "int", {}, 320, 7680, "px", "Resolucion que se codifica (se escala desde el lienzo).", "live", 1920},
        {"video.outputHeight", "Video", "Alto de salida", "int", {}, 240, 4320, "px", "", "live", 1080},
        {"video.fps", "Video", "Cuadros por segundo", "choice", choices({{"24","24"},{"25","25"},{"30","30"},{"50","50"},{"60","60"}}), 0, 0, "fps", "", "live", "30"},
        // --- Codificacion ----------------------------------------------------
        {"encoder.video", "Codificacion", "Codificador de video", "choice", encoders, 0, 0, "", "Si el elegido no esta o falla al arrancar, se usa el siguiente disponible y al final x264.", "live", "auto"},
        {"encoder.bitrate", "Codificacion", "Bitrate de video", "int", {}, 500, 50000, "kbps", "", "live", 6000},
        {"encoder.rateControl", "Codificacion", "Control de tasa", "choice", choices({{"CBR","CBR (constante, para transmitir)"},{"VBR","VBR (variable)"},{"CQP","CQP (calidad fija, para grabar)"}}), 0, 0, "", "", "live", "CBR"},
        {"encoder.keyint", "Codificacion", "Intervalo de keyframe", "int", {}, 1, 10, "s", "Las plataformas de streaming piden 2 s.", "live", 2},
        {"encoder.preset", "Codificacion", "Preset de x264", "choice", choices({{"ultrafast","ultrafast"},{"superfast","superfast"},{"veryfast","veryfast"},{"faster","faster"},{"fast","fast"},{"medium","medium"}}), 0, 0, "", "Solo aplica a x264: mas lento = mejor calidad y mas CPU.", "live", "veryfast"},
        {"encoder.audioBitrate", "Codificacion", "Bitrate de audio", "choice", choices({{"96","96"},{"128","128"},{"160","160"},{"192","192"},{"256","256"},{"320","320"}}), 0, 0, "kbps", "", "live", "160"},
        // --- Grabacion -------------------------------------------------------
        {"record.dir", "Grabacion", "Carpeta", "path", {}, 0, 0, "", "Relativa al bundle o absoluta.", "live", "Recordings"},
        {"record.container", "Grabacion", "Formato", "choice", choices({{"mkv","MKV (sobrevive a un corte de energia)"},{"mp4","MP4"},{"mov","MOV"},{"flv","FLV"}}), 0, 0, "", "", "live", "mkv"},
        {"record.onStart", "Grabacion", "Grabar al arrancar", "bool", {}, 0, 0, "", "Respaldo local automatico.", "live", false},
        // --- Transmision -----------------------------------------------------
        {"stream.server", "Transmision", "Servidor RTMP", "text", {}, 0, 0, "", "p. ej. rtmp://a.rtmp.youtube.com/live2", "live", ""},
        {"stream.keyEnv", "Transmision", "Variable con la clave", "text", {}, 0, 0, "", "La clave no se guarda en disco: se lee de esta variable de entorno o se escribe al transmitir.", "live", "BA_STREAM_KEY"},
        {"stream.onStart", "Transmision", "Transmitir al arrancar", "bool", {}, 0, 0, "", "", "live", false},
        // --- Invitados -------------------------------------------------------
        {"guests.count", "Invitados", "Cantidad de invitados", "int", {}, 0, 4, "", "Cambiarla reinicia el gateway.", "gateway", 2},
        {"guests.video", "Invitados", "Video de invitados", "bool", {}, 0, 0, "", "", "restart", true},
        {"guests.audio", "Invitados", "Audio de invitados", "bool", {}, 0, 0, "", "", "restart", true},
        {"guests.audioPreset", "Invitados", "Procesamiento de voz", "choice", choices({{"podium","Podio"},{"lavalier","Corbatero"},{"handheld","De mano"},{"","Sin procesar"}}), 0, 0, "", "Channel strip nativo: HPF, EQ, de-esser, gate, compresor y limitador.", "restart", "podium"},
        // --- Escena ----------------------------------------------------------
        {"transition.type", "Escena", "Transicion", "choice", choices({{"cut","Corte"},{"fade","Fundido"},{"swipe","Barrido"},{"slide","Deslizar"},{"fade_to_color","Fundido a color"}}), 0, 0, "", "", "live", "fade"},
        {"transition.ms", "Escena", "Duracion de la transicion", "int", {}, 0, 5000, "ms", "", "live", 300},
        // --- Gateway ---------------------------------------------------------
        {"gateway.room", "Gateway", "Sala", "text", {}, 0, 0, "", "Nombre de la sala a la que entran los invitados.", "gateway", "sala1"},
        {"gateway.port", "Gateway", "Puerto", "int", {}, 1024, 65535, "", "", "gateway", 8787},
        {"gateway.bind", "Gateway", "Acceso", "choice", choices({{"127.0.0.1","Solo esta maquina (via tunel)"},{"0.0.0.0","Red local (tambien sin tunel)"}}), 0, 0, "", "Red local expone el gateway a la LAN; el PIN y el limite de intentos siguen protegiendo.", "gateway", "127.0.0.1"},
        {"gateway.tunnel", "Gateway", "Tunel de Cloudflare", "bool", {}, 0, 0, "", "Necesita el token en BA_TUNNEL_TOKEN.", "gateway", true},
        {"gateway.videoW", "Gateway", "Video del invitado: ancho", "int", {}, 320, 3840, "px", "", "gateway", 1280},
        {"gateway.videoH", "Gateway", "Video del invitado: alto", "int", {}, 180, 2160, "px", "", "gateway", 720},
        {"gateway.returnW", "Gateway", "Retorno al invitado: ancho", "int", {}, 320, 1920, "px", "", "gateway", 640},
        {"gateway.returnH", "Gateway", "Retorno al invitado: alto", "int", {}, 180, 1080, "px", "", "gateway", 360},
        // --- Sala --------------------------------------------------------------
        {"room.lobby", "Sala", "Lobby de admision", "bool", {}, 0, 0, "", "Cada invitado espera a que lo hagas pasar (y mientras espera solo te ve a vos).", "gateway", true},
        {"room.lobbySlots", "Sala", "Previews del lobby", "int", {}, 0, 8, "", "Cuantos de los que esperan pueden mostrarte su camara a la vez.", "gateway", 4},
        {"room.maxUploadMB", "Sala", "Tope por archivo compartido", "int", {}, 1, 20480, "MB", "", "gateway", 2048},
        {"room.adminPort", "Sala", "Puerto de la API de control", "int", {}, 1024, 65535, "", "Solo en 127.0.0.1: consola web y esta interfaz.", "gateway", 8788},
        // --- Acceso directo ------------------------------------------------------
        {"direct.https", "Acceso directo", "HTTPS propio", "bool", {}, 0, 0, "", "Los navegadores solo dan camara y microfono en paginas seguras: sin HTTPS, un invitado por la red local o por IP publica no puede usarlos.", "gateway", true},
        {"direct.httpsPort", "Acceso directo", "Puerto HTTPS", "int", {}, 1024, 65535, "", "Un certificado propio en Config/tls/cert.pem + key.pem evita el aviso del navegador.", "gateway", 8789},
        {"direct.rtcMin", "Acceso directo", "WebRTC: primer puerto UDP", "int", {}, 1024, 65000, "", "Rango fijo para el firewall y la redireccion del servidor.", "gateway", 40000},
        {"direct.rtcMax", "Acceso directo", "WebRTC: ultimo puerto UDP", "int", {}, 1024, 65535, "", "", "gateway", 40099},
        {"wg.enabled", "Acceso directo", "Tunel WireGuard", "bool", {}, 0, 0, "", "Conecta a un servidor propio con IP publica: los invitados entran directo, sin Cloudflare. Sin permisos de administrador ni drivers.", "gateway", false},
        {"wg.endpoint", "Acceso directo", "Servidor WireGuard", "text", {}, 0, 0, "", "host:puerto (p. ej. vps.midominio.com:51820).", "gateway", ""},
        {"wg.serverKey", "Acceso directo", "Clave publica del servidor", "text", {}, 0, 0, "", "La que muestra 'wg show' en el servidor (base64).", "gateway", ""},
        {"wg.address", "Acceso directo", "Direccion en el tunel", "text", {}, 0, 0, "", "La que el servidor le asigna a este equipo (AllowedIPs).", "gateway", "10.99.0.2/24"},
        {"wg.publicAddress", "Acceso directo", "IP o dominio publico", "text", {}, 0, 0, "", "Por donde entran los invitados; tambien es la que se anuncia para WebRTC.", "gateway", ""},
        {"wg.keepalive", "Acceso directo", "Keepalive", "int", {}, 0, 120, "s", "Mantiene abierto el NAT del lado de este equipo.", "gateway", 25},
        // --- Sistema ---------------------------------------------------------
        {"launcher.shadow", "Sistema", "Copia sombra", "bool", {}, 0, 0, "", "Ejecutar desde una copia en la cache del usuario (permite actualizar el bundle en caliente).", "restart", false},
        {"launcher.recoveryUi", "Sistema", "Dialogo de recuperacion", "bool", {}, 0, 0, "", "Si esta apagado, el supervisor reinicia solo sin preguntar.", "restart", true},
        {"ui.theme", "Sistema", "Tema", "choice", choices({{"dark","Oscuro"},{"light","Claro"}}), 0, 0, "", "", "live", "dark"},
    };
    // clang-format on
    load();
}

QVariantMap SettingsModel::current() const {
    const AppConfig& c = rt_.config();
    const GatewaySettings& g = const_cast<AppRuntime&>(rt_).gateway();
    const LauncherSettings& l = const_cast<AppRuntime&>(rt_).launcher();
    return {
        {"video.width", c.video.baseWidth},
        {"video.height", c.video.baseHeight},
        {"video.outputWidth", c.video.outputWidth},
        {"video.outputHeight", c.video.outputHeight},
        {"video.fps", QString::number(c.video.fpsNum)},
        {"encoder.video", qs(c.encoder.videoEncoder)},
        {"encoder.bitrate", c.encoder.videoBitrateKbps},
        {"encoder.rateControl", qs(c.encoder.rateControl)},
        {"encoder.keyint", c.encoder.keyintSec},
        {"encoder.preset", qs(c.encoder.preset)},
        {"encoder.audioBitrate", QString::number(c.encoder.audioBitrateKbps)},
        {"record.dir", qs(c.rec.directory)},
        {"record.container", qs(c.rec.container)},
        {"record.onStart", c.record},
        {"stream.server", qs(c.streamCfg.server)},
        {"stream.keyEnv", qs(c.streamKeyEnv)},
        {"stream.onStart", c.stream},
        {"guests.count", c.guests},
        {"guests.video", c.guestVideo},
        {"guests.audio", c.guestAudio},
        {"guests.audioPreset", qs(c.guestAudioPreset)},
        {"transition.type", qs(c.transition.type)},
        {"transition.ms", c.transition.durationMs},
        {"gateway.room", qs(g.room)},
        {"gateway.port", g.port},
        {"gateway.bind", qs(g.bindAddress)},
        {"gateway.tunnel", g.tunnel},
        {"gateway.videoW", g.videoW},
        {"gateway.videoH", g.videoH},
        {"gateway.returnW", g.guestVideoW},
        {"gateway.returnH", g.guestVideoH},
        {"room.lobby", g.lobby},
        {"room.lobbySlots", g.lobbySlots},
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
        {"launcher.shadow", l.shadowCopy},
        {"launcher.recoveryUi", l.recoveryUi},
        {"ui.theme", qs(c.theme)},
    };
}

void SettingsModel::load() {
    beginResetModel();
    saved_ = current();
    draft_ = saved_;
    endResetModel();
    emit dirtyChanged();
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
        case ModifiedRole: return draft_.value(f.key) != saved_.value(f.key);
        default: return {};
    }
}

QHash<int, QByteArray> SettingsModel::roleNames() const {
    return {{KeyRole, "key"},     {SectionRole, "section"}, {LabelRole, "label"},
            {KindRole, "kind"},   {ValueRole, "value"},     {OptionsRole, "options"},
            {MinRole, "minimum"}, {MaxRole, "maximum"},     {SuffixRole, "suffix"},
            {HelpRole, "help"},   {ScopeRole, "scope"},     {ModifiedRole, "modified"}};
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
    if (wasDirty != dirty()) {
        emit dirtyChanged();
    }
}

QVariant SettingsModel::value(const QString& key) const { return draft_.value(key); }

void SettingsModel::revert() {
    draft_ = saved_;
    emit dataChanged(index(0), index(rowCount() - 1), {ValueRole, ModifiedRole});
    emit dirtyChanged();
}

void SettingsModel::resetSection(const QString& section) {
    for (const auto& f : fields_) {
        if (f.section == section) {
            setValue(f.key, f.def);
        }
    }
}

QString SettingsModel::apply() {
    const auto changed = [&](const QString& prefix) {
        for (auto it = draft_.cbegin(); it != draft_.cend(); ++it) {
            if (it.key().startsWith(prefix) && it.value() != saved_.value(it.key())) {
                return true;
            }
        }
        return false;
    };
    const auto s = [&](const char* k) { return draft_.value(k).toString().toStdString(); };
    const auto i = [&](const char* k) { return draft_.value(k).toInt(); };
    const auto b = [&](const char* k) { return draft_.value(k).toBool(); };
    QStringList notes;

    AppConfig& c = rt_.config();
    // Video y codificacion: en caliente si no hay salidas activas.
    if (changed("video.")) {
        VideoConfig v = c.video;
        v.baseWidth = i("video.width");
        v.baseHeight = i("video.height");
        v.outputWidth = i("video.outputWidth");
        v.outputHeight = i("video.outputHeight");
        v.fpsNum = draft_.value("video.fps").toInt();
        if (rt_.applyVideo(v)) {
            notes << "Video aplicado.";
        } else {
            c.video = v; // se guarda igual: rige al proximo arranque
            notes << "Video: " + qs(rt_.lastError()) + " (se aplica al reiniciar).";
        }
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
            notes << "Codificacion aplicada (rige en la proxima salida).";
        } else {
            c.encoder = e;
            notes << "Codificacion: se aplica al detener las salidas.";
        }
    }
    const std::filesystem::path recDir = s("record.dir");
    c.rec.directory = (recDir.is_absolute() ? recDir : rt_.dataRoot() / recDir).string();
    c.rec.container = s("record.container");
    c.record = b("record.onStart");
    c.streamCfg.server = s("stream.server");
    c.streamKeyEnv = s("stream.keyEnv");
    c.stream = b("stream.onStart");
    c.guestVideo = b("guests.video");
    c.guestAudio = b("guests.audio");
    c.guestAudioPreset = s("guests.audioPreset");
    c.theme = s("ui.theme");
    if (changed("transition.")) {
        rt_.setTransition(s("transition.type"), i("transition.ms"));
    }

    bool restartB = false;
    if (changed("gateway.") || changed("guests.count") || changed("room.") || changed("direct.") ||
        changed("wg.")) {
        GatewaySettings& g = rt_.gateway();
        g.room = s("gateway.room");
        g.port = static_cast<std::uint16_t>(i("gateway.port"));
        g.bindAddress = s("gateway.bind");
        g.tunnel = b("gateway.tunnel");
        g.videoW = i("gateway.videoW");
        g.videoH = i("gateway.videoH");
        g.guestVideoW = i("gateway.returnW");
        g.guestVideoH = i("gateway.returnH");
        g.lobby = b("room.lobby");
        g.lobbySlots = i("room.lobbySlots");
        g.maxUploadMB = i("room.maxUploadMB");
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
        c.guests = i("guests.count");
        restartB = true;
    }
    LauncherSettings& l = rt_.launcher();
    l.shadowCopy = b("launcher.shadow");
    l.recoveryUi = b("launcher.recoveryUi");
    if (changed("launcher.") || changed("guests.video") || changed("guests.audio") ||
        changed("guests.audioPreset")) {
        notes << "Algunos cambios rigen desde el proximo arranque.";
    }

    if (!rt_.save()) {
        notes << "ERROR al guardar: " + qs(rt_.lastError());
    } else {
        notes << "Configuracion guardada.";
    }
    if (restartB) {
        notes << (rt_.restartGateway() ? "Gateway reiniciado con la configuracion nueva."
                                       : "Gateway: " + qs(rt_.lastError()));
    }
    load();
    const QString summary = notes.join(" ");
    emit applied(summary);
    return summary;
}

} // namespace ba::app
