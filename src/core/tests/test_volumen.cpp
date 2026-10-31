// test_volumen.cpp — en qué disco cae una carpeta y cuánto le queda (platform/Volumen)
//
// Mismo volumen: una carpeta y su subcarpeta (aunque todavía no exista), y una
// carpeta temporal contra otro sistema de archivos (/dev en macOS y Linux).
// Horas: el cálculo puro con volúmenes inventados (la grabación y su copia en el
// mismo disco suman; en otro, cada uno lo suyo; manda el que antes se llena).
#include "platform/Volumen.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

namespace pf = ba::platform;
namespace fs = std::filesystem;

static int g_fallas = 0;
static void check(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++g_fallas;
}
static bool cerca(double a, double b) { return std::fabs(a - b) < 1e-6 * std::max(1.0, std::fabs(b)); }

int main() {
    // --- Mismo volumen (rutas de verdad) ---
    const fs::path tmp = fs::temp_directory_path();
    check(pf::volumenDe(tmp).has_value(), "la carpeta temporal tiene volumen");
    check(pf::mismoVolumen(tmp, tmp / "grabaciones" / "copia-que-no-existe"), "una subcarpeta que no existe: el mismo volumen");
    check(pf::volumenDe(tmp / "no" / "existe") == pf::volumenDe(tmp), "lo que no existe vale lo de su antecesor");
    check(pf::mismoVolumen("", tmp), "sin ruta: se da por el mismo (para no dejar de cuidarlo)");
#if !defined(_WIN32)
    if (fs::exists("/dev")) check(!pf::mismoVolumen(tmp, "/dev"), "otro sistema de archivos (/dev): otro volumen");
#endif

    // --- Bytes por hora ---
    check(cerca(pf::bytesPorHora(8000), 8000 * 1000.0 / 8 * 3600 * 1.05), "8 Mb/s: ~3,8 GB por hora");
    check(pf::bytesPorHora(0) == 0, "sin bitrate: nada");

    // --- Horas restantes (puro) ---
    const double gbHora = pf::bytesPorHora(10000) / 1e9; // 10 Mb/s
    {
        // Grabación y copia en el mismo disco: el doble de rápido se llena.
        const auto r = pf::sumarPorVolumen({{"grabación", "A", "", static_cast<std::int64_t>(100e9), 10000},
                                            {"copia", "A", "", static_cast<std::int64_t>(100e9), 10000}});
        check(r.volumenes.size() == 1, "mismo volumen: un grupo");
        check(cerca(r.horas, 100.0 / (2 * gbHora)), "mismo volumen: horas con el doble de bitrate");
        check(cerca(r.kbps, 20000), "kbps total");
        check(r.volumenes[0].que.size() == 2, "lo que escribe en él");
    }
    {
        // Copia en otro disco más chico: manda el que antes se llena.
        const auto r = pf::sumarPorVolumen({{"grabación", "A", "", static_cast<std::int64_t>(500e9), 10000},
                                            {"copia", "B", "", static_cast<std::int64_t>(50e9), 10000},
                                            {"ISO cámara 1", "A", "", static_cast<std::int64_t>(500e9), 10000}});
        check(r.volumenes.size() == 2, "dos volúmenes");
        check(cerca(r.horas, 50.0 / gbHora), "manda el que antes se llena (la copia)");
        check(cerca(r.libresGb, 50.0), "las libres son las de ese volumen");
        check(cerca(r.volumenes[0].horas, 500.0 / (2 * gbHora)), "la grabación y la ISO suman en el suyo");
    }
    {
        // Espacio desconocido en uno: no se inventa; vale el otro.
        const auto r = pf::sumarPorVolumen({{"grabación", "A", "", -1, 10000}, {"copia", "B", "", static_cast<std::int64_t>(20e9), 10000}});
        check(r.volumenes[0].horas < 0, "sin espacio conocido: -1");
        check(cerca(r.horas, 20.0 / gbHora), "vale el que se sabe");
    }
    check(pf::sumarPorVolumen({}).horas < 0, "nada que escriba: -1");
    check(pf::sumarPorVolumen({{"grabación", "A", "", static_cast<std::int64_t>(1e9), 0}}).horas < 0, "sin bitrate: -1");

    // --- Con el disco de verdad: algo razonable ---
    const auto real = pf::discoRestante({{"grabación", tmp / "ba-test-volumen", 8000}, {"copia", tmp, 8000}});
    check(real.volumenes.size() == 1, "dos carpetas del temporal: un volumen");
    check(real.libresGb > 0 && real.horas > 0, "con espacio libre de verdad: horas");

    std::printf("%s\n", g_fallas == 0 ? "OK" : "HAY FALLAS");
    return g_fallas == 0 ? 0 : 1;
}
