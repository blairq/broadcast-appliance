// test_rostros.cpp — reconocer a alguien que ya estuvo
//
//   1. `parecido`: el coseno, sus bordes y sus casos degenerados.
//   2. `enderezar` (via el motor): que los cinco puntos caigan donde el modelo
//      los espera. Es el paso que decide si esto sirve.
//   3. Con los modelos presentes: detectar de verdad, y sobre todo que NO
//      invente caras donde no hay.
//   4. La libreta de personas: alta, edición, búsqueda, tope de caras, y que
//      guardar y volver a leer no pierda nada.
//
// SIN MODELOS SE SALTA (código 77), no se pasa: `make deps-rostros` los baja.
// Un test que dice OK sin haber ejercitado nada es peor que uno que falta.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "rostros/Personas.hpp"
#include "rostros/Rostros.hpp"

using namespace ba::rostros;

namespace {
int g_fallos = 0;
#define CHECK(cond, que)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("  FALLO: %s (linea %d)\n", (que), __LINE__);                                        \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

/// Un cuadro con un degradado y un rectángulo claro: no es una cara, pero
/// sirve para comprobar que el motor corre y no inventa detecciones.
std::vector<std::uint8_t> cuadroFalso(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = px.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            const bool dentro = x > w / 3 && x < 2 * w / 3 && y > h / 3 && y < 2 * h / 3;
            p[0] = p[1] = p[2] = dentro ? 220 : static_cast<std::uint8_t>(x * 255 / w);
            p[3] = 255;
        }
    return px;
}

std::string carpetaModelos() {
    for (const char* c : {"extern/rostros/modelos", "../extern/rostros/modelos",
                          "../../extern/rostros/modelos", "../../../extern/rostros/modelos"})
        if (std::filesystem::exists(std::filesystem::path(c) / "yunet.onnx")) return c;
    return {};
}
} // namespace

