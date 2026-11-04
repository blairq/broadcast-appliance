// ============================================================================
// src/gateway/Cloudflared.cpp — implementacion del supervisor de cloudflared
//
// Proposito:
//   Implementa CloudflaredSupervisor (ver el header), igual en Windows, Linux y
//   macOS, sobre ba::platform::Process:
//     - spawn(): sin ventana, stdout/stderr agregados a Logs/cloudflared.log,
//       token por env TUNNEL_TOKEN (no por CLI) y killWithParent (Job Object en
//       Windows, PDEATHSIG en Linux): si B muere, el tunel muere con el.
//     - runLoop(): cada 2 s verifica salud (proceso vivo + puerto de metricas)
//       y, si cae, reinicia con backoff creciente (1 s -> 30 s).
//   Antes todo esto era Win32 y en el resto un no-op: fuera de Windows no
//   habia tunel y ningun invitado remoto podia llegar al gateway.
//
// Modelo de threading:
//   - runLoop corre en su thread y se detiene por stop_.
//   - start()/stop() se llaman desde el thread de control de B; el proceso
//     solo lo tocan spawn/kill, protegidos por procLock_.
//
// Estrategia de errores:
//   No lanza. Sin token o sin binario no se lanza nada (se avisa una vez). Un
//   fallo de lanzamiento se loguea y se reintenta con backoff. El gateway
//   sigue sin tunel: solo no hay conexiones nuevas desde internet.
// ============================================================================

#include "Cloudflared.hpp"
#include "Sondeo.hpp"
#include "ajustes/Ajustes.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Log.hpp"
#include "i18n/I18nTexto.hpp"
#include "platform/Hilo.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;

CloudflaredSupervisor::CloudflaredSupervisor(std::string cloudflaredPath, std::string tunnelToken,
                                             uint16_t metricsPort, std::string logPath, Log& log)
    : cloudflaredPath_(std::move(cloudflaredPath)), tunnelToken_(std::move(tunnelToken)),
      metricsPort_(metricsPort), logPath_(std::move(logPath)), log_(log) {}

CloudflaredSupervisor::~CloudflaredSupervisor() { stop(); }

bool CloudflaredSupervisor::start() {
    // Sin token no hay tunel posible: lanzarlo igual solo generaba un bucle de
    // reinicios. El gateway queda accesible en la red local.
    if (tunnelToken_.empty()) {
        log_.info(traducir("gateway", "cloudflared: sin token de túnel (Configuración → Cloudflare o BA_TUNNEL_TOKEN); "
                                      "sin túnel, solo acceso local"));
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(cloudflaredPath_, ec)) {
        log_.warn(llenar(traducir("gateway", "cloudflared: no está el binario (%1); sin túnel. `make deps-cloudflared`"),
                         {cloudflaredPath_}),
                  -1);
        return false;
    }
    stop_.store(false);
    if (!spawn()) {
        log_.warn(traducir("gateway", "cloudflared: no se pudo lanzar (se reintentara con backoff)"), -1);
    }
    thread_ = std::thread([this] { ba::platform::nombrarHilo("cloudflared"); runLoop(); });
    started_ = true;
    log_.info(llenar(traducir("gateway", "cloudflared: supervisor arrancado (metrics=127.0.0.1:%1)"),
                     {std::to_string(metricsPort_)}));
    return alive_.load();
}

void CloudflaredSupervisor::stop() {
    if (!started_) {
        return; // idempotente (lo llaman main y el destructor)
    }
    started_ = false;
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    kill();
    log_.info(llenar(traducir("gateway", "cloudflared: supervisor detenido (reinicios=%1)"),
                     {std::to_string(restartCount_.load())}));
}

bool CloudflaredSupervisor::running() const { return alive_.load(); }

int CloudflaredSupervisor::restartCount() const { return restartCount_.load(); }

