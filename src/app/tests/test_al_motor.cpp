// ============================================================================
// src/app/tests/test_al_motor.cpp — ida y vuelta de los tipos de antes al motor (F3-12)
//
// Cada opción que guardaba una fuente de la época de OBS (las cámaras, los
// micrófonos, ffmpeg_source de archivo y de red, los invitados, NDI, los textos)
// tiene que LLEGAR al motor o volver en `sinEfecto` (y avisarse): ninguna se
// pierde callada. Y lo que alMotor deja para el motor (ba_gst_fuente /
// ba_gst_medio) tiene que ser algo que el motor LEE: se busca cada clave en
// src/motor/Entrada.cpp (lo que no está ahí es dato de la app y va en la lista
// de abajo, con su porqué).
// ============================================================================
#include "emision/InputSpecs.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

// Lo que InputSpecs toma de audio/ y video/ (con GStreamer y el equipo): acá, lo justo.
namespace ba::app::audio {
std::string pipelineDeMicrofono(const std::string& d) { return "osxaudiosrc device=" + d; }
std::string idDesdeLibobs(const std::string& id) { return "gst:" + id; }
} // namespace ba::app::audio
namespace ba::app::video {
std::string idCamaraDesdeLibobs(const std::string& id) { return "estable:" + id; }
} // namespace ba::app::video

using json = nlohmann::json;
using namespace ba::app;

