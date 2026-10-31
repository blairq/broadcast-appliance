// ============================================================================
// src/gateway/MediaPump.hpp — Motor de medios de B (IPC <-> WebRTC)
//
// Proposito:
//   Mueve los medios entre las sesiones WebRTC de los participantes y los
//   canales de memoria compartida con A. QUE va a DONDE lo decide la ruta de
//   cada sesion (MediaRoute), que fija la sala; admitir a alguien cambia su
//   ruta sin tocar su conexion.
//
//   Subida (participante -> B -> A):
//     Program(n): camara  -> BA.Guest.<n>        (tamaño del programa)
//                 pantalla-> BA.GuestShare.<n>   (tamaño del programa)
//                 audio   -> BA.Audio.Guest.<n>
//     Lobby(k):   camara  -> BA.Lobby.<k>        (preview chico, solo operador)
//     Backstage(k): camara -> BA.Backstage.<k>   (fila de oradores: preview
//                  chico a ~10 fps, solo operador)
//                 audio   -> BA.Audio.Lobby.<k>  (solo si el operador le habla
//                                                 o lo atiende el asistente hablando)
//     Atencion:   audio   -> BA.Audio.Atencion   (el que espera en la sesión remota,
//                                                 atendido hablando; nada de video)
//     Registro(k): camara -> BA.Registro.<k>       (la persona que se registra
//                 audio   -> BA.Audio.Registro.<k>  frente a un tótem: la app
//                                                 reconoce la cara y la pasa a texto)
//   Bajada (A -> B -> participante):
//     Program(n): BA.Program + BA.Audio.ContentForReturn (la salida por
//                 omisión); con otra `salida`, BA.Salida.<id> +
//                 BA.Audio.Salida.<id> (lo mismo para Backstage: la fila, las
//                 pantallas y las vistas de los remotos)
//     Lobby(k):   BA.Operator (o una placa si A no lo publica) + la voz del
//                 operador BA.Audio.Operator solo si talk; con atiende (y sin
//                 talk), la voz del asistente BA.Audio.Atendedor
//     Atencion:   la voz del asistente BA.Audio.Atendedor (solo audio)
//     Registro(k): la voz del asistente de ese canal BA.Audio.RegistroVoz.<k>
//                 (solo audio, solo a esa conexión; k = 1..kMaxTotems), más la
//                 voz de quien la atiende por web, si la atiende
//     RegistroAtiende(k): la cámara del tótem del canal k (los mismos cuadros
//                 que suben, achicados) y lo que se oye ahí (la voz de la
//                 persona más la del asistente). Su micrófono, solo si atiende,
//                 baja a ese tótem (ColaVoz.hpp) y no va a ningún otro lado
//
//   Un sender existe siempre (spec §4.2): mientras un slot del programa no
//   tiene video del invitado, B publica una placa.
//
// Threading:
//   - threads de bajada (audio/video del programa, audio/video del operador, la
//     voz del que atiende y una voz por canal de registro) con reintento de
//     apertura cada 500 ms y resuscripcion si A se reinicia.
//   - un par de threads (video y audio) por cada salida que no es la por
//     omisión, BAJO DEMANDA: se levantan cuando una ruta la pide y se sueltan
//     cuando nadie la mira (ajustarBajadas). Con una sola salida no hay ninguno.
//   - 1 thread de placas.
//   - forwardAudio/forwardVideo/forwardScreen: desde los threads de libwebrtc;
//     un lock por canal de salida.
//   - setRoute: desde la sala; la tabla de rutas tiene su propio mutex.
// ============================================================================
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "dsp/Resampler.hpp"
#include "ipc/MedidaTransporte.hpp"
#include "ipc/SharedAudioRing.hpp"
#include "ipc/SpoutTransport.hpp"
#include "MediaRoute.hpp"
#include "Webrtc.hpp"

