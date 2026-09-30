// ============================================================================
// src/core/tests/test_diario_arranque.cpp — el diario de arranque
//
//   1. Un arranque que termina bien no deja culpable.
//   2. Un arranque que muere cargando algo deja ESE algo como culpable.
//   3. A la segunda rotura, eso queda apagado y no se carga mas.
//   4. Si carga bien, se le perdonan las roturas anteriores.
//   5. Prenderlo a mano le borra los antecedentes.
//   6. El numero de arranque sube, para poder cruzarlo con el registro.
//   7. Un archivo roto o a medias no impide arrancar.
// ============================================================================

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "diag/DiarioDeArranque.hpp"

namespace fs = std::filesystem;
using ba::diag::DiarioDeArranque;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ba-test-diario";
    const fs::path arch = dir / "arranque.json";
    std::error_code ec;
    fs::remove_all(dir, ec);

    // 1 y 6: arranque limpio, que termina.
    {
        DiarioDeArranque d(arch);
        CHECK(d.culpable().empty(), "el primer arranque no tiene culpable");
        CHECK(d.arranque() == 1, "y es el numero 1");
        d.empieza("vinculos");
        d.termina("vinculos");
        d.arranqueOk();
    }
    {
        DiarioDeArranque d(arch);
        CHECK(d.culpable().empty(), "si el anterior termino bien, no hay culpable");
        CHECK(d.arranque() == 2, "el numero de arranque sube");
    }

    // 2: se muere cargando algo (no se llama a termina ni a arranqueOk).
    {
        DiarioDeArranque d(arch);
        d.empieza("tarjeta-rota");
        // aca "se cuelga la maquina": nada mas se escribe
    }
    {
        DiarioDeArranque d(arch);
        CHECK(d.culpable() == "tarjeta-rota", "el que quedo a medias es el culpable");
        CHECK(d.roturas("tarjeta-rota") == 1, "lleva una rotura");
        CHECK(!d.apagado("tarjeta-rota"), "con una sola todavia se le da otra chance");
        // Y se vuelve a morir cargando lo mismo.
        d.empieza("tarjeta-rota");
    }

    // 3: a la segunda, apagada.
    {
        DiarioDeArranque d(arch);
        CHECK(d.roturas("tarjeta-rota") == 2, "van dos roturas");
        CHECK(d.apagado("tarjeta-rota"), "a la segunda queda apagada");
        const auto ap = d.apagados();
        CHECK(ap.size() == 1 && ap[0] == "tarjeta-rota", "y aparece en la lista de apagados");

        // El envoltorio la saltea sola.
        ba::diag::abrirDiario(arch);
        ba::diag::Cargando c("tarjeta-rota");
        CHECK(c.saltear(), "el envoltorio dice que no hay que cargarla");
    }

    // 4: algo que rompio una vez y despues carga bien queda limpio.
    {
        ba::diag::abrirDiario(arch);
        ba::diag::diario().empieza("a-medias");
    }
    {
        ba::diag::abrirDiario(arch);
        CHECK(ba::diag::diario().roturas("a-medias") == 1, "quedo con una rotura");
        { ba::diag::Cargando c("a-medias"); } // carga bien
    }
    {
        ba::diag::abrirDiario(arch);
        CHECK(ba::diag::diario().roturas("a-medias") == 0, "cargar bien le borra la rotura");
    }

    // 5: prenderla a mano.
    {
        ba::diag::abrirDiario(arch);
        ba::diag::diario().setApagado("tarjeta-rota", false);
        CHECK(!ba::diag::diario().apagado("tarjeta-rota"), "se puede prender a mano");
        CHECK(ba::diag::diario().roturas("tarjeta-rota") == 0, "y se le borran los antecedentes");
    }

    // 6b: modo seguro apaga TODO pero NO deja antecedentes: salir del modo
    // seguro tiene que alcanzar para volver a la normalidad.
    {
        ba::diag::abrirDiario(arch);
        auto& d = ba::diag::diario();
        d.setModoSeguro(true);
        CHECK(d.apagado("dashboard"), "en modo seguro no se carga nada");
        CHECK(d.apagados().empty(), "pero no queda anotado como apagado");
        d.setModoSeguro(false);
        CHECK(!d.apagado("dashboard"), "y al salir vuelve solo");
    }

    // 7: un archivo roto no impide arrancar.
    {
        std::ofstream f(arch, std::ios::binary);
        f << "{ esto no es json, se corto la luz a la mitad";
    }
    {
        DiarioDeArranque d(arch);
        CHECK(d.culpable().empty(), "un archivo roto no inventa un culpable");
        CHECK(!d.apagado("lo-que-sea"), "ni apaga nada");
        d.arranqueOk();
    }

    fs::remove_all(dir, ec);
    if (g_fallos == 0) std::printf("test_diario_arranque: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
