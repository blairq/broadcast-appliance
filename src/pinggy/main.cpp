// ============================================================================
// src/pinggy/main.cpp — ba-pinggy: el túnel de pinggy.io en un proceso propio
//
// Lo lanza y supervisa el gateway (como a cloudflared). Va aparte para que el
// OpenSSL de libpinggy no conviva con el BoringSSL de libwebrtc, y para que una
// caída del túnel no se lleve puesto al gateway. Ver docs/funciones/PINGGY.md.
//
// Contrato (lo usan otros, no cambiarlo a la ligera):
//   * Token por la variable BA_PINGGY_TOKEN (vacía = túnel gratis/anónimo).
//     NUNCA por la línea de comandos: ahí lo ve cualquiera con `ps`.
//   * --forward <tipo>=<host:puerto>[@<binding>]  (repetible; tipo: http, tcp,
//     tls, tlstcp, udp), --region auto|us|eu|ap, --host <dominio>,
//     --listablanca <ip,...>, --haproxy v1|v2|no, --xff, --reconectar.
//   * Stdout: un objeto JSON por línea ({"ev":"conectando"}, "listo",
//     "reconectando", "reconectado", "error", "uso", "aviso"). Stderr: texto libre.
//   * SIGTERM/SIGINT: corta prolijo y sale con 0. Error fatal: distinto de 0.
//   * Nunca reenvía la API de control (8788) ni el MCP (8790).
// ============================================================================
#include <pinggy.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

// --- Salida -------------------------------------------------------------------

std::string json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o + "\"";
}

std::string jsonLista(const std::vector<std::string>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + json(v[i]);
    return o + "]";
}

