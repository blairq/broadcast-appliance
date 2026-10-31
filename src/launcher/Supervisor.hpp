// Supervisor.hpp
//
// Proposito:
//   Corazon del launcher (Proceso 0, spec 3.1): instancia unica, verificacion
//   de integridad, modo de error, lanzamiento de los hijos (B, los emisores y
//   A), watchdog (crash por salida del proceso + cuelgue por heartbeat),
//   politica de reinicio con backoff y circuit breaker, readopt de hijos vivos
//   y cierre ordenado. Igual en Windows, Linux y macOS: todo lo de SO va por
//   `ba::platform` (via ChildProcess, ControlChannel y SingleInstance).
//
//   LOS HIJOS son una lista (`Hijo`): cada uno con su ejecutable, su slot de
//   BA.Health, su canal de control, su log y su backoff. Lo que es de uno solo
//   va por su papel: A (la app) lleva el --restore, el modo seguro escalonado y
//   el cartel de recuperación, y salir con 0 es el cierre del operador; B (la
//   sala) se relanza sin contar si se cerró en orden; un EMISOR (F3-01,
//   `ba-emisor --salida=<id>`, con `emisor.aparte`) es un hijo más que no cae
//   con A: forzar el reinicio desde el cartel no lo toca (la salida sigue al
//   aire mientras A vuelve).
//
// Modelo de threading:
//   run() se ejecuta en el thread principal. El loop de vigilancia vive en ese
//   mismo thread y sondea cada 250 ms (el umbral de cuelgue es 5 s, spec
//   3.1.7). `requestStop()` es async-signal-safe (solo escribe un atomic) y
//   se puede llamar desde un handler de señal o desde otro thread.
//
// Estrategia de errores:
//   Nunca lanza. Cada fallo (integridad, lanzamiento, dump) se loguea y se
//   traduce en una accion degradada: advertencia, reinicio con backoff o modo
//   seguro. Que A salga con codigo 0 es el cierre ordenado del operador, no
//   un fallo.

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "ChildProcess.hpp"
#include "ControlChannel.hpp"
#include "Integrity.hpp"
#include "MetricasLauncher.hpp"
#include "PerroGuardian.hpp"
#include "Politica.hpp"
#include "ipc/HealthBlock.hpp"
#include "platform/SingleInstance.hpp"
#include "RecoveryUi.hpp"
#include "Volcados.hpp"

namespace ba::launcher {

/// Supervisor de los procesos hijos: A (app), B (gateway) y los emisores.
class Supervisor {
   public:
    /// Configuracion del supervisor.
    struct Config {
        /// --reiniciar: esperar (hasta tanto) a que se cierre el launcher anterior en vez de no arrancar.
        int esperarInstanciaMs = 0;
        std::filesystem::path root;           // raiz del bundle (ba::platform::bundleRoot)
        std::filesystem::path dataRoot;       // raiz de los datos (ba::platform::dataRoot; vacia = root)
        std::filesystem::path appExe;         // bin/64bit/ba-app(.exe)
        std::filesystem::path gatewayExe;     // bin/64bit/ba-gateway(.exe)
        std::filesystem::path logsDir;        // Logs/ (dumps y salida de A y B)
        std::vector<std::string> appArgs;     // argumentos extra para A
        std::vector<std::string> gatewayArgs; // argumentos extra para B
        // Copia sombra (spec 3.1.5): OPCIONAL y desactivada por defecto. Sin
        // ella A y B corren desde el bundle, donde esta. Se activa con
        // --shadow (o "shadowCopy": true en Config/launcher.json).
        bool shadowCopy = false;
        bool recoveryUi = true;  // dialogo ante fallos (false = reinicio automatico)
        // Lo que se ajusta en Configuración → Sistema y datos (launcher.json); los
        // valores de acá son los de siempre. Rigen al volver a abrir el launcher.
        std::uint64_t cuelgueMs = 5000;          // latido quieto tanto tiempo = colgado (volcado y reinicio)
        std::uint64_t graciaArranqueMs = 20000;  // recién lanzado: todavía no se vigila el latido
        int maxCaidas = 5;                       // más caídas que esto en la ventana: modo seguro
        std::uint64_t ventanaCaidasMs = 120000;  // la ventana del cortacircuito
        int reiniciosSinPreguntar = 2;           // fallas seguidas que se reinician sin preguntar
        // Watchdog de hardware (F4-08, solo Linux): apagado de fábrica (perroS = 0).
        std::string perroDispositivo = "/dev/watchdog";
        int perroS = 0;                          // plazo de la placa; 0 = no se usa
        std::uint32_t slotA = ba::ipc::kSlotSaludApp;     // slot de A en BA.Health
        std::uint32_t slotB = ba::ipc::kSlotSaludGateway; // slot de B en BA.Health
        // El emisor aparte (F3-01): las salidas que codifica un `ba-emisor` propio
        // (vacío = A codifica todo, como siempre). Cada una ocupa un slot de BA.Health
        // (ipc::slotSaludEmisor) y A se entera por BA_EMISOR.
        std::filesystem::path emisorExe;      // bin/64bit/ba-emisor(.exe)
        std::vector<std::string> emisores;    // ids de salida ("principal")
    };

