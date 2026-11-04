// ============================================================================
// src/app/tests/test_secciones_config.cpp — qué sección de Configuración se pidió
// (ui/SeccionesConfig.hpp)
//
// El caso del hallazgo: el asistente pidió la sección por el título que vio, «Idioma ·
// Language»; la página mostró ese título y ningún campo (los campos se eligen por la
// clave, «Idioma»), y en la lista de la izquierda ninguna quedaba marcada.
// ============================================================================
#include "ui/SeccionesConfig.hpp"

#include <cstdio>

namespace {
int fallas = 0;
void check(bool c, const char* msg) {
    std::printf("%s: %s\n", c ? "ok" : "FALLA", msg);
    if (!c) ++fallas;
}
} // namespace

int main() {
    using ba::app::secciones::canonica;
    // Como SettingsModel::titulosSecciones: {clave: lo que se ve}.
    const QVariantMap es{{"Idioma", "Idioma · Language"}, {"Video", "Video"}, {"Grabación", "Grabación"},
                         {"Túnel propio (WireGuard)", "Túnel propio (WireGuard)"}};
    const QVariantMap en{{"Idioma", "Language"}, {"Video", "Video"}, {"Grabación", "Recording"}};

    check(canonica("Idioma", es) == "Idioma", "la clave queda como está");
    check(canonica("Idioma · Language", es) == "Idioma", "el título que se ve («Idioma · Language») lleva a la clave");
    check(canonica("idioma", es) == "Idioma", "la clave con otra capitalización");
    check(canonica("  Idioma · Language ", es) == "Idioma", "con espacios de más");
    check(canonica("Recording", en) == "Grabación", "el título traducido (programa en inglés)");
    check(canonica("language", en) == "Idioma", "el título traducido, en minúsculas");
    check(canonica("WireGuard", es) == "Túnel propio (WireGuard)", "un nombre de antes de reordenar");
    check(canonica("Escena", es) == "Video", "otro nombre de antes");
    check(canonica("Control del sistema", es) == "Control del sistema",
          "una sección propia de la página que no está en los títulos queda como está");
    check(canonica("Idioma · Language", {}) == "Idioma · Language", "sin títulos (sin modelo) no inventa nada");

    std::printf("%s\n", fallas == 0 ? "test_secciones_config: todo bien" : "test_secciones_config: hay fallas");
    return fallas == 0 ? 0 : 1;
}
