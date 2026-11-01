// ============================================================================
// src/app/tests/test_rostros_quien_es.cpp — quién es cada cara (rostros/QuienEs)
//
// Las reglas de Personas con una libreta en memoria y vectores de mentira (sin cámara, sin
// modelos, sin Qt):
//   1. Qué cara es quién: desde el parecido de un rótulo; debajo, nadie.
//   2. El anotado automático: tres pasadas antes de anotar una desconocida (con su foto
//      pendiente), se olvida la que no volvió, y a la conocida se le suma la pose.
//   3. «La vi recién»: se guarda cada tanto, no en cada vuelta.
//   4. Las etiquetas de la tarjeta: quién es cada cara con vector, solo si cambiaron.
//   5. La puerta: con el margen de abrir la puerta y una cara que sirve; un aviso por persona.
//   6. El lobby: la ronda rota y olvida a los que se fueron; entrar sola, una vez.
//   7. El alta: con nombre (o completando la anotada sola) y el alta automática del lobby.
// ============================================================================
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "rostros/QuienEs.hpp"

using namespace ba::rostros;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

/// Un vector de cara de mentira: el eje `i` (dos ejes distintos no se parecen nada).
std::vector<float> eje(int i) {
    std::vector<float> v(128, 0.0f);
    v[static_cast<std::size_t>(i)] = 1.0f;
    return v;
}

QuienEs::Visto visto(const std::vector<float>& v, int alto = 100, bool sirve = true) {
    QuienEs::Visto x;
    x.vector = v;
    x.cara.alto = alto;
    x.cara.ancho = alto;
    x.sirve = sirve;
    x.sugerencia = sirve ? "" : "acercate un poco";
    return x;
}

/// Una libreta con Ana (eje 0) y una anotada sola sin nombre (eje 1).
std::string conAna(Personas& l, std::string* sinNombre = nullptr) {
    Persona ana;
    ana.nombre = "Ana";
    ana.cargo = "Consejera";
    const std::string id = l.guardarFicha(ana);
    l.sumarCara(id, eje(0));
    const std::string otra = l.guardarFicha(Persona{});
    l.sumarCara(otra, eje(1));
    if (sinNombre != nullptr) *sinNombre = otra;
    return id;
}

void quienEs() {
    Personas l;
    const std::string ana = conAna(l);
    QuienEs q(l);
    CHECK(q.quien(eje(0)).id == ana && q.quien(eje(0)).nombre == "Ana", "la cara de Ana es Ana");
    const Coincidencia nadie = q.quien(eje(7));
    CHECK(nadie.id.empty() && nadie.nombre.empty(), "una cara que no se parece a nadie: nadie");
    const QuienEs::Resultado r = q.resolver({visto(eje(0)), visto(eje(7))}, false, 1000);
    CHECK(r.filas.size() == 2 && r.filas[0].id == ana && r.filas[1].id.empty(), "una fila por cara, en orden");
    CHECK(!r.alta && r.fotos.empty(), "sin registrar no se anota nada");
}

void anotado() {
    Personas l;
    conAna(l);
    QuienEs q(l);
    const std::size_t antes = l.todas().size();
    std::int64_t t = 1000;
    for (int i = 1; i < QuienEs::vecesParaAnotar(); ++i, t += 700) {
        const auto r = q.resolver({visto(eje(5))}, true, t);
        CHECK(!r.alta && r.filas[0].id.empty(), "una desconocida, todavía no se le cree");
    }
    auto r = q.resolver({visto(eje(5))}, true, t);
    CHECK(r.alta && l.todas().size() == antes + 1, "a la tercera pasada se anota sola");
    CHECK(r.fotos.size() == 1 && r.fotos[0].second == 0 && r.filas[0].id == r.fotos[0].first,
          "con su foto pendiente (la saca quien tiene el cuadro)");
    CHECK(l.porId(r.filas[0].id) != nullptr && l.porId(r.filas[0].id)->nombre.empty(), "sin nombre, a propósito");
    // La que no volvió a tiempo se olvida y empieza de nuevo.
    t += 700;
    q.resolver({visto(eje(9))}, true, t);
    q.resolver({visto(eje(9))}, true, t + 700);
    r = q.resolver({}, true, t + 700 + QuienEs::olvidoPendienteMs() + 1);
    r = q.resolver({visto(eje(9))}, true, t + 700 + QuienEs::olvidoPendienteMs() + 2);
    CHECK(!r.alta, "pasado el olvido, la cuenta vuelve a empezar");
    // A la conocida se le suma una pose distinta (y la libreta cambia).
    std::vector<float> otraPose = eje(0);
    otraPose[2] = 0.6f;
    r = q.resolver({visto(otraPose)}, true, t + 100000);
    CHECK(r.alta && !r.filas[0].id.empty(), "conocida con otra pose: se le suma");
}

void vistas() {
    Personas l;
    const std::string ana = conAna(l);
    QuienEs q(l);
    q.guardada(1000);
    auto r = q.resolver({visto(eje(0))}, true, 2000);
    CHECK(!r.guardarVistas, "recién guardada: «la vi recién» espera");
    CHECK(l.porId(ana)->vistaMs == 2000, "pero queda anotado");
    r = q.resolver({visto(eje(0))}, true, 2000 + QuienEs::guardarVistasMs());
    CHECK(r.guardarVistas, "pasado el rato, toca guardar");
    q.guardada(10000);
    CHECK(!q.vistas({ana}, 10001), "las del MCP: recién guardada, no");
    CHECK(q.vistas({ana}, 10001 + QuienEs::guardarVistasMs()), "las del MCP: pasado el rato, sí");
    CHECK(!q.vistas({}, 999999999), "sin nadie, nada que guardar");
}

