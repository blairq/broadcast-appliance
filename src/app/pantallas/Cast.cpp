// ============================================================================
// src/app/pantallas/Cast.cpp — ver Cast.hpp
// ============================================================================
#include "pantallas/Cast.hpp"

#include <atomic>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

namespace ba::app::pantallas {
namespace {

using json = nlohmann::json;

constexpr const char* kNsConexion = "urn:x-cast:com.google.cast.tp.connection";
constexpr const char* kNsLatido = "urn:x-cast:com.google.cast.tp.heartbeat";
constexpr const char* kNsReceptor = "urn:x-cast:com.google.cast.receiver";
constexpr const char* kYo = "sender-0";
constexpr const char* kPlataforma = "receiver-0";
/// Tope de un mensaje. Un RECEIVER_STATUS ronda el kilobyte; cien mil es
/// holgado y evita que un aparato roto (o algo que no es un Chromecast) haga
/// pedir memoria a lo loco.
constexpr std::uint32_t kMaxMensaje = 100000;

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// --- protobuf a mano ---------------------------------------------------------
// Solo hacen falta dos tipos de campo: varint (los enums) y bytes (las cadenas).

void varint(std::string& out, std::uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

void campoVarint(std::string& out, int campo, std::uint64_t v) {
    varint(out, static_cast<std::uint64_t>(campo) << 3 | 0); // wire type 0
    varint(out, v);
}

void campoBytes(std::string& out, int campo, const std::string& v) {
    varint(out, static_cast<std::uint64_t>(campo) << 3 | 2); // wire type 2
    varint(out, v.size());
    out += v;
}

/// Lee un varint desde `p`; deja `p` después. false si se pasa del final.
bool leerVarint(const char*& p, const char* fin, std::uint64_t& v) {
    v = 0;
    int corr = 0;
    while (p < fin) {
        const auto b = static_cast<unsigned char>(*p++);
        v |= static_cast<std::uint64_t>(b & 0x7f) << corr;
        if ((b & 0x80) == 0) return true;
        corr += 7;
        if (corr > 63) return false;
    }
    return false;
}

/// Un CastMessage: campos 2 (origen), 3 (destino), 4 (tema) y 6 (texto). Lo
/// que no se entiende se saltea, que es lo que manda el formato.
bool leerCastMessage(const std::string& datos, Mensaje& out) {
    const char* p = datos.data();
    const char* fin = p + datos.size();
    while (p < fin) {
        std::uint64_t clave = 0;
        if (!leerVarint(p, fin, clave)) return false;
        const int campo = static_cast<int>(clave >> 3);
        const int tipo = static_cast<int>(clave & 7);
        if (tipo == 0) {
            std::uint64_t v = 0;
            if (!leerVarint(p, fin, v)) return false;
        } else if (tipo == 2) {
            std::uint64_t largo = 0;
            if (!leerVarint(p, fin, largo)) return false;
            if (static_cast<std::uint64_t>(fin - p) < largo) return false;
            std::string v(p, largo);
            p += largo;
            switch (campo) {
                case 2: out.origen = std::move(v); break;
                case 3: out.destino = std::move(v); break;
                case 4: out.tema = std::move(v); break;
                case 6: out.texto = std::move(v); break;
                default: break;
            }
        } else if (tipo == 5) {
            if (fin - p < 4) return false;
            p += 4;
        } else if (tipo == 1) {
            if (fin - p < 8) return false;
            p += 8;
        } else {
            return false; // grupos: no los usa Cast
        }
    }
    return true;
}

/// Esperar `ms` SIN dejar de atender el canal. Un `sleep` a secas no sirve:
/// el aparato manda PING cada pocos segundos y si no se le contesta corta la
/// conexión, justo en el medio de la espera que hace falta.
void esperarMs(Canal& c, int ms) {
    Mensaje sobra;
    c.esperar("nunca-llega", ms, sobra, nullptr);
}

} // namespace

std::string armar(const Mensaje& m) {
    std::string cuerpo;
    campoVarint(cuerpo, 1, 0); // protocol_version: CASTV2_1_0
    campoBytes(cuerpo, 2, m.origen);
    campoBytes(cuerpo, 3, m.destino);
    campoBytes(cuerpo, 4, m.tema);
    campoVarint(cuerpo, 5, 0); // payload_type: STRING
    campoBytes(cuerpo, 6, m.texto);

    std::string out;
    out.reserve(cuerpo.size() + 4);
    const auto n = static_cast<std::uint32_t>(cuerpo.size());
    out.push_back(static_cast<char>((n >> 24) & 0xff));
    out.push_back(static_cast<char>((n >> 16) & 0xff));
    out.push_back(static_cast<char>((n >> 8) & 0xff));
    out.push_back(static_cast<char>(n & 0xff));
    out += cuerpo;
    return out;
}

bool sacar(std::string& buffer, Mensaje& out) {
    if (buffer.size() < 4) return false;
    const auto* b = reinterpret_cast<const unsigned char*>(buffer.data());
    const std::uint32_t n = (static_cast<std::uint32_t>(b[0]) << 24) |
                            (static_cast<std::uint32_t>(b[1]) << 16) |
                            (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
    if (n > kMaxMensaje) {
        buffer.clear(); // no es Cast: mejor cortar que seguir leyendo basura
        return false;
    }
    if (buffer.size() < 4 + n) return false;
    const std::string cuerpo = buffer.substr(4, n);
    buffer.erase(0, 4 + n);
    out = Mensaje{};
    return leerCastMessage(cuerpo, out);
}

// --- el canal ------------------------------------------------------------------------

struct Canal::Impl {
    mbedtls_net_context net{};
    mbedtls_ssl_context ssl{};
    mbedtls_ssl_config conf{};
    mbedtls_entropy_context entropia{};
    mbedtls_ctr_drbg_context drbg{};
    bool abierto = false;
    std::string buffer;
    int siguientePedido = 1;

    Impl() {
        mbedtls_net_init(&net);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropia);
        mbedtls_ctr_drbg_init(&drbg);
    }
    ~Impl() {
        if (abierto) mbedtls_ssl_close_notify(&ssl);
        mbedtls_net_free(&net);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropia);
    }
};

Canal::Canal() : d_(std::make_unique<Impl>()) {}
Canal::~Canal() = default;

/// Abre el TCP eligiendo NOSOTROS por dónde sale. `mbedtls_net_connect` deja
/// que el sistema elija, y cuando una red superpuesta anuncia una ruta que tapa
/// a la local, elige mal: el aparato termina contestando a una dirección de
/// otra red. Como `mbedtls_net_context` es solo un descriptor, se arma el
/// socket a mano y se lo mete ahí.
/// :returns: 0 si salió bien.
int conectarDesde(mbedtls_net_context& net, const std::string& host, int puerto, const std::string& desde) {
    if (desde.empty()) {
        const std::string p = std::to_string(puerto);
        return mbedtls_net_connect(&net, host.c_str(), p.c_str(), MBEDTLS_NET_PROTO_TCP);
    }
#if defined(_WIN32)
    const SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return -1;
#else
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
#endif
    sockaddr_in local{};
    local.sin_family = AF_INET;
    if (::inet_pton(AF_INET, desde.c_str(), &local.sin_addr) != 1 ||
        ::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
#if defined(_WIN32)
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        // Si no se pudo atar a esa dirección, mejor intentar como sea que no
        // intentar: puede ser que la interfaz se haya caído recién.
        const std::string p = std::to_string(puerto);
        return mbedtls_net_connect(&net, host.c_str(), p.c_str(), MBEDTLS_NET_PROTO_TCP);
    }
    sockaddr_in destino{};
    destino.sin_family = AF_INET;
    destino.sin_port = htons(static_cast<unsigned short>(puerto));
    if (::inet_pton(AF_INET, host.c_str(), &destino.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&destino), sizeof(destino)) != 0) {
#if defined(_WIN32)
        ::closesocket(fd);
#else
        ::close(fd);
#endif
        return -1;
    }
    net.fd = static_cast<int>(fd);
    return 0;
}

