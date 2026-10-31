// test_tema_web.cpp — tema/TemaWeb: del tema del evento a la hoja /comun/tema.css
//
// Que el contraste sea el de WCAG (no uno inventado), que lo que falta se calcule,
// que lo raro no llegue a la hoja (un nombre de fuente con llaves, un archivo con
// barras) y que cada familia de páginas reciba lo que dice el alcance.
#include "tema/TemaWeb.hpp"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
int fallos = 0;
void check(bool ok, const char* que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que);
    if (!ok) ++fallos;
}
bool tiene(const std::string& s, const std::string& que) { return s.find(que) != std::string::npos; }
} // namespace

int main() {
    using namespace ba::tema;
    using json = nlohmann::json;

    // --- Colores ---
    check(leerColor("#fff").has_value() && hex(*leerColor("#fff")) == "#ffffff", "#rgb se entiende");
    check(leerColor("FF4D2E").has_value() && hex(*leerColor("FF4D2E")) == "#ff4d2e", "sin # y en mayúsculas");
    check(!leerColor("rojo") && !leerColor("#12345") && !leerColor("url(x)"), "lo demás no es un color");
    check(std::fabs(contraste(*leerColor("#000"), *leerColor("#fff")) - 21.0) < 0.01, "negro sobre blanco: 21");
    check(std::fabs(contraste(*leerColor("#777"), *leerColor("#fff")) - 4.48) < 0.02, "#777 sobre blanco: 4.48 (WCAG)");
    check(hex(mezclar(*leerColor("#000"), *leerColor("#fff"), 0.5)) == "#808080", "mezcla a la mitad");

    // --- Archivos ---
    check(archivoValido("logo-1a2b.svg") && !archivoValido("../logo.svg") && !archivoValido("a/b.png") &&
              !archivoValido("Logo.svg") && !archivoValido(".svg") && !archivoValido("a.b.svg"),
          "solo nombres planos de Tema/");
    check(esImagenDeMarca("logo.webp") && !esImagenDeMarca("logo.gif") && esFuente("texto.woff2") && !esFuente("x.svg"),
          "imágenes y fuentes por extensión");

    // --- Resolver ---
    {
        const json r = resolver(json::object());
        check(r["modo"] == "oscuro" && r["colores"]["fondo"] == "#0b0b0c" && r["colores"]["acento"] == "#ff4d2e",
              "vacío: el de fábrica del sitio");
        check(r["alcance"]["publico"] == "todo" && r["alcance"]["control"] == "acento", "alcance por omisión");
        check(r["avisos"].empty(), "el de fábrica no tiene avisos");
    }
    {
        const json r = resolver({{"colores", {{"fondo", "#ffffff"}, {"acento", "#ffe600"}}}});
        check(r["modo"] == "claro", "un fondo claro sugiere el modo claro");
        check(r["colores"]["acentoTexto"] == "#0b0b0c", "sobre amarillo, texto oscuro");
        bool avisaFondo = false;
        for (const json& a : r["avisos"]) avisaFondo |= a["codigo"] == "acentoFondo";
        check(avisaFondo, "amarillo sobre blanco: avisa que el acento se confunde con el fondo");
    }
    {
        const json r = resolver({{"colores", {{"acento", "#1a237e"}}}});
        check(r["colores"]["acentoTexto"] == "#ffffff", "sobre azul oscuro, texto blanco");
    }
    {
        const json r = resolver({{"colores", {{"fondo", "#202020"}, {"texto", "#303030"}}}});
        check(!r["avisos"].empty() && r["avisos"][0]["codigo"] == "texto", "texto ilegible: avisa");
    }
    {
        const json r = resolver({{"tipografia",
                                  {{"texto", "Mi \"Fuente\"; } body{"},
                                   {"archivos", json::array({{{"familia", "Mi Fuente"}, {"archivo", "../x.woff2"}},
                                                             {{"familia", "Mi Fuente"}, {"archivo", "mi-fuente.woff2"},
                                                              {"peso", "100 900"}}})}}},
                                 {"marca", {{"logo", "../../etc/passwd"}, {"favicon", "fav.png"}}},
                                 {"alcance", {{"publico", "acento"}, {"control", "cualquiera"}}}});
        check(r["tipografia"]["texto"] == "Mi Fuente body", "el nombre de la fuente pierde lo que rompe la hoja");
        check(r["tipografia"]["archivos"].size() == 1 && r["tipografia"]["archivos"][0]["peso"] == "100 900",
              "un archivo de fuente con ruta rara no entra");
        check(r["marca"]["logo"] == "" && r["marca"]["favicon"] == "fav.png", "un logo con ruta rara no entra");
        check(r["alcance"]["publico"] == "acento" && r["alcance"]["control"] == "acento", "alcance raro: el de omisión");
    }

    // --- La hoja ---
    {
        const json r = resolver({{"colores", {{"acento", "#00a86b"}}},
                                 {"tipografia", {{"texto", "Señal"}, {"archivos", json::array({{{"familia", "Señal"}, {"archivo", "senal.woff2"}}})}}},
                                 {"forma", {{"radio", 99}}}});
        const std::string css = hojaCss(r, "/comun/tema/", "abc123");
        check(tiene(css, "--ev-acento: #00a86b;"), "el token del acento");
        check(tiene(css, "html[data-ev~=\"publico\"]") && tiene(css, "--bg: var(--ev-fondo)"), "el público: todo");
        check(tiene(css, "html[data-ev~=\"control\"] {\n  --acento: var(--ev-acento)"), "las consolas: el acento");
        const std::size_t ctl = css.find("html[data-ev~=\"control\"]");
        const std::string bloqueControl = ctl == std::string::npos ? std::string() : css.substr(ctl, css.find('}', ctl) - ctl);
        check(!bloqueControl.empty() && !tiene(bloqueControl, "--fondo:"), "las consolas no reciben el fondo por omisión");
        check(tiene(css, "url(\"/comun/tema/senal.woff2?v=abc123\") format(\"woff2\")"), "la fuente, versionada");
        check(tiene(css, "--ev-radio: 32px;"), "el radio, acotado");
        check(!tiene(css, "color-mix"), "nada de color-mix (televisores viejos)");
        check(tiene(css, "html[data-ev~=\"publico\"] body { font-family: var(--ev-fuente); }"), "la letra pisa la de la página");
        check(tiene(css, "--tinte: var(--ev-tinte);") && tiene(css, "--campo: var(--ev-campo);") &&
                  tiene(css, "--sombra: var(--ev-sombra);") && tiene(css, "--borde-fuerte: var(--ev-borde-fuerte);"),
              "los resaltes, campos y sombras que las páginas tenían fijos, puenteados");
        check(!tiene(bloqueControl, "--tinte:"), "las consolas (solo acento) no los reciben");
    }
    {
        // Con un tema claro, lo que estaba fijo para fondo oscuro se da vuelta: el resalte es
        // del color del texto (oscuro) y la sombra, más suave.
        const json oscuro = resolver(json::object()), claro = resolver({{"colores", {{"fondo", "#ffffff"}}}});
        check(claro["colores"]["tinte"] == rgba(*leerColor(claro["colores"]["texto"].get<std::string>()), 0.06),
              "claro: el resalte, del color del texto");
        check(oscuro["colores"]["sombra"] == "rgba(0, 0, 0, 0.45)" && claro["colores"]["sombra"] == "rgba(0, 0, 0, 0.14)",
              "la sombra, según el modo");
        check(claro["colores"]["campo"] != oscuro["colores"]["campo"], "el campo hundido, según el modo");
    }
    {
        const json r = resolver({{"alcance", {{"publico", "nada"}, {"control", "todo"}}}});
        const std::string css = hojaCss(r, "/comun/tema/", "");
        check(!tiene(css, "html[data-ev~=\"publico\"]"), "alcance nada: sin reglas para esa familia");
        check(tiene(css, "html[data-ev~=\"control\"] {\n  --acento") && tiene(css, "--superficie-plana"), "consolas con todo");
    }
    check(!tiene(hojaCss(json::object(), "/comun/tema/", ""), "html["), "sin tema: hoja sin reglas");
    // Con un idioma de coma decimal (la app corre así en castellano): la hoja sigue con punto.
    if (std::setlocale(LC_ALL, "es_AR.UTF-8") || std::setlocale(LC_ALL, "de_DE.UTF-8")) {
        const std::string css = hojaCss(resolver({{"colores", {{"acento", "#00a86b"}}}}), "/comun/tema/", "");
        check(tiene(css, "rgba(0, 168, 107, 0.") && !tiene(css, "rgba(0, 168, 107, 0,"), "decimales con punto aunque el idioma use coma");
        std::setlocale(LC_ALL, "C");
    }
    check(huella("a") != huella("b") && huella("a").size() == 12, "la huella cambia y mide 12");

    std::printf("%s\n", fallos == 0 ? "TODO OK" : "HAY FALLAS");
    return fallos == 0 ? 0 : 1;
}
