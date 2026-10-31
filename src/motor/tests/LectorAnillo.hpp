// LectorAnillo.hpp — leer un anillo de audio del motor como lo lee el gateway, para las pruebas
//
// Sin parlantes: lo que el motor escribe con baanillosink se lee directo de la memoria
// compartida (SharedAudioRing). De cada bloque: su sello (la hora de captura de su primera
// muestra, en QPC: lo pone baanillosink a partir del PTS) y cuándo se leyó. Con eso:
//   - el LAZO de un pulso: cuándo se leyó el bloque menos la hora de captura de la muestra
//     donde empieza el pulso (04-plan-unificado.md, F2-01: test_lazo_latencia);
//   - cuánto hay de una frecuencia (Goertzel): si una voz está o no está en un mix-minus.
#pragma once

#include "ipc/HealthBlock.hpp"
#include "ipc/SharedAudioRing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace prueba {

struct Lectura {
    std::vector<float> muestras;          ///< estéreo intercalado, todo lo leído
    std::vector<double> lazosMs;          ///< por pulso: leído − capturado
    bool abierto = false;
};

/// Lee `canal` durante `ms`. Un pulso es una muestra que pasa `umbral` después de al menos
/// `silencioMs` de silencio (audiotestsrc wave=ticks).
inline Lectura leer(const std::string& canal, int ms, float umbral = 0.05f, int silencioMs = 50) {
    Lectura out;
    ba::ipc::SharedAudioRing r;
    const std::wstring w(canal.begin(), canal.end());
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!(out.abierto = ba::ipc::SharedAudioRing::openReader(w.c_str(), r).ok)) {
        if (std::chrono::steady_clock::now() > fin) return out;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Lo viejo que haya, afuera: se mide desde ahora.
    std::vector<float> buf(r.blockFloats());
    ba::ipc::PoppedBlock b;
    while (r.pendientes() > 0) r.pop(buf.data(), b);
    std::uint64_t callado = 0; // cuadros seguidos en silencio
    const std::uint64_t hace = static_cast<std::uint64_t>(silencioMs) * 48;
    while (std::chrono::steady_clock::now() < fin) {
        while (r.pendientes() > 0 && r.pop(buf.data(), b)) {
            const std::uint64_t leido = ba::ipc::HealthBlock::nowQpc();
            const std::size_t cuadros = buf.size() / 2;
            for (std::size_t i = 0; i < cuadros; ++i) {
                const float x = std::max(std::fabs(buf[2 * i]), std::fabs(buf[2 * i + 1]));
                if (x >= umbral) {
                    if (callado >= hace && !b.isSilence) {
                        const std::uint64_t capturado = b.qpc + i * 1'000'000'000ULL / 48000;
                        out.lazosMs.push_back(static_cast<double>(static_cast<std::int64_t>(leido - capturado)) / 1e6);
                    }
                    callado = 0;
                } else {
                    ++callado;
                }
            }
            out.muestras.insert(out.muestras.end(), buf.begin(), buf.end());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return out;
}

/// La amplitud de `hz` en el canal izquierdo (0…1; un seno de amplitud a da ~a).
inline double goertzel(const std::vector<float>& estereo, double hz) {
    const std::size_t n = estereo.size() / 2;
    if (n == 0) return 0;
    const double k = 2.0 * std::cos(2.0 * 3.14159265358979 * hz / 48000.0);
    double s1 = 0, s2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s0 = estereo[2 * i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double pot = s1 * s1 + s2 * s2 - k * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, pot)) / static_cast<double>(n);
}

/// La mediana y el p95 (ms) de unos lazos; -1 si no hay.
inline void percentiles(std::vector<double> v, double& p50, double& p95) {
    if (v.empty()) {
        p50 = p95 = -1;
        return;
    }
    std::sort(v.begin(), v.end());
    p50 = v[v.size() / 2];
    p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
}

} // namespace prueba
