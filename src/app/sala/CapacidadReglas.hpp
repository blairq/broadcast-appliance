// ============================================================================
// src/app/sala/CapacidadReglas.hpp — ¿cuántos invitados aguanta la máquina? (las reglas puras)
//
// La pestaña «Capacidad» (Configuración → Sistema; docs/funciones/CAPACIDAD.md) mete
// invitados de prueba por escalones (10, 20, 40, 60, 80…) contra el gateway —el de esta
// máquina o el de otro BroadcastAppliance— y mira, escalón por escalón, si la máquina
// sigue bien. Acá va lo que no depende de Qt, del gateway ni del reloj, para probarlo
// solo (test_capacidad_reglas):
//
//   - los ESCALONES y cuánto dura cada uno (nunca más de 2 minutos: entrar + asentarse
//     + régimen);
//   - el JUICIO de un escalón con lo medido en su régimen: CPU sostenida (sin la parte del
//     generador si corre en la misma máquina), memoria disponible, la grilla (cuadros por
//     segundo del mosaico), la edad del audio de los invitados, los que no entraron y la
//     interfaz trabada;
//   - la GUARDIA de cada segundo (corta antes de que la máquina se ahogue: memoria, CPU
//     al tope sostenida, la interfaz parada);
//   - el RESULTADO: «aguanta N con margen (máx. M antes de degradarse); lo que limita es…»,
//     con la tendencia de la CPU si la rampa terminó sin degradarse;
//   - la conexión a internet medida «ida y vuelta» (por pinggy) como sala::enlace::Medicion.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ipc/LugaresSala.hpp"
#include "sala/EnlaceNecesita.hpp"

