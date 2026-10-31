// ============================================================================
// src/core/persist/ColaEscritura.hpp — el hilo escritor de la persistencia
//
// docs/arquitectura/04-plan-unificado.md, F1-02. El hilo de la UI (o el que cambie el
// estado) solo ENCOLA; el disco lo toca un hilo aparte, con el escritor durable
// (Escritor.hpp). Así un fsync (o un F_FULLFSYNC de decenas de ms, o un disco USB que
// tarda) nunca traba la interfaz ni los mandos.
//
//   - Fusión por archivo: si se encola dos veces el mismo antes de que se escriba,
//     queda solo lo último (el reloj de la línea escribe cada segundo; un fader,
//     muchas veces por segundo: al disco va lo último).
//   - Prioridad: lo de aire (Prioridad::Aire) pasa adelante de los ajustes.
//   - Lo encolado se ve enseguida: `leer` devuelve lo último encolado aunque todavía
//     no esté en el disco (quien guarda y relee ve lo suyo).
//   - Al cerrar, `vaciar` espera a que todo esté escrito (AppRuntime::stop, y antes de
//     pedirle a otro proceso que relea un archivo).
//   - Si el disco falla, el pedido queda y se reintenta (cada 500 ms) hasta que ande
//     o llegue otro más nuevo; el error va al registro una vez por archivo.
//
// Sin Qt (ba_core). Seguro entre hilos.
// ============================================================================
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "persist/Escritor.hpp"
#include "persist/Generaciones.hpp"

namespace ba::persist {

enum class Prioridad {
    Normal, ///< ajustes
    Aire,   ///< el estado de aire: primero
};

/// Cómo se escribe lo encolado.
struct Pedido {
    Durabilidad durabilidad = Durabilidad::Normal;
    Prioridad prioridad = Prioridad::Normal;
    Permisos permisos = Permisos::Comunes;
    /// Corre en el hilo escritor justo antes de escribir (p. ej. guardar la versión
    /// anterior del archivo). Con la fusión, rige el del último pedido.
    std::function<void()> antes;
};

class ColaEscritura {
   public:
    ColaEscritura();
    ~ColaEscritura(); ///< vacía lo pendiente (con tope) y termina el hilo
    ColaEscritura(const ColaEscritura&) = delete;
    ColaEscritura& operator=(const ColaEscritura&) = delete;

    /// Encola `bytes` para `ruta` (reemplaza lo que hubiera encolado para esa ruta).
    void escribir(const std::filesystem::path& ruta, std::string bytes, Pedido p = {});
    /// Encola `cuerpo` para un archivo en dos generaciones (Generaciones.hpp).
    void escribirGeneracion(const std::filesystem::path& archivo, std::string cuerpo,
                            Pedido p = {Durabilidad::Critica, Prioridad::Aire, Permisos::Comunes, {}});
    /// Encola el borrado de `ruta` (y de sus generaciones, si las tiene).
    void borrar(const std::filesystem::path& ruta, Prioridad prioridad = Prioridad::Normal);

    /// Lo último de `ruta`: lo encolado si hay, si no lo del disco. false si no hay
    /// archivo (o su borrado está encolado).
    bool leer(const std::filesystem::path& ruta, std::string& out) const;
    /// Lo mismo para un archivo en dos generaciones.
    Lectura leerGeneraciones(const std::filesystem::path& archivo) const;

    /// Espera a que no quede nada por escribir. false si venció el tope (el disco no
    /// responde) o si se llama desde el hilo escritor.
    bool vaciar(std::chrono::milliseconds tope = std::chrono::seconds(5));

    struct Cuentas {
        std::uint64_t pedidos = 0;    ///< encolados
        std::uint64_t escrituras = 0; ///< los que llegaron al disco
        std::uint64_t fusionados = 0; ///< reemplazados por uno más nuevo antes de escribirse
        std::uint64_t errores = 0;    ///< intentos fallidos (se reintentan)
    };
    Cuentas cuentas() const;
    bool esHiloEscritor() const { return std::this_thread::get_id() == hilo_.get_id(); }

   private:
    enum class Tipo { Archivo, Generacion, Borrar };
    struct Entrada {
        Tipo tipo = Tipo::Archivo;
        std::filesystem::path ruta;
        std::string bytes;
        Pedido pedido;
        std::uint64_t orden = 0;   ///< cuándo se encoló por primera vez (FIFO entre iguales)
        std::uint64_t version = 0; ///< sube con cada pedido nuevo para la ruta
        std::uint64_t tomada = 0;  ///< la versión que el hilo está escribiendo
        bool enCurso = false;      ///< el hilo la está escribiendo
        bool avisado = false;      ///< su error ya fue al registro
        bool igualar = false;      ///< la copia a la otra generación (ver kIgualar)
        std::chrono::steady_clock::time_point reintento{};
    };
    void encolar(Tipo t, const std::filesystem::path& ruta, std::string bytes, Pedido p);
    void correr();
    bool hacer(const Entrada& e, std::string& error);

    mutable std::mutex mu_;
    std::condition_variable hay_;   ///< hay algo para el hilo (o hay que parar)
    std::condition_variable listo_; ///< el hilo terminó algo (para vaciar)
    std::map<std::string, Entrada> pendientes_;
    std::uint64_t orden_ = 0;
    std::uint64_t version_ = 0;
    bool parar_ = false;
    Cuentas cuentas_;
    std::map<std::string, DosGeneraciones> generaciones_; ///< del hilo escritor (secuencia y turno)
    std::thread hilo_;
};

/// La cola del proceso.
ColaEscritura& cola();

} // namespace ba::persist
