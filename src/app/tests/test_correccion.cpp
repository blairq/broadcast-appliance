// test_correccion.cpp — la cola de corrección de subtítulos y el WebVTT (FlujosTexto).
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "agent/FlujosTexto.hpp"

using ba::app::agent::FlujosTexto;

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
void esperar(int ms) {
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    FlujosTexto fl(dir.path().toStdString());
    fl.setConfig([](const QString&) { return nlohmann::json{{"salidas", {{"archivo", true}}}}; });
    QStringList salieron;
    QObject::connect(&fl, &FlujosTexto::frase, [&](const QString&, const QString& t) { salieron << t; });

    fl.publicar("Sala", "sin cola", "Voz a texto en «Mic»", 1.0);
    CHECK(salieron == QStringList{"sin cola"}, "apagada: sale directo");

    fl.setCorreccion(true, 600);
    fl.publicar("Sala", "la diputada perez", "Voz a texto en «Mic»", 1.0);
    fl.publicar("Sala", "texto del operador", "Escrito a mano");
    CHECK(salieron.size() == 2 && salieron.last() == "texto del operador", "lo escrito a mano no espera");
    CHECK(fl.pendientes().size() == 1, "la transcripción espera");
    const auto id = fl.pendientes()[0].value("id", 0ULL);
    CHECK(fl.corregir(id, "la diputada Pérez", false).empty() && salieron.last() == "la diputada Pérez", "corregida sale ya");
    CHECK(!fl.corregir(id, "otra", false).empty(), "no se corrige dos veces");

    fl.publicar("Sala", "esto se tira", "Voz a texto en «Mic»", 1.0);
    CHECK(fl.corregir(fl.pendientes()[0].value("id", 0ULL), "", true).empty() && !salieron.contains("esto se tira"),
          "descartada no sale");
    fl.publicar("Sala", "nadie la toca", "Voz a texto en «Mic»", 1.0);
    esperar(400);
    CHECK(!salieron.contains("nadie la toca"), "todavía espera");
    esperar(500);
    CHECK(salieron.contains("nadie la toca"), "vencida, sale sola");
    fl.publicar("Sala", "al apagar sale", "Voz a texto en «Mic»", 1.0);
    fl.setCorreccion(false, 600);
    CHECK(salieron.contains("al apagar sale") && fl.pendientes().empty(), "apagarla suelta lo que esperaba");

    bool vtt = false;
    for (const QString& a : fl.archivosDeSesion())
        if (a.endsWith(".vtt")) {
            QFile f(a);
            f.open(QIODevice::ReadOnly);
            const QString t = QString::fromUtf8(f.readAll());
            vtt = t.startsWith("WEBVTT") && t.contains(" --> ") && t.contains("la diputada Pérez") && !t.contains("esto se tira");
        }
    CHECK(vtt, "WebVTT con lo que salió (corregido, sin lo descartado)");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
