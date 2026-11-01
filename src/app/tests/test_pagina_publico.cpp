// test_pagina_publico.cpp — los errores de la página del público dicen lo que pasó.
//
// sala::PaginaPublico (lo que usan publico_pagina y la GUI), sin B:
//   1. un fondo que no se puede leer: el error es el de la imagen, sin nada de «servidor»;
//   2. un fondo de más de 2 MB: el error dice que pesa de más, tampoco del servidor;
//   3. solo el título, con la sala sin contestar: el error dice que no se pudo hablar con
//      el servidor (y el porqué de la conferencia).
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

#include "platform/Paths.hpp"
#include "sala/PaginaPublico.hpp"
#include "ui/ConferenceClient.hpp"

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

/// Corre el bucle hasta que `listo` o se acaba el tiempo.
bool esperar(const std::function<bool()>& listo, int ms) {
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return listo();
}

/// El error que le llega a `listo` por cambiar(...), o nullopt si no volvió.
std::optional<std::string> errorDe(ba::app::sala::PaginaPublico& p, const std::optional<std::string>& titulo,
                                   const std::optional<std::string>& fondo) {
    std::optional<std::string> err;
    const std::string inmediato =
        p.cambiar(titulo, std::nullopt, fondo, [&err](const std::string& e, const nlohmann::json&) { err = e; });
    if (!inmediato.empty()) return inmediato;
    esperar([&err]() { return err.has_value(); }, 10000);
    return err;
}

bool contiene(const std::string& s, const char* que) { return s.find(que) != std::string::npos; }
} // namespace

int main(int argc, char** argv) {
    QTemporaryDir dir;
    qputenv(ba::platform::kDataRootEnv, dir.path().toUtf8());
    QCoreApplication app(argc, argv);
    ba::app::ConferenceClient sala; // sin gateway-admin.json: la sala no contesta
    ba::app::sala::PaginaPublico pagina(sala);

    const std::string falta = (std::filesystem::path(dir.path().toStdString()) / "no-esta.png").string();
    auto e = errorDe(pagina, std::nullopt, falta);
    CHECK(e && contiene(*e, "no-esta.png") && !contiene(*e, "servidor"),
          "fondo que no se lee: el error es de la imagen, no del servidor");
    if (e) std::printf("   «%s»\n", e->c_str());

    const std::string grande = (std::filesystem::path(dir.path().toStdString()) / "grande.jpg").string();
    std::ofstream(grande, std::ios::binary) << std::string(ba::app::sala::PaginaPublico::kMaxFondo + 10, 'x');
    e = errorDe(pagina, std::nullopt, grande);
    CHECK(e && contiene(*e, "2 MB") && !contiene(*e, "servidor"), "fondo de más de 2 MB: lo dice, sin «servidor»");

    e = errorDe(pagina, std::string("Título"), std::nullopt);
    CHECK(e && contiene(*e, "No pude hablar con el servidor"), "sin sala: el error dice que no se pudo hablar con el servidor");
    if (e) std::printf("   «%s»\n", e->c_str());

    std::printf(g_fallas == 0 ? "test_pagina_publico: OK\n" : "test_pagina_publico: %d fallo(s)\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