    explicit Supervisor(Config cfg);
    ~Supervisor();

    /// Punto de entrada: ejecuta todo el ciclo de vida del launcher.
    ///
    /// :returns: codigo de salida del proceso.
    int run();

    /// Pide el cierre ordenado (Ctrl+C, SIGTERM, fin de sesion).
    /// Async-signal-safe.
    static void requestStop();

   private:
    /// Qué es cada hijo: lo que hace distinto a uno de otro (ver arriba).
    enum class Rol { App, Sala, Emisor };
    /// Un proceso hijo y lo que el supervisor sabe de él.
    struct Hijo {
        Rol rol = Rol::Emisor;
        std::string nombre;          ///< "A", "B", "E.principal": registro, volcados
        std::string componente;      ///< "app", "gateway", "emisor": caidas.jsonl
        std::filesystem::path exe;
        std::vector<std::string> args;
        std::map<std::string, std::string> env; ///< además del canal de caídas
        std::uint32_t slot = 0;      ///< en BA.Health
        std::string canalControl;    ///< el cierre ordenado
        std::string log;             ///< el archivo en Logs/ con su salida
        ChildProcess proceso;
        Relanzador relanzador;       ///< el backoff agendado (Politica.hpp)
        /// Momento del ultimo lanzamiento (ms monotonicos): durante la gracia de
        /// arranque no se evalua el heartbeat (el proceso todavia no late).
        std::uint64_t lanzadoMs = 0;
        std::uint64_t latidoAlLanzar = 0; ///< el latido que había al lanzarlo (para saber si ya latió)
        bool otraVersion = false;         ///< readoptado de otra versión con aire: se reemplaza sin aire
    };

    // --- Ciclo de vida ------------------------------------------------------
    void applyExecRoot(const std::filesystem::path& execRoot);
    /// Dónde vive el caché de la verificación (en los datos, nunca adentro de la .app).
    std::filesystem::path cacheIntegridad() const;
    IntegrityReport verifyIntegrity();
    /// Lo que la verificación rápida dejó pendiente, en el fondo y con la app andando;
    /// guarda el caché apenas termina (si falla algo, deja la marca para el próximo arranque).
    void verificarEnElFondo();
    std::filesystem::path marcaFallo() const;