namespace ba::gateway {

class Log;

class MediaPump {
   public:
    struct Cfg {
        std::wstring returnAudioName;   // BA.Audio.ContentForReturn (A->B)
        // Mix-minus por slot (A->B): el programa sin la voz de ese invitado.
        // Indexado por slot, [0] reservado. Si el de un slot no está abierto, ese
        // invitado NO recibe el retorno común (tiene su voz: eco) sino silencio,
        // «retorno apagado» (RetornoLugar.hpp). Vacío = sin mix-minus (los tests).
        std::vector<std::wstring> returnSlotNames; // BA.Audio.Return.<n>
        std::wstring programVideoName;  // BA.Program (A->B)
        std::wstring operatorVideoName; // BA.Operator (A->B, para el lobby)
        // Escenas que no son el programa, para las pantallas de la sala (A->B):
        // [k] = BA.Escena.<k>. Qué escena va en cada canal lo decide A.
        std::vector<std::wstring> escenaVideoNames;
        std::wstring operatorAudioName; // BA.Audio.Operator (A->B, para el lobby)
        // La voz del asistente que atiende en el lobby (A->B): solo a los que
        // tienen `atiende` en su ruta y el operador no les está hablando.
        std::wstring atendedorAudioName; // BA.Audio.Atendedor
        // La voz del integrante que espera en la sesión remota y lo atiende el asistente
        // hablando (B->A, ruta Atencion): la app la pasa a texto. Vacío = no hay.
        std::wstring atencionAudioName; // BA.Audio.Atencion
        // El registro por tótem (docs/funciones/REGISTRO-TOTEM.md), [0] reservado, 1..kMaxTotems:
        // la voz de la persona (B->A, ruta Registro(k)) y la del asistente que le contesta (A->B,
        // solo a esa conexión). Vacíos = no hay registro.
        std::vector<std::wstring> registroAudioNames;    // BA.Audio.Registro.<k>
        std::vector<std::wstring> registroVozAudioNames; // BA.Audio.RegistroVoz.<k>
        // Y su cámara (B->A): la app reconoce la cara en el video y el operador lo ve en vivo.
        std::vector<std::wstring> registroVideoNames;    // BA.Registro.<k>
        int registroW = 960; // la cara se reconoce de lejos (un tótem): más que el preview del lobby
        int registroH = 540;
        // Indexados por slot, [0] reservado (tamaño guests+1 / lobbySlots+1).
        std::vector<std::wstring> guestAudioNames;     // BA.Audio.Guest.<n>
        std::vector<std::wstring> guestVideoNames;     // BA.Guest.<n>
        std::vector<std::wstring> shareVideoNames;     // BA.GuestShare.<n>
        std::vector<std::wstring> lobbyAudioNames;     // BA.Audio.Lobby.<k>
        std::vector<std::wstring> lobbyVideoNames;     // BA.Lobby.<k>
        std::vector<std::wstring> backstageVideoNames; // BA.Backstage.<k>
        std::vector<std::wstring> remotoAudioNames;    // BA.Audio.Remoto.<k> (R3)
        std::vector<std::wstring> remotoVideoNames;    // BA.Remoto.<k>
        std::vector<std::wstring> remotoContenidoNames; // BA.RemotoContenido.<k>: lo que comparte (pantalla, otra cámara)
        /// Los lugares con canales desde el arranque (el cupo de la sala); los demás
        /// hasta kMaxInvitados se crean al subir el cupo (ampliar). 0 = todos los nombres.
        int lugaresIniciales = 0;
        int audioRate = 48000;
        int audioChannels = 2;
        uint64_t sinVideoNs = 1000000000ULL; // invitado sin video tanto tiempo -> placa (Configuración → Invitados)
        int programW = 1280; // tamaño de los canales del programa (camara/pantalla)
        int programH = 720;
        int lobbyW = 640; // preview del lobby
        int lobbyH = 360;
        int backstageW = 320; // preview de la fila de oradores
        int backstageH = 180;
        int guestVideoW = 640; // retorno al participante
        int guestVideoH = 360;
        // El retorno del PROGRAMA va aparte y mas grande. 640x360 alcanza para
        // el monitor de confianza de alguien que mira su celular, pero la misma
        // imagen en un televisor de 55" se ve mal, y una pantalla de sala es
        // justamente eso. Nunca se agranda mas alla del original.
        int programVideoW = 1280;
        int programVideoH = 720;
    };

    MediaPump(WebrtcHub& hub, const Cfg& cfg, Log& log);
    ~MediaPump();

    MediaPump(const MediaPump&) = delete;
    MediaPump& operator=(const MediaPump&) = delete;

    void start();
    void stop();
    /// Cuántas sesiones reciben el programa ahora (invitados, la fila, televisores, la
    /// consola de la presidencia: toda ruta Program o Backstage). Es lo que la app usa
    /// para saber si puede dejar de bajarle el programa (BroadcastEngine::retornoEnUso):
    /// el que lo sabe de verdad es el gateway, no una lista de casos en la app.
    int miranPrograma() const;
    /// Lo mismo por cada salida que no es la por omisión y que alguien mira ahora:
    /// {id: cuántos}. La app publica BA.Salida.<id> solo mientras está acá.
    std::map<std::string, int> miranSalidas() const;

