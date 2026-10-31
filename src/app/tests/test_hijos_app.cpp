// test_hijos_app.cpp — los servidores que lanza la app mueren con ella (agent/HijosDeLaApp)
//
//   - parar() se lleva al hijo y a lo que él lanzó (su grupo de procesos);
//   - barrerHuerfanos() (al arrancar la app) mata al anotado que quedó andando de una
//     corrida que murió sin pararlo, y borra la anotación;
//   - un pid anotado que ahora es OTRO programa no se toca.
// POSIX: en Windows lo hace el Job Object (el sistema), no hay nada que barrer acá.

#include "agent/HijosDeLaApp.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#if !defined(_WIN32)
#include <csignal>
#include <unistd.h>
#endif

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__); \
            ++fallos;                                                        \
        }                                                                    \
    } while (0)
} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    std::printf("SKIP: en Windows los ata el Job Object\n");
    return 77;
#else
    QCoreApplication app(argc, argv);
    QTemporaryDir datos;
    ::setenv(ba::platform::kDataRootEnv, datos.path().toLocal8Bit().constData(), 1);
    namespace fs = std::filesystem;
    namespace hijos = ba::app::agent::hijos;
    const fs::path anotado = fs::path(datos.path().toStdString()) / "Runtime" / "hijos" / "prueba.json";

    // parar(): el hijo y su nieto (un sleep que lanza el shell), los dos.
    {
        QProcess p;
        hijos::preparar(p);
        p.start("/bin/sh", {"-c", "sleep 30 & echo $!; wait"});
        CHECK(p.waitForStarted(5000), "arranca");
        hijos::anotar(p, "prueba");
        CHECK(fs::exists(anotado), "queda anotado");
        CHECK(p.waitForReadyRead(5000), "dice el pid del nieto");
        const pid_t nieto = static_cast<pid_t>(p.readLine().trimmed().toLongLong());
        CHECK(nieto > 1 && ::kill(nieto, 0) == 0, "el nieto anda");
        hijos::parar(p, "prueba");
        CHECK(p.state() == QProcess::NotRunning, "parar: el hijo se fue");
        bool nietoVivo = true;
        for (int i = 0; i < 40 && nietoVivo; ++i) {
            nietoVivo = ::kill(nieto, 0) == 0;
            if (nietoVivo) ::usleep(50'000);
        }
        CHECK(!nietoVivo, "parar: y el nieto también (su grupo)");
        CHECK(!fs::exists(anotado), "parar: ya no está anotado");
    }
    // barrerHuerfanos(): uno anotado que sigue vivo (como si la app hubiera muerto sin pararlo).
    {
        QProcess p;
        hijos::preparar(p);
        p.start("/bin/sleep", {"30"});
        CHECK(p.waitForStarted(5000), "arranca el huérfano");
        hijos::anotar(p, "prueba");
        hijos::barrerHuerfanos();
        CHECK(p.waitForFinished(3000), "barrer: el huérfano se fue");
        CHECK(!fs::exists(anotado), "barrer: borra la anotación");
    }
    // Un pid anotado que ahora es otro programa (este test): no se toca.
    {
        fs::create_directories(anotado.parent_path());
        std::ofstream(anotado) << "{\"pid\": " << ::getpid() << ", \"exe\": \"/bin/sleep\"}";
        hijos::barrerHuerfanos();
        CHECK(::kill(::getpid(), 0) == 0, "barrer: un pid de otro programa queda (y este test sigue)");
        CHECK(!fs::exists(anotado), "barrer: la anotación vieja igual se borra");
    }
    std::printf("== test_hijos_app: %s ==\n", fallos == 0 ? "OK" : "FALLOS");
    return fallos == 0 ? 0 : 1;
#endif
}
