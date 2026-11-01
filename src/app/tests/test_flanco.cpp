// test_flanco.cpp — el flanco de subida (ui/Flanco: alSubir)
//
//   Lo que usa el PIN del día (T47): al pasar el guion a «corriendo de verdad», la sala
//   rota el PIN si cambió el día. Una vez por arranque: ni con cada aviso mientras sigue
//   corriendo, ni al parar; de nuevo al volver a arrancar. Si ya corría al conectarse, no
//   cuenta (no es un arranque).

#include <QCoreApplication>
#include <QObject>

#include <cstdio>

#include "ui/Flanco.hpp"

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
    // El «modelo»: un QObject cuyo aviso es objectNameChanged y cuyo estado es si el nombre
    // empieza con «corre» (cambiar el resto del nombre es «otro aviso con el mismo estado»).
    {
        QObject modelo;
        QObject receptor;
        int veces = 0;
        ba::app::alSubir(&modelo, &QObject::objectNameChanged, &receptor, [&]() { return modelo.objectName().startsWith("corre"); },
                         [&]() { ++veces; });
        modelo.setObjectName("parado");
        CHECK(veces == 0, "parado: nada");
        modelo.setObjectName("corre");
        CHECK(veces == 1, "arranca: una vez");
        // Otro aviso con el mismo estado (cambia otra cosa del modelo): no se repite.
        modelo.setObjectName("corre (otra cosa)");
        CHECK(veces == 1, "sigue corriendo: no se repite");
        modelo.setObjectName("parado");
        CHECK(veces == 1, "al parar: nada");
        modelo.setObjectName("corre");
        CHECK(veces == 2, "vuelve a arrancar: otra vez");
    }
    {
        QObject modelo;
        modelo.setObjectName("corre");
        int veces = 0;
        {
            QObject receptor;
            ba::app::alSubir(&modelo, &QObject::objectNameChanged, &receptor, [&]() { return modelo.objectName().startsWith("corre"); },
                             [&]() { ++veces; });
            modelo.setObjectName("corre (otra cosa)");
            CHECK(veces == 0, "ya corría al conectarse: no es un arranque");
        }
        modelo.setObjectName("parado");
        modelo.setObjectName("corre");
        CHECK(veces == 0, "sin el receptor, la conexión se fue con él");
    }
    if (g_fallas == 0) std::printf("test_flanco: todo OK\n");
    return g_fallas == 0 ? 0 : 1;
}
