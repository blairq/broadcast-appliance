// test_alerts.cpp — vigilancias (AlertEngine) con entradas y reloj falsos
//
//   audio_peak (retencion + enfriamiento por canal, ignora silenciados),
//   audio_silence (solo al aire), media_end (al terminar y "faltan N s"),
//   speaker_time (una vez por turno), lobby/hand (sin avisar lo que ya estaba),
//   chat (filtro y regex), stream_health (cuadros perdidos y corte), disk,
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
    AlertInputs::Status status;
    double disk = 100;

    AlertInputs inputs() {
        AlertInputs in;
        in.audio = [this]() { return audio; };
        in.media = [this]() { return media; };
        in.room = [this]() { return room; };
        in.chat = [this]() { return chat; };
        in.status = [this]() { return status; };
        in.diskFreeGb = [this]() { return disk; };
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
    CHECK(fired.empty(), "lo que ya estaba al crear la vigilancia no se avisa");
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
        CHECK(reloaded.list().size() == eng.list().size(), "las vigilancias se guardan y se recargan");
        const json again = reloaded.create(json{{"type", "lobby"}});
        CHECK(again["id"] != t1["id"] && again["id"] != t2["id"], "los ids no se reutilizan");
    }

    if (g_failures == 0) std::printf("test_alerts: OK\n");
    return g_failures == 0 ? 0 : 1;
}
