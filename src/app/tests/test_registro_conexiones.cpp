// ============================================================================
// src/app/tests/test_registro_conexiones.cpp — las conexiones en vivo de los tótems
//
// registro/ConexionesTotem con unos Puertos de mentira (anotan lo que se les pide) y la hora
// a mano (sin Qt, sin audio, sin red):
//   1. El tic: una conexión sin canal no lo pide; con canal sí (y abre el audio); al soltar
//      el canal o irse, lo suelta (y el dorso con él).
//   2. La cara: una sola foto no arranca; dos seguidas de la misma, sí (saluda, pide el
//      primer campo y habla); a lo sumo una foto cada kEntreFotosMs.
//   3. Hablar y escuchar: mientras la frase se sintetiza no vuelve a escuchar; cuando sale
//      entera y pasa el margen, «escuchando».
//   4. El turno: lo oído se junta y se cierra al fin del turno (el guion contesta).
//   5. La atención remota: atender calla al asistente y avisa al gateway; dos páginas no
//      atienden la misma; devolverla al asistente la libera.
//   6. Interrumpir: con el oído abierto mientras habla, se corta la voz (y la síntesis si
//      no habla en otra conexión).
//   7. Las filas: lo que ve la vista y lo que sale por MCP (sin números largos).
// ============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "registro/ConexionesTotem.hpp"
#include "registro/Totems.hpp"

using namespace ba::registro;
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

/// Anota lo que el modelo pide; lo que pregunta, lo contesta con lo que diga la prueba.
struct PuertosDePrueba : ConexionesTotem::Puertos {
    std::vector<std::pair<std::string, json>> enviados; ///< {conn, msg}
    std::vector<std::pair<std::string, json>> gateway;  ///< {ruta, cuerpo}
    std::vector<std::string> dichos, abiertos, cerrados, callados, escuchados, caras;
    std::vector<bool> callarSintesis;
    bool tic = false;
    int pedidosTic = 0, soltadosDorso = 0, vivos = 0, guardados = 0;
    bool voz = true, pendiente = false, oye = false, cuadroNuevo = true;

    void vivoCambio() override { ++vivos; }
    void conectadosCambio() override {}
    void registradosCambio() override {}
    void pedirTic(bool si) override {
        tic = si;
        ++pedidosTic;
    }
    void enviar(const std::string& conn, const json& msg) override { enviados.emplace_back(conn, msg); }
    void alGateway(const std::string& ruta, const json& cuerpo) override { gateway.emplace_back(ruta, cuerpo); }
    void abrirAudio(const std::string& conn, int) override { abiertos.push_back(conn); }
    void cerrarAudio(const std::string& conn, int) override { cerrados.push_back(conn); }
    void revisarOido(const std::string&, int) override {}
    bool sintetizar(const std::string&, int, const std::string& texto) override {
        if (voz) dichos.push_back(texto);
        return voz;
    }
    void callar(const std::string& conn, int, bool sintesis) override {
        callados.push_back(conn);
        callarSintesis.push_back(sintesis);
    }
    void escuchar(const std::string& conn) override { escuchados.push_back(conn); }
    bool oyeMientrasHabla(const std::string&) const override { return oye; }
    bool oyendoVoz(const std::string&) const override { return false; }
    bool vozPendiente(int) const override { return pendiente; }
    bool pedirCara(const std::string& conn, int) override {
        if (cuadroNuevo) caras.push_back(conn);
        return cuadroNuevo;
    }
    bool pedirDocumento(const std::string&, int, const std::vector<std::string>&) override { return false; }
    void traducirCodigo(const std::string&, const std::string&, const std::string&, const std::vector<std::string>&) override {}
    void pedirCuadroDePagina(const std::string&) override {}
    void prepararDorso() override {}
    void soltarDorso() override { ++soltadosDorso; }
    bool dorsoDisponible() const override { return false; }
    bool yaRegistrado(const std::string&) const override { return false; }
    void guardarRegistro(const std::string&, const std::string&, const std::string&, const std::string&, const json&,
                         const std::string&) override {
        ++guardados;
    }
    std::unique_ptr<Entendedor> crearEntendedor(const std::string&, const std::string&, Conversacion&, bool) override {
        return nullptr; // el guion solo
    }
    void prepararModelo() override {}
    void soltarModelo() override {}

    /// El último mensaje de `tipo` a `conn` (null si no hubo).
    json ultimo(const std::string& conn, const std::string& tipo) const {
        for (auto it = enviados.rbegin(); it != enviados.rend(); ++it)
            if (it->first == conn && it->second.value("type", "") == tipo) return it->second;
        return nullptr;
    }
};

Totems totemsDePrueba() {
    Totems ts;
    Totem t = totemNuevo("entrada", "Entrada");
    t.conModelo = false;
    t.foto = false;
    ts.agregar(t);
    return ts;
}

