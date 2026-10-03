// test_platform.cpp
//
// Test de CONTRATO de `ba::platform`: el mismo archivo, sin cambios, tiene que
// pasar en Windows, Linux y macOS. Es lo que garantiza que las tres
// implementaciones se comportan igual y que la logica de arriba (launcher,
// gateway, app) puede ignorar el sistema.
//
// El binario se relanza a si mismo como hijo (`--child <modo> ...`) para
// probar procesos y sockets cruzando un limite de proceso real, sin depender
// de utilidades del sistema (sleep, cmd.exe, sh).
//
// Casos:
//   1. Paths: bundleRootFor (funcion pura) para los layouts de Windows/Linux y
//      del .app de macOS; executablePath apunta a este binario; runtimeDir
//      existe (y es 0700 en POSIX).
//   2. Process: codigo de salida; ejecutable inexistente -> spawn falla con
//      motivo; entorno (envSet/envRemove) y cwd; stdout/stderr a un archivo
//      (agregando); wait con timeout; kill;
//      destruir el Process mata al hijo; kill alcanza a los nietos; si el
//      padre muere de golpe, el nieto con killWithParent muere tambien.
//   3. LocalSocket: eco entre procesos en bloques (lecturas parciales); una
//      escritura mas grande que el buffer del SO; EOF cuando el otro lado sale;
//      el nombre ocupado por un servidor vivo no se puede tomar; un servidor
//      que murio sin cerrar no bloquea el nombre; timeout de connect;
//      ControlEndpoint de punta a punta sobre el socket.
//   4. SingleInstance: exclusion dentro del proceso y entre procesos.

#include "ipc/ControlProtocol.hpp"
#include "platform/Herramientas.hpp"
#include "platform/LocalSocket.hpp"
#include "platform/Paths.hpp"
#include "platform/Process.hpp"
#include "platform/SingleInstance.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ba::platform;
using namespace std::chrono_literals;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            std::fflush(stdout);                                                                             \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

int currentPid() {
#if defined(_WIN32)
    return _getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

/// true si existe un proceso vivo con ese PID.
bool pidAlive(std::uint32_t pid) {
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    if (::kill(static_cast<pid_t>(pid), 0) != 0) {
        return false;
    }
#if defined(__linux__)
    // Un zombie ya murio: solo espera que alguien lo recoja. Un nieto huerfano
    // lo recoge el PID 1, que en un contenedor (tail, sh...) puede no hacerlo.
    std::ifstream st("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (std::getline(st, line)) {
        const auto rp = line.rfind(')');
        if (rp != std::string::npos && rp + 2 < line.size() && line[rp + 2] == 'Z') {
            return false;
        }
    }
#endif
    return true;
#endif
}

/// Espera hasta `ms` a que el PID deje de existir.
bool waitPidGone(std::uint32_t pid, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!pidAlive(pid)) {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return !pidAlive(pid);
}

/// Lee un PID escrito por un hijo en `path` (espera hasta que aparezca).
std::uint32_t readPidFile(const std::filesystem::path& path, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream in(path);
        unsigned long v = 0;
        if (in >> v && v != 0) {
            return static_cast<std::uint32_t>(v);
        }
        std::this_thread::sleep_for(20ms);
    }
    return 0;
}

void writePidFile(const std::string& path, std::uint32_t pid) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp);
        out << pid << "\n";
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
}

ProcessOptions childOpts(const std::vector<std::string>& args) {
    ProcessOptions o;
    o.exe = executablePath();
    o.args = {"--child"};
    o.args.insert(o.args.end(), args.begin(), args.end());
    return o;
}

std::string uniqueName(const char* tag) {
    return std::string("t.") + tag + "." + std::to_string(currentPid());
}

std::filesystem::path scratch(const std::string& file) { return runtimeDir() / file; }

// ============================================================================
// Modos hijo
// ============================================================================

[[noreturn]] void sleepForever() {
    for (;;) {
        std::this_thread::sleep_for(1s);
    }
}