bool CloudflaredSupervisor::spawn() {
    ba::platform::ProcessOptions o;
    o.exe = cloudflaredPath_;
    // Flags validos en cloudflared 2024.11.0 (`tunnel run --help`). El de
    // logging es `--loglevel` (sin guion interno): `--log-level` se rechaza y
    // el tunel moria en cada arranque. El token NO va aca: va por env.
    o.args = {"tunnel",     "--no-autoupdate",
              "--metrics",  "127.0.0.1:" + std::to_string(metricsPort_),
              "--loglevel", "info",
              "run"};
    o.envSet["TUNNEL_TOKEN"] = tunnelToken_;
    o.killWithParent = true; // si B muere, el tunel muere con el (spec §8.8)
    o.hideWindow = true;
    o.outputFile = logPath_;

    std::lock_guard<std::mutex> lock(procLock_);
    std::error_code ec;
    const auto size = std::filesystem::file_size(logPath_, ec);
    logOffset_ = ec ? 0 : size;
    std::string err;
    if (!ba::platform::Process::spawn(o, proc_, &err)) {
        log_.warn(llenar(traducir("gateway", "cloudflared: no se pudo lanzar: %1"), {err}), -1);
        return false;
    }
    alive_.store(true);
    // El backoff NO se resetea aca: un proceso que arranca y muere enseguida
    // (token invalido) lo relanzaria cada 2 s para siempre. Se resetea cuando
    // el tunel queda confirmado arriba (runLoop).
    log_.info(llenar(traducir("gateway", "cloudflared: lanzado (pid=%1)"), {std::to_string(proc_.pid())}));
    return true;
}

void CloudflaredSupervisor::kill() {
    std::lock_guard<std::mutex> lock(procLock_);
    if (proc_.running()) {
        proc_.terminate();
        if (!proc_.wait(3000).has_value()) {
            proc_.kill();
            proc_.wait(3000);
        }
    }
    proc_ = ba::platform::Process();
    alive_.store(false);
}

bool CloudflaredSupervisor::processAlive() {
    std::lock_guard<std::mutex> lock(procLock_);
    return proc_.running();
}

bool CloudflaredSupervisor::probeMetrics() const {
    // Que CONTESTE (cloudflared sirve Prometheus en /metrics): que el puerto acepte no
    // alcanza, a uno frenado (SIGSTOP, colgado) el sistema le sigue aceptando conexiones.
    return contestaHttp(metricsPort_, "/metrics", 1500);
}

std::string CloudflaredSupervisor::outputSinceSpawn() const {
    std::ifstream in(logPath_, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    std::streamoff from = static_cast<std::streamoff>(logOffset_.load());
    if (from > end) from = 0; // el log se roto o se trunco
    if (end - from > 8192) from = end - 8192;
    in.seekg(from);
    std::string out(static_cast<size_t>(end - from), '\0');
    in.read(out.data(), static_cast<std::streamsize>(out.size()));
    return out;
}

namespace {
std::uint64_t ahoraMono() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
} // namespace

std::uint64_t CloudflaredSupervisor::esperarRelanzar() {
    const std::uint64_t espera = rel_.caida(ahoraMono());
    // En tramos cortos para que stop() no espere un backoff entero.
    while (!rel_.toca(ahoraMono()) && !stop_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return espera;
}

void CloudflaredSupervisor::runLoop() {
    int probeFailures = 0;
    while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (stop_.load(std::memory_order_relaxed)) break;

        const bool alive = processAlive();
        // El puerto de metricas tarda unos segundos en abrir tras el
        // arranque: se toleran algunos sondeos fallidos con el proceso vivo.
        probeFailures = (alive && !probeMetrics()) ? probeFailures + 1 : 0;
        if (alive && probeFailures < 5) {
            if (probeFailures == 0 && !loggedUp_) {
                loggedUp_ = true;
                rel_.vigilar(ahoraMono(), true, 0); // arriba de verdad: las caídas se olvidan
                log_.info(traducir("gateway", "cloudflared: túnel arriba (metrics ok)"));
            }
            continue;
        }

        // Token invalido: reintentar no lo arregla (el log se llenaba de
        // reinicios para siempre). Se avisa una vez y se deja de supervisar.
        if (!alive && outputSinceSpawn().find("token is not valid") != std::string::npos) {
            log_.error(traducir("gateway", "cloudflared: el token del túnel (BA_TUNNEL_TOKEN) no es válido; túnel "
                                           "desactivado hasta corregirlo y reiniciar el gateway"),
                       -1);
            kill();
            break;
        }
        log_.warn(alive ? traducir("gateway", "cloudflared: sin metricas; reiniciando con backoff")
                        : traducir("gateway", "cloudflared: caido; reiniciando con backoff"),
                  -1);
        loggedUp_ = false;
        probeFailures = 0;
        kill();
        const std::uint64_t espera = esperarRelanzar();
        if (stop_.load(std::memory_order_relaxed)) break;
        if (spawn()) {
            ++restartCount_;
            rel_.lanzado(ahoraMono());
        }
        log_.info(llenar(traducir("gateway", "cloudflared: relanzado tras %1 s"), {std::to_string(espera / 1000)}));
    }
}

} // namespace ba::gateway
