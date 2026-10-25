// ============================================================================
// src/app/tests/test_grafico_proceso.cpp — el puente con `ba-grafico`
//
//   1. Dibuja y devuelve una imagen del tamaño pedido.
//   2. Un grafico roto devuelve el error, no una imagen vacia en silencio.
//   3. Un grafico que se cuelga se corta y se avisa, sin trabar a quien llama.
//   4. Despues de un corte, el pedido siguiente vuelve a andar (se relanza).
// ============================================================================

#include <cstdio>

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "graficos/GraficoProceso.hpp"

using ba::app::graficos::GraficoProceso;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

QString escribir(const QTemporaryDir& dir, const QString& nombre, const QString& contenido) {
    const QString ruta = dir.filePath(nombre);
    QFile f(ruta);
    f.open(QIODevice::WriteOnly);
    f.write(contenido.toUtf8());
    return ruta;
}

} // namespace

int main(int argc, char** argv) {
    // Sin pantalla (CI, SSH) en Linux, offscreen: el mismo criterio que
    // src/grafico/main.cpp. Sin esto QGuiApplication aborta al no encontrar
    // display. En Windows y macOS no se toca: ahi offscreen no da OpenGL.
#if !defined(_WIN32) && !defined(__APPLE__)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif
    QGuiApplication app(argc, argv);

    // El binario esta al lado del de este test (build/src/grafico).
    const QString bin = QCoreApplication::applicationDirPath() + "/../grafico/ba-grafico";
    if (!QFile::exists(bin)) {
        std::printf("SKIP: no esta ba-grafico compilado\n");
        return 77;
    }

    QTemporaryDir dir;
    GraficoProceso g(bin);
    CHECK(g.disponible(), "el renderizador esta");

    // --- 1) Un dibujo normal -------------------------------------------------
    {
        const QString qml = escribir(dir, "ok.qml",
                                     "import QtQuick\nItem { width: 200; height: 80\n"
                                     "  Rectangle { anchors.fill: parent; color: \"#ff6a2b\" } }\n");
        QString err;
        const QImage img = g.dibujar(QJsonObject{{"qml", qml}, {"ancho", 200}, {"alto", 80}}, 0, &err);
        // Sin OpenGL (un runner de CI sin drivers) no hay renderizador: se
        // saltea, como los demas tests que necesitan graficos. Cualquier otro
        // error es un fallo de verdad y se muestra.
        if (err.contains("contexto de OpenGL")) {
            std::printf("SKIP: sin OpenGL en esta maquina (%s)\n", qUtf8Printable(err));
            return 77;
        }
        if (!err.isEmpty()) std::printf("error del renderizador: %s\n", qUtf8Printable(err));
        CHECK(err.isEmpty(), "sin error");
        CHECK(img.width() == 200 && img.height() == 80, "del tamaño pedido");
    }

    // --- 2) Un grafico roto --------------------------------------------------
    {
        const QString qml = escribir(dir, "roto.qml", "import QtQuick\nEsto no es QML {\n");
        const QString err = g.validar(QJsonObject{{"qml", qml}});
        CHECK(!err.isEmpty(), "un grafico roto se reporta");
    }

    // --- 3) Un grafico que se cuelga ----------------------------------------
    // Es EL caso que justifica el proceso aparte: un bucle infinito. Tiene que
    // volver con un error, no dejar a la app esperando para siempre.
    {
        const QString qml = escribir(dir, "colgado.qml",
                                     "import QtQuick\nItem { width: 10; height: 10\n"
                                     "  Component.onCompleted: { while (true) {} } }\n");
        QString err;
        const QImage img = g.dibujar(QJsonObject{{"qml", qml}}, 0, &err);
        CHECK(img.isNull(), "no devuelve imagen");
        CHECK(!err.isEmpty(), "y dice que paso");
    }

    // --- 4) Se recupera ------------------------------------------------------
    {
        const QString qml = escribir(dir, "ok2.qml", "import QtQuick\nItem { width: 50; height: 50 }\n");
        QString err;
        const QImage img = g.dibujar(QJsonObject{{"qml", qml}, {"ancho", 50}, {"alto", 50}}, 0, &err);
        CHECK(err.isEmpty(), "despues de un corte, el pedido siguiente anda");
        CHECK(img.width() == 50, "y dibuja bien");
    }

    // --- 5) Un medio (secuencia de cuadros) con el reloj del gráfico ---------
    // Relativo a la CARPETA del gráfico aunque el QML venga como texto, y el
    // mismo cuadro en el mismo instante pedido de cualquier forma.
    {
        const QString carpeta = dir.filePath("con-medio");
        QDir().mkpath(carpeta + "/medios/sec");
        for (int i = 0; i < 10; ++i) {
            QImage c(8, 8, QImage::Format_ARGB32);
            c.fill(QColor(i * 25, 0, 0));
            c.save(QString("%1/medios/sec/%2.png").arg(carpeta).arg(i, 3, 10, QChar('0')));
        }
        escribir(dir, "con-medio/medios/sec/secuencia.json", "{\"fps\": 10}");
        const QJsonObject pedido{{"fuente", "import QtQuick\nimport BA.Grafico\nItem {\n"
                                            "  Medio { anchors.fill: parent; source: \"medios/sec\"; bucle: false; inicio: 100 }\n}\n"},
                                 {"carpeta", carpeta},
                                 {"ancho", 20},
                                 {"alto", 20}};
        const auto rojo = [&](int ms) {
            QString err;
            const QImage img = g.dibujar(pedido, ms, &err);
            if (!err.isEmpty()) std::printf("medio a los %d ms: %s\n", ms, qUtf8Printable(err));
            return img.isNull() ? -1 : (qAlpha(img.pixel(10, 10)) == 0 ? -2 : qRed(img.pixel(10, 10)));
        };
        CHECK(rojo(50) == -2, "medio: antes de su inicio no se ve");
        CHECK(rojo(100) == 0, "medio: en su inicio, el primer cuadro");
        CHECK(rojo(550) == 100, "medio: a los 450 ms de medio (10 por segundo), el cuadro 4");
        CHECK(rojo(9000) == 225, "medio: sin bucle se queda en el último");
        CHECK(rojo(550) == 100, "medio: para atrás da lo mismo");
        QString err;
        const auto tira = g.tira(pedido, {100, 550, 9000}, &err);
        CHECK(err.isEmpty() && tira.size() == 3 && qRed(tira[1].imagen.pixel(10, 10)) == 100 &&
                  qRed(tira[2].imagen.pixel(10, 10)) == 225,
              "medio: la tira da los mismos cuadros");
    }

    // --- 6) Una torta con datos escritos a mano («Sí: 3; No: 1») -------------
    {
        const QJsonObject pedido{
            {"parametros", QJsonObject{{"votos", "Sí: 3; No: 1 #c62828"}}},
            {"declarativo", QJsonObject{{"ancho", 200},
                                        {"alto", 200},
                                        {"entrada", QJsonObject{{"efecto", "aparece"}, {"duracion", 10}}},
                                        {"capas", QJsonArray{QJsonObject{{"tipo", "torta"},
                                                                         {"datos", "{votos}"},
                                                                         {"etiquetas", false},
                                                                         {"izquierda", 0},
                                                                         {"arriba", 0},
                                                                         {"ancho", 200},
                                                                         {"alto", 200}}}}}}};
        QString err;
        const QImage antes = g.dibujar(pedido, 0, &err);
        const QImage despues = g.dibujar(pedido, 1500, &err);
        CHECK(err.isEmpty(), qUtf8Printable("torta: dibuja (" + err + ")"));
        CHECK(!antes.isNull() && qAlpha(antes.pixel(60, 150)) == 0, "torta: al principio no giró");
        // «Sí» (75 %) va desde arriba en sentido horario hasta la izquierda: abajo a la
        // izquierda es «Sí» y arriba a la izquierda, «No».
        CHECK(!despues.isNull() && qAlpha(despues.pixel(60, 150)) > 200, "torta: entera al terminar");
        CHECK(!despues.isNull() && qRed(despues.pixel(60, 60)) > 150 && qBlue(despues.pixel(60, 60)) < 100,
              "torta: «No», con el color que se le escribió");
    }

    // --- 7) Un CAMBIO al aire: sin entrada, desde los valores de antes ------
    {
        const QJsonObject desc{{"ancho", 400},
                               {"alto", 60},
                               {"entrada", QJsonObject{{"efecto", "aparece"}, {"duracion", 300}}},
                               {"capas", QJsonArray{QJsonObject{{"tipo", "barras"},
                                                                {"datos", "{v}"},
                                                                {"orientacion", "horizontal"},
                                                                {"mostrar", "nada"},
                                                                {"maximo", 10},
                                                                {"izquierda", 0},
                                                                {"arriba", 0},
                                                                {"ancho", 400},
                                                                {"alto", 60}}}}};
        const QJsonObject cambio{{"declarativo", desc},
                                 {"parametros", QJsonObject{{"v", "A: 5"}}},
                                 {"anteriores", QJsonObject{{"v", "A: 10"}}},
                                 {"sinEntrada", true}};
        QString err;
        const QImage c0 = g.dibujar(cambio, 0, &err);
        const QImage c1 = g.dibujar(cambio, 1500, &err);
        CHECK(err.isEmpty() && !c0.isNull() && !c1.isNull(), "cambio: dibuja");
        // La barra llena (10 de 10) llega hasta x≈312; a la mitad, hasta x≈216.
        CHECK(!c0.isNull() && qAlpha(c0.pixel(290, 30)) > 200, "cambio: arranca en el valor de antes, sin entrar");
        CHECK(!c1.isNull() && qAlpha(c1.pixel(290, 30)) == 0 && qAlpha(c1.pixel(180, 30)) > 200,
              "cambio: termina en el valor nuevo");
        // Sin cambio, la misma descripción entra de cero (con su fundido).
        const QImage e0 = g.dibujar(QJsonObject{{"declarativo", desc}, {"parametros", QJsonObject{{"v", "A: 5"}}}}, 0, &err);
        CHECK(!e0.isNull() && qAlpha(e0.pixel(180, 30)) == 0, "sin cambio: entra de cero");
    }

    // --- 8) Un video sin importar no rompe el gráfico ------------------------
    {
        QString err;
        const QImage img = g.dibujar(QJsonObject{{"fuente", "import QtQuick\nimport BA.Grafico\nItem {\n"
                                                            "  Medio { anchors.fill: parent; source: \"clip.webm\" }\n}\n"},
                                                 {"carpeta", dir.path()},
                                                 {"ancho", 20},
                                                 {"alto", 20}},
                                     0, &err);
        CHECK(err.isEmpty() && !img.isNull(), "un medio que no se puede leer: el gráfico se dibuja igual");
    }

    if (g_fallos == 0) std::printf("test_grafico_proceso: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