// Una línea por evento, entera y enseguida: el gateway lee línea por línea.
void emitir(const std::string& linea) {
    std::fwrite(linea.data(), 1, linea.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void evento(const char* ev) { emitir(std::string("{\"ev\":") + json(ev) + "}"); }
void aviso(const std::string& t) { emitir("{\"ev\":\"aviso\",\"texto\":" + json(t) + "}"); }
void error(const std::string& e, bool recuperable) {
    emitir("{\"ev\":\"error\",\"error\":" + json(e) + ",\"recuperable\":" + (recuperable ? "true" : "false") + "}");
}

// --- Opciones -----------------------------------------------------------------

struct Forward {
    std::string tipo, local, binding;
};

struct Opciones {
    std::vector<Forward> forwards;
    std::string region = "auto";
    std::string host;
    std::vector<std::string> listaBlanca;
    std::string haproxy; // "" = por defecto (v1 si hay tcp/tls/tlstcp), "no", "v1", "v2"
    bool xff = false;
    bool reconectar = false;
    bool probar = false; // --probar: ver si pinggy acepta la dirección y salir (sin desplazar otro túnel)
};

const char* const kUso =
    "uso: ba-pinggy --forward <tipo>=<host:puerto>[@<binding>] [...]\n"
    "               [--region auto|us|eu|ap] [--host <dominio>] [--listablanca <ip,...>]\n"
    "               [--haproxy v1|v2|no] [--xff] [--reconectar] [--probar]\n"
    "  tipo: http, tcp, tls, tlstcp, udp. El token va en BA_PINGGY_TOKEN (vacío = gratis).\n";

std::vector<std::string> partir(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(sep, i);
        if (j == std::string::npos) j = s.size();
        std::string p = s.substr(i, j - i);
        while (!p.empty() && p.front() == ' ') p.erase(p.begin());
        while (!p.empty() && p.back() == ' ') p.pop_back();
        if (!p.empty()) v.push_back(p);
        i = j + 1;
    }
    return v;
}

// host:puerto ([v6]:puerto también). Devuelve vacío si está bien, o el problema.
std::string validarLocal(const std::string& local) {
    if (local.find("://") != std::string::npos) return "sin esquema: solo host:puerto";
    size_t dp = local.rfind(':');
    if (dp == std::string::npos || dp == 0) return "falta host:puerto";
    std::string host = local.substr(0, dp), puerto = local.substr(dp + 1);
    if (host.empty() || puerto.empty() || puerto.size() > 5) return "host:puerto inválido";
    for (char c : puerto)
        if (c < '0' || c > '9') return "puerto inválido";
    int p = std::atoi(puerto.c_str());
    if (p < 1 || p > 65535) return "puerto fuera de rango";
    // La API de control y el MCP no salen NUNCA a internet, sea cual sea el host
    // con que se los nombre (127.0.0.1, localhost, ::1, 0.0.0.0, la IP de la LAN...).
    if (p == 8788 || p == 8790) return "el puerto " + puerto + " (API de control / MCP) no se publica nunca";
    return {};
}

bool leerOpciones(int argc, char** argv, Opciones& o, std::string& problema) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto valor = [&](std::string& v) {
            if (i + 1 >= argc) {
                problema = a + ": falta el valor";
                return false;
            }
            v = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--forward") {
            if (!valor(v)) return false;
            size_t eq = v.find('=');
            if (eq == std::string::npos) {
                problema = "--forward " + v + ": falta <tipo>=";
                return false;
            }
            Forward f;
            f.tipo = v.substr(0, eq);
            std::string resto = v.substr(eq + 1);
            size_t at = resto.find('@');
            f.local = resto.substr(0, at);
            if (at != std::string::npos) f.binding = resto.substr(at + 1);
            if (f.tipo != "http" && f.tipo != "tcp" && f.tipo != "tls" && f.tipo != "tlstcp" && f.tipo != "udp") {
                problema = "--forward " + v + ": tipo desconocido (http, tcp, tls, tlstcp, udp)";
                return false;
            }
            std::string mal = validarLocal(f.local);
            if (!mal.empty()) {
                problema = "--forward " + v + ": " + mal;
                return false;
            }
            o.forwards.push_back(f);
        } else if (a == "--region") {
            if (!valor(o.region)) return false;
            if (o.region != "auto" && o.region != "us" && o.region != "eu" && o.region != "ap") {
                problema = "--region " + o.region + ": auto, us, eu o ap";
                return false;
            }
        } else if (a == "--host") {
            if (!valor(o.host)) return false;
        } else if (a == "--listablanca") {
            if (!valor(v)) return false;
            o.listaBlanca = partir(v, ',');
        } else if (a == "--haproxy") {
            if (!valor(o.haproxy)) return false;
            if (o.haproxy != "v1" && o.haproxy != "v2" && o.haproxy != "no") {
                problema = "--haproxy " + o.haproxy + ": v1, v2 o no";
                return false;
            }
        } else if (a == "--xff") {
            o.xff = true;
        } else if (a == "--reconectar") {
            o.reconectar = true;
        } else if (a == "--probar") {
            o.probar = true;
        } else if (a == "--token" || a.rfind("--token=", 0) == 0) {
            problema = "el token no va por la línea de comandos: BA_PINGGY_TOKEN";
            return false;
        } else if (a == "-h" || a == "--help" || a == "--ayuda") {
            std::fputs(kUso, stderr);
            std::exit(0);
        } else {
            problema = "opción desconocida: " + a;
            return false;
        }
    }
    if (o.forwards.empty()) {
        problema = "falta al menos un --forward";
        return false;
    }
    return true;
}

// --- Estado del túnel (los callbacks corren en este mismo hilo, en resume) -----

std::atomic<bool> gParar{false};

// Lo que dice el mapa de forwardings de pinggy de cada uno: a dónde va y sus URLs.
struct Publicado {
    std::string modo, local;
    std::vector<std::string> urls;
    bool usado = false;
};

struct Estado {
    const Opciones* op = nullptr;
    bool listo = false;       // ya avisó "listo" alguna vez en esta sesión
    bool terminado = false;   // la sesión se cayó (falló o se agotaron los reintentos)
    bool fatal = false;       // y no tiene sentido reintentar
    std::string ultimoError;
    std::chrono::steady_clock::time_point ultimoUso{};
    std::string ultimasVivas; // «"numLiveConnections":N» de la última línea de uso anotada
    // "listo"/"reconectado" esperan un momento al mapa de forwardings (llega justo
    // después del aviso de establecido) para decir qué URL es de cada forwarding.
    const char* pendiente = nullptr; // "listo" o "reconectado"
    std::vector<std::string> urlsPendientes;
    std::chrono::steady_clock::time_point pendienteDesde{};
    std::vector<Publicado> mapa;
    bool hayMapa = false;
    pinggy_ref_t tunel = INVALID_PINGGY_REF;
};