namespace {

int fallas = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FALLA %s:%d: %s\n", __FILE__, __LINE__, #c);                                        \
            ++fallas;                                                                                                  \
        }                                                                                                              \
    } while (0)

/// Lo que guarda la app al lado de los ajustes del motor (el motor no lo lee):
/// el tipo lógico (tipoLogico, los grupos de gente) y lo que muestra la interfaz.
const std::set<std::string> kDatosDeLaApp = {
    "video_invitado", "slot", "channel", "anillo", // qué invitado / remoto es (grupos, retornos)
    "ndi", "ndi_name", "host", "port",             // el emisor NDI (para mostrarlo y reconectar)
};

std::string fuenteDelMotor() {
    std::ifstream f(BA_TEST_SRC "/src/motor/Entrada.cpp");
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// Cada clave de una fuente del motor la lee el motor (o es dato de la app).
void loLeeElMotor(const SourceSpec& s) {
    static const std::string motor = fuenteDelMotor();
    CHECK(!motor.empty());
    if (s.id != inputs::kMicType && s.id != inputs::kMedioType) return;
    const json a = json::parse(s.settingsJson);
    for (const auto& [k, v] : a.items()) {
        const bool lee = motor.find("\"" + k + "\"") != std::string::npos || kDatosDeLaApp.count(k) > 0;
        if (!lee) std::fprintf(stderr, "  «%s» (%s) no lo lee el motor\n", k.c_str(), s.name.c_str());
        CHECK(lee);
    }
}

SourceSpec fuente(const std::string& tipo, const json& ajustes, const std::string& nombre = "f") {
    SourceSpec s;
    s.id = tipo;
    s.name = nombre;
    s.settingsJson = ajustes.dump();
    s.volume = 0.5f;
    s.muted = true;
    s.rect = CanvasRect{0.1f, 0.2f, 0.3f, 0.4f};
    return s;
}

json ajustes(const SourceSpec& s) { return json::parse(s.settingsJson); }

bool tiene(const std::vector<std::string>& v, const std::string& k) {
    for (const auto& x : v)
        if (x == k) return true;
    return false;
}

/// Pasa, comprueba lo común (nombre, lugar, volumen, idempotente, lo lee el motor) y devuelve el resultado.
inputs::AlMotor pasar(SourceSpec& s) {
    const SourceSpec antes = s;
    const inputs::AlMotor r = inputs::alMotor(s);
    CHECK(s.name == antes.name && s.volume == antes.volume && s.muted == antes.muted && s.rect.w == antes.rect.w);
    SourceSpec otra = s;
    CHECK(!inputs::alMotor(otra).cambio); // idempotente
    CHECK(otra.settingsJson == s.settingsJson);
    loLeeElMotor(s);
    return r;
}

} // namespace

int main() {
    // 1. Red guardada por el networkSpec de antes: rtsp_transport, nobuffer y la CA llegan.
    {
        SourceSpec s = fuente("ffmpeg_source", {{"is_local_file", false},
                                                {"input", "rtsp://u:c@10.0.0.5/s"},
                                                {"input_format", ""},
                                                {"hw_decode", true},
                                                {"reconnect_delay_sec", 7},
                                                {"buffering_mb", 1},
                                                {"restart_on_activate", false},
                                                {"close_when_inactive", false},
                                                {"clear_on_media_end", false},
                                                {"ffmpeg_options", "rtsp_transport=udp fflags=nobuffer flags=low_delay "
                                                                   "ca_file=/b/cacert.pem tls_verify=1 "}});
        const auto r = pasar(s);
        const json a = ajustes(s);
        CHECK(r.cambio && r.sinEfecto.empty());
        CHECK(s.id == inputs::kMicType);
        CHECK(a["uri"] == "rtsp://u:c@10.0.0.5/s");
        CHECK(a["rtsp_transporte"] == "udp");
        CHECK(a["latencia"] == 50);
        CHECK(a["tls_ca"] == "/b/cacert.pem");
        CHECK(a["reconectar_s"] == 7);
        CHECK(inputs::isNetworkInput(s.id, a));
    }
    // 2. Lo que el motor no tiene vuelve en sinEfecto (no se pierde callado).
    {
        SourceSpec s = fuente("ffmpeg_source", {{"is_local_file", false},
                                                {"input", "rtmp://x/y"},
                                                {"hw_decode", false},
                                                {"input_format", "flv"},
                                                {"ffmpeg_options", "analyzeduration=100 rtsp_transport=tcp"}});
        const auto r = pasar(s);
        CHECK(tiene(r.sinEfecto, "hw_decode"));
        CHECK(tiene(r.sinEfecto, "input_format"));
        CHECK(tiene(r.sinEfecto, "ffmpeg_options:analyzeduration=100"));
        CHECK(ajustes(s)["rtsp_transporte"] == "tcp");
        CHECK(ajustes(s)["latencia"] == 200); // sin nobuffer ni 1 MB: normal
    }
    // 3. Un archivo: a ba_gst_medio con lo suyo.
    {
        SourceSpec s = fuente("ffmpeg_source", {{"is_local_file", true}, {"local_file", "Media/a.mp4"}, {"looping", true},
                                                {"restart_on_activate", true}, {"clear_on_media_end", false}});
        const auto r = pasar(s);
        CHECK(s.id == inputs::kMedioType && r.sinEfecto.empty());
        CHECK(ajustes(s)["local_file"] == "Media/a.mp4" && ajustes(s)["looping"] == true);
        SourceSpec t = fuente("ffmpeg_source", {{"is_local_file", true}, {"local_file", "x.mp4"}, {"speed_percent", 50}});
        CHECK(tiene(pasar(t).sinEfecto, "speed_percent"));
    }
    // 4. Invitados, remotos, NDI, cámaras y micrófonos de antes.
    {
        SourceSpec v = fuente("ba_guest_video", {{"slot", 3}, {"channel", "share"}});
        CHECK(pasar(v).sinEfecto.empty());
        CHECK(inputs::esVideoInvitado(v.id, v.settingsJson) && ajustes(v)["slot"] == 3 && ajustes(v)["channel"] == "share");
        SourceSpec a = fuente("ba_guest_audio", {{"slot", 2}, {"channel", "remoto"}});
        CHECK(pasar(a).sinEfecto.empty());
        CHECK(ajustes(a)["anillo"] == "remoto" && ajustes(a)["pipeline_audio"] == "baanillo canal=BA.Audio.Remoto.2");
        SourceSpec n = fuente("ba_ndi_input", {{"ndi_name", "CAM (1)"}, {"host", "10.0.0.9"}, {"port", 5960}});
        CHECK(pasar(n).sinEfecto.empty() && ajustes(n)["ndi_name"] == "CAM (1)");
        SourceSpec c = fuente(inputs::kVideoType, {{inputs::kVideoProp, "x1"}, {"resolution", "1280x720"}});
        const auto rc = pasar(c);
        CHECK(ajustes(c)["camara"] == "estable:x1" && tiene(rc.sinEfecto, "resolution"));
        SourceSpec m = fuente(inputs::kTipoMicViejo, {{inputs::kAudioInProp, "m1"}});
        CHECK(pasar(m).sinEfecto.empty());
        CHECK(ajustes(m)["microfono"] == "gst:m1" && inputs::esMicrofono(m.id, ajustes(m)));
    }
    // 5. Textos y colores de antes, al tipo registrado de siempre.
    {
        SourceSpec g = fuente("text_gdiplus_v2", {{"text", "hola"}, {"color", 4278190335LL}, {"font", {{"face", "Arial"}}}});
        CHECK(pasar(g).sinEfecto.empty());
        CHECK(g.id == "text_ft2_source_v2" && ajustes(g)["color1"] == 4278190335LL && !ajustes(g).contains("color"));
        SourceSpec b = fuente("ba_texto", {{"text", "x"}});
        pasar(b);
        CHECK(ajustes(b)["font"]["flags"] == 1); // iba siempre en negrita
        SourceSpec col = fuente("color_source", {{"color", 1}, {"width", 10}});
        pasar(col);
        CHECK(col.id == "color_source_v3" && ajustes(col)["width"] == 10);
        SourceSpec raro = fuente("text_ft2_source", {{"text", "x"}, {"chatlog_mode", true}});
        CHECK(tiene(pasar(raro).sinEfecto, "chatlog_mode"));
    }
    // 6. Lo que arma hoy networkSpec ya es del motor y lo lee entero.
    {
        SourceSpec s = inputs::networkSpec("rtsps://cam/x", "Red", json{{"latency", "low"}, {"rtspTransport", "udp"}},
                                           "/b/cacert.pem");
        CHECK(!pasar(s).cambio);
        CHECK(ajustes(s)["rtsp_transporte"] == "udp" && ajustes(s)["tls_ca"] == "/b/cacert.pem" && ajustes(s)["latencia"] == 50);
        SourceSpec h = inputs::networkSpec("http://x/y.m3u8", "H", json{{"audio", false}}, "/b/ca.pem");
        CHECK(!ajustes(h).contains("tls_ca") && !ajustes(h).contains("rtsp_transporte") && h.muted);
    }
    // 7. Lo del motor no se toca.
    {
        SourceSpec s = inputs::hardwareSpec("cam", "C", json::object());
        CHECK(!pasar(s).cambio);
        SourceSpec v = inputs::videoInvitadoSpec(1, "guest", "Invitado 1");
        CHECK(!pasar(v).cambio);
    }
    std::printf(fallas == 0 ? "test_al_motor: OK\n" : "test_al_motor: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
