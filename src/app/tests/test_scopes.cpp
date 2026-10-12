// src/app/tests/test_scopes.cpp — los instrumentos de video (T39) con imágenes sintéticas

#include "video/Scopes.hpp"

#include <QGuiApplication>
#include <QImage>
#include <cstdio>

using namespace ba::app::video;

namespace {
int fallos = 0;
#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (c)                                                                                               \
            std::printf("ok: %s\n", m);                                                                      \
        else {                                                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

QImage lleno(QRgb c) {
    QImage i(320, 180, QImage::Format_RGB32);
    i.fill(c);
    return i;
}

/// ¿Hay algo dibujado (no fondo) en la fila `y` de la imagen?
bool filaConTrazo(const QImage& s, int y) {
    for (int x = 20; x < s.width() - 4; ++x)
        if (qGreen(s.pixel(x, y)) > 120) return true;
    return false;
}
} // namespace

int main(int argc, char** argv) {
    // Los rótulos usan fuentes: hace falta la app de Qt (sin pantalla).
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TipoScope t;
    CHECK(tipoScopeDeTexto("onda", t) && t == TipoScope::Onda, "onda");
    CHECK(tipoScopeDeTexto("Vectorscope", t) && t == TipoScope::Vector, "vectorscope en inglés");
    CHECK(!tipoScopeDeTexto("nada", t), "tipo desconocido");

    // Gris al 50 %: la forma de onda tiene un trazo a media altura y nada arriba.
    const QImage s = dibujarScope(lleno(qRgb(128, 128, 128)), TipoScope::Onda, 512, 288);
    CHECK(s.width() == 512 && s.height() == 288, "tamaño pedido");
    const int medio = 6 + (288 - 12) / 2;
    CHECK(filaConTrazo(s, medio) || filaConTrazo(s, medio + 1) || filaConTrazo(s, medio - 1), "gris: trazo al medio");
    CHECK(!filaConTrazo(s, 20), "gris: nada cerca del 100 %");

    // Un gris en el vectorscopio cae en el centro.
    const QImage v = dibujarScope(lleno(qRgb(128, 128, 128)), TipoScope::Vector, 300, 300);
    CHECK(qGreen(v.pixel(150, 150)) > 100, "gris: punto en el centro del vectorscopio");

    // Legalidad: amarillo al 100 % se pasa de la gama compuesta; al 75 % no.
    CHECK(medirLegalidad(lleno(qRgb(255, 255, 0))).cromaPct > 99, "amarillo 100 %: fuera de gama");
    CHECK(medirLegalidad(lleno(qRgb(191, 191, 0))).cromaPct < 1, "amarillo 75 %: legal");
    CHECK(medirLegalidad(lleno(qRgb(255, 255, 255))).lumaAltaPct > 99, "blanco puro: quemado");
    CHECK(medirLegalidad(lleno(qRgb(0, 0, 0))).lumaBajaPct > 99, "negro puro: aplastado");

    // Los otros dos no fallan con una imagen vacía.
    CHECK(!dibujarScope(QImage(), TipoScope::Parade).isNull(), "parade sin imagen");
    CHECK(!dibujarScope(lleno(qRgb(10, 200, 30)), TipoScope::Histograma).isNull(), "histograma");
    std::printf(fallos == 0 ? "\nscopes: OK\n" : "\nscopes: %d fallo(s)\n", fallos);
    return fallos == 0 ? 0 : 1;
}
