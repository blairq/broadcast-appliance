// test_hijos_app.cpp — los servidores que lanza la app mueren con ella (agent/HijosDeLaApp,
// platform/Huerfanos)
//
//   - parar() se lleva al hijo y a lo que él lanzó: su grupo de procesos y también lo que
//     lanzó DESPRENDIDO (su propia sesión: así lanza opencode sus MCP);
//   - barrerHuerfanos() (al arrancar la app; el launcher al arrancar y al cerrar) mata al
//     anotado que quedó andando de una corrida que murió sin pararlo, con su árbol, y borra
//     la anotación; si quien lo lanzó sigue vivo (el launcher readoptó a la app), no;
//   - un pid anotado que ahora es OTRO programa no se toca.
// POSIX: en Windows lo hace el Job Object (el sistema), no hay nada que barrer acá.

#include "agent/HijosDeLaApp.hpp"
#include "platform/Huerfanos.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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

#if !defined(_WIN32)
bool seFue(pid_t pid, int ms = 3000) {
    for (int i = 0; i < ms / 50 && ba::platform::huerfanos::vivo(pid); ++i) ::usleep(50'000);
    return !ba::platform::huerfanos::vivo(pid);
}
#endif
} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    std::printf("SKIP: en Windows los ata el Job Object\n");
    return 77;
#else
    // Ayudante: lanza un nieto desprendido (su propia sesión, como los MCP que lanza
    // opencode), dice su pid y espera.
    if (argc > 1 && std::string(argv[1]) == "--desprendido") {
        if (::fork() == 0) {
            ::setsid(); // dice su pid ya desprendido
            std::printf("%d\n", static_cast<int>(::getpid()));
            std::fflush(stdout);
            for (;;) ::pause();
        }
        for (;;) ::pause();
    }
    QCoreApplication app(argc, argv);
    QTemporaryDir datos;
    ::setenv(ba::platform::kDataRootEnv, datos.path().toLocal8Bit().constData(), 1);
    namespace fs = std::filesystem;
    namespace hijos = ba::app::agent::hijos;
    const fs::path anotado = fs::path(datos.path().toStdString()) / "Runtime" / "hijos" / "prueba.json";
    const QString yo = QCoreApplication::applicationFilePath();
    // Como si quien lo anotó hubiera muerto: el dueño, un pid que ya no vive.
    const auto sinDueno = [&anotado]() {
        QProcess t;
        t.start("/bin/sh", {"-c", "exit 0"});
        t.waitForStarted(3000);
        const std::int64_t muerto = t.processId();
        t.waitForFinished(3000);
        nlohmann::json j = nlohmann::json::parse(std::ifstream(anotado), nullptr, false);
        j["dueno"] = muerto;
        std::ofstream(anotado) << j.dump();
    };

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
        CHECK(seFue(nieto), "parar: y el nieto también (su grupo)");
        CHECK(!fs::exists(anotado), "parar: ya no está anotado");
    }
    // parar(): un nieto DESPRENDIDO (otra sesión: matar el grupo no lo alcanza) también.
    {
        QProcess p;
        hijos::preparar(p);
        p.start(yo, {"--desprendido"});
        CHECK(p.waitForStarted(5000), "arranca el que desprende");
        hijos::anotar(p, "prueba");
        CHECK(p.waitForReadyRead(5000), "dice el pid del desprendido");
        const pid_t nieto = static_cast<pid_t>(p.readLine().trimmed().toLongLong());
        CHECK(nieto > 1 && ::getsid(nieto) == nieto, "el nieto está en su propia sesión");
        hijos::parar(p, "prueba");
        CHECK(p.state() == QProcess::NotRunning, "parar: el hijo se fue");
        CHECK(seFue(nieto), "parar: y el desprendido también (su árbol)");
    }
    // barrerHuerfanos(): uno anotado que sigue vivo y cuyo dueño murió, con un desprendido.
    {
        QProcess p;
        hijos::preparar(p);
        p.start(yo, {"--desprendido"});
        CHECK(p.waitForStarted(5000), "arranca el huérfano");
        hijos::anotar(p, "prueba");
        CHECK(p.waitForReadyRead(5000), "dice el pid de su desprendido");
        const pid_t nieto = static_cast<pid_t>(p.readLine().trimmed().toLongLong());
        hijos::barrerHuerfanos();
        CHECK(p.state() == QProcess::Running && fs::exists(anotado), "barrer: con el dueño vivo, no se toca");
        sinDueno();
        hijos::barrerHuerfanos();
        CHECK(p.waitForFinished(3000), "barrer: el huérfano se fue");
        CHECK(seFue(nieto), "barrer: y su desprendido también");
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
