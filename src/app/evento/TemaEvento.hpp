// ============================================================================
// src/app/evento/TemaEvento.hpp — la cara del evento en todas las páginas web
//
// Cada evento tiene su tema: el acento, el fondo, la tipografía, el logo y el
// nombre que ve la gente en todas las páginas del sistema (la del evento, la de
// invitados, la sesión remota, las consultas del público, el tótem, las
// pantallas) y, con el alcance que se elija, en las consolas de los operadores.
// Ver docs/planes/PLAN-TEMA-EVENTO.md.
//
//   <evento>/tema.json   lo de abajo (claves desconocidas, también adentro, se conservan)
//   <evento>/Tema/       el logo y las fuentes (nombres que cambian con el contenido:
//                        «logo-1a2b3c4d5e6f.svg»; así el navegador no se queda con el viejo)
//
// NO VA EN evento.json: Evento::aJson pierde lo que no conoce. Ni en Web/sitio.json:
// el tema existe aunque el evento no tenga sitio (la sesión remota igual tiene cara).
// Si no hay tema.json pero hay sitio, se arma con sus colores (el sitio los escribía
// una vez en Web/estilo.css; `migrarSitio` pasa esas páginas a /comun/tema.css).
//
// Esto es el modelo: leer, guardar, validar y copiar los archivos. Qué colores salen
// (el contraste, lo que falta) lo calcula ba::tema::resolver, que usa también el gateway.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::evento {

struct TemaEvento {
    /// "oscuro" | "claro" | "" (según el fondo).
    std::string modo;
    struct Colores {
        std::string acento, acentoTexto, fondo, superficie, texto, textoSuave; ///< "#rrggbb" o "" (se calcula)
    } colores;
    struct Fuente {
        std::string familia, archivo, peso = "400", estilo = "normal";
    };
    struct Tipografia {
        std::string texto, titulos; ///< familias ("" = la de cada página)
        std::vector<Fuente> archivos;
    } tipografia;
    int radio = -1; ///< px de las esquinas; -1 = el de cada página
    struct Marca {
        std::string nombre, logo, logoClaro, favicon; ///< archivos de Tema/
    } marca;
    struct Alcance {
        std::string publico = "todo";   ///< nada | acento | todo
        std::string control = "acento"; ///< las consolas: por omisión solo el acento
    } alcance;
    /// Lo que se leyó, entero: aJson escribe encima, así lo desconocido no se pierde.
    nlohmann::json resto = nlohmann::json::object();
    /// Hay tema propio (tema.json, o los colores del sitio). Sin tema las páginas quedan como son.
    bool existe = false;
    /// Vino del sitio (no hay tema.json todavía).
    bool delSitio = false;

    static TemaEvento deJson(const nlohmann::json& j);
    nlohmann::json aJson() const;

    /// El de la carpeta del evento: tema.json; si no está, el que sale de Web/sitio.json.
    static TemaEvento leer(const std::filesystem::path& carpetaEvento);
    /// Escribe tema.json (atómico). No toca Tema/.
    bool guardar(const std::filesystem::path& carpetaEvento) const;
    /// Lo que se publica (lo que resuelve ba::tema::resolver): aJson(), con el acento y el
    /// fondo de Web/sitio.json donde el tema no dice nada. Así el color que se elige para el
    /// sitio (evento_web sitio, la página Guion → Página web) sigue mandando mientras el tema
    /// no fije el suyo, y no hay dos colores que se contradicen. Vacío si no hay tema.
    nlohmann::json paraPublicar(const std::filesystem::path& carpetaEvento) const;

    /// Los archivos que nombra (logo, logoClaro, favicon, fuentes).
    std::vector<std::string> archivos() const;
};

inline std::filesystem::path carpetaTema(const std::filesystem::path& carpetaEvento) { return carpetaEvento / "Tema"; }

/// Los topes de peso (un logo de 40 MB tarda en el celular de la gente de afuera).
constexpr std::uintmax_t kTopeLogo = 2u * 1024 * 1024;
constexpr std::uintmax_t kTopeFuente = 4u * 1024 * 1024;

struct RecursoCopiado {
    std::string archivo; ///< el nombre en Tema/ («logo-1a2b3c4d5e6f.svg»), vacío si no se pudo
    std::string error;   ///< el original en castellano: traducir("evento", error) al mostrarlo
};

/// Copia a Tema/ un logo (`tipo` "logo": svg, png o webp) o una fuente ("fuente": woff2, woff,
/// ttf, otf), mirando la extensión, el peso y que adentro sea lo que dice ser (la firma del
/// archivo; un SVG con <script> o <foreignObject> no entra). El nombre lleva una huella del
/// contenido. No cambia el tema: eso lo hace quien llama.
RecursoCopiado copiarRecurso(const std::filesystem::path& carpetaEvento, const std::filesystem::path& origen,
                             const std::string& tipo);

/// Borra de Tema/ lo que el tema ya no nombra (un logo reemplazado). Devuelve cuántos.
int limpiarRecursos(const std::filesystem::path& carpetaEvento, const TemaEvento& t);

/// Pasa un sitio ya generado (Web/) al tema común: cada .html enlaza /comun/tema.css antes de
/// estilo.css y dice <html data-ev="publico">, y estilo.css deja de fijar --accent/--bg con lo
/// que escribía la plantilla vieja. Lo que la persona cambió a mano no se toca. Devuelve los
/// archivos cambiados (relativos a Web/).
std::vector<std::string> migrarSitio(const std::filesystem::path& carpetaWeb);

} // namespace ba::evento
