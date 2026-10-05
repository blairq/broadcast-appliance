// test_seguir_microfono.cpp — un micrófono de banca que se abre propone su banca.
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "control/BusEventos.hpp"
#include "legislatura/Bancas.hpp"
#include "legislatura/Cuerpo.hpp"
#include "legislatura/SeguirMicrofono.hpp"

using namespace ba::app;
using json = nlohmann::json;

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
    const std::filesystem::path d = dir.path().toStdString();
    control::BusEventos bus;
    legislatura::Cuerpo cuerpo(d / "cuerpo.json");
    std::string err;
    cuerpo.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}, {"microfono", {{"fuente", "Mic 3"}}}}, &err);
    cuerpo.guardar({{"nombre", "Juan Gómez"}, {"banca", "7"}, {"microfono", {{"fuente", "Mic 7"}}}}, &err);
    std::vector<std::pair<std::string, bool>> mics;
    legislatura::Bancas::Ganchos g;
    g.microfono = [&](const std::string& f, bool abierto) { mics.emplace_back(f, abierto); };
    legislatura::Bancas bancas(cuerpo, g);
    legislatura::SeguirMicrofono seg(d, bus, cuerpo, bancas);

    bus.publicar("audio/nivel/Mic 3", {{"pico_db", -12.0}, {"mudo", false}});
    esperar(1200);
    CHECK(seg.estado()["propuesta"].is_null(), "apagado no propone nada");

    seg.configurar({{"activo", true}, {"sostener_ms", 300}, {"soltar_ms", 400}});
    esperar(200);
    CHECK(seg.estado()["propuesta"].is_null(), "todavía no pasó el tiempo de sostener");
    esperar(400);
    const json p = seg.estado()["propuesta"];
    CHECK(p.is_object() && p.value("banca", std::string()) == "3", "propone la banca del micrófono abierto");
    CHECK(bus.ultimo("oradores/propuesta")["valor"].value("nombre", std::string()) == "Ana Pérez", "y lo publica en el bus");

    bus.publicar("audio/nivel/Mic 7", {{"pico_db", -50.0}, {"mudo", false}});
    CHECK(seg.aceptar().empty(), "aceptar");
    CHECK(bancas.oradorParaZocalo().value("banca", std::string()) == "3", "la banca 3 quedó al aire");
    esperar(300);
    CHECK(seg.estado()["propuesta"].is_null(), "no se vuelve a proponer la que ya está al aire");

    // Se cierra el 3 y se abre el 7.
    bus.publicar("audio/nivel/Mic 3", {{"pico_db", -60.0}, {"mudo", false}});
    bus.publicar("audio/nivel/Mic 7", {{"pico_db", -8.0}, {"mudo", false}});
    esperar(700);
    CHECK(seg.estado()["propuesta"].value("banca", std::string()) == "7", "el que pide la palabra después");
    seg.descartar();
    esperar(300);
    CHECK(seg.estado()["propuesta"].is_null(), "descartada no vuelve mientras siga abierto");

    // Mudo no cuenta.
    seg.configurar({{"modo", "automatico"}});
    bus.publicar("audio/nivel/Mic 7", {{"pico_db", -60.0}, {"mudo", false}});
    esperar(600);
    bus.publicar("audio/nivel/Mic 7", {{"pico_db", -8.0}, {"mudo", true}});
    esperar(600);
    CHECK(bancas.oradorParaZocalo().value("banca", std::string()) == "3", "un micrófono muteado no cuenta");
    bus.publicar("audio/nivel/Mic 7", {{"pico_db", -9.0}, {"mudo", false}});
    esperar(700);
    CHECK(bancas.oradorParaZocalo().value("banca", std::string()) == "7", "automático: la arma sola");

    // El sistema de conferencia del recinto como fuente.
    bancas.terminar();
    seg.configurar({{"modo", "proponer"}, {"fuente", "conferencia"}});
    bus.publicar("plugin/conferencia-recinto/conferencia/microfono", {{"banca", "3"}, {"abierto", true}}, true);
    esperar(300);
    CHECK(seg.estado()["propuesta"].value("banca", std::string()) == "3" &&
              seg.estado()["propuesta"].value("nombre", std::string()) == "Ana Pérez",
          "conferencia: propone la banca que abrió su micrófono");
    bus.publicar("plugin/conferencia-recinto/conferencia/microfono", {{"banca", "3"}, {"abierto", false}}, true);
    esperar(300);
    CHECK(seg.estado()["propuesta"].is_null(), "conferencia: al cerrar se va la propuesta");
    CHECK(!seg.configurar({{"fuente", "otra"}}).empty(), "fuente inválida");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
