// test_rutas_portables.cpp — las rutas guardadas (arranque/RutasPortables) vuelven a apuntar
// al mismo archivo
//
// Con unos datos de mentira (BA_DATA_ROOT, BA_CONFIG_ROOT en una carpeta temporal):
//   - una de la configuración (Media/…) se guarda relativa y vuelve a ser la de ahora;
//   - una de adentro de los datos pero fuera de las carpetas conocidas (Manuales/…) se
//     guarda ABSOLUTA: relativa, al leer quedaba así y el archivo se buscaba en la
//     configuración (2026-11-10: un video de los manuales en una escena, «Resource not
//     found» en cada arranque);
//   - lo que ya quedó guardado así (Manuales/… relativo) se arregla al leer si el archivo
//     existe; un texto que parece una ruta y no es un archivo («sí/no») queda tal cual;
//   - lo de afuera de los datos, tal cual.

#include "arranque/RutasPortables.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)

void ponerVariable(const char* k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k, v.c_str());
#else
    setenv(k, v.c_str(), 1);
#endif
}

std::string texto(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

void tocar(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}
} // namespace

int main() {
    const fs::path datos = fs::temp_directory_path() / "ba_test_rutas_portables" / "Datos";
    std::error_code ec;
    fs::remove_all(datos.parent_path(), ec);
    const fs::path config = datos / "Configuraciones" / "Prueba";
    fs::create_directories(config, ec);
    ponerVariable("BA_DATA_ROOT", texto(datos));
    ponerVariable("BA_CONFIG_ROOT", texto(config));
    const fs::path medio = config / "Media" / "logo.png";
    const fs::path manual = datos / "Manuales" / "es" / "videos" / "manual.mp4";
    tocar(medio);
    tocar(manual);

    // De la configuración: relativa al guardar, la de ahora al leer.
    {
        const json g = ba::app::rutasRelativas(json{{"file", texto(medio)}});
        std::printf("Media guardada: %s\n", g.dump().c_str());
        CHECK(g["file"] == "Media/logo.png", "Media: se guarda relativa");
        CHECK(fs::path(ba::app::rutasAbsolutas(g)["file"].get<std::string>()) == medio, "Media: al leer, la de ahora");
    }
    // De los datos, fuera de las carpetas conocidas: absoluta (y al leer, el mismo archivo).
    {
        const json g = ba::app::rutasRelativas(json{{"local_file", texto(manual)}});
        std::printf("Manuales guardada: %s\n", g.dump().c_str());
        CHECK(g["local_file"] == texto(manual), "Manuales: se guarda absoluta (al leer no se sabría rearmar relativa)");
        CHECK(fs::path(ba::app::rutasAbsolutas(g)["local_file"].get<std::string>()) == manual, "Manuales: al leer, el mismo archivo");
    }
    // Lo que ya quedó guardado relativo (antes de este arreglo): se arregla al leer.
    {
        const json l = ba::app::rutasAbsolutas(json{{"local_file", "Manuales/es/videos/manual.mp4"}, {"texto", "sí/no"}});
        std::printf("leído: %s\n", l.dump().c_str());
        CHECK(fs::path(l["local_file"].get<std::string>()) == manual, "Manuales relativa de antes: al leer, el archivo de los datos");
        CHECK(l["texto"] == "sí/no", "un texto con barra que no es un archivo queda tal cual");
    }
    // De afuera de los datos: tal cual, ida y vuelta.
    {
        const std::string afuera = "/otro/disco/video.mp4";
        CHECK(ba::app::rutasRelativas(json{{"f", afuera}})["f"] == afuera, "afuera: se guarda tal cual");
        // Como ruta (en Windows la normaliza con «\»: la misma ruta).
        CHECK(fs::path(ba::app::rutasAbsolutas(json{{"f", afuera}})["f"].get<std::string>()) == fs::path(afuera),
              "afuera: se lee como la misma ruta");
    }

    fs::remove_all(datos.parent_path(), ec);
    std::printf("== test_rutas_portables: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
