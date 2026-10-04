// ============================================================================
// src/app/agent/AlertEngine.hpp — Disparadores programables (alertas)
//
// El asistente de IA no tiene que "mirar cada tanto" con temporizadores: le
// pide al servidor que vigile algo (alert_create) y el servidor le AVISA
// cuando pasa. Se evalua en el thread de la UI cada 250 ms contra el estado
// real (niveles de audio, medios, sala, chats, salidas, disco) y cada disparo
// es un evento que:
//   - se escribe en el chat del agente de la pestaña Asistente (lo despierta),
//   - opcionalmente muestra un aviso en la interfaz,
//   - opcionalmente ejecuta acciones (herramientas MCP) en el acto,
//   - y queda en la cola que leen alert_events / alert_wait (otros clientes).
//
// Tipos (params):
//   audio_peak    source ("*"|nombre), thresholdDb (-1), holdMs (300)
//   audio_silence source, belowDb (-55), forSec (15), onlyWhenLive (true)
//   media_end     source ("*"|nombre), beforeSec (0: al terminar)
//   speaker_time  remainingSec (0), participant (opcional)
//   lobby         (alguien entra al lobby)
//   hand          (alguien levanta la mano)
//   guest_drop    (un invitado admitido se desconecta)
//   chat          platform (all|youtube|twitch|facebook|room), contains | regex
//   guest_message un invitado escribe en el chat de la sala (privateOnly)
//   texto         una SALIDA DE TEXTO (una «Voz a texto» con nombre) dice algo
//                 que coincide: flujo ("*" o el nombre), contains | palabras |
//                 regex (sin distinguir mayúsculas ni tildes)
//   stream_health droppedPct (2), windowSec (30); y corte de la transmision
//   disk          minGb (5) (solo grabando)
//   timer         in (seg) | at ("HH:MM") | every (seg)
//
// Las reglas se guardan en Config/alertas.json (sobreviven reinicios).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "JsonSignals.hpp"

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ba::app::agent {

using json = nlohmann::json;

struct AlertInputs {
    struct Audio {
        std::string name;
        double peakDb = -100;
        bool muted = false;
    };
    struct Media {
        std::string name;
        std::string state;
        std::int64_t durationMs = 0;
        std::int64_t timeMs = 0;
    };
    struct Status {
        bool streaming = false;
        bool recording = false;
        int droppedFrames = 0;
        int totalFrames = 0;
        /// La salida se corto y esta intentando volver sola. libobs reintenta
        /// por su cuenta; lo que no hace es avisar cuando lleva un rato sin
        /// lograrlo, que es justo el momento en el que hay que hacer algo.
        bool reconnecting = false;
    };
    std::function<std::vector<Audio>()> audio;
    std::function<std::vector<Media>()> media;
    std::function<json()> room;                ///< snapshot de la sala (participants, order, serverNowMs)
    std::function<std::vector<json>()> chat;   ///< {key, platform, author, text}
    std::function<json()> roomChat;            ///< chat de la sala: [{uid, fromId, fromName, to, text, mine}]
    std::function<std::vector<json>()> textos; ///< salidas de texto: {key, flujo, texto, origen}
    std::function<Status()> status;
    std::function<double()> diskFreeGb;  ///< -1 = desconocido
    std::function<std::int64_t()> nowMs; ///< reloj (inyectable en tests)
};

class AlertEngine : public QObject {
    Q_OBJECT
   public:
    AlertEngine(AlertInputs inputs, std::string storePath, QObject* parent = nullptr);

    /// Tipos con su descripcion y parametros (para la ayuda y alert_types).
    static json types();

    /// Crea una regla; devuelve la regla (con id) o {"error": ...}.
    json create(const json& spec);
    json update(const std::string& id, const json& changes);
    bool remove(const std::string& id);
    void clear();
    json list() const;
    /// Eventos con id > sinceId (los ultimos `limit`).
    json events(std::uint64_t sinceId, int limit) const;
    std::uint64_t lastEventId() const { return seq_; }
    /// Llama `cb` con los eventos nuevos apenas haya (o vacio al vencer).
    void wait(std::uint64_t sinceId, int timeoutMs, std::function<void(json)> cb);

    /// Una pasada de evaluacion (la llama el timer; publica para los tests).
    void tick();
    void start(int intervalMs = 250);

   signals:
    /// Un disparo: {id, ruleId, ruleName, type, text, at, notify[], actions[]}.
    void fired(const nlohmann::json& event);

   private:
    struct State {
        std::int64_t overSinceMs = -1;
        std::int64_t lastFiredMs = -1;
        std::map<std::string, std::string> seen; ///< claves ya avisadas / estado previo
        std::map<std::string, std::int64_t> since;
        bool primed = false;
    };
    void evaluate(json& rule, State& st, std::int64_t now);
    void fire(json& rule, State& st, const std::string& text, const json& data, std::int64_t now);
    void save() const;
    void load();
    std::string newId();

    AlertInputs in_;
    std::string storePath_;
    std::vector<json> rules_;
    std::map<std::string, State> state_;
    std::deque<json> events_;
    std::uint64_t seq_ = 0;
    int ruleSeq_ = 0;
    QTimer timer_;
    struct Waiter {
        std::uint64_t since;
        std::int64_t deadline;
        std::function<void(json)> cb;
    };
    std::vector<Waiter> waiters_;
};

} // namespace ba::app::agent
