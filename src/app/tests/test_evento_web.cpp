// ============================================================================
// src/app/tests/test_evento_web.cpp — la página web del evento en vivo
//
//   1. Generar escribe portada, una página por sección, cierre, estilo y sitio.json.
//   2. Regenerar no pisa lo editado (salvo sobrescribir).
//   3. El subdominio sale del nombre (sin acentos, sin «_», con guiones).
//   4. El estado público: la página de la sección actual, sin cues ni notas.
//   5. resolverPagina: por nombre, id, número; nada fuera de Web/.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "evento/EventoWeb.hpp"

namespace fs = std::filesystem;
using namespace ba::evento;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)
std::string leer(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ba-evento-web-" + std::to_string(::time(nullptr)));
    fs::remove_all(dir);
    Evento e;
    e.nombre = "Entrevista_Pepito Ñandú";
    e.fecha = "2026-10-15";
    e.inicio = "19:00";
    Bloque a; a.id = "b1"; a.nombre = "Presentación"; a.nota = "NOTA SECRETA DEL OPERADOR";
    Cue c; c.texto = "Cortar a la cámara 2 (instrucción interna)"; c.duracionSeg = 300; a.cues.push_back(c);
    Bloque b; b.id = "b2"; b.nombre = "Preguntas <del público>";
    e.bloques = {a, b};

    // 1
    ResultadoWeb r = generar(e, dir, false);
    CHECK(r.error.empty(), "sin error");
    for (const char* f : {"portada.html", "seccion-b1.html", "seccion-b2.html", "cierre.html", "estilo.css", "sitio.json"})
        CHECK(fs::exists(dir / f), f);
    const std::string p1 = leer(dir / "seccion-b1.html");
    CHECK(p1.find("Presentación") != std::string::npos, "la sección lleva su nombre");
    CHECK(p1.find("/evento/_/vivo.js") != std::string::npos, "carga vivo.js");
    CHECK(p1.find("NOTA SECRETA") == std::string::npos && p1.find("instrucción interna") == std::string::npos,
          "ni notas ni cues en la página pública");
    CHECK(leer(dir / "seccion-b2.html").find("&lt;del público&gt;") != std::string::npos, "nombres escapados");

    // 2
    { std::ofstream(dir / "portada.html") << "MIA"; }
    r = generar(e, dir, false);
    CHECK(leer(dir / "portada.html") == "MIA", "no pisa lo editado");
    CHECK(!r.salteadas.empty(), "dice qué salteó");
    generar(e, dir, true);
    CHECK(leer(dir / "portada.html") != "MIA", "sobrescribir sí pisa");

    // 3
    CHECK(subdominioDe("Entrevista_Pepito Ñandú") == "entrevista-pepito-nandu", "subdominio");
    CHECK(subdominioDe("  --Ánimo!! 2026  ") == "animo-2026", "subdominio con basura");
    CHECK(SitioWeb::leer(dir, e).subdominio == "entrevista-pepito-nandu", "sitio.json trae el subdominio del nombre");

    // 4
    const SitioWeb s = SitioWeb::leer(dir, e);
    nlohmann::json est = estadoPublico(e, s, "en_vivo", 1, 0, 1000, 2000);
    CHECK(est["pagina"] == "seccion-b2.html", "página de la sección actual");
    CHECK(est["secciones"][0]["estado"] == "hecha" && est["secciones"][1]["estado"] == "actual", "estados de la agenda");
    CHECK(est.dump().find("SECRETA") == std::string::npos && est.dump().find("interna") == std::string::npos, "nada privado");
    CHECK(estadoPublico(e, s, "proximo", -1, 0, 0, 0)["pagina"] == "portada.html", "antes: portada");
    CHECK(estadoPublico(e, s, "terminado", -1, 0, 0, 0)["pagina"] == "cierre.html", "después: cierre");

    // 5
    CHECK(resolverPagina(e, "Presentación") == "seccion-b1.html", "por nombre");
    CHECK(resolverPagina(e, "2") == "seccion-b2.html", "por número");
    CHECK(resolverPagina(e, "portada") == "portada.html", "portada");
    CHECK(resolverPagina(e, "../../etc/passwd").empty(), "nada fuera de Web/");
    CHECK(resolverPagina(e, "sub/x.html").empty(), "sin carpetas");
    CHECK(resolverPagina(e, "extra.html") == "extra.html", "un .html propio");

    fs::remove_all(dir);
    std::printf(g_fallos ? "test_evento_web: %d fallos\n" : "test_evento_web: OK\n", g_fallos);
    return g_fallos ? 1 : 0;
}
