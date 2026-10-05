// test_proxy_nombres.cpp — el proxy por nombre (T61) de punta a punta, en loopback:
// un servicio falso, un cliente TLS con SNI, el certificado de cada nombre, la
// redirección de «/», la cabecera PROXY y el 80 (desafío ACME y redirección).

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "proxy/Acme.hpp"
#include "proxy/Certificados.hpp"

#include <mbedtls/pk.h>
#include "proxy/ProxyNombres.hpp"

using namespace ba::gateway::proxy;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

#ifndef _WIN32
/// Un servicio de adentro: contesta con lo que recibió (para ver la cabecera PROXY).
struct ServicioFalso {
    int fd = -1;
    uint16_t puerto = 0;
    std::atomic<bool> fin{false};
    std::thread t;
    std::string ultimo;
    void arrancar() {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        int uno = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        socklen_t l = sizeof(a);
        getsockname(fd, reinterpret_cast<sockaddr*>(&a), &l);
        puerto = ntohs(a.sin_port);
        listen(fd, 8);
        t = std::thread([this]() {
            while (!fin) {
                const int c = accept(fd, nullptr, nullptr);
                if (c < 0) break;
                std::string r;
                char b[4096];
                while (r.find("\r\n\r\n") == std::string::npos) {
                    const ssize_t n = recv(c, b, sizeof b, 0);
                    if (n <= 0) break;
                    r.append(b, static_cast<size_t>(n));
                }
                ultimo = r;
                const std::string cuerpo = "hola desde adentro";
                const std::string resp = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(cuerpo.size()) +
                                         "\r\nConnection: close\r\n\r\n" + cuerpo;
                send(c, resp.data(), resp.size(), 0);
                close(c);
            }
        });
    }
    ~ServicioFalso() {
        fin = true;
        shutdown(fd, SHUT_RDWR);
        close(fd);
        if (t.joinable()) t.join();
    }
};

/// Un pedido por TLS con SNI `host`: devuelve la respuesta y el CN del certificado que mostró.
std::string pedirTls(uint16_t puerto, const std::string& host, const std::string& ruta, std::string* cn) {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context ent;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&ent);
    mbedtls_ctr_drbg_init(&drbg);
    std::string out;
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, nullptr, 0) == 0 &&
        mbedtls_net_connect(&net, "127.0.0.1", std::to_string(puerto).c_str(), MBEDTLS_NET_PROTO_TCP) == 0 &&
        mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE); // el autofirmado de la prueba
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_setup(&ssl, &conf);
        if (!host.empty()) mbedtls_ssl_set_hostname(&ssl, host.c_str());
        mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, nullptr);
        if (mbedtls_ssl_handshake(&ssl) == 0) {
            if (const mbedtls_x509_crt* c = mbedtls_ssl_get_peer_cert(&ssl); c != nullptr && cn != nullptr) {
                char b[256];
                mbedtls_x509_dn_gets(b, sizeof b, &c->subject);
                *cn = b;
            }
            const std::string req = "GET " + ruta + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
            mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(req.data()), req.size());
            unsigned char b[4096];
            for (;;) {
                const int n = mbedtls_ssl_read(&ssl, b, sizeof b);
                if (n <= 0) break;
                out.append(reinterpret_cast<char*>(b), static_cast<size_t>(n));
            }
        }
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_net_free(&net);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&ent);
    return out;
}

std::string pedirHttp(uint16_t puerto, const std::string& host, const std::string& ruta) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::string out;
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) {
        const std::string req = "GET " + ruta + " HTTP/1.1\r\nHost: " + host + "\r\n\r\n";
        send(fd, req.data(), req.size(), 0);
        char b[4096];
        for (;;) {
            const ssize_t n = recv(fd, b, sizeof b, 0);
            if (n <= 0) break;
            out.append(b, static_cast<size_t>(n));
        }
    }
    close(fd);
    return out;
}
#endif
} // namespace