bool empiezaCon(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// Un lector mínimo para el mapa de pinggy, que tiene esta forma (pinggy dice que
// «no está cerrada»; si cambia, se cae a repartir las URLs por esquema):
//   [{"forwardTo":"127.0.0.1:8786","mode":"http","remoteBindings":["https://..."]}]
std::string leerCadena(const std::string& s, size_t& i) {
    std::string o;
    if (i >= s.size() || s[i] != '"') return o;
    for (++i; i < s.size() && s[i] != '"'; ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) ++i;
        o += s[i];
    }
    ++i;
    return o;
}

std::vector<Publicado> leerMapa(const std::string& s) {
    std::vector<Publicado> v;
    size_t i = 0;
    while ((i = s.find('{', i)) != std::string::npos) {
        size_t fin = s.find('}', i);
        if (fin == std::string::npos) break;
        std::string obj = s.substr(i, fin - i + 1);
        Publicado p;
        auto campo = [&](const char* k) -> size_t {
            size_t j = obj.find(std::string("\"") + k + "\"");
            if (j == std::string::npos) return j;
            j = obj.find(':', j);
            if (j == std::string::npos) return j;
            ++j;
            while (j < obj.size() && obj[j] == ' ') ++j;
            return j;
        };
        size_t j = campo("forwardTo");
        if (j != std::string::npos) p.local = leerCadena(obj, j);
        j = campo("mode");
        if (j != std::string::npos) p.modo = leerCadena(obj, j);
        j = campo("remoteBindings");
        if (j != std::string::npos && j < obj.size() && obj[j] == '[') {
            ++j;
            while (j < obj.size() && obj[j] != ']') {
                if (obj[j] == '"') p.urls.push_back(leerCadena(obj, j));
                else ++j;
            }
        }
        if (!p.local.empty()) v.push_back(p);
        i = fin + 1;
    }
    return v;
}

// El forward_to de pinggy puede venir con esquema (http://127.0.0.1:8786) o sin.
std::string sinEsquema(const std::string& s) {
    size_t k = s.find("://");
    return k == std::string::npos ? s : s.substr(k + 3);
}

// A qué forwarding corresponde cada URL pública. Con el mapa de pinggy, por
// destino local y tipo; sin él, repartiendo la lista de URLs por esquema y en orden.
std::string forwardingsJson(const Opciones& op, const std::vector<std::string>& urls, std::vector<Publicado> mapa) {
    std::vector<bool> usada(urls.size(), false);
    std::string o = "[";
    for (size_t i = 0; i < op.forwards.size(); ++i) {
        const Forward& f = op.forwards[i];
        std::vector<std::string> suyas;
        for (auto& p : mapa)
            if (!p.usado && sinEsquema(p.local) == f.local && (p.modo.empty() || p.modo == f.tipo)) {
                p.usado = true;
                suyas = p.urls;
                break;
            }
        if (suyas.empty()) {
            auto tomar = [&](const std::string& esquema) {
                for (size_t k = 0; k < urls.size(); ++k)
                    if (!usada[k] && empiezaCon(urls[k], esquema.c_str())) {
                        usada[k] = true;
                        suyas.push_back(urls[k]);
                        return;
                    }
            };
            if (f.tipo == "http") {
                tomar("https://");
                if (suyas.empty()) tomar("http://");
            } else {
                tomar(f.tipo + "://");
            }
        }
        // El público principal: la https si hay.
        std::string publico;
        for (auto& u : suyas)
            if (empiezaCon(u, "https://")) {
                publico = u;
                break;
            }
        if (publico.empty() && !suyas.empty()) publico = suyas.front();
        o += std::string(i ? "," : "") + "{\"tipo\":" + json(f.tipo) + ",\"local\":" + json(f.local) +
             ",\"publico\":" + (publico.empty() ? std::string("null") : json(publico)) +
             ",\"publicos\":" + jsonLista(suyas) + "}";
    }
    return o + "]";
}

std::vector<std::string> aLista(pinggy_len_t n, pinggy_char_p_p_t urls) {
    std::vector<std::string> v;
    for (int i = 0; i < n; ++i)
        if (urls && urls[i]) v.emplace_back(urls[i]);
    return v;
}

std::string mensajes(pinggy_len_t n, pinggy_char_p_p_t m) {
    std::string s;
    for (int i = 0; i < n; ++i)
        if (m && m[i]) s += (s.empty() ? "" : "; ") + std::string(m[i]);
    return s;
}

