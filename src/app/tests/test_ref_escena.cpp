// ============================================================================
// src/app/tests/test_ref_escena.cpp — lo que apunta a una escena la sigue por su id
// (escenas/RefEscena.hpp): renombrarla no deja nada con el nombre viejo, y lo guardado
// de antes (solo el nombre) pasa al id sin perder nada. La línea de tiempo, las
// transiciones de entrada, el contenido del DSK, los botones y lo editado a mano.
// ============================================================================
#include "escenas/RefEscena.hpp"
#include "evento/Linea.hpp"
#include "evento/LineaEdicion.hpp"

#include <cstdio>
#include <set>

using namespace ba::app::escenas;
using json = nlohmann::json;
namespace ln = ba::evento::linea;

namespace {
int fallas = 0;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            std::printf("FALLA: %s (%s, linea %d)\n", msg, #c, __LINE__);               \
            ++fallas;                                                                     \
        } else {                                                                          \
            std::printf("ok: %s\n", msg);                                                 \
        }                                                                                 \
    } while (0)

// Dos escenas; después «Cámara» pasa a llamarse «Plano general» (el mismo id).
const TablaEscenas antes({{"esc-00000001", "Cámara"}, {"esc-00000002", "Placa"}});
const TablaEscenas despues({{"esc-00000001", "Plano general"}, {"esc-00000002", "Placa"}});

void testTabla() {
    CHECK(antes.id("Cámara") == "esc-00000001" && antes.id("esc-00000002") == "esc-00000002", "por nombre o por id");
    CHECK(antes.id("No está").empty() && antes.nombre("esc-00000009").empty(), "lo que no está, vacío");
    CHECK(despues.nombre("esc-00000001") == "Plano general", "el nombre de ahora por el id");
    std::set<std::string> usados;
    for (int i = 0; i < 200; ++i) {
        const std::string id = nuevoIdEscena([&](const std::string& x) { return usados.count(x) > 0; });
        CHECK(id.rfind(kPrefijoId, 0) == 0 && id.size() == 12 && usados.insert(id).second, "un id nuevo, corto y que no está");
        if (fallas) break;
    }
}

void testRef() {
    // Un archivo de antes: solo el nombre. Toma el id.
    json r{{"escena", "Cámara"}, {"otra", 1}};
    CHECK(seguirRef(r, antes) && r["escena_id"] == "esc-00000001" && r["otra"] == 1, "lo de antes toma el id sin perder nada");
    CHECK(!seguirRef(r, antes), "al día: no cambia nada");
    // El renombre: el id manda, el nombre se pone al día.
    CHECK(seguirRef(r, despues) && r["escena"] == "Plano general" && r["escena_id"] == "esc-00000001", "renombrada: sigue");
    CHECK(apunta(r, despues) == "esc-00000001", "al motor, el id");
    // Una escena que no está: queda como está (y se pide por el nombre).
    json x{{"escena", "Fantasma"}};
    CHECK(!seguirRef(x, despues) && !x.contains("escena_id") && apunta(x, despues) == "Fantasma", "una que no está, igual");
    // Un id de otra corrida (no llegó a guardarse app.json) con un nombre que sí está.
    json y{{"escena", "Placa"}, {"escena_id", "esc-ffffffff"}};
    CHECK(seguirRef(y, despues) && y["escena_id"] == "esc-00000002", "un id que no está toma el del nombre");
    // Un id puesto donde va el nombre.
    json z{{"escena", "esc-00000002"}};
    CHECK(seguirRef(z, despues) && z["escena"] == "Placa" && z["escena_id"] == "esc-00000002", "un id en el nombre se entiende");
    // Lo editado a mano: otra escena con el id de antes. Sin soltarlo, el id pisaría lo editado.
    json viejo{{"escena", "Plano general"}, {"escena_id", "esc-00000001"}};
    json nuevo = viejo;
    nuevo["escena"] = "Placa";
    soltarIdViejo(viejo, nuevo);
    CHECK(!nuevo.contains("escena_id") && seguirRef(nuevo, despues) && nuevo["escena_id"] == "esc-00000002",
          "editada a otra escena: el id es el de la nueva");
    json igual = viejo;
    soltarIdViejo(viejo, igual);
    CHECK(igual.contains("escena_id"), "sin tocar el nombre, el id queda");
    // Con otras claves (los botones: `name` y `escena_id`).
    json args{{"name", "Cámara"}};
    CHECK(seguirRef(args, antes, "name", "escena_id") && args["escena_id"] == "esc-00000001", "con otras claves");
    // Sin escenas a la vista (la primera foto todavía no llegó): no se toca nada.
    json w{{"escena", "Cámara"}};
    CHECK(!seguirRef(w, TablaEscenas()) && !w.contains("escena_id"), "sin escenas, nada");
}