bool Canal::abrir(const std::string& host, int puerto, int timeoutMs, std::string* error,
                  const std::string& desde) {
    const auto falla = [error](const std::string& que) {
        if (error != nullptr) *error = que;
        return false;
    };
    const std::string p = std::to_string(puerto);
    if (mbedtls_ctr_drbg_seed(&d_->drbg, mbedtls_entropy_func, &d_->entropia, nullptr, 0) != 0)
        return falla("no se pudo inicializar el azar");
    if (conectarDesde(d_->net, host, puerto, desde) != 0) return falla("no contesta en " + host + ":" + p);
    mbedtls_net_set_nonblock(&d_->net);
    if (mbedtls_ssl_config_defaults(&d_->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        return falla("no se pudo preparar el TLS");
    // El certificado del aparato es suyo, no lo firma nadie conocido: no hay
    // nada que verificar. No es un descuido —el canal va por la red local y lo
    // que viaja es "mostrá esta página"—, pero por eso mismo no se manda nada
    // secreto por acá.
    mbedtls_ssl_conf_authmode(&d_->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&d_->conf, mbedtls_ctr_drbg_random, &d_->drbg);
    mbedtls_ssl_conf_read_timeout(&d_->conf, static_cast<std::uint32_t>(timeoutMs));
    if (mbedtls_ssl_setup(&d_->ssl, &d_->conf) != 0) return falla("no se pudo preparar el TLS");
    mbedtls_ssl_set_bio(&d_->ssl, &d_->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);

    const std::int64_t vence = ahoraMs() + timeoutMs;
    for (;;) {
        const int r = mbedtls_ssl_handshake(&d_->ssl);
        if (r == 0) break;
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE)
            return falla("el aparato rechazó la conexión segura");
        if (ahoraMs() > vence) return falla("no terminó de conectar a tiempo");
    }
    d_->abierto = true;
    // Sin esto el aparato corta a los pocos segundos sin decir nada.
    return mandar(Mensaje{kYo, kPlataforma, kNsConexion, R"({"type":"CONNECT"})"}, error);
}

void Canal::cerrar() {
    if (!d_->abierto) return;
    mandar(Mensaje{kYo, kPlataforma, kNsConexion, R"({"type":"CLOSE"})"}, nullptr);
    mbedtls_ssl_close_notify(&d_->ssl);
    d_->abierto = false;
}

bool Canal::mandar(const Mensaje& m, std::string* error) {
    if (!d_->abierto) {
        if (error != nullptr) *error = "el canal no está abierto";
        return false;
    }
    const std::string datos = armar(m);
    std::size_t escrito = 0;
    while (escrito < datos.size()) {
        const int r = mbedtls_ssl_write(
            &d_->ssl, reinterpret_cast<const unsigned char*>(datos.data()) + escrito, datos.size() - escrito);
        if (r > 0) {
            escrito += static_cast<std::size_t>(r);
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (error != nullptr) *error = "se cortó al escribir";
        d_->abierto = false;
        return false;
    }
    return true;
}

bool Canal::esperar(const std::string& tema, int timeoutMs, Mensaje& out, std::string* error) {
    const std::int64_t vence = ahoraMs() + timeoutMs;
    for (;;) {
        // Lo que ya esté en el buffer, primero.
        while (sacar(d_->buffer, out)) {
            // Los PING se contestan acá y no molestan a quien esperaba: si no
            // se contestan, el aparato corta el canal.
            if (out.tema == kNsLatido) {
                const json j = json::parse(out.texto, nullptr, false);
                if (j.is_object() && j.value("type", "") == "PING")
                    mandar(Mensaje{kYo, out.origen, kNsLatido, R"({"type":"PONG"})"}, nullptr);
                if (tema != kNsLatido) continue;
            }
            if (tema.empty() || out.tema == tema) return true;
        }
        if (ahoraMs() > vence) {
            if (error != nullptr) *error = "el aparato no contestó a tiempo";
            return false;
        }
        unsigned char buf[4096];
        const int r = mbedtls_ssl_read(&d_->ssl, buf, sizeof(buf));
        if (r > 0) {
            d_->buffer.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(r));
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE || r == MBEDTLS_ERR_SSL_TIMEOUT)
            continue;
        if (error != nullptr) *error = "el aparato cerró la conexión";
        d_->abierto = false;
        return false;
    }
}

bool Canal::pedir(const std::string& destino, const std::string& tema, json cuerpo, int timeoutMs,
                  json& respuesta, std::string* error) {
    const int id = d_->siguientePedido++;
    cuerpo["requestId"] = id;
    if (!mandar(Mensaje{kYo, destino, tema, cuerpo.dump()}, error)) return false;
    const std::int64_t vence = ahoraMs() + timeoutMs;
    for (;;) {
        Mensaje m;
        const int queda = static_cast<int>(vence - ahoraMs());
        if (queda <= 0) {
            if (error != nullptr) *error = "el aparato no contestó a tiempo";
            return false;
        }
        if (!esperar(tema, queda, m, error)) return false;
        const json j = json::parse(m.texto, nullptr, false);
        if (!j.is_object()) continue;
        // Las respuestas traen el requestId del pedido; los avisos sueltos
        // (que el aparato manda cuando cambia algo) traen 0 o ninguno.
        if (j.value("requestId", 0) != id) continue;
        respuesta = j;
        return true;
    }
}

// --- las operaciones de una ----------------------------------------------------------

namespace {

/// Saca de un RECEIVER_STATUS lo que nos importa.
void leerEstado(const json& r, Estado& e) {
    const json st = r.value("status", json::object());
    const json apps = st.value("applications", json::array());
    if (!apps.empty() && apps[0].is_object()) {
        e.appId = apps[0].value("appId", "");
        e.appNombre = apps[0].value("displayName", "");
        e.transportId = apps[0].value("transportId", "");
    }
    if (st.contains("volume") && st["volume"].is_object()) {
        e.volumen = st["volume"].value("level", -1.0);
        e.silenciado = st["volume"].value("muted", false);
    }
}

/// Abre el canal y pregunta el estado. Deja el canal abierto para seguir.
bool arrancar(Canal& c, const std::string& host, int puerto, int timeoutMs, Estado& e, json& status,
              const std::string& desde = {}) {
    if (!c.abrir(host, puerto, timeoutMs, &e.error, desde)) return false;
    if (!c.pedir(kPlataforma, kNsReceptor, json{{"type", "GET_STATUS"}}, timeoutMs, status, &e.error))
        return false;
    leerEstado(status, e);
    return true;
}

} // namespace

Estado consultar(const std::string& host, int puerto, int timeoutMs, const std::string& desde) {
    Estado e;
    Canal c;
    json status;
    if (!arrancar(c, host, puerto, timeoutMs, e, status, desde)) return e;
    // De paso, en el mismo canal: si sabe mostrar paginas. Preguntar NO abre
    // nada, asi que se puede avisar "este aparato no sirve para esto" sin
    // pisarle a nadie lo que este viendo.
    json r;
    if (c.pedir(kPlataforma, kNsReceptor,
                json{{"type", "GET_APP_AVAILABILITY"}, {"appId", json::array({kAppDashCast})}}, timeoutMs, r,
                nullptr)) {
        e.puedeMostrar = r.value("availability", json::object()).value(kAppDashCast, "") == "APP_AVAILABLE";
    }
    c.cerrar();
    e.ok = true;
    return e;
}

Estado puedeAbrir(const std::string& host, int puerto, const std::string& appId, int timeoutMs,
                  bool& disponible) {
    disponible = false;
    Estado e;
    Canal c;
    json status;
    if (!arrancar(c, host, puerto, timeoutMs, e, status)) return e;
    json r;
    if (!c.pedir(kPlataforma, kNsReceptor,
                 json{{"type", "GET_APP_AVAILABILITY"}, {"appId", json::array({appId})}}, timeoutMs, r,
                 &e.error))
        return e;
    // Contesta {"availability": {"<appId>": "APP_AVAILABLE" | "APP_UNAVAILABLE"}}.
    disponible = r.value("availability", json::object()).value(appId, "") == "APP_AVAILABLE";
    c.cerrar();
    e.ok = true;
    return e;
}

Estado mostrarUrl(const std::string& host, int puerto, const std::string& url, int recargarSeg, int timeoutMs,
                  const std::string& desde) {
    // ESTA SECUENCIA ES LA QUE FUNCIONA, y no es la que uno escribiría solo.
    // Está copiada de un sistema en producción que castea todos los días:
    //
    //     apagar lo que haya  ->  esperar  ->  lanzar  ->  ESPERAR  ->  mandar
    //
    // La tentación es saltear pasos: si DashCast ya está corriendo, ¿para qué
    // apagarlo y volver a abrirlo? Se probó, y no anda: el receptor acepta el
    // mensaje y no hace nada con él. Y mandar la URL apenas contesta el LAUNCH
    // tampoco: la aplicación todavía se está armando del otro lado y el
    // mensaje se pierde sin que nadie avise. Los segundos de espera son el
    // precio de que aparezca.
    Estado e;
    Canal c;
    json status;
    if (!arrancar(c, host, puerto, timeoutMs, e, status, desde)) return e;

    // 1. Apagar lo que esté corriendo (incluso DashCast: uno recién abierto
    //    acepta la URL, uno viejo a veces no).
    const json apps = status.value("status", json::object()).value("applications", json::array());
    if (apps.is_array() && !apps.empty() && apps[0].is_object()) {
        json r;
        c.pedir(kPlataforma, kNsReceptor,
                json{{"type", "STOP"}, {"sessionId", apps[0].value("sessionId", "")}}, timeoutMs, r, nullptr);
        esperarMs(c, 2000);
    }

    // 2. Lanzar. Abrir una aplicación en un televisor tarda: tope propio.
    json r;
    const int topeLanzar = std::max(timeoutMs, 25000);
    if (!c.pedir(kPlataforma, kNsReceptor, json{{"type", "LAUNCH"}, {"appId", kAppDashCast}}, topeLanzar, r,
                 &e.error)) {
        // Puede haber abierto igual y habérsenos perdido la respuesta.
        json st;
        if (!c.pedir(kPlataforma, kNsReceptor, json{{"type", "GET_STATUS"}}, timeoutMs, st, nullptr))
            return e;
        leerEstado(st, e);
        if (e.appId != kAppDashCast) return e;
        e.error.clear();
    } else {
        if (r.value("type", "") == "LAUNCH_ERROR") {
            e.error = "el televisor no pudo abrir DashCast (" + r.value("reason", "sin motivo") + ")";
            return e;
        }
        leerEstado(r, e);
    }
    if (e.transportId.empty()) {
        e.error = "el televisor no dijo por dónde hablarle a la aplicación";
        return e;
    }

    // 3. Esperar a que la aplicación termine de armarse del otro lado. Sin
    //    esto, el mensaje llega y se pierde.
    esperarMs(c, 5000);

    // 4. Presentarse ante la aplicación (el CONNECT del principio era para la
    //    plataforma, no para ella) y recién ahí mandarle la URL.
    if (!c.mandar(Mensaje{kYo, e.transportId, kNsConexion, R"({"type":"CONNECT"})"}, &e.error)) return e;
    const json orden{
        {"url", url}, {"force", true}, {"reload", recargarSeg > 0}, {"reload_time", recargarSeg * 1000}};
    if (!c.mandar(Mensaje{kYo, e.transportId, kNsDashCast, orden.dump()}, &e.error)) return e;
    // DashCast no contesta nada: un respiro para que el mensaje salga antes de
    // cerrar el canal.
    esperarMs(c, 800);
    c.cerrar();
    e.ok = true;
    e.appId = kAppDashCast;
    return e;
}

Estado parar(const std::string& host, int puerto, int timeoutMs, const std::string& desde) {
    Estado e;
    Canal c;
    json status;
    if (!arrancar(c, host, puerto, timeoutMs, e, status, desde)) return e;
    if (e.appId.empty()) { // ya no había nada corriendo
        c.cerrar();
        e.ok = true;
        return e;
    }
    const json apps = status.value("status", json::object()).value("applications", json::array());
    const std::string sesion =
        apps.is_array() && !apps.empty() && apps[0].is_object() ? apps[0].value("sessionId", "") : "";
    json r;
    if (!c.pedir(kPlataforma, kNsReceptor, json{{"type", "STOP"}, {"sessionId", sesion}}, timeoutMs, r,
                 &e.error))
        return e;
    leerEstado(r, e);
    c.cerrar();
    e.ok = true;
    return e;
}

Estado volumen(const std::string& host, int puerto, double nivel, int silenciar, int timeoutMs,
               const std::string& desde) {
    Estado e;
    Canal c;
    json status;
    if (!arrancar(c, host, puerto, timeoutMs, e, status, desde)) return e;
    json vol = json::object();
    if (nivel >= 0) vol["level"] = nivel < 0 ? 0 : (nivel > 1 ? 1 : nivel);
    if (silenciar >= 0) vol["muted"] = silenciar != 0;
    if (!vol.empty()) {
        json r;
        if (!c.pedir(kPlataforma, kNsReceptor, json{{"type", "SET_VOLUME"}, {"volume", vol}}, timeoutMs, r,
                     &e.error))
            return e;
        leerEstado(r, e);
    }
    c.cerrar();
    e.ok = true;
    return e;
}

} // namespace ba::app::pantallas
