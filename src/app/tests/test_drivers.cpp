// src/app/tests/test_drivers.cpp — la prueba de drivers en un proceso hijo
// (dispositivos/Sondeo + SondaDrivers) y la cuarentena de punta a punta
// (dispositivos/Drivers), con drivers DE MENTIRA: uno que anda, uno que da
// error, uno que se cuelga, uno que se cae y uno que hace lo de JackRouter.
// Sin ASIO ni GStreamer: anda igual en los tres sistemas.
//
// El proceso hijo es este mismo test con --sonda-drivers (como la app).

#include "dispositivos/Drivers.hpp"
#include "dispositivos/SondaDrivers.hpp"
#include "dispositivos/Sondeo.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

using namespace ba::app::dispositivos;
namespace fs = std::filesystem;

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

bool esperar(const std::function<bool()>& listo, int ms) {
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms) {
        QEventLoop l;
        QTimer::singleShot(20, &l, &QEventLoop::quit);
        l.exec();
    }
    return listo();
}

fs::path carpetaTemporal() {
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path p = fs::temp_directory_path() / ("ba-test-drivers-" + std::to_string(t));
    fs::create_directories(p);
    return p;
}

int lineasCon(const fs::path& diario, const std::string& id) {
    std::ifstream in(diario);
    std::string l;
    int n = 0;
    while (std::getline(in, l)) n += l == id ? 1 : 0;
    return n;
}

Sondeo::Opciones opciones(const QString& falsos, const fs::path& diario, const QString& firma = "1") {
    Sondeo::Opciones o;
    o.programa = QCoreApplication::applicationFilePath();
    o.argsBase = {sonda::kBandera};
    o.limiteMs = 1500;
    o.arranqueMs = 10000;
    o.entorno = QProcessEnvironment::systemEnvironment();
    o.entorno.insert("BA_SONDA_FALSOS", falsos);
    o.entorno.insert("BA_SONDA_FALSOS_FIRMA", firma);
    o.entorno.insert("BA_SONDA_FALSOS_DIARIO", QString::fromStdString(diario.string()));
    return o;
}

void sondeoEnHijo(const fs::path& dir) {
    const fs::path diario = dir / "diario-sondeo.txt";
    Sondeo s(opciones("anda,error,cuelga,cae,jack", diario));

    bool listo = false;
    std::vector<ba::hw::DriverListado> lista;
    s.listar([&](bool ok, std::vector<ba::hw::DriverListado> l, QString) {
        listo = ok;
        lista = std::move(l);
    });
    CHECK(esperar([&] { return listo; }, 10000), "listar en el hijo");
    CHECK(lista.size() == 5 && lista[0].id == "falso:anda" && lista[0].firma == "1", "los cinco de mentira, con su firma");
    CHECK(!fs::exists(diario), "listar no carga ninguno");

    // El que se cuelga y el que se cae van en el medio: los que siguen se prueban igual.
    std::map<std::string, ResultadoSonda> r;
    bool fin = false;
    QString error;
    QElapsedTimer t;
    t.start();
    s.probar({"falso:cuelga", "falso:anda", "falso:cae", "falso:error", "falso:jack", "falso:noesta"},
             [&](const ResultadoSonda& x) { r[x.id] = x; },
             [&](QString e) {
                 fin = true;
                 error = e;
             });
    CHECK(esperar([&] { return fin; }, 30000), "la prueba termina aunque uno se cuelgue y otro se caiga");
    CHECK(error.isEmpty(), "sin error general");
    CHECK(r.size() == 6, "un resultado por driver");
    CHECK(!r["falso:cuelga"].ok && r["falso:cuelga"].falla == ba::hw::Falla::Tiempo, "el que se cuelga: tiempo agotado");
    CHECK(r["falso:cuelga"].motivo.find("se colgó") != std::string::npos, "con su motivo legible");
    CHECK(r["falso:anda"].ok && r["falso:anda"].detalle == "1 dispositivo de prueba", "el que anda, después del colgado: anda");
    CHECK(r["falso:anda"].dispositivos.size() == 1, "con sus dispositivos");
    CHECK(!r["falso:cae"].ok && r["falso:cae"].falla == ba::hw::Falla::Caida, "el que se cae: caída");
    CHECK(r["falso:cae"].motivo.find("me caigo") != std::string::npos, "con lo último que escribió");
    CHECK(!r["falso:error"].ok && r["falso:error"].falla == ba::hw::Falla::Error &&
              r["falso:error"].motivo == "no arrancó: el driver de prueba no arranca",
          "el que da error: error, con su mensaje");
    CHECK(r["falso:jack"].motivo == "no contesta: ¿está corriendo el servidor de JACK?",
          "el de JACK: el motivo sale de su stderr");
    CHECK(r["falso:noesta"].ausente && !r["falso:noesta"].ok, "uno que no existe: ausente (no es falla)");
    CHECK(lineasCon(diario, "falso:anda") == 1 && lineasCon(diario, "falso:cuelga") == 1, "cada uno se cargó una vez");
    CHECK(t.elapsed() < 20000, "no esperó de más");
    CHECK(!s.ocupado(), "quedó libre");

    // Un programa que no existe: error general, no se culpa a ningún driver.
    Sondeo::Opciones o = opciones("anda", diario);
    o.programa = QString::fromStdString((dir / "no-existe").string());
    Sondeo malo(o);
    bool fin2 = false;
    QString error2;
    int resultados = 0;
    malo.probar({"falso:anda"}, [&](const ResultadoSonda&) { ++resultados; },
                [&](QString e) {
                    fin2 = true;
                    error2 = e;
                });
    CHECK(esperar([&] { return fin2; }, 10000) && !error2.isEmpty() && resultados == 0,
          "sin sonda: avisa y no anota fallas");
}

