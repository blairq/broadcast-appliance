// ============================================================================
// src/core/persist/Generaciones.hpp — un archivo de estado en dos generaciones
//
// docs/arquitectura/04-plan-unificado.md, F1-03. Para lo que no se puede perder en un
// corte (Runtime/al-aire.json, Runtime/linea-aire.json): en vez de un archivo, dos
// (`al-aire.A.json` y `al-aire.B.json`), escritos por turno con el formato de
// SnapshotStore:
//
//   Línea 1:      #BA-SNAP v1 seq=<secuencia> sha256=<hex64 del cuerpo>
//   Línea 2..N:   el cuerpo (el JSON de siempre)
//
// Al leer se toma la de mayor secuencia cuyo SHA-256 coincide. Si un corte (o el
// disco) rompe la última, vuelve la anterior; si se rompen las dos, se SABE (estado
// Danada) en vez de arrancar callado con «nada al aire».
//
// Retrocompatible: si no hay ninguna generación se lee el archivo viejo de una sola
// (`al-aire.json`, sin cabecera: estado Vieja); la primera escritura nueva lo borra
// para que no vuelva a aparecer desactualizado.
//
// Sin Qt (ba_core). Un solo escritor por archivo (el hilo de ColaEscritura).
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "persist/Escritor.hpp"

namespace ba::persist {

/// SHA-256 (FIPS 180-4) en hexadecimal minúscula (64 caracteres).
std::string sha256Hex(std::string_view datos);

/// El contenido de una generación: la cabecera con `secuencia` y el hash, y el cuerpo.
std::string conCabecera(std::uint64_t secuencia, std::string_view cuerpo);
/// Lo inverso: true si `contenido` tiene cabecera válida y el hash coincide.
bool sinCabecera(const std::string& contenido, std::uint64_t& secuencia, std::string& cuerpo);

/// Lo que se encontró al leer.
struct Lectura {
    enum class Estado {
        Nada,   ///< no hay ningún archivo (primer arranque)
        Valida, ///< una generación entera: la más nueva que sirve
        Vieja,  ///< el archivo de antes de las generaciones (sin verificar: lo valida quien lo usa)
        Danada, ///< hay generaciones pero ninguna sirve
    };
    Estado estado = Estado::Nada;
    std::string cuerpo;
    std::uint64_t secuencia = 0;
    int descartadas = 0; ///< generaciones que estaban y no servían (rotas o truncadas)
};

class DosGeneraciones {
   public:
    /// `archivo` es el nombre de siempre (…/al-aire.json): las generaciones son
    /// …/al-aire.A.json y …/al-aire.B.json.
    explicit DosGeneraciones(std::filesystem::path archivo);

    Lectura leer() const;
    /// Escribe `cuerpo` en la generación que no tiene la última (secuencia + 1).
    bool escribir(std::string_view cuerpo, Durabilidad d = Durabilidad::Critica, std::string* error = nullptr);
    /// Borra las dos generaciones y el archivo viejo.
    bool borrar();

    const std::filesystem::path& archivo() const { return archivo_; }
    std::filesystem::path rutaA() const;
    std::filesystem::path rutaB() const;

   private:
    std::filesystem::path archivo_;
    bool conocido_ = false; ///< ya se miró el disco (secuencia y turno)
    std::uint64_t secuencia_ = 0;
    bool siguienteA_ = true;
};

} // namespace ba::persist
