// ============================================================================
// src/app/tests/test_account_guides.cpp — guias para conectar las plataformas
//
//   1. Cada plataforma tiene pasos con enlace y los campos del formulario.
//   2. Facebook usa la URI de redireccion de esta instalacion.
//   3. El JSON de Google se encuentra en Descargas (solo el de "App de
//      escritorio", el mas nuevo).
// ============================================================================

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>

#include "AccountGuides.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

#ifndef _WIN32
void write(const QString& path, const QByteArray& data) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    // Descargas de mentira: HOME temporal (antes de que Qt mire las rutas).
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (home.path() + "/.config").toUtf8());
#endif
    QCoreApplication app(argc, argv);
    using ba::app::accounts::guide;

    for (const char* p : {"youtube", "twitch", "facebook"}) {
        const auto g = guide(p, "http://localhost:9999/");
        CHECK(!g.is_null() && g["steps"].size() >= 3, "tiene pasos");
        bool links = true;
        for (const auto& s : g["steps"])
            links = links && !s["links"].empty() && s["links"][0].value("u", "").rfind("https://", 0) == 0 &&
                    s.value("u", "") == s["links"][0].value("u", "");
        CHECK(links, "cada paso tiene un enlace https (y l/u para las pestañas)");
        CHECK(!g["credential"]["fields"].empty(), "campos del formulario");
        bool secretField = false;
        for (const auto& f : g["credential"]["fields"])
            secretField = secretField || f.value("secret", false);
        CHECK(secretField, "el secreto se marca como tal (campo enmascarado)");
    }
    CHECK(guide("meta", "http://localhost:9999/")["paste"].value("uriDeRedireccion", "") ==
              "http://localhost:9999/",
          "facebook: la URI de redireccion de esta instalacion");
    CHECK(guide("meta", "http://localhost:9999/")["steps"][1].value("d", "").find("http://localhost:9999/") !=
              std::string::npos,
          "y aparece en el paso");
    CHECK(guide("otra").is_null(), "plataforma desconocida");

#ifndef _WIN32
    QString why;
    CHECK(ba::app::accounts::findGoogleClientJson(&why).isEmpty() && why.contains("Descargas"),
          "sin archivo");
    QDir(home.path()).mkpath("Downloads");
    const QString dl = home.path() + "/Downloads/";
    write(dl + "client_secret_web.apps.googleusercontent.com.json",
          R"({"web": {"client_id": "w", "client_secret": "s"}})");
    CHECK(ba::app::accounts::findGoogleClientJson().isEmpty(), "un cliente web no sirve");
    write(dl + "client_secret_viejo.apps.googleusercontent.com.json",
          R"({"installed": {"client_id": "viejo", "client_secret": "s"}})");
    QThread::msleep(1100);
    write(dl + "client_secret_nuevo.apps.googleusercontent.com.json",
          R"({"installed": {"client_id": "nuevo", "client_secret": "s"}})");
    CHECK(ba::app::accounts::findGoogleClientJson().endsWith(
              "client_secret_nuevo.apps.googleusercontent.com.json"),
          "el de App de escritorio mas nuevo");
#endif

    if (g_failures == 0) std::printf("test_account_guides: OK\n");
    return g_failures == 0 ? 0 : 1;
}
