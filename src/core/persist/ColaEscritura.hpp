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
//     pedirle a otro proceso que relea un archivo, o de mover, copiar o borrar la carpeta
//     que lo tiene: lo encolado volvería a crearla).
//   - Los diarios (`agregar`: líneas que solo se suman) no se fusionan: lo encolado se
//     junta y se agrega en orden, una vez.
//   - Si el disco falla, el pedido queda y se reintenta (cada 500 ms) hasta que ande
//     o llegue otro más nuevo; el error va al registro una vez por archivo.
//
// Sin Qt (ba_core). Seguro entre hilos.
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "persist/Lectura.hpp"
#include "persist/Pedido.hpp"

namespace ba::persist {

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
    /// Encola `bytes` para agregar al final de `ruta` (un diario; ver persist::agregar). Lo
    /// encolado para el mismo archivo se junta, en orden.
    void agregar(const std::filesystem::path& ruta, std::string bytes, Pedido p = {});

    /// Lo último de `ruta`: lo encolado si hay, si no lo del disco. false si no hay
    /// archivo (o su borrado está encolado).
    bool leer(const std::filesystem::path& ruta, std::string& out) const;
    /// Lo mismo para un archivo en dos generaciones.
    Lectura leerGeneraciones(const std::filesystem::path& archivo) const;
    /// ¿Hay `ruta`? Lo encolado cuenta (una escritura encolada sí; un borrado encolado no),
    /// sin leerlo.
    bool existe(const std::filesystem::path& ruta) const;

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
    bool esHiloEscritor() const;

   private:
    /// El estado (lo pendiente, el candado, el hilo): detalle de ColaEscritura.cpp.
    struct Impl;
    std::unique_ptr<Impl> d_;
};

/// La cola del proceso.
ColaEscritura& cola();

} // namespace ba::persist
