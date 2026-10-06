// test_alerts.cpp — disparadores (AlertEngine) con entradas y reloj falsos
//
//   audio_peak (retencion + enfriamiento por canal, ignora silenciados),
//   audio_silence (solo al aire), media_end (al terminar y "faltan N s"),
//   speaker_time (una vez por turno), lobby/hand (sin avisar lo que ya estaba),
//   chat (filtro y regex), stream_health (cuadros perdidos y corte),
//   stream_down (se cayo sola y no vuelve; NO cuando la paraste vos), disk,
//   program_black / program_frozen (solo al aire), record_down,
//   timer (in / every), once, persistencia, alert_wait (evento y vencimiento),
//   validaciones.

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

#include "agent/AlertEngine.hpp"

using ba::app::agent::AlertEngine;
using ba::app::agent::AlertInputs;
using json = nlohmann::json;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", msg, __LINE__);                                            \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

struct World {
    std::int64_t now = 1'800'000'000'000;
    std::vector<AlertInputs::Audio> audio{{"Mic", -30, false}, {"Musica", -20, false}, {"Mudo", 0, true}};
    std::vector<AlertInputs::Media> media{{"Musica", "playing", 60000, 0}};
    json room{{"serverNowMs", 0.0}, {"participants", json::array()}};
    std::vector<json> chat;
    std::vector<json> textos;
    AlertInputs::Status status;
    double disk = 100;
    json prog{{"valido", true}, {"luma", 120.0}, {"quieto_s", 0.0}};

