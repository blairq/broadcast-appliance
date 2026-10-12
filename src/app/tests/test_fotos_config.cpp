// ============================================================================
// src/app/tests/test_fotos_config.cpp — las fotos de Config/ del histórico
//
//   1. La foto nombra cada *.json (también en subcarpetas) y deja afuera las
//      copias (*.anterior) y lo que no es JSON.
//   2. Los secretos no llegan al disco; «clave» de un catálogo, sí.
//   3. Dos fotos iguales no guardan dos veces lo mismo.
//   4. Comparar dice qué archivo cambió, cuál apareció y cuál se fue, con el
//      JSON Patch del cambio.
//   5. Un JSON roto (se estaba escribiendo) no rompe la foto.
//   6. Guardar y cargar una foto.
// ============================================================================
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "evento/FotosConfig.hpp"

namespace fs = std::filesystem;
using ba::evento::FotosConfig;
using json = nlohmann::json;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)

void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << s;
}

std::size_t contar(const fs::path& d) {
    std::size_t n = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(d, ec)) n += e.is_regular_file() ? 1 : 0;
    return n;
}
} // namespace

int correr();

int main() {
    // Sin búfer: si algo se cae (en Windows, una excepción sin atrapar), lo que
    // ya se imprimió no se pierde.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        return correr();
    } catch (const std::exception& e) {
        std::printf("FALLO: excepción: %s\n", e.what());
        return 1;
    }
}

