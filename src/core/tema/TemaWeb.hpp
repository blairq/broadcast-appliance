// ============================================================================
// src/core/tema/TemaWeb.hpp — el tema del evento, hecho CSS (docs/planes/PLAN-TEMA-EVENTO.md)
//
// El evento tiene su cara: acento, fondo, tipografía, logo. Lo guarda la app
// (evento::TemaEvento, en Eventos/<x>/tema.json) y lo pinta el gateway en
// /comun/tema.css para TODAS las páginas web. Esto es lo del medio: de lo que
// escribió una persona (o el asistente) a valores listos, y de ahí a la hoja.
//
// TODO SE CALCULA ACÁ, NO EN EL NAVEGADOR. Nada de color-mix() ni de calcular el
// contraste en JavaScript: los Chromecast y los televisores viejos no lo saben
// hacer, y una página de la sala no puede quedar ilegible porque el televisor
// del hall tiene un Chrome de 2019. El texto encima del acento sale de comparar
// el contraste WCAG del blanco y del negro; lo que falta (superficie, texto
// suave, borde) se mezcla de lo que hay.
//
// En el núcleo (sin Qt): lo usa el gateway para servir la hoja y la app para
// mostrar en el panel los mismos avisos de contraste que va a tener la página.
//
// LOS DIALECTOS. Las páginas no hablan todas igual: las consolas (neuro) dicen
// --acento/--foco/--fondo, la sala (sesión, registro, público, prompter) dice
// --acento/--fondo/--texto/--panel, la de invitados y el sitio dicen
// --accent/--bg/--fg/--text, el marco del evento dice --color. La hoja define
// los tokens propios (--ev-*) y los «puentes» a cada dialecto, así una página
// no se reescribe: alcanza con que diga quién es en <html data-ev="…">.
// ============================================================================
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::tema {

/// Un color sin alfa, cada canal de 0 a 1.
struct Rgb {
    double r = 0, g = 0, b = 0;
};

/// «#rgb» o «#rrggbb» (con o sin #, mayúsculas o minúsculas). Lo demás: nada.
std::optional<Rgb> leerColor(const std::string& texto);
/// «#rrggbb» en minúsculas.
std::string hex(const Rgb& c);
/// «rgba(r, g, b, a)».
std::string rgba(const Rgb& c, double alfa);
/// La luminancia relativa de WCAG 2.x (0 negro, 1 blanco).
double luminancia(const Rgb& c);
/// El contraste WCAG entre dos colores (de 1 a 21). 4.5 es lo mínimo para texto común.
double contraste(const Rgb& a, const Rgb& b);
/// `a` corrido hacia `b` en la proporción `t` (0 = a, 1 = b), canal por canal.
Rgb mezclar(const Rgb& a, const Rgb& b, double t);

/// Los niveles del alcance: qué toma una página del tema del evento.
///   nada   — nada (la página queda como es)
///   acento — el color de acción, el foco y el logo
///   todo   — además fondo, superficies, texto, tipografía y forma
enum class Nivel { Nada, Acento, Todo };
Nivel nivelDe(const std::string& texto, Nivel siNoVale);
const char* nombreDe(Nivel n);

/// Los nombres de archivo que se aceptan en Tema/: minúsculas, números, «-», «_» y un
/// punto antes de la extensión, hasta 80 letras. Sin barras: nada fuera de la carpeta.
bool archivoValido(const std::string& nombre);
/// Las extensiones de cada cosa.
bool esImagenDeMarca(const std::string& nombre); ///< .svg .png .webp
bool esFuente(const std::string& nombre);        ///< .woff2 .woff .ttf .otf

/// El tema tal como lo escribió alguien (lo de tema.json, ver evento::TemaEvento) con
/// TODO calculado y saneado, listo para la hoja:
///   {nombre, modo, colores{acento, acentoTexto, fondo, superficie, texto, textoSuave,
///    superficie2, hundido, textoTenue, acento2, borde, bordeFuerte, foco, acentoSuave,
///    acentoMedio, acentoFuerte, acentoNada, fondoVelo, tinte, tinteFuerte, campo, velo, sombra},
///    tipografia{texto, titulos, archivos[{familia, archivo,
///    peso, estilo}]}, forma{radio}, marca{nombre, logo, logoClaro, favicon},
///    alcance{publico, control}, contraste{acento, texto, acentoSobreFondo},
///    avisos[{codigo: texto|acentoTexto|acentoFondo, contraste, minimo}]}
/// Un color que no se entiende se toma como no puesto; un archivo con nombre raro, igual.
/// Los avisos van con código y números: el texto lo pone quien los muestra, en su idioma.
nlohmann::json resolver(const nlohmann::json& crudo);

/// La hoja /comun/tema.css de un tema ya resuelto. `recursos`: de dónde se sirven los
/// archivos de Tema/ (p. ej. "/comun/tema/"); `version`: va en sus URL (?v=) para que un
/// cambio de fuente no quede en la caché. Sin tema (`resuelto` vacío): una hoja sin reglas.
std::string hojaCss(const nlohmann::json& resuelto, const std::string& recursos, const std::string& version);

/// Los colores de un QR con la cara del evento (qr::Estilo por omisión, ya resuelto el
/// tema): {primario, fondo}. Un QR se lee con módulos oscuros sobre fondo claro y buen
/// contraste: el acento si es bien más oscuro que el fondo (4.5), si no el texto, si no
/// negro; el fondo del evento si es claro, si no blanco. Sin tema: negro sobre blanco.
nlohmann::json coloresQr(const nlohmann::json& resuelto);

/// Una imagen de marca de Tema/ (svg, png o webp, hasta 2 MB) como «data:…;base64,…», para
/// incrustarla (el logo del evento al centro de un QR). "" si no es eso o no se puede leer.
std::string imagenComoDataUrl(const std::filesystem::path& archivo);

/// Una huella corta y estable de un texto (FNV-1a de 64 bits, 12 hex): la versión de la hoja.
std::string huella(const std::string& texto);

} // namespace ba::tema