void testLista() {
    // El contenido de una capa DSK: escenas y fuentes mezcladas.
    json nombres = json::array({"Cámara", "Logo"}), ids = json::array();
    CHECK(seguirLista(nombres, ids, antes) && ids == json::array({"esc-00000001", ""}), "la escena toma el id; la fuente, no");
    CHECK(seguirLista(nombres, ids, despues) && nombres == json::array({"Plano general", "Logo"}), "renombrada en la capa");
    CHECK(listaParaPedir(nombres, ids, despues) == std::vector<std::string>({"esc-00000001", "Logo"}), "al motor: el id y la fuente");
}

void testClaves() {
    // transiciones.json de antes: por el nombre.
    json tr{{"Cámara", {{"tipo", "fade"}, {"ms", 500}}}, {"Borrada", {{"tipo", "cut"}, {"ms", 0}}}};
    CHECK(seguirClaves(tr, antes) && tr.contains("esc-00000001") && !tr.contains("Cámara") && !tr.contains("Borrada") &&
              tr["esc-00000001"]["ms"] == 500 && tr["esc-00000001"]["escena"] == "Cámara",
          "las transiciones pasan al id (con el nombre al lado) y la de una borrada se va");
    CHECK(seguirClaves(tr, despues) && tr["esc-00000001"]["escena"] == "Plano general" && tr["esc-00000001"]["tipo"] == "fade",
          "renombrada: la transición sigue, con el nombre nuevo");
    CHECK(!seguirClaves(tr, despues), "al día");
    json v = tr;
    CHECK(!seguirClaves(v, TablaEscenas()) && v == tr, "sin escenas a la vista no se borra nada");
}

ln::Linea linea() {
    return ln::Linea::deJson(json::parse(R"({
        "version": 2, "salidas": ["principal"],
        "pistas": [{"id":"escena-principal","tipo":"escena","salida":"principal"}],
        "bloques": [{"id":"b","nombre":"B","pasos":[
            {"id":"p1","piezas":[{"id":"z1","pista":"escena-principal","tipo":"escena","datos":{"escena":"Cámara","transicion":"fade"}}]},
            {"id":"p2","piezas":[{"id":"z2","pista":"escena-principal","tipo":"escena","datos":{"escena":"Placa"}}]}]}],
        "estante": [{"id":"e1","pieza":{"id":"ze","tipo":"escena","datos":{"escena":"Cámara"}}}]})"));
}

void testLinea() {
    ln::Linea l = linea();
    // Un evento de antes: las piezas toman el id; lo demás queda igual.
    CHECK(ln::seguirEscenas(l, antes), "la línea de antes pasa a ids");
    const ln::Paso* p1 = l.paso("p1");
    CHECK(p1->piezas[0].datos["escena_id"] == "esc-00000001" && p1->piezas[0].datos["transicion"] == "fade", "con el id, sin perder nada");
    CHECK(l.estante[0].pieza->datos["escena_id"] == "esc-00000001", "el estante también");
    CHECK(!ln::seguirEscenas(l, antes), "al día");
    // Se guarda y se vuelve a leer igual.
    l = ln::Linea::deJson(l.aJson());
    CHECK(ln::escenaParaPedir(l, *l.paso("p1"), "principal") == "esc-00000001", "se pide por el id");
    // Renombrada: el cue sigue, con el nombre nuevo para mostrar.
    CHECK(ln::seguirEscenas(l, despues) && ln::escenaDe(l, *l.paso("p1"), "principal") == "Plano general", "el cue sigue al renombre");
    CHECK(ln::escenaParaPedir(l, *l.paso("p1"), "principal") == "esc-00000001", "y la pide por el mismo id");
    // Editada a otra escena con los datos de antes (el editor devuelve el id viejo).
    const ln::Edicion e = ln::editar(
        l, json::parse(R"([{"op":"cambiar","id":"z1","campos":{"datos":{"escena":"Placa","escena_id":"esc-00000001"}}}])"));
    CHECK(e.ok, e.error.c_str());
    ln::Linea l2 = e.linea;
    ln::seguirEscenas(l2, despues);
    CHECK(ln::escenaDe(l2, *l2.paso("p1"), "principal") == "Placa" &&
              ln::escenaParaPedir(l2, *l2.paso("p1"), "principal") == "esc-00000002",
          "editada a otra: queda la nueva (el id viejo no la pisa)");
    // Lo mismo desde el guion de cues (proyectar → editar el cue → reconciliar).
    ln::Proyeccion pr = ln::proyectar(l);
    for (auto& b : pr.bloques)
        for (auto& c : b.cues)
            if (c.datos.value("escena", std::string()) == "Plano general") c.datos["escena"] = "Placa"; // el id queda (copia)
    ln::Linea l3 = ln::reconciliar(l, pr.bloques);
    ln::seguirEscenas(l3, despues);
    CHECK(ln::escenaDe(l3, *l3.paso("p1"), "principal") == "Placa", "editada en el guion: queda la nueva");
}

} // namespace

int main() {
    testTabla();
    testRef();
    testLista();
    testClaves();
    testLinea();
    if (fallas) std::printf("%d FALLAS\n", fallas);
    return fallas ? 1 : 0;
}
