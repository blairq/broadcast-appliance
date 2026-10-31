// ============================================================================
// src/emisor/Emisor.hpp — la salida de una salida, en un proceso aparte (F3-01)
//
// docs/arquitectura/04-plan-unificado.md, F3-01 (T1, T2). Lo que la app publica de
// una salida (el video en su canal y el audio en el anillo del emisor) entra acá y
// sale codificado, grabado y transmitido con las MISMAS clases de la app
// (motor::Grabacion, motor::Transmision, motor::Repeticion: el codificador
// compartido, la grabación en tramos, la lista negra, el ingest de respaldo). Si la
// app se cae, este proceso sigue: la conexión RTMP/SRT no se corta y el archivo no se
// parte; mientras la app no publica, sale la PLACA («Volvemos enseguida») con silencio.
//
//   video:  bavideoanillo canal=BA.Emisor.<id> (NV12, del tamaño de la salida) ! al
//           tamaño y los cuadros del pedido ─► input-selector ─► intersink video:emisor:<id>
//           placa (un cuadro rotulado congelado, en vivo) ─┘
//   limpio: lo mismo con BA.Emisor.<id>.limpio ─► video:emisor:<id>:limpio (solo si se pide)
//   audio:  baanillo canal=BA.Audio.Emisor.<id> ─► audiomixer (en vivo) ─► intersink bus:emisor:<id>
//           silencio (en vivo, siempre)            ─┘
//   pistas: lo mismo con BA.Audio.Emisor.<id>.pista<n> ─► bus:emisor:<id>:pista<n> (las que graba)
//
// Lo que sale de ahí: la transmisión principal y los destinos extra (video y audio
// del programa), la grabación y su copia (con las pistas pedidas), la repetición y la
// señal limpia (su video, el audio del programa): lo mismo que la app arma para una
// salida sin emisor (BroadcastEngine), con la misma configuración.
//
// EL CONMUTADOR DE VIDEO no espera (como la placa de las entradas, Entrada.hpp): el
// vigilante de tic pasa a la placa si el canal no trae nada por `emisor.sinSenalMs`
// (500 ms de fábrica) y la sonda de la entrada vuelve a la señal con el primer
// cuadro. EL AUDIO no conmuta: el silencio está siempre en la mezcla y el
// audiomixer, en vivo, sale a su hora aunque el anillo no traiga nada; así no hay
// huecos en el AAC ni en el FLV.
//
// Lo que se pide (Pedido.hpp) se aplica comparando: lo que no cambió sigue como está
// (una app que vuelve y vuelve a pedir lo mismo no corta nada).
//
// Hilos: tic(), aplicar(), parar() y estado() desde un solo hilo (el bucle de main).
// Guardar la repetición junta tramos (tarda): va en un hilo propio, como en la app.
// ============================================================================
#pragma once

#include "Edad.hpp"
#include "Pedido.hpp"
#include "Reloj.hpp"
#include "Salidas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ba::emisor {

class Emisor {
   public:
    struct Config {
        std::string id = "principal";
        std::string canalVideo;  ///< "" = BA.Emisor.<id> (CanalesSalida.hpp)
        std::string canalAudio;  ///< "" = BA.Audio.Emisor.<id>; las pistas, <canalAudio>.pista<n>
        std::string canalLimpio; ///< "" = BA.Emisor.<id>.limpio
    };
    Emisor(motor::Reloj& reloj, Config cfg);
    ~Emisor();
    Emisor(const Emisor&) = delete;
    Emisor& operator=(const Emisor&) = delete;