bool gConToken = false;

// Los mensajes de bienvenida del servidor (p. ej. «el túnel gratis dura 60
// minutos»): salen como aviso, es lo único que dice pinggy de los límites.
std::string saludo(pinggy_ref_t tunel) {
    char buf[8192] = {};
    pinggy_capa_t largo = 0;
    int n = pinggy_tunnel_get_greeting_msgs_len(tunel, sizeof buf, buf, &largo);
    return n > 0 ? std::string(buf) : std::string();
}

// Emite el "listo"/"reconectado" pendiente (con el mapa si llegó, o sin él).
void soltarPendiente(Estado& e) {
    if (!e.pendiente) return;
    const char* ev = e.pendiente;
    e.pendiente = nullptr;
    std::string g = e.tunel != INVALID_PINGGY_REF ? saludo(e.tunel) : std::string();
    // Con un token que pinggy no reconoce NO falla: levanta un túnel gratis
    // (60 minutos, URL al azar con la IP pública adentro) y solo lo dice en el
    // saludo. Eso no es lo que pidió el operador: no se anuncia, se corta y se avisa.
    if (gConToken && g.find("not authenticated") != std::string::npos) {
        e.ultimoError = "pinggy no aceptó el token (quedaba un túnel gratis y anónimo): revisar el token";
        e.terminado = true;
        e.fatal = true;
        error(e.ultimoError, false);
        return;
    }
    emitir(std::string("{\"ev\":") + json(ev) + ",\"urls\":" + jsonLista(e.urlsPendientes) +
           ",\"forwardings\":" + forwardingsJson(*e.op, e.urlsPendientes, e.mapa) + "}");
    if (!g.empty() && g != "[]" && g != "null" && g != "\"\"") aviso("pinggy: " + g);
    // Probando: ya se sabe que pinggy aceptó la dirección; se corta.
    if (e.op->probar) gParar = true;
}

void dejarPendiente(Estado& e, const char* ev, std::vector<std::string> urls) {
    soltarPendiente(e);
    e.pendiente = ev;
    e.urlsPendientes = std::move(urls);
    e.pendienteDesde = std::chrono::steady_clock::now();
    // El mapa a veces llega ANTES que el aviso de establecido: entonces ya está.
    if (e.hayMapa) soltarPendiente(e);
}

void cbEstablecido(pinggy_void_p_t ud, pinggy_ref_t, pinggy_len_t n, pinggy_char_p_p_t urls) {
    auto* e = static_cast<Estado*>(ud);
    e->listo = true;
    dejarPendiente(*e, "listo", aLista(n, urls));
}

void cbFallo(pinggy_void_p_t ud, pinggy_ref_t, pinggy_const_char_p_t msg) {
    auto* e = static_cast<Estado*>(ud);
    e->ultimoError = msg ? msg : "falló el túnel";
    e->terminado = true;
    error(e->ultimoError, e->op->reconectar);
}

void cbDesconectado(pinggy_void_p_t ud, pinggy_ref_t, pinggy_const_char_p_t err, pinggy_len_t n, pinggy_char_p_p_t m) {
    auto* e = static_cast<Estado*>(ud);
    std::string s = err ? err : "";
    std::string extra = mensajes(n, m);
    if (!extra.empty() && extra != s) s += (s.empty() ? "" : ": ") + extra;
    if (s.empty()) s = "desconectado";
    if (gParar || e->terminado) return; // lo cortamos nosotros, o ya se avisó el error
    e->ultimoError = s;
    std::fprintf(stderr, "ba-pinggy: desconectado: %s\n", s.c_str());
    // Sin reconexión automática la sesión termina acá.
    if (!e->op->reconectar) {
        e->terminado = true;
        error(s, false);
    }
}

void cbVaAReconectar(pinggy_void_p_t ud, pinggy_ref_t, pinggy_const_char_p_t err, pinggy_len_t n, pinggy_char_p_p_t m) {
    auto* e = static_cast<Estado*>(ud);
    std::string s = err ? err : "";
    std::string extra = mensajes(n, m);
    if (!extra.empty() && extra != s) s += (s.empty() ? "" : ": ") + extra;
    std::fprintf(stderr, "ba-pinggy: se cortó, reconectando (%s)\n", s.c_str());
    e->hayMapa = false; // el de la sesión anterior ya no vale
    e->mapa.clear();
    evento("reconectando");
}

