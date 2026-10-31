// ============================================================================
// src/app/tests/test_carrusel.cpp — la tira de imagenes de una escena
//
//   1. Agregar: no entra dos veces la misma, ni sin archivo, ni pasado el tope.
//   2. Saltar, que es la razon de que este carrusel exista.
//   3. Siguiente y anterior dan la vuelta.
//   4. Quitar deja parado donde corresponde.
//   5. Reordenar NO cambia la que esta al aire.
//   6. Etiquetas: se piden una sola vez, aunque salgan vacias.
//   7. Ida y vuelta por JSON, y un `actual` guardado que ya no existe.
// ============================================================================
#include "persist/ColaEscritura.hpp"
#include <cstdio>
#include <string>

#include <filesystem>
#include <fstream>

#include "carrusel/Carrusel.hpp"
#include "carrusel/CarruselStore.hpp"

using namespace ba::carrusel;
using json = nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

Lamina lam(const std::string& archivo, const std::string& titulo = {}) {
    Lamina l;
    l.archivo = archivo;
    l.titulo = titulo;
    return l;
}

Carrusel tresLaminas() {
    Carrusel c;
    c.setNombre("Presentacion");
    c.setFuente("Lamina");
    c.agregar(lam("/m/1.png", "Portada"));
    c.agregar(lam("/m/2.png", "Balance"));
    c.agregar(lam("/m/3.png", "Cierre"));
    return c;
}

// --- 1) Agregar -------------------------------------------------------------
void agregar() {
    Carrusel c;
    CHECK(c.vacio(), "arranca vacio");
    CHECK(c.actual() == -1, "y sin ninguna al aire");
    CHECK(c.lamina() == nullptr, "no hay lamina");

    CHECK(c.agregar(lam("/m/1.png")), "entra la primera");
    CHECK(c.tamano() == 1, "queda una");
    CHECK(c.actual() == 0, "y es la que esta al aire");

    // La misma imagen dos veces es casi siempre un descuido al armarla.
    CHECK(!c.agregar(lam("/m/1.png")), "la misma ruta no entra dos veces");
    CHECK(c.tamano() == 1, "sigue habiendo una");
    CHECK(!c.agregar(lam("")), "sin archivo no entra");

    for (std::size_t i = 0; i < Carrusel::kMax() + 10; ++i)
        c.agregar(lam("/m/relleno" + std::to_string(i) + ".png"));
    CHECK(c.tamano() == Carrusel::kMax(), "no pasa del tope");
}

// --- 2) Saltar --------------------------------------------------------------
void saltar() {
    Carrusel c = tresLaminas();
    CHECK(c.actual() == 0, "arranca en la primera");
    CHECK(c.ir(2), "salta a la tercera");
    CHECK(c.actual() == 2, "y quedo ahi");
    CHECK(c.lamina()->titulo == "Cierre", "es la que se pidio");
    // Saltar es lo que el operador hace tocando una tarjeta: de la ultima a la
    // primera, sin pasar por el medio.
    CHECK(c.ir(0), "vuelve a la primera de un salto");
    CHECK(c.lamina()->titulo == "Portada", "y es la que era");

    CHECK(!c.ir(-1), "fuera de rango no");
    CHECK(!c.ir(3), "ni pasado el final");
    CHECK(c.actual() == 0, "y no se movio");
}

// --- 3) Siguiente y anterior ------------------------------------------------
void rotar() {
    Carrusel c = tresLaminas();
    c.siguiente();
    CHECK(c.actual() == 1, "avanza");
    c.siguiente();
    c.siguiente();
    // Da la vuelta: en un bucle de fondo que corre toda la tarde, frenar en la
    // ultima dejaria la pantalla clavada.
    CHECK(c.actual() == 0, "da la vuelta al llegar al final");
    c.anterior();
    CHECK(c.actual() == 2, "y para atras tambien");

    Carrusel v;
    v.siguiente();
    v.anterior();
    CHECK(v.actual() == -1, "uno vacio no se mueve ni explota");
}

// --- 4) Quitar --------------------------------------------------------------
void quitar() {
    Carrusel c = tresLaminas();
    c.ir(1);
    CHECK(c.quitar(1), "saca la del medio, que era la que estaba");
    CHECK(c.tamano() == 2, "quedan dos");
    // Queda parado en la que OCUPO su lugar, no en la anterior: sacar una del
    // medio y seguir hacia adelante es lo que uno espera.
    CHECK(c.lamina()->titulo == "Cierre", "queda en la que ocupo su lugar");

    c.ir(1);
    CHECK(c.quitar(1), "saca la ultima, que era la que estaba");
    CHECK(c.actual() == 0, "queda en la que hay");
    CHECK(!c.quitar(9), "una posicion que no existe avisa");

    c.quitar(0);
    CHECK(c.vacio() && c.actual() == -1, "vaciada del todo, no hay actual");
}

// --- 5) Reordenar -----------------------------------------------------------
void reordenar() {
    Carrusel c = tresLaminas();
    c.ir(2); // "Cierre" al aire
    CHECK(c.mover(0, 2), "mueve la primera al final");
    CHECK(c.laminas()[2].titulo == "Portada", "quedo al final");
    // Reordenar la tira NO es cambiar de lamina: la que estaba al aire sigue.
    CHECK(c.lamina()->titulo == "Cierre", "la que estaba al aire sigue al aire");

    CHECK(!c.mover(0, 0), "moverla a su lugar no es un movimiento");
    CHECK(!c.mover(0, 9), "fuera de rango avisa");
}

