// ============================================================================
// src/core/platform/Nucleos.hpp — la topología del procesador y los grupos de núcleos
//
// Una sola fuente para los dos procesos que mueven medios (docs/planes/PLAN-GATEWAY-
// FRAGMENTOS.md, F4):
//
//   - ba-app (src/motor/Prioridad.cpp): EL GRUPO DE MEDIOS. Las entradas, el cruce a cada
//     capa, los mezcladores y el bucle del motor en UN grupo de núcleos que comparte la L3:
//     lo que se pasan (bloques de audio de 10 ms, encargos a la GPU) queda en la misma caché.
//     AMD con varios CCD: el CCD más grande (el primero si son iguales), y lo demás del
//     proceso (Windows) al otro. Intel híbrido: los de rendimiento. Un solo L3 y núcleos
//     iguales: nada. macOS no deja elegir núcleos.
//   - ba-gateway: EL GRUPO DEL GATEWAY. Sus hilos calientes (red, trabajo, TURN,
//     decodificación, audio, grilla, WireGuard) en el bloque de caché que NO usan los
//     medios de ba-app: los dos procesos no se pisan la L3 y lo que se pasan los hilos del
//     gateway entre ellos no cruza de CCD. Sin los núcleos que atienden las interrupciones
//     de la placa de red (en la .177 la I225-V, sin RSS, manda todo al 0 y al 1). Solo con
//     varios grupos de L3 entre los núcleos rápidos; en un híbrido o con un solo L3, nada
//     (el otro «grupo» serían los núcleos de eficiencia).
//
// Los dos procesos calculan lo mismo con la misma función, así que coinciden sin hablarse.
// Windows con CPU Sets (preferencia blanda: el sistema puede igual mover un hilo si hace
// falta); Linux con sched_setaffinity (dura). Ajustes: `procesador.mediosJuntos` (ba-app) y
// `gateway.afinidad` / `gateway.afinidadEvitar` (gateway); los lee quien llama.
// ============================================================================
#pragma once

#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::platform::nucleos {

/// El procesador: {clases:[{clase, nucleos, logicos}], grupos_l3:[{logicos, cpus|mascara}]}.
/// Sin aplicar nada.
nlohmann::json topologia();
/// Una línea para el log: «8 de rendimiento + 2 de eficiencia; 2 grupos de L3 (CCD)».
std::string resumen(const nlohmann::json& topo);

// --- ba-app: el grupo de medios ----------------------------------------------------
/// Lo calcula y lo deja listo (Windows: además, lo demás del proceso a los otros núcleos).
/// Devuelve lo que hizo, para el log ("un solo grupo de núcleos: no hace falta", …).
std::string armarGrupoMedios();
/// El hilo que llama, al grupo de medios (si se armó).
void alGrupoMedios();
bool hayGrupoMedios();

// --- ba-gateway: el bloque de caché que no usan los medios ---------------------------
/// `evitar`: los procesadores lógicos que no se usan (las interrupciones de la placa de
/// red). "auto": Linux los saca de /proc/interrupts (los que atienden la mayor parte de las
/// interrupciones de las placas de red); Windows, el 0 y el 1 (sin RSS, ahí caen las DPC de
/// la placa). "" = ninguno; "0,1" o "0-1" = esos. Se sacan solo si quedan al menos 2.
/// Lo calcula, lo pone como el grupo de omisión del PROCESO (los hilos de libwebrtc que no
/// creamos nosotros también caen ahí: Windows SetProcessDefaultCpuSets; Linux, el hilo que
/// llama, y los que cree después lo heredan: llamarlo en el principal antes de arrancar
/// nada) y devuelve lo que hizo, para el log.
std::string armarGrupoGateway(const std::string& evitar);
/// El hilo que llama, al grupo del gateway (si se armó; si no, nada). Para los hilos
/// calientes del gateway (rtc/Capas, turn, colas compartidas, grillas, WireGuard): barato,
/// se puede llamar siempre.
void alGrupoGateway();
bool hayGrupoGateway();

// --- Para las pruebas ------------------------------------------------------------------
/// Un procesador lógico tal como lo ve el reparto.
struct Cpu {
    int logico = 0;     ///< número de procesador lógico (Linux: cpuN; Windows: grupo*64 + índice)
    unsigned long id = 0; ///< Windows: el id del CPU Set; Linux: = logico
    int clase = 0;      ///< más alto = más rápido
    int l3 = 0;         ///< índice del grupo de L3
};
struct Reparto {
    std::set<int> medios;  ///< lógicos del grupo de medios (vacío = no hace falta)
    std::set<int> gateway; ///< lógicos del grupo del gateway (vacío = no hace falta)
    bool hibrido = false;
};
/// El reparto puro (sin sistema): lo usan las dos funciones de arriba y test_nucleos.
Reparto repartir(const std::vector<Cpu>& cpus, const std::set<int>& evitar);
/// «0-5,12-17» → {0..5, 12..17}. Vacío o basura: {}.
std::set<int> listaCpus(const std::string& s);
/// Las CPUs que atienden la mayor parte de las interrupciones de las placas de red, a
/// partir del texto de /proc/interrupts y los nombres de las interfaces (`eth0`, `enp3s0`…):
/// las que suman al menos el 20 % del total. Pura: la prueba le pasa el texto.
std::set<int> cpusDeInterrupciones(const std::string& procInterrupts, const std::vector<std::string>& interfaces);

} // namespace ba::platform::nucleos