void cbReintento(pinggy_void_p_t, pinggy_ref_t, pinggy_uint16_t intento) {
    std::fprintf(stderr, "ba-pinggy: reintento %u\n", static_cast<unsigned>(intento));
}

void cbReconectado(pinggy_void_p_t ud, pinggy_ref_t, pinggy_len_t n, pinggy_char_p_p_t urls) {
    auto* e = static_cast<Estado*>(ud);
    // Con reconexión automática, libpinggy avisa la PRIMERA conexión por acá
    // (no por «establecido»): esa es "listo", las siguientes "reconectado".
    const char* ev = e->listo ? "reconectado" : "listo";
    e->listo = true;
    dejarPendiente(*e, ev, aLista(n, urls));
}

void cbReconexionFallo(pinggy_void_p_t ud, pinggy_ref_t, pinggy_uint16_t intentos) {
    auto* e = static_cast<Estado*>(ud);
    e->terminado = true;
    e->ultimoError = "no pudo reconectar después de " + std::to_string(intentos) + " intentos";
    error(e->ultimoError, e->op->reconectar);
}

void cbErrorTunel(pinggy_void_p_t ud, pinggy_ref_t, pinggy_uint32_t, pinggy_const_char_p_t err, pinggy_bool_t recuperable) {
    auto* e = static_cast<Estado*>(ud);
    std::string s = err ? err : "error del túnel";
    if (!recuperable) {
        e->ultimoError = s;
        e->terminado = true;
        if (!e->op->reconectar) e->fatal = true;
    }
    error(s, recuperable != 0 || e->op->reconectar);
}

void cbUso(pinggy_void_p_t ud, pinggy_ref_t, pinggy_const_char_p_t uso) {
    auto* e = static_cast<Estado*>(ud);
    // Al log solo si cambió la cantidad de conexiones vivas (como mucho uno cada 5 s) o, si
    // no, uno por minuto: antes era uno cada 5 s siempre (~4 MB por día con la caja prendida).
    std::string u = uso ? uso : "";
    std::string vivas;
    if (const auto p = u.find("\"numLiveConnections\":"); p != std::string::npos)
        vivas = u.substr(p, u.find_first_of(",}", p) - p);
    auto ahora = std::chrono::steady_clock::now();
    const auto desde = ahora - e->ultimoUso;
    const bool primera = !e->ultimoUso.time_since_epoch().count();
    if (!primera && desde < std::chrono::seconds(5)) return;
    if (!primera && vivas == e->ultimasVivas && desde < std::chrono::seconds(60)) return;
    e->ultimoUso = ahora;
    e->ultimasVivas = vivas;
    if (u.size() > 2 && u.front() == '{' && u.back() == '}')
        emitir("{\"ev\":\"uso\"," + u.substr(1));
}

void cbForwardingExtra(pinggy_void_p_t, pinggy_ref_t, pinggy_const_char_p_t bind, pinggy_const_char_p_t local,
                       pinggy_const_char_p_t tipo) {
    std::fprintf(stderr, "ba-pinggy: forwarding adicional %s %s -> %s\n", tipo ? tipo : "", bind ? bind : "",
                 local ? local : "");
}

void cbForwardingExtraFallo(pinggy_void_p_t, pinggy_ref_t, pinggy_const_char_p_t bind, pinggy_const_char_p_t local,
                            pinggy_const_char_p_t tipo, pinggy_const_char_p_t err) {
    aviso(std::string("forwarding ") + (tipo ? tipo : "") + " " + (bind ? bind : "") + " -> " + (local ? local : "") +
          " rechazado: " + (err ? err : "?"));
}

void cbMapa(pinggy_void_p_t ud, pinggy_ref_t, pinggy_const_char_p_t mapa) {
    auto* e = static_cast<Estado*>(ud);
    std::string m = mapa ? mapa : "";
    std::fprintf(stderr, "ba-pinggy: mapa de forwardings: %s\n", m.c_str());
    e->mapa = leerMapa(m);
    // Llega de a uno (primero el http, después el tcp...): se espera a que estén todos.
    e->hayMapa = e->mapa.size() >= e->op->forwards.size();
    if (e->hayMapa) soltarPendiente(*e);
}

void cbExcepcion(pinggy_const_char_p_t donde, pinggy_const_char_p_t que) {
    std::fprintf(stderr, "ba-pinggy: excepción en libpinggy: %s %s\n", donde ? donde : "", que ? que : "");
}

