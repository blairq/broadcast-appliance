// ============================================================================
// src/app/tests/test_padron.cpp — el padron del evento
//
//   1. Alta y busqueda: por id, por ficha de rostro y por nombre tipeado
//      (con acentos, mayusculas y espacios de mas, que es como se tipea).
//   2. Dejar pasar solo necesita ficha de rostro: por nombre no vale.
//   3. Acreditar: marca que llego y guarda la hora de la PRIMERA vez.
//   4. El resumen contesta "quienes estan" y el quorum.
//   5. Ida y vuelta por JSON, y que una palabra desconocida no tire la fila.
//   6. Disco: sin archivo es un padron vacio, no un error.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "evento/Padron.hpp"

namespace fs = std::filesystem;
using namespace ba::evento;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

Asistente alguien(const std::string& nombre, Papel papel, Canal canal) {
    Asistente a;
    a.nombre = nombre;
    a.papel = papel;
    a.canal = canal;
    return a;
}

/// Una asamblea chica: dos que votan, un orador y la prensa.
Padron asamblea() {
    Padron p;
    Asistente ana = alguien("José Pérez", Papel::Publico, Canal::Presencial);
    ana.personaId = "p-jose";
    ana.vota = true;
    p.agregar(ana);

    Asistente luis = alguien("Luis Gómez", Papel::Publico, Canal::Online);
    luis.vota = true;
    p.agregar(luis);

    Asistente ora = alguien("La intendenta", Papel::Orador, Canal::Presencial);
    ora.personaId = "p-intendenta";
    ora.dejarPasarSolo = true;
    p.agregar(ora);

    p.agregar(alguien("Canal 7", Papel::Prensa, Canal::Presencial));
    return p;
}

// --- 1) Alta y busqueda -----------------------------------------------------
void alta_y_busqueda() {
    Padron p;
    const std::string id = p.agregar(alguien("José Pérez", Papel::Publico, Canal::Presencial));
    CHECK(!id.empty(), "dar de alta devuelve un id");
    CHECK(p.gente().size() == 1, "y queda uno en la lista");
    CHECK(p.porId(id) != nullptr, "se lo encuentra por su id");
    CHECK(p.porId("no-existe") == nullptr, "un id que no esta no inventa a nadie");

    // El nombre se tipea, y se tipea mal: sin acentos, en minuscula, con
    // espacios de mas. Los tres son el mismo señor.
    CHECK(p.porNombre("José Pérez") != nullptr, "tal cual como esta");
    CHECK(p.porNombre("jose perez") != nullptr, "sin acentos y en minuscula");
    CHECK(p.porNombre("  JOSE   PEREZ  ") != nullptr, "con espacios de mas");
    CHECK(p.porNombre("jose") == nullptr, "pero no a medias: eso seria otro");
    CHECK(p.porNombre("") == nullptr, "un vacio no encuentra a nadie");

    Asistente conFicha = alguien("La intendenta", Papel::Orador, Canal::Presencial);
    conFicha.personaId = "p-intendenta";
    p.agregar(conFicha);
    CHECK(p.porPersona("p-intendenta") != nullptr, "se la encuentra por su ficha de rostro");
    CHECK(p.porPersona("") == nullptr, "sin ficha no se busca por ficha");
    CHECK(p.porPersona("p-nadie") == nullptr, "una ficha que no esta tampoco");

    Padron q = asamblea();
    CHECK(q.quitar(q.gente()[0].id), "se puede quitar a alguien");
    CHECK(q.gente().size() == 3, "y queda uno menos");
    CHECK(!q.quitar("no-existe"), "quitar a quien no esta avisa");
}

