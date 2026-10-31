// test_reloj_vistas.cpp — el Reloj de las vistas (ui/Reloj) y las fotos de las personas (ui/FotosPersonas)
//
//   Reloj: corre solo mientras alguien está conectado a su aviso (un enlace de QML se
//   conecta igual que acá), cada uno por separado, y se apaga al irse el último.
//   FotosPersonas: una foto por persona; la URL cambia solo si la foto cambió; lo que no
//   es JPEG no entra; dejarSolo saca a los que se fueron.

#include <QCoreApplication>
#include <QObject>
#include <QSet>

#include <cstdio>

#include "ui/FotosPersonas.hpp"
#include "ui/Reloj.hpp"

using ba::app::FotosPersonas;
using ba::app::Reloj;

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

QByteArray jpeg(char marca) { return QByteArray("\xFF\xD8\xFF") + marca; }
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // --- Reloj ------------------------------------------------------------------------------
    {
        Reloj r;
        CHECK(r.lectores() == 0 && r.lectoresFinos() == 0, "reloj: sin nadie, quieto");
        QObject a, b;
        const auto c1 = QObject::connect(&r, &Reloj::ahoraCambio, &a, [] {});
        const auto c2 = QObject::connect(&r, &Reloj::ahoraCambio, &b, [] {});
        CHECK(r.lectores() == 2, "reloj: cuenta cada lector");
        const auto c3 = QObject::connect(&r, &Reloj::ahoraFinoCambio, &a, [] {});
        CHECK(r.lectoresFinos() == 1 && r.lectores() == 2, "reloj: el fino se cuenta aparte");
        QObject::disconnect(c1);
        CHECK(r.lectores() == 1, "reloj: se va uno, queda el otro");
        QObject::disconnect(c2);
        QObject::disconnect(c3);
        CHECK(r.lectores() == 0 && r.lectoresFinos() == 0, "reloj: sin lectores se apaga");
        // Un lector que se destruye sin desconectarse.
        {
            QObject c;
            QObject::connect(&r, &Reloj::ahoraCambio, &c, [] {});
            CHECK(r.lectores() == 1, "reloj: otro lector");
        }
        CHECK(r.lectores() == 0, "reloj: el lector destruido se suelta solo");
        CHECK(static_cast<qint64>(r.ahora()) % 1000 == 0, "reloj: «ahora» va redondeado al segundo");
    }
    // --- FotosPersonas -----------------------------------------------------------------------
    {
        FotosPersonas& f = FotosPersonas::las();
        int avisos = 0;
        QObject::connect(&f, &FotosPersonas::cambio, [&] { ++avisos; });
        f.poner("sala:a", jpeg('1'));
        const QString u1 = f.url("sala:a");
        CHECK(!u1.isEmpty() && u1.startsWith("image://personas/") && avisos == 1, "fotos: entra una y avisa");
        f.poner("sala:a", jpeg('1'));
        CHECK(f.url("sala:a") == u1 && avisos == 1, "fotos: la misma foto no cambia la URL ni avisa");
        f.poner("sala:a", jpeg('2'));
        CHECK(f.url("sala:a") != u1 && avisos == 2, "fotos: otra foto cambia la URL");
        f.poner("sala:b", QByteArray("no es jpeg"));
        CHECK(f.url("sala:b").isEmpty() && avisos == 2, "fotos: lo que no es JPEG no entra");
        f.poner("integrante:7", jpeg('3'));
        CHECK(f.jpeg("integrante:7") == jpeg('3'), "fotos: los bytes, por clave");
        f.dejarSolo("sala:", QSet<QString>{});
        CHECK(f.url("sala:a").isEmpty() && !f.url("integrante:7").isEmpty(), "fotos: dejarSolo saca solo las de ese prefijo");
        CHECK(f.urls().size() == 1 && f.urls().contains("integrante:7"), "fotos: urls tiene las que quedan");
    }
    std::printf(g_fallas == 0 ? "\nTODO BIEN\n" : "\n%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
