// test_motor_lugares_vacios.cpp — un lugar vacío no tiene hilos; llegar e irse no deja nada colgado
//
// Con el GStreamer del bundle y nuestros plugins (sin ellos: SKIP). Como arma la app los
// lugares de la sala (InputSpecs.cpp): por lugar, una entrada de video (bavideoanillo, por el
// lector común) y una de audio (baanillo, por el lector común). En la .177 con cupo 200,
// ba-app arrancaba con ~3 hilos por lugar sin nadie adentro (la placa: intersrc e
// imagefreeze; y un «entrada-trabajo» por lugar) y sumaba 3 colas por invitado.
//   - Lugares vacíos desde antes de crear sus entradas (como al cambiar el cupo): nacen en
//     reposo, SIN placa (nadie los mira) y los hilos del proceso casi no crecen (< 0,25 por lugar).
//   - Uno que se muestra en una escena: con su placa (la placa de un lugar vacío se ve).
//   - Llega gente a 8 lugares: < 0,5 hilos por invitado; se van: vuelven a reposo.
//   - Ir y venir tres veces no deja hilos ni memoria colgados (contadores del motor: recursos).
//   - Borrar todas las entradas: los hilos vuelven a los del principio (± los compartidos) y
//     los de trabajo, a los de base.

#include "Memoria.hpp"
#include "Motor.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"
#include "Trabajos.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <tlhelp32.h>
#define getpid _getpid
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#else
#include <filesystem>
#include <unistd.h>
#endif

using namespace ba::motor;
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

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Los hilos de este proceso.
int hilos() {
#if defined(_WIN32)
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 t{};
    t.dwSize = sizeof(t);
    int n = 0;
    const DWORD yo = GetCurrentProcessId();
    for (BOOL ok = Thread32First(s, &t); ok; ok = Thread32Next(s, &t))
        if (t.th32OwnerProcessID == yo) ++n;
    CloseHandle(s);
    return n;
#elif defined(__APPLE__)
    thread_act_array_t lista = nullptr;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &lista, &n) != KERN_SUCCESS) return -1;
    for (mach_msg_type_number_t i = 0; i < n; ++i) mach_port_deallocate(mach_task_self(), lista[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(lista), n * sizeof(thread_act_t));
    return static_cast<int>(n);
#else
    int n = 0;
    std::error_code ec;
    for (const auto& d : std::filesystem::directory_iterator("/proc/self/task", ec)) {
        (void)d;
        ++n;
    }
    return ec ? -1 : n;
#endif
}

constexpr int kLugares = 24;
constexpr int kLlegan = 8;

/// Espera a que `listo()` se cumpla (mirando cada 100 ms), hasta `topeMs`. Las cuentas del
/// motor cambian en sus hilos: con la máquina cargada tardan más que con la máquina sola, así
/// que se espera la condición y no un tiempo fijo.
template <class F> bool esperarQue(F listo, int topeMs = 20000) {
    const auto tope = std::chrono::steady_clock::now() + std::chrono::milliseconds(topeMs);
    while (!listo()) {
        if (std::chrono::steady_clock::now() >= tope) return false;
        esperar(100);
    }
    return true;
}

