// SPDX-License-Identifier: GPL-2.0-or-later
#include "persist/Escritor.hpp"
#include "BaWeb.hpp"
#include "Sondeo.hpp"
#include "../web/version.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "Log.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "diag/Metricas.hpp"
#include "platform/Hilo.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif


namespace ba::gateway {

using ba::i18n::llenar;
using ba::i18n::traducir;

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

/// Lo que ba-web contesta él mismo (sin el gateway): el sondeo de vida del supervisor.
constexpr const char* kVivo = "/_ba/vivo";

} // namespace

bool WebSupervisor::contesta(uint16_t puerto, int topeMs) { return contestaHttp(puerto, kVivo, topeMs); }

std::string WebSupervisor::armarConf(const Cfg& c, const std::string& aliasFile, const std::string& logPath) {
    // Los workers, resueltos acá (como «auto» en ba-web: los núcleos, hasta 4): los topes
    // de abajo se reparten entre ellos.
    const int procesos =
        c.procesos > 0 ? c.procesos : std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 4);
    const std::string cache = std::to_string(c.cacheMs > 0 ? c.cacheMs : 1000) + "ms";
    // Lo que ba-web le abre al gateway sale de los cupos del gateway (F0-07), no de un número
    // propio: cada escucha del gateway acepta `room.maxConexiones` y cada IP real, sumando
    // todo, `room.maxConexionesPorIp`. Repartido entre los workers (cada uno cuenta lo suyo),
    // ba-web nunca le manda más de lo que acepta: lo que sobra lo frena ba-web con un 503,
    // sin un hilo del gateway de por medio. Antes ba-web tenía 512 por worker contra 256 del
    // gateway por escucha. Una cuarta parte de cada escucha queda para lo que llega directo
    // (la red local por HTTP, el túnel si ba-web se cae y se relanza).
    const int cupoEscucha = ba::ajustes::entero("room.maxConexiones", 256);
    const int porIp = ba::ajustes::entero("room.maxConexionesPorIp", 32);
    const int alGateway = std::max(1, cupoEscucha * 3 / 4 / procesos);
    const int tunelesPorIp = std::max(1, porIp / procesos);
    std::string s;
    s += "# Generado por el gateway al arrancar (BaWeb.cpp): no se edita a mano.\n";
    s += "worker_processes " + std::to_string(procesos) + ";\n";
    s += "worker_connections 4096;\n";
    s += "upstream_max_conns " + std::to_string(alGateway) + ";\n";
    s += "limit_tunnels_per_ip " + std::to_string(tunelesPorIp) + ";\n";
    // El túnel. real_ip_xff: la IP real es la última de X-Forwarded-For, la que agrega pinggy (--xff).
    // Sin el túnel, la misma escucha local igual (sin creerle a X-Forwarded-For): la usa el
    // sondeo de vida del supervisor.
    if (c.tunel)
        s += "listen 127.0.0.1:" + std::to_string(c.puerto) + (c.proxyProtocol ? " proxy_protocol" : "") +
             " real_ip_xff;\n";
    else
        s += "listen 127.0.0.1:" + std::to_string(c.puerto) + ";\n";
    // La red local, por HTTPS (el certificado llega por la entrada estándar): a la escucha
    // local del gateway, no a la del túnel (allá todo es «de afuera»).
    if (c.lanPuerto && !c.tlsPem.empty())
        s += "listen " + (c.lanBind.empty() ? std::string("0.0.0.0") : c.lanBind) + ":" + std::to_string(c.lanPuerto) +
             " ssl upstream=127.0.0.1:" + std::to_string(c.lanUpstream) + ";\n";
    // El general (el microcaché y lo que no diga otra cosa).
    s += "upstream 127.0.0.1:" + std::to_string(c.tunel ? c.upstream : c.lanUpstream) + ";\n";
    s += "error_log " + (logPath.empty() ? std::string("stderr") : q(logPath)) + ";\n";
    s += "gzip on;\ngzip_comp_level 6;\n";
    // La raíz: el marco del evento o la sala de invitados (lo decide el gateway); igual
    // para todos, así que un segundo de caché la reparte comprimida.
    s += "location = / { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $uri; }\n";
    s += "location = /index.html { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $uri; }\n";
    // La sala de invitados (los archivos del programa).
    s += "location = /invitados { return 301 /invitados/; }\n";
    s += std::string("location = ") + kVivo + " { return 200 ok; }\n";
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
    // El tema del evento: lo piden TODAS las páginas al abrir y cada 15 s; es igual para todos.
    // La hoja, por URL entera: la versionada (?v=) se guarda un día y la otra no.
    s += "location = /comun/tema.css { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $request_uri; }\n";
    s += "location = /comun/tema.json { proxy_pass; proxy_cache_valid " + cache + "; proxy_cache_key $uri; }\n";
    // Todo lo demás, al gateway (con gzip si conviene; WebSocket y POST como túnel).
    s += "location / { proxy_pass; }\n";
    return s;
}