Drivers::Opciones opcionesDrivers(const fs::path& archivo, const std::string& huella, const QString& falsos,
                                  const fs::path& diario, std::map<std::string, int>* rangos,
                                  const QString& firma = "1") {
    Drivers::Opciones o;
    o.archivo = archivo;
    o.huella = huella;
    o.etiqueta = "Máquina de prueba";
    o.umbral = 2; // para no lanzar cuatro rondas
    o.sondeo = opciones(falsos, diario, firma);
    o.ponerRango = [rangos](const std::string& p, int r) { (*rangos)[p] = r; };
    return o;
}

void arrancarYEsperar(Drivers& d) {
    bool listo = false;
    QObject::connect(&d, &Drivers::listoArranque, [&] { listo = true; });
    d.arrancar();
    esperar([&] { return listo; }, 30000);
}

void cuarentenaDePuntaAPunta(const fs::path& dir) {
    const fs::path archivo = dir / "Config" / "drivers.json";
    const fs::path diario = dir / "diario-drivers.txt";
    std::map<std::string, int> rangos;
    const QString falsos = "anda,cuelga,cae";
    {
        Drivers d(opcionesDrivers(archivo, "maqA", falsos, diario, &rangos));
        CHECK(!d.maquinaConocida(), "máquina nueva");
        arrancarYEsperar(d);
        CHECK(d.listo() && !d.revisando(), "terminó lo del arranque");
        CHECK(lineasCon(diario, "falso:anda") == 1 && lineasCon(diario, "falso:cuelga") == 1 &&
                  lineasCon(diario, "falso:cae") == 1,
              "máquina nueva: se probó todo");
        CHECK(d.uno("falso:anda").value("estado").toString() == "anda", "el que anda: anda");
        CHECK(d.uno("falso:cuelga").value("estado").toString() == "falla", "el que se cuelga: una falla (umbral 2)");
        CHECK(d.rowCount() == 3, "el modelo tiene los tres");
        CHECK(d.salud().maquinaConocida(), "el sondeo completo la vuelve conocida");
    }
    {
        // Máquina conocida: lo que anduvo NO se vuelve a probar; lo que venía fallando sí.
        Drivers d(opcionesDrivers(archivo, "maqA", falsos, diario, &rangos));
        CHECK(d.maquinaConocida(), "la misma máquina: conocida");
        bool quedo = false;
        QObject::connect(&d, &Drivers::cuarentenaNueva, [&](const QString& id, const QString&, const QString& motivo) {
            if (id == "falso:cuelga" && motivo.contains("se colgó")) quedo = true;
        });
        arrancarYEsperar(d);
        CHECK(lineasCon(diario, "falso:anda") == 1, "conocida: el que anduvo no se re-sondea");
        CHECK(lineasCon(diario, "falso:cuelga") == 2 && lineasCon(diario, "falso:cae") == 2,
              "conocida: los que venían fallando sí");
        CHECK(quedo && d.uno("falso:cuelga").value("estado").toString() == "cuarentena",
              "segunda falla seguida: cuarentena (y avisa)");
        CHECK(d.enCuarentena() == 2, "el que se cae también");
        CHECK(d.resumen().contains("2 en cuarentena"), "el resumen lo dice");
    }
    {
        // Tercer arranque: la cuarentena no se carga sola.
        Drivers d(opcionesDrivers(archivo, "maqA", falsos, diario, &rangos));
        arrancarYEsperar(d);
        CHECK(lineasCon(diario, "falso:cuelga") == 2 && lineasCon(diario, "falso:cae") == 2,
              "en cuarentena: no se vuelve a cargar al arrancar");
        // «Probar de nuevo»: se prueba una vez; sigue colgándose, sigue en cuarentena.
        CHECK(d.reintentar("falso:cuelga").isEmpty(), "probar de nuevo");
        CHECK(d.uno("falso:cuelga").value("estado").toString() == "probando", "se ve «probando»");
        CHECK(esperar([&] { return !d.revisando(); }, 15000), "terminó el reintento");
        CHECK(lineasCon(diario, "falso:cuelga") == 3, "se cargó una vez más");
        CHECK(d.uno("falso:cuelga").value("estado").toString() == "cuarentena", "sigue en cuarentena");
        CHECK(!d.reintentar("falso:noexiste").isEmpty(), "reintentar uno que no hay: error");
    }
    {
        // Otra máquina (el disco viajó): todo de nuevo, aunque en A esté en cuarentena.
        Drivers d(opcionesDrivers(archivo, "maqB", falsos, diario, &rangos));
        CHECK(!d.maquinaConocida(), "otra máquina: nueva");
        arrancarYEsperar(d);
        CHECK(lineasCon(diario, "falso:anda") == 2 && lineasCon(diario, "falso:cuelga") == 4,
              "máquina nueva: se prueba todo otra vez");
    }
    {
        // Una versión nueva del driver: se la prueba aunque esté en cuarentena.
        Drivers d(opcionesDrivers(archivo, "maqA", falsos, diario, &rangos, "2"));
        arrancarYEsperar(d);
        CHECK(lineasCon(diario, "falso:anda") == 3 && lineasCon(diario, "falso:cuelga") == 5,
              "otra firma (versión): se vuelve a probar todo lo que cambió");
    }
}

