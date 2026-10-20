// test_latencia_estudio.cpp — la latencia del estudio, estable (Reloj::revisarLatencia)
//
// Cambiarla en vivo mueve la línea de tiempo de todas las salidas: no sigue cada medición.
// - un pedido más alto que no se sostiene (un pico) no la sube;
// - sostenido 2 s, sube (una vez);
// - lo que piden baja: sin permiso no baja (con algo al aire); a pedido, sí;
// - lo que pasa el tope no arrastra; quién la marca.

#include "Reloj.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace ba::motor;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
}
constexpr std::int64_t ms = 1000000;
} // namespace

int main() {
    gst_init(nullptr, nullptr);
    Reloj r;
    r.anotar("camara 1", 80 * ms);
    r.anotar("camara 2", 100 * ms);
    r.saltearArranque();
    check(r.latencia() == 100 * ms && r.laMarca() == "camara 2", "fijada en lo que pide la más lenta (100), la marca la cámara 2");

    r.anotar("camara 1", 150 * ms); // un pico
    r.revisarLatencia(false);
    check(r.latencia() == 100 * ms, "un pedido más alto no la sube enseguida");
    r.anotar("camara 1", 80 * ms); // se le pasó
    r.revisarLatencia(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    r.revisarLatencia(false);
    check(r.latencia() == 100 * ms, "el pico que no se sostuvo no la subió");

    r.anotar("camara 1", 150 * ms);
    r.revisarLatencia(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    const bool cambio = r.revisarLatencia(false);
    check(cambio && r.latencia() == 150 * ms && r.laMarca() == "camara 1", "sostenido 2 s: sube a 150 (y avisa que cambió)");

    r.anotar("camara 1", 60 * ms);
    for (int i = 0; i < 3; ++i) r.revisarLatencia(false);
    check(r.latencia() == 150 * ms && r.puedeBajarse() && r.objetivo() == 100 * ms,
          "piden menos, al aire (sin permiso): queda en 150 y avisa que puede bajar a 100");
    check(r.bajarLatencia() && r.latencia() == 100 * ms && !r.puedeBajarse(), "a pedido: baja a 100");
    check(!r.bajarLatencia(), "ya está en lo que piden: no hace nada");

    r.anotar("superres", 900 * ms); // pasa el tope (250): no arrastra
    r.revisarLatencia(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    r.revisarLatencia(false);
    check(r.latencia() == 100 * ms && r.laMarca() == "camara 2", "lo que pasa el tope no arrastra");

    std::printf("test_latencia_estudio: %s\n", fallos == 0 ? "OK" : "FALLAS");
    return fallos == 0 ? 0 : 1;
}
