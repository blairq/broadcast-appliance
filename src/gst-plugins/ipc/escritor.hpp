// ============================================================================
// src/gst-plugins/ipc/escritor.hpp — escribir audio en un anillo del gateway
//
// Lo de un escritor de anillo, para los dos elementos que le escriben al gateway:
// baanillosink (un anillo: el programa) y baretornos (uno por invitado: su
// mix-minus). Lo que llega (F32, 48 kHz, estéreo intercalado) se junta en bloques
// de 480 cuadros (10 ms), cada uno sellado con la hora del gateway (QPC) que le
// corresponde a su hora en el pipeline. Si el lector no llega (anillo lleno), el
// bloque se descarta: nunca traba al que escribe.
//
// Cada `abrir` es una generación nueva del escritor: el gateway lo nota y reabre (no
// queda mudo si la app se reinicia). Sin candado: lo cuida el elemento que lo usa.
// ============================================================================
#pragma once

#include "ipc/HoraQpc.hpp"
#include "ipc/SharedAudioRing.hpp"

#include "hora.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define ba_escritor_getpid _getpid
#else
#include <unistd.h>
#define ba_escritor_getpid getpid
#endif

namespace ba::ipc_gst {

class EscritorAnillo {
   public:
    static constexpr std::size_t kCuadros = ba::ipc::audio_ring::kBlockFrames;
    static constexpr std::size_t kCanales = 2;

    /// Crea el anillo `canal` (p. ej. BA.Audio.Return.1). false si no se pudo.
    bool abrir(const std::string& canal) {
        cerrar();
        const std::wstring w(canal.begin(), canal.end());
        const auto gen = static_cast<std::uint32_t>(std::time(nullptr));
        const ba::ipc::Status st = ba::ipc::SharedAudioRing::init(w.c_str(), kCanales, ba::ipc::audio_ring::kDefaultCapacityBlocks,
                                                                   static_cast<std::uint32_t>(ba_escritor_getpid()), gen, anillo_);
        abierto_ = st.ok;
        bloque_.assign(kCuadros * kCanales, 0.0f);
        llenos_ = 0;
        horaBloque_ = GST_CLOCK_TIME_NONE;
        escritos_ = descartados_ = 0;
        return abierto_;
    }
    /// Lo suelta (el gateway ve que el escritor se fue).
    void cerrar() {
        anillo_ = ba::ipc::SharedAudioRing();
        abierto_ = false;
    }
    bool abierto() const { return abierto_; }
    std::uint64_t escritos() const { return escritos_; }
    std::uint64_t descartados() const { return descartados_; }

    /// Escribe `cuadros` cuadros de `d` (nullptr o `silencio`: ceros) cuya primera muestra
    /// tiene la hora `rt` (de ejecución) en el pipeline de `e`.
    void escribir(GstElement* e, const float* d, std::size_t cuadros, GstClockTime rt, bool silencio) {
        for (std::size_t i = 0; abierto_ && i < cuadros;) {
            if (llenos_ == 0)
                horaBloque_ = GST_CLOCK_TIME_IS_VALID(rt) ? rt + gst_util_uint64_scale(i, GST_SECOND, 48000) : GST_CLOCK_TIME_NONE;
            const std::size_t n = std::min(kCuadros - llenos_, cuadros - i);
            if (silencio || d == nullptr)
                std::memset(bloque_.data() + llenos_ * kCanales, 0, n * kCanales * sizeof(float));
            else
                std::memcpy(bloque_.data() + llenos_ * kCanales, d + i * kCanales, n * kCanales * sizeof(float));
            llenos_ += n;
            i += n;
            if (llenos_ == kCuadros) {
                if (anillo_.push(bloque_.data(), qpcDe(e, horaBloque_)))
                    ++escritos_;
                else
                    ++descartados_;
                llenos_ = 0;
            }
        }
    }

    /// La hora del gateway (QPC) de algo que tiene `rt` en el pipeline de `e`.
    static std::uint64_t qpcDe(GstElement* e, GstClockTime rt) {
        GstClock* reloj = gst_element_get_clock(e);
        const std::uint64_t q = ahoraQpc();
        if (reloj == nullptr || !GST_CLOCK_TIME_IS_VALID(rt)) {
            if (reloj != nullptr) gst_object_unref(reloj);
            return q;
        }
        const GstClockTime ahora = gst_clock_get_time(reloj);
        gst_object_unref(reloj);
        return ba::ipc::qpcDeOtroReloj(gst_element_get_base_time(e) + rt, ahora, q);
    }

   private:
    ba::ipc::SharedAudioRing anillo_;
    bool abierto_ = false;
    std::vector<float> bloque_; ///< lo acumulado hasta completar 480 cuadros
    std::size_t llenos_ = 0;    ///< cuadros en `bloque_`
    GstClockTime horaBloque_ = GST_CLOCK_TIME_NONE; ///< la hora (ejecución) del primer cuadro del bloque
    std::uint64_t escritos_ = 0, descartados_ = 0;
};

} // namespace ba::ipc_gst