// --- 2) Dejar pasar solo ----------------------------------------------------
void dejar_pasar_solo() {
    Padron p;
    // Sin ficha de rostro, dejar pasar solo no significa nada: entraria por
    // haber ESCRITO un nombre, que es lo que no se quiere.
    Asistente sinFicha = alguien("Cualquiera", Papel::Publico, Canal::Online);
    sinFicha.dejarPasarSolo = true;
    const std::string id = p.agregar(sinFicha);
    CHECK(!p.porId(id)->dejarPasarSolo, "sin ficha de rostro no entra solo");

    // Y si despues se le engancha la ficha, se lo puede volver a prender.
    Asistente cambio;
    cambio.personaId = "p-alguien";
    CHECK(p.cambiar(id, cambio, {"personaId"}), "se le engancha la ficha");
    Asistente prender;
    prender.dejarPasarSolo = true;
    CHECK(p.cambiar(id, prender, {"dejarPasarSolo"}), "y ahora si");
    CHECK(p.porId(id)->dejarPasarSolo, "con ficha, entra solo");

    // Al revés: sacarle la ficha lo apaga sin que nadie se acuerde de hacerlo.
    Asistente sacar;
    CHECK(p.cambiar(id, sacar, {"personaId"}), "se le saca la ficha");
    CHECK(!p.porId(id)->dejarPasarSolo, "y deja de entrar solo, sin avisar dos veces");

    CHECK(!p.cambiar("no-existe", sacar, {"nombre"}), "cambiar a quien no esta avisa");
}

// --- 3) Acreditar -----------------------------------------------------------
void acreditar() {
    Padron p = asamblea();
    const std::string id = p.gente()[0].id;
    CHECK(p.porId(id)->estado == Estado::Invitado, "arranca como invitado");
    CHECK(p.porId(id)->llegoMs == 0, "y sin hora de llegada");

    CHECK(p.acreditar(id, "rostro", 1000), "se lo acredita");
    CHECK(p.porId(id)->estado == Estado::Llego, "queda como llegado");
    CHECK(p.porId(id)->llegoMs == 1000, "con la hora");
    CHECK(p.porId(id)->porDonde == "rostro", "y por donde entro");

    // En la puerta se vuelve a escanear todo el tiempo. La hora que importa es
    // la de la primera vez: pisarla convertiria "llego 18:40" en "llego 20:15".
    CHECK(p.acreditar(id, "mostrador", 9999), "acreditar de nuevo no es un error");
    CHECK(p.porId(id)->llegoMs == 1000, "pero NO pisa la hora de la primera vez");
    CHECK(p.porId(id)->porDonde == "rostro", "ni por donde entro la primera vez");

    CHECK(p.despedir(id), "se lo puede despedir");
    CHECK(p.porId(id)->estado == Estado::SeFue, "queda como que se fue");
    CHECK(p.porId(id)->llegoMs == 1000, "y la hora de llegada sigue ahi");

    CHECK(!p.acreditar("no-existe", "qr", 1), "acreditar a quien no esta avisa");
    CHECK(!p.despedir("no-existe"), "despedir a quien no esta tambien");
}

// --- 4) Quienes estan -------------------------------------------------------
void quienes_estan() {
    Padron p = asamblea();
    Resumen r = p.resumen();
    CHECK(r.total == 4, "cuatro en la lista");
    CHECK(r.presentes == 0, "todavia no llego nadie");
    CHECK(r.faltan == 4, "faltan los cuatro");
    CHECK(r.conVoto == 2, "dos tienen voto");
    CHECK(r.votoPresente == 0, "sin quorum");

    // Llega uno presencial que vota y uno de prensa que no.
    p.acreditar(p.gente()[0].id, "rostro", 1000);
    p.acreditar(p.gente()[3].id, "mostrador", 1100);
    r = p.resumen();
    CHECK(r.presentes == 2, "hay dos");
    CHECK(r.presenciales == 2, "los dos por la puerta");
    CHECK(r.enLinea == 0, "ninguno por la sala");
    CHECK(r.faltan == 2, "faltan dos");
    CHECK(r.votoPresente == 1, "y solo uno de los presentes vota");

    // El de online entra por la sala: el padron es UNO para los dos mundos.
    p.acreditar(p.gente()[1].id, "lobby", 1200);
    r = p.resumen();
    CHECK(r.presentes == 3, "tres presentes contando los dos mundos");
    CHECK(r.presenciales == 2 && r.enLinea == 1, "y se sabe por donde vino cada uno");
    CHECK(r.votoPresente == 2, "ahora hay quorum de los que votan");

    // El que se fue deja de estar.
    p.despedir(p.gente()[0].id);
    r = p.resumen();
    CHECK(r.presentes == 2, "el que se fue ya no esta");
    CHECK(r.faltan == 1, "y no vuelve a contar como que falta");
}

