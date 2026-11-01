// test_evento_abierto.cpp — el nombre del evento abierto (ui/EventoAbierto)
//
//   Es lo que pone el vínculo {evento.nombre} de los gráficos (StudioViewModel::
//   contextoDeVinculos, con la llamada tipada eventoAbierto().nombre()): antes se leía
//   `property("nombre")` del EventoViewModel, una propiedad que no existía, y salía vacío.
//   El del guion; sin nombre, la carpeta; sin evento, vacío.

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

#include "ui/EventoAbierto.hpp"

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    ba::app::EventoAbierto ab(std::filesystem::path(tmp.path().toStdString()) / "Eventos");
    CHECK(ab.nombre().isEmpty(), "sin evento abierto: vacío");
    ba::evento::Evento e;
    e.nombre = "Sesión ordinaria";
    ab.poner("2026/sesion-ordinaria", e);
    CHECK(ab.nombre() == QStringLiteral("Sesión ordinaria"), "el nombre del guion");
    ab.poner("2026/sin-nombre", ba::evento::Evento{});
    CHECK(ab.nombre() == QStringLiteral("2026/sin-nombre"), "sin nombre en el guion: la carpeta");
    ab.cerrar();
    CHECK(ab.nombre().isEmpty(), "al cerrar: vacío");
    if (g_fallas == 0) std::printf("test_evento_abierto: todo OK\n");
    return g_fallas == 0 ? 0 : 1;
}
