// test_captura_web.cpp — la foto de una página con chrome-headless-shell (net/CapturaWeb).
#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include "net/CapturaWeb.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using ba::app::CapturaWeb;

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
/// Bajo Wine (los tests de Windows se corren así desde Linux) Chrome no se prueba: eso
/// va en un Windows de verdad. Wine se reconoce por `wine_get_version` de su ntdll.
bool bajoWine() {
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}
} // namespace

int main(int argc, char** argv) {
    QTemporaryDir datos;
    qputenv("BA_DATA_ROOT", datos.path().toUtf8());
    if (bajoWine()) {
        std::printf("SALTEADO: bajo Wine no se prueba Chrome\n");
        return 0;
    }
    if (!QFileInfo(BA_TEST_CHROME).isExecutable()) {
        std::printf("SKIP: no está %s (make deps-chrome-headless)\n", BA_TEST_CHROME);
        return 0;
    }
    qputenv("BA_CHROME_HEADLESS", BA_TEST_CHROME);
    QCoreApplication app(argc, argv);
    CHECK(CapturaWeb::disponible().isEmpty(), "el Chrome sin ventana está");
    CapturaWeb c;
    QImage foto;
    QString error = "sin respuesta";
    c.capturar({"data:text/html,<body style='margin:0;background:rgb(255,0,0)'></body>", 430, 900, 500},
               [&](const QImage& img, const QString& e) {
                   foto = img;
                   error = e;
                   app.quit();
               });
    QTimer::singleShot(60000, &app, &QCoreApplication::quit);
    app.exec();
    CHECK(error.isEmpty(), "sacó la foto sin error");
    CHECK(foto.width() == 430 && foto.height() == 900, "del tamaño pedido (como un celular)");
    const QColor centro = foto.pixelColor(215, 450);
    CHECK(centro.red() > 240 && centro.green() < 20 && centro.blue() < 20, "y se ve la página (roja)");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
