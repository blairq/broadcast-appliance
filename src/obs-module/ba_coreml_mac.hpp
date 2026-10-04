// src/obs-module/ba_coreml_mac.hpp — tarjeta de superresolución CoreML, SIN copias a la CPU
//
// La tarjeta «Superresolución» del grafo de video con un plugin CoreML (una red
// .mlmodelc: Real-ESRGAN, LSDIR, la red propia…), en macOS, sin bajar los
// cuadros de la GPU:
//
//   textura de OBS --blit OpenGL--> IOSurface de entrada --CoreML (Neural
//   Engine/GPU)--> IOSurface de salida --> textura de OBS (gs_texture_create_from_iosurface)
//
// Ningún píxel pasa por la memoria de la CPU (el TarjetaGst, en cambio, baja la
// textura, la pasa por GStreamer y la sube). La red corre en otro hilo con dos
// juegos de IOSurfaces: el hilo de dibujo de OBS nunca espera (dibuja el último
// resultado listo); la demora (lo que tarda en volver un cuadro) se mide y el
// audio de la entrada se atrasa lo mismo.
//
// `escalaRed` 4: la red agranda x4 y recibe la imagen a la mitad (el blit
// achica): sale x2 de la original con la cuarta parte del trabajo.
#pragma once

#include <graphics/graphics.h>

#include <cstdint>
#include <memory>
#include <string>

namespace ba::coreml {

class TarjetaCoreMl {
   public:
    TarjetaCoreMl(std::string modelo, int escalaRed);
    ~TarjetaCoreMl();
    TarjetaCoreMl(const TarjetaCoreMl&) = delete;
    TarjetaCoreMl& operator=(const TarjetaCoreMl&) = delete;

    /// Entra la textura `entrada` (cx x cy); dibuja el resultado (2cx x 2cy) en el
    /// destino activo. false: todavía no hay resultado (el modelo carga) o falló:
    /// el grafo dibuja la imagen agrandada común.
    bool procesar(gs_texture_t* entrada, std::uint32_t cx, std::uint32_t cy);
    int factor() const { return 2; }
    const std::string& modelo() const { return modelo_; }
    int escalaRed() const { return escalaRed_; }
    /// La demora medida (ms): desde que entra un cuadro hasta que su resultado
    /// está listo para dibujarse (promediada).
    double demoraMs() const;
    /// Lo que tarda la red sola (ms, promedio).
    double msRed() const;
    std::string error() const;

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::string modelo_;
    int escalaRed_;
};

} // namespace ba::coreml
