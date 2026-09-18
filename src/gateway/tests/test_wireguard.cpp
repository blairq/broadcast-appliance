// test_wireguard.cpp
//
// WireGuard en espacio de usuario (src/gateway/net).
//
//   1. Primitivas contra vectores de implementaciones de TERCEROS:
//      BLAKE2s (RFC 7693 + hashlib), HMAC-BLAKE2s (hmac+hashlib), X25519
//      (RFC 7748), ChaCha20-Poly1305 con el nonce de WireGuard
//      (cryptography/OpenSSL), HChaCha20 (draft-irtf-cfrg-xchacha) y
//      XChaCha20-Poly1305 (libsodium via pynacl).
//   2. Dos WgDevice en loopback: handshake (iniciador/respondedor), paquetes
//      en ambos sentidos, con PSK.
//   3. Interoperabilidad con el par de REFERENCIA en Python
//      (wg_reference_peer.py: implementacion independiente sobre hashlib y
//      OpenSSL), en los DOS roles. Sin python3/cryptography: SKIP de esa parte.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/Blake2s.hpp"
#include "net/WgCrypto.hpp"
#include "net/WgDevice.hpp"

using namespace ba::gateway::net;

namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

std::vector<uint8_t> unhex(const std::string& h) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        v.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
    return v;
}

Key32 key32(const std::string& h) {
    Key32 k{};
    const auto v = unhex(h);
    std::memcpy(k.data(), v.data(), 32);
    return k;
}

