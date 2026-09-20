// ChildOutput.hpp
//
// Proposito:
//   Archivo de Logs/ donde queda la salida (stdout + stderr) de un hijo del
//   launcher. A informa todo por printf/stderr (motor que no arranca, fuente
//   omitida, app.json invalido, avisos de Qt/QML, libobs) y en Windows es
//   subsistema GUI: sin este archivo esa salida se pierde y no hay con que dar
//   soporte.
//
//   El archivo lo escribe el propio hijo, en modo agregar
//   (ba::platform::ProcessOptions::outputFile). Aca solo se lo prepara ANTES
//   de lanzar: se crea Logs/, se rota si crecio demasiado y se agrega una
//   linea de encabezado que separa una corrida de la siguiente.
//
// Tamaño acotado:
//   Se rota solo antes de lanzar, nunca con el hijo vivo (en Windows no se
//   puede renombrar un archivo abierto): app.log -> app.log.1 -> app.log.2.
//   Un hijo ADOPTADO (readopt, spec 3.1.9) no pasa por aca: sigue escribiendo
//   en el archivo que ya tenia abierto y recien se rota en el proximo
//   lanzamiento. ba::diag::LogRotator no sirve para esto: escribe el las
//   lineas y tiene el nombre fijo (ba.log).
//
// Modelo de threading:
//   Se usa desde el thread del supervisor. No es thread-safe.
//
// Estrategia de errores:
//   No lanza. Un fallo de disco nunca impide lanzar al hijo: prepareChildOutput
//   devuelve false y el hijo arranca sin captura.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ba::launcher {

/// Tamaño a partir del cual se rota la salida de un hijo antes de lanzarlo.
inline constexpr std::uintmax_t kChildOutputMaxBytes = 5u * 1024u * 1024u;

/// Generaciones rotadas que se conservan (<archivo>.1 .. <archivo>.N).
inline constexpr int kChildOutputKeepFiles = 2;

/// Rota `file` si supera `maxBytes`: <archivo> -> <archivo>.1 -> <archivo>.2
/// ..., y la generacion `keepFiles` se pierde. Llamar SIN el hijo vivo.
///
/// Si el archivo no se puede renombrar (Windows: otro proceso lo tiene
/// abierto) no se toca ninguna generacion: se sigue agregando al mismo archivo
/// y el proximo lanzamiento reintenta.
///
/// :returns: true si se roto.
bool rotateChildOutput(const std::filesystem::path& file, std::uintmax_t maxBytes = kChildOutputMaxBytes,
                       int keepFiles = kChildOutputKeepFiles);

/// Los argumentos en una linea, con los valores sensibles tapados ("***"):
/// todo flag cuyo nombre sugiere un secreto o una URL (pin, token, key,
/// server...), en las dos formas que usan A y B: `--flag=valor` y
/// `--flag valor`.
std::string redactedArgs(const std::vector<std::string>& args);

/// Deja `file` listo para recibir la salida de un lanzamiento: crea su
/// carpeta, lo rota si hace falta y agrega el encabezado de la corrida
/// (fecha y hora local, nombre del hijo, ejecutable y argumentos sin
/// secretos).
///
/// :param name: nombre logico del hijo ("A", "B").
/// :returns: true si el archivo se puede escribir; false = lanzar sin captura.
bool prepareChildOutput(const std::filesystem::path& file, const std::string& name,
                        const std::filesystem::path& exe, const std::vector<std::string>& args,
                        std::uintmax_t maxBytes = kChildOutputMaxBytes,
                        int keepFiles = kChildOutputKeepFiles);

} // namespace ba::launcher