WebSupervisor::WebSupervisor(Cfg cfg, Log& log) : cfg_(std::move(cfg)), log_(log) {
    confPath_ = (std::filesystem::path(cfg_.dir) / "ba-web.conf").string();
    aliasPath_ = (std::filesystem::path(cfg_.dir) / "evento-web.dir").string();
}

WebSupervisor::~WebSupervisor() {
    stop();
    std::fill(cfg_.tlsPem.begin(), cfg_.tlsPem.end(), '\0'); // la clave privada, fuera de la memoria
}

std::string WebSupervisor::versionDe(const std::string& exe, const std::string& dir) {
    ba::platform::ProcessOptions o;
    o.exe = exe;
    o.args = {"-v"};
    o.hideWindow = true;
    o.killWithParent = true;
    o.outputFile = (std::filesystem::path(dir) / "ba-web-version.txt");
    ba::persist::borrar(o.outputFile); // la salida se agrega al final: que sea solo la de ahora
    ba::platform::Process p;
    if (!ba::platform::Process::spawn(o, p, nullptr) || !p.wait(5000).has_value()) {
        p.kill();
        return {};
    }
    std::string texto;
    if (!ba::persist::leerArchivo(o.outputFile, texto)) return {};
    while (!texto.empty() && (texto.back() == '\n' || texto.back() == '\r' || texto.back() == ' ')) texto.pop_back();
    return texto;
}

bool WebSupervisor::start() {
    if (started_.load()) return alive_.load();
    std::error_code ec;
    if (!std::filesystem::exists(cfg_.exe, ec)) {
        log_.warn(llenar(traducir("gateway", "ba-web: no está el binario (%1); el túnel va directo al gateway"), {cfg_.exe}), -1);
        return false;
    }
    std::filesystem::create_directories(cfg_.dir, ec);
    // La configuración usa directivas de esta versión de ba-web: si el binario es otro (un
    // paquete mezclado), no se lo usa.
    if (const std::string v = versionDe(cfg_.exe, cfg_.dir); v != std::string("ba-web/") + BW_VERSION) {
        log_.warn(llenar(traducir("gateway", "ba-web: es otra versión (%1, se esperaba %2); el túnel va directo al gateway"),
                         {v.empty() ? "?" : v, std::string("ba-web/") + BW_VERSION}),
                  -1);
        return false;
    }
    setEventoDir("");
    // ba-web escribe (y rota: 5 MiB, tres archivos) su propio log; lo de stderr va aparte.
    if (!ba::persist::escribir(confPath_, armarConf(cfg_, aliasPath_, cfg_.logPath))) {
        log_.warn(llenar(traducir("gateway", "ba-web: no se pudo escribir %1; el túnel va directo al gateway"), {confPath_}), -1);
        return false;
    }
    stop_.store(false);
    if (!spawn()) return false;
    started_.store(true);
    thread_ = std::thread([this] { ba::platform::nombrarHilo("ba-web"); runLoop(); });
    log_.info(traducir("gateway", "ba-web: lanzado") +
              (cfg_.tunel ? " " + llenar(traducir("gateway", "(túnel 127.0.0.1:%1 -> %2)"),
                                         {std::to_string(cfg_.puerto), std::to_string(cfg_.upstream)})
                          : std::string()) +
              (atiendeLan() ? " " + llenar(traducir("gateway", "(HTTPS de la red local :%1 -> %2)"),
                                           {std::to_string(cfg_.lanPuerto), std::to_string(cfg_.lanUpstream)})
                            : std::string()));
    return true;
}

