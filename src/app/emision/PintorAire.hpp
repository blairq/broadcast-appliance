// ============================================================================
// src/app/emision/PintorAire.hpp — un hilo que pinta para el aire
//
// Lo que pinta la app y sale al aire (los subtítulos, el chat en pantalla) se
// pintaba en el hilo de la interfaz, con un QTimer: con la interfaz trabada (un
// diálogo, un archivo grande, un bucle) esas capas se congelaban AL AIRE. Un
// subtítulo que tenía que borrarse quedaba puesto; el chat no se desvanecía.
//
// Esto es un hilo con nombre que llama a `pintar` cada `periodo` (y antes, si
// alguien lo despierta porque llegó algo nuevo). Pintar con QPainter sobre una
// QImage es seguro fuera del hilo principal (Qt lo documenta: QImage y QPainter
// sobre QImage son reentrantes, las letras también desde 4.4). Lo que pinta
// sale con fuentes::empujar, que se puede llamar desde cualquier hilo.
//
// EL ESTADO ES DE QUIEN PINTA. Lo que viene de la interfaz (los mensajes del
// chat, una frase nueva) llega como COPIA bajo el mutex de quien lo usa: el
// hilo nunca lee un modelo de la interfaz. Así, trabada la interfaz, lo que
// depende del tiempo (borrar, desvanecer, pasar al bloque siguiente) sigue.
// ============================================================================
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ba::app::fuentes {

class PintorAire {
   public:
    /// Arranca el hilo `nombre`; `pintar` corre en él cada `periodo`.
    PintorAire(std::string nombre, std::chrono::milliseconds periodo, std::function<void()> pintar);
    /// Para el hilo y espera a que termine la vuelta que esté pintando.
    ~PintorAire();
    PintorAire(const PintorAire&) = delete;
    PintorAire& operator=(const PintorAire&) = delete;

    /// Que pinte ya, sin esperar al período (llegó algo nuevo). Desde cualquier hilo.
    void despertar();

   private:
    void bucle();

    std::string nombre_;
    std::chrono::milliseconds periodo_;
    std::function<void()> pintar_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool fin_ = false;
    bool despierto_ = false;
    std::thread hilo_;
};

} // namespace ba::app::fuentes