void rangosDeGStreamer(const fs::path& dir) {
    // Un proveedor de GStreamer que queda en cuarentena baja a NONE en el proceso; si
    // «Probar de nuevo» anda, vuelve a su rango. (Con ids de mentira «gst:…» que la sonda
    // de prueba no conoce: se los marca a mano en el registro de salud.)
    std::map<std::string, int> rangos;
    Drivers d(opcionesDrivers(dir / "rangos.json", "maqR", "", dir / "diario-rangos.txt", &rangos));
    d.salud().novedades({{"gst:colgado", "Colgado", "gstreamer", "1", 256}});
    d.salud().falla("gst:colgado", ba::hw::Falla::Tiempo, "x", "t");
    d.salud().falla("gst:colgado", ba::hw::Falla::Tiempo, "x", "t");
    arrancarYEsperar(d);
    CHECK(rangos.count("colgado") == 1 && rangos["colgado"] == 0, "en cuarentena: rango NONE en el proceso");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], sonda::kBandera) == 0)
        return sonda::principal(argc, argv, {sonda::fuenteDePrueba()});
    QCoreApplication app(argc, argv);
    const fs::path dir = carpetaTemporal();
    sondeoEnHijo(dir);
    cuarentenaDePuntaAPunta(dir);
    rangosDeGStreamer(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf(fallos == 0 ? "test_drivers: OK\n" : "test_drivers: %d FALLOS\n", fallos);
    return fallos == 0 ? 0 : 1;
}