int childMain(int argc, char** argv) {
    const std::string mode = argv[2];
    auto arg = [&](int i) { return i < argc ? std::string(argv[i]) : std::string(); };

    if (mode == "exit") {
        return std::atoi(argv[3]);
    }
    if (mode == "sleep") {
        // Hijo "propio": se cae si el padre muere (en macOS es la unica via).
        exitWhenParentDies();
        sleepForever();
    }
    if (mode == "env") {
        const char* set = std::getenv("BA_TEST_SET");
        const char* removed = std::getenv("BA_TEST_REMOVED");
        const bool ok = set != nullptr && std::string(set) == "hola mundo" && removed == nullptr;
        return ok ? 0 : 1;
    }
    if (mode == "print") {
        std::fprintf(stdout, "salida-%s\n", arg(3).c_str());
        std::fprintf(stderr, "error-%s\n", arg(3).c_str());
        return 0;
    }
    if (mode == "cwd") {
        std::error_code ec;
        const bool ok = std::filesystem::equivalent(std::filesystem::current_path(), arg(3), ec);
        return ok ? 0 : 1;
    }
    if (mode == "args") {
        // Argumentos con espacios, comillas y barras: el quoting de Windows es
        // la parte delicada.
        const bool ok = arg(3) == "con espacio" && arg(4) == "com\"illa" && arg(5) == "barra\\" &&
                        arg(6).empty() && argc == 7;
        return ok ? 0 : 1;
    }
    if (mode == "grandparent" || mode == "orphan") {
        // Lanza un nieto "sleep", publica su PID y, segun el modo, se queda
        // dormido (grandparent) o muere de golpe sin limpiar (orphan).
        Process grandchild;
        std::string err;
        if (!Process::spawn(childOpts({"sleep"}), grandchild, &err)) {
            std::printf("hijo: no pudo lanzar el nieto: %s\n", err.c_str());
            return 2;
        }
        writePidFile(arg(3), grandchild.pid());
        if (mode == "orphan") {
            std::fflush(stdout);
            std::_Exit(0); // sin destructores: el nieto queda huerfano
        }
        sleepForever();
    }
    if (mode == "echo") {
        LocalStream s;
        std::string err;
        if (!LocalStream::connect(arg(3), 5000, s, &err)) {
            std::printf("echo: connect: %s\n", err.c_str());
            return 2;
        }
        std::uint8_t buf[8192];
        for (;;) {
            s.waitReadable(-1);
            const int n = s.read(buf, sizeof(buf));
            if (n < 0) {
                return 0; // el padre cerro
            }
            if (n > 0 && !s.write(buf, static_cast<std::size_t>(n))) {
                return 3;
            }
        }
    }
    if (mode == "sink") {
        // Lee N bytes (sin responder hasta el final) y devuelve la suma.
        LocalStream s;
        if (!LocalStream::connect(arg(3), 5000, s)) {
            return 2;
        }
        const std::size_t want = static_cast<std::size_t>(std::atoll(argv[4]));
        std::size_t got = 0;
        std::uint64_t sum = 0;
        std::vector<std::uint8_t> buf(16384);
        while (got < want) {
            s.waitReadable(-1);
            const int n = s.read(buf.data(), buf.size());
            if (n < 0) {
                return 3;
            }
            for (int i = 0; i < n; ++i) {
                sum += buf[static_cast<std::size_t>(i)];
            }
            got += static_cast<std::size_t>(n);
        }
        return s.write(reinterpret_cast<const std::uint8_t*>(&sum), sizeof(sum)) ? 0 : 4;
    }
    if (mode == "listen-and-die") {
        LocalServer server;
        if (!LocalServer::listen(arg(3), server)) {
            return 2;
        }
        std::fflush(stdout);
        std::_Exit(0); // sin close(): deja el socket huerfano
    }
    if (mode == "try-lock") {
        SingleInstance lock;
        return SingleInstance::acquire(arg(3), lock) ? 0 : 3;
    }
    std::printf("modo hijo desconocido: %s\n", mode.c_str());
    return 99;
}

// ============================================================================
// 1) Paths
// ============================================================================

