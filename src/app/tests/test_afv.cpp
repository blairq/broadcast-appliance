// El audio que sigue al video (escenas::Afv; PLAN-SALIDAS, etapa 2): la cuenta de cada
// salida es suya. Dos salidas con escenas distintas, la misma escena en las dos, el
// nivel que se recuerda al bajar y vuelve al subir, y transiciones.json de antes
// (que es el de la por omisión).
#include "escenas/Afv.hpp"

#include <cstdio>
#include <map>

using namespace ba::app::escenas;
using json = nlohmann::json;

static int fallas = 0;
#define CHECK(c, m)                        \
    do {                                   \
        if (!(c)) {                        \
            std::printf("FALLA: %s\n", m); \
            ++fallas;                      \
        } else {                           \
            std::printf("ok: %s\n", m);    \
        }                                  \
    } while (0)

namespace {
/// Una salida de prueba: su AFV y el fader de cada fuente en SU mezcla.
struct Salida {
    Afv afv;
    std::map<std::string, float> fader;
    /// Corta a una escena y deja los faders donde terminan los fundidos.
    void cortar(const std::vector<std::string>& escena) {
        bool recordo = false;
        for (const auto& f : afv.alCortar(escena, [this](const std::string& n) { return fader.count(n) ? fader[n] : 1.0f; }, &recordo))
            fader[f.fuente] = f.hasta;
    }
};
} // namespace

int main() {
    const std::vector<std::string> planoA{"Cámara 1", "Mic A"}, planoB{"Cámara 2", "Mic B"};
    Salida a, b;
    a.afv.fuentes = {"Mic A", "Mic B"};
    b.afv.fuentes = {"Mic A", "Mic B"};
    a.fader = b.fader = {{"Mic A", 0.8f}, {"Mic B", 0.5f}};
    // Escenas distintas: cada salida sube lo suyo y baja lo otro, sin tocar la otra.
    a.cortar(planoA);
    b.cortar(planoB);
    // (lo que entra sin nivel recordado va a 0 dB, como siempre)
    CHECK(a.fader["Mic A"] == 1.0f && a.fader["Mic B"] == 0.0f, "salida A en el plano A: sube Mic A, baja Mic B");
    CHECK(b.fader["Mic B"] == 1.0f && b.fader["Mic A"] == 0.0f, "salida B en el plano B: sube Mic B, baja Mic A");
    CHECK(a.afv.niveles["Mic B"] == 0.5f && b.afv.niveles["Mic A"] == 0.8f, "cada una recuerda el nivel de lo que bajó");
    // El operador de A cambia el nivel de Mic A: B no se entera.
    a.fader["Mic A"] = 0.3f;
    a.cortar(planoB);
    a.cortar(planoA);
    CHECK(a.fader["Mic A"] == 0.3f, "A vuelve al plano A con su nivel nuevo");
    b.cortar(planoA);
    CHECK(b.fader["Mic A"] == 0.8f && b.fader["Mic B"] == 0.0f, "B en el plano A con el nivel de B, no el de A");
    // La misma escena en las dos: las dos suben lo mismo, cada una a su nivel.
    CHECK(a.fader["Mic A"] > 0 && b.fader["Mic A"] > 0 && a.fader["Mic A"] != b.fader["Mic A"], "la misma escena en las dos: cada una a su nivel");
    // Una fuente que no sigue al video en una salida no se mueve en ella.
    Salida c;
    c.afv.fuentes = {"Mic A"};
    c.fader = {{"Mic A", 1.0f}, {"Mic B", 0.7f}};
    c.cortar(planoA);
    CHECK(c.fader["Mic B"] == 0.7f, "sin AFV en esa salida, el fader queda");
    // Sin nivel recordado, entra a 0 dB (como siempre).
    Salida d;
    d.afv.fuentes = {"Mic B"};
    d.fader = {{"Mic B", 0.0f}};
    d.cortar(planoB);
    CHECK(d.fader["Mic B"] == 1.0f, "sin nivel recordado: entra a 1");
    // transiciones.json: el de siempre se lee igual, y guardar y leer da lo mismo.
    const json viejo = {{"entrada", json::object()}, {"afv", {"Mic A"}}, {"afv_niveles", {{"Mic A", 0.6}, {"Otra", 0.2}}}};
    const Afv v = Afv::deJson(viejo);
    CHECK(v.fuentes.count("Mic A") == 1 && v.niveles.at("Mic A") == 0.6f, "transiciones.json de antes: el AFV de la por omisión");
    const json g = v.aJson();
    CHECK(g["afv"] == json({"Mic A"}) && !g["afv_niveles"].contains("Otra"), "guarda solo los niveles de las que siguen al video");
    CHECK(Afv::deJson(g).aJson() == g, "ida y vuelta");
    std::printf(fallas == 0 ? "TODO OK\n" : "%d FALLAS\n", fallas);
    return fallas == 0 ? 0 : 1;
}
