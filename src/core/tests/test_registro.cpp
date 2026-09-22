// ============================================================================
// src/core/tests/test_registro.cpp — el registro y su lector
//
//   1. Una linea del Registro se vuelve a leer entera (ida y vuelta).
//   2. Se lee TAMBIEN lo viejo: el formato `[hora] nivel | comp | cod | ctx`
//      del launcher y del gateway.
//   3. Lo que NO esta estructurado (libobs, Qt, un printf suelto) no se tira:
//      entra con el componente del archivo. Es justo lo que suele explicar el
//      problema.
//   4. El nivel filtra de ahi para arriba, y lo de abajo ni se registra.
//   5. Los filtros: componente, texto y desde cuando.
//   6. El anillo en memoria no crece sin fin y devuelve lo ULTIMO.
//   7. Varios archivos se entrelazan POR HORA, que es el punto de que la hora
//      sea UTC.
// ============================================================================

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "diag/LectorDeLogs.hpp"
#include "diag/Registro.hpp"

namespace fs = std::filesystem;
using namespace ba::diag;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                       \
            ++g_fallos;                                                                            \
        }                                                                                          \
    } while (0)

void escribir(const fs::path& p, const std::string& contenido) {
    std::ofstream f(p, std::ios::binary);
    f << contenido;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "ba-test-registro";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 1. Ida y vuelta.
    {
        Registrado r;
        r.ms = 1758546202145LL;
        r.nivel = Nivel::Aviso;
        r.componente = "motor";
        r.codigo = 42;
        r.texto = "no arranco la camara";
        const std::string linea = r.aLinea();
        CHECK(linea.rfind("2025-09-22T", 0) == 0 || linea.rfind("20", 0) == 0,
              "la linea empieza con la fecha");

        Registrado v;
        CHECK(partirLinea(linea, "otro", v), "se vuelve a partir");
        CHECK(v.ms == r.ms, "la hora sobrevive al milisegundo");
        CHECK(v.nivel == Nivel::Aviso, "el nivel sobrevive");
        CHECK(v.componente == "motor", "el modulo sobrevive");
        CHECK(v.codigo == 42, "el codigo sobrevive");
        CHECK(v.texto == "no arranco la camara", "y la descripcion");
    }

    // 2 y 3: los formatos que ya existian, y lo que no tiene formato.
    {
        Registrado v;
        CHECK(partirLinea("[2026-01-02T03:04:05.006Z] error | gateway | 7 | se cayo", "x", v),
              "el formato viejo del gateway se lee igual");
        CHECK(v.nivel == Nivel::Error && v.componente == "gateway" && v.codigo == 7,
              "con su nivel, su modulo y su codigo");
        CHECK(v.ms > 0, "y su hora");

        CHECK(!partirLinea("info: [obs] cargando modulo x", "app", v),
              "una linea de libobs no tiene el formato");
        CHECK(v.componente == "app", "pero se le pone el modulo del archivo");
        CHECK(v.texto == "info: [obs] cargando modulo x", "y NO se tira el texto");
        CHECK(v.nivel == Nivel::Info, "queda como info, que es lo honesto");
    }

    // 4. El nivel.
    {
        Registro reg;
        reg.setEspejoEnStderr(false);
        reg.setNivel(Nivel::Aviso);
        CHECK(!reg.registra(Nivel::Info), "info esta por debajo de aviso");
        CHECK(reg.registra(Nivel::Error), "error esta por arriba");
        reg.info("esto no se registra");
        reg.error("esto si");
        CHECK(reg.cuenta() == 1, "solo se registro una");
        CHECK(reg.descartadas() == 1, "y la otra se conto como descartada");

        // 6. El anillo devuelve lo ultimo.
        reg.setNivel(Nivel::Traza);
        for (int i = 0; i < 20; ++i) reg.info("linea " + std::to_string(i));
        const auto u = reg.ultimas(3);
        CHECK(u.size() == 3, "pide tres, da tres");
        CHECK(u.back().texto == "linea 19", "y la ultima es la ultima");
    }

    // 5 y 7: los filtros y el entrelazado por hora.
    {
        escribir(dir / "app.log",
                 "2026-01-02T03:04:01.000Z info | motor | 0 | escena al aire\n"
                 "2026-01-02T03:04:03.000Z error | motor | 5 | se cayo la camara\n"
                 "una linea suelta de libobs\n");
        escribir(dir / "gateway.log",
                 "2026-01-02T03:04:02.000Z aviso | sala | 9 | el invitado no contesta\n");

        Filtro f;
        auto todo = leerLogs(dir, f);
        CHECK(todo.size() == 4, "se leen las cuatro lineas de los dos archivos");
        CHECK(todo[0].texto == "escena al aire", "la mas vieja primero");
        CHECK(todo[1].componente == "sala", "el del otro archivo se entrelaza por hora");
        CHECK(todo[2].texto == "se cayo la camara", "y despues la que sigue");
        CHECK(todo[3].ms == todo[2].ms, "la linea sin hora hereda la de la anterior");

        f.minimo = Nivel::Aviso;
        CHECK(leerLogs(dir, f).size() == 2, "el nivel minimo deja dos");

        f = Filtro{};
        f.componentes = {"sala"};
        const auto soloSala = leerLogs(dir, f);
        CHECK(soloSala.size() == 1 && soloSala[0].componente == "sala", "filtra por modulo");

        f = Filtro{};
        f.busca = "CAMARA";
        CHECK(leerLogs(dir, f).size() == 1, "busca sin distinguir mayusculas");
        f.busca = "motor";
        // Dos, no tres: la linea suelta de libobs no es del modulo «motor»
        // sino del archivo («app»), porque no dice de que modulo viene.
        CHECK(leerLogs(dir, f).size() == 2, "y busca tambien en el nombre del modulo");

        f = Filtro{};
        f.desdeMs = 1767322982000LL; // 2026-01-02T03:03:02Z, antes de todo
        CHECK(leerLogs(dir, f).size() == 4, "desde antes de todo, esta todo");
        f.desdeMs = 4102444800000LL; // 2100
        CHECK(leerLogs(dir, f).empty(), "desde el futuro, no hay nada");

        // Tres: motor, sala y «app», que es donde cae lo que no dice de donde
        // viene. Que aparezca como modulo propio es lo que se quiere: en el
        // visor se puede mirar o esconder la salida cruda.
        const auto comps = componentesEnLogs(dir);
        CHECK(comps.size() == 3, "los modulos que aparecen, mas el del archivo");
    }

    fs::remove_all(dir, ec);
    if (g_fallos == 0) std::printf("test_registro: todo bien\n");
    return g_fallos == 0 ? 0 : 1;
}