// --- 6) Etiquetas -----------------------------------------------------------
void etiquetas() {
    Carrusel c = tresLaminas();
    CHECK(c.proximaSinAnalizar() != nullptr, "hay para analizar");
    CHECK(c.proximaSinAnalizar()->archivo == "/m/1.png", "empieza por la primera");

    CHECK(c.etiquetar("/m/1.png", "portada titulo del acto"), "se le ponen etiquetas");
    CHECK(c.proximaSinAnalizar()->archivo == "/m/2.png", "sigue por la que falta");

    // Una imagen que no dio nada NO se reintenta para siempre: son llamadas a
    // un modelo.
    CHECK(c.etiquetar("/m/2.png", ""), "se marca aunque salga vacia");
    CHECK(c.proximaSinAnalizar()->archivo == "/m/3.png", "y no vuelve sobre esa");

    c.etiquetar("/m/3.png", "cierre gracias");
    CHECK(c.proximaSinAnalizar() == nullptr, "cuando estan todas, no hay mas");
    CHECK(!c.etiquetar("/m/no-esta.png", "x"), "etiquetar lo que no esta avisa");
}

// --- 7) Ida y vuelta --------------------------------------------------------
void ida_y_vuelta() {
    Carrusel a = tresLaminas();
    a.setAvanceMs(4000);
    a.ir(1);
    a.etiquetar("/m/2.png", "grafico de barras");

    const Carrusel b = Carrusel::deJson(a.aJson());
    CHECK(b.tamano() == 3, "vuelven las tres");
    CHECK(b.nombre() == "Presentacion" && b.fuente() == "Lamina", "nombre y fuente sobreviven");
    CHECK(b.avanceMs() == 4000, "el avance tambien");
    CHECK(b.actual() == 1, "y donde estaba parado");
    CHECK(b.laminas()[1].etiquetas == "grafico de barras", "las etiquetas sobreviven");
    CHECK(b.laminas()[1].analizada, "y que ya fue analizada");

    // Un `actual` guardado cuando la tira era mas larga apuntaria afuera.
    json j = a.aJson();
    j["actual"] = 99;
    const Carrusel c = Carrusel::deJson(j);
    CHECK(c.actual() == 0, "un actual que ya no existe no rompe nada");

    // El avance se acota: menos de medio segundo es un parpadeo.
    Carrusel d;
    d.setAvanceMs(10);
    CHECK(d.avanceMs() == 500, "un avance ridiculo se sube al minimo");
    d.setAvanceMs(-5);
    CHECK(d.avanceMs() == 0, "y un negativo lo apaga");

    CHECK(Carrusel::deJson(json::array()).vacio(), "una lista no es un carrusel");
    CHECK(Carrusel::deJson(json("hola")).vacio(), "un texto tampoco");
}

// --- 8) La biblioteca -------------------------------------------------------
void biblioteca() {
    const std::string dir = (std::filesystem::temp_directory_path() / "ba_test_carruseles").string();
    std::filesystem::remove_all(dir);
    const std::string archivo = dir + "/carruseles.json";

    {
        Store s(archivo);
        CHECK(s.todos().empty(), "una biblioteca que no existe esta vacia, no rota");
        CHECK(s.error().empty(), "y eso no es un error");

        Carrusel sinNombre;
        sinNombre.agregar(lam("/m/1.png"));
        CHECK(!s.guardar(sinNombre), "uno sin nombre no se guarda: no se podria volver a encontrar");

        CHECK(s.guardar(tresLaminas()), "uno con nombre si");
        CHECK(s.todos().size() == 1, "queda uno");
        CHECK(s.porNombre("presentacion") != nullptr, "se lo encuentra sin importar mayusculas");
        CHECK(s.porNombre("PRESENTACION") != nullptr, "ni al reves");
        CHECK(s.porNombre("otro") == nullptr, "uno que no esta no aparece");

        // Guardar con el mismo nombre REEMPLAZA: es editar, no duplicar.
        Carrusel editado = tresLaminas();
        editado.setAvanceMs(7000);
        CHECK(s.guardar(editado), "se guarda de nuevo");
        CHECK(s.todos().size() == 1, "sigue habiendo uno solo");
        CHECK(s.porNombre("Presentacion")->avanceMs() == 7000, "con lo editado");
    }
    {
        // Otra instancia: lo que se guardo tiene que estar en el disco.
        Store s(archivo);
        CHECK(s.todos().size() == 1, "sobrevive a cerrar y abrir");
        CHECK(s.porNombre("Presentacion")->tamano() == 3, "con sus laminas");
        CHECK(!std::filesystem::exists(archivo + ".tmp"), "sin dejar el temporal tirado");

        CHECK(s.borrar("presentacion"), "se borra");
        CHECK(s.todos().empty(), "y queda vacia");
        CHECK(!s.borrar("no-existe"), "borrar lo que no esta avisa");
    }
    {
        // Un archivo roto SE DICE. Si se hiciera pasar por vacia, el operador
        // rearma todo sin saber que lo suyo sigue ahi.
        ba::persist::cola().vaciar(); // lo guardado antes, al disco: si no, se leería eso
        std::ofstream(archivo) << "{esto no es json";
        Store s(archivo);
        CHECK(!s.error().empty(), "una biblioteca rota avisa");
    }
    ba::persist::cola().vaciar();
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    agregar();
    saltar();
    rotar();
    quitar();
    reordenar();
    etiquetas();
    ida_y_vuelta();
    biblioteca();
    if (g_fallos == 0) std::printf("test_carrusel: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
