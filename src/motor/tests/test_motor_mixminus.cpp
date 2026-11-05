// test_motor_mixminus.cpp — los retornos de muchos invitados: una suma, una resta por invitado
//
// Con el GStreamer del bundle y nuestros plugins (sin ellos: SKIP). Señales sintéticas,
// nada por los parlantes; los anillos con otro prefijo (no pisan los de una app andando).
//   - 12 invitados, cada uno con su tono: en el retorno de cada uno NO está su voz
//     (Goertzel a su frecuencia: ~0) y SÍ están todos los demás;
//   - el costo es lineal: con 24 entradas, prender los 24 retornos suma unos 24 hilos
//     (una rama por entrada en UN bus), no 24×23 (una mezcla por invitado);
//   - entrar y salir invitados (un retorno nuevo, otro apagado y de vuelta, uno que pasa a
//     «programa») no corta el retorno de los demás: sus bloques siguen llegando cada 10 ms;
//   - en «programa» el invitado se oye; de vuelta en mix_minus, no.

#include "LectorAnillo.hpp"
#include "Motor.hpp"
#include "Piezas.hpp"
#include "Reloj.hpp"
#include "ipc/CanalesSalida.hpp"

#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <process.h>
#define getpid _getpid
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                             \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);         \
            ++fallos;                                                                \
        }                                                                            \
    } while (0)

