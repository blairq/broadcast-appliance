// test_i18n.cpp — los idiomas (i18n/I18n): perfiles, catálogos, respaldos, plurales, pseudo
//
// Con carpetas propias (un idioma de mentira, «zz», con respaldo en «en»), así no
// depende de lo que esté traducido de verdad. Y los perfiles reales: que estén bien
// armados y que el original sea uno solo.
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace i18n = ba::i18n;
using json = nlohmann::json;

static int g_fallas = 0;
static void check(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++g_fallas;
}

static void escribir(const fs::path& p, const json& j) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << j.dump(2);
}

static void ponerEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v == nullptr ? "" : v);
#else
    if (v == nullptr) unsetenv(k);
    else setenv(k, v, 1);
#endif
}

int main() {
    // --- Los perfiles de verdad (assets/idiomas) ---------------------------------
    {
        const auto todos = i18n::idiomas();
        int fuentes = 0;
        bool hayEn = false, hayPseudo = false;
        for (const auto& p : todos) {
            fuentes += p.fuente ? 1 : 0;
            hayEn = hayEn || p.codigo == "en";
            hayPseudo = hayPseudo || p.pseudo;
            check(!p.nombre.empty() && !p.locale.empty(), ("perfil " + p.codigo + ": nombre y locale").c_str());
            check(i18n::formasDePlural(p.plural) >= 1, ("perfil " + p.codigo + ": regla de plural").c_str());
        }
        check(fuentes == 1, "un solo idioma original");
        check(!todos.empty() && todos.front().fuente, "el original va primero");
        check(hayEn && hayPseudo, "están el inglés y el pseudo-idioma");
        check(i18n::actual().fuente, "sin usar(): el original");
        check(i18n::traducir("x", "Conferencia") == "Conferencia", "sin usar(): el texto tal cual");
    }

    // --- Un juego de mentira ------------------------------------------------------
    const fs::path base = fs::temp_directory_path() / "ba-test-i18n";
    fs::remove_all(base);
    const fs::path perfiles = base / "idiomas", catalogos = base / "i18n";
    escribir(perfiles / "es.json", {{"codigo", "es"}, {"nombre", "Español"}, {"locale", "es_AR"}, {"fuente", true}});
    escribir(perfiles / "en.json", {{"codigo", "en"}, {"nombre", "English"}, {"locale", "en_US"}});
    escribir(perfiles / "zz.json", {{"codigo", "zz"}, {"nombre", "Zeta"}, {"locale", "zz_ZZ"}, {"plural", "eslavo"},
                                    {"respaldo", {"en"}}, {"direccion", "rtl"}});
    escribir(perfiles / "xx.json", {{"codigo", "xx"}, {"nombre", "Pseudo"}, {"pseudo", true}});
    escribir(perfiles / "malo.json", {{"codigo", "otro"}, {"nombre", "Mal armado"}});
    escribir(catalogos / "es.json",
             {{"total", 1}, {"traducidos", 1},
              {"mensajes", {{"sala", {{"%n invitado", {"%n invitado", "%n invitados"}}}}}}});
    escribir(catalogos / "en.json",
             {{"total", 4}, {"traducidos", 3},
              {"mensajes",
               {{"sala",
                 {{"Conferencia", "Conference"},
                  {"Salida\x04" "audio", "Output"},
                  {"%n invitado", {"%n guest", "%n guests"}}}}}}});
    escribir(catalogos / "zz.json",
             {{"total", 2}, {"traducidos", 1},
              {"mensajes", {{"sala", {{"%n invitado", {"%n uno", "%n pocos", "%n muchos"}}}}}}});
    i18n::carpetas(perfiles, catalogos);

    {
        const auto todos = i18n::idiomas();
        check(todos.size() == 4, "el perfil que dice otro código no entra");
        check(todos.front().codigo == "es" && todos.back().codigo == "xx", "orden: original, el resto, pseudo");
        check(i18n::perfil("ZZ").derechaAIzquierda, "perfil por código sin importar mayúsculas; rtl");
        check(i18n::perfil("no").codigo.empty(), "uno que no existe: vacío");
        check(i18n::cobertura("en").traducidos == 3 && i18n::cobertura("en").total == 4, "cobertura del catálogo");
        check(i18n::cobertura("es").fraccion() == 1.0 && i18n::cobertura("xx").fraccion() == 1.0,
              "el original y el pseudo, completos");
    }

    // --- Elegir el idioma ----------------------------------------------------------
    check(i18n::elegirDelSistema({"en-GB", "es-AR"}) == "en", "del sistema: por la lengua");
    check(i18n::elegirDelSistema({"de-DE", "es-AR"}) == "es", "del sistema: el segundo preferido");
    check(i18n::elegirDelSistema({"de-DE"}) == "en", "del sistema: ninguno, el inglés");
    check(i18n::elegirDelSistema({"xx"}) == "en", "el pseudo nunca sale solo");
    check(i18n::resolver("zz") == "zz" && i18n::resolver("en-AU") == "en", "resolver un código");
    {
        const fs::path datos = base / "datos";
        escribir(datos / "launcher.json", {{"idioma", "zz"}});
        ponerEnv("BA_IDIOMA", nullptr);
        check(i18n::idiomaDeLaMaquina(datos) == "zz", "el guardado en launcher.json");
        ponerEnv("BA_IDIOMA", "en");
        check(i18n::idiomaDeLaMaquina(datos) == "en", "BA_IDIOMA manda");
        ponerEnv("BA_IDIOMA", nullptr);
        check(ba::platform::launcherJsonFor(datos) == datos / "launcher.json", "launcher.json de la máquina");
    }

    // --- Traducir ------------------------------------------------------------------
    i18n::usar("en");
    check(i18n::actual().codigo == "en", "usar");
    check(i18n::traducir("sala", "Conferencia") == "Conference", "traducido");
    check(i18n::traducir("otro", "Conferencia") == "Conferencia", "otro contexto: el original");
    check(i18n::traducir("sala", "Salida", "audio") == "Output", "con desambiguación");
    check(i18n::traducir("sala", "Salida") == "Salida", "sin la desambiguación no es el mismo");
    check(i18n::traducir("sala", "%n invitado", nullptr, 1) == "1 guest", "plural: uno");
    check(i18n::traducir("sala", "%n invitado", nullptr, 5) == "5 guests", "plural: otro");
    check(i18n::traducir("sala", "Nuevo texto") == "Nuevo texto", "sin traducir: el original");

    check(i18n::En("es").traducir("sala", "%n invitado", nullptr, 2) == "2 invitados",
          "el original también tiene plurales");
    check(i18n::En("zz").traducir("sala", "%n invitado", nullptr, 3) == "3 pocos", "eslavo: pocos");
    check(i18n::En("zz").traducir("sala", "%n invitado", nullptr, 11) == "11 muchos", "eslavo: 11 es muchos");
    check(i18n::En("zz").traducir("sala", "Conferencia") == "Conference", "lo que falta sale del respaldo");
    check(i18n::traducir("sala", "Conferencia") == "Conference", "otro idioma a la vez no cambia el del operador");

    i18n::usar("xx");
    const std::string p = i18n::traducir("sala", "Conferencia %1 <b>en vivo</b> &amp; %n", nullptr, 2);
    check(p.rfind("⟦Çöñfërëñçïä %1 <b>ëñ vïvö</b> &amp; 2", 0) == 0, "pseudo: acentos, respeta %1, etiquetas y &amp;");
    check(p.size() > std::string("⟦Çöñfërëñçïä %1 <b>ëñ vïvö</b> &amp; 2⟧").size(), "pseudo: más largo");
    check(i18n::pseudo("").empty(), "pseudo de vacío: vacío");

    // --- Llenar y plurales sueltos ---------------------------------------------------
    check(i18n::llenar("%2 grabando en %1", {"disco", "Cámara"}) == "Cámara grabando en disco",
          "llenar en el orden de la traducción");
    check(i18n::llenar("al %1 %", {"100"}) == "al 100 %", "un % suelto queda");
    check(i18n::formaPlural("frances", 0) == 0 && i18n::formaPlural("uno-otro", 0) == 1, "el cero: francés y otros");
    check(i18n::formaPlural("arabe", 2) == 2 && i18n::formaPlural("arabe", 105) == 3 &&
              i18n::formaPlural("arabe", 111) == 4 && i18n::formaPlural("arabe", 100) == 5,
          "árabe");
    check(i18n::formaPlural("polaco", 22) == 1 && i18n::formaPlural("polaco", 25) == 2, "polaco");

    i18n::carpetas({}, {});
    fs::remove_all(base);
    std::printf("%s\n", g_fallas == 0 ? "TODO BIEN" : "HAY FALLAS");
    return g_fallas == 0 ? 0 : 1;
}
