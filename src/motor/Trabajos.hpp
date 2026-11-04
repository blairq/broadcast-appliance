// ============================================================================
// src/motor/Trabajos.hpp — los trabajos en orden de cada entrada, en pocos hilos compartidos
//
// Cada Entrada (y cada Escucha) encarga lo que tarda (armar, desarmar, el reposo, la
// placa) a «su» trabajador, en orden y de a uno. Antes era un std::async por trabajo
// encadenado al anterior: en Windows (MSVC) std::async usa el pool del sistema, que
// guardaba los hilos que crecían al arrancar (con cupo 200, ~170 «entrada-trabajo» vivos
// sin hacer nada: docs/hardware/pc-ryzen9-5900x-rx5700xt.md, «Con todo integrado»), y
// cada trabajo en espera del anterior tenía un hilo bloqueado.
//
// Ahora: una FILA por dueño (en orden, de a uno, como antes) y los trabajos de todas las
// filas en unos pocos hilos compartidos («motor-trabajo»). Hay `base` hilos siempre
// (de fábrica 2); si llega trabajo y no hay ninguno libre (uno espera a una cámara de red o
// a GStreamer), se suma otro, hasta `tope` (de fábrica 16), y el que sobra se va después de
// unos segundos sin trabajo. Así arrancar 200 lugares no espera de a dos, y en reposo
// quedan 2 hilos, no uno por lugar.
//
// `esperar` (al destruir el dueño) espera lo encargado; llamado desde un trabajo de la
// MISMA fila, no espera (se trabaría a sí mismo).
// ============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace ba::motor {

class FilaDeTrabajo {
   public:
    FilaDeTrabajo();
    /// Espera lo pendiente (como el futuro de antes).
    ~FilaDeTrabajo();
    FilaDeTrabajo(const FilaDeTrabajo&) = delete;
    FilaDeTrabajo& operator=(const FilaDeTrabajo&) = delete;

    /// Al final de la fila: corre después de todo lo encargado antes, en un hilo compartido.
    void encargar(std::function<void()> f);
    /// Hasta que termine todo lo encargado hasta ahora (no desde un trabajo de esta fila).
    void esperar();
    /// Cuántos trabajos hay encargados o corriendo.
    std::size_t pendientes() const;

    struct Estado; // Trabajos.cpp

   private:
    std::shared_ptr<Estado> e_;
};

/// Los hilos compartidos: los vivos ahora, los que tienen trabajo y los máximos que hubo.
struct HilosDeTrabajo {
    int vivos = 0;
    int ocupados = 0;
    int maximo = 0;
    std::size_t filasEsperando = 0; ///< filas con trabajo que esperan un hilo libre
};
HilosDeTrabajo hilosDeTrabajo();
/// Los que quedan siempre (1..8) y cuántos puede haber como mucho (base..64). Lo de
/// fábrica: 2 y 16. Rige para los hilos que se crean después.
void ponerHilosDeTrabajo(int base, int tope);
/// Cuánto espera sin trabajo un hilo que sobra antes de irse (50 ms..60 s; de fábrica 5 s).
/// Rige desde la próxima espera de cada hilo. Los tests que miden los hilos que QUEDAN la
/// achican para no esperar 5 s cada vez.
void ponerEsperaDeHilosDeTrabajo(int ms);

} // namespace ba::motor
