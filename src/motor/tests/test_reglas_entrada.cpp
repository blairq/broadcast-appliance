// test_reglas_entrada.cpp — las reglas puras de una entrada (ReglasEntrada.hpp)
//
// El caso del 2026-11-01 (prueba de sala con 20/30/40 invitados): los lugares de la
// conferencia se rearmaban por «no llegan datos» con la persona conectada (19, 71 y 149
// veces), y el realineo corría segundos hacia adelante las horas del audio de un anillo.
//   - una entrada cualquiera sin datos el plazo se rearma, una vez (no en cada mirada);
//   - un lugar (anillo del gateway) nunca se rearma: avisa una vez y, si vuelve, lo dice;
//   - lo que nunca dio nada desde que empezó (armada o despertada) no es «dejó de llegar»;
//   - las horas de un anillo nunca se realinean; las de una fuente con sus horas, sí.

#include "ReglasEntrada.hpp"

#include <cstdio>
#include <string>

using ba::motor::realinearHoras;
using ba::motor::VigiaDatos;
using Que = VigiaDatos::Que;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
}
constexpr std::uint64_t S = 1'000'000'000ULL;
constexpr std::uint64_t kPlazo = 10 * S;
} // namespace

int main() {
    {
        // Una cámara que deja de mandar: se rearma una vez.
        VigiaDatos v;
        v.empezar(0, 0);
        check(v.mirar(0, 5 * S, kPlazo, false) == Que::Nada, "sin datos todavía: nada");
        check(v.mirar(0, 20 * S, kPlazo, false) == Que::Nada, "nunca llegó nada desde que empezó: no se rearma");
        check(v.mirar(30, 21 * S, kPlazo, false) == Que::Llegan, "llegan datos");
        check(v.mirar(60, 22 * S, kPlazo, false) == Que::Llegan, "siguen llegando");
        check(v.mirar(60, 31 * S, kPlazo, false) == Que::Nada, "9 s sin datos: todavía no");
        check(v.mirar(60, 32 * S, kPlazo, false) == Que::Rearmar, "10 s sin datos: se rearma");
        check(v.mirar(60, 50 * S, kPlazo, false) == Que::Nada, "una vez: no en cada mirada");
        // Rearmada: cuenta de nuevo desde cero.
        v.empezar(0, 51 * S);
        check(v.mirar(0, 70 * S, kPlazo, false) == Que::Nada, "rearmada y sin nada: no se rearma en cadena");
        check(v.mirar(5, 71 * S, kPlazo, false) == Que::Llegan, "rearmada, vuelve a dar");
    }
    {
        // Un lugar de la conferencia: la persona sigue conectada (lo sabe el gateway).
        VigiaDatos v;
        v.empezar(0, 0);
        check(v.mirar(100, 1 * S, kPlazo, true) == Que::Llegan, "lugar: llegan bloques");
        check(v.mirar(100, 12 * S, kPlazo, true) == Que::Avisar, "lugar sin datos el plazo: se avisa, no se rearma");
        bool rearma = false, avisaOtraVez = false;
        for (std::uint64_t t = 13; t < 600; ++t) {
            const Que q = v.mirar(100, t * S, kPlazo, true);
            rearma = rearma || q == Que::Rearmar;
            avisaOtraVez = avisaOtraVez || q == Que::Avisar;
        }
        check(!rearma, "lugar: nunca se rearma (diez minutos sin datos)");
        check(!avisaOtraVez, "lugar: el aviso, una sola vez");
        check(v.avisado(), "lugar: queda avisado");
        check(v.mirar(101, 601 * S, kPlazo, true) == Que::Vuelven, "lugar: vuelve a llegar (se dice)");
        check(!v.avisado(), "lugar: ya no está avisado");
        check(v.mirar(101, 620 * S, kPlazo, true) == Que::Avisar, "lugar: otro corte, otro aviso");
    }
    {
        // Callado o con el micrófono apagado el gateway igual manda bloques (de silencio): el
        // vigía cuenta bloques, no sonido.
        VigiaDatos v;
        v.empezar(0, 0);
        std::uint64_t datos = 0;
        bool algo = false;
        for (std::uint64_t t = 1; t <= 3600; ++t) {
            datos += 100; // 100 bloques de silencio por segundo
            const Que q = v.mirar(datos, t * S, kPlazo, true);
            algo = algo || q == Que::Avisar || q == Que::Rearmar;
        }
        check(!algo, "callado una hora: ni aviso ni rearmado");
    }
    {
        // Al despertar (llegó alguien al lugar) se cuenta desde lo que había: lo de antes del
        // reposo no cuenta como «llegó algo» (el 2026-11-01 se rearmaban a los 10 s de despertar
        // sin haber dado nada, por lo que habían dado antes de dormirse).
        VigiaDatos v;
        v.empezar(0, 0);
        check(v.mirar(500, 1 * S, kPlazo, false) == Que::Llegan, "antes del reposo daba");
        v.empezar(500, 100 * S); // despierta
        check(v.mirar(500, 115 * S, kPlazo, false) == Que::Nada, "despertada y sin nada nuevo: no se rearma");
        check(v.mirar(510, 116 * S, kPlazo, false) == Que::Llegan, "despertada, primer dato");
        check(v.mirar(510, 127 * S, kPlazo, false) == Que::Rearmar, "y si después deja de dar, sí");
    }
    {
        check(!realinearHoras(true, true, 4497.0), "anillo del gateway corrido 4,5 s: no se realinea");
        check(!realinearHoras(true, true, -2000.0), "anillo del gateway adelantado: tampoco");
        check(realinearHoras(false, true, 4497.0), "fuente con sus horas corrida 4,5 s: se realinea");
        check(realinearHoras(false, true, -1500.0), "fuente con sus horas adelantada: se realinea");
        check(!realinearHoras(false, false, 4497.0), "no estable: no se toca");
        check(!realinearHoras(false, true, 800.0), "menos de un segundo: no se toca");
    }
    if (fallos == 0) std::printf("test_reglas_entrada: OK\n");
    return fallos == 0 ? 0 : 1;
}