// --- 5) Ida y vuelta --------------------------------------------------------
void ida_y_vuelta() {
    const Padron a = asamblea();
    const Padron b = Padron::deJson(a.aJson());
    CHECK(b.gente().size() == 4, "vuelven los cuatro");
    CHECK(b.gente()[0].nombre == "José Pérez", "con el nombre y sus acentos");
    CHECK(b.gente()[0].vota, "y con el voto");
    CHECK(b.gente()[2].papel == Papel::Orador, "el papel sobrevive");
    CHECK(b.gente()[1].canal == Canal::Online, "el canal tambien");
    CHECK(b.gente()[2].dejarPasarSolo, "y el permiso de entrar solo");

    // Un padron escrito por una version mas nueva: la palabra que no se
    // entiende deja el valor de fabrica, pero la persona NO se pierde. Perder a
    // alguien del padron es peor que mostrarlo mal.
    nlohmann::json j = a.aJson();
    j["gente"][0]["papel"] = "moderador";
    j["gente"][0]["canal"] = "holograma";
    const Padron c = Padron::deJson(j);
    CHECK(c.gente().size() == 4, "sigue estando");
    CHECK(c.gente()[0].papel == Papel::Publico, "con el papel de fabrica");
    CHECK(c.gente()[0].canal == Canal::SinDefinir, "y el canal sin definir");

    // Una fila sin id se puede quedar sin poder referenciarse: se le da uno.
    nlohmann::json k = a.aJson();
    k["gente"][1]["id"] = "";
    const Padron d = Padron::deJson(k);
    CHECK(!d.gente()[1].id.empty(), "una fila sin id igual recibe uno");

    // Basura: no explota.
    CHECK(Padron::deJson(nlohmann::json::array()).gente().empty(), "una lista no es un padron");
    CHECK(Padron::deJson(nlohmann::json("hola")).gente().empty(), "un texto tampoco");
}

// --- 6) Disco ---------------------------------------------------------------
void disco() {
    const fs::path tmp = fs::temp_directory_path() / "ba_test_padron";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    // Sin archivo: padron vacio y SIN error. Es lo normal hasta que alguien
    // anota al primero.
    bool ok = false;
    const Padron vacio = cargarPadron(tmp, &ok);
    CHECK(ok, "una carpeta sin padron no es un error");
    CHECK(vacio.gente().empty(), "es un padron vacio");

    CHECK(guardarPadron(tmp, asamblea()), "se guarda");
    CHECK(fs::exists(tmp / "padron.json"), "y el archivo queda");
    CHECK(!fs::exists(tmp / "padron.json.tmp"), "sin dejar el temporal tirado");

    const Padron leido = cargarPadron(tmp, &ok);
    CHECK(ok, "se lee");
    CHECK(leido.gente().size() == 4, "con los cuatro");
    CHECK(leido.gente()[0].nombre == "José Pérez", "y los acentos sobreviven al disco");

    // Un archivo roto se dice, no se hace pasar por un padron vacio: si no,
    // el dia del evento la lista aparece en blanco y nadie sabe por que.
    { std::ofstream(tmp / "padron.json") << "{esto no es json"; }
    std::string err;
    cargarPadron(tmp, &ok, &err);
    CHECK(!ok, "un padron roto avisa");
    CHECK(!err.empty(), "y dice que le pasa");

    fs::remove_all(tmp, ec);
}

} // namespace

int main() {
    alta_y_busqueda();
    dejar_pasar_solo();
    acreditar();
    quienes_estan();
    ida_y_vuelta();
    disco();
    if (g_fallos == 0) std::printf("test_padron: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
