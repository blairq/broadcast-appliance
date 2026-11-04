// ============================================================================
// src/core/i18n/I18nIdioma.hpp — el idioma del proceso, otro idioma y sus archivos
//
// Elegir el idioma del operador (`usar`, `resolver`, `idiomaDeLaMaquina`), dónde están
// los perfiles y los catálogos, traducir en OTRO idioma (`En`: el del evento o el de un
// invitado), los archivos largos de un idioma (prompts, ayuda, guías), los plurales y el
// pseudo-idioma. Sin el perfil de un idioma (`Perfil`, en I18nPerfiles.hpp): tocarlo no
// recompila a quien solo elige o traduce. Trae lo de traducir (I18nTexto.hpp), así un
// archivo incluye siempre una sola parte. El cuadro completo, en I18n.hpp.
// ============================================================================
#pragma once

#include "i18n/I18nTexto.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ba::i18n {

/// Dónde están los perfiles y los catálogos. Por defecto, los del bundle
/// (`data/idiomas`, `data/i18n`) y, si no están, los de la compilación (tests,
/// correr desde el build). Un test puede apuntar a otras carpetas.
void carpetas(const std::filesystem::path& perfiles, const std::filesystem::path& catalogos);
std::filesystem::path carpetaPerfiles();
std::filesystem::path carpetaCatalogos();

/// El idioma de este proceso: `BA_IDIOMA` si está (pruebas, un proceso a mano); si
/// no, el guardado en launcher.json (`"idioma"`, de la máquina); si no o si es "",
/// el del sistema (`elegirDelSistema`).
std::string idiomaDeLaMaquina(const std::filesystem::path& datos);
/// El primero de los idiomas preferidos del sistema que hay (exacto, "es-AR"; o por la
/// lengua, "es"); si ninguno, el inglés si está; si no, el original.
std::string elegirDelSistema(const std::vector<std::string>& preferidos);
/// Lo mismo para un código cualquiera (uno guardado que ya no existe → el del sistema).
std::string resolver(const std::string& pedido);

/// El idioma del operador para este proceso (carga su catálogo y sus respaldos).
void usar(const std::string& codigo);

/// Lo mismo que traducir() en otro idioma (el del evento, el de un invitado), que carga su
/// catálogo la primera vez: `ba::i18n::En(evento.idioma).traducir("graficos", "En vivo")`. Un
/// objeto y no un argumento más porque lupdate reconoce `traducir(contexto, texto…)`
/// con esos argumentos en ese orden.
class En {
public:
    explicit En(std::string codigo) : codigo_(std::move(codigo)) {}
    std::string traducir(const char* contexto, const char* texto, const char* desambiguacion = nullptr,
                         int n = -1) const;
    /// Los archivos de ese idioma (ver ba::i18n::archivo).
    std::filesystem::path archivo(const std::string& relativo) const;
    std::string leer(const std::string& relativo) const;
    const std::string& codigo() const { return codigo_; }

private:
    std::string codigo_;
};

/// Los archivos de un idioma, para lo que es largo (los prompts del asistente, la ayuda
/// del MCP, las guías y plantillas de eventos): `<perfiles>/<código>/<relativo>`, en la
/// fuente `assets/idiomas/es/prompts/oficina.md`. El del idioma del operador; si no lo
/// tiene, el de sus respaldos y al final el del original. Vacía si no está en ninguno.
/// El pseudo-idioma no tiene archivos: da los del original.
std::filesystem::path archivo(const std::string& relativo);
/// Su contenido ("" si no está en ningún idioma).
std::string leer(const std::string& relativo);

/// Qué forma del plural corresponde a n según la regla (la misma numeración que
/// las <numerusform> de un .ts). Reglas: uno-otro, frances, sin-plural, eslavo,
/// polaco, checo, arabe. Una desconocida: uno-otro.
int formaPlural(const std::string& regla, long long n);
/// Cuántas formas tiene la regla.
int formasDePlural(const std::string& regla);

/// El pseudo-idioma: «Conferencia» → «⟦Çöñfërëñçïä····⟧», con acentos en todas las
/// letras y un 40 % más largo. Respeta %1/%n/%L1, las etiquetas <b>…</b> y las
/// entidades &amp;. Lo que se ve sin ⟦ ⟧ no pasó por la traducción; lo que se corta
/// no aguanta un idioma más largo.
std::string pseudo(const std::string& texto);

} // namespace ba::i18n
