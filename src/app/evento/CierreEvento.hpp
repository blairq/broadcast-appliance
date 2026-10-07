// ============================================================================
// src/app/evento/CierreEvento.hpp — lo que queda cuando un evento deja de estar en curso (H5)
//
// Hasta acá, «la sesión en curso» de la asamblea (orden del día, quórum,
// votaciones, sesión remota) vivía en Config/ y la pisaba la próxima. Los logs
// rotan y se pierden. Al cerrar un evento se le deja, en su carpeta:
//
//     asamblea/       copia (sin secretos) de lo de la asamblea como quedó:
//                     orden-del-dia, quorum, votaciones-oficiales, cuerpo,
//                     sesion-remota, conferencia, seguir-microfono
//     registro/       el pedazo de cada log que cae entre el inicio y el fin
//                     del evento (app, mcp, botonera, gateway, sesion-remota…)
//     sellos.json     sha256 de cada archivo de la carpeta: el histórico
//                     queda verificable (que nadie lo tocó después)
//
// Sellar lee las grabaciones enteras (gigas): va en un hilo aparte y se puede
// cancelar; un sello a medias no se escribe.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {

struct CierreEvento {
    using json = nlohmann::json;

    /// Los archivos de Config/ que son de la asamblea.
    static const std::vector<std::string>& archivosAsamblea();

    /// Copia a <evento>/asamblea/ los de la asamblea, sin secretos. Devuelve cuántos.
    static int copiarAsamblea(const std::filesystem::path& config, const std::filesystem::path& evento);

    /// La hora de una línea de log, en ms UTC (0 si la línea no empieza con una):
    ///   «2026-10-07T04:28:37.700Z …»  UTC        «[2026-…Z] …» también
    ///   «2026-10-05T04:22:11.294  …»  hora local «==== 2026-10-07T01:28:35 | …» también
    ///   «{"ms": 1791225273871, …}» / «{"t": …}»   (jsonl)
    static std::int64_t horaDeLinea(const std::string& linea);

    /// Copia a <destino>/<nombre> las líneas de cada log de `logs` (también
    /// los rotados: app.log.1, .2…, del más viejo al más nuevo) con hora entre
    /// desde y hasta. Una línea sin hora sigue a la anterior. Devuelve
    /// {archivo: líneas}.
    static json recortarRegistros(const std::filesystem::path& logs, std::int64_t desdeMs, std::int64_t hastaMs,
                                  const std::filesystem::path& destino);

    /// sha256 de cada archivo de la carpeta (menos sellos.json y Respaldos/).
    /// Con `cancelar` en true, se corta y devuelve null.
    static json sellar(const std::filesystem::path& evento, std::int64_t ahoraMs,
                       const std::atomic<bool>* cancelar = nullptr);

    /// Compara la carpeta con su sellos.json: {ok, cambiados, faltan, nuevos}.
    static json verificar(const std::filesystem::path& evento, const std::atomic<bool>* cancelar = nullptr);
};

} // namespace ba::evento
