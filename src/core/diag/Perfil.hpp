// ============================================================================
// src/core/diag/Perfil.hpp — dónde se va la CPU y la GPU (solo con BA_PERFILAR)
//
// Se compila SIEMPRE (así no se pudre), pero solo arranca en un build con
// -DBA_PERFILAR=ON (`make … PERFILAR=1`): en uno normal `arrancar` no hace nada
// y el programa queda igual.
//
// Cada segundo, del proceso que lo arranca (la app, el gateway):
//   - la CPU de CADA HILO, con su nombre: los de GStreamer se llaman como su
//     elemento y su pad («queue12:src», «d3d12convert0:src»), los de Qt
//     («QSGRenderThread»), los del motor; así se ve qué parte consume;
//   - la CPU total del proceso (en núcleos: 1,0 = un núcleo entero) y la memoria;
//   - en Windows, la GPU del proceso por motor (3D, VideoDecode, VideoEncode,
//     Copy…), de los contadores «GPU Engine» del sistema (los del Administrador
//     de tareas).
// Va a <logs>/perfil-<proceso>.jsonl (una línea JSON por segundo, rota a los
// 50 MB) y cada 10 s un resumen al registro (los 5 hilos que más consumen).
// `ultimo()` da la última medición (la herramienta MCP `perfil`).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace ba::diag {

class Perfil {
   public:
    /// ¿Este build mide? (true con BA_PERFILAR).
    static bool activo();
    /// Arranca la medición de este proceso (una vez; en un build normal, nada).
    static void arrancar(const std::string& proceso, const std::filesystem::path& logsDir);
    /// Lo último medido: {t, proceso, cpu_nucleos, memoria_mb, hilos: [{nombre, tid, cpu}],
    /// gpu: {motor: %}}; vacío si no mide.
    static nlohmann::json ultimo();
    /// Para el hilo de medición (al salir).
    static void parar();
};

} // namespace ba::diag
