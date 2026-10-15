// test_control_web.cpp — el control web (control/ControlWeb) con un McpCore de prueba
//
//   emparejar (código de un uso → solicitud → aprobar SOLO una persona → el token
//   vale, otro no); el perfil filtra las herramientas del auth, los llamar y los
//   mirar; una herramienta de aire con aire pide confirmar y no corre hasta
//   confirmado; mirar empuja solo lo que cambió y deja de consultar lo que nadie
//   mira; ampliar un perfil lo hace una persona (achicar no); las herramientas
//   control_web nunca las ve un aparato; quitar corta y el token deja de valer;
//   pedirAprobacion sin a quién preguntar dice que no (y en modo prueba, que sí).

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <string>
#include <vector>

#include "control/ControlWeb.hpp"
#include "mcp/McpCore.hpp"

using ba::app::control::ControlWeb;
using namespace ba::app::mcp;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                    \
    do {                                                               \
        if (!(c)) {                                                    \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);        \
            ++g_fallas;                                                \
        } else {                                                       \
            std::printf("ok: %s\n", m);                                \
        }                                                              \
    } while (0)

void esperar(int ms) {
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}

Tool herramienta(const std::string& nombre, const std::string& categoria, bool soloLectura,
                 std::function<void(const json&, Done)> run) {
    Tool t;
    t.name = nombre;
    t.title = nombre;
    t.category = categoria;
    t.readOnly = soloLectura;
    t.run = std::move(run);
    return t;
}