#ifdef _WIN32
BOOL WINAPI alCtrl(DWORD) {
    gParar = true;
    return TRUE;
}
#else
void alSenal(int) { gParar = true; }
#endif

// El padre se murió sin avisar (macOS no tiene PDEATHSIG): el túnel no puede
// quedar abierto solo, apuntando a una máquina sin nadie que lo supervise.
bool padreMurio([[maybe_unused]] long padre) {
#ifdef _WIN32
    return false; // en Windows lo cubre el Job Object del supervisor
#else
    return padre > 1 && static_cast<long>(getppid()) != padre;
#endif
}

std::string servidor(const std::string& region) {
    if (region == "us" || region == "eu" || region == "ap") return region + ".a.pinggy.io";
    return "a.pinggy.io";
}

#ifdef _WIN32
using Socket = SOCKET;
void cerrar(Socket s) { closesocket(s); }
#else
using Socket = int;
void cerrar(Socket s) { close(s); }
#endif

// ¿Se puede abrir TCP a esta dirección en un tiempo razonable?
bool responde(const addrinfo* a, int ms) {
    Socket s = socket(a->ai_family, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return false;
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    if (s < 0) return false;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
    fd_set w, x;
    FD_ZERO(&w);
    FD_ZERO(&x);
    FD_SET(s, &w);
    FD_SET(s, &x);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    bool ok = select(static_cast<int>(s) + 1, nullptr, &w, &x, &tv) > 0 && FD_ISSET(s, &w);
    if (ok) {
        int err = 0;
        socklen_t largo = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &largo);
        ok = err == 0;
    }
    cerrar(s);
    return ok;
}

// libpinggy se conecta a la primera dirección que resuelve y, si no contesta,
// espera el timeout de TCP entero (más de un minuto) sin probar otra. Pasó con
// IPv6: SYN_SENT colgado. Así que se elige antes una IPv4 que conteste y se le da
// esa IP (el SNI sigue siendo a.pinggy.io, el nombre del certificado). Si
// ninguna IPv4 contesta, se le deja el nombre (y que pruebe él, por IPv6).
std::string direccionServidor(const std::string& host) {
    addrinfo pista{};
    pista.ai_family = AF_INET;
    pista.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string elegida;
    if (getaddrinfo(host.c_str(), "443", &pista, &res) == 0) {
        for (addrinfo* a = res; a && elegida.empty() && !gParar; a = a->ai_next) {
            char ip[INET_ADDRSTRLEN] = {};
            auto* sin = reinterpret_cast<sockaddr_in*>(a->ai_addr);
            if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof ip)) continue;
            if (responde(a, 4000)) elegida = std::string(ip) + ":443";
            else std::fprintf(stderr, "ba-pinggy: %s (%s) no contesta\n", ip, host.c_str());
        }
        freeaddrinfo(res);
    }
    return elegida.empty() ? host + ":443" : elegida;
}

