// test_salud_drivers.cpp — la cuarentena de drivers (hw/SaludDrivers), el
// antibucle de enumeración (hw/Antibucle) y la huella de la máquina
//
// Sin hardware ni procesos: el contador y el umbral, el reseteo con un éxito,
// «Probar de nuevo», la persistencia por máquina, qué es NUEVO en una máquina
// conocida (y que lo que anduvo no se vuelve a probar), los motivos legibles y
// los avisos sintéticos de «cambiaron los dispositivos».
#include "hw/Antibucle.hpp"
#include "hw/SaludDrivers.hpp"
#include "platform/Maquina.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ba::hw::Antibucle;
using ba::hw::DriverListado;
using ba::hw::Falla;
using ba::hw::SaludDrivers;

static int g_fallas = 0;
static void check(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "FALLA", que);
    if (!ok) ++g_fallas;
}

static fs::path carpetaTemporal() {
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path p = fs::temp_directory_path() / ("ba-test-salud-drivers-" + std::to_string(t));
    fs::create_directories(p);
    return p;
}

static bool tiene(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

static void cuarentena() {
    SaludDrivers s({}, "maq");
    s.setUmbral(4);
    s.novedades({{"asio:{SAR}", "SAR", "asio", "f1", 0}});
    check(!s.falla("asio:{SAR}", Falla::Error, "x", "t1"), "1ra falla: no queda en cuarentena");
    check(!s.falla("asio:{SAR}", Falla::Tiempo, "x", "t2"), "2da falla");
    check(!s.falla("asio:{SAR}", Falla::Caida, "x", "t3"), "3ra falla");
    check(s.uno("asio:{SAR}")->estado == "falla" && s.uno("asio:{SAR}")->fallas == 3, "tres fallas: «falla», sin cuarentena");
    check(s.sePuedeCargar("asio:{SAR}"), "con 3 fallas todavía se carga");
    check(s.falla("asio:{SAR}", Falla::Tiempo, "no contestó", "t4"), "4ta falla: QUEDA en cuarentena");
    check(s.enCuarentena("asio:{SAR}") && !s.sePuedeCargar("asio:{SAR}"), "en cuarentena no se carga");
    check(!s.falla("asio:{SAR}", Falla::Error, "y", "t5"), "otra falla en cuarentena no avisa de nuevo");
    check(s.uno("asio:{SAR}")->ultimaFalla == "error" && s.uno("asio:{SAR}")->motivo == "y", "guarda cómo y por qué");

    // «Probar de nuevo»: una prueba; si falla, se queda; si anda, sale.
    check(s.reintentar("asio:{SAR}").empty(), "reintentar uno que existe");
    check(s.sePuedeCargar("asio:{SAR}"), "con «Probar de nuevo» se lo puede cargar una vez");
    s.falla("asio:{SAR}", Falla::Error, "sigue sin JACK", "t6");
    check(s.enCuarentena("asio:{SAR}") && !s.sePuedeCargar("asio:{SAR}"), "falló el reintento: sigue en cuarentena");
    s.reintentar("asio:{SAR}");
    s.exito("asio:{SAR}", "2 entradas, 2 salidas", "t7");
    check(!s.enCuarentena("asio:{SAR}") && s.uno("asio:{SAR}")->fallas == 0, "anduvo: sale y el contador vuelve a 0");
    check(s.uno("asio:{SAR}")->fallasTotal == 6, "las fallas totales se recuerdan");
    check(!s.reintentar("no:existe").empty(), "reintentar uno que no existe: error");

    // Un éxito en el medio resetea: hacen falta 4 SEGUIDAS.
    s.falla("asio:{SAR}", Falla::Error, "a", "t");
    s.falla("asio:{SAR}", Falla::Error, "a", "t");
    s.falla("asio:{SAR}", Falla::Error, "a", "t");
    s.exito("asio:{SAR}", "", "t");
    s.falla("asio:{SAR}", Falla::Error, "a", "t");
    check(!s.enCuarentena("asio:{SAR}"), "un éxito en el medio: no hay 4 seguidas");

    SaludDrivers u({}, "maq");
    u.setUmbral(2);
    u.falla("gst:x", Falla::Error, "a", "t");
    check(u.falla("gst:x", Falla::Error, "a", "t"), "umbral configurable (2)");
}

static void persistenciaYMaquinas() {
    const fs::path dir = carpetaTemporal();
    const fs::path archivo = dir / "Config" / "drivers.json";
    {
        SaludDrivers a(archivo, "maqA", "MacBook");
        check(!a.maquinaConocida(), "máquina A: nueva");
        const auto n = a.novedades({{"gst:avf", "AVF", "gstreamer", "1.28", 256},
                                    {"gst:raro", "Raro", "gstreamer", "0.1", 128},
                                    {"gst:osx", "Core Audio", "gstreamer", "1.28", 256}});
        check(n.size() == 3, "máquina nueva: se prueba todo");
        a.exito("gst:avf", "1 dispositivo", "t");
        a.exito("gst:osx", "2 dispositivos", "t");
        for (int i = 0; i < 4; ++i) a.falla("gst:raro", Falla::Tiempo, "se colgó", "t");
        a.sondeoCompleto("2026-10-26T10:00:00");
        check(a.maquinaConocida(), "terminado el sondeo completo: conocida");
    }
    {
        SaludDrivers a(archivo, "maqA");
        check(a.maquinaConocida(), "releída: sigue conocida");
        check(a.enCuarentena("gst:raro"), "la cuarentena se guarda");
        check(a.uno("gst:avf")->estado == "anda" && a.uno("gst:avf")->detalle == "1 dispositivo", "lo que anduvo se guarda");
        check(a.uno("gst:raro")->fallas == 4 && a.uno("gst:raro")->motivo == "se colgó", "fallas y motivo se guardan");
        // Máquina conocida: lo que anduvo NO se vuelve a probar; la cuarentena tampoco;
        // lo nuevo sí; lo que cambió de versión sí; lo que no está, ausente.
        const auto n = a.novedades({{"gst:avf", "AVF", "gstreamer", "1.28", 256},
                                    {"gst:raro", "Raro", "gstreamer", "0.1", 128},
                                    {"gst:decklink", "DeckLink", "gstreamer", "1.28", 256}});
        check(!tiene(n, "gst:avf"), "conocida: lo que anduvo no se re-sondea");
        check(!tiene(n, "gst:raro"), "conocida: la cuarentena no se carga");
        check(tiene(n, "gst:decklink") && n.size() == 1, "conocida: solo lo nuevo");
        check(!a.uno("gst:osx")->presente && a.uno("gst:osx")->estado == "anda", "lo que no está: ausente, no falla");
        const auto m = a.novedades({{"gst:avf", "AVF", "gstreamer", "1.30", 256},
                                    {"gst:raro", "Raro", "gstreamer", "0.2", 128}});
        check(tiene(m, "gst:avf") && tiene(m, "gst:raro"), "otra versión: se vuelve a probar (aunque estuviera en cuarentena)");
        check(!a.enCuarentena("gst:raro") && a.uno("gst:raro")->fallas == 0, "otra versión: contador en cero");
        a.falla("gst:raro", Falla::Error, "x", "t");
        const auto k = a.novedades({{"gst:raro", "Raro", "gstreamer", "0.2", 128}});
        check(tiene(k, "gst:raro"), "lo que viene fallando sin llegar a la cuarentena: se vuelve a probar");
    }
    {
        SaludDrivers b(archivo, "maqB", "PC de la sala");
        check(!b.maquinaConocida(), "otra máquina (el disco viajó): nueva");
        check(!b.enCuarentena("gst:raro"), "la cuarentena de A no vale en B");
        const auto n = b.novedades({{"gst:avf", "AVF", "gstreamer", "1.28", 256}});
        check(n.size() == 1, "máquina nueva: se prueba aunque en A anduviera");
        b.falla("gst:avf", Falla::Error, "x", "t");
    }
    {
        SaludDrivers a(archivo, "maqA");
        check(a.maquinaConocida() && a.uno("gst:avf").has_value(), "guardar B no borró lo de A");
        check(SaludDrivers::cuarentenaDe(archivo, "maqA", "gst:").empty(), "cuarentenaDe: nada (raro cambió de versión)");
        for (int i = 0; i < 4; ++i) a.falla("gst:raro", Falla::Caida, "tumbó", "t");
        const auto q = SaludDrivers::cuarentenaDe(archivo, "maqA", "gst:");
        check(q.size() == 1 && q[0] == "gst:raro", "cuarentenaDe: lo lee GstEntorno sin armar nada");
        check(SaludDrivers::cuarentenaDe(archivo, "maqB", "gst:").empty(), "cuarentenaDe: por máquina");
        check(SaludDrivers::cuarentenaDe(dir / "no-existe.json", "maqA", "gst:").empty(), "sin archivo: nada");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void motivos() {
    using ba::hw::motivoLegible;
    check(motivoLegible(Falla::Error, "JackRouter", "", "Open error: is JACK server running?\n", 8000) ==
              "no contesta: ¿está corriendo el servidor de JACK?",
          "JackRouter sin JACK: el motivo claro");
    check(motivoLegible(Falla::Caida, "SAR", "", "algo\nOpen error: is JACK server running?\n", 8000).find("JACK") !=
              std::string::npos,
          "SAR sobre JackRouter: también JACK");
    check(motivoLegible(Falla::Tiempo, "X", "", "", 8000) == "no contestó en 8 s: se colgó al cargarlo", "tiempo agotado");
    check(motivoLegible(Falla::Caida, "X", "", "linea 1\nme caigo\n\n", 8000) ==
              "se cayó el proceso que lo probaba (el driver lo tumbó): me caigo",
          "caída: con lo último que escribió");
    check(motivoLegible(Falla::Error, "X", "no hay placa", "", 8000) == "no arrancó: no hay placa", "error con mensaje");
    check(motivoLegible(Falla::Error, "JackRouter", "", "", 8000) == "no arrancó (no dijo por qué)",
          "el nombre solo no inventa el motivo de JACK");
}

static void antibucle() {
    Antibucle a({800, 2000, 10000});
    const std::string A = "i:mic|o:parlantes", B = "i:mic|i:usb|o:parlantes";
    check(a.aviso(0, A), "primer aviso: se programa");
    check(a.falta(0) == 800, "con la espera corta");
    check(!a.vence(500, A), "antes de la espera no vence");
    check(a.vence(800, A), "vence");
    a.empieza(800, A);
    // Los avisos que provoca la enumeración misma (cargar un driver mueve los dispositivos).
    for (int t = 900; t < 3000; t += 100) a.aviso(t, B);
    check(a.falta(3000) == -1, "durante la enumeración: los avisos no programan otra");
    a.termina(3000, A);
    check(!a.aviso(4000, B), "en la gracia después de enumerar: tampoco");
    check(a.falta(4000) == -1, "nada programado tras el eco");
    // Ir y volver (ASIO4ALL toma la placa y la suelta): misma firma al vencer, no se enumera.
    check(a.aviso(6000, B), "aviso de verdad después de la gracia");
    check(!a.vence(20000, A), "al vencer la firma volvió a la de antes: no se enumera");
    check(!a.aviso(21000, A), "la misma firma que vio la última: no se programa");
    // Tope: aunque lleguen avisos, una cada 10 s.
    a.empieza(30000, A);
    a.termina(31000, A);
    check(a.aviso(33500, B), "cambio real");
    check(a.falta(33500) == 6500, "pero no antes de 10 s desde la anterior (30 s + 10 s)");
    check(!a.vence(39000, B), "todavía no");
    check(a.vence(40000, B), "a los 10 s");
    // Lo que cambia mientras se enumera: queda una programada al terminar.
    a.empieza(40000, B);
    a.termina(41000, A);
    check(a.falta(41000) >= 0, "cambió durante la enumeración: queda otra programada");
    // Un bucle de avisos cada 1,8 s durante 2 minutos: como mucho una cada 10 s.
    Antibucle b({800, 2000, 10000});
    int enumeraciones = 0;
    std::string firma = A;
    for (std::int64_t t = 0; t < 120000; t += 100) {
        if (t % 1800 == 0) {
            firma = firma == A ? B : A;
            b.aviso(t, firma);
        }
        if (b.vence(t, firma)) {
            ++enumeraciones;
            b.empieza(t, firma);
            b.termina(t + 300, firma);
        }
    }
    check(enumeraciones <= 13, "bucle sintético de 2 min: una enumeración cada 10 s como mucho");
    check(b.ignorados() > 0, "y los avisos de más se ignoran");
}

static void huella() {
    check(ba::platform::huellaDe("abc") == ba::platform::huellaDe("abc"), "huella estable");
    check(ba::platform::huellaDe("abc") != ba::platform::huellaDe("abd"), "ids distintos, huellas distintas");
    check(ba::platform::huellaDe("abc").size() == 16, "16 hex");
    check(ba::platform::huellaDe("abc").find("abc") == std::string::npos, "no contiene el id");
    const std::string h = ba::platform::huellaDeMaquina();
    check(h.size() == 16 && h == ba::platform::huellaDeMaquina(), "la de esta máquina: 16 hex y siempre la misma");
#if defined(_WIN32)
    _putenv_s("BA_HUELLA_MAQUINA", "otra");
#else
    setenv("BA_HUELLA_MAQUINA", "otra", 1);
#endif
    check(ba::platform::huellaDeMaquina() == "otra", "BA_HUELLA_MAQUINA la fija (simular otra máquina)");
}

int main() {
    cuarentena();
    persistenciaYMaquinas();
    motivos();
    antibucle();
    huella();
    std::printf(g_fallas == 0 ? "test_salud_drivers: OK\n" : "test_salud_drivers: %d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
