// SPDX-License-Identifier: GPL-2.0-or-later
#include "persist/Escritor.hpp"
#include "BaWeb.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "Log.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "platform/Hilo.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
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

#ifdef _WIN32
using Sock = SOCKET;
constexpr Sock kSinSock = INVALID_SOCKET;
void cerrarSock(Sock s) { closesocket(s); }
#else
using Sock = int;
constexpr Sock kSinSock = -1;
void cerrarSock(Sock s) { ::close(s); }
#endif

/// ¿El socket está listo (para escribir o leer) antes de `ms`?
bool listo(Sock s, bool escribir, int ms) {
    fd_set fs;
    FD_ZERO(&fs);
    FD_SET(s, &fs);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return ::select(static_cast<int>(s) + 1, escribir ? nullptr : &fs, escribir ? &fs : nullptr, nullptr, &tv) > 0;
}

} // namespace

bool WebSupervisor::contesta(uint16_t puerto, int topeMs) {
    // Una conexión TCP no alcanza: a un ba-web frenado (SIGSTOP, colgado) el sistema le
    // sigue aceptando conexiones en la cola de su escucha. Tiene que CONTESTAR, a tiempo.
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return true; // sin red no se puede saber: no se lo mata
#endif
    bool ok = false;
    const Sock s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s != kSinSock) {
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
#else
        ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        const int uno = 1; // un ba-web que cierra a mitad no tiene que voltear al gateway (SIGPIPE)
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &uno, sizeof uno);
#endif
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(puerto);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        const auto hasta = std::chrono::steady_clock::now() + std::chrono::milliseconds(topeMs);
        const auto queda = [&hasta]() {
            return static_cast<int>(std::max<long long>(
                0, std::chrono::duration_cast<std::chrono::milliseconds>(hasta - std::chrono::steady_clock::now()).count()));
        };
        ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
        if (listo(s, true, queda())) {
            const std::string pedido = std::string("GET ") + kVivo + " HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n";
#ifdef MSG_NOSIGNAL
            constexpr int kSinSenal = MSG_NOSIGNAL;
#else
            constexpr int kSinSenal = 0;
#endif
            if (::send(s, pedido.data(), static_cast<int>(pedido.size()), kSinSenal) == static_cast<int>(pedido.size())) {
                std::string r;
                char buf[256];
                while (r.size() < 12 && listo(s, false, queda())) {
                    const int n = static_cast<int>(::recv(s, buf, sizeof buf, 0));
                    if (n <= 0) break;
                    r.append(buf, static_cast<size_t>(n));
                }
                ok = r.rfind("HTTP/1.", 0) == 0 && r.size() >= 12 && r.compare(9, 3, "200") == 0;
            }
        }
        cerrarSock(s);
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return ok;
}

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

bool WebSupervisor::start() {
    if (started_.load()) return alive_.load();
    std::error_code ec;
    if (!std::filesystem::exists(cfg_.exe, ec)) {
        log_.warn(llenar(traducir("gateway", "ba-web: no está el binario (%1); el túnel va directo al gateway"), {cfg_.exe}), -1);
        return false;
    }
    std::filesystem::create_directories(cfg_.dir, ec);
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
    int backoffMs = 500;
    // El sondeo de vida: cada `cadaMs`, que conteste; `fallas` seguidas (con el proceso vivo)
    // es un ba-web frenado o colgado, y se lo mata y relanza como si se hubiera cerrado. Lo
    // de recién lanzado tiene su gracia (abre las escuchas en unos cientos de ms).
    constexpr int cadaMs = 1000, topeMs = 1500, fallasMax = 3; // frenado ~7 s y se relanza
    auto proximoSondeo = std::chrono::steady_clock::now() + std::chrono::milliseconds(cadaMs * 2);
    int fallas = 0;
    while (!stop_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
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
            if (fallas == 0) backoffMs = 500;
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
        log_.warn(llenar(traducir("gateway", "ba-web: se cerró; se relanza en %1 ms"), {std::to_string(backoffMs)}), -1);
        for (int w = 0; w < backoffMs && !stop_.load(); w += 100) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stop_.load()) break;
        {
            std::lock_guard<std::mutex> l(procLock_);
            proc_ = ba::platform::Process();
        }
        if (spawn()) restarts_.fetch_add(1);
        if (backoffMs < 8000) backoffMs *= 2;
        proximoSondeo = std::chrono::steady_clock::now() + std::chrono::milliseconds(cadaMs * 2);
    }
}

void WebSupervisor::setEventoDir(const std::string& dir) {
    std::lock_guard<std::mutex> l(dirLock_);
    if (dir == dirActual_) return;
    if (ba::persist::escribir(aliasPath_, dir.empty() ? std::string() : dir + "\n")) dirActual_ = dir;
}

} // namespace ba::gateway
