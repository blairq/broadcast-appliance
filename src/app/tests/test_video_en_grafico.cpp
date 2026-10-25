// src/app/tests/test_video_en_grafico.cpp — video en vivo dentro de un gráfico
//
// La cámara que entra con el zócalo (graficos/VideoEnGrafico): leer lo que declara la
// ficha (con la fuente de un parámetro), armar la línea de tiempo (entrada, quieta, la
// salida), recortarla a donde arranca lo que se dibuja (sacar() arranca ya entrado) y
// pasarla al lienzo del motor (con «cubrir»: el recorte que llena sin deformar). Y que
// el «Zócalo con cámara» de fábrica se entienda.

#include "graficos/VideoEnGrafico.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <cstdio>

#ifndef BA_TEST_GRAFICOS
#define BA_TEST_GRAFICOS "assets/graficos" // lo pone CMake (la carpeta de los de fábrica)
#endif

using namespace ba::app::graficos;

namespace {

int fallos = 0;

#define CHECK(cond, msg)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::fprintf(stderr, "FALLA: %s\n", msg);       \
            ++fallos;                                       \
        }                                                   \
    } while (0)

bool cerca(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

QJsonObject obj(const char* json) { return QJsonDocument::fromJson(json).object(); }

} // namespace

int main() {
    // Una ficha de 1000x200 con la cámara de un parámetro.
    const QJsonObject ficha = obj(R"({
        "ancho": 1000, "alto": 200,
        "parametros": [{"clave": "camara", "tipo": "fuente", "porDefecto": "Cámara 1"},
                       {"clave": "nombre", "tipo": "texto", "porDefecto": "X"}],
        "escena": [{"fuente": "{camara}", "donde": "debajo",
                    "claves": [{"ms": 0, "x": 100, "y": 20, "ancho": 200, "alto": 100, "alfa": 0},
                               {"ms": 400, "alfa": 1, "suave": false}],
                    "salida": [{"ms": 0}, {"ms": 200, "x": 0, "alfa": 0}]}]
    })");

    // --- leer ------------------------------------------------------------------------
    {
        auto capas = video::leer(ficha, {});
        CHECK(capas.size() == 1, "una capa de video");
        CHECK(capas[0].fuente == "Cámara 1", "sin pedido: la fuente sale del porDefecto del parámetro");
        CHECK(!capas[0].arriba && !capas[0].propia && capas[0].ajuste == "cubrir", "donde/propia/ajuste");
        CHECK(capas[0].claves.size() == 2, "dos claves de entrada");
        const auto& k1 = capas[0].claves[1];
        CHECK(cerca(k1.x, 0.1) && cerca(k1.y, 0.1) && cerca(k1.ancho, 0.2) && cerca(k1.alto, 0.5),
              "lo que una clave no dice lo hereda (en fracciones del gráfico)");
        CHECK(cerca(k1.alfa, 1) && !k1.suave, "lo que sí dice, lo suyo");
        CHECK(capas[0].salida.size() == 2 && cerca(capas[0].salida[1].x, 0) && cerca(capas[0].salida[1].ancho, 0.2),
              "la salida hereda de la última clave de entrada");

        const QJsonObject pedido = obj(R"({"parametros": {"camara": "Invitado 2"}})");
        capas = video::leer(ficha, pedido);
        CHECK(capas.size() == 1 && capas[0].fuente == "Invitado 2", "con pedido: la fuente de sus valores");

        const QJsonObject vacia = obj(R"({"parametros": {"camara": ""}})");
        CHECK(video::leer(ficha, vacia).empty(), "el parámetro vacío: sale sin video");
        CHECK(video::leer(ficha, vacia, nullptr, true).size() == 1, "el editor la muestra igual (sinFuente)");

        // Lo del editor (la descripción sin guardar) manda sobre la ficha.
        const QJsonObject delEditor = obj(R"({"parametros": {}, "declarativo": {"ancho": 500, "alto": 100,
            "escena": [{"fuente": "Cámara 3", "propia": true, "ajuste": "contener", "claves": [{"x": 250}]}]}})");
        capas = video::leer(ficha, delEditor);
        CHECK(capas.size() == 1 && capas[0].fuente == "Cámara 3" && capas[0].propia && capas[0].ajuste == "contener",
              "la de la descripción del editor, no la de la ficha");
        CHECK(capas[0].claves.size() == 1 && cerca(capas[0].claves[0].x, 0.5), "con el tamaño de la descripción");
        CHECK(capas[0].arriba, "de fábrica, arriba del gráfico");

        CHECK(video::leer(obj(R"({"ancho": 900})"), {}).empty(), "sin «escena»: sin video");
        CHECK(video::tieneVideo(ficha) && !video::tieneVideo(obj("{}")), "tieneVideo");
        CHECK(video::parametrosDeFuente(ficha) == QStringList{"camara"}, "los parámetros de tipo fuente");
    }

    // --- línea de tiempo -----------------------------------------------------------------
    {
        const auto c = video::leer(ficha, {})[0];
        auto t = video::lineaDeTiempo(c, 0, 300);
        CHECK(t.size() == 2, "sin salidaEn: solo la entrada");
        t = video::lineaDeTiempo(c, 2000, 300);
        CHECK(t.size() == 5, "con salidaEn: entrada, quieta y la salida declarada");
        CHECK(t[2].ms == 2000 && cerca(t[2].alfa, 1), "quieta hasta que se va");
        CHECK(t[4].ms == 2200 && cerca(t[4].alfa, 0) && cerca(t[4].x, 0), "la salida, corrida a salidaEn");

        video::Capa sinSalida = c;
        sinSalida.salida.clear();
        t = video::lineaDeTiempo(sinSalida, 1000, 300);
        CHECK(t.back().ms == 1300 && cerca(t.back().alfa, 0) && cerca(t.back().x, 0.1),
              "sin salida declarada: se funde con la del gráfico, en su lugar");

        // en(): lineal (suave: false) a mitad de camino; antes de todo, la primera; después, la última.
        const auto e = video::en(c.claves, 200);
        CHECK(cerca(e.alfa, 0.5), "a mitad de un tramo lineal, la mitad");
        CHECK(cerca(video::en(c.claves, -50).alfa, 0) && cerca(video::en(c.claves, 9999).alfa, 1), "los bordes");

        // desde(): sacar() arranca ya entrado (a los 3000).
        t = video::desde(video::lineaDeTiempo(c, 3000, 300), 3000);
        CHECK(t.front().ms == 0 && cerca(t.front().alfa, 1), "recortada: arranca entera en 0");
        CHECK(t.back().ms == 200 && cerca(t.back().alfa, 0), "y se va en lo que dura su salida");
        t = video::desde(c.claves, 200);
        CHECK(t.size() == 2 && cerca(t[0].alfa, 0.5) && t[1].ms == 200, "recortada en medio de un tramo");
    }

    // --- aJson y el lienzo ---------------------------------------------------------------
    {
        const auto capas = video::leer(ficha, {});
        const auto j = video::aJson(capas, 0, 300, 0);
        CHECK(j.is_array() && j.size() == 1 && j[0]["fuente"] == "Cámara 1" && j[0]["arriba"] == false, "aJson");
        CHECK(j[0]["claves"].size() == 2 && j[0]["claves"][1]["ms"] == 400, "aJson lleva las claves");

        // El gráfico (1000x200) en un ítem de 1920x1080 a todo el lienzo: contenido, centrado.
        const auto g = video::dondeSeVe({0, 0, 1920, 1080}, 1000, 200);
        CHECK(cerca(g.ancho, 1920) && cerca(g.alto, 384) && cerca(g.y, 348), "dondeSeVe: contenido y centrado");

        // Cubrir: un recuadro de 200x100 del gráfico → (384x192 en el lienzo, 2:1) con una
        // fuente de 1920x1080 (16:9): se recorta arriba y abajo lo que sobra.
        bool contener = true;
        const auto k = video::clavesEnLienzo(j[0], g, 1920, 1080, &contener);
        CHECK(!contener, "cubrir: no se contiene (se recorta)");
        CHECK(k.size() == 2 && cerca(k[0]["x"].get<double>(), 192) && cerca(k[0]["y"].get<double>(), 348 + 38.4, 1e-3),
              "la clave, en el lienzo");
        CHECK(cerca(k[0]["ancho"].get<double>(), 384, 1e-3) && cerca(k[0]["alto"].get<double>(), 192, 1e-3), "su tamaño");
        const double arr = k[0]["recorte"]["arr"].get<double>(), aba = k[0]["recorte"]["aba"].get<double>();
        CHECK(cerca(arr, 60, 1e-3) && cerca(aba, 60, 1e-3) && cerca(k[0]["recorte"]["izq"].get<double>(), 0),
              "cubrir: 1080 - 1920/2 = 120 de alto que sobran, mitad arriba y mitad abajo");

        // Sin el tamaño de la fuente (todavía no dio imagen): contenida, sin recorte.
        const auto k2 = video::clavesEnLienzo(j[0], g, 0, 0, &contener);
        CHECK(contener && cerca(k2[0]["recorte"]["arr"].get<double>(), 0), "sin tamaño: contenida");
    }

    // --- El de fábrica -------------------------------------------------------------------
    {
        QFile f(QStringLiteral(BA_TEST_GRAFICOS) + "/zocalo-camara/grafico.json");
        CHECK(f.open(QIODevice::ReadOnly), "está el «Zócalo con cámara» de fábrica");
        const QJsonObject fab = QJsonDocument::fromJson(f.readAll()).object();
        const auto capas = video::leer(fab, {});
        CHECK(capas.size() == 1 && capas[0].fuente == "Cámara", "lleva la cámara del parámetro");
        CHECK(capas[0].claves.size() == 2 && cerca(capas[0].claves[0].alfa, 0) && cerca(capas[0].claves[1].alfa, 1),
              "entra fundiéndose");
        CHECK(video::parametrosDeFuente(fab) == QStringList{"camara"}, "su parámetro de tipo fuente");
    }

    std::printf("== test_video_en_grafico: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
}