// Una sesión del túnel: de conectar hasta que se cae del todo o nos piden parar.
// Devuelve true si terminó porque nos pidieron parar.
bool sesion(const Opciones& op, const std::string& token, long padre, Estado& e) {
    pinggy_ref_t cfg = pinggy_create_config();
    if (cfg == INVALID_PINGGY_REF) {
        e.ultimoError = "no se pudo crear la configuración de libpinggy";
        e.fatal = true;
        error(e.ultimoError, false);
        return false;
    }
    std::string srv = servidor(op.region);
    std::string dir = direccionServidor(srv);
    // El SNI es SIEMPRE a.pinggy.io, también en us./eu./ap.: el certificado de
    // todos sus servidores es de a.pinggy.io y con otro SNI cortan la conexión.
    char sni[] = "a.pinggy.io";
    std::fprintf(stderr, "ba-pinggy: conectando a %s (%s, SNI %s)\n", dir.c_str(), srv.c_str(), sni);
    pinggy_config_set_server_address(cfg, dir.data());
    pinggy_config_set_sni_server_name(cfg, sni);
    if (!token.empty()) {
        std::string t = token;
        pinggy_config_set_token(cfg, t.data());
    }
    // Un túnel por token: si quedó colgada una sesión vieja (el gateway se
    // reinició), esta la desplaza en vez de fallar.
    // Probando una dirección NO: si otro túnel la usa, que pinggy lo diga en vez de tirarlo.
    pinggy_config_set_force(cfg, op.probar ? pinggy_false : pinggy_true);

    bool primeroHttp = true;
    bool hayTcp = false;
    for (const Forward& f : op.forwards) {
        std::string tipo = f.tipo, local = f.local, binding = f.binding;
        if (tipo == "http" && primeroHttp) {
            if (binding.empty()) binding = op.host;
            primeroHttp = false;
        }
        if (tipo == "tcp" || tipo == "tls" || tipo == "tlstcp") hayTcp = true;
        pinggy_config_add_forwarding(cfg, tipo.data(), binding.data(), local.data());
    }
    if (!op.listaBlanca.empty()) {
        std::string lb = jsonLista(op.listaBlanca);
        pinggy_config_set_ip_white_list(cfg, lb.c_str());
    }
    std::string hp = op.haproxy.empty() ? (hayTcp ? "v1" : "") : (op.haproxy == "no" ? "" : op.haproxy);
    pinggy_config_set_haproxy(cfg, hp.c_str());
    if (op.xff) pinggy_config_set_x_forwarded_for(cfg, pinggy_true);
    if (op.reconectar) {
        pinggy_config_set_auto_reconnect(cfg, pinggy_true);
        pinggy_config_set_max_reconnect_attempts(cfg, 0); // 0 = sin límite
        pinggy_config_set_reconnect_interval(cfg, 5);
    }

    pinggy_ref_t t = pinggy_tunnel_initiate(cfg);
    if (t == INVALID_PINGGY_REF) {
        pinggy_free_ref(cfg);
        e.ultimoError = "no se pudo iniciar el túnel (¿configuración inválida?)";
        e.fatal = true;
        error(e.ultimoError, false);
        return false;
    }
    pinggy_tunnel_set_on_tunnel_established_callback(t, cbEstablecido, &e);
    pinggy_tunnel_set_on_tunnel_failed_callback(t, cbFallo, &e);
    pinggy_tunnel_set_on_disconnected_callback(t, cbDesconectado, &e);
    pinggy_tunnel_set_on_will_reconnect_callback(t, cbVaAReconectar, &e);
    pinggy_tunnel_set_on_reconnecting_callback(t, cbReintento, &e);
    pinggy_tunnel_set_on_reconnection_completed_callback(t, cbReconectado, &e);
    pinggy_tunnel_set_on_reconnection_failed_callback(t, cbReconexionFallo, &e);
    pinggy_tunnel_set_on_tunnel_error_callback(t, cbErrorTunel, &e);
    pinggy_tunnel_set_on_usage_update_callback(t, cbUso, &e);
    pinggy_tunnel_set_on_additional_forwarding_succeeded_callback(t, cbForwardingExtra, &e);
    pinggy_tunnel_set_on_additional_forwarding_failed_callback(t, cbForwardingExtraFallo, &e);
    pinggy_tunnel_set_on_forwardings_changed_callback(t, cbMapa, &e);

    e.tunel = t;
    evento("conectando");
    pinggy_tunnel_start_usage_update(t);
    bool sigue = pinggy_tunnel_start_non_blocking(t) != 0;
    while (sigue && !gParar && !e.terminado) {
        sigue = pinggy_tunnel_resume_timeout(t, 250) != 0;
        // Si el mapa no llegó en un segundo y medio, se avisa igual (repartiendo por esquema).
        if (e.pendiente && std::chrono::steady_clock::now() - e.pendienteDesde > std::chrono::milliseconds(1500))
            soltarPendiente(e);
        if (padreMurio(padre)) {
            std::fprintf(stderr, "ba-pinggy: el proceso padre ya no está; cierro el túnel\n");
            gParar = true;
        }
    }
    bool pedido = gParar.load();
    if (!sigue && !pedido && !e.terminado) {
        e.terminado = true;
        if (e.ultimoError.empty()) e.ultimoError = "el túnel terminó";
        error(e.ultimoError, op.reconectar);
    }
    pinggy_tunnel_stop(t);
    // Drena hasta que libpinggy termina de cerrar (con un tope: no colgarse al salir).
    auto limite = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (pinggy_tunnel_resume_timeout(t, 100) && std::chrono::steady_clock::now() < limite) {
    }
    pinggy_free_ref(t);
    pinggy_free_ref(cfg);
    return pedido;
}