    /// Lo pedido: arma las entradas (con el tamaño del pedido) y prende, cambia o para
    /// cada salida según lo que haya cambiado.
    void aplicar(const Pedido& p);
    /// Una vuelta (~100 ms): las entradas (errores, la placa) y las salidas (se rearman solas).
    void tic();
    /// Cierre ordenado: para las transmisiones y cierra los archivos (EOS). Devuelve los
    /// archivos que escribió la grabación principal (para pasarlos a MP4/MOV).
    std::vector<std::string> parar();
    /// {id, seq, placa, placas, video_hace_ms, audio_hace_ms, edad_entrada, error,
    ///  transmision: Transmision::estado() | null, grabacion, copia: Grabacion::estado() | null,
    ///  extras: {id: Transmision::estado()}, pistas: [n], replay: {activa, segundos, ultimo,
    ///  guardando, guardado_seq, error} | null, limpio: {placa, transmision, grabacion} | null}
    nlohmann::json estado() const;
    /// En la placa ahora (la app no publica).
    bool enPlaca() const { return video_.placa.load(); }
    /// Lo último pedido (vacío si no se pidió nada).
    const Pedido& pedido() const { return pedido_; }

   private:
    struct Rama {
        GstElement* pipeline = nullptr;
        std::int64_t reintentoNs = 0;
        std::string error;
    };
    /// Una entrada de video: un canal, con su placa.
    struct Video {
        std::string canal, productor;
        Rama rama;
        std::atomic<bool> placa{true};
        std::atomic<std::int64_t> ultimoUs{0};
        std::uint64_t placas = 0; ///< veces que pasó a la placa
        int ancho = 0, alto = 0, fpsN = 0, fpsD = 0;
        std::string texto;
        motor::MedidorEdad edad; ///< la edad de lo que entra a codificar (el reloj del estudio)
    };
    /// Una entrada de audio: un anillo, con el silencio siempre en la mezcla.
    struct Audio {
        std::string canal, productor;
        Rama rama;
        std::atomic<std::int64_t> ultimoUs{0};
    };
    /// Una salida armada y lo pedido con que se armó (para comparar).
    template <class T>
    struct Armada {
        std::unique_ptr<T> salida;
        nlohmann::json cfg;
    };
    /// La repetición: compartida con el hilo que guarda.
    struct Repeticion {
        std::shared_ptr<motor::Repeticion> rep;
        nlohmann::json cfg;
        std::uint64_t guardadoSeq = 0; ///< el último `guardarSeq` atendido
        std::thread guardando;
        std::shared_ptr<std::atomic<bool>> ocupada = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::mutex> mu = std::make_shared<std::mutex>();
        std::shared_ptr<std::string> ultimo = std::make_shared<std::string>(), error = std::make_shared<std::string>();
    };

    void armarVideo(Video& v);
    void armarAudio(Audio& a);
    void desarmar(Rama& r, motor::MedidorEdad* edad = nullptr);
    /// Errores del pipeline: se desarma y se vuelve a armar en un segundo. `armar` arma.
    void atender(Rama& r, const std::string& que, const std::function<void()>& armar, motor::MedidorEdad* edad = nullptr);
    /// Pasa a la placa si el canal no trae nada hace `emisor.sinSenalMs`.
    void vigilarPlaca(Video& v);
    /// Que el video tenga el tamaño, los cuadros y el texto de la placa de ahora.
    void ajustarVideo(Video& v);
    std::string textoPlaca() const;
    motor::Fuentes fuentes() const;
    motor::Fuentes fuentesGrabacion() const;
    motor::Fuentes fuentesLimpio() const;
    void aplicarEntradas();
    void aplicarSalidas();
    void pararRepeticion();

    motor::Reloj& reloj_;
    Config cfg_;
    Pedido pedido_;
    bool hayPedido_ = false;
    Video video_, limpio_;
    bool limpioPedido_ = false; ///< el limpio se arma solo si se pide
    Audio audio_;
    std::map<int, std::unique_ptr<Audio>> pistas_;

    Armada<motor::Transmision> tx_;
    Armada<motor::Grabacion> grab_, copia_;
    std::map<std::string, Armada<motor::Transmision>> extras_;
    Armada<motor::Transmision> limpioTx_;
    Armada<motor::Grabacion> limpioGrab_;
    Repeticion rep_;
    std::int64_t reintentoSalidasNs_ = 0;
    std::string errorSalidas_;
};

} // namespace ba::emisor
