// ============================================================================
// src/app/agent/AlertEngine.hpp — Disparadores programables (alertas)
//
// El asistente de IA no tiene que "mirar cada tanto" con temporizadores: le
// pide al servidor que vigile algo (alert_create) y el servidor le AVISA
// cuando pasa. Se evalua cada 250 ms contra el estado real (niveles de audio,
// medios, sala, chats, salidas, disco) EN EL HILO DEL PLANO DE CONTROL (F3-03,
// arranque/PlanoControl): con la interfaz trabada, las alarmas siguen. Lo que
// es de la interfaz llega como foto (lo copia ella); lo del motor que sale al
// aire (silencio y saturación del programa, negro, congelado, transmisión y
// grabación caídas, loudness, persistencia, recursos), directo. Seguro entre
// hilos: todo lo público toma su candado. Cada disparo es un evento que:
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
//   superres_tarde (la superresolución de una entrada no llega a tiempo)
//   guest_drop    (un invitado admitido se desconecta)
//   chat          platform (all|youtube|twitch|facebook|room), contains | regex
//   guest_message un invitado escribe en el chat de la sala (privateOnly)
//   texto         una SALIDA DE TEXTO (una «Voz a texto» con nombre) dice algo
//                 que coincide: flujo ("*" o el nombre), contains | palabras |
//                 regex (sin distinguir mayúsculas ni tildes)
//   stream_health droppedPct (2), windowSec (30); y corte de la transmision
//   disk          minGb (5) | minMinutos (las horas que quedan al bitrate de TODO lo que
//                 se graba, por disco), siempre; y que la copia de una grabación va al
//                 mismo disco que ella (mismoVolumen, true)
//   program_black forSec (3), umbral (16 de 255), onlyWhenLive (true)
//   program_frozen forSec (30), onlyWhenLive (true)
//   record_down   forSec (5): la grabación se cortó sin que nadie la parara
//   recursos      recurso ("*"|cpu|gpu|ram|vram|red|motor), nivel (aviso|critico):
//                 la máquina está al límite (sostenido; lo mide diag::Recursos)
//   persistencia  el estado guardado volvió dañado o la carpeta de datos está en FAT/exFAT
//   energia       se cortó la luz (batería/UPS) y, después, la política cerró las grabaciones
//   timer         in (seg) | at ("HH:MM") | every (seg)
//
// Las reglas se guardan en Config/alertas.json (sobreviven reinicios).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "comun/JsonSignals.hpp"

