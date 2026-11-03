// test_archivos_prueba.cpp — el disco y el probador de las pruebas de sala y de asamblea
// (sala/ArchivosPrueba): carpetas, credenciales solo para el dueño, el informe de hinchada,
// borrar, lo que quedó de antes por la cola, y cortar el probador sin esperarlo (sin que
// quede zombi ni huérfano).

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#endif

#include "persist/ColaEscritura.hpp"
#include "sala/ArchivosPrueba.hpp"

namespace fs = std::filesystem;
namespace ar = ba::app::sala::archivos;
using json = nlohmann::json;

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
    const fs::path base = fs::temp_directory_path() / ("ba_test_archivos_prueba_" + std::to_string(QCoreApplication::applicationPid()));
    fs::remove_all(base);

    // Carpetas.
    CHECK(ar::crearCarpetas({base / "a" / "b", base / "grabado", fs::path()}).empty(), "crea las carpetas (y las de arriba)");
    CHECK(fs::is_directory(base / "a" / "b") && fs::is_directory(base / "grabado"), "están");
    {
        std::ofstream(base / "archivo") << "x";
        CHECK(!ar::crearCarpetas({base / "archivo" / "adentro"}).empty(), "donde no se puede: el error");
    }

    // Credenciales: con su carpeta, solo para el dueño.
    const fs::path cred = base / "prueba-asamblea-1" / "integrantes.json";
    CHECK(ar::escribirCredenciales(cred, R"({"integrantes":[]})").empty(), "escribe las credenciales");
#ifndef _WIN32
    struct stat st {};
    CHECK(::stat(cred.c_str(), &st) == 0 && (st.st_mode & 0077) == 0, "solo las lee el dueño (0600)");
#endif
    ar::borrar(cred);
    CHECK(!fs::exists(cred), "borrar");
    ar::borrar(cred);
    ar::borrar(fs::path());
    CHECK(true, "borrar lo que no está (o nada): sin error");

    // El informe de hinchada.
    CHECK(ar::informeHinchada(base / "nada").empty(), "sin informe: objeto vacío");
    CHECK(ar::informeHinchada(fs::path()).empty(), "sin carpeta: objeto vacío");
    ar::crearCarpetas({base / "salida" / "hinchada"});
    std::ofstream(base / "salida" / "hinchada" / "informe.json") << "no es json";
    CHECK(ar::informeHinchada(base / "salida").empty(), "un informe roto: objeto vacío");
    std::ofstream(base / "salida" / "hinchada" / "informe.json")
        << R"({"asamblea":{"entraron":4},"integrantes":[{"id":"p1"}],"resumen":{"ms":3},"otro":1})";
    const json h = ar::informeHinchada(base / "salida");
    CHECK(h.value("asamblea", json::object()).value("entraron", 0) == 4 && h["integrantes"].size() == 1 &&
              h.contains("resumen") && h["notas"].is_array() && !h.contains("otro") &&
              h.value("carpeta", std::string()) == (base / "salida" / "hinchada").string(),
          "el informe: lo que va al de la app");

    // Lo que quedó de antes, por la cola (lo encolado se ve aunque no esté en el disco).
    const fs::path enCurso = base / "Runtime" / "prueba-en-curso.json";
    CHECK(ar::leerJsonEncolado(enCurso).is_null(), "sin archivo: null");
    ba::persist::cola().escribir(enCurso, R"({"inicioMs":5,"archivo":"x"})");
    CHECK(ar::leerJsonEncolado(enCurso).value("inicioMs", 0) == 5, "lo encolado");
    ba::persist::cola().borrar(enCurso);
    CHECK(ar::leerJsonEncolado(enCurso).is_null(), "borrado encolado: null");
    ba::persist::cola().vaciar();

#ifndef _WIN32
    // El probador: se corta sin esperarlo, y no queda ni zombi ni huérfano.
    {
        auto* dueno = new QObject;
        auto* p = new QProcess(dueno);
        bool avisoAlDueno = false;
        QObject::connect(p, &QProcess::finished, dueno, [&]() { avisoAlDueno = true; });
        p->start("/bin/sleep", {"30"});
        CHECK(p->waitForStarted(5000), "arrancó el hijo");
        const qint64 pid = p->processId();
        QPointer<QProcess> vivo(p);
        QElapsedTimer t;
        t.start();
        ar::soltarProceso(p, dueno);
        delete dueno; // el dueño se va (como el modelo al cerrar el programa)
        const qint64 ms = t.elapsed();
        CHECK(ms < 200, "soltar + destruir al dueño: sin esperar al hijo");
        CHECK(vivo, "el QProcess no se destruyó con el dueño (~QProcess esperaría)");
        // Con el bucle de eventos andando: el hijo termina, se recoge y el QProcess se borra solo.
        QElapsedTimer espera;
        espera.start();
        while (espera.elapsed() < 5000 && (vivo || ::kill(static_cast<pid_t>(pid), 0) == 0)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        CHECK(!vivo, "el QProcess se borró solo al terminar");
        CHECK(::kill(static_cast<pid_t>(pid), 0) != 0, "el hijo no existe más (ni zombi)");
        CHECK(!avisoAlDueno, "al dueño no le llegó nada después de soltarlo");
    }
#endif
    ar::soltarProceso(nullptr, nullptr);

    fs::remove_all(base);
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
