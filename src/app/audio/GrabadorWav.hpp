// src/app/audio/GrabadorWav.hpp — grabar a un .wav lo que le llega a una tarjeta
//
// La tarjeta «Salida de audio» de un rack puede grabar todo lo que le llega a
// un archivo (Recordings/Audio/<fuente>-<fecha>.wav). El hilo de audio solo
// copia a una cola (`agregar`); un hilo propio escribe al disco. WAV de 32
// bits flotante, estéreo: sin pérdida y sin recorte (lo que se oye, tal
// cual). Un WAV no puede pasar de 4 GB (unas 2 h 50 min a 48 kHz): antes de
// eso cierra y sigue en «-parte2.wav», y así.
//
// Sin Qt (se prueba solo).

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ba::app::audio {

class GrabadorWav {
   public:
    /// `base`: la ruta sin extensión (se le agrega «.wav», «-parte2.wav»...).
    GrabadorWav(std::filesystem::path base, std::uint32_t frecuencia);
    ~GrabadorWav();
    GrabadorWav(const GrabadorWav&) = delete;
    GrabadorWav& operator=(const GrabadorWav&) = delete;

    /// Abre el primer archivo y arranca el hilo. "" o el error.
    std::string empezar();
    /// Cierra (completa los encabezados). Se puede llamar más de una vez.
    void terminar();

    /// HILO DE AUDIO: copia y vuelve (no toca el disco).
    void agregar(const float* l, const float* r, std::uint32_t n);

    bool grabando() const { return grabando_.load(); }
    /// El archivo que se está escribiendo ahora.
    std::filesystem::path archivo() const;
    double segundos() const { return static_cast<double>(frames_.load()) / frecuencia_; }
    std::uint64_t bytes() const { return frames_.load() * 8; }

    /// Tope por archivo (bytes de datos); los tests lo achican.
    void setTope(std::uint64_t bytes) { tope_ = bytes; }

   private:
    void bucle();
    bool abrir(int parte);
    void cerrarArchivo();

    std::filesystem::path base_;
    std::uint32_t frecuencia_;
    std::uint64_t tope_ = 3'900'000'000ull; // < 4 GiB con margen

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::vector<float> cola_; ///< intercalado L R
    bool salir_ = false;
    std::thread hilo_;

    std::ofstream f_;
    std::filesystem::path actual_;
    std::uint64_t datosArchivo_ = 0; ///< bytes de datos en el archivo actual
    int parte_ = 1;
    std::atomic<bool> grabando_{false};
    std::atomic<std::uint64_t> frames_{0};
};

} // namespace ba::app::audio
