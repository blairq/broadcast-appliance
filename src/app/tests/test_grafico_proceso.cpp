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

    if (g_fallos == 0) std::printf("test_grafico_proceso: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