    /// Las salidas que hay (las manda la app). Las rutas guardan la salida que
    /// pidieron; la que baja de verdad la decide esto (SalidasConocidas), así que
    /// cambiar la lista re-rutea a todos sin que nadie vuelva a pedir nada.
    void ponerSalidas(const SalidasConocidas& salidas);
    /// Las salidas con hilos de bajada levantados ahora (para las pruebas y el log).
    std::vector<std::string> bajadasVivas() const;

    /// Crea los canales (cámara, pantalla, audio) de los lugares hasta `slots`
    /// (a lo sumo kMaxInvitados) que todavía no estén. Se llama al subir el cupo,
    /// ANTES de que la sala admita a alguien ahí: así el cupo cambia sin reiniciar
    /// la sala. Nunca borra canales (bajar el cupo no saca a nadie).
    void ampliar(int slots);
    /// Cuántos lugares tienen canales.
    int lugares() const;

    /// Los lugares del programa con un invitado adentro que no recibe retorno: su mix-minus
    /// (BA.Audio.Return.<n>) no está abierto, y el programa entero no se le manda porque
    /// tiene su propia voz (RetornoLugar.hpp). La sala lo publica en su participante.
    std::vector<int> lugaresSinRetorno() const;
    /// Cómo andan los anillos de audio y los canales de video que lee el gateway: la foto
    /// de la última ventana de 10 s de cada uno (ipc/MedidaTransporte). Va al uso de la sala
    /// (la foto de B que lee la app) y de ahí a `sistema_recursos`.
    nlohmann::json transporte() const;
    /// Lo que mueve la bajada de video desde que arrancó (F2-13): {cuadros, leidos_bytes
    /// (del canal), escalados_bytes, empujes (cuadro × sesión), demorados (los que esperaron
    /// a su audio), conversiones y convertidos_bytes (a I420: una por cuadro, no por sesión)}.
    nlohmann::json copiasVideo() const;

    /// Ruta de la sesion `session` (1..hub.sessions()).
    void setRoute(int session, const MediaRoute& route);
    /// La ruta vigente: la pedida, con la salida que baja de verdad (ver ponerSalidas).
    MediaRoute route(int session) const;

    /// Subida desde la sesion `session` (se rutea segun su MediaRoute).
    void forwardAudio(int session, const float* samples, size_t n, int rate);
    void forwardVideo(int session, const uint8_t* pixels, size_t bytes, int w, int h);
    void forwardScreen(int session, const uint8_t* pixels, size_t bytes, int w, int h);
    /// El remoto del canal `canal` dejó de mandar (se le sacó la palabra o se fue):
    /// un cuadro negro en su video, para que la entrada no se quede con la cara del
    /// anterior (a los 3 s el motor pone «sin señal»).
    void apagarRemoto(int canal);
    /// La conversación del canal de registro `canal` terminó: un cuadro negro en su cámara.
    void apagarRegistro(int canal);

    /// Prueba técnica de un invitado admitido (T33): durante `ms` le suma al retorno un
    /// tono de 1 kHz cortado (250 ms sí, 250 no) y mira cuánto de ese tono vuelve por su
    /// micrófono (correlación con el patrón, a cualquier demora hasta 1 s). Bloquea `ms`.
    /// {eco_db (cuánto más 1 kHz hay con el tono que sin), demora_ms, eco: bool, red: {…}}.
    std::string pruebaTecnica(int session, int ms);
    /// El código seudoaleatorio de la prueba (tramos de 250 ms): true = tono.
    static bool codigoPrueba(long long tramo);
    /// El análisis de la prueba: energía de 1 kHz del micrófono en trozos de 20 ms
    /// (-1 = sin dato) contra el código, a demoras de 0 a 1 s. {eco_db, eco, demora_ms…}.
    static nlohmann::json analizarEco(const std::vector<double>& bins, int dur);

