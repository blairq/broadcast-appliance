// test_mcp_salidas.cpp — las salidas (`salida` en las herramientas de salida) y el límite de frecuencia por sesión
//
// Era una parte de test_mcp.cpp (ver McpPrueba.hpp): los mismos chequeos, en su programa.
#include <chrono>
#include <set>
#include <thread>

#include "McpPrueba.hpp"

using namespace ba::app::mcp;
using namespace ba::app::mcp::prueba;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_mcp_salidas ==\n");

    // --- Las salidas (PLAN-SALIDAS, etapa 3): `salida` en las herramientas de salida
    {
        McpCore sal;
        std::string vista = "?"; // la salida que vio la última corrida
        int corridas = 0;
        Tool t;
        t.name = "scene_switch"; // de aire (declarada)
        t.clase = Clase::Aire;
        t.porQueAire = "cambia la escena al aire";
        t.porSalida = true;
        t.inputSchema = {{"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}};
        McpCore* sp = &sal;
        t.run = [sp, &vista, &corridas](const json& a, Done done) {
            ++corridas;
            vista = sp->salidaDe(a);
            done(ToolResult::ok("ok"));
        };
        sal.addTool(std::move(t));
        json lista = json::array({{{"id", "principal"}, {"nombre", "Principal"}, {"por_omision", true}},
                                  {{"id", "yt2"}, {"nombre", "YouTube 2"}, {"por_omision", false}}});
        std::set<std::string> salen;
        sal.setSalidas([&lista]() { return lista; }, [&salen](const std::string& id) { return salen.count(id) > 0; });
        sal.setHayAire([&salen]() { return !salen.empty(); });
        sal.setApprovalPolicy(ApprovalPolicy::Never);
        int preguntas = 0;
        sal.setApprover([&preguntas](const Tool&, const json&, const std::string&, std::function<void(bool)> d) {
            ++preguntas;
            d(false);
        });
        const auto llamar = [&](const json& args, const CallContext& c = CallContext{"S", "", "asistente"}) {
            json res;
            sal.callTool({{"name", "scene_switch"}, {"arguments", args}}, [&res](json x) { res = x; }, c);
            return res;
        };
        // El esquema la trae sola.
        const json lst = call(sal, req(1, "tools/list"));
        bool tiene = false;
        for (const auto& x : lst["result"]["tools"])
            if (x["name"] == "scene_switch") tiene = x["inputSchema"]["properties"].contains("salida");
        CHECK(tiene, "una herramienta de salida tiene `salida` en el esquema");
        // Sin salida: la por omisión ("").
        json r = llamar({{"name", "A"}});
        CHECK(!r.value("isError", true) && vista.empty(), "sin `salida`: la por omisión");
        r = llamar({{"name", "A"}, {"salida", "yt2"}});
        CHECK(!r.value("isError", true) && vista == "yt2", "con `salida` por id");
        r = llamar({{"name", "A"}, {"salida", "youtube 2"}});
        CHECK(!r.value("isError", true) && vista == "yt2", "con `salida` por nombre (sin mayúsculas)");
        r = llamar({{"name", "A"}, {"salida", "principal"}});
        CHECK(!r.value("isError", true) && vista.empty(), "la por omisión por su id es \"\"");
        const int antes = corridas;
        r = llamar({{"name", "A"}, {"salida", "no-existe"}});
        CHECK(r.value("isError", false) && corridas == antes && textOf(r).find("no-existe") != std::string::npos &&
                  textOf(r).find("yt2") != std::string::npos,
              "salida inexistente: error claro (con las que hay) y no corre");
        // La del contexto (perfil, aparato, asistente) si no viene en los argumentos.
        CallContext c{"S", "", "asistente"};
        c.salida = "yt2";
        r = llamar({{"name", "A"}}, c);
        CHECK(!r.value("isError", true) && vista == "yt2", "sin `salida`: la del contexto");
        r = llamar({{"name", "A"}, {"salida", "principal"}}, c);
        CHECK(!r.value("isError", true) && vista.empty(), "`salida` de los argumentos manda sobre el contexto");
        c.salida = "otra";
        r = llamar({{"name", "A"}}, c);
        CHECK(r.value("isError", false), "la del contexto también se valida");
        // De aire según qué salida sale.
        salen = {"yt2"};
        preguntas = 0;
        r = llamar({{"name", "A"}});
        CHECK(preguntas == 0 && !r.value("isError", true), "sale otra salida: cambiar la por omisión no es de aire");
        r = llamar({{"name", "A"}, {"salida", "yt2"}});
        CHECK(preguntas == 1 && r.value("isError", false), "sale esa salida: es de aire (pregunta; rechazada no corre)");
        // Una de aire que no es de salida cuenta si sale cualquiera.
        Tool u;
        u.name = "stream_stop_todo";
        u.run = [](const json&, Done done) { done(ToolResult::ok("ok")); };
        sal.addTool(std::move(u));
        sal.aire().ajustar("stream_stop_todo", "aire"); // sin archivo: queda en memoria
        json y;
        sal.callTool({{"name", "stream_stop_todo"}, {"arguments", json::object()}}, [&y](json x) { y = x; },
                     CallContext{"S", "", "asistente"});
        CHECK(preguntas == 2, "las que no son de salida: de aire si sale cualquiera");
        // Lo mismo sin llamar: la botonera lo pregunta antes (para pedir confirmar en el aparato).
        CHECK(!sal.cambiaElAire("scene_switch", {{"name", "A"}}, ""), "cambiaElAire: la por omisión no sale");
        CHECK(sal.cambiaElAire("scene_switch", {{"name", "A"}}, "yt2"), "cambiaElAire: sale la del aparato");
        CHECK(sal.cambiaElAire("scene_switch", {{"name", "A"}, {"salida", "yt2"}}, ""), "cambiaElAire: la de los argumentos manda");
        CHECK(sal.cambiaElAire("stream_stop_todo", json::object(), ""), "cambiaElAire: la que no es de salida, si sale cualquiera");
        CHECK(sal.cambiaElAire("scene_switch", {{"name", "A"}}, "no-existe"), "cambiaElAire: una salida que no existe confirma de más");
        salen.clear();
        CHECK(!sal.cambiaElAire("scene_switch", {{"name", "A"}}, "yt2") && !sal.cambiaElAire("stream_stop_todo", json::object(), ""),
              "cambiaElAire: sin aire, no");
        // Sin setSalidas (un núcleo sin la app), lo pedido pasa tal cual.
        McpCore solo;
        std::string err;
        CHECK(solo.resolverSalida("x", err) == "x" && err.empty(), "sin lista de salidas, no valida");
        CHECK(McpCore::salidaPedida(json{{"salida", ""}}, CallContext{}) .empty(), "`salida` vacía = la por omisión");
    }

    // --- N4) límite de frecuencia por sesión --------------------------------------------
    // Una ráfaga de cambios de una sesión se frena con un error que dice cuánto esperar;
    // otra sesión, lo que lee y lo que apretó una persona no se cuentan; el aire, aparte.
    {
        McpCore lim;
        int cambios = 0, aires = 0, lecturas = 0;
        const auto agregar = [&lim](const char* nombre, Clase c, int& cuenta) {
            Tool t;
            t.name = nombre;
            t.clase = c;
            if (c == Clase::Aire) t.porQueAire = "prueba";
            t.run = [&cuenta](const json&, Done done) {
                ++cuenta;
                done(ToolResult::ok("ok"));
            };
            lim.addTool(std::move(t));
        };
        agregar("cambia", Clase::Escritura, cambios);
        agregar("corta", Clase::Aire, aires);
        agregar("mira", Clase::Lectura, lecturas);
        lim.setLimiteFrecuencia({1.0, 3, 0.5, 1});
        const auto pedir = [&lim](const char* n, const CallContext& c) {
            json r;
            lim.callTool({{"name", n}, {"arguments", json::object()}}, [&r](json x) { r = std::move(x); }, c);
            return r;
        };
        const CallContext a{"A", "", "asistente"}, b{"B", "", "asistente"};
        json r;
        for (int i = 0; i < 5; ++i) r = pedir("cambia", a);
        CHECK(cambios == 3 && r.value("isError", false) &&
                  r.value("structuredContent", json::object()).value("limite_frecuencia", false) &&
                  r["structuredContent"].value("reintentar_ms", 0) > 0,
              "N4: la ráfaga de una sesión se frena después de 3, con cuánto esperar");
        CHECK(lim.limitadas() == 2, "N4: cuenta las frenadas");
        r = pedir("cambia", b);
        CHECK(cambios == 4 && !r.value("isError", true), "N4: otra sesión tiene su propio balde");
        for (int i = 0; i < 5; ++i) pedir("mira", a);
        CHECK(lecturas == 5, "N4: lo que lee no cuenta");
        for (int i = 0; i < 5; ++i) pedir("cambia", CallContext{"", "", "receta"});
        CHECK(cambios == 9, "N4: lo de adentro sin sesión (una receta) no se frena a mitad");
        CallContext persona = a;
        persona.humano = true;
        r = pedir("cambia", persona);
        CHECK(cambios == 10 && !r.value("isError", true), "N4: lo que apretó una persona no se frena");
        pedir("corta", a);
        r = pedir("corta", a);
        CHECK(aires == 1 && r["structuredContent"].value("clase", std::string()) == "aire", "N4: el aire tiene su balde (1 seguida)");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        r = pedir("cambia", a);
        CHECK(cambios == 11 && !r.value("isError", true), "N4: al segundo vuelve una ficha");
        lim.setLimiteFrecuencia({});
        for (int i = 0; i < 20; ++i) pedir("cambia", a);
        CHECK(cambios == 31, "N4: en 0, sin límite (lo de fábrica del núcleo)");
    }

    if (g_failures == 0) {
        std::printf("test_mcp_salidas: OK\n");
        return 0;
    }
    std::printf("test_mcp_salidas: %d fallo(s)\n", g_failures);
    return 1;
}
