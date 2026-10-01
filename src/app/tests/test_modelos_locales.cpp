// src/app/tests/test_modelos_locales.cpp — lo que decide si un modelo bajado vale
//
// Sin red y sin motor: la respuesta de la API de Hugging Face (?blobs=true)
// se lee bien —commit, tamaños, sha256 de LFS y blobId de git—, un nombre de
// archivo que intenta salir de la carpeta se descarta, y el sha1 de blob de git
// es el mismo que da `git hash-object` (es el que HF publica para los archivos
// chicos: config.json, prompt.json...).
//
// Con BA_MODELOS_VIVO=<carpeta de ba-modelo> y BA_MODELOS_REPO=<repo> corre
// además de punta a punta, con red y motor: baja el repo (BA_MODELOS_SEMILLA=
// <carpeta con el repo ya bajado> lo siembra por enlaces, y solo se verifica),
// lo activa, elige y genera. Es la prueba de la Mac; en el CI no corre.

#include "agent/ModelosLocales.hpp"
#include "net/HttpsClient.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ba::app::agent;

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

std::filesystem::path escribir(const std::string& nombre, const std::string& contenido) {
    const auto p = std::filesystem::path(BA_TEST_TMP) / nombre;
    std::ofstream(p, std::ios::binary) << contenido;
    return p;
}

/// Corre el bucle de eventos hasta que `listo()` o se acabe el tiempo.
template <typename F> bool esperar(F listo, int ms) {
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return listo();
}

void vivo(const char* binarios, const char* repo) {
    namespace fs = std::filesystem;
    const fs::path raiz = fs::path(BA_TEST_TMP) / "modelos-vivo";
    std::error_code ec;
    fs::remove_all(raiz, ec);
    const fs::path destino = raiz / "Modelos" / carpetaDeRepo(repo);
    if (const char* semilla = std::getenv("BA_MODELOS_SEMILLA")) {
        fs::create_directories(destino, ec);
        for (const auto& e : fs::directory_iterator(semilla, ec))
            if (e.is_regular_file()) fs::create_hard_link(e.path(), destino / e.path().filename(), ec);
    }
    ba::app::net::HttpsClient::setCaFile(BA_TEST_CACERT);
    ModelosLocales m(raiz, QString::fromLocal8Bit(binarios));
    CHECK(!m.catalogo().isEmpty(), "el catalogo viaja en el binario y tiene modelos para esta plataforma");
    m.descargar(QString::fromLocal8Bit(repo));
    CHECK(esperar([&] { return !m.descargando(); }, 30 * 60 * 1000), "la descarga termina");
    std::printf("   %s\n", m.textoDescarga().toUtf8().constData());
    CHECK(fs::exists(destino / ".ok"), "el modelo queda instalado y verificado");

    m.activar(QString::fromLocal8Bit(repo));
    CHECK(esperar([&] { return m.estado() == "listo" || m.estado() == "error"; }, 5 * 60 * 1000) &&
              m.estado() == "listo",
          "el motor arranca y lee el prefijo");
    std::printf("   %s\n", m.detalle().toUtf8().constData());

    nlohmann::json eleccion;
    m.elegir("dale take", [&](const nlohmann::json& j) { eleccion = j; });
    esperar([&] { return !eleccion.is_null(); }, 60000);
    CHECK(eleccion.value("herramienta", std::string()) == "scene_take", "elegir: «dale take» -> scene_take");
    std::printf("   %s\n", eleccion.dump().c_str());

    nlohmann::json fin;
    QString texto;
    m.generar(
        "<|im_start|>user\nDecime hola en una "
        "palabra.<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n",
        false, 16, [&](const QString& d) { texto += d; }, [&](const nlohmann::json& j) { fin = j; });
    esperar([&] { return !fin.is_null(); }, 60000);
    CHECK(fin.value("fin", false) && !texto.isEmpty(), "generar: llegan los delta y el fin");
    std::printf("   «%s» %s\n", texto.toUtf8().constData(), fin.dump().c_str());
    m.apagar();
    CHECK(m.estado() == "apagado", "apagar");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Recortada de la API real (BlairQ/broadcast-reactivo-qwen3.5-4b-mtp-ud-mlx-q4).
    const std::string api = R"({
      "id": "BlairQ/broadcast-reactivo-qwen3.5-4b-mtp-ud-mlx-q4",
      "sha": "35673284aa00bb11cc22dd33ee44ff5566778899",
      "private": false,
      "siblings": [
        {"rfilename": ".gitattributes", "blobId": "a6344aac8c09253b3b630fb776ae94478aa0275b", "size": 144},
        {"rfilename": "huella-prompt.json", "blobId": "796f6984216832c249d658846649876806edbd49", "size": 109},
        {"rfilename": "model.safetensors", "blobId": "0f1e", "size": 2367237277,
         "lfs": {"sha256": "9de3d14bce00aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "size": 2367237277}},
        {"rfilename": "../fuera.txt", "blobId": "x", "size": 1},
        {"rfilename": "/etc/passwd", "blobId": "x", "size": 1}
      ]
    })";
    const RepoHf r = leerRepoHf(api);
    CHECK(r.commit == "35673284aa00bb11cc22dd33ee44ff5566778899", "el commit del repo");
    CHECK(!r.privado, "publico");
    CHECK(r.archivos.size() == 3, "los nombres que salen de la carpeta se descartan");
    bool lfs = false, chico = false;
    for (const ArchivoHf& a : r.archivos) {
        if (a.nombre == "model.safetensors")
            lfs = a.tamano == 2367237277ULL && a.sha256.rfind("9de3d14bce00", 0) == 0;
        if (a.nombre == "huella-prompt.json")
            chico =
                a.sha256.empty() && a.blob == "796f6984216832c249d658846649876806edbd49" && a.tamano == 109;
    }
    CHECK(lfs, "archivo LFS: tamano y sha256");
    CHECK(chico, "archivo chico: blobId, sin sha256");
    CHECK(leerRepoHf("no es json").archivos.empty(), "una respuesta rota no da archivos");

    // Los mismos valores que `printf 'hola\n' | git hash-object --stdin`.
    CHECK(shaBlobGit(escribir("blob-hola.txt", "hola\n")) == "5c1b14949828006ed75a3e8858957f86a2f7e2eb",
          "sha1 de blob de git");
    CHECK(shaBlobGit(escribir("blob-vacio.txt", "")) == "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391",
          "sha1 de blob de git (vacio)");
    CHECK(shaBlobGit(std::filesystem::path(BA_TEST_TMP) / "no-existe").empty(), "sin archivo, sin hash");

    CHECK(carpetaDeRepo("BlairQ/broadcast-reactivo-qwen3.5-4b-mlx-q4") ==
              "BlairQ__broadcast-reactivo-qwen3.5-4b-mlx-q4",
          "la carpeta de un repo");
    const std::string plat = plataformaCatalogo();
    CHECK(plat == "mac-arm64" || plat == "mac-x64" || plat == "linux-x64" || plat == "linux-arm64" ||
              plat == "win-x64" || plat == "win-arm64",
          "la plataforma como la nombra el catalogo");

    if (const char* b = std::getenv("BA_MODELOS_VIVO")) {
        const char* repo = std::getenv("BA_MODELOS_REPO");
        vivo(b, repo != nullptr ? repo : "BlairQ/broadcast-reactivo-qwen3.5-4b-mtp-ud-mlx-q4");
    }

    std::printf(g_failures == 0 ? "test_modelos_locales: OK\n" : "test_modelos_locales: %d fallos\n",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