struct Enviado {
    std::vector<std::string> a;
    json msg;
};
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;

    // --- Un servidor MCP de prueba ---------------------------------------------------
    McpCore core;
    core.addCategory("escenas", "Escenas", "");
    core.addCategory("audio", "Audio", "");
    core.addCategory("camaras", "Cámaras", "");
    core.addCategory("control_web", "Control web", "");
    bool hayAire = false;
    core.setHayAire([&] { return hayAire; });
    std::string programa = "A";
    int consultasEscenas = 0, cortes = 0, audios = 0;
    core.addTool(herramienta("scene_list", "escenas", true, [&](const json&, Done d) {
        ++consultasEscenas;
        d(ToolResult().data({{"programa", programa}, {"escenas", {"A", "B"}}}));
    }));
    core.addTool(herramienta("scene_take", "escenas", false, [&](const json&, Done d) {
        ++cortes;
        d(ToolResult::ok("Cortado."));
    }));
    core.addTool(herramienta("audio_set", "audio", false, [&](const json&, Done d) {
        ++audios;
        d(ToolResult::ok("Listo."));
    }));
    core.addTool(herramienta("ptz_camaras", "camaras", true, [](const json&, Done d) { d(ToolResult().data({{"camaras", json::array()}})); }));
    core.addTool(herramienta("control_web_perfil", "control_web", false, [](const json&, Done d) { d(ToolResult::ok("no")); }));

    ControlWeb cw(dir.path().toStdString());
    cw.setCore(&core);
    std::vector<Enviado> enviados;
    std::vector<std::string> cortadas;
    int credenciales = 0;
    std::vector<json> resueltas;
    QObject::connect(&cw, &ControlWeb::enviar, [&](const QStringList& a, const json& m) {
        Enviado e;
        for (const QString& s : a) e.a.push_back(s.toStdString());
        e.msg = m;
        enviados.push_back(e);
    });
    QObject::connect(&cw, &ControlWeb::cortar, [&](const QString& c, const QString&) { cortadas.push_back(c.toStdString()); });
    QObject::connect(&cw, &ControlWeb::credencialesCambio, [&] { ++credenciales; });
    QObject::connect(&cw, &ControlWeb::resuelta, [&](const json& r) { resueltas.push_back(r); });
    const auto ultimo = [&](const std::string& tipo) -> json {
        for (auto it = enviados.rbegin(); it != enviados.rend(); ++it)
            if (it->msg.value("type", "") == tipo) return it->msg;
        return nullptr;
    };
    const auto cuantos = [&](const std::string& tipo) {
        int n = 0;
        for (const auto& e : enviados) n += e.msg.value("type", "") == tipo;
        return n;
    };

    // --- Perfiles (estáticos) -------------------------------------------------------------
    CHECK(ControlWeb::normalizarPerfil("*") == json::array({"*"}), "\"*\" es todo");
    CHECK(ControlWeb::normalizarPerfil(json::array({"audio", "escenas", "audio"})) == json::array({"audio", "escenas"}),
          "ordenado y sin repetidos");
    CHECK(ControlWeb::normalizarPerfil(json::array({"control_web", "audio"})) == json::array({"audio"}),
          "control_web no entra en un perfil");
    CHECK(ControlWeb::normalizarPerfil(42).is_null(), "un número no es un perfil");
    CHECK(ControlWeb::amplia(json::array({"audio"}), json::array({"audio", "escenas"})), "sumar una categoría amplía");
    CHECK(ControlWeb::amplia(json::array({"audio"}), json::array({"*"})), "pasar a todo amplía");
    CHECK(!ControlWeb::amplia(json::array({"audio", "escenas"}), json::array({"audio"})), "sacar no amplía");
    CHECK(!ControlWeb::amplia(json::array({"*"}), json::array({"audio"})), "de todo a algo no amplía");
    CHECK(!ControlWeb::permite(json::array({"*"}), "control_web"), "ni con todo se usa control_web");

    // --- Emparejar ----------------------------------------------------------------------
    const json cod = cw.nuevoCodigo(json::array({"escenas"}), "iPad");
    const std::string codigo = cod.value("codigo", "");
    CHECK(codigo.size() == 32, "código de 128 bits");
    CHECK(cw.solicitud("s1", std::string(32, 'f'), "x", "192.168.1.9") != "", "un código inventado no sirve");
    CHECK(cw.solicitud("s1", codigo, "iPad Dirección", "192.168.1.9").empty(), "el código canjeado: queda la solicitud");
    CHECK(cw.solicitud("s2", codigo, "otro", "192.168.1.10") != "", "el código es de un solo uso");
    CHECK(cw.solicitudes().size() == 1, "una solicitud esperando a una persona");
    json r = cw.resolverSolicitud("s1", true, nullptr, false);
    CHECK(r.contains("error") && cw.solicitudes().size() == 1, "aprobar sin una persona: no (y la solicitud sigue)");
    const int credAntes = credenciales;
    r = cw.resolverSolicitud("s1", true, nullptr, true);
    const std::string token = r.value("token", "");
    const std::string ap = r.value("aparato", "");
    CHECK(r.value("aprobada", false) && token.size() == 64 && !ap.empty(), "una persona aprueba: aparato y token");
    CHECK(credenciales > credAntes && !resueltas.empty() && resueltas.back().value("estado", "") == "aprobada" &&
              resueltas.back().value("token", "") == token,
          "credenciales al gateway y el token a la solicitud");
    CHECK(cw.autenticar(token) == ap, "el token vale");
    CHECK(cw.autenticar(std::string(64, '0')).empty(), "otro token no");
    {
        const json cred = cw.credencialesParaGateway();
        CHECK(cred["aparatos"].size() == 1 && cred["aparatos"][0].value("hash", "").size() == 64 &&
                  cred["aparatos"][0].value("hash", "") != token,
              "al gateway va el hash, nunca el token");
    }
    {
        // Persistido y sin el token en claro.
        ControlWeb otra(dir.path().toStdString());
        CHECK(otra.autenticar(token) == ap, "guardado: el token sigue valiendo al volver a abrir");
    }

    // --- Conexión: el auth con las herramientas del perfil -------------------------------------
    cw.conexion("c1", ap, true, "192.168.1.9", "sala");
    json auth = ultimo("auth");
    CHECK(auth.value("ok", false) && auth.value("aparato", "") == ap, "auth ok al conectarse");
    {
        std::set<std::string> nombres;
        for (const auto& h : auth["herramientas"]) nombres.insert(h.value("name", ""));
        CHECK(nombres.count("scene_list") && nombres.count("scene_take"), "ve las de su perfil");
        CHECK(!nombres.count("audio_set") && !nombres.count("ptz_camaras"), "no ve las de fuera del perfil");
        CHECK(!nombres.count("control_web_perfil"), "no ve las de administrar el control web");
        bool aire = false;
        for (const auto& h : auth["herramientas"])
            if (h.value("name", "") == "scene_take") aire = h.value("aire", false);
        CHECK(aire, "scene_take viene marcada de aire");
    }
    // --- Preferencias del aparato: en su registro (sobreviven a reiniciar la app) ---------------
    CHECK(auth.value("preferencias", json()).is_object() && auth["preferencias"].empty(), "sin preferencias todavía");
    cw.entrada("c1", ap, {{"type", "preferencias"}, {"datos", {{"ocultas", {"ptz", "audio"}}}}});
    cw.entrada("c1", ap, {{"type", "preferencias"}, {"datos", "no es un objeto"}}); // se ignora
    {
        ControlWeb otra(dir.path().toStdString()); // la app de nuevo: lee el archivo
        otra.setCore(&core);
        std::vector<json> enviados2;
        QObject::connect(&otra, &ControlWeb::enviar,
                         [&](const QStringList&, const nlohmann::json& m) { enviados2.push_back(m); });
        otra.conexion("r1", ap, true, "192.168.1.9", "sala");
        json pref;
        for (const auto& m : enviados2)
            if (m.value("type", "") == "auth") pref = m.value("preferencias", json());
        CHECK(pref.value("ocultas", json::array()) == json({"ptz", "audio"}), "las preferencias vuelven al reconectarse");
    }
    cw.conexion("c9", "cw-no-existe", true, "192.168.1.66", "sala");
    CHECK(!cortadas.empty() && cortadas.back() == "c9" && !ultimo("auth").value("ok", true), "un aparato desconocido: auth no y se corta");

    // --- llamar ---------------------------------------------------------------------------
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 1}, {"herramienta", "audio_set"}, {"args", json::object()}});
    json res = ultimo("resultado");
    CHECK(res.value("id", 0) == 1 && !res.value("ok", true) && audios == 0, "fuera del perfil: no corre");
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 2}, {"herramienta", "scene_list"}, {"args", json::object()}});
    res = ultimo("resultado");
    CHECK(res.value("id", 0) == 2 && res.value("ok", false) && res["datos"].value("programa", "") == "A",
          "en el perfil: corre y devuelve los datos");
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 3}, {"herramienta", "no_existe"}});
    CHECK(!ultimo("resultado").value("ok", true), "una que no existe: error");

    // --- Confirmar lo de aire ------------------------------------------------------------------
    hayAire = true;
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 4}, {"herramienta", "scene_take"}, {"args", json::object()}});
    const json conf = ultimo("confirmar");
    CHECK(conf.value("id", 0) == 4 && conf.value("herramienta", "") == "scene_take" && cortes == 0,
          "de aire con aire: pide confirmar y no corre");
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 5}, {"herramienta", "scene_take"}, {"args", json::object()}, {"confirmado", true}});
    CHECK(cortes == 1 && ultimo("resultado").value("id", 0) == 5 && ultimo("resultado").value("ok", false),
          "confirmado: corre (como persona, sin pasar por la guarda del asistente)");
    hayAire = false;
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 6}, {"herramienta", "scene_take"}, {"args", json::object()}});
    CHECK(cortes == 2, "sin aire: corre directo");

    // --- mirar ----------------------------------------------------------------------------
    const int estadosAntes = cuantos("estado");
    cw.entrada("c1", ap, {{"type", "mirar"}, {"temas", {"escenas", "ptz", "nada"}}});
    const json mir = ultimo("mirando");
    CHECK(mir["temas"] == json::array({"escenas"}), "mira lo de su perfil");
    CHECK(mir["rechazados"].size() == 2, "rechaza ptz (fuera del perfil) y un tema que no existe");
    CHECK(cuantos("estado") == estadosAntes + 1 && ultimo("estado").value("tema", "") == "escenas" &&
              ultimo("estado")["datos"].value("programa", "") == "A",
          "empuja el estado al empezar a mirar");
    esperar(1200); // más de dos vueltas de 500 ms, sin cambios
    CHECK(cuantos("estado") == estadosAntes + 1 && consultasEscenas >= 3, "sin cambios no manda nada (pero consulta)");
    programa = "B";
    esperar(700);
    CHECK(cuantos("estado") == estadosAntes + 2 && ultimo("estado")["datos"].value("programa", "") == "B",
          "cambió: lo empuja una vez");
    // Una segunda conexión que mira lo mismo recibe lo último ya, sin esperar.
    cw.conexion("c2", ap, true, "192.168.1.9", "sala");
    cw.entrada("c2", ap, {{"type", "mirar"}, {"temas", {"escenas"}}});
    CHECK(!enviados.empty() && enviados.back().msg.value("type", "") == "estado" && enviados.back().a == std::vector<std::string>{"c2"},
          "la segunda conexión recibe lo último al empezar a mirar");
    cw.entrada("c1", ap, {{"type", "mirar"}, {"temas", json::array()}});
    cw.entrada("c2", ap, {{"type", "mirar"}, {"temas", json::array()}});
    const int consultas = consultasEscenas;
    esperar(1100);
    CHECK(consultasEscenas == consultas, "nadie lo mira: no se consulta más");

    // --- Perfil: ampliar lo hace una persona ---------------------------------------------------
    CHECK(cw.setPerfil(ap, json::array({"*"}), false) != "", "el asistente no amplía");
    CHECK(cw.setPerfil(ap, json::array({"escenas", "audio"}), true).empty(), "una persona amplía");
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 7}, {"herramienta", "audio_set"}, {"args", json::object()}});
    CHECK(audios == 1, "con el perfil ampliado, corre");
    CHECK(cw.setPerfil(ap, json::array({"escenas"}), false).empty(), "achicar sí puede el asistente");
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 8}, {"herramienta", "audio_set"}, {"args", json::object()}});
    CHECK(audios == 1, "achicado: ya no corre");
    cw.entrada("c1", ap, {{"type", "llamar"}, {"id", 9}, {"herramienta", "control_web_perfil"}, {"args", json::object()}});
    CHECK(!ultimo("resultado").value("ok", true), "un aparato no se amplía solo (control_web, nunca)");

    // --- Túnel ----------------------------------------------------------------------------
    CHECK(cw.setTunel(true, "1234", false) != "" && !cw.tunel(), "habilitar desde afuera: solo una persona");
    CHECK(cw.setTunel(true, "12", true) != "", "PIN corto: no");
    CHECK(cw.setTunel(true, "4321", true).empty() && cw.tunel(), "una persona lo habilita con PIN");
    CHECK(cw.credencialesParaGateway()["tunel"].value("hash_pin", "").size() == 64, "al gateway va el hash del PIN");
    CHECK(cw.setTunel(false, "", false).empty() && !cw.tunel(), "apagarlo lo puede cualquiera");

    // --- Quitar ---------------------------------------------------------------------------
    cortadas.clear();
    CHECK(cw.quitar(ap), "quitar");
    CHECK(cortadas.size() == 2 && cw.autenticar(token).empty() && cw.conectados() == 0, "corta sus conexiones y el token no vale más");

    // --- pedirAprobacion -------------------------------------------------------------------
    bool dijo = true;
    core.pedirAprobacion("control_web_perfil", json::object(), "prueba", [&](bool ok) { dijo = ok; });
    CHECK(!dijo, "sin a quién preguntar: no");
    core.setApprover([](const Tool&, const json&, const std::string&, std::function<void(bool)> d) { d(true); });
    core.pedirAprobacion("control_web_perfil", json::object(), "prueba", [&](bool ok) { dijo = ok; });
    CHECK(dijo, "una persona aprueba: sí");
    core.setApprover(nullptr);
    core.setSinAprobaciones(true);
    dijo = false;
    core.pedirAprobacion("control_web_perfil", json::object(), "prueba", [&](bool ok) { dijo = ok; });
    CHECK(dijo, "modo prueba: sí");

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