int main() {
    // --- 1) el coseno --------------------------------------------------------
    {
        const std::vector<float> a{1, 0, 0}, b{1, 0, 0}, c{0, 1, 0}, d{-1, 0, 0};
        CHECK(std::fabs(parecido(a, b) - 1.0f) < 1e-5f, "identicos = 1");
        CHECK(std::fabs(parecido(a, c)) < 1e-5f, "perpendiculares = 0");
        CHECK(parecido(a, d) < -0.99f, "opuestos = -1");
        // La escala no importa: lo que se compara es hacia donde apuntan.
        CHECK(std::fabs(parecido(a, {5, 0, 0}) - 1.0f) < 1e-5f, "la magnitud no cuenta");
        // Nada de esto puede tirar abajo el programa.
        CHECK(parecido({}, {}) == 0.0f, "vacios no rompen");
        CHECK(parecido(a, {1, 0}) == 0.0f, "distinto largo no rompe");
        CHECK(parecido({0, 0, 0}, a) == 0.0f, "el vector cero no rompe");
    }

    const std::string modelos = carpetaModelos();
    if (modelos.empty()) {
        std::printf("test_rostros: SALTADO (faltan los modelos; correr `make deps-rostros`)\n");
        return 77;
    }

    Motor m;
    if (!m.abrir(modelos)) {
        std::printf("test_rostros: no se pudieron abrir los modelos: %s\n", m.error().c_str());
        return 1;
    }
    CHECK(m.abierto(), "el motor abre con los modelos presentes");

    // --- 2) no inventa caras -------------------------------------------------
    // Importa tanto como encontrarlas: una deteccion falsa en el lobby le pone
    // el nombre de otro a alguien que ni siquiera esta.
    {
        const auto px = cuadroFalso(640, 480);
        const auto caras = m.detectar(px.data(), 640, 480);
        CHECK(caras.empty(), "un cuadro sin caras no da caras");
        CHECK(m.error().empty(), "y no fue porque fallara la llamada");
    }

    // --- 3) tamaños raros no rompen ------------------------------------------
    // El cuadro llega del video como venga: muy ancho, muy alto, diminuto.
    for (const auto [w, h] : {std::pair{1920, 1080}, std::pair{100, 640}, std::pair{32, 32}}) {
        const auto px = cuadroFalso(w, h);
        m.detectar(px.data(), w, h); // no se exige nada: que no se caiga
        CHECK(m.error().empty(), "un cuadro de proporciones raras no hace fallar la llamada");
    }

    // --- 4) argumentos imposibles --------------------------------------------
    CHECK(m.detectar(nullptr, 100, 100).empty(), "sin pixeles no hay caras");
    {
        const auto px = cuadroFalso(64, 64);
        CHECK(m.detectar(px.data(), 0, 64).empty(), "ancho cero no rompe");
        CHECK(m.describir(px.data(), 64, 64, Cara{}).size() == 128,
              "describir siempre devuelve 128 numeros si el motor esta abierto");
    }

    // --- 5) la libreta de personas -------------------------------------------
    {
        const auto tmp = std::filesystem::temp_directory_path() / "ba-personas-test.json";
        std::filesystem::remove(tmp);
        Personas lib;
        CHECK(lib.leer(tmp.string()), "sin archivo no es un error: todavia no hay libreta");
        CHECK(lib.todas().empty(), "y queda vacia");

        Persona ana;
        ana.nombre = "Ana Perez";
        ana.cargo = "Ministra";
        const std::string idAna = lib.guardarFicha(ana);
        CHECK(!idAna.empty(), "crear devuelve un id");
        Persona beto;
        beto.nombre = "Beto Gomez";
        const std::string idBeto = lib.guardarFicha(beto);
        CHECK(idAna != idBeto, "dos personas, dos ids");

        // Vectores separados a proposito: (1,0..) y (0,1,0..) no se parecen.
        std::vector<float> vA(128, 0.0f), vB(128, 0.0f), vAcasi(128, 0.0f);
        vA[0] = 1;
        vB[1] = 1;
        vAcasi[0] = 1;
        vAcasi[5] = 0.1f; // la misma cara, otra toma
        using Alta = ba::rostros::Personas::Alta;
        CHECK(lib.sumarCara(idAna, vA) == Alta::Sumada, "se le suma una cara");
        CHECK(lib.sumarCara("no-existe", vA) == Alta::NoExiste, "a quien no existe, no");
        CHECK(lib.sumarCara(idBeto, vB) == Alta::Sumada, "y a la otra la suya");

        const Coincidencia c = lib.buscar(vA);
        CHECK(c.id == idAna && c.parecido > 0.99f, "se encuentra a si misma");
        CHECK(lib.buscar(vB).id == idBeto, "y a la otra tambien");
        CHECK(lib.buscar({}).parecido == 0.0f, "un vector vacio no encuentra a nadie");

        // Casi iguales: no suma otra copia de la misma toma.
        CHECK(lib.sumarCara(idAna, vAcasi) == Alta::Repetida, "la casi igual se descarta, y lo dice");
        CHECK(lib.porId(idAna)->caras.size() == 1, "una toma casi igual no se guarda de nuevo");

        // Editar la ficha NO puede perder las caras: nadie espera que corregir
        // una tilde obligue a volver a mostrarle la cara al sistema.
        Persona edit = *lib.porId(idAna);
        edit.nombre = "Ana Pérez";
        edit.caras.clear();
        lib.guardarFicha(edit);
        CHECK(lib.porId(idAna)->caras.size() == 1, "editar la ficha no borra las caras");
        CHECK(lib.porId(idAna)->nombre == "Ana Pérez", "pero si cambia el nombre");

        // Tope de caras por persona: se va la mas vieja.
        for (int i = 0; i < 30; ++i) {
            std::vector<float> v(128, 0.0f);
            v[static_cast<std::size_t>(20 + i)] = 1;
            lib.sumarCara(idAna, v);
        }
        CHECK(lib.porId(idAna)->caras.size() == Personas::kMaxCarasPorPersona, "no crece sin limite");

        CHECK(lib.guardar(tmp.string()), "guarda");
        Personas otra;
        CHECK(otra.leer(tmp.string()), "y se vuelve a leer");
        CHECK(otra.todas().size() == 2, "con las dos personas");
        CHECK(otra.porId(idAna) != nullptr && otra.porId(idAna)->cargo == "Ministra", "con sus datos");
        CHECK(otra.buscar(vB).id == idBeto, "y sus caras siguen sirviendo");
        CHECK(otra.borrar(idBeto) && otra.todas().size() == 1, "borrar saca una");
        CHECK(!otra.borrar("no-existe"), "borrar lo que no hay avisa");

        // Fusionar: la misma persona anotada dos veces. Lo que importa es que
        // la ficha que queda reconozca por las caras de LAS DOS —si perdiera
        // las de la absorbida, fusionar seria un borrar con mas pasos—.
        Personas dup;
        Persona uno;
        uno.nombre = "Ana";
        uno.cargo = "Ministra";
        const std::string idUno = dup.guardarFicha(uno);
        Persona dos; // la que anoto el alta automatica: sin nombre
        dos.nota = "entra por la puerta lateral";
        const std::string idDos = dup.guardarFicha(dos);
        std::vector<float> vUno(128, 0.0f), vDos(128, 0.0f);
        vUno[0] = 1;
        vDos[7] = 1;
        dup.sumarCara(idUno, vUno);
        dup.sumarCara(idDos, vDos);
        CHECK(!dup.fusionar(idUno, idUno), "fusionar una consigo misma no");
        CHECK(!dup.fusionar(idUno, "no-existe"), "ni con una que no existe");
        CHECK(dup.fusionar(idUno, idDos), "fusiona");
        CHECK(dup.todas().size() == 1, "y queda una sola");
        CHECK(dup.buscar(vDos).id == idUno, "las caras de la absorbida siguen reconociendo");
        CHECK(dup.porId(idUno)->nombre == "Ana", "el nombre escrito gana");
        CHECK(dup.porId(idUno)->nota == "entra por la puerta lateral",
              "y lo que estaba vacio se completa con lo de la otra");
        std::filesystem::remove(tmp);
    }

    if (g_fallos == 0) {
        std::printf("test_rostros: OK\n");
        return 0;
    }
    std::printf("test_rostros: %d fallo(s)\n", g_fallos);
    return 1;
}
