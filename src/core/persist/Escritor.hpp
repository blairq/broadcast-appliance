// ============================================================================
// src/core/persist/Escritor.hpp — el escritor único y durable de archivos de estado
//
// docs/arquitectura/04-plan-unificado.md, F1-01. TODO lo que se guarda en Config/ y
// Runtime/ pasa por acá (directo, o por la cola de ColaEscritura.hpp, que es lo que
// usa el hilo de la UI). Un corte de luz en cualquier momento deja el archivo
// anterior entero o el nuevo entero, nunca uno vacío ni a medias:
//
//   temporal en la MISMA carpeta (mismo sistema de archivos: el rename es atómico)
//   → escribir todo → bajarlo al disco → rename encima del destino → bajar la carpeta
//   (sin esto último, tras el corte el rename puede no estar y vuelve el viejo).
//
// «Bajarlo al disco» según la plataforma:
//   - Linux:   fsync del archivo y de la carpeta.
//   - macOS:   fsync solo llega a la controladora del disco, que puede tenerlo en su
//              caché; con Durabilidad::Critica se usa F_FULLFSYNC (vacía también esa
//              caché: cuesta decenas de ms, por eso va en el hilo escritor y solo
//              para el estado de aire).
//   - Windows: FlushFileBuffers + MoveFileExW(MOVEFILE_WRITE_THROUGH), que no
//              vuelve hasta que el rename está en el disco.
//
// Sin Qt (ba_core). Las funciones no lanzan: devuelven false y, si se pide, por qué.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ba::persist {

/// Cuánto se asegura que lo escrito sobreviva a un corte de luz.
enum class Durabilidad {
    Normal,  ///< ajustes: fsync del archivo y de la carpeta
    Critica, ///< el estado de aire: además vacía la caché del disco (F_FULLFSYNC en macOS)
};

/// Quién puede leer el archivo.
enum class Permisos {
    Comunes,  ///< los de siempre (0644 en POSIX)
    Privados, ///< solo el dueño (0600 en POSIX; en Windows rige la ACL del perfil)
};

/// Escribe `bytes` en `ruta` de forma atómica y durable (ver arriba). Crea la carpeta
/// si falta. Un solo escritor por ruta: el temporal es `<ruta>.tmp`.
/// :returns: false si no quedó escrito (el archivo anterior, si había, sigue entero).
bool escribir(const std::filesystem::path& ruta, std::string_view bytes, Durabilidad d = Durabilidad::Normal,
              Permisos p = Permisos::Comunes, std::string* error = nullptr);

/// Borra `ruta` (si está) y baja la carpeta al disco: tras un corte no reaparece.
bool borrar(const std::filesystem::path& ruta);

/// Lee el archivo entero. false si no está o no se puede leer.
bool leerArchivo(const std::filesystem::path& ruta, std::string& out);

/// El nombre del sistema de archivos donde vive `ruta` («apfs», «ext4», «NTFS»,
/// «exfat», «msdos»…), o "" si no se sabe.
std::string sistemaDeArchivos(const std::filesystem::path& ruta);

/// true si `ruta` está en un sistema de archivos sin diario (FAT, FAT32, exFAT): ahí
/// un corte puede romper la carpeta entera aunque cada archivo se escriba bien.
bool sinDiario(const std::filesystem::path& ruta);

} // namespace ba::persist