namespace ba::app::sala::capacidad {

/// Los umbrales del juicio y de la guardia.
struct Criterios {
    double cpuMaxPct = 85;           ///< CPU de la máquina medida (sin el generador local), mediana del régimen
    double cpuEquipoMaxPct = 95;     ///< la máquina que GENERA (entera): por encima, el resultado es un mínimo
    double memAvisoMb = 4500;        ///< memoria disponible baja (mínimo del régimen): degradado
    double memCorteMb = 3000;        ///< la guardia corta YA (la de las pruebas de carga)
    double cpsMin = 25;              ///< la grilla (el mosaico) por debajo: degradado
    double audioMaxMs = 50;          ///< la edad del audio de los invitados (mediana): por encima, degradado
    double entraronMin = 0.9;        ///< fracción de los pedidos que tienen que estar con video
    double uiMaxMs = 500;            ///< la interfaz: p90 de la demora del latido por encima, trabada
    double uiTrabadaMs = 2000;       ///< una sola demora así ya es «trabada»
    double cpuAlTopePct = 97;        ///< la guardia: la máquina entera así…
    int cpuAlTopeS = 10;             ///< …tantos segundos seguidos, corta
    double uiCorteMs = 5000;         ///< la guardia: la interfaz parada tanto tiempo, corta
    double margen = 0.8;             ///< «con margen»: esta fracción del máximo
    double cpuConMargenPct = 68;     ///< si el último escalón sano anduvo por debajo, ya tiene margen
};

/// Los tiempos de un escalón (todo junto ≤ maxEscalonS).
struct Tiempos {
    int asentarS = 15;     ///< después de que entraron: no se mide (se acomoda la grilla)
    int regimenS = 30;     ///< lo que se mide
    int maxEscalonS = 120; ///< el tope de un escalón entero
    int entradaMinMs = 50, entradaMaxMs = 300; ///< separación entre entradas
    /// Lo que queda para que entren los nuevos.
    int entradaS() const { return maxEscalonS - asentarS - regimenS; }
};

/// Los escalones hasta `tope` (incluido): 10, 20, 40, 60, 80, 100, 120, 160, 200, 240,
/// 320, 400, 480, 512… recortados; el último es siempre `tope`. Vacío si tope < 1.
std::vector<int> escalones(int tope);
/// La separación entre entradas para que `nuevos` entren a tiempo (ms).
int entradaCadaMs(int nuevos, const Tiempos& t = {});
/// Lo que dura el escalón (s) con `nuevos` que entran: ≤ t.maxEscalonS.
int duracionEscalonS(int nuevos, const Tiempos& t = {});

/// Una muestra del segundo. -1 = no se sabe (no juzga por eso).
struct Muestra {
    int64_t t = 0;
    double cpuMaquina = -1;      ///< % de la máquina MEDIDA, entera
    double cpuGenerador = -1;    ///< % de la máquina medida que usa el generador (solo si es local)
    double cpuEquipo = -1;       ///< % de la máquina que GENERA (local: la misma; remoto: esta)
    double memLibreMb = -1;      ///< memoria disponible de la máquina medida
    double memTotalMb = -1;
    double cpuApp = -1, cpuGateway = -1;
    double cps = -1;             ///< la grilla: cuadros por segundo del mosaico (la última medición)
    double audioMs = -1;         ///< edad del audio de los invitados (mediana entre ellos)
    double uiMs = -1;            ///< cuánto se atrasó el latido de la interfaz (ms)
    int entraron = -1;           ///< invitados de prueba en la sala
    int conVideo = -1;           ///< …con medios arriba
    double kbpsSubida = -1, kbpsBajada = -1;
};
nlohmann::json aJson(const Muestra& m);
Muestra muestraDe(const nlohmann::json& j);

/// La guardia de cada segundo: "" si sigue; si no, por qué se corta (traducido).
/// `ultimas`: las muestras del escalón, la más nueva al final.
std::string guardia(const std::vector<Muestra>& ultimas, const Criterios& c = {});
/// La clave del límite que disparó la guardia ("memoria", "cpu", "interfaz"), "" si no.
std::string claveGuardia(const std::vector<Muestra>& ultimas, const Criterios& c = {});

/// Un escalón juzgado.
struct Paso {
    int n = 0;                  ///< invitados pedidos
    int64_t desde = 0, hasta = 0;
    double cpu = -1;            ///< mediana del régimen, SIN el generador si es local
    double cpuTotal = -1;       ///< mediana de la máquina medida entera
    double cpuGenerador = -1;   ///< mediana de la parte del generador (local)
    double cpuEquipo = -1;      ///< mediana de la máquina que genera
    double memMinMb = -1;
    double memTotalMb = -1;
    double cps = -1;            ///< el peor del régimen
    double audioMs = -1;        ///< mediana
    double uiMs = -1;           ///< p90
    double uiMaxMs = -1;
    int entraron = -1, conVideo = -1; ///< la última muestra
    double kbpsSubida = -1, kbpsBajada = -1; ///< mediana
    bool degradado = false;
    bool guardia = false;       ///< lo cortó la guardia (no llegó al régimen entero)
    /// Lo que se pasó, en orden: cpu, memoria, grilla, audio, entrada, interfaz, generador.
    std::vector<std::string> limites;
    std::vector<std::string> motivos; ///< traducidos
};
nlohmann::json aJson(const Paso& p);

/// Juzga un escalón con las muestras de su régimen. `restarGenerador`: el generador corre
/// en la misma máquina (su CPU se descuenta de la de la máquina para el juicio).
/// `porGuardia`: la guardia lo cortó (cuenta como degradado aunque falten muestras).
Paso juzgar(int n, const std::vector<Muestra>& regimen, bool restarGenerador, const Criterios& c = {},
            const std::string& guardiaClave = {}, const std::string& guardiaMotivo = {});

/// El resultado de toda la rampa.
struct Resultado {
    int maximo = 0;       ///< el último escalón sano (0: ni el primero)
    int conMargen = 0;    ///< lo que conviene usar
    bool alMenos = false; ///< llegó al tope pedido sin degradarse (puede más)
    int degradoEn = 0;    ///< el escalón donde se degradó (0: no se degradó)
    std::string limita;   ///< la clave del límite (cpu, memoria, grilla, audio, entrada, interfaz, generador)
    int estimadoCpu = -1; ///< por la tendencia de la CPU, dónde llegaría al tope (-1: no se puede)
    double cpuGenerador = -1; ///< la parte del generador local en el último escalón sano (%)
    bool cortada = false; ///< se canceló a mano antes de terminar
};
Resultado resumir(const std::vector<Paso>& pasos, int topePedido, bool cancelada, const Criterios& c = {});
nlohmann::json aJson(const Resultado& r);
/// La frase para el operador: «Aguanta 30 invitados con margen (40 como máximo antes de
/// degradarse). Lo que limita es la CPU.» Traducida.
std::string frase(const Resultado& r);
/// El nombre para mostrar de un límite (traducido).
std::string nombreLimite(const std::string& clave);

/// La conexión medida «ida y vuelta» (esta máquina → internet → pinggy → el túnel → esta
/// máquina): cada byte cruza la subida y la bajada, así que lo medido es lo MENOS que dan
/// las dos; para la cuenta del evento se toma como subida y bajada a la vez (un mínimo).
sala::enlace::Medicion idaYVuelta(double kbps, double rttReposoMs, double rttCargaMs, double perdida);

/// El CÓDIGO que muestra el equipo que «recibe» una prueba y se pega en el que la genera:
/// todo junto en una sola línea para copiar («BACAP1.» + base64url del JSON), así nada
/// viaja en una URL ni en la línea de comandos. La huella fija el certificado autofirmado
/// de la caja (vacía si la URL tiene certificado de verdad, p. ej. pinggy).
struct Codigo {
    std::string url;        ///< el gateway del otro (https://IP:puerto o la de pinggy)
    std::string credencial; ///< la credencial de prueba (vence sola)
    std::string huella;     ///< SHA-256 del certificado «AB:CD:…» (o vacía)
    std::string maquina;    ///< su nombre, para mostrar
};
std::string armarCodigo(const Codigo& c);
/// false si no es un código de capacidad (o le falta la URL o la credencial).
bool leerCodigo(const std::string& texto, Codigo& c);

/// La edad del audio de los invitados (mediana de su edad p50) de lo que da la latencia del
/// estudio (estudio_latencia: `entradas` con audio y sin video que empiezan con «Invitado»).
/// -1 si no hay ninguno.
double edadAudioInvitados(const nlohmann::json& latencia);
/// Los cuadros por segundo de la grilla de lo que da grilla_personas_medir: el peor mosaico
/// (la entrada y, si está al aire, el programa). -1 si no midió nada.
double cpsDeMedicion(const nlohmann::json& medicion);
/// De la foto de la sala: {entraron, conVideo} de los de prueba y cuántos reales hay conectados.
struct GenteSala {
    int prueba = 0, pruebaConVideo = 0, reales = 0;
};
GenteSala genteDe(const nlohmann::json& participantes);

/// Lo que se guarda en el historial: un nombre de archivo para la fecha y la máquina.
std::string nombreArchivo(int64_t inicioMs, const std::string& maquina, const std::string& tipo);
/// Un nombre de máquina apto para archivo (sin barras ni espacios raros).
std::string limpiarNombre(const std::string& s);

} // namespace ba::app::sala::capacidad
