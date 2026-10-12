// SPDX-License-Identifier: GPL-2.0-or-later
#include "BaWeb.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Log.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ba::gateway {

namespace {

/// Una ruta entre comillas para la configuración de ba-web: barras normales (también en
/// Windows: el '\' escapa en su sintaxis) y las comillas escapadas.
std::string q(const std::string& ruta) {
    std::string out = "\"";
    for (char c : ruta) {
        if (c == '\\') c = '/';
        if (c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}

bool escribirAtomico(const std::filesystem::path& p, const std::string& contenido) {
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << contenido;
        if (!f) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}

} // namespace

std::string WebSupervisor::armarConf(const Cfg& c, const std::string& aliasFile, const std::string& logPath) {
    const std::string procesos = c.procesos > 0 ? std::to_string(c.procesos) : "auto";
    const std::string cache = std::to_string(c.cacheMs > 0 ? c.cacheMs : 1000) + "ms";
    std::string s;
    s += "# Generado por el gateway al arrancar (BaWeb.cpp): no se edita a mano.\n";
    s += "worker_processes " + procesos + ";\n";
    s += "worker_connections 4096;\n";
    // real_ip_xff: la IP real es la última de X-Forwarded-For, la que agrega pinggy (--xff).
    s += "listen 127.0.0.1:" + std::to_string(c.puerto) + (c.proxyProtocol ? " proxy_protocol" : "") + " real_ip_xff;\n";
    s += "upstream 127.0.0.1:" + std::to_string(c.upstream) + ";\n";
    s += "error_log " + (logPath.empty() ? std::string("stderr") : q(logPath)) + ";\n";
    s += "gzip on;\ngzip_comp_level 6;\n";
    // La raíz: el marco del evento o la sala de invitados (lo decide el gateway); igual
    // para todos, así que un segundo de caché la reparte comprimida.
    s += "location = / { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $uri; }\n";
    s += "location = /index.html { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $uri; }\n";
    // La sala de invitados (los archivos del programa).
    s += "location = /invitados { return 301 /invitados/; }\n";
    if (!c.webroot.empty())
        s += "location /invitados/ { alias " + q(c.webroot + "/") +
             "; index index.html; add_header Cache-Control no-cache; try_files $uri @upstream; }\n";
    // La página del evento: lo del programa y la carpeta Web/ del evento en vivo.
    if (!c.eventoWebroot.empty())
        s += "location /evento/_/ { alias " + q(c.eventoWebroot + "/") +
             "; add_header Cache-Control no-cache; try_files $uri @upstream; }\n";
    s += "location /evento/ { alias_file " + q(aliasFile) +
         "; index portada.html; add_header Cache-Control no-cache; try_files $uri @upstream; }\n";
    // La API del evento: un microcaché; los ids de mirada van al gateway.
    s += "location = /api/v1/evento { proxy_pass; proxy_cache_valid " + cache +
         "; proxy_cache_key $uri; ba_vistos v; }\n";
    s += "location = /api/v1/evento/vivos { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $uri; }\n";
    // Todo lo demás, al gateway (con gzip si conviene; WebSocket y POST como túnel).
    s += "location / { proxy_pass; }\n";
    return s;
}

WebSupervisor::WebSupervisor(Cfg cfg, Log& log) : cfg_(std::move(cfg)), log_(log) {
    confPath_ = (std::filesystem::path(cfg_.dir) / "ba-web.conf").string();
    aliasPath_ = (std::filesystem::path(cfg_.dir) / "evento-web.dir").string();
}

WebSupervisor::~WebSupervisor() { stop(); }

bool WebSupervisor::start() {
    if (started_.load()) return alive_.load();
    std::error_code ec;
    if (!std::filesystem::exists(cfg_.exe, ec)) {
        log_.warn("ba-web: no está el binario (" + cfg_.exe + "); el túnel va directo al gateway", -1);
        return false;
    }
    std::filesystem::create_directories(cfg_.dir, ec);
    setEventoDir("");
    // ba-web escribe (y rota: 5 MiB, tres archivos) su propio log; lo de stderr va aparte.
    if (!escribirAtomico(confPath_, armarConf(cfg_, aliasPath_, cfg_.logPath))) {
        log_.warn("ba-web: no se pudo escribir " + confPath_ + "; el túnel va directo al gateway", -1);
        return false;
    }
    stop_.store(false);
    if (!spawn()) return false;
    started_.store(true);
    thread_ = std::thread(&WebSupervisor::runLoop, this);
    log_.info("ba-web: lanzado (127.0.0.1:" + std::to_string(cfg_.puerto) + " -> gateway 127.0.0.1:" +
              std::to_string(cfg_.upstream) + ")");
    return true;
}

void WebSupervisor::stop() {
    if (!started_.load()) return;
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    kill();
    started_.store(false);
    log_.info("ba-web: detenido (reinicios=" + std::to_string(restarts_.load()) + ")");
}

bool WebSupervisor::spawn() {
    ba::platform::ProcessOptions o;
    o.exe = cfg_.exe;
#ifdef _WIN32
    const long long yo = static_cast<long long>(::GetCurrentProcessId());
#else
    const long long yo = static_cast<long long>(::getpid());
#endif
    o.args = {"-c", confPath_, "--padre", std::to_string(yo)};
    o.envSet["BA_WEB_CLAVE"] = cfg_.clave; // por el entorno: por la línea de comandos se vería en `ps`
    o.killWithParent = true;
    o.hideWindow = true;
    // La salida cruda (solo errores antes de abrir su log, o un aborto): aparte y con tope.
    const std::filesystem::path salida = cfg_.logPath + ".salida";
    std::error_code ec;
    if (std::filesystem::file_size(salida, ec) > (1u << 20) && !ec) std::filesystem::resize_file(salida, 0, ec);
    o.outputFile = salida;
    std::lock_guard<std::mutex> l(procLock_);
    std::string err;
    if (!ba::platform::Process::spawn(o, proc_, &err)) {
        log_.warn("ba-web: no se pudo lanzar: " + err, -1);
        alive_.store(false);
        return false;
    }
    alive_.store(true);
    return true;
}

void WebSupervisor::kill() {
    std::lock_guard<std::mutex> l(procLock_);
    if (proc_.running()) {
        proc_.terminate(); // SIGTERM: los workers terminan lo que están mandando (hasta 5 s)
        if (!proc_.wait(6000).has_value()) {
            proc_.kill();
            proc_.wait(3000);
        }
    }
    proc_ = ba::platform::Process();
    alive_.store(false);
}

void WebSupervisor::runLoop() {
    int backoffMs = 500;
    while (!stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        bool vive;
        {
            std::lock_guard<std::mutex> l(procLock_);
            vive = proc_.running();
        }
        if (vive) {
            backoffMs = 500;
            continue;
        }
        alive_.store(false);
        log_.warn("ba-web: se cerró; se relanza en " + std::to_string(backoffMs) + " ms", -1);
        for (int w = 0; w < backoffMs && !stop_.load(); w += 100) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stop_.load()) break;
        {
            std::lock_guard<std::mutex> l(procLock_);
            proc_ = ba::platform::Process();
        }
        if (spawn()) restarts_.fetch_add(1);
        if (backoffMs < 8000) backoffMs *= 2;
    }
}

void WebSupervisor::setEventoDir(const std::string& dir) {
    std::lock_guard<std::mutex> l(dirLock_);
    if (dir == dirActual_) return;
    if (escribirAtomico(aliasPath_, dir.empty() ? std::string() : dir + "\n")) dirActual_ = dir;
}

} // namespace ba::gateway
