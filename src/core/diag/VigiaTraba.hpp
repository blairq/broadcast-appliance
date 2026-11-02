// ============================================================================
// src/core/diag/VigiaTraba.hpp — DÓNDE estaba la interfaz cuando se trabó
//
// EL PROBLEMA, visto en la PC de pruebas con 60 invitados entrando: la interfaz
// se quedó quieta 5, 12, 18 s (el motor siguió: el aire no se cortó) y lo único
// que quedó fue «la interfaz estuvo trabada 18531 ms» y «corría: ?». Sin volcado:
// el launcher solo vuelca al dar la app por colgada (mucho después).
//
// Esto es un hilo propio que mira el latido de la interfaz (VigiaBucle::edadMs:
// cuánto hace del último turno de su temporizador de 50 ms). Pasado el umbral
// (`diag.trabaInterfazMs`, 2000 ms; 0 = apagado) y como mucho una vez cada
// `diag.trabaInterfazCadaS` (60 s), MIENTRAS SIGUE TRABADA saca las pilas de
// todos los hilos con su nombre (diag::capturarPilas: en Windows lo escribe el
// launcher desde afuera, sin matar ni relanzar nada) a
// Logs/dumps/A-<fecha>-traba-<ms>.{pila|dmp} con su .json, y lo dice en el
// registro con la ruta y, donde hay nombres (macOS, Linux), qué hacía el hilo
// de la interfaz y el de render de Qt en una línea (diag::resumirPila).
// Al volver, otra línea con lo que duró (y el .json lo anota: `duracion_ms`).
//
// tools/caidas/analizar.py lee esos archivos y muestra primero la pila de la
// interfaz y la del hilo de render.
//
// Sin Qt (se testea con un hilo que se duerme): la app le da el latido, el id
// del hilo de la interfaz y qué corría (ui/VigiaInterfaz).
// ============================================================================
#pragma once

#include "diag/Caidas.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ba::diag {

class VigiaTraba {
   public:
    struct Opciones {
        std::filesystem::path dir;  ///< Logs/dumps
        std::string proceso = "A";  ///< el prefijo de los archivos (como los del launcher)
        /// Cuánto hace (ms) del último latido de la interfaz; -1 = todavía no latió.
        std::function<std::int64_t()> edadMs;
        /// El umbral (ms; 0 = apagado) y cada cuánto como mucho (s). Se leen en cada vuelta.
        std::function<int()> umbralMs;
        std::function<int()> cadaS;
        /// Lo que corría en el bucle (VigiaBucle::enCurso), para el .json y la línea.
        std::function<std::string()> corria;
        /// La captura (de fábrica, diag::capturarPilas). Los tests la cambian.
        std::function<CapturaPilas(const std::filesystem::path& dir, const std::string& base, const std::string& motivo,
                                   std::uint64_t hilo, const std::string& extraJson)>
            capturar;
        /// Lo que se dice (de fábrica, al registro como aviso de «interfaz»).
        std::function<void(const std::string&)> avisar;
        int sondeoMs = 100;
    };

    VigiaTraba() = default;
    ~VigiaTraba() { detener(); }
    VigiaTraba(const VigiaTraba&) = delete;
    VigiaTraba& operator=(const VigiaTraba&) = delete;

    /// Arranca el hilo. `hiloInterfaz`: idDelHilo() del de la interfaz.
    void iniciar(Opciones o, std::uint64_t hiloInterfaz);
    void detener();

    /// {capturas, ultima: {archivo, ms, interfaz, render, error}}.
    nlohmann::json estado() const;

    /// De las pilas (el texto de un .pila), el id del primer hilo cuyo nombre
    /// contiene `nombre` (0 si no hay). Para el de render de Qt: «QSGRenderThread».
    static std::uint64_t hiloPorNombre(const std::string& texto, const std::string& nombre);

   private:
    void bucle();
    void capturar(std::int64_t edad, int umbral);
    void terminoLaTraba(std::int64_t duracionMs);

    Opciones o_;
    std::uint64_t hiloInterfaz_ = 0;
    std::thread hilo_;
    std::mutex esperaMu_;
    std::condition_variable espera_;
    bool corriendo_ = false; ///< con esperaMu_

    mutable std::mutex mu_; ///< lo de abajo
    std::uint64_t capturas_ = 0;
    nlohmann::json ultima_;
    std::filesystem::path ultimoJson_;
};

} // namespace ba::diag
