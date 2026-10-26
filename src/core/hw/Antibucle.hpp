// ============================================================================
// src/core/hw/Antibucle.hpp — cuándo volver a enumerar los dispositivos
//
// El sistema avisa «cambiaron los dispositivos» y la app vuelve a enumerar. El
// círculo de la notebook con ASIO4ALL/SAR: enumerar cargaba los drivers, cargar
// ASIO4ALL movía los dispositivos de Windows, Windows avisaba, y otra
// enumeración (una cada ~1,8 s durante 12 minutos). Esto decide, sin relojes ni
// timers propios (el que lo usa le pasa la hora), si un aviso merece otra
// enumeración:
//
//   - los avisos que llegan MIENTRAS se enumera, o hasta `graciaMs` después, no
//     cuentan: son el eco de la enumeración misma;
//   - un aviso se espera `esperaMs` (un USB llega en varios avisos seguidos) y al
//     vencer se mira de nuevo la FIRMA de lo que hay (la lista de ids que da el
//     sistema, barata): si es la misma que vio la última enumeración, fue un
//     ir y venir y no se enumera;
//   - nunca más de una enumeración cada `minimoEntreMs`, lleguen los avisos que
//     lleguen (se programa una para cuando se cumple).
//   - si la firma cambió MIENTRAS se enumeraba, al terminar queda programada otra
//     (lo enchufado en ese momento no se pierde).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace ba::hw {

class Antibucle {
   public:
    struct Tiempos {
        std::int64_t esperaMs = 800;
        std::int64_t graciaMs = 2000;
        std::int64_t minimoEntreMs = 10000;
    };
    explicit Antibucle(Tiempos t) : t_(t) {}
    Antibucle() : Antibucle(Tiempos{}) {}
    void setTiempos(Tiempos t) { t_ = t; }

    /// Llegó un aviso del sistema; `firma`: lo que hay ahora. true si quedó una
    /// enumeración programada (nueva o la que ya estaba).
    bool aviso(std::int64_t ahoraMs, const std::string& firma);
    /// ¿Toca enumerar ya? (la consume). `firma`: lo que hay ahora.
    bool vence(std::int64_t ahoraMs, const std::string& firma);
    /// Empieza una enumeración (programada o pedida a mano). `firma`: lo que hay.
    void empieza(std::int64_t ahoraMs, const std::string& firma);
    void termina(std::int64_t ahoraMs, const std::string& firma);
    /// ms hasta la programada (0 si ya venció; -1 si no hay ninguna).
    std::int64_t falta(std::int64_t ahoraMs) const;
    bool enumerando() const { return enumerando_; }
    int ignorados() const { return ignorados_; }

   private:
    void programar(std::int64_t ahoraMs);
    Tiempos t_;
    bool enumerando_ = false;
    bool pendiente_ = false;
    bool hubo_ = false; ///< hubo al menos una enumeración
    std::int64_t programada_ = 0, inicio_ = 0, fin_ = 0;
    std::string firmaInicio_, firmaVista_;
    int ignorados_ = 0;
};

} // namespace ba::hw
