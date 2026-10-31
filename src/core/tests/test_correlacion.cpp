// test_correlacion.cpp
//
// Una orden MCP seguida de punta a punta (F4-02), con las piezas de verdad salvo el
// transporte: el núcleo MCP abre la orden (Ambito) y registra; ConferenceClient la manda
// en la cabecera X-BA-Orden; la API de la sala la toma de las cabeceras y registra al
// atenderla; el launcher manda su cierre por el canal de control (ipc::Message) y el
// hijo lo registra. Al final, un «grep» del id en lo registrado la encuentra en los tres.
// Y: lo que viene de afuera se valida (sin saltos ni basura en el registro), el ámbito
// se anida y se restaura, y los ids no se repiten.

#include "diag/Correlacion.hpp"
#include "diag/Registro.hpp"
#include "ipc/ControlProtocol.hpp"

#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg);         \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

namespace corr = ba::diag::correlacion;

/// Lo que haría el lado B con las cabeceras de un pedido HTTP (AdminApi: getHeader).
static std::string cabecera(const std::string& headers, const std::string& nombre) {
    const auto k = headers.find(nombre + ": ");
    if (k == std::string::npos) return {};
    const auto fin = headers.find("\r\n", k);
    return headers.substr(k + nombre.size() + 2, fin - k - nombre.size() - 2);
}

int main() {
    auto& reg = ba::diag::registro();
    std::mutex mu;
    std::vector<std::string> lineas;
    const int oyente = reg.agregarOyente([&](const ba::diag::Registrado& r) {
        std::lock_guard<std::mutex> l(mu);
        lineas.push_back(r.componente + " | " + r.texto);
    });

    // --- A: la herramienta MCP abre su orden; lo que registre la lleva.
    std::string id, headersHaciaB;
    {
        const corr::Ambito orden(corr::nueva('m'));
        id = corr::actual();
        CHECK(id.size() == 10 && id[0] == 'm' && id[1] == '-', "id m-XXXXXXXX");
        reg.linea(ba::diag::Nivel::Info, "mcp", 0, "room_admit [asistente] (3 ms)");
        // ConferenceClient: la cabecera sale con la orden de este hilo.
        headersHaciaB = "POST /api/v1/participants/p1/admit HTTP/1.1\r\nAuthorization: Bearer x\r\n" +
                        std::string(corr::kCabecera) + ": " + corr::actual() + "\r\n";
    }
    CHECK(corr::actual().empty(), "al cerrar el ámbito, sin orden");

    // --- B (otro hilo en la vida real): la API de la sala la toma de la cabecera.
    {
        const corr::Ambito orden(cabecera(headersHaciaB, corr::kCabecera));
        reg.linea(ba::diag::Nivel::Info, "gateway", 0, "POST /api/v1/participants/p1/admit");
    }

    // --- El launcher: su cierre viaja en el mensaje del canal de control.
    std::string idCierre;
    {
        const corr::Ambito orden(corr::nueva('l'));
        idCierre = orden.id();
        ba::ipc::Message m;
        m.type = ba::ipc::Type::Shutdown;
        m.payload = "{}";
        m.correlacion = orden.id();
        const std::string cable = m.toJson();
        reg.linea(ba::diag::Nivel::Info, "control", 0, "shutdown enviado por 'control.gateway'");
        ba::ipc::Message r;
        CHECK(ba::ipc::Message::fromJson(cable, r), "el mensaje vuelve");
        CHECK(r.correlacion == idCierre, "la orden viaja en el mensaje");
        // El hijo la abre al atenderlo (en su hilo, sin la del launcher).
        const corr::Ambito delHijo(r.correlacion);
        reg.linea(ba::diag::Nivel::Info, "gateway", 0, "control: shutdown pedido por el launcher");
    }
    // Un mensaje sin orden (un par viejo) sigue andando; uno con basura la descarta.
    {
        ba::ipc::Message r;
        CHECK(ba::ipc::Message::fromJson(R"({"type":"shutdown","payload":{}})", r) && r.correlacion.empty(), "sin orden: vacía");
        CHECK(ba::ipc::Message::fromJson(R"({"type":"shutdown","correlacion":"a b\n[x]"})", r) && r.correlacion.empty(),
              "con basura: se descarta");
    }

    CHECK(reg.vaciar(2000), "el escritor pasó todo");
    reg.quitarOyente(oyente);

    // El «grep»: la orden MCP aparece en A y en B; la del launcher, en el launcher y en B.
    int enA = 0, enB = 0, cierre = 0;
    for (const auto& l : lineas) {
        if (l.find("[orden=" + id + "]") != std::string::npos) {
            if (l.rfind("mcp", 0) == 0) ++enA;
            if (l.rfind("gateway", 0) == 0) ++enB;
        }
        if (l.find("[orden=" + idCierre + "]") != std::string::npos) ++cierre;
    }
    CHECK(enA == 1, "la orden en el registro de A");
    CHECK(enB == 1, "la misma orden en el registro de B");
    CHECK(cierre == 2, "el cierre en el launcher y en B");

    // Lo que viene de afuera se valida; un ámbito inválido deja el anterior.
    CHECK(!corr::valido("") && !corr::valido("a b") && !corr::valido(std::string(60, 'a')) && !corr::valido("x\ny"),
          "ids inválidos");
    CHECK(corr::valido("m-0a1b2c3d") && corr::valido("ext_42.b"), "ids válidos");
    {
        const corr::Ambito a("m-11111111");
        {
            const corr::Ambito b("w-22222222");
            CHECK(corr::actual() == "w-22222222", "anidado: el de adentro");
            const corr::Ambito c("roto con espacios");
            CHECK(corr::actual() == "w-22222222", "uno inválido no pisa");
        }
        CHECK(corr::actual() == "m-11111111", "al salir, vuelve el de afuera");
        CHECK(corr::sufijo("ya dice m-11111111").empty(), "no se repite si el texto ya la nombra");
        CHECK(corr::sufijo("algo") == " [orden=m-11111111]", "el sufijo");
    }
    std::set<std::string> vistos;
    for (int i = 0; i < 10000; ++i) vistos.insert(corr::nueva('m'));
    CHECK(vistos.size() == 10000, "10000 ids sin repetir");

    if (g_failures == 0) std::printf("test_correlacion: OK\n");
    return g_failures == 0 ? 0 : 1;
}
