// ============================================================================
// src/gateway/Pinggy.cpp — implementacion del supervisor de ba-pinggy
//
// Proposito:
//   Implementa PinggySupervisor (ver el header), igual en Windows, Linux y
//   macOS, sobre ba::platform::Process. El estado sale de la salida de
//   ba-pinggy (una línea JSON por evento) que queda en Logs/pinggy.log: se lee
//   desde el tamaño que tenía el log al lanzar, así no se confunden los
//   eventos de una corrida anterior con los de ésta.
//
// Modelo de threading:
//   - runLoop corre en su thread y se detiene por stop_.
//   - El proceso solo lo tocan spawn/kill (procLock_); el estado, aplicarLinea
//     bajo estadoLock_.
//
// Estrategia de errores:
//   No lanza. Sin binario o con un forwarding prohibido no se lanza nada (se
//   avisa una vez). Un error no recuperable de ba-pinggy corta los reintentos.
// ============================================================================

#include "Pinggy.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Log.hpp"
#include "ajustes/Ajustes.hpp"

namespace ba::gateway {

namespace {

using json = nlohmann::json;

/// "tcp://host:puerto/" -> "host:puerto" (el público de un forwarding).
std::string sinEsquema(std::string s) {
    if (const auto p = s.find("://"); p != std::string::npos) s = s.substr(p + 3);
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

/// El puerto de "127.0.0.1:3478" (0 si no se entiende).
uint16_t puertoDe(const std::string& hostPuerto) {
    const auto p = hostPuerto.rfind(':');
    if (p == std::string::npos) return 0;
    try {
        const int v = std::stoi(hostPuerto.substr(p + 1));
        return (v > 0 && v < 65536) ? static_cast<uint16_t>(v) : 0;
    } catch (...) {
        return 0;
    }
}

/// El binding (puerto público reservado, Pro) de un forwarding TCP/UDP. Va tal
/// cual al binding_url de libpinggy, que acepta ":puerto" (en TCP/UDP el nombre
/// se ignora).
std::string binding(uint16_t puertoPublico) {
    return puertoPublico > 0 ? "@:" + std::to_string(puertoPublico) : std::string();
}

std::string texto(const json& j, const char* clave) {
    const auto it = j.find(clave);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::vector<std::string> listaDeTextos(const json& j, const char* clave) {
    std::vector<std::string> out;
    const auto it = j.find(clave);
    if (it == j.end() || !it->is_array()) return out;
    for (const auto& v : *it)
        if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

/// La URL web entre las que informó pinggy: https antes que http.
std::string urlWeb(const std::vector<std::string>& urls) {
    for (const auto& u : urls)
        if (u.rfind("https://", 0) == 0) return u;
    for (const auto& u : urls)
        if (u.rfind("http://", 0) == 0) return u;
    return {};
}

} // namespace

PinggySupervisor::PinggySupervisor(Cfg cfg, Log& log) : cfg_(std::move(cfg)), log_(log) {}

PinggySupervisor::~PinggySupervisor() { stop(); }

// ============================================================================
// Piezas puras
// ============================================================================

std::vector<std::string> PinggySupervisor::argumentos(const Cfg& cfg, std::string* error) {
    const auto prohibido = [&](uint16_t p) { return p == 0 || cfg.prohibidos.count(p) > 0; };
    if (prohibido(cfg.puertoTunel) || (cfg.turn && prohibido(cfg.turnLocal))) {
        if (error != nullptr)
            *error = "forwarding a un puerto prohibido (" +
                     std::to_string(prohibido(cfg.puertoTunel) ? cfg.puertoTunel : cfg.turnLocal) +
                     "): la API de control, el MCP y la señalización común nunca van por el túnel";
        return {};
    }
    const std::string local = "127.0.0.1:";
    std::vector<std::string> a{"--forward", "http=" + local + std::to_string(cfg.puertoTunel)};
    if (cfg.turn) {
        const std::string turn = local + std::to_string(cfg.turnLocal);
        // TCP (turn:host:puerto?transport=tcp) y, si se pide, UDP. TLSTCP no: en
        // el plan gratis «Forwarding type tlstcp not allowed» tira abajo toda la
        // sesión (probado con ba-pinggy), y en Pro chocaría con el http en el 443
        // del mismo dominio. El medio igual va cifrado (DTLS-SRTP de punta a punta)
        // y la credencial del TURN es efímera.
        a.insert(a.end(), {"--forward", "tcp=" + turn + binding(cfg.turnTcpPuerto)});
        if (cfg.turnUdp) a.insert(a.end(), {"--forward", "udp=" + turn + binding(cfg.turnUdpPuerto)});
    }
    const std::string region = cfg.region.empty() ? std::string("auto") : cfg.region;
    a.insert(a.end(), {"--region", region});
    if (!cfg.host.empty()) a.insert(a.end(), {"--host", cfg.host});
    if (!cfg.listaBlanca.empty()) a.insert(a.end(), {"--listablanca", cfg.listaBlanca});
    a.insert(a.end(), {"--haproxy", cfg.haproxy.empty() ? std::string("no") : cfg.haproxy});
    // X-Forwarded-For en el túnel HTTP: la IP real cuando no va PROXY v1 (con el
    // TURN prendido). La escucha del túnel toma el último de la lista.
    a.push_back("--xff");
    a.push_back("--reconectar");
    return a;
}

bool PinggySupervisor::aplicarLinea(const std::string& linea, Estado& e, uint16_t turnLocal) {
    const auto inicio = linea.find('{');
    if (inicio == std::string::npos) return false;
    const json j = json::parse(linea.begin() + static_cast<std::ptrdiff_t>(inicio), linea.end(), nullptr, false);
    if (!j.is_object()) return false;
    const std::string ev = texto(j, "ev");
    if (ev == "conectando") {
        e.estado = "conectando";
        e.error.clear();
        return true;
    }
    if (ev == "listo" || ev == "reconectado") {
        e.estado = "listo";
        e.error.clear();
        if (auto urls = listaDeTextos(j, "urls"); !urls.empty()) e.urls = std::move(urls);
        if (const auto f = j.find("forwardings"); f != j.end() && f->is_array()) {
            e.urlHttp.clear();
            e.turnPublico.clear();
            e.turnTipo.clear();
            e.turnUdp.clear();
            for (const auto& fw : *f) {
                if (!fw.is_object()) continue;
                const std::string tipo = texto(fw, "tipo");
                const std::string publico = texto(fw, "publico");
                if (publico.empty()) continue;
                const bool esTurn = puertoDe(texto(fw, "local")) == turnLocal;
                if (tipo == "http" || tipo == "https") {
                    if (e.urlHttp.empty() || publico.rfind("https://", 0) == 0) e.urlHttp = publico;
                } else if (esTurn && tipo == "tlstcp") {
                    e.turnPublico = sinEsquema(publico); // el preferido: atraviesa firewalls
                    e.turnTipo = "tlstcp";
                } else if (esTurn && tipo == "tcp" && e.turnTipo != "tlstcp") {
                    e.turnPublico = sinEsquema(publico);
                    e.turnTipo = "tcp";
                } else if (esTurn && tipo == "udp") {
                    e.turnUdp = sinEsquema(publico);
                }
            }
        }
        if (e.urlHttp.empty()) e.urlHttp = urlWeb(e.urls);
        return true;
    }
    if (ev == "reconectando") {
        e.estado = "reconectando";
        return true;
    }
    if (ev == "error") {
        e.estado = "error";
        e.error = texto(j, "error");
        if (e.error.empty()) e.error = "error sin detalle";
        const auto r = j.find("recuperable");
        e.fatal = r != j.end() && r->is_boolean() && !r->get<bool>();
        return true;
    }
    if (ev == "aviso") {
        e.aviso = texto(j, "texto");
        return true;
    }
    return false;
}

std::string PinggySupervisor::urlPublicaDe(const Estado& e, const std::string& host) {
    // Reconectando: la URL de antes vuelve (subdominio o dominio fijo del token),
    // mejor seguir mostrándola que dejar el QR en blanco unos segundos.
    if (e.estado != "listo" && e.estado != "reconectando") return {};
    std::string u = !host.empty() ? "https://" + host : e.urlHttp;
    if (u.empty()) return {};
    if (u.rfind("http://", 0) == 0) u = "https://" + u.substr(7); // pinggy termina TLS: siempre hay https
    if (u.find("://") == std::string::npos) u = "https://" + u;
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

json PinggySupervisor::estadoJsonDe(bool activo, const Estado& e, const std::string& host) {
    json j{{"activo", activo},
           {"estado", e.estado},
           {"urls", e.urls},
           {"urlPublica", urlPublicaDe(e, host)},
           {"error", e.error},
           {"turn", {{"publico", e.turnPublico}, {"tipo", e.turnTipo}, {"udp", e.turnUdp}}}};
    if (!e.aviso.empty()) j["aviso"] = e.aviso;
    return j;
}

// ============================================================================
// Estado
// ============================================================================

PinggySupervisor::Estado PinggySupervisor::estado() const {
    std::lock_guard<std::mutex> l(estadoLock_);
    return estado_;
}

std::string PinggySupervisor::urlPublica() const {
    std::lock_guard<std::mutex> l(estadoLock_);
    return urlPublicaDe(estado_, cfg_.host);
}

std::string PinggySupervisor::turnPublico() const {
    std::lock_guard<std::mutex> l(estadoLock_);
    return (estado_.estado == "listo" || estado_.estado == "reconectando") ? estado_.turnPublico : std::string();
}

json PinggySupervisor::estadoJson() const {
    std::lock_guard<std::mutex> l(estadoLock_);
    return estadoJsonDe(started_.load(), estado_, cfg_.host);
}

void PinggySupervisor::setEstado(const std::string& estado, const std::string& error) {
    std::lock_guard<std::mutex> l(estadoLock_);
    estado_.estado = estado;
    estado_.error = error;
}

// ============================================================================
// Proceso
// ============================================================================

bool PinggySupervisor::start() {
    if (started_.load()) return alive_.load();
    std::string error;
    if (argumentos(cfg_, &error).empty()) {
        log_.error("pinggy: " + error + "; túnel desactivado", -1);
        setEstado("error", error);
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(cfg_.exe, ec)) {
        log_.warn("pinggy: no está el binario (" + cfg_.exe + "); sin túnel", -1);
        setEstado("error", "falta ba-pinggy en bin/64bit");
        return false;
    }
    if (cfg_.token.empty())
        log_.warn("pinggy: sin token (BA_PINGGY_TOKEN o Credenciales/pinggy-token): túnel anónimo, "
                  "60 minutos y URL al azar",
                  -1);
    stop_.store(false);
    started_.store(true);
    if (!spawn()) log_.warn("pinggy: no se pudo lanzar (se reintentará con backoff)", -1);
    thread_ = std::thread(&PinggySupervisor::runLoop, this);
    log_.info("pinggy: supervisor arrancado (http -> 127.0.0.1:" + std::to_string(cfg_.puertoTunel) +
              (cfg_.turn ? ", TURN -> 127.0.0.1:" + std::to_string(cfg_.turnLocal) : std::string()) + ")");
    return alive_.load();
}

void PinggySupervisor::stop() {
    if (!started_.load()) return; // idempotente (main y el destructor)
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    kill();
    started_.store(false);
    setEstado("apagado");
    log_.info("pinggy: supervisor detenido (reinicios=" + std::to_string(restartCount_.load()) + ")");
}

bool PinggySupervisor::spawn() {
    ba::platform::ProcessOptions o;
    o.exe = cfg_.exe;
    o.args = argumentos(cfg_);
    // El token SOLO por entorno: por línea de comandos se vería en `ps`.
    if (!cfg_.token.empty())
        o.envSet["BA_PINGGY_TOKEN"] = cfg_.token;
    else
        o.envRemove.push_back("BA_PINGGY_TOKEN");
    o.killWithParent = true;
    o.hideWindow = true;
    o.outputFile = cfg_.logPath;

    std::lock_guard<std::mutex> lock(procLock_);
    std::error_code ec;
    const auto size = std::filesystem::file_size(cfg_.logPath, ec);
    logOffset_ = ec ? 0 : size;
    pendiente_.clear();
    {
        std::lock_guard<std::mutex> l(estadoLock_);
        estado_ = Estado{};
        estado_.estado = "conectando";
    }
    std::string err;
    if (!ba::platform::Process::spawn(o, proc_, &err)) {
        log_.warn("pinggy: no se pudo lanzar: " + err, -1);
        setEstado("error", "no se pudo lanzar ba-pinggy: " + err);
        return false;
    }
    alive_.store(true);
    log_.info("pinggy: lanzado (pid=" + std::to_string(proc_.pid()) + ")");
    return true;
}

void PinggySupervisor::kill() {
    std::lock_guard<std::mutex> lock(procLock_);
    if (proc_.running()) {
        proc_.terminate(); // ba-pinggy cierra la sesión y sale con 0
        if (!proc_.wait(3000).has_value()) {
            proc_.kill();
            proc_.wait(3000);
        }
    }
    proc_ = ba::platform::Process();
    alive_.store(false);
}

bool PinggySupervisor::processAlive() {
    std::lock_guard<std::mutex> lock(procLock_);
    return proc_.running();
}

void PinggySupervisor::leerLog() {
    std::ifstream in(cfg_.logPath, std::ios::binary);
    if (!in) return;
    in.seekg(0, std::ios::end);
    const std::streamoff fin = in.tellg();
    if (fin < 0) return;
    auto desde = static_cast<std::streamoff>(logOffset_);
    if (desde > fin) desde = 0; // el log se rotó o se truncó
    if (fin == desde) return;
    in.seekg(desde);
    std::string nuevo(static_cast<size_t>(fin - desde), '\0');
    in.read(nuevo.data(), static_cast<std::streamsize>(nuevo.size()));
    nuevo.resize(static_cast<size_t>(in.gcount()));
    logOffset_ = static_cast<std::uintmax_t>(desde) + nuevo.size();
    pendiente_ += nuevo;
    if (pendiente_.size() > 65536) pendiente_.erase(0, pendiente_.size() - 65536);

    std::size_t pos = 0;
    for (std::size_t nl; (nl = pendiente_.find('\n', pos)) != std::string::npos; pos = nl + 1) {
        std::string linea = pendiente_.substr(pos, nl - pos);
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        Estado antes;
        Estado despues;
        {
            std::lock_guard<std::mutex> l(estadoLock_);
            antes = estado_;
            if (!aplicarLinea(linea, estado_, cfg_.turnLocal)) continue;
            despues = estado_;
        }
        // Al registro solo lo que cambia (nunca el token: ba-pinggy no lo escribe).
        if (despues.estado == "listo" && (antes.estado != "listo" || antes.urls != despues.urls)) {
            std::string u;
            for (const auto& x : despues.urls) u += (u.empty() ? "" : " ") + x;
            log_.info("pinggy: túnel listo: " + u +
                      (despues.turnPublico.empty() ? std::string()
                                                   : " (TURN " + despues.turnTipo + " " + despues.turnPublico + ")"));
            backoffMs_ = 1000;
        } else if (despues.estado == "reconectando" && antes.estado != "reconectando") {
            log_.warn("pinggy: reconectando", -1);
        } else if (despues.estado == "error" && despues.error != antes.error) {
            log_.warn("pinggy: " + despues.error + (despues.fatal ? " (no se reintenta)" : ""), -1);
        } else if (despues.aviso != antes.aviso) {
            log_.info("pinggy: " + despues.aviso);
        }
    }
    pendiente_.erase(0, pos);
}

void PinggySupervisor::sleepBackoff() {
    for (int waited = 0; waited < backoffMs_ && !stop_.load(); waited += 100)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (backoffMs_ < ba::ajustes::sAMs("gateway.tunelReintentoMaxS", 30)) backoffMs_ *= 2;
}

void PinggySupervisor::runLoop() {
    while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (stop_.load(std::memory_order_relaxed)) break;
        leerLog();
        const bool fatal = estado().fatal;
        if (fatal) {
            // Token inválido o parecido: reintentar no lo arregla.
            log_.error("pinggy: error no recuperable; túnel desactivado hasta corregirlo y reiniciar el gateway",
                       -1);
            kill();
            break;
        }
        if (processAlive()) continue;

        leerLog(); // lo último que dijo antes de morir
        if (estado().fatal) {
            log_.error("pinggy: error no recuperable; túnel desactivado hasta corregirlo y reiniciar el gateway",
                       -1);
            kill();
            break;
        }
        kill();
        setEstado("reconectando", "ba-pinggy se cerró; reintento en " + std::to_string(backoffMs_ / 1000) + " s");
        log_.warn("pinggy: ba-pinggy se cerró; reiniciando con backoff", -1);
        sleepBackoff();
        if (stop_.load(std::memory_order_relaxed)) break;
        if (spawn()) ++restartCount_;
    }
}

} // namespace ba::gateway