void WebSupervisor::stop() {
    if (!started_.load()) return;
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    kill();
    started_.store(false);
    log_.info(llenar(traducir("gateway", "ba-web: detenido (reinicios=%1)"), {std::to_string(restarts_.load())}));
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
    // El certificado y la clave del HTTPS de la red local: por la entrada estándar (un pipe
    // que se cierra), nunca al disco, a la línea de comandos ni al entorno.
    if (atiendeLan()) o.stdinData = cfg_.tlsPem;
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
        log_.warn(llenar(traducir("gateway", "ba-web: no se pudo lanzar: %1"), {err}), -1);
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
    // Cuándo relanzar (platform/Relanzador.hpp): duplicando de 0,5 a 8 s; las caídas se
    // olvidan recién tras 10 s andando y contestando (antes, apenas vivía 250 ms: uno que
    // moría a los 300 ms se relanzaba cada ~0,75 s para siempre); 8 seguidas sin eso abren
    // el cortacircuito (un intento cada 5 min, y la app se entera).
    ba::platform::Relanzador rel(ba::platform::PoliticaRelanzar{
        .esperaMs = ba::platform::PoliticaRelanzar::duplicando(500, []() { return std::uint64_t{8000}; }),
        .estableMs = 10000,
        .cortarTras = 8});
    const auto ahora = []() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    rel.lanzado(ahora());
    // El sondeo de vida: cada `cadaMs`, que conteste; `fallas` seguidas (con el proceso vivo)
    // es un ba-web frenado o colgado, y se lo mata y relanza como si se hubiera cerrado. Lo
    // de recién lanzado tiene su gracia (abre las escuchas en unos cientos de ms).
    constexpr int cadaMs = 1000, topeMs = 1500, fallasMax = 3; // frenado ~7 s y se relanza
    auto proximoSondeo = std::chrono::steady_clock::now() + std::chrono::milliseconds(cadaMs * 2);
    int fallas = 0;
    bool estabaCortado = false;
    while (!stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (rel.agendado()) {
            // Caído, esperando su turno para relanzarlo.
            if (!rel.toca(ahora())) continue;
            {
                std::lock_guard<std::mutex> l(procLock_);
                proc_ = ba::platform::Process();
            }
            if (spawn()) restarts_.fetch_add(1);
            rel.lanzado(ahora());
            proximoSondeo = std::chrono::steady_clock::now() + std::chrono::milliseconds(cadaMs * 2);
            avisar(rel);
            continue;
        }
        bool vive;
        {
            std::lock_guard<std::mutex> l(procLock_);
            vive = proc_.running();
        }
        bool colgado = false;
        if (vive && std::chrono::steady_clock::now() >= proximoSondeo) {
            fallas = contesta(cfg_.puerto, topeMs) ? 0 : fallas + 1;
            colgado = fallas >= fallasMax;
            proximoSondeo = std::chrono::steady_clock::now() + std::chrono::milliseconds(cadaMs);
        }
        if (vive && !colgado) {
            if (rel.vigilar(ahora(), fallas == 0) && estabaCortado) {
                estabaCortado = false;
                log_.info(traducir("gateway", "ba-web: anda estable de nuevo"));
                avisar(rel);
            }
            continue;
        }
        alive_.store(false);
        fallas = 0;
        if (colgado) {
            log_.warn(llenar(traducir("gateway", "ba-web: no contesta (%1 sondeos seguidos); se lo mata y se relanza"),
                             {std::to_string(fallasMax)}),
                      -1);
            std::lock_guard<std::mutex> l(procLock_);
            proc_.kill(); // frenado no atiende SIGTERM: SIGKILL a él y a sus workers (su grupo)
            proc_.wait(3000);
        }
        const std::uint64_t espera = rel.caida(ahora());
        if (rel.cortado() && !estabaCortado) {
            estabaCortado = true;
            log_.error(llenar(traducir("gateway", "ba-web: se cae una y otra vez (%1 seguidas); se reintenta cada %2 s"),
                              {std::to_string(rel.consecutivas()), std::to_string(espera / 1000)}),
                       -1);
        } else {
            log_.warn(llenar(traducir("gateway", "ba-web: se cerró; se relanza en %1 ms"), {std::to_string(espera)}), -1);
        }
        avisar(rel);
    }
}

void WebSupervisor::avisar(const ba::platform::Relanzador& rel) {
    // Las métricas (F4-01, G-47) y, para la app, el estado (evento «web» de la sala).
    static ba::diag::Contador& reinicios = ba::diag::Metricas::proceso().contador(
        "ba_web_reinicios_total", QT_TRANSLATE_NOOP("metricas", "Veces que se relanzó ba-web (se cerró o dejó de contestar)"));
    static ba::diag::Medidor& vivo =
        ba::diag::Metricas::proceso().medidor("ba_web_vivo", QT_TRANSLATE_NOOP("metricas", "ba-web andando (1) o no (0)"));
    static int contados = 0;
    const int r = restarts_.load();
    if (r > contados) {
        reinicios.sumar(static_cast<std::uint64_t>(r - contados));
        contados = r;
    }
    vivo.poner(alive_.load() ? 1.0 : 0.0);
    std::function<void(const nlohmann::json&)> fn;
    {
        std::lock_guard<std::mutex> l(avisoLock_);
        fn = alCambiar_;
    }
    if (fn)
        fn(nlohmann::json{{"vivo", alive_.load()},
                          {"reinicios", r},
                          {"cortado", rel.cortado()},
                          {"seguidas", rel.consecutivas()}});
}

void WebSupervisor::setEventoDir(const std::string& dir) {
    std::lock_guard<std::mutex> l(dirLock_);
    if (dir == dirActual_) return;
    if (ba::persist::escribir(aliasPath_, dir.empty() ? std::string() : dir + "\n")) dirActual_ = dir;
}

} // namespace ba::gateway