#ifdef _WIN32
void ponerEntorno(const char* k, const std::string& v) {
    SetEnvironmentVariableA(k, v.c_str());
    _putenv_s(k, v.c_str());
}
#else
void ponerEntorno(const char* k, const std::string& v) { setenv(k, v.c_str(), 1); }
#endif

// El OpenSSL de libpinggy trae compilada una carpeta del sistema donde busca su
// openssl.cnf (y módulos). Portable: nada del sistema, así que se lo apunta a
// una carpeta propia que no tiene configuración (sin archivo, OpenSSL sigue con
// sus valores de fábrica). El certificado raíz de pinggy viene adentro de la biblioteca.
void aislarOpenSsl(const char* argv0) {
    std::string dir = argv0 ? argv0 : "";
    size_t b = dir.find_last_of("/\\");
    dir = (b == std::string::npos) ? std::string(".") : dir.substr(0, b);
    std::string propio = dir + "/pinggy";
    ponerEntorno("OPENSSL_CONF", propio + "/openssl.cnf");
    ponerEntorno("OPENSSL_MODULES", propio);
    ponerEntorno("OPENSSL_ENGINES", propio);
}

} // namespace

int main(int argc, char** argv) {
    Opciones op;
    std::string problema;
    if (!leerOpciones(argc, argv, op, problema)) {
        std::fprintf(stderr, "ba-pinggy: %s\n%s", problema.c_str(), kUso);
        error(problema, false);
        return 2;
    }

    // El token: de la variable, y se borra enseguida del entorno del proceso.
    std::string token;
    if (const char* t = std::getenv("BA_PINGGY_TOKEN")) token = t;
#ifdef _WIN32
    SetEnvironmentVariableA("BA_PINGGY_TOKEN", nullptr);
    _putenv_s("BA_PINGGY_TOKEN", "");
#else
    unsetenv("BA_PINGGY_TOKEN");
#endif
    gConToken = !token.empty();
    aislarOpenSsl(argv[0]);

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SetConsoleCtrlHandler(alCtrl, TRUE);
    long padre = 0;
#else
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, alSenal);
    std::signal(SIGINT, alSenal);
    long padre = static_cast<long>(getppid());
#endif

    // libpinggy escribe su log en stdout (y con el token adentro): se apaga,
    // stdout es solo para los eventos. Para diagnosticar: BA_PINGGY_LOG=<archivo>.
    if (const char* lg = std::getenv("BA_PINGGY_LOG"); lg && *lg) {
        std::string ruta = lg;
        pinggy_set_log_path(ruta.data());
        std::fprintf(stderr, "ba-pinggy: log de libpinggy en %s (puede tener el token: no compartirlo)\n", lg);
    } else {
        pinggy_set_log_enable(pinggy_false);
#ifdef _WIN32
        char nul[] = "NUL";
#else
        char nul[] = "/dev/null";
#endif
        pinggy_set_log_path(nul);
    }
    pinggy_set_on_exception_callback(cbExcepcion);
    {
        char v[64] = {};
        pinggy_version(sizeof v, v);
        std::fprintf(stderr, "ba-pinggy: libpinggy %s, servidor %s%s\n", v, servidor(op.region).c_str(),
                     token.empty() ? " (sin token: túnel gratis, 60 min)" : "");
    }

    // libpinggy tiene llamadas que bloquean (conectar, reconectar) y no se
    // enteran de la señal: si a los 4 s de pedir que pare sigue acá, se sale igual.
    std::thread([] {
        while (!gParar) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::this_thread::sleep_for(std::chrono::seconds(4));
        std::fflush(stdout);
        std::_Exit(0);
    }).detach();

    int espera = 5; // segundos entre sesiones, con --reconectar
    for (;;) {
        Estado e;
        e.op = &op;
        bool pedido = sesion(op, token, padre, e);
        if (pedido || gParar) return 0;
        if (e.fatal || !op.reconectar) return 1; // el error ya salió por stdout
        // Se cayó del todo (p. ej. sin red al arrancar): otra sesión desde cero,
        // con espera creciente hasta un minuto.
        if (e.listo) espera = 5;
        evento("reconectando");
        for (int i = 0; i < espera * 10 && !gParar; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (gParar) return 0;
        espera = std::min(espera * 2, 60);
    }
}