void testPrimitives() {
    uint8_t out[32];
    Blake2s::hash(out, 32, reinterpret_cast<const uint8_t*>("abc"), 3);
    CHECK(hex(out, 32) == "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982",
          "BLAKE2s-256(abc)");
    uint8_t key[32];
    for (int i = 0; i < 32; ++i)
        key[i] = static_cast<uint8_t>(i);
    Blake2s::hash(out, 32, nullptr, 0, key, 32);
    CHECK(hex(out, 32) == "48a8997da407876b3d79c0d92325ad3b89cbb754d86ab71aee047ad345fd2c49",
          "BLAKE2s con clave, vacio");
    uint8_t data[200];
    for (int i = 0; i < 200; ++i)
        data[i] = static_cast<uint8_t>(i);
    Blake2s::hash(out, 32, data, 200, key, 32);
    CHECK(hex(out, 32) == "13c88480a5d00d6c8c7ad2110d76a82d9b70f4fa6696d4e5dd42a066dcaf9920",
          "BLAKE2s con clave, 200 bytes (varios bloques)");
    uint8_t m16[16];
    mac(m16, key, 16, reinterpret_cast<const uint8_t*>("hola wireguard"), 14);
    CHECK(hex(m16, 16) == "ca8d429a6b401a602c25c06bb59e62fa", "MAC (BLAKE2s-128 con clave de 16)");
    hmacBlake2s(out, reinterpret_cast<const uint8_t*>("clave"), 5,
                reinterpret_cast<const uint8_t*>("mensaje"), 7);
    CHECK(hex(out, 32) == "983cc7f05e375c8883f2295bd7379aa8f2374c5267d95cdd64b74bd12b361924", "HMAC-BLAKE2s");
    uint8_t longKey[100];
    for (int i = 0; i < 100; ++i)
        longKey[i] = static_cast<uint8_t>(i);
    hmacBlake2s(out, longKey, 100, reinterpret_cast<const uint8_t*>("mensaje"), 7);
    CHECK(hex(out, 32) == "def3d3a7391884ab44dfa2c9d3091b4fea2e23b0c4f93230bbd214e85dbf9a98",
          "HMAC-BLAKE2s con clave larga (se hashea)");

    Key32 shared{};
    CHECK(x25519(shared, key32("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
                 key32("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c")),
          "X25519 calcula");
    CHECK(hex(shared.data(), 32) == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
          "X25519 (RFC 7748 §5.2)");
    Key32 zero{};
    CHECK(!x25519(shared, key32("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"), zero),
          "X25519 rechaza un punto de orden bajo");
    Key32 a{}, ap{}, b{}, bp{}, s1{}, s2{};
    x25519Generate(a, ap);
    x25519Generate(b, bp);
    x25519(s1, a, bp);
    x25519(s2, b, ap);
    CHECK(s1 == s2, "DH simetrico");

    Key32 k{};
    for (int i = 0; i < 32; ++i)
        k[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    const char* pt = "paquete de prueba 123";
    std::vector<uint8_t> ct(std::strlen(pt) + 16);
    aeadEncrypt(ct.data(), k, 0x0102030405060708ULL, reinterpret_cast<const uint8_t*>(pt), std::strlen(pt),
                reinterpret_cast<const uint8_t*>("aad"), 3);
    CHECK(hex(ct.data(), ct.size()) ==
              "8028dc91ce4d8f3800ce0abc010d04da8126c3f6bb4f9256a7ca3b33b07847fa1414db8990",
          "ChaCha20-Poly1305 con nonce de WireGuard (OpenSSL)");
    std::vector<uint8_t> back(std::strlen(pt));
    CHECK(aeadDecrypt(back.data(), k, 0x0102030405060708ULL, ct.data(), ct.size(),
                      reinterpret_cast<const uint8_t*>("aad"), 3) &&
              std::memcmp(back.data(), pt, back.size()) == 0,
          "descifra");
    ct[3] ^= 1;
    CHECK(!aeadDecrypt(back.data(), k, 0x0102030405060708ULL, ct.data(), ct.size(),
                       reinterpret_cast<const uint8_t*>("aad"), 3),
          "rechaza un texto alterado");

    const auto hk = unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const auto hn = unhex("000000090000004a0000000031415927");
    hchacha20(out, hk.data(), hn.data());
    CHECK(hex(out, 32) == "82413b4227b27bfed30e42508a877d73a0f9e4d58a74a853c12ec41326d3ecdc",
          "HChaCha20 (draft-irtf-cfrg-xchacha §2.2.1)");
    uint8_t xnonce[24];
    for (int i = 0; i < 24; ++i)
        xnonce[i] = static_cast<uint8_t>(i);
    uint8_t aad16[16];
    for (int i = 0; i < 16; ++i)
        aad16[i] = static_cast<uint8_t>(i);
    uint8_t xct[32];
    xaeadEncrypt(xct, k, xnonce, reinterpret_cast<const uint8_t*>("cookie-16-bytes!"), 16, aad16, 16);
    CHECK(hex(xct, 32) == "fdad6014f9b7a09f056944b7bf37dbc9d2a8e51a79c1cef8ee7ade4d4d1d9788",
          "XChaCha20-Poly1305 (libsodium)");

    Key32 kk{};
    x25519Generate(a, kk);
    const std::string b64 = toBase64(kk);
    Key32 again{};
    CHECK(b64.size() == 44 && fromBase64(again, b64) && again == kk, "clave en base64 (formato de wg)");
}

/// Paquete IPv4/UDP minimo para las pruebas.
std::vector<uint8_t> udpPacket(uint32_t src, uint32_t dst, uint16_t sp, uint16_t dp,
                               const std::string& payload) {
    std::vector<uint8_t> p(28 + payload.size(), 0);
    const size_t total = p.size();
    p[0] = 0x45;
    p[2] = static_cast<uint8_t>(total >> 8);
    p[3] = static_cast<uint8_t>(total);
    p[8] = 64;
    p[9] = 17;
    for (int i = 0; i < 4; ++i) {
        p[12 + i] = static_cast<uint8_t>(src >> (24 - 8 * i));
        p[16 + i] = static_cast<uint8_t>(dst >> (24 - 8 * i));
    }
    p[20] = static_cast<uint8_t>(sp >> 8);
    p[21] = static_cast<uint8_t>(sp);
    p[22] = static_cast<uint8_t>(dp >> 8);
    p[23] = static_cast<uint8_t>(dp);
    p[24] = static_cast<uint8_t>((8 + payload.size()) >> 8);
    p[25] = static_cast<uint8_t>(8 + payload.size());
    std::memcpy(p.data() + 28, payload.data(), payload.size());
    return p;
}

bool waitFor(const std::function<bool()>& f, int ms) {
    for (int i = 0; i < ms / 20; ++i) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return f();
}

void testLoopback() {
    Key32 aPriv{}, aPub{}, bPriv{}, bPub{}, psk{};
    x25519Generate(aPriv, aPub);
    x25519Generate(bPriv, bPub);
    randomBytes(psk.data(), 32);

    WgConfig bc;
    bc.privateKey = bPriv;
    bc.peerPublicKey = aPub;
    bc.presharedKey = psk;
    bc.persistentKeepaliveSec = 0;
    WgDevice b(bc);
    std::mutex m;
    std::vector<std::vector<uint8_t>> atB, atA;
    b.setOnPacket([&](const uint8_t* p, size_t n) {
        std::lock_guard<std::mutex> l(m);
        atB.emplace_back(p, p + n);
    });
    CHECK(b.start(), "respondedor escuchando");

    WgConfig ac;
    ac.privateKey = aPriv;
    ac.peerPublicKey = bPub;
    ac.presharedKey = psk;
    ac.endpointHost = "127.0.0.1";
    ac.endpointPort = b.localPort();
    ac.persistentKeepaliveSec = 0;
    WgDevice a(ac);
    a.setOnPacket([&](const uint8_t* p, size_t n) {
        std::lock_guard<std::mutex> l(m);
        atA.emplace_back(p, p + n);
    });
    CHECK(a.start(), "iniciador arrancado");

    const auto ping = udpPacket(0x0A630002, 0x0A630001, 5000, 7, "hola desde A");
    CHECK(a.send(ping.data(), ping.size()), "A encola (dispara el handshake)");
    CHECK(waitFor(
              [&] {
                  std::lock_guard<std::mutex> l(m);
                  return !atB.empty();
              },
              3000),
          "el paquete llega a B por el tunel");
    {
        std::lock_guard<std::mutex> l(m);
        CHECK(!atB.empty() && atB[0] == ping, "identico (sin el relleno)");
    }
    CHECK(a.stats().up && b.stats().up, "sesion arriba en los dos lados");
    const auto pong = udpPacket(0x0A630001, 0x0A630002, 7, 5000, "respuesta de B");
    CHECK(b.send(pong.data(), pong.size()), "B contesta");
    CHECK(waitFor(
              [&] {
                  std::lock_guard<std::mutex> l(m);
                  return !atA.empty();
              },
              3000),
          "la respuesta llega a A");
    {
        std::lock_guard<std::mutex> l(m);
        CHECK(!atA.empty() && atA[0] == pong, "identica");
    }
    for (int i = 0; i < 200; ++i)
        a.send(ping.data(), ping.size());
    CHECK(waitFor(
              [&] {
                  std::lock_guard<std::mutex> l(m);
                  return atB.size() >= 201;
              },
              3000),
          "rafaga de 200 paquetes");
    CHECK(a.stats().handshakes == 1, "un solo handshake para todo");
    a.stop();
    b.stop();

    // PSK distinta: el handshake NO debe prosperar.
    Key32 other{};
    randomBytes(other.data(), 32);
    bc.presharedKey = other;
    WgDevice b2(bc);
    b2.start();
    ac.endpointPort = b2.localPort();
    WgDevice a2(ac);
    std::atomic<int> got{0};
    b2.setOnPacket([&](const uint8_t*, size_t) { got++; });
    a2.start();
    a2.send(ping.data(), ping.size());
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    CHECK(got == 0 && !a2.stats().up, "con PSK distinta no hay sesion");
    a2.stop();
    b2.stop();
}

bool havePythonPeer() {
    return std::system("python3 -c \"import cryptography, hashlib\" >/dev/null 2>&1") == 0;
}

void testInterop() {
    if (!havePythonPeer()) {
        std::printf("SKIP interop: falta python3 con 'cryptography'\n");
        return;
    }
    Key32 cPriv{}, cPub{}, pPriv{}, pPub{}, psk{};
    x25519Generate(cPriv, cPub);
    x25519Generate(pPriv, pPub);
    randomBytes(psk.data(), 32);
    const std::string peer = BA_TEST_WG_PEER;

    // --- C++ inicia, Python responde (el caso real: somos cliente) ----------
    {
        const int port =
            30000 + static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() % 20000);
        const std::string cmd = "python3 " + peer + " respond " + std::to_string(port) + " " +
                                toBase64(pPriv) + " " + toBase64(cPub) + " " + toBase64(psk) +
                                " 4 >/dev/null 2>&1";
        std::atomic<int> rc{-1};
        std::thread py([&] { rc = std::system(cmd.c_str()); });
        std::this_thread::sleep_for(std::chrono::milliseconds(700)); // que abra el socket
        WgConfig c;
        c.privateKey = cPriv;
        c.peerPublicKey = pPub;
        c.presharedKey = psk;
        c.endpointHost = "127.0.0.1";
        c.endpointPort = static_cast<uint16_t>(port);
        c.persistentKeepaliveSec = 0;
        WgDevice dev(c);
        std::mutex m;
        std::vector<uint8_t> echo;
        dev.setOnPacket([&](const uint8_t* p, size_t n) {
            std::lock_guard<std::mutex> l(m);
            echo.assign(p, p + n);
        });
        dev.start();
        const auto pkt = udpPacket(0x0A630002, 0x0A630001, 4000, 9, "hola referencia");
        dev.send(pkt.data(), pkt.size());
        const bool got = waitFor(
            [&] {
                std::lock_guard<std::mutex> l(m);
                return !echo.empty();
            },
            3000);
        CHECK(got, "C++ (iniciador) <-> Python (respondedor): handshake + ida y vuelta");
        {
            std::lock_guard<std::mutex> l(m);
            const auto want = udpPacket(0x0A630001, 0x0A630002, 9, 4000, "hola referencia");
            CHECK(got && echo.size() == want.size() &&
                      std::memcmp(echo.data() + 12, want.data() + 12, 16) == 0 &&
                      std::memcmp(echo.data() + 28, want.data() + 28, want.size() - 28) == 0,
                  "el eco de la referencia es el esperado");
        }
        dev.stop();
        py.join();
        CHECK(rc == 0, "la referencia descifro nuestros paquetes");
    }

    // --- Python inicia, C++ responde ------------------------------------------
    {
        WgConfig c;
        c.privateKey = cPriv;
        c.peerPublicKey = pPub;
        c.presharedKey = psk;
        c.persistentKeepaliveSec = 0; // sin endpoint: solo respondedor
        WgDevice dev(c);
        WgDevice* devp = &dev;
        dev.setOnPacket([devp](const uint8_t* p, size_t n) {
            // Eco: IPs y puertos intercambiados.
            std::vector<uint8_t> e(p, p + n);
            for (int i = 0; i < 4; ++i)
                std::swap(e[12 + i], e[16 + i]);
            std::swap(e[20], e[22]);
            std::swap(e[21], e[23]);
            devp->send(e.data(), e.size());
        });
        dev.start();
        const std::string cmd = "python3 " + peer + " initiate " + std::to_string(dev.localPort()) + " " +
                                toBase64(pPriv) + " " + toBase64(cPub) + " " + toBase64(psk) +
                                " 4 >/dev/null 2>&1";
        const int rc = std::system(cmd.c_str());
        CHECK(rc == 0, "Python (iniciador) <-> C++ (respondedor): handshake + eco correcto");
        dev.stop();
    }
}

} // namespace

int main() {
    std::printf("== test_wireguard ==\n");
    testPrimitives();
    testLoopback();
    testInterop();
    if (g_failures == 0) {
        std::printf("test_wireguard: OK\n");
        return 0;
    }
    std::printf("test_wireguard: %d fallo(s)\n", g_failures);
    return 1;
}