void etiquetas() {
    Personas l;
    const std::string ana = conAna(l);
    QuienEs q(l);
    const json caras = json::array({{{"id", 3}, {"vector", eje(0)}}, {{"id", 4}, {"vector", eje(1)}}});
    const auto t = q.etiquetas("entrada:Cam 1", caras);
    CHECK(t.has_value(), "la primera vez, las etiquetas");
    const json e = json::parse(t.value_or("[]"));
    CHECK(e.size() == 1 && e[0].value("id", 0) == 3 && e[0].value("nombre", "") == "Ana" && e[0].value("persona", "") == ana,
          "solo la que tiene nombre (la anotada sola no rotula)");
    CHECK(!q.etiquetas("entrada:Cam 1", caras).has_value(), "lo mismo otra vez: no se repite");
    CHECK(q.etiquetas("entrada:Cam 2", caras).has_value(), "otra cámara: lo suyo");
    CHECK(!q.etiquetas("entrada:Cam 3", json::array({{{"id", 1}}})).has_value(), "sin vectores: nada");
}

void puerta() {
    Personas l;
    conAna(l);
    QuienEs q(l);
    const std::vector<QuienEs::Visto> caras{visto(eje(0)), visto(eje(0), 100, false), visto(eje(7))};
    const auto r = q.resolver(caras, false, 1000);
    const auto a = q.paraAcreditar(r, caras);
    CHECK(a.size() == 1 && a[0] == 0, "se acredita la reconocida que sirve (no la borrosa, no la desconocida)");
    CHECK(q.avisarPuerta("x") && !q.avisarPuerta("x"), "un aviso por persona");
    q.olvidarPuerta();
    CHECK(q.avisarPuerta("x"), "otra puerta: se avisa de nuevo");
}

void lobby() {
    Personas l;
    const std::string ana = conAna(l);
    QuienEs q(l);
    auto t = q.proximoDelLobby({{"u1", 1}, {"u2", 2}});
    const std::string primero = t.uid;
    t = q.proximoDelLobby({{"u1", 1}, {"u2", 2}});
    CHECK(!primero.empty() && t.uid != primero, "la ronda rota entre los que esperan");
    const std::vector<QuienEs::Visto> caras{visto(eje(7), 50), visto(eje(0), 120)};
    const auto r = q.resolver(caras, false, 1000);
    q.enLobby("u1", 1, r, caras, 640, 360);
    const json& v = q.enLobby()["u1"];
    CHECK(v.value("id", "") == ana && v.value("lugar", 0) == 1 && v.value("cargo", "") == "Consejera",
          "la baldosa: la cara más grande, con su ficha y su lugar");
    CHECK(v.value("cw", 0) == 640 && v.value("sirve", false), "con el tamaño del cuadro y si sirve");
    t = q.proximoDelLobby({{"u2", 2}});
    CHECK(t.idos.size() == 1 && t.idos[0] == "u1" && !q.enLobby().contains("u1"), "se fue: se olvida lo que se sabía");
    CHECK(q.podriaPasar("u2", 0.9f, true), "parecido de sobra y cara que sirve: podría pasar");
    CHECK(!q.podriaPasar("u2", 0.5f, true), "con parecido de rótulo, no");
    CHECK(!q.podriaPasar("u2", 0.9f, false), "con una cara que no sirve, no");
    q.paso("u2");
    CHECK(!q.podriaPasar("u2", 0.9f, true), "una sola vez por persona");
}

void altas() {
    Personas l;
    std::string sinNombre;
    const std::string ana = conAna(l, &sinNombre);
    QuienEs q(l);
    auto a = q.alta(visto(eje(0)), "Otra", "");
    CHECK(a.id == ana && !a.cambio, "la cara ya es de Ana: no se crea otra ficha");
    a = q.alta(visto(eje(1)), "Beto", "Prensa");
    CHECK(a.id == sinNombre && a.cambio && l.porId(sinNombre)->nombre == "Beto", "la anotada sola se completa");
    CHECK(a.sinFoto, "y pide su foto");
    a = q.alta(visto(eje(20)), "Caro", "");
    CHECK(!a.id.empty() && a.id != ana && a.id != sinNombre && a.cambio, "una cara nueva: ficha nueva");
    // El alta automática del lobby.
    Personas l2;
    std::string anon;
    const std::string ana2 = conAna(l2, &anon);
    QuienEs q2(l2);
    bool propia = true;
    auto p = q2.nombrar("u1", anon, true, "Dani", "Invitada", &propia);
    CHECK(p && p->nombre == "Dani" && p->cargo == "Invitada" && !propia, "sin nombre: toma el que dio");
    CHECK(!q2.nombrar("u1", anon, false, "Dani", ""), "con una cara que no sirve, no");
    CHECK(!q2.nombrar("u1", ana2, true, "Otra", ""), "una ficha nombrada a mano no se toca");
    l2.guardarFicha(*p);
    q2.nombrada("u1", anon);
    p = q2.nombrar("u1", anon, true, "Daniela", "", &propia);
    CHECK(p && p->nombre == "Daniela" && propia, "le corrigieron el nombre en el lobby: la suya se corrige");
    l2.guardarFicha(*p);
    CHECK(!q2.nombrar("u1", anon, true, "Daniela", ""), "el mismo nombre: nada que hacer");
}

} // namespace

int main() {
    quienEs();
    anotado();
    vistas();
    etiquetas();
    puerta();
    lobby();
    altas();
    if (g_fallos == 0) std::printf("test_rostros_quien_es: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
