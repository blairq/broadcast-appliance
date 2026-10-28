// test_informes_resultado.cpp — plantillas del resultado de la sesión: rellenar, varias, PDF.
#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>

#include "asamblea/InformesResultado.hpp"

using ba::app::asamblea::InformesResultado;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const json d{{"cuerpo", "Concejo"},
                 {"quorum", {{"presentes", 14}, {"hay", true}}},
                 {"puntos", json::array({json{{"n", 1}, {"titulo", "Presupuesto"}}, json{{"n", 2}, {"titulo", "Tasas"}}})},
                 {"votaciones", json::array()}};
    const auto R = [&](const std::string& t) { return InformesResultado::rellenar(t, d); };
    CHECK(R("{{cuerpo}}: {{quorum.presentes}}") == "Concejo: 14", "campos y campos con punto");
    CHECK(R("[{{nada}}{{quorum.nada}}]") == "[]", "lo que no está queda vacío");
    CHECK(R("{{#puntos}}{{n}}-{{titulo}}({{cuerpo}}) {{/puntos}}") == "1-Presupuesto(Concejo) 2-Tasas(Concejo) ",
          "una lista se repite y ve lo de afuera");
    CHECK(R("{{#votaciones}}x{{/votaciones}}{{^votaciones}}ninguna{{/votaciones}}") == "ninguna", "lista vacía: la invertida");
    CHECK(R("{{#quorum.hay}}hay{{/quorum.hay}}") == "hay", "un sí sale una vez");
    CHECK(R("{{#puntos.0}}cabeza{{/puntos.0}}{{#votaciones.0}}no{{/votaciones.0}}") == "cabeza", "lista.0: si hay alguno");
    CHECK(R("a\n{{#puntos}}\n| {{n}} |\n{{/puntos}}\nb") == "a\n| 1 |\n| 2 |\nb", "los tags solos en su renglón se lo llevan");
    CHECK(R("{{#puntos}}sin cerrar") == "", "una sección sin cerrar corta ahí");

    QTemporaryDir dir, evento;
    json contenido = d;
    QString pedido;
    InformesResultado inf(
        dir.path().toStdString(), [&]() { return contenido; }, [&]() { return std::filesystem::path(evento.path().toStdString()); },
        [&](const QString& md, const QString&, const QString& destino) {
            pedido = md;
            return destino.endsWith(".pdf") ? QString() : QString("mal");
        });
    int avisos = 0;
    QObject::connect(&inf, &InformesResultado::cambio, [&]() { ++avisos; });
    CHECK(inf.plantillas().size() == 1, "la primera vez hay una de fábrica");
    const std::string fabrica = inf.plantillas()[0].value("id", std::string());
    CHECK(!inf.texto(fabrica).empty(), "la de fábrica tiene texto");
    std::string err;
    const std::string id = inf.crear("Acta corta", "# {{cuerpo}}\n{{#puntos}}- {{titulo}}\n{{/puntos}}", &err);
    CHECK(id == "acta-corta" && inf.plantillas().size() == 2, "otra plantilla, con su id");
    CHECK(inf.crear("Acta corta", "x", &err) == "acta-corta-2", "mismo nombre: otro id");
    CHECK(inf.crear("  ", "x", &err).empty() && !err.empty(), "sin nombre: error");
    CHECK(inf.vista(id) == "# Concejo\n- Presupuesto\n- Tasas\n", "la vista rellena con lo de ahora");
    CHECK(inf.guardar(id, "{{cuerpo}}!", "Breve").empty() && inf.texto(id) == "{{cuerpo}}!",
          "guardar cambia el texto");
    CHECK(inf.plantillas()[1].value("nombre", std::string()) == "Breve", "y el nombre");
    const json g = inf.generar(id);
    CHECK(g.contains("archivo") && pedido == "Concejo!" && std::filesystem::exists(g.value("markdown", std::string())),
          "generar: el PDF de la rellenada y el .md al lado, en la carpeta del evento");
    CHECK(inf.generar("no-existe").contains("error"), "plantilla que no existe");
    CHECK(inf.quitar("acta-corta-2").empty() && inf.plantillas().size() == 2 && !inf.quitar("acta-corta-2").empty(), "quitar");
    CHECK(avisos >= 4, "cada cambio avisa");
    {
        InformesResultado otra(dir.path().toStdString(), {}, {}, {});
        CHECK(otra.plantillas().size() == 2 && otra.texto(id) == "{{cuerpo}}!", "quedan guardadas");
        CHECK(otra.generar(id).contains("error"), "sin evento abierto no genera");
    }
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