json conexion(const std::string& conn, bool conectado = true) {
    return {{"tipo", "conexion"}, {"conn", conn}, {"id", "entrada"}, {"conectado", conectado}, {"ip", "10.0.0.5"}};
}
json canal(const std::string& conn, int k) { return {{"tipo", "canal"}, {"conn", conn}, {"id", "entrada"}, {"canal", k}}; }

ConexionesTotem::Cara cara(const std::string& id) {
    ConexionesTotem::Cara c;
    c.personaId = id;
    c.nombre = "Juana";
    c.hayCara = true;
    c.sirve = true;
    c.foto = "jpeg";
    return c;
}

void elTic() {
    PuertosDePrueba p;
    const Totems ts = totemsDePrueba();
    ConexionesTotem v(p);
    v.ponerEvento(&ts, "Expo", "es");
    v.alEvento(conexion("a"), 1000);
    CHECK(!p.tic && !v.activo(), "una TV conectada sin canal no pide el reloj");
    CHECK(!p.ultimo("a", "pantalla").is_null(), "le manda la pantalla del tótem");
    CHECK(v.conectados("entrada") == 1, "cuenta la conexión del tótem");
    v.alEvento(canal("a", 1), 1100);
    CHECK(p.tic && v.activo(), "con canal pide el reloj");
    CHECK(p.abiertos.size() == 1 && p.abiertos[0] == "a", "y abre el audio");
    v.alEvento(canal("a", 0), 1200);
    CHECK(!p.tic, "sin canal y sin conversación lo suelta");
    CHECK(p.soltadosDorso >= 1, "y suelta el dorso");
    v.alEvento(canal("a", 2), 1300);
    CHECK(p.tic, "otro canal: de nuevo");
    v.alEvento(conexion("a", false), 1400);
    CHECK(!p.tic && !v.hayConexiones(), "se fue: no queda nadie y para el reloj");
    CHECK(v.conectados("entrada") == 0, "sin conexiones del tótem");
}

/// Una conexión con canal y una persona que se quedó parada: empieza y habla.
void arrancar(PuertosDePrueba& p, ConexionesTotem& v, std::int64_t& t) {
    v.alEvento(conexion("a"), t);
    v.alEvento(canal("a", 1), t);
    t += ConexionesTotem::kEntreFotosMs;
    v.tic(t);
    CHECK(p.caras.size() == 1, "con canal mira la cara");
    v.tic(t + 10);
    CHECK(p.caras.size() == 1, "una foto por vez: hasta que vuelva la anterior, no pide otra");
    v.alCara("a", cara("p1"), t);
    CHECK(p.ultimo("a", "dice").is_null(), "una sola foto no arranca: puede ser alguien que pasa");
    t += ConexionesTotem::kEntreFotosMs;
    v.tic(t);
    CHECK(p.caras.size() == 2, "la segunda foto, pasado el intervalo");
    v.alCara("a", cara("p1"), t);
    CHECK(!p.ultimo("a", "dice").is_null(), "dos fotos seguidas de la misma cara: empieza y saluda");
    CHECK(p.ultimo("a", "estado").value("estado", "") == (p.voz ? "hablando" : "escuchando"),
          "la página: hablando (o, sin voz, escuchando)");
    CHECK(!p.ultimo("a", "pedir").is_null(), "y le muestra qué pide");
}

void hablarYEscuchar() {
    PuertosDePrueba p;
    const Totems ts = totemsDePrueba();
    ConexionesTotem v(p);
    v.ponerEvento(&ts, "Expo", "es");
    std::int64_t t = 10000;
    arrancar(p, v, t);
    p.pendiente = true;
    v.alTerminarFrase("a");
    t += 2000;
    v.tic(t);
    CHECK(p.escuchados.empty(), "con voz pendiente en el canal, no vuelve a escuchar");
    p.pendiente = false;
    v.tic(t);
    t += RitmoTotem::kMargenVozMs;
    v.tic(t);
    CHECK(p.escuchados.size() == 1, "salió entera y pasó el margen: vuelve a escuchar");
    CHECK(p.ultimo("a", "estado").value("estado", "") == "escuchando", "la página: escuchando");

    // El turno: lo oído se junta y se cierra al fin del turno.
    const std::size_t antes = p.dichos.size();
    v.alOido("a", "juana", t);
    v.alOido("a", "prueba", t + 100);
    CHECK(p.ultimo("a", "oido").value("texto", "") == "juana prueba", "lo oído del turno, junto");
    v.tic(t + 100 + RitmoTotem::kFinTurnoMs - 1);
    CHECK(p.dichos.size() == antes, "antes del fin del turno no contesta");
    v.tic(t + 100 + RitmoTotem::kFinTurnoMs);
    CHECK(p.dichos.size() > antes, "al fin del turno contesta el guion");
    const json filas = v.filas();
    CHECK(filas.size() == 1 && filas[0].value("quien", "") != "", "la fila dice quién es");
    CHECK(filas[0].value("modo", "") == "guion", "sin modelo: el guion");
}