void esperar(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// Los hilos del proceso.
int hilos() {
#if defined(_WIN32)
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (h == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 t{};
    t.dwSize = sizeof(t);
    int n = 0;
    const DWORD yo = GetCurrentProcessId();
    for (BOOL ok = Thread32First(h, &t); ok; ok = Thread32Next(h, &t))
        if (t.th32OwnerProcessID == yo) ++n;
    CloseHandle(h);
    return n;
#elif defined(__APPLE__)
    thread_act_array_t lista = nullptr;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &lista, &n) != KERN_SUCCESS) return -1;
    for (mach_msg_type_number_t i = 0; i < n; ++i) mach_port_deallocate(mach_task_self(), lista[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(lista), n * sizeof(thread_act_t));
    return static_cast<int>(n);
#else
    DIR* d = opendir("/proc/self/task");
    if (d == nullptr) return -1;
    int n = 0;
    while (dirent* e = readdir(d))
        if (e->d_name[0] != '.') ++n;
    closedir(d);
    return n;
#endif
}

/// La CPU del proceso en `ms` (núcleos: 1 = uno entero). -1 donde no se mide.
double cpuDurante(int ms) {
#if defined(_WIN32)
    esperar(ms);
    return -1;
#else
    const auto usada = []() {
        rusage u{};
        getrusage(RUSAGE_SELF, &u);
        return static_cast<double>(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
    };
    const double a = usada();
    esperar(ms);
    return (usada() - a) / (ms / 1000.0);
#endif
}

/// El tono del invitado n (Hz): separados, para que Goertzel los distinga en 800 ms.
double frecuencia(int n) { return 310.0 + 97.0 * n; }

json tono(double hz, double vol) {
    return {{"pipeline_audio", "audiotestsrc is-live=true wave=sine volume=" + std::to_string(vol).substr(0, 5) +
                                   " freq=" + std::to_string(static_cast<int>(hz))}};
}

std::string id(int n) { return "inv" + std::to_string(n); }

/// Los sellos (QPC, ns) de los bloques que llegan a `canal` durante `ms`.
std::vector<std::uint64_t> sellos(const std::string& canal, int ms) {
    std::vector<std::uint64_t> out;
    ba::ipc::SharedAudioRing r;
    const std::wstring w(canal.begin(), canal.end());
    const auto fin = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!ba::ipc::SharedAudioRing::openReader(w.c_str(), r).ok) {
        if (std::chrono::steady_clock::now() > fin) return out;
        esperar(20);
    }
    std::vector<float> buf(r.blockFloats());
    ba::ipc::PoppedBlock b;
    while (r.pendientes() > 0) r.pop(buf.data(), b);
    while (std::chrono::steady_clock::now() < fin) {
        while (r.pendientes() > 0 && r.pop(buf.data(), b)) out.push_back(b.qpc);
        esperar(2);
    }
    return out;
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
    for (const char* e : {"intersink", "audiomixer", "baanillosink", "baretornos", "balimitador"})
        if (!hayFabrica(e)) {
            std::printf("SKIP: falta %s\n", e);
            return 77;
        }
    Motor::Config cfg;
    cfg.prefijoAnillos = "BA.Audio.PruebaMM" + std::to_string(getpid()) + ".";
    cfg.sinkEscucha = "fakesink sync=true";
    cfg.video = false;
    Motor motor(cfg);
    CHECK(motor.iniciar().empty(), "el motor arranca");
    const auto canal = [&](int n) { return cfg.prefijoAnillos + ba::ipc::sufijoRetorno("", n); };

    // --- El costo: 24 entradas, y después sus 24 retornos ---
    constexpr int kEntradas = 24;
    for (int n = 1; n <= kEntradas; ++n)
        CHECK(motor.agregarEntrada(id(n), tono(frecuencia(n), 0.03), {{"global", true}}).empty(), "un invitado");
    motor.esperarCola();
    esperar(1500);
    const int antes = hilos();
    const double cpuAntes = cpuDurante(3000);
    for (int n = 1; n <= kEntradas; ++n) CHECK(motor.retorno(n, "mix_minus", id(n)).empty(), "su retorno en mix_minus");
    motor.esperarCola();
    esperar(1500);
    const int despues = hilos();
    const double cpuDespues = cpuDurante(3000);
    std::printf("hilos: %d con %d entradas, %d con sus %d retornos (+%d)\n", antes, kEntradas, despues, kEntradas, despues - antes);
    std::printf("CPU del proceso: %.2f núcleos sin retornos, %.2f con los %d (+%.2f)\n", cpuAntes, cpuDespues, kEntradas,
                cpuDespues - cpuAntes);
    CHECK(antes > 0 && despues - antes <= kEntradas + 16, "una rama por entrada en un solo bus (lineal, no N×N)");

    // --- Nadie se oye a sí mismo; cada uno oye a todos los demás (12 lugares) ---
    for (int n = 13; n <= kEntradas; ++n) motor.retorno(n, "apagado", "");
    motor.esperarCola();
    esperar(500);
    for (int n : {1, 6, 12}) {
        const prueba::Lectura l = prueba::leer(canal(n), 1000, 2.0f);
        CHECK(l.abierto && l.muestras.size() > 48000, "el anillo del retorno existe y escribe");
        const double suya = prueba::goertzel(l.muestras, frecuencia(n));
        double menor = 1;
        for (int o = 1; o <= kEntradas; ++o)
            if (o != n) menor = std::min(menor, prueba::goertzel(l.muestras, frecuencia(o)));
        std::printf("%s: su voz %.5f, el menor de los otros %d: %.4f\n", canal(n).c_str(), suya, kEntradas - 1, menor);
        CHECK(suya < 0.002, "el invitado no se oye a sí mismo");
        CHECK(menor > 0.02, "oye a todos los demás");
    }
    // Los apagados no tienen anillo.
    CHECK(!prueba::leer(canal(20), 300).abierto, "un retorno apagado no escribe");

    // --- Entrar y salir no corta a los demás ---
    std::atomic<bool> listo{false};
    std::vector<std::uint64_t> s1;
    std::thread lector([&]() {
        s1 = sellos(canal(1), 3000);
        listo = true;
    });
    esperar(500);
    CHECK(motor.agregarEntrada("inv25", tono(frecuencia(25), 0.03), {{"global", true}}).empty(), "entra otro");
    CHECK(motor.retorno(25, "mix_minus", "inv25").empty(), "con su retorno");
    esperar(300);
    CHECK(motor.retorno(6, "apagado", "").empty(), "a uno se le apaga");
    esperar(300);
    CHECK(motor.retorno(6, "mix_minus", id(6)).empty(), "y vuelve");
    esperar(300);
    CHECK(motor.retorno(12, "programa", id(12)).empty(), "otro pasa a programa");
    esperar(300);
    CHECK(motor.quitarEntrada("inv25").empty(), "se va el que entró");
    CHECK(motor.retorno(25, "apagado", "").empty(), "y su retorno");
    lector.join();
    std::uint64_t mayor = 0;
    for (std::size_t i = 1; i < s1.size(); ++i) mayor = std::max(mayor, s1[i] - s1[i - 1]);
    std::printf("retorno 1 mientras entran y salen: %zu bloques, el mayor salto entre sellos %.1f ms\n", s1.size(), mayor / 1e6);
    CHECK(s1.size() >= 280, "el retorno 1 siguió escribiendo (~300 bloques en 3 s)");
    CHECK(mayor < 25'000'000ULL, "sin cortes (los sellos van de a 10 ms)");

    // --- En «programa» se oye; de vuelta en mix_minus, no ---
    motor.esperarCola();
    esperar(400);
    double suya = prueba::goertzel(prueba::leer(canal(12), 800, 2.0f).muestras, frecuencia(12));
    std::printf("retorno 12 en programa: su voz %.4f\n", suya);
    CHECK(suya > 0.02, "en programa el invitado se oye");
    CHECK(motor.retorno(12, "mix_minus", id(12)).empty(), "de vuelta a mix_minus");
    motor.esperarCola();
    esperar(400);
    suya = prueba::goertzel(prueba::leer(canal(12), 800, 2.0f).muestras, frecuencia(12));
    std::printf("retorno 12 de vuelta en mix_minus: su voz %.5f\n", suya);
    CHECK(suya < 0.002, "de vuelta en mix_minus no se oye");
    const double vuelto = prueba::goertzel(prueba::leer(canal(6), 800, 2.0f).muestras, frecuencia(6));
    CHECK(vuelto < 0.002, "el que volvió tampoco se oye");

    motor.detener();
    std::printf("== test_motor_mixminus: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