// Las herramientas de medios: primeras en el PATH, una sola vez, con sus variables.
void testHerramientas() {
    namespace fs = std::filesystem;
#ifndef _WIN32
    const fs::path dir = fs::temp_directory_path() / ("ba-herr-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    { std::ofstream(dir / "ffmpeg") << "#!/bin/sh\n"; }
    setenv("BA_HERRAMIENTAS_RAIZ", dir.string().c_str(), 1);
    setenv("PATH", "/usr/bin:/bin", 1);
    CHECK(herramientasDir() == dir);
    CHECK(herramienta("ffmpeg") == dir / "ffmpeg");
    CHECK(herramienta("no-existe").empty());
    CHECK(prepararHerramientas());
    const std::string path = std::getenv("PATH");
    CHECK(path.rfind(dir.string() + ":", 0) == 0);
    CHECK(path.find("/usr/bin:/bin") != std::string::npos);
    CHECK(std::string(std::getenv("BA_FFMPEG")) == (dir / "ffmpeg").string());
    CHECK(std::string(std::getenv("BA_HERRAMIENTAS")) == dir.string());
    prepararHerramientas(); // no se acumula
    CHECK(std::string(std::getenv("PATH")) == path);
    unsetenv("BA_HERRAMIENTAS_RAIZ");
    fs::remove_all(dir);
#endif
}

void testPaths() {
    namespace fs = std::filesystem;
    CHECK(bundleRootFor(fs::path("/opt/ba/bin/64bit")) == fs::path("/opt/ba"));
    CHECK(bundleRootFor(fs::path("/opt/ba/bin/64bit/")) == fs::path("/opt/ba"));
    CHECK(bundleRootFor(fs::path("/opt/ba")) == fs::path("/opt/ba"));
    CHECK(bundleRootFor(fs::path("/Applications/BA.app/Contents/MacOS")) ==
          fs::path("/Applications/BA.app/Contents/Resources"));
    CHECK(bundleRootFor(fs::path("/Applications/BA.app/Contents/Resources/bin/64bit")) ==
          fs::path("/Applications/BA.app/Contents/Resources"));
    CHECK(bundleRootFor(fs::path("/x/64bit")) == fs::path("/x/64bit")); // sin bin/: no es el layout
    CHECK(bundleRootFor(fs::path()).empty());
    // Los datos: en macOS al lado de la .app; en el resto, la misma carpeta.
    CHECK(dataRootFor(fs::path("/Users/x/BA/BroadcastAppliance.app/Contents/Resources")) ==
          fs::path("/Users/x/BA/BroadcastAppliance Datos"));
    CHECK(dataRootFor(fs::path("/Users/x/BA/BroadcastAppliance.app/Contents/Resources/")) ==
          fs::path("/Users/x/BA/BroadcastAppliance Datos"));
    CHECK(dataRootFor(fs::path("/opt/ba")) == fs::path("/opt/ba"));
    CHECK(dataRootFor(fs::path("/x/Contents/Resources")) == fs::path("/x/Contents/Resources")); // sin .app
    CHECK(dataRootFor(fs::path()).empty());

    const fs::path exe = executablePath();
    CHECK(!exe.empty());
    CHECK(exe.is_absolute());
    CHECK(fs::exists(exe));
    CHECK(exe.stem().string().rfind("test_platform", 0) == 0);
    CHECK(executableDir() == exe.parent_path());

    const fs::path rt = runtimeDir();
    CHECK(!rt.empty());
    CHECK(fs::is_directory(rt));
#if !defined(_WIN32)
    struct stat st {};
    CHECK(::stat(rt.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);
#endif
}

// ============================================================================
// 2) Process
// ============================================================================

void testProcessExitCode() {
    Process p;
    std::string err;
    CHECK(Process::spawn(childOpts({"exit", "7"}), p, &err));
    CHECK(p.pid() != 0);
    const auto code = p.wait(10000);
    CHECK(code.has_value() && *code == 7);
    CHECK(!p.running());
    CHECK(!p.valid());
}

void testProcessMissingExe() {
    Process p;
    ProcessOptions o;
    o.exe = executableDir() / "no-existe-este-binario";
    std::string err;
    CHECK(!Process::spawn(o, p, &err));
    CHECK(!err.empty());
    CHECK(p.pid() == 0);
}

void testProcessEnvAndCwd() {
    // Variable a quitar presente en el padre: el hijo NO debe verla.
#if defined(_WIN32)
    _putenv_s("BA_TEST_REMOVED", "x");
#else
    ::setenv("BA_TEST_REMOVED", "x", 1);
#endif
    ProcessOptions o = childOpts({"env"});
    o.envSet["BA_TEST_SET"] = "hola mundo";
    o.envRemove.push_back("BA_TEST_REMOVED");
    Process p;
    CHECK(Process::spawn(o, p));
    auto code = p.wait(10000);
    CHECK(code.has_value() && *code == 0);

    const std::filesystem::path dir = runtimeDir();
    ProcessOptions c = childOpts({"cwd", dir.string()});
    c.workDir = dir;
    Process q;
    CHECK(Process::spawn(c, q));
    code = q.wait(10000);
    CHECK(code.has_value() && *code == 0);

    Process a;
    CHECK(Process::spawn(childOpts({"args", "con espacio", "com\"illa", "barra\\", ""}), a));
    code = a.wait(10000);
    CHECK(code.has_value() && *code == 0);
}

void testProcessOutputFile() {
    const std::filesystem::path out = scratch(uniqueName("out") + ".log");
    std::filesystem::remove(out);
    for (const char* run : {"uno", "dos"}) {
        ProcessOptions o = childOpts({"print", run});
        o.outputFile = out;
        Process p;
        CHECK(Process::spawn(o, p));
        const auto code = p.wait(10000);
        CHECK(code.has_value() && *code == 0);
    }
    std::ifstream in(out);
    const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // stdout y stderr al mismo archivo, y la segunda corrida AGREGA.
    CHECK(all.find("salida-uno") != std::string::npos);
    CHECK(all.find("error-uno") != std::string::npos);
    CHECK(all.find("salida-dos") != std::string::npos);
    CHECK(all.find("error-dos") != std::string::npos);
    in.close();
    std::filesystem::remove(out);
}

void testProcessWaitAndKill() {
    Process p;
    CHECK(Process::spawn(childOpts({"sleep"}), p));
    CHECK(!p.wait(100).has_value()); // timeout: sigue vivo
    CHECK(p.running());
    CHECK(p.kill());
    const auto code = p.wait(10000);
    CHECK(code.has_value());
    CHECK(code.has_value() && *code != 0);
    CHECK(!p.running());
}

void testProcessDestructorKills() {
    std::uint32_t pid = 0;
    {
        Process p;
        CHECK(Process::spawn(childOpts({"sleep"}), p));
        pid = p.pid();
        CHECK(pidAlive(pid));
    }
    CHECK(waitPidGone(pid, 5000));
}

void testProcessKillReachesGrandchildren() {
    const std::filesystem::path pidFile = scratch(uniqueName("gc") + ".pid");
    std::filesystem::remove(pidFile);
    Process p;
    CHECK(Process::spawn(childOpts({"grandparent", pidFile.string()}), p));
    const std::uint32_t gc = readPidFile(pidFile, 10000);
    CHECK(gc != 0);
    CHECK(pidAlive(gc));
    CHECK(p.kill());
    CHECK(p.wait(10000).has_value());
    CHECK(waitPidGone(gc, 5000));
    std::filesystem::remove(pidFile);
}

void testProcessParentDeath() {
    // El hijo lanza un nieto y muere con _Exit (sin destructores). El nieto
    // tiene que morir igual: Job Object (Windows), PDEATHSIG (Linux) o
    // exitWhenParentDies (macOS).
    const std::filesystem::path pidFile = scratch(uniqueName("orphan") + ".pid");
    std::filesystem::remove(pidFile);
    Process p;
    CHECK(Process::spawn(childOpts({"orphan", pidFile.string()}), p));
    const std::uint32_t gc = readPidFile(pidFile, 10000);
    CHECK(gc != 0);
    const auto code = p.wait(10000);
    CHECK(code.has_value() && *code == 0);
    CHECK(waitPidGone(gc, 5000));
    std::filesystem::remove(pidFile);
}

// ============================================================================
// 3) LocalSocket
// ============================================================================

/// Lee exactamente `len` bytes (con timeout total).
bool readExact(LocalStream& s, std::uint8_t* buf, std::size_t len, int ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    std::size_t got = 0;
    while (got < len) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        if (!s.waitReadable(100)) {
            continue;
        }
        const int n = s.read(buf + got, len - got);
        if (n < 0) {
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

void testSocketEcho() {
    const std::string name = uniqueName("echo");
    LocalServer server;
    std::string err;
    CHECK(LocalServer::listen(name, server, &err));
    Process child;
    CHECK(Process::spawn(childOpts({"echo", name}), child));
    LocalStream s;
    CHECK(server.accept(10000, s, &err));

    // Sin datos todavia: read devuelve 0 (no EOF).
    std::uint8_t probe[4];
    CHECK(s.read(probe, sizeof(probe)) == 0);

    std::vector<std::uint8_t> out(4096);
    std::vector<std::uint8_t> in(4096);
    bool allOk = true;
    for (int round = 0; round < 16; ++round) {
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::uint8_t>((i * 31 + static_cast<std::size_t>(round)) & 0xFF);
        }
        allOk = allOk && s.write(out.data(), out.size());
        allOk = allOk && readExact(s, in.data(), in.size(), 10000);
        allOk = allOk && in == out;
    }
    CHECK(allOk);

    // Cerrar nuestro lado: el hijo ve EOF y sale con 0.
    s.close();
    const auto code = child.wait(10000);
    CHECK(code.has_value() && *code == 0);
}

void testSocketLargeWrite() {
    const std::string name = uniqueName("sink");
    LocalServer server;
    CHECK(LocalServer::listen(name, server));
    const std::size_t total = 1024 * 1024; // mucho mas que el buffer del SO
    Process child;
    CHECK(Process::spawn(childOpts({"sink", name, std::to_string(total)}), child));
    LocalStream s;
    CHECK(server.accept(10000, s));
    std::vector<std::uint8_t> big(total);
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < total; ++i) {
        big[i] = static_cast<std::uint8_t>(i * 7);
        sum += big[i];
    }
    CHECK(s.write(big.data(), big.size())); // bloquea mientras el hijo drena
    std::uint64_t got = 0;
    CHECK(readExact(s, reinterpret_cast<std::uint8_t*>(&got), sizeof(got), 10000));
    CHECK(got == sum);
    const auto code = child.wait(10000);
    CHECK(code.has_value() && *code == 0);

    // El hijo salio: el proximo read informa cierre (<0), no "sin datos".
    std::uint8_t b = 0;
    CHECK(s.waitReadable(5000));
    CHECK(s.read(&b, 1) < 0);
}

// Wine no respeta FILE_FLAG_FIRST_PIPE_INSTANCE (Windows si): bajo Wine (CI
// de Windows desde Linux) la prueba de "nombre ocupado" no aplica.
bool underWine() {
#ifdef _WIN32
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
    return false;
#endif
}

void testSocketNameInUseAndStale() {
    const std::string name = uniqueName("busy");
    LocalServer a;
    CHECK(LocalServer::listen(name, a));
    LocalServer b;
    std::string err;
    if (!underWine()) {
        CHECK(!LocalServer::listen(name, b, &err)); // ocupado por un servidor vivo
        CHECK(!err.empty());
    }
    b.close();
    a.close();
    CHECK(LocalServer::listen(name, b, &err)); // liberado
    b.close();

    // Un servidor que muere sin cerrar no debe dejar el nombre bloqueado.
    const std::string stale = uniqueName("stale");
    Process child;
    CHECK(Process::spawn(childOpts({"listen-and-die", stale}), child));
    const auto code = child.wait(10000);
    CHECK(code.has_value() && *code == 0);
    LocalServer c;
    CHECK(LocalServer::listen(stale, c, &err));
}

void testSocketConnectTimeout() {
    LocalStream s;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!LocalStream::connect(uniqueName("nadie"), 200, s, &err));
    const auto dt = std::chrono::steady_clock::now() - t0;
    CHECK(dt >= 150ms);
    CHECK(dt < 5s);
    CHECK(!err.empty());
    CHECK(!LocalStream::connect("nombre/invalido", 0, s, &err));
}

void testControlEndpointOverSocket() {
    const std::string name = uniqueName("ctl");
    LocalServer server;
    CHECK(LocalServer::listen(name, server));
    LocalStream clientStream;
    std::thread t([&]() { LocalStream::connect(name, 5000, clientStream); });
    LocalStream serverStream;
    CHECK(server.accept(5000, serverStream));
    t.join();
    CHECK(clientStream.isOpen());

    ba::ipc::ControlEndpoint launcher(clientStream, ba::ipc::ControlEndpoint::kLauncher);
    ba::ipc::ControlEndpoint gateway(serverStream, ba::ipc::ControlEndpoint::kGateway);
    ba::ipc::Message hello;
    hello.type = ba::ipc::Type::Hello;
    hello.payload = R"({"device":"launcher"})";
    CHECK(launcher.sendRequest(hello));

    ba::ipc::Message got;
    bool ok = false;
    for (int i = 0; i < 200 && !ok; ++i) {
        serverStream.waitReadable(50);
        ok = gateway.poll(got);
    }
    CHECK(ok);
    CHECK(got.type == ba::ipc::Type::Hello);
    CHECK(got.payload == hello.payload);
    CHECK(gateway.connected());

    // Cierre del cliente -> el gateway detecta la desconexion.
    bool fired = false;
    gateway.onDisconnect([&fired]() { fired = true; });
    clientStream.close();
    for (int i = 0; i < 100 && !fired; ++i) {
        serverStream.waitReadable(50);
        gateway.poll(got);
    }
    CHECK(fired);
}

// ============================================================================
// 4) SingleInstance
// ============================================================================

void testSingleInstance() {
    const std::string name = uniqueName("lock");
    SingleInstance a;
    std::string err;
    CHECK(SingleInstance::acquire(name, a, &err));
    CHECK(a.held());

    SingleInstance b;
    CHECK(!SingleInstance::acquire(name, b, &err)); // mismo proceso
    CHECK(!b.held());

    Process child;
    CHECK(Process::spawn(childOpts({"try-lock", name}), child));
    auto code = child.wait(10000);
    CHECK(code.has_value() && *code == 3); // otro proceso: rechazado

    a.release();
    CHECK(SingleInstance::acquire(name, b, &err));
    b.release();

    Process child2;
    CHECK(Process::spawn(childOpts({"try-lock", name}), child2));
    code = child2.wait(10000);
    CHECK(code.has_value() && *code == 0); // libre: el hijo lo toma

#if defined(__linux__)
    // Linux: lock en el espacio abstracto; no debe quedar NADA en disco.
    CHECK(!std::filesystem::exists(runtimeDir() / (name + ".lock")));
#else
    // El lockfile queda a proposito (borrarlo al soltar abre una carrera con
    // quien ya lo tiene abierto); el test limpia el suyo.
    std::error_code ec;
    std::filesystem::remove(runtimeDir() / (name + ".lock"), ec);
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0) {
        return childMain(argc, argv);
    }
#if !defined(_WIN32)
    // Un write a un socket cerrado no debe matar al test (lo cubre
    // MSG_NOSIGNAL/SO_NOSIGPIPE, esto es red de seguridad).
    ::signal(SIGPIPE, SIG_IGN);
#endif

    testPaths();
    testHerramientas();
    testProcessExitCode();
    testProcessMissingExe();
    testProcessEnvAndCwd();
    testProcessOutputFile();
    testProcessWaitAndKill();
    testProcessDestructorKills();
    testProcessKillReachesGrandchildren();
    testProcessParentDeath();
    testSocketEcho();
    testSocketLargeWrite();
    testSocketNameInUseAndStale();
    testSocketConnectTimeout();
    testControlEndpointOverSocket();
    testSingleInstance();

    if (g_failures == 0) {
        std::printf("test_platform: OK\n");
        return 0;
    }
    std::printf("test_platform: %d fallo(s)\n", g_failures);
    return 1;
}