void atencion() {
    PuertosDePrueba p;
    const Totems ts = totemsDePrueba();
    ConexionesTotem v(p);
    v.ponerEvento(&ts, "Expo", "es");
    v.alEvento(conexion("a"), 1000);
    CHECK(v.atender("x", "Laura", "", 1000) != "", "una conexión que no existe: el porqué");
    v.alEvento({{"tipo", "atiende-conexion"}, {"aconn", "w1"}, {"conectado", true}, {"nombre", "Laura"}}, 1000);
    v.alEvento({{"tipo", "atiende-conexion"}, {"aconn", "w2"}, {"conectado", true}, {"nombre", "Pedro"}}, 1000);
    CHECK(v.hayAtienden() && v.atiendenJson().size() == 2, "dos páginas de atención");
    CHECK(v.atender("a", "Laura", "w1", 1100).empty(), "Laura la atiende (empieza callada)");
    CHECK(p.callados.size() == 1, "el asistente se calla");
    CHECK(!p.gateway.empty() && p.gateway.front().first == "enviar", "a la página de atención que entra, lo que hay");
    bool avisoAtendida = false;
    for (const auto& [ruta, c] : p.gateway)
        if (ruta == "atendida" && c.value("aconn", "") == "w1") avisoAtendida = true;
    CHECK(avisoAtendida, "el gateway sabe quién la atiende (su voz baja a ese tótem)");
    CHECK(p.ultimo("a", "atiende").value("activo", false), "la pantalla: te atiende una persona");
    CHECK(v.atender("a", "Pedro", "w2", 1200) != "", "otra página no la puede atender");
    CHECK(v.filas()[0].value("atendida", false), "la fila: atendida");
    CHECK(v.atiendenJson()[0].value("atiende", "") == "a", "Laura atiende «a»");
    CHECK(v.activo(), "conversando (atendida, sin canal): el reloj anda");
    CHECK(v.terminar("a", "asistente", "w2", 1300) != "", "Pedro no la puede devolver");
    CHECK(v.terminar("a", "asistente", "w1", 1300).empty(), "Laura la devuelve al asistente");
    CHECK(!v.filas()[0].value("atendida", true), "ya no está atendida");
    // Se fue la página de atención de quien atiende: vuelve sola al asistente.
    CHECK(v.atender("a", "Laura", "w1", 1400).empty(), "la vuelve a atender");
    v.alEvento({{"tipo", "atiende-conexion"}, {"aconn", "w1"}, {"conectado", false}}, 1500);
    CHECK(!v.filas()[0].value("atendida", true), "Laura cerró la página: vuelve al asistente");
    CHECK(v.atiendenJson().size() == 1, "queda Pedro");
}

void interrumpir() {
    PuertosDePrueba p;
    const Totems ts = totemsDePrueba();
    ConexionesTotem v(p);
    v.ponerEvento(&ts, "Expo", "es");
    std::int64_t t = 10000;
    arrancar(p, v, t);
    // Mientras habla, lo oído no cuenta (el eco de su propia voz)...
    v.alOido("a", "hola", t);
    CHECK(p.ultimo("a", "oido").is_null(), "hablando: lo oído no cuenta");
    // ...salvo con el oído abierto mientras habla: interrumpe.
    p.oye = true;
    v.alOido("a", "perdón, me llamo Ana", t + 50);
    CHECK(p.callados.size() == 1 && p.callarSintesis.back(), "corta la voz y la síntesis (no habla en otra)");
    CHECK(p.ultimo("a", "oido").value("texto", "") == "perdón, me llamo Ana", "y lo oye");
}

void paraAfuera() {
    PuertosDePrueba p;
    const Totems ts = totemsDePrueba();
    ConexionesTotem v(p);
    v.ponerEvento(&ts, "Expo", "es");
    std::int64_t t = 10000;
    p.voz = false; // sin voz: lo dicho queda escrito y sigue escuchando
    arrancar(p, v, t);
    v.alOido("a", "mi documento es 30123456", t);
    const json f = v.filasParaAfuera();
    CHECK(f.size() == 1 && f[0].value("oido", "").find("30123456") == std::string::npos, "por MCP, sin el número largo");
    CHECK(v.filas()[0].value("oido", "").find("30123456") != std::string::npos, "en la app, como se oyó");
    CHECK(v.conversando() == 1, "una conversación en curso");
}

} // namespace

int main() {
    elTic();
    hablarYEscuchar();
    atencion();
    interrumpir();
    paraAfuera();
    if (g_fallos == 0) std::printf("test_registro_conexiones: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