int main() {
    std::printf("== test_proxy_nombres ==\n");
    // Certificados: clave, autofirmado con varios nombres, vencimiento y CSR.
    const std::string clave = claveNueva();
    CHECK(clave.find("PRIVATE KEY") != std::string::npos, "clave ECDSA nueva");
    const std::string casa = autofirmado(clave, {"sala.prueba.local", "sesion.prueba.local", "192.168.1.5"});
    CHECK(casa.find("BEGIN CERTIFICATE") != std::string::npos, "autofirmado de la casa");
    CHECK(vence(casa) > 1900000000, "vence en 2035");
    const auto ns = nombresDe(casa);
    CHECK(ns.size() == 2 && std::find(ns.begin(), ns.end(), "sala.prueba.local") != ns.end() &&
              std::find(ns.begin(), ns.end(), "sesion.prueba.local") != ns.end(),
          "los nombres DNS del SAN (sin la IP)");
    CHECK(csrDer(clave, {"sala.prueba.local"}).size() > 100, "CSR en DER");
    CHECK(base64url(std::string("\xfb\xff", 2)) == "-_8", "base64url sin relleno");
    // JOSE: la firma ES256 cruda (r‖s) verifica con la clave pública.
    {
        std::string firma;
        CHECK(firmarEs256(clave, "hola", firma) && firma.size() == 64, "firma ES256 de 64 bytes");
        // r‖s -> DER para verificarla con mbedtls.
        const auto entero = [](std::string v) {
            while (v.size() > 1 && v[0] == '\0' && !(static_cast<unsigned char>(v[1]) & 0x80)) v.erase(0, 1);
            if (static_cast<unsigned char>(v[0]) & 0x80) v.insert(0, 1, '\0');
            return std::string("\x02", 1) + static_cast<char>(v.size()) + v;
        };
        const std::string cuerpo = entero(firma.substr(0, 32)) + entero(firma.substr(32));
        const std::string der = std::string("\x30", 1) + static_cast<char>(cuerpo.size()) + cuerpo;
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        std::string z = clave;
        z.push_back('\0');
        mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char*>(z.data()), z.size(), nullptr, 0, nullptr, nullptr);
        const std::string h = sha256("hola");
        CHECK(mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(h.data()), 32,
                                reinterpret_cast<const unsigned char*>(der.data()), der.size()) == 0,
              "la firma verifica con la clave pública");
        mbedtls_pk_free(&pk);
        std::string x, y;
        CHECK(coordenadas(clave, x, y) && x.size() == 32 && y.size() == 32, "coordenadas de la JWK");
        ClienteAcme acme(ClienteAcme::kLetsEncryptPruebas, "", clave);
        const std::string au = acme.autorizacion("tokXYZ");
        CHECK(au.rfind("tokXYZ.", 0) == 0 && au.size() == 7 + 43, "autorización del desafío: token.huella (43 caracteres)");
    }
    CHECK(ProxyNombres::ipLocal(inet_addr("192.168.0.10")) && !ProxyNombres::ipLocal(inet_addr("8.8.8.8")), "red local");

#ifndef _WIN32
    ServicioFalso sv;
    sv.arrancar();
    ProxyNombres::Config cfg;
    cfg.bind = "127.0.0.1";
    cfg.puertoTls = 18443;
    cfg.puertoHttp = 18080;
    cfg.dominio = "prueba.local";
    cfg.servicios = {{"sala", sv.puerto, "/", false, true}, {"sesion", sv.puerto, "/sesion/", false, true}};
    ProxyNombres px(cfg, [](const std::string& m) { std::printf("  [proxy] %s\n", m.c_str()); });
    CHECK(px.start(casa, clave), "arranca en 18443 y 18080");
    // Un certificado propio para sesion.prueba.local: el SNI lo tiene que elegir.
    const std::string clave2 = claveNueva();
    CHECK(px.ponerCertificado("sesion.prueba.local", autofirmado(clave2, {"sesion.prueba.local"}), clave2, "acme"),
          "certificado propio para sesion");

    std::string cn;
    const std::string r1 = pedirTls(18443, "sala.prueba.local", "/hola", &cn);
    CHECK(r1.find("hola desde adentro") != std::string::npos, "sala: pasa al servicio y vuelve");
    CHECK(cn.find("sala.prueba.local") != std::string::npos, "sala: el certificado de la casa (CN del primero)");
    CHECK(sv.ultimo.rfind("PROXY TCP4 127.0.0.1 ", 0) == 0 && sv.ultimo.find("GET /hola") != std::string::npos,
          "el servicio recibe la cabecera PROXY y el pedido");

    std::string cn2;
    const std::string r2 = pedirTls(18443, "sesion.prueba.local", "/", &cn2);
    CHECK(cn2.find("CN=sesion.prueba.local") != std::string::npos, "sesion: el SNI elige su certificado");
    CHECK(r2.find("302") != std::string::npos && r2.find("Location: /sesion/") != std::string::npos,
          "sesion: «/» se redirige a /sesion/");
    const std::string r3 = pedirTls(18443, "sesion.prueba.local", "/sesion/", nullptr);
    CHECK(r3.find("hola desde adentro") != std::string::npos, "sesion: /sesion/ pasa");
    const std::string r4 = pedirTls(18443, "", "/x", nullptr);
    CHECK(r4.find("hola desde adentro") != std::string::npos, "sin SNI (por IP): el servicio por defecto");

    px.ponerDesafio("tok123", "tok123.huella");
    CHECK(pedirHttp(18080, "sala.prueba.local", "/.well-known/acme-challenge/tok123").find("tok123.huella") != std::string::npos,
          "80: contesta el desafío ACME");
    CHECK(pedirHttp(18080, "sala.prueba.local", "/.well-known/acme-challenge/otro").find("404") != std::string::npos,
          "80: un desafío desconocido es 404");
    const std::string r5 = pedirHttp(18080, "sala.prueba.local", "/algo");
    CHECK(r5.find("301") != std::string::npos && r5.find("Location: https://sala.prueba.local:18443/algo") != std::string::npos,
          "80: lo demás va a https");
    const auto e = px.estado();
    CHECK(e["servicios"].size() == 2 && e["servicios"][1]["certificado"]["origen"] == "acme" && e["total"].get<int>() >= 6,
          "estado: servicios, certificado por nombre y conexiones");
    px.stop();
#endif
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
