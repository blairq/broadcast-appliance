// ============================================================================
// src/gateway/Room.hpp — Estado de la sala: lobby, participantes, chat, medios
//
// Proposito:
//   El MODELO de la conferencia, sin sockets ni WebRTC: quien espera en el
//   lobby, quien esta adentro y en que slot, que compartio cada uno y el chat.
//   Lo usan las dos superficies del gateway:
//     - la de invitados (WebSocket de sala), que traduce mensajes del navegador
//       a llamadas de este modelo;
//     - la API de control (REST + SSE), que es la unica forma de operarlo; de
//       ella cuelgan la vista web y la vista Qt, que asi no duplican logica.
//
//   Ciclo de vida de un participante:
//     connect()  -> "waiting" (lobby) o "admitted" directo si el lobby esta
//                   apagado y hay slot libre.
//     admit()    -> "admitted": se le asigna un slot 1..maxGuests.
//     reject()/kick()/leave() -> se va (y libera el slot).
//     disconnect() -> sigue en la sala `graceMs` esperando que vuelva (una
//                   recarga o un corte de red no le hacen perder el lugar);
//                   resume() con su clave lo reengancha. tick() purga.
//
// Salidas:
//   Los cambios se informan por `Sinks` (mensajes al invitado, eventos a los
//   operadores, cortes de conexion, slots liberados). Se invocan SIEMPRE con
//   el lock SUELTO: cada operacion junta sus salidas y las despacha al final,
//   asi un sink que escribe en un socket nunca bloquea la sala entera.
//
// Threading: thread-safe (un mutex interno).
// ============================================================================
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "MediaRoute.hpp"

namespace ba::gateway {

class Room {
   public:
    /// Codigos de resultado de las operaciones (la API los traduce a HTTP).
    enum Err : int {
        kOk = 0,
        kNotFound = 1,  ///< participante/archivo inexistente
        kBadState = 2,  ///< la operacion no aplica en su estado actual
        kFull = 3,      ///< no hay slot libre
        kLocked = 4,    ///< sala cerrada a nuevos ingresos
        kLobbyFull = 5, ///< demasiada gente esperando
        kBadKey = 6,    ///< clave de reconexion invalida
        kEmpty = 7,     ///< mensaje vacio
    };

    enum class State { Waiting, Admitted };

    struct Participant {
        std::string id;
        std::string key; ///< secreto del participante (reconexion + subidas); no sale en snapshots
        std::string name;
        std::string ip;
        State state = State::Waiting;
        int slot = 0;      ///< 1..maxGuests cuando esta admitido
        int lobbySlot = 0; ///< 1..lobbySlots mientras espera (su camara la ve el operador)
        bool talk = false; ///< el operador le esta hablando (conversacion privada del lobby)
        bool connected = false;
        bool mediaUp = false; ///< PeerConnection establecida
        bool mic = true;
        bool cam = true;
        bool screen = false; ///< compartiendo pantalla/ventana
        bool hand = false;   ///< mano levantada
        int64_t joinedMs = 0;
        int64_t admittedMs = 0;
        int64_t lastSeenMs = 0;
    };

    /// Archivo que un invitado compartio (imagen, video o audio).
    struct Share {
        std::string id;
        std::string participantId;
        std::string participantName;
        std::string name; ///< nombre original (saneado), solo para mostrar
        std::string kind; ///< "image" | "video" | "audio"
        std::string mime;
        std::string path; ///< ruta local en disco (la usa A para tomarlo)
        uint64_t bytes = 0;
        int64_t atMs = 0;
    };

    struct ChatMsg {
        uint64_t seq = 0;
        int64_t atMs = 0;
        std::string fromId; ///< "host" si lo escribio el operador
        std::string fromName;
        std::string to; ///< "all" o id de participante
        std::string text;
    };

    struct Cfg {
        std::string name = "sala1";
        int maxGuests = 2;
        bool lobby = true;   ///< true: el operador admite a cada uno
        int graceMs = 30000; ///< cuanto se guarda el lugar de un desconectado
        int maxWaiting = 16; ///< tope del lobby
        int lobbySlots = 4;  ///< cuantos del lobby pueden mostrar su camara a la vez
        size_t chatKeep = 200;
    };

    struct Sinks {
        /// Mensaje JSON para UN invitado (por su WebSocket).
        std::function<void(const std::string& participantId, const nlohmann::json& msg)> toGuest;
        /// Evento para TODOS los operadores (SSE). `event`: "room" | "chat".
        std::function<void(const std::string& event, const nlohmann::json& data)> toHosts;
        /// Cortar la conexion del invitado (despues de entregarle su mensaje).
        std::function<void(const std::string& participantId)> dropGuest;
        /// El slot quedo libre: la sesion WebRTC de ese slot debe cerrarse.
        std::function<void(int slot, const std::string& participantId)> slotFreed;
        /// Cambio la ruta de medios de un participante (lobby -> programa,
        /// hablar/no hablar, None al irse). El servidor la aplica a su sesion.
        std::function<void(const std::string& participantId, const MediaRoute& route)> route;
    };

    explicit Room(Cfg cfg);

    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;

    /// Conecta las salidas. Llamar antes de usar la sala.
    void setSinks(Sinks sinks);
    /// Datos que ven los operadores y no el invitado (PIN, enlace de ingreso).
    void setHostInfo(const std::string& pin, const std::string& guestUrl);