#include <deque>
#include <mutex>
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
        bool grabacionPedida = false; ///< alguien la puso a grabar (y no la paró): si no graba, se cortó sola
        /// Las salidas (ids) con la grabación pedida y cortada: el motor la está rearmando en otra parte.
        std::vector<std::string> grabacionesCaidas;
    };
    std::function<std::vector<Audio>()> audio;
    std::function<std::vector<Media>()> media;
    std::function<json()> room;                ///< snapshot de la sala (participants, order, serverNowMs)
    std::function<std::vector<json>()> chat;   ///< {key, platform, author, text}
    std::function<json()> roomChat;            ///< chat de la sala: [{uid, fromId, fromName, to, text, mine}]
    std::function<std::vector<json>()> textos; ///< salidas de texto: {key, flujo, texto, origen}
    std::function<Status()> status;
    /// El disco (AppRuntime::discoGrabacion): {libre_gb, horas (-1 = no se sabe), mismo_volumen[{salida, grabacion, copia}]}.
    std::function<json()> disco;
    /// Las entradas de video y si tienen señal ({nombre, con_senal}).
    std::function<std::vector<std::pair<std::string, bool>>()> senales;
    /// El loudness de lo que sale ({i, tp_reciente, objetivo{lufs, tp}}).
    std::function<json()> loudness;
    /// La superresolución de cada entrada: {entrada: {tarde, ms, bypass_s}} (programador).
    std::function<json()> superres;
    /// El programa en imagen: {valido, luma (0–255), quieto_s} (alarmas de negro y congelado).
    std::function<json()> programaVideo;
    /// Los recursos de la máquina: {cpu: {estado, valor, sostenido_s}, …} (diag::Recursos::niveles).
    std::function<json()> recursos;
    /// Avisos de la persistencia (AppRuntime::avisosPersistencia): cada uno suena una vez.
    std::function<std::vector<std::string>()> persistencia;
    /// La energía (arranque/EnergiaAire::estado): {en_bateria, porcentaje, minutos, fuente, accion}.
    std::function<json()> energia;
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
    /// Los disparadores de fábrica (silencio y saturación del programa, la
    /// transmisión, el disco, entradas sin señal, invitados caídos): se siembran
    /// en el primer arranque; `restablecer` vuelve a ponerlos como vinieron.
    json fabrica(bool restablecer);
    /// Las alarmas que esperan que alguien las reconozca (las de `reconocer`).
    json activas() const;
    /// Reconoce una (por id de evento) o todas (0). Devuelve cuántas.
    int reconocer(std::uint64_t eventoId);
    /// El resultado de lo que hizo un disparo (lo anota quien ejecuta las acciones).
    void anotarResultado(std::uint64_t eventoId, const std::string& herramienta, bool ok, const std::string& texto,
                         bool bloqueadaPorRol);
    json list() const;
    /// Eventos con id > sinceId (los ultimos `limit`).
    json events(std::uint64_t sinceId, int limit) const;
    std::uint64_t lastEventId() const;
    /// Llama `cb` con los eventos nuevos apenas haya (o vacio al vencer).
    void wait(std::uint64_t sinceId, int timeoutMs, std::function<void(json)> cb);

    /// Una pasada de evaluacion (la llama el timer; publica para los tests).
    void tick();
    void start(int intervalMs = 250);

   signals:
    /// Un disparo: {id, ruleId, ruleName, type, text, at, notify[], actions[]}.
    void fired(const nlohmann::json& event);
    /// Cambiaron las alarmas por reconocer.
    void alarmasCambio();
    /// Cambiaron las reglas (crear, cambiar, borrar, restablecer las de fábrica).
    /// Lo emite el motor, no quien lo llamó: la vista se entera sola venga el
    /// cambio de la interfaz, del MCP o de un plugin.
    void reglasCambio();
    /// Algo cambió en el evento (entró o se fue alguien, una entrada tomó o perdió señal, el
    /// aire o la grabación): para que los asistentes lo tengan en contexto, haya o no
    /// disparadores. El texto no lleva nada escrito por invitados (solo ids).
    void novedad(const QString& texto);

   private:
    std::vector<json> activas_; ///< eventos con `reconocer` todavía sin reconocer
    /// Las reglas de fábrica; con `faltantes`, solo las de tipos que nunca se ofrecieron (las nuevas, al actualizar).
    void sembrar(bool faltantes = false);
    struct State {
        std::int64_t overSinceMs = -1;
        std::int64_t lastFiredMs = -1;
        std::map<std::string, std::string> seen; ///< claves ya avisadas / estado previo
        std::map<std::string, std::int64_t> since;
        bool primed = false;
    };
    void evaluate(json& rule, State& st, std::int64_t now);
    /// Compara con la vuelta anterior y emite novedad() por cada cambio.
    void novedades(std::int64_t now);
    std::map<std::string, std::string> previo_;       ///< el estado de la vuelta anterior (novedades)
    std::map<std::string, std::int64_t> cambioDesde_; ///< señal de una entrada: desde cuándo es distinta
    bool novedadesListas_ = false;                    ///< la primera vuelta solo anota

    void fire(json& rule, State& st, const std::string& text, const json& data, std::int64_t now);
    void save() const;
    void load();
    std::string newId();

    /// Recursivo: un disparo emite y despierta a quien espera, y eso puede volver acá
    /// (anotarResultado, events) en el mismo hilo.
    mutable std::recursive_mutex mu_;
    AlertInputs in_;
    std::string storePath_;
    std::vector<json> rules_;
    std::map<std::string, State> state_;
    std::deque<json> events_;
    std::uint64_t seq_ = 0;
    int ruleSeq_ = 0;
    std::vector<std::string> sembrados_; ///< tipos de fábrica ya ofrecidos (borrados a propósito no vuelven)
    QTimer timer_;
    struct Waiter {
        std::uint64_t since;
        std::int64_t deadline;
        std::function<void(json)> cb;
    };
    std::vector<Waiter> waiters_;
};

} // namespace ba::app::agent