   private:
    /// Canal de video B->A con tamaño fijo (el frame se ajusta adentro).
    struct VideoOut {
        ba::ipc::SpoutSender sender;
        std::mutex lock;
        std::vector<uint8_t> scratch;
        int w = 0;
        int h = 0;
        std::atomic<uint64_t> lastNs{0}; // ultimo frame REAL (no placa)
        uint64_t minIntervalNs = 0;      // 0 = todos los cuadros; si no, tope de fps
        // A cuantos cuadros por segundo esta LLEGANDO lo que sube el
        // participante. Es la mitad que faltaba para saber donde se va el
        // retardo: si el telefono entrega 30 parejos, lo que se ve tarde se
        // esta tardando mas adelante; si entrega 12 a los tirones, el camino
        // de subida es el problema y no hay nada que apurar de este lado.
        std::string nombre;
        uint64_t informeNs = 0;
        uint64_t cuadros = 0;
    };
    /// Canal de audio B->A: ring + re-bloqueo a 10 ms + re-muestreo.
    struct AudioOut {
        ba::ipc::SharedAudioRing ring;
        std::mutex lock;
        std::vector<float> buf;
        size_t pos = 0;
        std::unique_ptr<ba::dsp::Resampler> resampler;
        int resamplerRate = 0;
        std::vector<float> resampleOut;
    };

    /// La voz del operador para los de adentro que tienen «hablar» prendido (IFB): el
    /// hilo del retorno de SU salida la mezcla con el sonido (una sola fuente por
    /// sesión). Una cola por retorno: cada salida tiene el suyo.
    struct ColaIfb {
        std::mutex lock;
        std::deque<float> voz;
    };
    /// El audio de retorno de la salida `salida` ("" = la por omisión, con los
    /// mix-minus por lugar) a las sesiones que la oyen, con el IFB de `ifb` y el tono
    /// de la prueba técnica. Corre hasta stop() o hasta que `parar` se prenda.
    void retornoAudioThread(std::wstring nombre, std::string salida, ColaIfb* ifb, const std::atomic<bool>* parar);
    /// Un canal de video A->B hacia las sesiones que cumplen `para` (escalado a
    /// lo sumo a programVideoW x programVideoH). El retorno del programa, cada
    /// canal de escena y cada salida son esto mismo con otro nombre y otro filtro.
    /// Cada cuadro se convierte una vez para todas (CuadroSaliente). `audio`: el anillo de
    /// su sonido (BA.Audio.ContentForReturn, BA.Audio.Salida.<id>): a quien mira y no
    /// conversa, el cuadro le sale a la hora de ese audio (Labios.hpp, F2-11); vacío = sin
    /// alinear (una escena). `parar`: el de una bajada bajo demanda (nullptr = hasta stop()).
    void videoBajadaThread(std::wstring nombre, std::string etiqueta, std::function<bool(const MediaRoute&)> para,
                           std::wstring audio, const std::atomic<bool>* parar = nullptr);
    /// Levanta los hilos de las salidas que alguna ruta mira y suelta los de las que
    /// ya nadie mira. Después de cada cambio de rutas o de salidas.
    void ajustarBajadas();
    bool sigue(const std::atomic<bool>* parar) const {
        return !stop_.load(std::memory_order_relaxed) && (parar == nullptr || !parar->load(std::memory_order_relaxed));
    }
    void operatorAudioThread();
    /// Una voz sintetizada A->B (`nombre`) a las sesiones que cumplen `para`, sin
    /// mezclar con nada: la del que atiende (MediaRoute::oyeAlAtendedor) y la de cada
    /// canal de registro (MediaRoute::oyeVozRegistro) son esto mismo.
    void vozAudioThread(std::wstring nombre, std::string etiqueta, std::function<bool(const MediaRoute&)> para);
    /// La voz del asistente del canal de registro `canal`, con lo que hay que mezclarle:
    /// al tótem, la voz de quien lo atiende por web; a quien lo atiende, la de la persona.
    void vozRegistroThread(std::wstring nombre, int canal);
    void operatorVideoThread();
    void plateThread();

    /// Cada 10 s (de `desde`), la foto de la ventana de `m` a transporte_; al registro solo si
    /// hubo algo raro (o al volver a la normalidad), con `registrar`. Desde el hilo dueño de `m`.
    /// true si cerró la ventana (y la foto, en `fuera` si no es nullptr).
    bool cerrarVentana(ba::ipc::MedidaTransporte& m, std::chrono::steady_clock::time_point& desde, bool registrar,
                       ba::ipc::FotoTransporte* fuera = nullptr);

    std::unique_ptr<VideoOut> crearVideo(const std::wstring& nombre, int w, int h);
    std::unique_ptr<AudioOut> crearAudio(const std::wstring& nombre);

    void publishVideo(VideoOut& out, const uint8_t* pixels, int w, int h, bool real);
    void publishAudio(AudioOut& out, const float* samples, size_t n, int rate);
    /// Sesiones cuya ruta cumple `pred` (copia de la tabla bajo lock).
    template <typename Pred> std::vector<int> sessionsWhere(Pred pred) const;

