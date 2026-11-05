// ============================================================================
// src/gateway/webrtc/AudioCompartidoRtc.hpp — el audio de retorno compartido por dentro (libwebrtc)
//
// Lo usa Webrtc.cpp: la fábrica de codificadores de audio de la PeerConnectionFactory y los
// grupos (AudioCompartido). Ver AudioCompartido.hpp para el porqué.
//
//   - Grupo: el flujo Opus de los que oyen lo mismo. Codifica cada bloque una vez por cada
//     VARIANTE de Opus que pidieron sus conexiones (la misma configuración negociada: mono o
//     estéreo, FEC, DTX, bitrate, cuadros por paquete) y guarda los últimos paquetes.
//   - El codificador de cada conexión (de paso, en AudioCompartidoRtc.cpp) es el Opus de
//     libwebrtc envuelto: si el bloque que le llega trae la marca de un grupo y el paquete
//     del grupo empieza justo ahí, manda los bytes del grupo con su sello y su payload type;
//     si no (o si el grupo deja de servirle en medio de un paquete), codifica con el suyo,
//     rearmando el paquete con los bloques que ya había recibido. Así salir del grupo no
//     pierde ni repite audio.
// ============================================================================
#pragma once


#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/scoped_refptr.h"
#include "rtc_base/buffer.h"

namespace ba::gateway::audioret {

/// El flujo Opus de un grupo (AudioCompartido::Impl lo tiene). Thread-safe: codifica el hilo
/// del retorno, leen los codificadores de paso (la cola de cada conexión, de cualquier capa
/// de hilos: F2 de PLAN-GATEWAY-FRAGMENTOS.md). Los que leen no se esperan entre sí (candado
/// compartido) ni esperan a que se codifique: Opus corre sin candado y lo nuevo se publica
/// de una vez al final.
class Grupo {
   public:
    Grupo(std::uint16_t id, rtc::scoped_refptr<webrtc::AudioEncoderFactory> base);
    std::uint16_t id() const { return id_; }

    /// Un bloque de 10 ms (float32 estéreo intercalado): lo codifica cada variante pedida.
    /// Devuelve su número (de 1 en adelante).
    std::uint64_t codificar(const float* pcm, std::size_t n, int rate);
    /// El último bloque codificado (0: ninguno).
    std::uint64_t ultimo() const;

    // --- Del codificador de paso de una conexión ------------------------------------
    /// La conexión con esta variante (`firma` = su formato) quiere los paquetes del grupo:
    /// la variante se codifica desde el próximo bloque (y deja de codificarse si nadie la
    /// pide en un rato).
    void pedir(const std::string& firma, const webrtc::SdpAudioFormat& formato, int pt);
    /// Si un paquete de la variante empieza en el bloque `k`.
    bool empieza(const std::string& firma, std::uint64_t k) const;
    /// Si el bloque `k` cae en medio de un paquete de la variante (empezó antes).
    bool enCurso(const std::string& firma, std::uint64_t k) const;
    /// El paquete de la variante que termina en el bloque `k` (agregado a `out`), con el
    /// bloque en que empieza. false si no hay.
    bool paquete(const std::string& firma, std::uint64_t k, std::uint64_t& inicio, rtc::Buffer& out,
                 webrtc::AudioEncoder::EncodedInfoLeaf& info) const;

    std::atomic<std::uint64_t> repartidos{0}; ///< paquetes que salieron por conexiones
    /// Cuántas variantes codifica ahora.
    std::size_t variantes() const;

   private:
    struct Paquete {
        std::uint64_t inicio = 0, fin = 0;
        std::vector<std::uint8_t> bytes;
        webrtc::AudioEncoder::EncodedInfoLeaf info;
    };
    /// Un atómico que se puede mover (para guardarlo en el map).
    struct Atomico {
        std::atomic<std::uint64_t> v{0};
        Atomico() = default;
        Atomico(Atomico&& o) noexcept : v(o.v.load(std::memory_order_relaxed)) {}
    };
    struct Variante {
        std::unique_ptr<webrtc::AudioEncoder> enc;
        Atomico pedida;              ///< el último bloque en que alguien la pidió (la escriben los lectores)
        std::uint64_t pendiente = 0; ///< el primer bloque del paquete que se está armando (0: ninguno), publicado
        std::uint64_t pendCod = 0;   ///< lo mismo, del lado de codificar (sin candado)
        std::uint32_t ts = 0;
        std::deque<Paquete> paquetes;
        std::vector<std::int16_t> pcm;
        rtc::Buffer buf;
    };
    const std::uint16_t id_;
    const rtc::scoped_refptr<webrtc::AudioEncoderFactory> base_;
    mutable std::shared_mutex m_; ///< variantes_ (el map), pendiente y paquetes
    std::mutex codM_;             ///< un codificar a la vez (enc, pcm, buf, ts, pendCod)
    std::atomic<std::uint64_t> seq_{0};
    std::map<std::string, Variante> variantes_;
};

/// Los grupos por su número (el de la marca), para que el codificador de paso encuentre el
/// suyo. Uno por hub.
class Registro {
   public:
    explicit Registro(rtc::scoped_refptr<webrtc::AudioEncoderFactory> base) : base_(std::move(base)) {}
    /// El grupo llamado `nombre` (el mismo para el mismo nombre).
    std::shared_ptr<Grupo> grupo(const std::string& nombre);
    std::shared_ptr<Grupo> buscar(std::uint16_t id) const;
    /// [{nombre, bloques, repartidos, variantes}] (WebrtcHub::audioCompartidoEstado).
    nlohmann::json estado() const;

   private:
    const rtc::scoped_refptr<webrtc::AudioEncoderFactory> base_;
    mutable std::mutex m_;
    std::map<std::string, std::shared_ptr<Grupo>> porNombre_;
    std::map<std::uint16_t, std::shared_ptr<Grupo>> porId_;
};

/// La fábrica de codificadores de audio de las PeerConnection: la de libwebrtc (`base`), con
/// cada Opus envuelto en uno de paso que usa los grupos de `registro`.
rtc::scoped_refptr<webrtc::AudioEncoderFactory> fabricaDePaso(rtc::scoped_refptr<webrtc::AudioEncoderFactory> base,
                                                              std::shared_ptr<Registro> registro);

/// La firma de una variante: el formato entero (nombre, frecuencia, canales, parámetros).
std::string firma(const webrtc::SdpAudioFormat& f);

} // namespace ba::gateway::audioret

