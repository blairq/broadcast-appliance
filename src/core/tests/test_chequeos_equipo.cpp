// test_chequeos_equipo.cpp
//
// diag::chequeosDeEquipo con datos falsos (F4-07): sesiones de codificador contra el
// tope de la placa (NVENC 8; las demás sin tope; el tope a mano), placas que no están a
// 48 kHz, PTP (sin usar, sin arrancar, sin permisos) y la deriva (enganchada, midiendo,
// con cortes en el programa). Y los permisos del ayudante de PTP con un archivo común.

#include "diag/ChequeosEquipo.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using ba::diag::DatosEquipo;
using json = nlohmann::json;

static ba::platform::ItemPreparacion punto(const DatosEquipo& d, const std::string& id) {
    for (const auto& it : ba::diag::chequeosDeEquipo(d))
        if (it.id == id) return it;
    return {};
}

int main() {
    DatosEquipo d;
    CHECK(ba::diag::chequeosDeEquipo(d).size() == 4, "cuatro puntos");
    CHECK(punto(d, "codificador_sesiones").estado == "no_aplica", "sin salidas: no aplica");
    CHECK(punto(d, "audio_48k").estado == "no_aplica" && punto(d, "deriva").estado == "no_aplica", "sin placas: no aplica");
    CHECK(punto(d, "ptp").estado == "no_aplica" && !punto(d, "ptp").detalle.empty(), "sin PTP: igual se explica");

    // Sesiones: NVENC de GeForce, 8.
    CHECK(ba::diag::limiteSesionesDe("nvh264enc") == 8 && ba::diag::limiteSesionesDe("nvh265enc") == 8, "NVENC: 8");
    CHECK(ba::diag::limiteSesionesDe("vtenc_h264_hw") < 0 && ba::diag::limiteSesionesDe("x264enc") < 0, "los demás: sin tope");
    for (int i = 0; i < 6; ++i) d.codificaciones.push_back({{"fabrica", "nvh264enc"}, {"tipo", "video"}});
    d.codificaciones.push_back({{"fabrica", "avenc_aac"}, {"tipo", "audio"}}); // el audio no cuenta
    CHECK(punto(d, "codificador_sesiones").estado == "ok", "6 de 8: bien");
    d.codificaciones.push_back({{"fabrica", "nvh265enc"}, {"tipo", "video"}});
    CHECK(punto(d, "codificador_sesiones").estado == "revisar", "7 de 8: queda una");
    d.codificaciones.push_back({{"fabrica", "nvh264enc"}, {"tipo", "video"}});
    const auto lleno = punto(d, "codificador_sesiones");
    CHECK(lleno.estado == "falta" && !lleno.como.empty(), "8 de 8: falta, con cómo arreglarlo");
    CHECK(lleno.detalle.find("8") != std::string::npos, "el detalle dice cuántas");
    d.limiteSesiones = 32; // una profesional
    CHECK(punto(d, "codificador_sesiones").estado == "ok", "con el tope a mano");
    d.codificaciones = json::array({{{"fabrica", "vtenc_h264_hw"}, {"tipo", "video"}}});
    d.limiteSesiones = 0;
    CHECK(punto(d, "codificador_sesiones").estado == "ok", "VideoToolbox: sin tope fijo");

    // Placas.
    d.placas = json::array({{{"entrada", "Mic 1"}, {"dispositivo", "Scarlett"}, {"hz", 48000}, {"ppm", 12.3}, {"medido_s", 120},
                             {"corregida", true}, {"enganchada", true}, {"reanclajes", 0}}});
    CHECK(punto(d, "audio_48k").estado == "ok", "a 48 kHz");
    CHECK(punto(d, "deriva").estado == "ok", "enganchada y sin cortes");
    d.placas.push_back({{"entrada", "Mic 2"}, {"dispositivo", "USB"}, {"hz", 44100}, {"ppm", 3}, {"medido_s", 20}});
    const auto a = punto(d, "audio_48k");
    CHECK(a.estado == "falta" && a.detalle.find("44100") != std::string::npos, "una a 44,1 kHz: falta");
    CHECK(punto(d, "deriva").estado == "revisar", "una midiendo todavía");
    d.placas[1]["hz"] = 0;
    CHECK(punto(d, "audio_48k").estado == "revisar", "sin saber la frecuencia: revisar");
    d.placas[1]["medido_s"] = 90;
    d.placas[1]["reanclajes"] = 3;
    CHECK(punto(d, "deriva").estado == "falta", "cortes en el programa: falta");
    d.placas[1]["reanclajes"] = 0;
    d.placas[1]["ppm"] = 250.0;
    CHECK(punto(d, "deriva").estado == "falta", "250 ppm: falta");

    // PTP.
    d.relojTipo = "ptp";
    d.relojError = "PTP no arrancó: necesita gst-ptp-helper con permisos";
    d.ptpAyudante = true;
    d.ptpPermisos = 0;
    auto p = punto(d, "ptp");
    CHECK(p.estado == "falta" && p.detalle.find("no arrancó") != std::string::npos && !p.como.empty(), "PTP sin arrancar, con cómo");
    d.relojError.clear();
    CHECK(punto(d, "ptp").estado == "revisar", "anda pero el ayudante no tiene permisos: revisar");
    d.ptpPermisos = 1;
    CHECK(punto(d, "ptp").estado == "ok", "PTP bien");
    d.ptpAyudante = false;
    CHECK(punto(d, "ptp").estado == "revisar", "sin ayudante en el paquete");

    // Los permisos del ayudante: un archivo común no los tiene; uno que no está, no se sabe.
    const auto f = std::filesystem::temp_directory_path() / ("ba-ptp-" + std::to_string(std::time(nullptr)));
    std::ofstream(f) << "x";
#if defined(_WIN32)
    CHECK(ba::diag::permisosDeAyudantePtp(f) == -1, "Windows: no se sabe");
#else
    CHECK(ba::diag::permisosDeAyudantePtp(f) == 0, "archivo común: sin permisos");
#endif
    std::filesystem::remove(f);
    CHECK(ba::diag::permisosDeAyudantePtp(f) == -1, "no está: no se sabe");

    if (g_failures == 0) std::printf("test_chequeos_equipo: OK\n");
    return g_failures == 0 ? 0 : 1;
}