    WebrtcHub& hub_;
    Cfg cfg_;
    Log& log_;
    std::atomic<bool> stop_{false};
    std::vector<std::thread> threads_;

    // Los de los invitados del programa: lugar para kMaxInvitados, creados a pedido
    // (ampliar). Los hilos de medios leen el puntero atómico sin lock; los canales se
    // crean bajo canalesLock_ y viven hasta el destructor (nunca se borran andando).
    std::array<std::atomic<VideoOut*>, kMaxInvitados + 1> guestVideo_{};
    std::array<std::atomic<VideoOut*>, kMaxInvitados + 1> shareVideo_{};
    std::array<std::atomic<AudioOut*>, kMaxInvitados + 1> guestAudio_{};
    mutable std::mutex canalesLock_;
    std::vector<std::unique_ptr<VideoOut>> videosInvitado_; // los dueños
    std::vector<std::unique_ptr<AudioOut>> audiosInvitado_;
    int lugares_ = 0; // con canales (bajo canalesLock_)

    // [0] reservado en todos.
    std::vector<std::unique_ptr<VideoOut>> lobbyVideo_;
    std::vector<std::unique_ptr<VideoOut>> backstageVideo_;
    std::vector<std::unique_ptr<AudioOut>> lobbyAudio_;
    std::vector<std::unique_ptr<VideoOut>> remotoVideo_;
    std::vector<std::unique_ptr<VideoOut>> remotoContenido_;
    std::vector<std::unique_ptr<AudioOut>> remotoAudio_;
    std::unique_ptr<AudioOut> atencionAudio_; // BA.Audio.Atencion (ruta Atencion)
    std::vector<std::unique_ptr<AudioOut>> registroAudio_; // BA.Audio.Registro.<k> ([0] reservado)
    std::vector<std::unique_ptr<VideoOut>> registroVideo_; // BA.Registro.<k> ([0] reservado)
    // La atención remota del registro, por canal ([0] reservado): las voces que esperan a
    // mezclarse en la bajada del asistente (ColaVoz.hpp) y el cuadro achicado para quien mira.
    struct MonitorRegistro {
        std::mutex lock;
        std::deque<float> persona; // la voz de la persona del tótem, para quien la atiende
        std::deque<float> habla;   // la voz de quien la atiende, para el tótem
        std::vector<uint8_t> video;
        uint64_t videoNs = 0;
    };
    std::array<MonitorRegistro, kMaxTotems + 1> monitorRegistro_;

    ColaIfb ifb_; // el de la salida por omisión
    // Las bajadas de las otras salidas, por id (bajo demanda). Orden de candados:
    // bajadasLock_ y adentro routesLock_, nunca al revés.
    struct BajadaSalida {
        std::atomic<bool> parar{false};
        ColaIfb ifb;
        std::thread video, audio;
    };
    mutable std::mutex bajadasLock_;
    std::map<std::string, std::unique_ptr<BajadaSalida>> bajadas_;
    std::atomic<bool> enMarcha_{false}; // entre start() y stop(): se pueden levantar bajadas
    // Prueba técnica: la sesión que se prueba, la fase del tono y la energía de 1 kHz
    // que vuelve por su micrófono, en trozos de 20 ms desde que empezó.
    std::atomic<int> pruebaSesion_{0};
    std::atomic<std::int64_t> pruebaDesdeNs_{0};
    double pruebaFase_ = 0;
    std::mutex pruebaLock_;
    std::vector<double> pruebaBins_;

    // El mix-minus de cada lugar abierto (lo escribe el hilo del retorno por omisión).
    std::array<std::atomic<bool>, kMaxInvitados + 1> mixMinusAbierto_{};
    // La última foto de cada canal que se lee, por nombre, y si estaba anómala (para avisar
    // una sola vez cuando vuelve a la normalidad).
    struct FotoCanal {
        ba::ipc::FotoTransporte foto;
        bool anomala = false;
    };
    mutable std::mutex transporteLock_;
    std::map<std::string, FotoCanal> transporte_;
    struct Copias {
        std::atomic<std::uint64_t> cuadros{0}, leidos{0}, escalados{0}, empujes{0}, demorados{0};
    };
    Copias copias_;

    mutable std::mutex routesLock_;
    std::vector<MediaRoute> routes_;  // por sesion, [0] reservado: la vigente (con la salida que baja)
    std::vector<MediaRoute> pedidas_; // por sesion: la que pidio la sala, tal cual
    SalidasConocidas salidas_;        // bajo routesLock_
};

} // namespace ba::gateway
