// test_despachador.cpp — roles y despachador, con un motor falso
//
//   Roles: los de fabrica, quien reclama que disparador (solo activos), lo que
//   elige el operador se guarda y se relee, cambiar de proveedor borra el
//   modelo.
//   Despachador: un evento sin "llm" no despierta a nadie; uno que ningun rol
//   activo reclama tampoco; uno reclamado lanza una corrida y avisa
//   (eventoTomado); la corrida lee la salida stream-json del motor (pasos,
//   herramientas, RESUMEN); una por rol a la vez (cola); agrupacion (cinco
//   picos = una corrida); profundidad maxima; contexto y memoria (el encargo
//   lleva lo de la sala y el resumen anterior); motor que falla; detener; el
//   mismo origen de lobby no se duplica.
//
// El motor falso es un script de shell que emite lo que emitiria Claude Code
// en modo -p --output-format stream-json y guarda el encargo que recibio.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <fstream>

#include "agent/Despachador.hpp"
#include "agent/Roles.hpp"

using ba::app::agent::Despachador;
using ba::app::agent::Lanzamiento;
using ba::app::agent::Motores;
using ba::app::agent::Rol;
using ba::app::agent::Roles;
using json = nlohmann::json;

namespace {

int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// Gira el event loop hasta que `cond` sea verdad (o venza).
template <class F> bool esperar(F cond, int ms = 8000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return cond();
}

json evento(const std::string& tipo, const json& notify = json::array({"llm"}),
            const json& data = json::object()) {
    return json{{"id", 1},
                {"ruleId", "a1"},
                {"type", tipo},
                {"text", "texto"},
                {"llmText", "texto seguro"},
                {"data", data},
                {"atMs", 0},
                {"notify", notify},
                {"actions", json::array()}};
}

std::string leer(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll().toStdString();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_despachador ==\n");
    QTemporaryDir tmp;
    const QString dir = tmp.path();

    // --- Roles --------------------------------------------------------------------------
    {
        Roles r((dir + "/roles.json").toStdString());
        CHECK(r.porId("recepcion") != nullptr && r.porId("tecnico") != nullptr, "roles de fabrica");
        CHECK(!r.porId("recepcion")->activo, "apagados de fabrica");
        CHECK(r.porDisparador("lobby") == nullptr, "apagado: no reclama");
        r.setActivo("recepcion", true);
        CHECK(r.porDisparador("lobby") == &*r.porId("recepcion"), "prendido: reclama lobby");
        CHECK(r.porDisparador("audio_peak") == nullptr, "audio_peak no es de recepcion");
        r.setProveedor("recepcion", "opencode-go");
        r.setModelo("recepcion", "gpt-x");
        r.setProveedor("recepcion", "claude-cuenta");
        CHECK(r.porId("recepcion")->modelo.empty(), "cambiar de proveedor borra el modelo");
        CHECK(r.categoriasPorRol()["recepcion"].count("sala") == 1, "categorias del rol");
        CHECK(Roles::comunes().count("ayuda") == 1 && Roles::comunes().count("agentes") == 1, "comunes");
        Roles r2((dir + "/roles.json").toStdString());
        CHECK(r2.porId("recepcion")->activo && r2.porId("recepcion")->proveedor == "claude-cuenta",
              "lo elegido se relee");
        CHECK(!r2.porId("recepcion")->categorias.empty(), "lo del sistema no depende del archivo");
    }

#ifndef _WIN32
    // --- El motor falso -------------------------------------------------------------------
    // $1 = encargo; escribe el encargo en encargo.txt y emite stream-json.
    // Con MOTOR_FALLA=1 termina con codigo 3; con MOTOR_LENTO=1 duerme 30 s.
    const QString motor = dir + "/motor.sh";
    {
        std::ofstream s(motor.toStdString());
        s << "#!/bin/sh\n"
             "printf '%s' \"$1\" > \"$PWD/encargo.txt\"\n"
             "[ -n \"$MOTOR_LENTO\" ] && sleep 30\n"
             "printf '%s\\n' "
             "'{\"type\":\"system\",\"subtype\":\"init\",\"model\":\"falso-1\",\"tools\":[\"a\",\"b\"]}'\n"
             "printf '%s\\n' "
             "'{\"type\":\"assistant\",\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"Voy a "
             "mirar.\"},"
             "{\"type\":\"tool_use\",\"name\":\"mcp__broadcast__room_status\",\"input\":{\"x\":1}}]}}'\n"
             "printf '%s\\n' "
             "'{\"type\":\"user\",\"message\":{\"content\":[{\"type\":\"tool_result\",\"content\":[{\"type\":"
             "\"text\","
             "\"text\":\"todo bien\"}]}]}}'\n"
             "[ -n \"$MOTOR_FALLA\" ] && exit 3\n"
             "printf '%s\\n' "
             "'{\"type\":\"assistant\",\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"Listo."
             "\\nRESUMEN: atendi a "
             "Juan y quedo esperando.\"}]}}'\n"
             "printf '%s\\n' "
             "'{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"Listo.\\nRESUMEN:"
             " atendi a "
             "Juan y quedo esperando.\",\"num_turns\":2,\"duration_ms\":10}'\n";
    }
    QFile::setPermissions(motor, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    Roles roles((dir + "/roles2.json").toStdString());
    QString ambiente; // MOTOR_FALLA / MOTOR_LENTO
    Motores m;
    std::vector<Lanzamiento> lanzados;
    m.lanzar = [&](const Lanzamiento& l, QString*) {
        lanzados.push_back(l);
        ba::app::term::Pty::Options o;
        o.program = "/bin/sh";
        o.args = {motor, l.encargo};
        o.workDir = dir;
        if (!ambiente.isEmpty()) o.env[ambiente] = "1";
        return o;
    };
    m.proveedores = []() { return QVariantList{}; };
    m.modelosDe = [](const QString&) { return QStringList{"uno"}; };
    Despachador d(roles, std::move(m));
    int tomados = 0;
    QObject::connect(&d, &Despachador::eventoTomado, [&](const QString& rol, const QVariantMap& ev) {
        ++tomados;
        CHECK(rol == "recepcion" || rol == "tecnico", "eventoTomado trae el rol");
        CHECK(ev.contains("type"), "eventoTomado trae el evento");
    });
    int errores = 0;
    QObject::connect(&d, &Despachador::notify, [&](const QString&, const QString& level) {
        if (level == "error") ++errores;
    });

    // --- Que no despierta a nadie ----------------------------------------------------
    CHECK(!d.evento(evento("lobby", json::array({"ui"}))), "sin llm: no");
    d.setActivo("recepcion", true);
    CHECK(!d.evento(evento("lobby", json::array({"ui"}))), "sin llm: no, aunque el rol este prendido");
    CHECK(!d.evento(evento("audio_peak")), "tecnico apagado: nadie reclama audio_peak");
    CHECK(d.corridas().empty() && tomados == 0, "nada corrio");
    {
        std::string err;
        CHECK(d.encargar("tecnico", "hola", "operador", 0, &err).empty() &&
                  err.find("apagado") != std::string::npos,
              "encargar a un rol apagado explica");
        CHECK(d.encargar("nadie", "hola", "operador", 0, &err).empty() &&
                  err.find("no existe") != std::string::npos,
              "rol inexistente");
        CHECK(d.encargar("recepcion", "x", "rol:otro", 2, &err).empty() &&
                  err.find("profundidad") != std::string::npos,
              "profundidad 2: no");
    }

    // --- Una corrida completa ----------------------------------------------------------
    d.setContexto("recepcion", []() { return std::string("En la sala: nadie."); });
    CHECK(
        d.evento(evento("lobby", json::array({"llm", "ui"}), json{{"participant", "p1"}, {"name", "Juan"}})),
        "lobby reclamado");
    CHECK(tomados == 1, "eventoTomado");
    CHECK(d.corridas().size() == 1 && d.corridas()[0]["origen"] == "disparador:lobby", "una corrida");
    CHECK(d.profundidadDe("recepcion") == 0, "en curso, profundidad 0");
    CHECK(esperar([&]() { return d.corridas()[0]["estado"] == "ok"; }), "termina ok");
    {
        const json c = d.corrida(d.corridas()[0]["id"].get<std::string>());
        CHECK(c["herramientas"] == 1, "una herramienta");
        CHECK(c["resumen"] == "atendi a Juan y quedo esperando.", "RESUMEN extraido");
        const json pasos = c["pasos"];
        CHECK(pasos.size() == 6, "seis pasos (inicio, texto, herramienta, resultado, texto, fin)");
        CHECK(pasos[0]["tipo"] == "inicio" && pasos[2]["tipo"] == "herramienta" &&
                  pasos[2]["herramienta"] == "room_status" && pasos[3]["tipo"] == "resultado" &&
                  pasos[5]["tipo"] == "fin",
              "pasos leidos del stream-json");
        CHECK(lanzados.size() == 1 && lanzados[0].rol == "recepcion", "se lanzo con el rol");
        const std::string enc = leer(dir + "/encargo.txt");
        CHECK(enc.find("En la sala: nadie.") != std::string::npos,
              "el encargo lleva el contexto del sistema");
        CHECK(enc.find("ENCARGO:") != std::string::npos && enc.find("texto seguro") != std::string::npos,
              "el encargo lleva el texto seguro del evento");
        CHECK(enc.find("Lo que paso antes") == std::string::npos, "primera corrida: sin memoria previa");
    }
    // La segunda corrida recibe el resumen de la primera.
    {
        std::string err;
        const std::string id = d.encargar("recepcion", "segunda", "operador", 0, &err);
        CHECK(!id.empty(), "encargo del operador");
        CHECK(esperar([&]() { return d.corrida(id)["estado"] == "ok"; }), "segunda termina");
        const std::string enc = leer(dir + "/encargo.txt");
        CHECK(enc.find("Lo que paso antes") != std::string::npos &&
                  enc.find("atendi a Juan") != std::string::npos,
              "memoria como dato: el resumen anterior viaja en el encargo");
    }

    // --- Cola: una por rol a la vez; lobby no se duplica ---------------------------------
    {
        ambiente = "MOTOR_LENTO";
        const std::string a = d.encargar("recepcion", "lenta", "lobby:p9");
        const std::string b = d.encargar("recepcion", "otra", "operador");
        const std::string a2 = d.encargar("recepcion", "de nuevo", "lobby:p9");
        CHECK(a2 == a, "el mismo lobby en curso no se duplica");
        CHECK(d.corrida(a)["estado"] == "corriendo" && d.corrida(b)["estado"] == "esperando", "una a la vez");
        CHECK(d.enCurso() == 1, "enCurso");
        d.detener(QString::fromStdString(a));
        CHECK(esperar([&]() { return d.corrida(a)["estado"] == "detenida"; }), "detener mata la corrida");
        ambiente.clear();
        CHECK(esperar([&]() { return d.corrida(b)["estado"] == "ok"; }), "la que esperaba corre despues");
    }

    // --- Agrupacion: cinco picos = una corrida --------------------------------------------
    {
        d.setActivo("tecnico", true);
        const std::size_t antes = d.corridas().size();
        for (int i = 0; i < 5; ++i)
            CHECK(d.evento(evento("audio_peak")), "pico reclamado");
        CHECK(d.corridas().size() == antes + 1, "cinco eventos, una corrida (tope de agrupacion)");
        const std::string id = d.corridas()[0]["id"];
        CHECK(d.corridas()[0]["rol"] == "tecnico", "es del tecnico");
        CHECK(esperar([&]() { return d.corrida(id)["estado"] == "ok"; }), "termina");
        const std::string enc = leer(dir + "/encargo.txt");
        std::size_t n = 0;
        for (std::size_t p = enc.find("tipo audio_peak"); p != std::string::npos;
             p = enc.find("tipo audio_peak", p + 1))
            ++n;
        CHECK(n == 5, "el encargo lleva los cinco");
    }

    // --- Motor que falla ------------------------------------------------------------------
    {
        ambiente = "MOTOR_FALLA";
        const std::string id = d.encargar("tecnico", "falla", "operador");
        CHECK(esperar([&]() { return d.corrida(id)["estado"] == "error"; }), "codigo 3 -> error");
        CHECK(d.corrida(id)["error"].get<std::string>().find("codigo 3") != std::string::npos,
              "el error dice el codigo");
        CHECK(d.corrida(id)["herramientas"] == 1, "lo que alcanzo a hacer queda");
        CHECK(errores >= 1, "avisa a la interfaz");
        ambiente.clear();
    }

    // --- Profundidad 1: puede encargar; lo lanzado por un encargo, no ----------------------
    {
        std::string err;
        const std::string id = d.encargar("tecnico", "de recepcion", "rol:recepcion", 1, &err);
        CHECK(!id.empty(), "profundidad 1 se acepta");
        CHECK(esperar([&]() { return d.corrida(id)["estado"] == "ok"; }), "termina");
    }
#endif

    if (g_failures == 0) {
        std::printf("test_despachador: OK\n");
        return 0;
    }
    std::printf("test_despachador: %d fallo(s)\n", g_failures);
    return 1;
}