int correr() {
    const fs::path raiz = fs::temp_directory_path() /
                          ("ba-test-fotos-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path cfg = raiz / "Config", obj = raiz / "Eventos" / ".objetos";
    escribir(cfg / "escenas.json", R"({"actual": "Principal", "escenas": ["Principal", "Cierre"]})");
    escribir(cfg / "destinos.json", R"({"youtube": {"stream_key": "abcd-efgh-ijkl-mnop-qrst", "url": "rtmp://a.rtmp.youtube.com/live2"}})");
    escribir(cfg / "audio-racks.json", R"({"clave": "gate", "umbral": -40})");
    escribir(cfg / "personas" / "datos.json", R"({"n": 1})");
    escribir(cfg / "app.json.anterior", R"({"viejo": true})");
    escribir(cfg / "personas" / "cara.jpg", "no es json");

    FotosConfig fotos(cfg, obj);
    const json f1 = fotos.foto(1000);
    const json a1 = f1["archivos"];
    CHECK(a1.size() == 4, "cuatro json (con la subcarpeta, sin la copia ni la foto)");
    CHECK(a1.contains("personas/datos.json"), "ruta con / también en Windows");
    CHECK(!a1.contains("app.json.anterior"), "las copias no entran");

    const json dest = fotos.leer(a1["destinos.json"]["sha"]);
    CHECK(dest["youtube"]["stream_key"] == FotosConfig::kTapado, "la clave de transmisión, tapada");
    CHECK(dest["youtube"]["url"] == "rtmp://a.rtmp.youtube.com/live2", "lo demás, igual");
    CHECK(fotos.leer(a1["audio-racks.json"]["sha"])["clave"] == "gate", "«clave» de un catálogo no se tapa");
    bool fuga = false;
    for (const auto& e : fs::directory_iterator(obj)) {
        std::ifstream f(e.path());
        const std::string s((std::istreambuf_iterator<char>(f)), {});
        fuga = fuga || s.find("abcd-efgh") != std::string::npos;
    }
    CHECK(!fuga, "el secreto no está en ningún objeto");

    const std::size_t objetos = contar(obj);
    const json f2 = fotos.foto(2000);
    CHECK(f2["archivos"] == a1 && contar(obj) == objetos, "foto igual: nada nuevo en objetos");
    CHECK(fotos.comparar(f1, f2).empty(), "sin cambios");

    // Cambios: uno cambia, uno aparece, uno se va. (Que cambie la fecha del archivo.)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    escribir(cfg / "escenas.json", R"({"actual": "Cierre", "escenas": ["Principal", "Cierre"]})");
    escribir(cfg / "nuevo.json", R"({"x": 1})");
    fs::remove(cfg / "personas" / "datos.json");
    const json f3 = fotos.foto(3000);
    const json c = fotos.comparar(f1, f3);
    CHECK(c.size() == 3, "tres archivos distintos");
    bool cambio = false, agregado = false, quitado = false;
    for (const auto& x : c) {
        if (x["archivo"] == "escenas.json" && x["que"] == "cambiado") {
            cambio = x["total"] == 1 && x["cambios"][0]["path"] == "/actual" && x["cambios"][0]["value"] == "Cierre";
        }
        agregado = agregado || (x["archivo"] == "nuevo.json" && x["que"] == "agregado");
        quitado = quitado || (x["archivo"] == "personas/datos.json" && x["que"] == "quitado");
    }
    CHECK(cambio, "el cambio con su JSON Patch");
    CHECK(agregado && quitado, "el que aparece y el que se va");

    // Roto.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    escribir(cfg / "nuevo.json", R"({"x": 1,)");
    const json f4 = fotos.foto(4000);
    CHECK(f4["archivos"].contains("nuevo.json"), "un JSON roto igual entra (como texto)");

    // Volver a una foto: se prepara, no toca Config/ hasta aplicar; lo tapado
    // toma lo de ahora; lo de antes queda respaldado.
    escribir(cfg / "destinos.json", R"({"youtube": {"stream_key": "otra-clave-nueva-0000-1111", "url": "rtmp://otro"}})");
    const json prep = fotos.prepararRestauracion(f1, {}, "Sesion-03");
    CHECK(prep.contains("archivos") && !prep.contains("error"), "preparado");
    CHECK(FotosConfig::restauracionPendiente(cfg)["desde"] == "Sesion-03", "queda pendiente");
    CHECK(fotos.leer(fotos.foto(4500)["archivos"]["escenas.json"]["sha"])["actual"] == "Cierre",
          "preparar no toca Config/ (y .restaurar no entra en la foto)");
    CHECK(!fotos.foto(4600)["archivos"].contains(".restaurar/plan.json"), ".restaurar no es configuración");
    const json ap = FotosConfig::aplicarPendiente(cfg, 5000);
    CHECK(!ap.is_null() && ap["aplicados"].size() >= 3, "aplicado");
    // (Cada lectura en su bloque: en Windows un archivo abierto no se puede borrar.)
    {
        std::ifstream fe(cfg / "escenas.json");
        CHECK(json::parse(fe)["actual"] == "Principal", "escenas como en la foto");
    }
    json d2;
    {
        std::ifstream fd(cfg / "destinos.json");
        d2 = json::parse(fd);
    }
    CHECK(d2["youtube"]["stream_key"] == "otra-clave-nueva-0000-1111", "lo tapado toma lo de ahora");
    CHECK(d2["youtube"]["url"] == "rtmp://a.rtmp.youtube.com/live2", "lo demás, de la foto");
    CHECK(fs::exists(cfg / ".restaurar" / "antes-5000" / "escenas.json"), "lo de antes, respaldado");
    CHECK(FotosConfig::restauracionPendiente(cfg).is_null(), "ya no queda pendiente");
    CHECK(FotosConfig::aplicarPendiente(cfg, 6000).is_null(), "aplicar sin nada no hace nada");

    CHECK(FotosConfig::guardar(raiz / "ev" / "config" / "inicio.json", f4), "guardar");
    CHECK(FotosConfig::cargar(raiz / "ev" / "config" / "inicio.json") == f4, "cargar lo mismo");
    std::error_code ec;
    fs::remove_all(raiz, ec);
    std::printf(g_fallos == 0 ? "test_fotos_config: OK\n" : "test_fotos_config: %d fallos\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