    // --- Hijos --------------------------------------------------------------
    /// Arma la lista (B, los emisores, A: el orden de lanzamiento) desde la Config.
    void armarHijos();
    void launchChildren();
    void launchOne(Hijo& h);
    bool startChild(Hijo& h);
    void restartChild(Hijo& h); // agenda el relanzamiento con backoff (comun a crash/hang)
    /// Lanza los hijos cuyo relanzamiento agendado ya toca (el bucle no duerme el backoff).
    void relanzarAgendados();
    /// Un hijo readoptado de OTRA versión: cierre ordenado y lanzamiento del de ahora.
    void reemplazarOtraVersion(Hijo& h);
    /// Runtime/ de los datos (al-aire.json, ultima-caida.json).
    std::filesystem::path runtimeDir() const;
    void handleCrash(Hijo& h);
    /// Deja escrita la causa de la caída (Runtime/ultima-caida.json y caidas.jsonl).
    void anotarCaida(Hijo& h, const std::string& causa, std::optional<int> code, const Volcado* volcado = nullptr);
    void handleHang(Hijo& h);
    Hijo& app() { return *hijos_.back(); }   ///< A, el último en lanzarse
    Hijo& sala() { return *hijos_.front(); } ///< B, el primero

    // --- Recuperacion (UI) --------------------------------------------------
    /// Muestra el cartel de recuperación EN OTRO HILO: el watchdog sigue relanzando
    /// mientras tanto (antes esperaba hasta 5 min la respuesta con la sala abajo). La
    /// decisión llega por `decisionRecuperacion_` y se toma en la vuelta siguiente.
    void presentRecovery(bool appDown, bool appHung, bool gwDown, bool gwHung);
    /// -1 = sin decisión pendiente; si no, un RecoveryAction.
    std::shared_ptr<std::atomic<int>> decisionRecuperacion_ = std::make_shared<std::atomic<int>>(-1);
    std::shared_ptr<std::atomic<bool>> preguntando_ = std::make_shared<std::atomic<bool>>(false);
    void forceRestart();

    // --- Vigilancia y cierre ------------------------------------------------
    void watchdogLoop();
    void orderedShutdown();
    void stopChild(Hijo& h);

    // --- Estado -------------------------------------------------------------
    Config cfg_;
    /// B salió con código 0, sin volcado y no recién (dos en 10 s ya no cuentan como pedido).
    bool esCierreOrdenadoB(std::optional<int> code = std::nullopt);
    std::uint64_t ultimoCierreOrdenadoB_ = 0; ///< la última vez que la sala cerró en orden (reinicio pedido)
    /// En orden de lanzamiento: B, los emisores, A (el cierre: A, los emisores, B).
    std::vector<std::unique_ptr<Hijo>> hijos_;
    ba::ipc::HealthBlock health_;
    ControlChannel control_;
    ba::platform::SingleInstance instance_;
    bool shuttingDown_ = false;

    static std::atomic<bool> stopRequested_;

    std::unique_ptr<Volcados> volcados_; ///< los volcados de los hijos, con su .json (Volcados.hpp)
    PerroGuardian perro_;         ///< el watchdog de hardware (PerroGuardian.hpp), si se pidió
    MetricasLauncher metricas_;   ///< Runtime/metricas-launcher.prom (MetricasLauncher.hpp)
    bool inStartupGrace(const Hijo& h) const;

    std::uint64_t ultimaMiradaAire_ = 0; // ms monotonicos: al-aire.json se mira cada tanto, no cada vuelta
    bool appNeedsRestore_ = false;             // A relanzado tras crash arranca con --restore
    ba::fault::ModoSeguro modoSeguro_;         // cortacircuito escalonado: un nivel por disparo (F1-10)
    std::optional<ba::fault::Pieza> ultimaPiezaA_; // a quién se atribuyó la última caída de A (Volcados)
    std::vector<std::uint64_t> appCrashTimes_; // ms monotonicos, para el circuit breaker
    std::uint64_t lastFailureMs_ = 0;          // ultima caida/cuelgue de un hijo
    int recentFailures_ = 0;                   // fallas dentro de la ventana (decide el dialogo)

    std::thread fondo_;                   ///< la verificación de fondo
    std::atomic<bool> pararFondo_{false}; ///< se cierra: dejar de leer

    // Cache del reporte de integridad para la UI de recuperacion.
    std::uint64_t lastIntegrityCheckMs_ = 0;
    IntegrityReport lastIntegrityReport_;
    bool hasIntegrityReport_ = false;
};

} // namespace ba::launcher
