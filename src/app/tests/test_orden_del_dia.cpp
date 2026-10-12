// test_orden_del_dia.cpp — puntos desde texto y CSV, estados, uso de la palabra y anexo del acta.
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "control/BusEventos.hpp"
#include "asamblea/OrdenDelDia.hpp"

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
    control::BusEventos bus;
    asamblea::OrdenDelDia od(dir.path().toStdString(), bus);
    int n = 0;
    CHECK(od.cargar("1. Expte. 123/24 – Ley de bosques\n2) Homenaje a los veteranos\n\n3 - Expediente Nº 77/25: Presupuesto", &n).empty() && n == 3,
          "tres puntos de texto");
    const json p = od.puntos();
    CHECK(p[0]["expediente"] == "123/24" && p[0]["titulo"] == "Ley de bosques", "expediente y título");
    CHECK(p[1]["expediente"] == "" && p[1]["titulo"] == "Homenaje a los veteranos", "sin expediente");
    CHECK(p[2]["n"] == 3 && p[2]["expediente"] == "77/25" && p[2]["titulo"] == "Presupuesto", "«Expediente Nº»");
    CHECK(od.cargar("n;expediente;titulo\n5;10/26;Otro\n6;;Varios", &n).empty() && n == 2 && od.puntos()[0]["n"] == 5,
          "CSV con encabezado");
    od.cargar("1. Uno\n2. Dos", &n);

    CHECK(od.siguiente().empty() && od.actual()["n"] == 1, "siguiente: el 1 en tratamiento");
    CHECK(bus.ultimo("sesion/punto")["valor"]["titulo"] == "Uno", "publicado en el bus");
    bus.publicar("oradores/al_aire", {{"nombre", "Ana Pérez"}, {"banca", "3"}});
    esperar(1300);
    CHECK(od.punto(2, "en_tratamiento", "").empty(), "el 2 en tratamiento");
    CHECK(od.puntos()[0]["estado"] == "votado", "el anterior pasa a votado");
    esperar(1200);
    bus.publicar("oradores/al_aire", json::object());
    const json tp = od.tiemposPalabra(json::array({{{"nombre", "Remoto"}, {"segundos", 40}}}));
    bool ana = false, remoto = false;
    for (const auto& o : tp) {
        if (o["nombre"] == "Ana Pérez" && o["segundos"] >= 2 && o["por_punto"].contains("1") && o["por_punto"].contains("2")) ana = true;
        if (o["nombre"] == "Remoto" && o["segundos"] == 40) remoto = true;
    }
    CHECK(ana, "el tiempo de Ana repartido entre los puntos 1 y 2");
    CHECK(remoto, "los remotos suman su tiempo");
    od.punto(2, "comision", "pasa a Asuntos Generales");
    const std::string a = od.anexoActa(json::array());
    CHECK(a.find("## Orden del día") != std::string::npos && a.find("a comisión") != std::string::npos &&
              a.find("Asuntos Generales") != std::string::npos && a.find("## Uso de la palabra") != std::string::npos,
          "anexo del acta");
    CHECK(!od.punto(9, "votado", "").empty(), "punto inexistente");
    CHECK(!od.punto(1, "raro", "").empty(), "estado inválido");
    {
        asamblea::OrdenDelDia otra(dir.path().toStdString(), bus);
        CHECK(otra.puntos().size() == 2, "queda guardado");
    }
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
