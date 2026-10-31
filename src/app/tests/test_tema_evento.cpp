// test_tema_evento.cpp — evento::TemaEvento: tema.json, sus archivos y la migración del sitio
//
// Que lo desconocido sobreviva a leer y guardar (también adentro de «colores»), que sin
// tema.json salga el del sitio, que a Tema/ solo entren logos y fuentes de verdad (con
// tope de peso y sin SVG con scripts), y que un sitio generado antes del tema pase a
// /comun/tema.css sin perder lo que alguien cambió a mano.
#include "evento/TemaEvento.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
int fallos = 0;
void check(bool ok, const char* que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que);
    if (!ok) ++fallos;
}
void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}
std::string leer(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

int main() {
    using namespace ba::evento;
    const fs::path raiz = fs::temp_directory_path() / ("ba-test-tema-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(raiz);
    const fs::path ev = raiz / "Evento";
    fs::create_directories(ev);

    // --- Sin nada: no hay tema ---
    check(!TemaEvento::leer(ev).existe, "sin tema.json ni sitio: no hay tema");

    // --- Del sitio ---
    escribir(ev / "Web" / "sitio.json", R"({"titulo":"Foro","color":"#00a86b","fondo":"#101010"})");
    {
        const TemaEvento t = TemaEvento::leer(ev);
        const json p = t.paraPublicar(ev);
        check(t.existe && t.delSitio && t.colores.acento.empty() && p["colores"]["acento"] == "#00a86b" &&
                  p["colores"]["fondo"] == "#101010" && p["marca"]["nombre"] == "Foro",
              "sin tema.json: los colores y el título del sitio (sin copiarlos al tema)");
    }

    // --- Ida y vuelta, con lo desconocido ---
    escribir(ev / "tema.json",
             R"({"modo":"claro","futuro":{"x":1},"colores":{"acento":"#123456","degradado":"#abcdef"},)"
             R"("tipografia":{"texto":"Sora","archivos":[{"familia":"Sora","archivo":"fuente-1.woff2"}]},)"
             R"("forma":{"radio":8},"marca":{"logo":"logo-1.svg"},"alcance":{"control":"todo"}})");
    {
        TemaEvento t = TemaEvento::leer(ev);
        check(t.existe && !t.delSitio && t.modo == "claro" && t.colores.acento == "#123456", "tema.json manda");
        check(t.radio == 8 && t.alcance.control == "todo" && t.alcance.publico == "todo", "radio y alcance");
        check(t.tipografia.archivos.size() == 1 && t.tipografia.archivos[0].peso == "400", "fuente con peso por omisión");
        t.colores.acento = "#654321";
        check(t.guardar(ev), "guarda");
        // Leído y cerrado ya: en Windows un archivo abierto no se puede reemplazar (y abajo se guarda de nuevo).
        const json j = [&] {
            std::ifstream f(ev / "tema.json");
            return json::parse(f);
        }();
        check(j["futuro"]["x"] == 1 && j["colores"]["degradado"] == "#abcdef", "lo desconocido se conserva (también adentro)");
        check(j["colores"]["acento"] == "#654321", "lo cambiado se guarda");
        t.colores.acento.clear();
        check(t.paraPublicar(ev)["colores"]["acento"] == "#00a86b", "sin acento propio: el del sitio");
        check(t.paraPublicar(ev)["colores"]["fondo"] == "#101010", "sin fondo propio: el del sitio");
        t.colores.acento = "#654321";
        t.radio = -1;
        t.guardar(ev);
        check(!TemaEvento::leer(ev).aJson()["forma"].contains("radio"), "radio -1: sin radio");
    }

    // --- Archivos ---
    {
        escribir(raiz / "logo.svg", R"(<svg xmlns="http://www.w3.org/2000/svg"><circle r="4"/></svg>)");
        const RecursoCopiado r = copiarRecurso(ev, raiz / "logo.svg", "logo");
        check(r.error.empty() && r.archivo.rfind("logo-", 0) == 0 && fs::exists(carpetaTema(ev) / r.archivo),
              "un SVG limpio entra con huella en el nombre");
        escribir(raiz / "malo.svg", R"(<svg><script>alert(1)</script></svg>)");
        check(!copiarRecurso(ev, raiz / "malo.svg", "logo").error.empty(), "un SVG con script no entra");
        escribir(raiz / "evento.svg", R"x(<svg xmlns="http://www.w3.org/2000/svg" onload="alert(1)"><circle r="4"/></svg>)x");
        check(!copiarRecurso(ev, raiz / "evento.svg", "logo").error.empty(), "un SVG con onload= no entra");
        escribir(raiz / "fo.svg", R"(<svg><foreignObject><div/></foreignObject></svg>)");
        check(!copiarRecurso(ev, raiz / "fo.svg", "logo").error.empty(), "un SVG con foreignObject no entra");
        escribir(raiz / "js.svg", R"x(<svg><a href="javascript:alert(1)"><circle r="4"/></a></svg>)x");
        check(!copiarRecurso(ev, raiz / "js.svg", "logo").error.empty(), "un SVG con javascript: no entra");
        escribir(raiz / "texto.svg", R"(<svg xmlns="http://www.w3.org/2000/svg"><text font-family="Montserrat">Congreso de la Nación</text></svg>)");
        check(copiarRecurso(ev, raiz / "texto.svg", "logo").error.empty(), "un SVG con texto común (palabras con «on») entra");
        // Un PNG y un WebP de verdad (la firma alcanza) entran con su extensión.
        escribir(raiz / "logo.png", std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\0'));
        const RecursoCopiado png = copiarRecurso(ev, raiz / "logo.png", "logo");
        check(png.error.empty() && png.archivo.size() > 4 && png.archivo.substr(png.archivo.size() - 4) == ".png" &&
                  fs::exists(carpetaTema(ev) / png.archivo),
              "un PNG entra");
        escribir(raiz / "logo.webp", std::string("RIFF") + std::string(4, '\x10') + "WEBPVP8 " + std::string(32, '\0'));
        const RecursoCopiado webp = copiarRecurso(ev, raiz / "logo.webp", "logo");
        check(webp.error.empty() && webp.archivo.size() > 5 && webp.archivo.substr(webp.archivo.size() - 5) == ".webp",
              "un WebP entra");
        escribir(raiz / "riff.webp", std::string("RIFF") + std::string(4, '\x10') + "AVI " + std::string(32, '\0'));
        check(!copiarRecurso(ev, raiz / "riff.webp", "logo").error.empty(), "un RIFF que no es WebP no entra");
        escribir(raiz / "grande.png", std::string("\x89PNG\r\n\x1a\n", 8) + std::string(kTopeLogo, '\0'));
        check(!copiarRecurso(ev, raiz / "grande.png", "logo").error.empty(), "un logo de más de 2 MB no entra");
        check(copiarRecurso(ev, raiz / "logo.png", "logo").archivo == png.archivo, "el mismo archivo, el mismo nombre (huella)");
        escribir(raiz / "trucho.png", "MZ esto es un exe");
        check(!copiarRecurso(ev, raiz / "trucho.png", "logo").error.empty(), "un .png que no es PNG no entra");
        escribir(raiz / "logo.gif", "GIF89a");
        check(!copiarRecurso(ev, raiz / "logo.gif", "logo").error.empty(), "GIF no");
        escribir(raiz / "f.woff2", std::string("wOF2") + std::string(100, 'x'));
        const RecursoCopiado f = copiarRecurso(ev, raiz / "f.woff2", "fuente");
        check(f.error.empty() && f.archivo.rfind("fuente-", 0) == 0, "una WOFF2 de verdad entra");
        escribir(raiz / "grande.woff2", std::string("wOF2") + std::string(kTopeFuente, 'x'));
        check(!copiarRecurso(ev, raiz / "grande.woff2", "fuente").error.empty(), "tope de peso");

        TemaEvento t = TemaEvento::leer(ev);
        t.marca.logo = r.archivo;
        t.tipografia.archivos.clear();
        escribir(carpetaTema(ev) / "a-mano.png", "x");
        check(limpiarRecursos(ev, t) == 4 && !fs::exists(carpetaTema(ev) / png.archivo) && !fs::exists(carpetaTema(ev) / f.archivo) &&
                  fs::exists(carpetaTema(ev) / r.archivo) && fs::exists(carpetaTema(ev) / "a-mano.png"),
              "limpiar: se van la fuente y los logos que ya no se usan; el logo y lo puesto a mano quedan");
    }

    // --- Migrar un sitio generado antes del tema ---
    {
        const fs::path web = ev / "Web";
        escribir(web / "portada.html",
                 "<!doctype html>\n<html lang=\"es\">\n<head>\n  <link rel=\"stylesheet\" href=\"/evento/_/sitio.css\" />\n"
                 "  <link rel=\"stylesheet\" href=\"estilo.css\" />\n</head><body></body></html>\n");
        escribir(web / "estilo.css", ":root {\n  --accent: #00a86b;\n  --bg: #222222;\n}\n");
        const std::vector<std::string> c = migrarSitio(web);
        const std::string h = leer(web / "portada.html"), css = leer(web / "estilo.css");
        check(c.size() == 2, "cambian la página y el estilo");
        check(h.find("<html data-ev=\"publico\" lang=\"es\">") != std::string::npos, "la página dice que es del público");
        check(h.find("/comun/tema.css") < h.find("href=\"estilo.css\"") && h.find("/comun/tema.js") != std::string::npos,
              "el tema va antes del estilo del evento");
        check(css.find("--accent") == std::string::npos, "el acento que escribió la plantilla se va");
        check(css.find("--bg: #222222;") != std::string::npos && css.find(":root, html[data-ev] {") != std::string::npos,
              "el fondo cambiado a mano se queda, y le gana al tema");
        check(migrarSitio(web).empty(), "dos veces no cambia nada");
    }

    fs::remove_all(raiz);
    std::printf("%s\n", fallos == 0 ? "TODO OK" : "HAY FALLAS");
    return fallos == 0 ? 0 : 1;
}
