// ============================================================================
// src/gateway/CargaVideo.hpp — cuánto se exige al decodificar video (para CpsSegunCarga)
//
// Tres medidas, cada segundo (Medidor::medir):
//   - el MOTOR DE VIDEO de la placa (solo Windows): los contadores «GPU Engine» del sistema
//     (los del Administrador de tareas), de los motores de códec (VideoDecode, VideoEncode,
//     «Video Codec» en AMD: decodifica y codifica en el mismo), SUMANDO todos los procesos
//     —la app codifica en el mismo motor— y el más cargado de todos. pdh.dll se carga al
//     medir, no se importa (como diag/Recursos.cpp);
//   - los PERDIDOS (portable): de los cuadros que llegan armados al TapTransformer (antes del
//     decodificador), los que no llegaron a un Decode(). La recepción de libwebrtc descarta lo
//     que el decodificador no alcanza a tomar: con la placa saturada, las celdas caen de 15 a
//     7,5 cps y esto lo ve;
//   - el TIEMPO DE DECODIFICAR por cuadro (portable): lo que tarda cada Decode() del
//     envoltorio de CodecsVideo.cpp (cada uno lleva una Actividad, que también dice si ese
//     decodificador trabaja o está en pausa).
// Los contadores son atómicos sueltos: los anotan los hilos de libwebrtc sin candado.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <nlohmann/json.hpp>

#include "CpsSegunCarga.hpp"

namespace ba::gateway::cargavideo {

/// Llegó un cuadro de video armado (antes del decodificador): TapTransformer.
void alRecibir();

/// Lo que hace UN decodificador (cada envoltorio de CodecsVideo.cpp tiene una): se anota
/// mientras vive, y cada Decode() marca cuánto tardó.
class Actividad {
   public:
    Actividad();
    ~Actividad();
    Actividad(const Actividad&) = delete;
    Actividad& operator=(const Actividad&) = delete;
    /// Un Decode() que tardó `ns`.
    void decodifico(std::int64_t ns);
    std::int64_t ultimoMs() const { return ultimoMs_.load(std::memory_order_relaxed); }

   private:
    std::atomic<std::int64_t> ultimoMs_{0};
};

/// Los decodificadores abiertos: cuántos decodificaron algo en los últimos `ventanaMs` y
/// cuántos están sin trabajar (en pausa: su capa no viaja; libwebrtc no los suelta).
struct Decodificadores {
    int trabajando = 0;
    int enPausa = 0;
};
Decodificadores decodificadores(std::int64_t ventanaMs = 2000);

class Medidor {
   public:
    Medidor();
    ~Medidor();
    Medidor(const Medidor&) = delete;
    Medidor& operator=(const Medidor&) = delete;

    /// Lo de desde la última llamada (la primera vez, desde que se creó).
    MedidaCarga medir();
    /// {motor: true si se lee el motor de la placa, recibidos_cps, decodificados_cps}
    /// de la última medición.
    nlohmann::json estado() const;

   private:
    struct Motor;
    std::unique_ptr<Motor> motor_;
    std::uint64_t recibidos_ = 0, decodificados_ = 0, ns_ = 0;
    std::int64_t ultimoMs_ = 0;
    double recibidosCps_ = 0, decodificadosCps_ = 0;
};

} // namespace ba::gateway::cargavideo