    // --- Lado invitado ------------------------------------------------------
    struct Joined {
        int err = kOk;
        std::string id;
        std::string key;
    };
    /// Entra a la sala (WebSocket recien abierto con un token valido).
    /// `presetId`: id ya elegido por el servidor (vacio = lo genera la sala).
    Joined connect(const std::string& name, const std::string& ip, int64_t nowMs,
                   const std::string& presetId = {});
    /// Reengancha a un participante desconectado (recarga, corte de red).
    int resume(const std::string& id, const std::string& key, int64_t nowMs);
    /// Se corto su conexion: conserva el lugar `graceMs`.
    void disconnect(const std::string& id, int64_t nowMs);
    /// Se fue a proposito: libera el lugar ya.
    void leave(const std::string& id);
    /// Estado de sus medios que informa el navegador (mic, cam, screen, hand).
    void guestMedia(const std::string& id, const nlohmann::json& media);
    void setMediaUp(const std::string& id, bool up);
    int chatFromGuest(const std::string& id, const std::string& text, int64_t nowMs);

    // --- Lado operador ------------------------------------------------------
    int admit(const std::string& id, int64_t nowMs);
    int reject(const std::string& id);
    int kick(const std::string& id);
    /// Conversacion privada con alguien del lobby: le llega la voz del operador
    /// (y el operador escucha la suya). Solo aplica a quien espera.
    int setTalk(const std::string& id, bool on);
    /// Pedido al invitado: "mute-mic", "mute-cam", "stop-screen", "lower-hand".
    int request(const std::string& id, const std::string& what);
    /// `to`: "all" o el id de un participante.
    int chatFromHost(const std::string& to, const std::string& text, int64_t nowMs);
    void setLobby(bool on, int64_t nowMs);
    void setLocked(bool on);

    // --- Archivos compartidos -----------------------------------------------
    /// Registra un archivo ya guardado en disco. :returns: su id.
    std::string addShare(Share share, int64_t nowMs);
    /// Quita un archivo. `pathOut` recibe la ruta para borrarlo del disco.
    bool removeShare(const std::string& id, std::string* pathOut);
    bool share(const std::string& id, Share& out) const;
    /// Todos los archivos de un participante (para limpiar al irse).
    std::vector<Share> sharesOf(const std::string& participantId) const;

    // --- Consultas ----------------------------------------------------------
    int slotOf(const std::string& id) const;
    /// Ruta de medios vigente del participante.
    MediaRoute routeOf(const std::string& id) const;
    /// Participante admitido dueño de `key` (autoriza subidas).
    bool admittedByKey(const std::string& key, Participant& out) const;
    bool participant(const std::string& id, Participant& out) const;
    /// Estado completo para los operadores.
    nlohmann::json snapshot() const;
    const Cfg& cfg() const { return cfg_; }

    /// Purga desconectados vencidos. Llamar periodicamente (~1 s).
    void tick(int64_t nowMs);

    /// Nombre saneado: sin controles, recortado, nunca vacio.
    static std::string cleanName(const std::string& raw);
    /// Texto de chat saneado (sin controles salvo salto de linea, <= 1000 bytes).
    static std::string cleanText(const std::string& raw);

   private:
    struct Out {
        std::vector<std::pair<std::string, nlohmann::json>> guest;
        std::vector<std::pair<std::string, nlohmann::json>> hosts;
        std::vector<std::string> drop;
        std::vector<std::pair<int, std::string>> freed;
        std::vector<std::pair<std::string, MediaRoute>> routes;
        bool roomChanged = false;
    };
    void flush(Out& out);

    Participant* findLocked(const std::string& id);
    const Participant* findLocked(const std::string& id) const;
    int freeSlotLocked() const;
    void admitLocked(Participant& p, int slot, int64_t nowMs, Out& out);
    /// Quita un participante (libera su slot) y reordena el lobby.
    void removeLocked(const std::string& id, Out& out);
    /// Lobby apagado: hace pasar a los que esperan mientras haya slots.
    void autoAdmitLocked(int64_t nowMs, Out& out);
    /// Informa a cada uno de los que esperan su posicion en la fila.
    void lobbyPositionsLocked(Out& out);
    /// Reparte los lugares de preview del lobby entre los que esperan.
    void assignLobbySlotsLocked(Out& out);
    static MediaRoute routeFor(const Participant& p);
    void pushChatLocked(ChatMsg msg, Out& out);
    nlohmann::json snapshotLocked() const;
    static nlohmann::json toJson(const Participant& p);
    static nlohmann::json toJson(const Share& s);
    static nlohmann::json toJson(const ChatMsg& c);

    mutable std::mutex m_;
    Cfg cfg_;
    Sinks sinks_;
    bool locked_ = false;
    std::string pin_;
    std::string guestUrl_;
    std::vector<Participant> people_; // orden de llegada (el lobby es FIFO)
    std::vector<Share> shares_;
    std::deque<ChatMsg> chat_;
    uint64_t chatSeq_ = 0;
    int64_t lastNowMs_ = 0; // ultimo "ahora" visto (para admisiones sin reloj propio)
};

} // namespace ba::gateway