    AlertInputs inputs() {
        AlertInputs in;
        in.audio = [this]() { return audio; };
        in.media = [this]() { return media; };
        in.room = [this]() { return room; };
        in.chat = [this]() { return chat; };
        in.textos = [this]() { return textos; };
        in.status = [this]() { return status; };
        in.diskFreeGb = [this]() { return disk; };
        in.programaVideo = [this]() { return prog; };
        in.nowMs = [this]() { return now; };
        return in;
    }
};

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_alerts ==\n");
    QTemporaryDir dir;
    const std::string store = (dir.path() + "/alertas.json").toStdString();
    World w;
    AlertEngine eng(w.inputs(), store);
    std::vector<json> fired;
    QObject::connect(&eng, &AlertEngine::fired, [&](const json& e) { fired.push_back(e); });
    const auto step = [&](int ms) {
        w.now += ms;
        eng.tick();
    };

    // --- validaciones -------------------------------------------------------------------------
    CHECK(eng.create(json{{"type", "nada"}}).contains("error"), "tipo desconocido");
    CHECK(eng.create(json{{"type", "timer"}, {"params", json::object()}}).contains("error"),
          "timer sin cuando");
    CHECK(eng.create(json{{"type", "chat"}, {"params", {{"regex", "("}}}}).contains("error"),
          "regex invalida");

    // --- audio_peak -----------------------------------------------------------------------------
    const json peak = eng.create(
        json{{"type", "audio_peak"}, {"params", {{"source", "*"}, {"thresholdDb", -1}, {"holdMs", 300}}}});
    step(250);
    w.audio[0].peakDb = -0.2;
    w.audio[2].peakDb = 0; // silenciado: no cuenta
    step(250);
    CHECK(fired.empty(), "no avisa antes de la retencion");
    step(250);
    step(100);
    CHECK(fired.size() == 1 && fired[0]["data"]["source"] == "Mic", "avisa la saturacion del canal");
    CHECK(fired[0]["text"].get<std::string>().find("Mic") != std::string::npos, "el texto nombra el canal");
    step(1000);
    CHECK(fired.size() == 1, "enfriamiento: no repite enseguida");
    step(20000);
    CHECK(fired.size() == 2, "repite pasado el enfriamiento");
    w.audio[0].peakDb = -30;
    eng.remove(peak["id"]);
    fired.clear();

    // --- audio_silence (solo al aire) -----------------------------------------------------------
    eng.create(
        json{{"type", "audio_silence"}, {"params", {{"source", "Mic"}, {"belowDb", -55}, {"forSec", 5}}}});
    w.audio[0].peakDb = -80;
    step(6000);
    step(100);
    CHECK(fired.empty(), "fuera del aire no avisa silencio");
    w.status.streaming = true;
    step(100);
    step(5100);
    CHECK(fired.size() == 1 && fired[0]["type"] == "audio_silence", "al aire, avisa el silencio");
    step(10000);
    CHECK(fired.size() == 1, "una sola vez mientras sigue el silencio");
    w.audio[0].peakDb = -20;
    w.status.streaming = false;
    eng.clear();
    fired.clear();

    // --- media_end ------------------------------------------------------------------------------
    eng.create(json{{"type", "media_end"}, {"params", {{"source", "Musica"}}}});
    eng.create(json{{"type", "media_end"}, {"params", {{"source", "Musica"}, {"beforeSec", 10}}}});
    step(250);
    w.media[0].timeMs = 45000;
    step(250);
    CHECK(fired.empty(), "faltan 15 s: nada");
    w.media[0].timeMs = 51000;
    step(250);
    CHECK(fired.size() == 1 && fired[0]["text"].get<std::string>().find("le quedan") != std::string::npos,
          "avisa cuando faltan 10 s");
    w.media[0].state = "ended";
    step(250);
    CHECK(fired.size() == 2 && fired[1]["text"].get<std::string>().find("Termino") != std::string::npos,
          "avisa al terminar");
    step(250);
    CHECK(fired.size() == 2, "no repite");
    eng.clear();
    fired.clear();

    // --- sala: speaker_time, lobby, hand, guest_drop ----------------------------------------------
    w.room["participants"] = json::array({{{"id", "p1"}, {"name", "Ana"}, {"state", "waiting"}}});
    eng.create(json{{"type", "lobby"}});
    eng.create(json{{"type", "hand"}});
    eng.create(json{{"type", "guest_drop"}});
    eng.create(json{{"type", "speaker_time"}, {"params", {{"remainingSec", 30}}}});
    step(250);
    CHECK(fired.empty(), "lo que ya estaba al crear el disparador no se avisa");
    w.room["participants"].push_back({{"id", "p2"}, {"name", "Beto"}, {"state", "waiting"}});
    step(250);
    CHECK(fired.size() == 1 && fired[0]["type"] == "lobby" && fired[0]["data"]["name"] == "Beto",
          "Beto entra al lobby");
    w.room["participants"][0] = {{"id", "p1"},         {"name", "Ana"},      {"state", "admitted"},
                                 {"connected", true},  {"hand", true},       {"speaker", "onair"},
                                 {"allottedSec", 120}, {"onAirSinceMs", 0.0}};
    w.room["serverNowMs"] = 80000.0; // le quedan 40 s
    step(250);
    CHECK(fired.size() == 2 && fired[1]["type"] == "hand", "Ana levanta la mano");
    w.room["serverNowMs"] = 91000.0; // 29 s
    step(250);
    CHECK(fired.size() == 3 && fired[2]["type"] == "speaker_time", "a Ana le quedan menos de 30 s");
    w.room["serverNowMs"] = 95000.0;
    step(250);
    CHECK(fired.size() == 3, "una vez por turno");
    w.room["participants"][0]["connected"] = false;
    step(250);
    CHECK(fired.size() == 4 && fired[3]["type"] == "guest_drop", "Ana pierde la conexion");
    eng.clear();
    fired.clear();

    // --- chat -----------------------------------------------------------------------------------
    w.chat = {{{"key", "k1"}, {"platform", "youtube"}, {"author", "x"}, {"text", "hola pregunta vieja"}}};
    eng.create(json{{"type", "chat"}, {"params", {{"platform", "youtube"}, {"contains", "PREGUNTA"}}}});
    eng.create(json{{"type", "chat"}, {"params", {{"regex", "^!escena\\s+\\d+$"}}}});
    step(250);
    w.chat.push_back({{"key", "k2"}, {"platform", "twitch"}, {"author", "y"}, {"text", "una pregunta"}});
    w.chat.push_back({{"key", "k3"}, {"platform", "youtube"}, {"author", "z"}, {"text", "Otra Pregunta"}});
    w.chat.push_back({{"key", "k4"}, {"platform", "facebook"}, {"author", "w"}, {"text", "!escena 4"}});
    step(250);
    CHECK(fired.size() == 2, "filtra por plataforma, texto y regex (y no repite lo viejo)");
    CHECK(fired.size() == 2 && fired[0]["data"]["author"] == "z" && fired[1]["data"]["text"] == "!escena 4",
          "los correctos");
    eng.clear();
    fired.clear();

    // --- texto (una salida de texto con nombre) --------------------------------------------------
    w.textos = {{{"key", "1"}, {"flujo", "Orador"}, {"texto", "pasamos a un cuarto intermedio"}}};
    eng.create(json{{"type", "texto"}, {"params", {{"flujo", "orador"}, {"contains", "CUARTO INTERMEDIO"}}}});
    eng.create(json{{"type", "texto"}, {"params", {{"palabras", {"votación", "moción"}}}}});
    step(250);
    CHECK(fired.empty(), "lo que ya estaba dicho al crear el disparador no se avisa");
    w.textos.push_back({{"key", "2"}, {"flujo", "Sala"}, {"texto", "cuarto intermedio de diez minutos"}});
    w.textos.push_back({{"key", "3"}, {"flujo", "Orador"}, {"texto", "Hacemos un cuarto intermedio"}});
    w.textos.push_back({{"key", "4"}, {"flujo", "Sala"}, {"texto", "pido una votacion nominal"}});
    step(250);
    CHECK(fired.size() == 2, "filtra por salida y frase, y las palabras sin importar tildes");
    CHECK(fired.size() == 2 && fired[0]["data"]["texto"] == "Hacemos un cuarto intermedio" &&
              fired[1]["data"]["flujo"] == "Sala",
          "los correctos");
    CHECK(fired.size() == 2 && fired[0]["untrusted"] == true,
          "lo que dijo una persona llega como dato no confiable");
    eng.clear();
    fired.clear();

    // --- stream_health y disk ---------------------------------------------------------------------
    eng.create(json{{"type", "stream_health"}, {"params", {{"droppedPct", 2}, {"windowSec", 10}}}});
    eng.create(json{{"type", "disk"}, {"params", {{"minGb", 5}}}});
    w.status = {true, true, 0, 1000};
    step(250);
    w.status.totalFrames = 1300;
    w.status.droppedFrames = 30; // 10%
    step(1000);
    CHECK(fired.size() == 1 && fired[0]["type"] == "stream_health", "cuadros perdidos");
    w.disk = 3;
    step(250);
    CHECK(fired.size() == 2 && fired[1]["type"] == "disk", "poco disco grabando");
    w.status.streaming = false;
    step(250);
    CHECK(fired.size() == 3 && fired[2]["text"].get<std::string>().find("detuvo") != std::string::npos,
          "se corto");
    eng.clear();
    fired.clear();

    // --- stream_down: solo cuando se cayo SOLA y no vuelve -----------------------------------------
    // Es la diferencia con stream_health, que tambien avisa cuando la parás
    // vos. A esta se le engancha una accion (volver a salir, poner la placa de
    // «ya volvemos»), asi que no puede dispararse cuando el corte fue a
    // proposito: seria pelearle al operador.
    eng.create(json{{"type", "stream_down"}, {"params", {{"forSec", 20}}}});
    w.status = {true, false, 0, 1000, false};
    step(250);
    // El operador la para: NO es un corte.
    w.status.streaming = false;
    step(1000);
    CHECK(fired.empty(), "pararla a proposito no dispara stream_down");

    // Ahora se cae sola y libobs reintenta. Un corte corto no es noticia.
    w.status = {true, false, 0, 1000, true};
    step(5000);
    CHECK(fired.empty(), "reintentar unos segundos todavia no avisa");
    step(20000);
    CHECK(fired.size() == 1 && fired[0]["type"] == "stream_down", "lleva rato sin volver: avisa");
    CHECK(fired.size() == 1 &&
              fired[0]["text"].get<std::string>().find("sin poder volver") != std::string::npos,
          "y dice cuanto lleva");

    // Vuelve: se olvida, para que el proximo corte se cuente de cero.
    w.status = {true, false, 0, 1000, false};
    step(250);
    // Y se deja pasar el enfriamiento: el segundo aviso no tiene que salir
    // pegado al primero (un corte que va y viene llenaria la pantalla).
    step(61000);
    fired.clear();
    // Se cae otra vez y el reintento se da por vencido.
    w.status.reconnecting = true;
    step(250);
    w.status = {false, false, 0, 1000, false};
    step(250);
    CHECK(fired.size() == 1 && fired[0]["text"].get<std::string>().find("vencida") != std::string::npos,
          "si el reintento se rinde, avisa aunque no haya pasado el tiempo");
    eng.clear();
    fired.clear();

    // --- program_black, program_frozen, record_down ---------------------------------------------
    eng.create(json{{"type", "program_black"}, {"params", {{"forSec", 3}, {"umbral", 16}}}});
    eng.create(json{{"type", "program_frozen"}, {"params", {{"forSec", 30}}}});
    w.status = {};
    w.prog = {{"valido", true}, {"luma", 2.0}, {"quieto_s", 40.0}};
    step(5000);
    step(250);
    CHECK(fired.empty(), "fuera del aire, ni negro ni congelado");
    w.status.recording = true;
    step(250);
    step(2000);
    CHECK(fired.size() == 1 && fired[0]["type"] == "program_frozen", "congelado: avisa enseguida (ya lleva 40 s)");
    step(1500);
    CHECK(fired.size() == 2 && fired[1]["type"] == "program_black", "negro: avisa a los 3 s");
    step(10000);
    CHECK(fired.size() == 2, "una sola vez mientras sigue");
    w.prog = {{"valido", true}, {"luma", 120.0}, {"quieto_s", 0.0}};
    step(250);
    eng.clear();
    fired.clear();
    eng.create(json{{"type", "record_down"}, {"params", {{"forSec", 5}}}});
    w.status = {};
    w.status.grabacionPedida = false;
    step(6000);
    CHECK(fired.empty(), "parada a propósito: no avisa");
    w.status.grabacionPedida = true;
    w.status.recording = true;
    step(250);
    w.status.recording = false; // se cortó sola
    step(250);
    step(3000);
    CHECK(fired.empty(), "unos segundos todavía no");
    step(2500);
    CHECK(fired.size() == 1 && fired[0]["type"] == "record_down", "la grabación se cortó: avisa");
    eng.clear();
    fired.clear();
    w.status = {};

    // --- timer, once, persistencia, wait ----------------------------------------------------------------
    const json t1 = eng.create(json{{"type", "timer"}, {"name", "Pausa"}, {"params", {{"in", 60}}}});
    const json t2 = eng.create(json{{"type", "timer"}, {"name", "Cada 10"}, {"params", {{"every", 10}}}});
    CHECK(t1.value("once", false) && !t2.value("once", true), "in = una vez; every = repite");
    json waited;
    bool gotWait = false;
    eng.wait(eng.lastEventId(), 30000, [&](json e) {
        waited = std::move(e);
        gotWait = true;
    });
    step(9000);
    CHECK(!gotWait, "alert_wait espera");
    step(1100);
    CHECK(gotWait && waited.size() == 1 && waited[0]["ruleName"] == "Cada 10",
          "alert_wait vuelve con el evento");
    for (int i = 0; i < 5; ++i)
        step(10000); // un aviso por vuelta (lo atrasado no se repite de golpe)
    int pausa = 0, cada = 0;
    for (const auto& e : fired) {
        if (e["ruleName"] == "Pausa") ++pausa;
        if (e["ruleName"] == "Cada 10") ++cada;
    }
    CHECK(pausa == 1 && cada >= 5, "el de una vez dispara una sola vez; el periodico sigue");
    bool disabled = false;
    for (const auto& r : eng.list())
        if (r["id"] == t1["id"]) disabled = !r.value("enabled", true);
    CHECK(disabled, "una vez: queda desactivada");
    bool timedOut = false;
    eng.wait(eng.lastEventId(), 1000, [&](json e) { timedOut = e.empty(); });
    step(1100);
    CHECK(timedOut, "alert_wait vence vacio");
    {
        World w2;
        AlertEngine reloaded(w2.inputs(), store);
        CHECK(reloaded.list().size() == eng.list().size(), "los disparadores se guardan y se recargan");
        const json again = reloaded.create(json{{"type", "lobby"}});
        CHECK(again["id"] != t1["id"] && again["id"] != t2["id"], "los ids no se reutilizan");
    }

    if (g_failures == 0) std::printf("test_alerts: OK\n");
    return g_failures == 0 ? 0 : 1;
}