/// Los hilos del proceso ya en calma: los de trabajo que sobraron se fueron (a los 0,5 s sin
/// trabajo: ponerEsperaDeHilosDeTrabajo en main; con la máquina cargada, más) y la cuenta no
/// cambió en el último medio segundo. Lo que se mide son los hilos que QUEDAN, no la ráfaga
/// de un cambio: antes era un esperar fijo de 6 s y, con carga, la ráfaga seguía viva y se
/// contaba como de los lugares (test_motor_lugares_vacios fallaba con otros compilando).
int hilosEnCalma() {
    int antes = -1, h = hilos();
    esperarQue(
        [&] {
            esperar(400);
            antes = h;
            h = hilos();
            return h == antes && hilosDeTrabajo().vivos <= 2;
        },
        30000);
    return h;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
#if defined(BA_TEST_GST_PLUGINS)
    g_setenv("BA_GST_PLUGINS_EXTRA", BA_TEST_GST_PLUGINS, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    for (const char* e : {"audiomixer", "baanillocomun", "bavideoanillocomun", "input-selector", "imagefreeze", "intersink"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    // Los hilos de trabajo que sobran se van a los 0,5 s sin trabajo (de fábrica, 5 s): lo que
    // se mide es lo que QUEDA, y cada medición esperaba los 5 s enteros (eran ~39 s de test).
    ponerEsperaDeHilosDeTrabajo(500);
    const std::string pid = std::to_string(getpid());
    Motor::Config cfg;
    cfg.lienzo.ancho = 320;
    cfg.lienzo.alto = 180;
    cfg.prefijoAnillos = "BA.Audio.PruebaLV" + pid + ".";
    cfg.sinkEscucha = "fakesink sync=true"; // nada por los parlantes
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    const int h0 = hilosEnCalma();
    const std::int64_t m0 = memoria::privada();

    // Los lugares vacíos ANTES de crear sus entradas (la app lo hace así al cambiar el cupo).
    const auto canalV = [&](int i) { return "BA.PruebaLV" + pid + "." + std::to_string(i); };
    const auto canalA = [&](int i) { return "BA.Audio.PruebaLVG" + pid + "." + std::to_string(i); };
    const auto lugares = [&](int conGente) {
        std::map<std::string, bool> l;
        for (int i = 1; i <= kLugares; ++i) {
            l[canalV(i)] = i <= conGente;
            l[canalA(i)] = i <= conGente;
        }
        return l;
    };
    motor.ponerLugares(lugares(0));
    for (int i = 1; i <= kLugares; ++i) {
        const std::string n = std::to_string(i);
        CHECK(motor.agregarEntrada("Invitado " + n, {{"pipeline", "bavideoanillo canal=" + canalV(i)}, {"pipeline_audio", ""},
                                                    {"audio", false}, {"normalizar", false}, {"estudio", false},
                                                    {"video_invitado", true}, {"slot", i}})
                  .empty(),
              "el video del lugar");
        CHECK(motor.agregarEntrada("Invitado " + n + " (audio)",
                                   {{"pipeline", ""}, {"pipeline_audio", "baanillo canal=" + canalA(i)}, {"audio", true}, {"estudio", false}},
                                   {{"global", true}})
                  .empty(),
              "el audio del lugar");
    }
    json rec;
    esperarQue([&] { rec = motor.estado()["recursos"]; return rec.value("en_reposo", -1) == 2 * kLugares; });
    // Crear 48 entradas de golpe agranda los hilos de trabajo compartidos hasta su tope (16);
    // los que sobran se van a los 0,5 s sin trabajo (ponerEsperaDeHilosDeTrabajo, arriba). Lo
    // que se mide es lo que QUEDA: los hilos de los lugares, no la ráfaga del arranque.
    const int h1 = hilosEnCalma();
    std::printf("%d lugares vacíos: hilos %d → %d (%.2f por lugar); %s\n", kLugares, h0, h1,
                static_cast<double>(h1 - h0) / kLugares, rec.dump().c_str());
    CHECK(rec.value("placas_armadas", -1) == 0, "un lugar vacío que nadie mira no arma su placa");
    CHECK(rec.value("en_reposo", -1) == 2 * kLugares, "todos los lugares vacíos en reposo (READY)");
    CHECK(rec.value("andando", -1) == 0, "ninguno andando");
    CHECK(h1 - h0 <= kLugares / 4, "los lugares vacíos casi no suman hilos (< 0,25 por lugar; eran ~3)");
    CHECK(rec["hilos_trabajo"].value("vivos", 99) <= 16, "los trabajos de las entradas en pocos hilos compartidos");

    // Uno que se muestra: con su placa (y sin gente sigue armado para verse).
    motor.definirEscena({{"nombre", "Lugar 1"}, {"capas", {{{"entrada", "Invitado 1"}}}}});
    CHECK(motor.alAire("Lugar 1").empty(), "al aire el lugar 1");
    esperarQue([&] { rec = motor.estado()["recursos"]; return rec.value("placas_armadas", -1) == 1; });
    CHECK(rec.value("placas_armadas", -1) == 1, "el lugar vacío que se muestra, con su placa");
    CHECK(motor.estado()["entradas"]["Invitado 1"].value("placa_armada", false), "es la suya");
    motor.definirEscena({{"nombre", "Vacía"}, {"capas", json::array()}});
    CHECK(motor.alAire("Vacía").empty(), "al aire una escena sin nadie");

    // Llega gente a 8 lugares: pocos hilos por invitado (el lector común es uno para todos).
    motor.ponerLugares(lugares(kLlegan));
    esperarQue([&] { rec = motor.estado()["recursos"]; return rec.value("en_reposo", -1) == 2 * (kLugares - kLlegan); });
    const int h2 = hilosEnCalma(); // la ráfaga de los hilos de trabajo se va (ver arriba): lo que queda
    std::printf("llegan %d: hilos %d → %d (%.2f por invitado); %s\n", kLlegan, h1, h2, static_cast<double>(h2 - h1) / kLlegan,
                rec.dump().c_str());
    CHECK(rec.value("en_reposo", -1) == 2 * (kLugares - kLlegan), "los que llegaron, andando; los demás en reposo");
    CHECK(h2 - h1 <= kLlegan / 2 + 2, "< 0,5 hilos por invitado (+2: los lectores comunes, uno por medio)");

    // Se van y vuelven tres veces: ni hilos ni memoria colgados.
    motor.ponerLugares(lugares(0));
    esperarQue([&] { return motor.estado()["recursos"].value("en_reposo", -1) == 2 * kLugares; });
    const int h3 = hilosEnCalma();
    const std::int64_t m3 = memoria::privada();
    // Cada ida y cada vuelta, hasta que el motor la hizo (los que llegan, andando; los que se
    // van, en reposo): antes eran 800 ms fijos por paso (4,8 s del test), que con la máquina
    // cargada podían no alcanzar (la vuelta empezaba con la ida a medio hacer) y sola sobraban.
    for (int vuelta = 0; vuelta < 3; ++vuelta) {
        motor.ponerLugares(lugares(kLlegan));
        CHECK(esperarQue([&] { return motor.estado()["recursos"].value("en_reposo", -1) == 2 * (kLugares - kLlegan); }),
              "ir y venir: llegan");
        motor.ponerLugares(lugares(0));
        CHECK(esperarQue([&] { return motor.estado()["recursos"].value("en_reposo", -1) == 2 * kLugares; }),
              "ir y venir: se van");
    }
    esperarQue([&] {
        rec = motor.estado()["recursos"];
        return rec.value("en_reposo", -1) == 2 * kLugares && rec.value("placas_armadas", -1) == 0;
    });
    const int h4 = hilosEnCalma(); // los hilos de trabajo que sobran se van a los ~0,5 s
    const std::int64_t m4 = memoria::privada();
    std::printf("tres idas y vueltas: hilos %d → %d, privada %lld → %lld MB; %s\n", h3, h4, static_cast<long long>(m3 / 1048576),
                static_cast<long long>(m4 / 1048576), rec.dump().c_str());
    CHECK(rec.value("en_reposo", -1) == 2 * kLugares && rec.value("placas_armadas", -1) == 0, "todos de nuevo en reposo, sin placa");
    CHECK(h4 <= h3 + 1, "ir y venir no deja hilos colgados");
    if (m3 > 0 && m4 > 0) CHECK(m4 - m3 < 64LL * 1048576, "ir y venir no deja memoria colgada (< 64 MB)");

    // Se borran todos: los hilos, a los del principio (± los compartidos); los de trabajo, a los de base.
    for (int i = 1; i <= kLugares; ++i) {
        motor.quitarEntrada("Invitado " + std::to_string(i));
        motor.quitarEntrada("Invitado " + std::to_string(i) + " (audio)");
    }
    esperarQue([&] { return motor.estado()["recursos"].value("entradas", -1) == 0; });
    const int h5 = hilosEnCalma();
    rec = motor.estado()["recursos"];
    const memoria::Compactada c = memoria::compactar();
    std::printf("sin lugares: hilos %d (al principio %d); privada %lld → %lld MB (compactada en %lld µs); %s\n", h5, h0,
                static_cast<long long>(m0 / 1048576), static_cast<long long>(c.despues / 1048576), static_cast<long long>(c.us),
                rec.dump().c_str());
    CHECK(rec.value("entradas", -1) == 0, "no quedó ninguna entrada");
    CHECK(rec["hilos_trabajo"].value("vivos", 99) <= 2, "los hilos de trabajo, a los de base");
    // (+6: los 2 de trabajo de base, los 2 lectores comunes y los que GLib guarda un rato en su pool.)
    CHECK(h5 <= h0 + 6, "los hilos vuelven a los del principio (± los compartidos: trabajo y lectores comunes)");
    if (m0 > 0 && c.despues > 0) CHECK(c.despues - m0 < 128LL * 1048576, "la memoria vuelve cerca de la del principio (< 128 MB)");

    motor.detener();
    if (fallos == 0) std::printf("test_motor_lugares_vacios: OK\n");
    return fallos == 0 ? 0 : 1;
}
