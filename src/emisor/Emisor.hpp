// ============================================================================
// src/emisor/Emisor.hpp — la salida de una salida, en un proceso aparte (F3-01)
//
// docs/arquitectura/04-plan-unificado.md, F3-01 (T1, T2). Lo que la app publica de
// una salida (el video en su canal y el audio en el anillo del emisor) entra acá y
// sale codificado, grabado y transmitido con las MISMAS clases de la app
// (motor::Grabacion, motor::Transmision: el codificador compartido, la grabación en
// tramos, la lista negra, el ingest de respaldo). Si la app se cae, este proceso
// sigue: la conexión RTMP/SRT no se corta y el archivo no se parte; mientras la app
// no publica, sale la PLACA («Volvemos enseguida») con silencio.
//
//   video:  bavideoanillo canal=<BA.Program|BA.Salida.<id>> ! a NV12 del tamaño y los
//           cuadros del pedido ─► input-selector ─► intersink video:emisor:<id>
//           placa (un cuadro rotulado congelado, en vivo)  ─┘
//   audio:  baanillo canal=BA.Audio.Emisor.<id> ─► audiomixer (en vivo) ─► intersink bus:emisor:<id>
//           silencio (en vivo, siempre)            ─┘
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
#include <memory>
#include <string>

namespace ba::emisor {

class Emisor {
   public:
    struct Config {
        std::string id = "principal";
        std::string canalVideo; ///< "" = el de la salida (CanalesSalida.hpp)
        std::string canalAudio; ///< "" = BA.Audio.Emisor.<id>
    };
    Emisor(motor::Reloj& reloj, Config cfg);
    ~Emisor();
    Emisor(const Emisor&) = delete;
    Emisor& operator=(const Emisor&) = delete;

    /// Lo pedido: arma la entrada (con el tamaño del pedido) y prende, cambia o para
    /// la transmisión y la grabación según lo que haya cambiado.
    void aplicar(const Pedido& p);
    /// Una vuelta (~100 ms): la entrada (errores, la placa) y las salidas (se rearman solas).
    void tic();
    /// Cierre ordenado: para la transmisión y cierra los archivos (EOS).
    void parar();
    /// {id, seq, placa, placas, video_hace_ms, audio_hace_ms, edad_entrada, error,
    ///  transmision: Transmision::estado() | null, grabacion, copia: Grabacion::estado() | null}
    nlohmann::json estado() const;
    /// En la placa ahora (la app no publica).
    bool enPlaca() const { return placaActiva_.load(); }

   private:
    struct Rama {
        GstElement* pipeline = nullptr;
        std::int64_t reintentoNs = 0;
        std::string error;
    };
    void armarVideo();
    void armarAudio();
    void desarmar(Rama& r);
    void atender(Rama& r, bool video);
    motor::Fuentes fuentes() const;
    void aplicarSalidas();

    motor::Reloj& reloj_;
    Config cfg_;
    Pedido pedido_;
    bool hayPedido_ = false;
    Rama video_, audio_;
    int anchoArmado_ = 0, altoArmado_ = 0, fpsNArmado_ = 0, fpsDArmado_ = 0;
    std::string placaArmada_;
    std::atomic<bool> placaActiva_{true};
    std::atomic<std::int64_t> ultimoVideoUs_{0}, ultimoAudioUs_{0};
    std::uint64_t placas_ = 0; ///< veces que pasó a la placa
    motor::MedidorEdad edad_;  ///< la edad de lo que entra a codificar (el reloj del estudio)

    std::unique_ptr<motor::Transmision> tx_;
    std::unique_ptr<motor::Grabacion> grab_, copia_;
    nlohmann::json txCfg_, grabCfg_, copiaCfg_; ///< lo pedido con que se armaron (para comparar)
    std::int64_t reintentoSalidasNs_ = 0;
    std::string errorSalidas_;
};

} // namespace ba::emisor
