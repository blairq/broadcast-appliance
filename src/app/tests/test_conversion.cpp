// ============================================================================
// src/app/tests/test_conversion.cpp — de un .pptx a laminas, de verdad
//
// Esto NO simula la conversion: corre el DeckCraft vendoreado (tools/deps/craft.sh)
// sobre una presentacion de tres laminas y cuenta los PNG que salen.
//
//   1. Que sabemos desarmar, y que un PDF no pasa por DeckCraft.
//   2. Un .pptx entero: tres paginas, tres PNG, y el avance llega al final.
//   3. Un archivo que no existe falla avisando, no revienta.
//   4. Una ruta con espacios (la carpeta del aparato se llama «Broadcast Appliance»).
// ============================================================================
#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include "carrusel/Conversion.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace ba::carrusel;

namespace {

int g_fallos = 0;

/// Bajo Wine (los tests de Windows se corren asi desde Linux) las herramientas de
/// Windows no se prueban: eso va en un Windows de verdad. Wine se reconoce por
/// `wine_get_version`, que solo exporta su ntdll.
bool bajoWine() {
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

/// Hay un DeckCraft que se pueda correr aca.
bool hayDeck() { return !Conversor::herramienta("deckcraft").isEmpty() && !bajoWine(); }
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

/// Corre una conversion hasta que termine. Devuelve las laminas, o {} si fallo.
QStringList correr(Conversor& c, const QString& entrada, const QString& salida, QString* motivo,
                   int* ultimoHecho, int* ultimoTotal) {
    QStringList res;
    bool termino = false;
    QObject::connect(&c, &Conversor::listo, [&](QStringList l) {
        res = l;
        termino = true;
    });
    QObject::connect(&c, &Conversor::fallo, [&](QString m) {
        *motivo = m;
        termino = true;
    });
    QObject::connect(&c, &Conversor::avance, [&](QString, int h, int t) {
        *ultimoHecho = h;
        *ultimoTotal = t;
    });
    c.convertir(entrada, salida);
    // Tope duro: si algo se cuelga, el test tiene que terminar igual.
    QTimer tope;
    tope.setSingleShot(true);
    tope.start(300000);
    while (!termino && tope.isActive())
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 100);
    if (!termino) *motivo = "se colgo";
    return res;
}

QString fixture(const char* nombre) { return QString(BA_TEST_DATOS) + "/" + nombre; }

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // CON ESPACIO, A PROPOSITO: QTemporaryDir nombra la carpeta con el nombre del
    // programa, y el aparato se llama «Broadcast Appliance».
    QCoreApplication::setApplicationName("Prueba De Conversion");
    QTemporaryDir datos;
    qputenv("BA_DATA_ROOT", datos.path().toUtf8()); // la carpeta de las herramientas (Runtime/craft), acá

    // --- 1. Que sabemos desarmar -------------------------------------------
    CHECK(Conversor::esDocumento("a.pptx"), "pptx es documento");
    CHECK(Conversor::esDocumento("A.PDF"), "la extension no distingue mayusculas");
    CHECK(!Conversor::esDocumento("a.jpg"), "una foto no se desarma");
    CHECK(!Conversor::necesitaConvertir("a.pdf"), "un PDF no pasa por DeckCraft");
    CHECK(Conversor::necesitaConvertir("a.pptx"), "un pptx si");
    CHECK(!Conversor::esDocumento("a.ppt"), "el .ppt viejo no: DeckCraft no lo lee");

    // --- 3. Un archivo que no existe ---------------------------------------
    {
        QTemporaryDir tmp;
        Conversor c;
        QString motivo;
        int h = -1, t = -1;
        const QStringList r = correr(c, tmp.path() + "/no-esta.pptx", tmp.path() + "/out", &motivo, &h, &t);
        CHECK(r.isEmpty(), "no devuelve laminas de la nada");
        CHECK(motivo.contains("no existe"), "avisa que no existe");
    }

    // --- 2. El .pptx entero -------------------------------------------------
    if (!hayDeck()) {
        std::printf(bajoWine() ? "SALTEADO: bajo Wine no se prueban las herramientas de Windows\n"
                               : "SALTEADO: no hay extern/craft en este equipo\n");
    } else {
        QTemporaryDir tmp;
        Conversor c;
        QString motivo;
        int hecho = -1, total = -1;
        const QStringList r =
            correr(c, fixture("tres-laminas.pptx"), tmp.path() + "/out", &motivo, &hecho, &total);
        CHECK(motivo.isEmpty(), qPrintable("la conversion fallo: " + motivo));
        CHECK(r.size() == 3, "tres laminas del pptx de tres paginas");
        CHECK(hecho == 3 && total == 3, "el avance llego al final");
        for (const QString& p : r) {
            CHECK(QFileInfo(p).size() > 0, "la lamina no quedo vacia");
            QImage img(p);
            CHECK(!img.isNull(), "la lamina es una imagen de verdad");
            CHECK(img.width() == 1920, "sale al ancho pedido");
        }
    }

    // --- 4. Una ruta con espacios -------------------------------------------
    // No es rebuscado: la carpeta del aparato se llama «Broadcast Appliance».
    if (hayDeck()) {
        QTemporaryDir tmp;
        const QString conEspacios = tmp.path() + "/Con Espacios Adentro";
        QDir().mkpath(conEspacios);
        Conversor c;
        QString motivo;
        int h = -1, t2 = -1;
        const QStringList r = correr(c, fixture("tres-laminas.pptx"), conEspacios, &motivo, &h, &t2);
        CHECK(motivo.isEmpty(), qPrintable("con espacios fallo: " + motivo));
        CHECK(r.size() == 3, "tres laminas tambien con espacios en la ruta");
    }

    if (g_fallos == 0) std::printf("test_conversion: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
