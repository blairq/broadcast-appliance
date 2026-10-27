// ============================================================================
// src/core/i18n/I18n.hpp — los idiomas del sistema (docs/planes/PLAN-I18N.md)
//
// Un idioma es una carpeta de recursos, nunca código: su PERFIL
// (`assets/idiomas/<código>.json` → `data/idiomas/`) y su CATÁLOGO
// (`i18n/ba_<código>.ts` → `tools/i18n/ts-a-json.py` → `data/i18n/<código>.json`).
// Los idiomas son los perfiles que hay: sumar uno no toca este archivo.
//
// El texto ORIGINAL es el español con el que está escrito el código: lo que no
// tiene traducción en el idioma pedido (ni en sus respaldos) sale tal cual. Por eso
// el español no tiene catálogo.
//
// Lo usan los tres programas (launcher, ba-app, ba-gateway), sin Qt: el adaptador
// de Qt (`qsTr`, `QObject::tr`) está en la app (arranque/TraductorQt) y le pregunta
// a esto. La web pide los mismos catálogos al gateway (/comun/i18n/).
//
// Dos idiomas a la vez en el mismo proceso: el del OPERADOR (`usar`, `traducir`) y el
// del EVENTO o el del invitado (`En`), que puede ser otro.
//
// Marcar un texto para traducir (lo encuentra `make i18n-actualizar`, con lupdate):
//   ba::i18n::traducir("contexto", "Texto en español")
//   ba::i18n::traducir("contexto", "Se conectó %n invitado", nullptr, n)   // plural
//   ba::i18n::llenar(ba::i18n::traducir("ctx", "Grabando %1 en %2"), {nombre, disco})
// Nunca una frase armada con pedazos: el orden de las palabras cambia con el idioma.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// Marca un texto de una tabla que se arma una vez sin traducirlo (lo encuentra lupdate) y se
// traduce al mostrarlo con traducir(contexto, texto). La misma definición que Qt: el código
// sin Qt la usa igual y no choca si después se incluye Qt.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::i18n {

/// El perfil de un idioma: lo que cambia entre idiomas, en datos.
struct Perfil {
    std::string codigo;        ///< BCP 47: "es", "en", "pt-BR"
    std::string nombre;        ///< en ese idioma: "Español", "English", "Português (Brasil)"
    std::string locale;        ///< el de los formatos (números, fechas) si el sistema no dice otro: "es_AR"
    bool derechaAIzquierda = false;
    std::string plural = "uno-otro"; ///< la regla de plurales (ver formaPlural)
    std::vector<std::string> respaldo; ///< a qué idiomas recurrir antes que al original
    bool fuente = false;       ///< el idioma en que está escrito el código (no tiene catálogo)
    bool pseudo = false;       ///< el de pruebas: transforma el original (ver pseudo)
    std::string tipografia;    ///< la familia para esta escritura ("" = la de siempre)
    nlohmann::json datos;      ///< el perfil entero (voz, tótem… los leen sus módulos)
};

/// La cobertura de un idioma: cuántos textos del catálogo están traducidos.
struct Cobertura {
    int total = 0;
    int traducidos = 0;
    /// 0..1; el original y el pseudo-idioma están completos por definición.
    double fraccion() const { return total <= 0 ? 1.0 : static_cast<double>(traducidos) / total; }
};

/// Dónde están los perfiles y los catálogos. Por defecto, los del bundle
/// (`data/idiomas`, `data/i18n`) y, si no están, los de la compilación (tests,
/// correr desde el build). Un test puede apuntar a otras carpetas.
void carpetas(const std::filesystem::path& perfiles, const std::filesystem::path& catalogos);
std::filesystem::path carpetaPerfiles();
std::filesystem::path carpetaCatalogos();

/// Todos los idiomas que hay (el original primero; el resto por código).
std::vector<Perfil> idiomas();
/// El perfil de ese código, o uno vacío (codigo == "") si no existe.
Perfil perfil(const std::string& codigo);
Cobertura cobertura(const std::string& codigo);

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
/// El que se está usando (el original si nunca se llamó a usar).
const Perfil& actual();

/// El texto en el idioma del operador. `desambiguacion` es la de lupdate (dos
/// textos iguales con sentidos distintos: «Salida» de audio y de la sala). Con
/// `n >= 0` elige la forma del plural y reemplaza %n por n.
std::string traducir(const char* contexto, const char* texto, const char* desambiguacion = nullptr, int n = -1);
/// Lo mismo en otro idioma (el del evento, el de un invitado), que carga su catálogo
/// la primera vez: `ba::i18n::En(evento.idioma).traducir("graficos", "En vivo")`. Un
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

/// El idioma del EVENTO abierto en este proceso: lo que ven y escuchan el público y los
/// invitados (gráficos, placas, tótems, voz, la web, el acta). Lo fija la app al abrir el
/// evento (y al cambiarle el idioma) y el gateway con el estado que le manda la app.
/// Vacío o sin fijar = el del operador.
void usarEnEvento(const std::string& codigo);
std::string idiomaDelEvento();
/// traducir() en el idioma del evento abierto (lupdate la reconoce igual):
/// `ba::i18n::traducirEvento("graficos", "Volvemos enseguida")`.
std::string traducirEvento(const char* contexto, const char* texto, const char* desambiguacion = nullptr, int n = -1);

/// Los archivos de un idioma, para lo que es largo (los prompts del asistente, la ayuda
/// del MCP, las guías y plantillas de eventos): `<perfiles>/<código>/<relativo>`, en la
/// fuente `assets/idiomas/es/prompts/oficina.md`. El del idioma del operador; si no lo
/// tiene, el de sus respaldos y al final el del original. Vacía si no está en ninguno.
/// El pseudo-idioma no tiene archivos: da los del original.
std::filesystem::path archivo(const std::string& relativo);
/// Su contenido ("" si no está en ningún idioma).
std::string leer(const std::string& relativo);

/// Llena %1, %2… (hasta %9) con los valores, en el orden en que los diga la
/// traducción. Un %n que quedó se deja (lo llena traducir).
std::string llenar(const std::string& plantilla, std::initializer_list<std::string> valores);

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
