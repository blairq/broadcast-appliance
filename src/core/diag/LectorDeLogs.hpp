// ============================================================================
// src/core/diag/LectorDeLogs.hpp — leer lo que quedó escrito
//
// El visor no puede pedirle al Registro lo que pasó ANTES de que arrancara el
// proceso, y justamente lo que uno quiere mirar después de un incidente es la
// corrida anterior. Eso está en los archivos de Logs/, de los tres procesos, y
// esto es lo que los lee.
//
// TOLERA LO QUE NO ESTÁ ESTRUCTURADO, y no es un detalle: en Logs/app.log
// conviven las líneas del Registro con lo que escupen GStreamer, Qt y cualquier
// printf suelto. Una línea que no se puede partir NO se tira —muchas veces es
// justo la que explica el problema— sino que entra como `info` del componente
// que diga el archivo. El visor la muestra igual; lo único que pierde es el
// nivel.
//
// Lee DESDE EL FINAL: un log de varios megabytes no se carga entero para
// mostrar las últimas doscientas líneas.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "diag/Registro.hpp"

namespace ba::diag {

/// Qué se quiere leer. Todo vacío o en cero = lo último de todo.
struct Filtro {
    Nivel minimo = Nivel::Traza;
    /// Vacío = todos. Si no, solo estos componentes.
    std::vector<std::string> componentes;
    /// Vacío = todo. Si no, solo las líneas que lo contengan (sin distinguir
    /// mayúsculas ni acentos que no tenga el texto buscado).
    std::string busca;
    /// 0 = sin piso. Si no, solo lo registrado desde ese instante (época ms).
    std::int64_t desdeMs = 0;
    /// Cuántas devolver como mucho, de las más nuevas.
    std::size_t cuantas = 400;
};

/// Lee los archivos de log de `dir` y devuelve lo que pase el filtro, de lo más
/// viejo a lo más nuevo (que es como se lee un log).
///
/// Toma el archivo activo de cada proceso y sus rotados, hasta `porArchivoKiB`
/// desde el final de cada uno: sin ese tope, un filtro que no encuentra nada
/// terminaría leyendo todo el disco.
std::vector<Registrado> leerLogs(const std::filesystem::path& dir, const Filtro& f,
                                 std::size_t porArchivoKiB = 512);

/// Los componentes que aparecen en los logs, para poder ofrecerlos en el visor
/// sin que nadie los escriba a mano en ningún lado.
std::vector<std::string> componentesEnLogs(const std::filesystem::path& dir, std::size_t porArchivoKiB = 128);

/// Parte una línea. Devuelve false si no tiene la forma del Registro; en ese
/// caso `out` igual queda usable (nivel info, el texto entero).
///
/// :param componentePorDefecto: el que se le pone a una línea sin estructura
///        (normalmente, el nombre del archivo del que salió).
bool partirLinea(const std::string& linea, const std::string& componentePorDefecto, Registrado& out);

} // namespace ba::diag
