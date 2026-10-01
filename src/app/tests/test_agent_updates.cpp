// src/app/tests/test_agent_updates.cpp — buscar la ultima version de los agentes
//
// Pregunta DE VERDAD a la fuente oficial de cada agente (agent/Actualizacion)
// y revisa que lo que vuelve se pueda instalar como la version fijada: una
// version que no es mas vieja que la de agentes.json, y para cada plataforma
// una URL https y su hash. Del paquete de Gemini (chico) se baja el archivo y
// se verifica la integridad SHA-512 de npm, que es el unico hash que no es
// SHA-256. Sin red se saltea (77).

#include "agent/Actualizacion.hpp"
#include "net/HttpsClient.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <QByteArray>
#include <QCryptographicHash>
#include <QVersionNumber>

using ba::app::agent::ultimaPublicada;
using json = nlohmann::json;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

bool esHex64(const std::string& s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}

} // namespace

int main() {
    ba::app::net::HttpsClient::setCaFile(BA_TEST_CACERT);
    std::ifstream in(BA_AGENTES_JSON);
    std::stringstream ss;
    ss << in.rdbuf();
    const json pins = json::parse(ss.str(), nullptr, false);
    if (!pins.is_object()) {
        std::printf("FALLO: no se pudo leer %s\n", BA_AGENTES_JSON);
        return 1;
    }

    int respondieron = 0;
    for (const char* runtime : {"claude", "opencode", "gemini"}) {
        const json base = pins.value(runtime, json::object());
        std::string err;
        const json nueva = ultimaPublicada(runtime, base, &err);
        if (nueva.is_null()) {
            // Sin red (o la fuente caida): no es un error de este codigo.
            std::printf("sin respuesta de %s: %s\n", runtime, err.c_str());
            continue;
        }
        ++respondieron;
        const std::string v = nueva.value("version", std::string());
        std::printf("%s: fijada %s, publicada %s\n", runtime, base.value("version", std::string()).c_str(),
                    v.c_str());
        CHECK(QVersionNumber::fromString(QString::fromStdString(v)) >=
                  QVersionNumber::fromString(QString::fromStdString(base.value("version", std::string()))),
              "la publicada no es mas vieja que la fijada");
        const json plats = nueva.value("plataformas", json::object());
        CHECK(!plats.empty(), "trae plataformas");
        bool todas = true;
        for (const auto& [clave, p] : plats.items()) {
            const std::string url = p.value("url", std::string());
            const bool hash = std::string(runtime) == "gemini" || esHex64(p.value("sha256", std::string()));
            if (url.rfind("https://", 0) != 0 || !hash || p.value("exe", std::string()).empty()) {
                std::printf("   plataforma %s incompleta\n", clave.c_str());
                todas = false;
            }
        }
        CHECK(todas, "cada plataforma con url https, hash y ejecutable");
        CHECK(plats.contains("mac-arm64") && plats.contains("linux-x64") && plats.contains("win-x64"),
              "estan las tres plataformas del aparato");

        if (std::string(runtime) == "gemini") {
            const json pkg = nueva.value("paquete", json::object());
            const std::string sha512 = pkg.value("sha512", std::string());
            CHECK(!sha512.empty() && !pkg.contains("sha256"), "el paquete trae la integridad SHA-512 de npm");
            // Se baja y se verifica igual que lo hace la instalacion.
            ba::app::net::HttpRequest req;
            req.url = pkg.value("url", std::string());
            req.responseFile = std::string(BA_TEST_TMP) + "/gemini-cli.tgz";
            const auto res = ba::app::net::HttpsClient::download(req);
            if (res.error.empty()) {
                std::ifstream f(req.responseFile, std::ios::binary);
                std::stringstream b;
                b << f.rdbuf();
                const std::string datos = b.str();
                const QByteArray h =
                    QCryptographicHash::hash(QByteArray(datos.data(), static_cast<qsizetype>(datos.size())),
                                             QCryptographicHash::Sha512)
                        .toBase64();
                CHECK(h == QByteArray::fromStdString(sha512), "el paquete bajado coincide con su SHA-512");
                std::remove(req.responseFile.c_str());
            } else {
                std::printf("no se pudo bajar el paquete: %s\n", res.error.c_str());
            }
        }
    }
    if (respondieron == 0) {
        std::printf("SALTEADO: ninguna fuente respondio (sin red)\n");
        return 77;
    }
    if (g_failures == 0) {
        std::printf("test_agent_updates: OK\n");
        return 0;
    }
    std::printf("test_agent_updates: %d fallo(s)\n", g_failures);
    return 1;
}
