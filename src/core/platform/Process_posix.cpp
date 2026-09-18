// ============================================================================
// src/core/platform/Process_posix.cpp — Procesos hijos en Linux y macOS
//
// Linux: fork + execve. Hace falta fork (y no posix_spawn) porque
//   PR_SET_PDEATHSIG se fija DENTRO del hijo, entre el fork y el exec. Entre
//   ambos solo se usan llamadas async-signal-safe: argv/envp se arman antes.
//   Un pipe con CLOEXEC devuelve al padre el errno de un execve fallido, asi
//   `spawn` reporta "no existe el ejecutable" en vez de un hijo que sale 127.
// macOS: posix_spawn (el camino rapido del SO) con POSIX_SPAWN_CLOEXEC_DEFAULT
//   para que el hijo no herede sockets ni fds del padre.
// Ambos: grupo de procesos propio, para que kill() alcance tambien a los
//   nietos del hijo, y mascara de señales limpia.
// ============================================================================

#include "platform/Process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <set>
#include <thread>

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <spawn.h>
#include <sys/event.h>
#endif

extern char** environ;

namespace ba::platform {

namespace {

/// argv/envp como almacenamiento propio + vector de punteros para execve.
struct CStrings {
    std::vector<std::string> storage;
    std::vector<char*> ptrs;

    void finish() {
        ptrs.clear();
        for (auto& s : storage) {
            ptrs.push_back(s.data());
        }
        ptrs.push_back(nullptr);
    }
};

CStrings buildEnv(const ProcessOptions& opts) {
    CStrings env;
    std::set<std::string> overridden;
    for (const auto& name : opts.envRemove) {
        overridden.insert(name);
    }
    for (const auto& kv : opts.envSet) {
        overridden.insert(kv.first);
    }
    if (opts.inheritEnv && environ != nullptr) {
        for (char** e = environ; *e != nullptr; ++e) {
            const char* eq = std::strchr(*e, '=');
            const std::string name =
                eq ? std::string(*e, static_cast<std::size_t>(eq - *e)) : std::string(*e);
            if (overridden.count(name) == 0) {
                env.storage.emplace_back(*e);
            }
        }
    }
    for (const auto& kv : opts.envSet) {
        env.storage.push_back(kv.first + "=" + kv.second);
    }
    env.finish();
    return env;
}

CStrings buildArgv(const ProcessOptions& opts) {
    CStrings argv;
    argv.storage.push_back(opts.exe.string());
    for (const auto& a : opts.args) {
        argv.storage.push_back(a);
    }
    argv.finish();
    return argv;
}

int decodeStatus(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

void setErr(std::string* err, const std::string& msg) {
    if (err != nullptr) {
        *err = msg;
    }
}

} // namespace

Process::~Process() { reset(); }

Process::Process(Process&& other) noexcept
    : pid_(other.pid_), exitCode_(other.exitCode_), adopted_(other.adopted_),
      killOnDestroy_(other.killOnDestroy_) {
    other.pid_ = 0;
    other.exitCode_.reset();
}

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        reset();
        pid_ = other.pid_;
        exitCode_ = other.exitCode_;
        adopted_ = other.adopted_;
        killOnDestroy_ = other.killOnDestroy_;
        other.pid_ = 0;
        other.exitCode_.reset();
    }
    return *this;
}

void Process::reset() {
    // Mismo contrato que el Job Object de Windows: un Process con
    // killWithParent que se destruye con el hijo vivo lo mata y lo recoge. Sin
    // killWithParent el hijo sigue (A y B sobreviven al launcher); un hijo ya
    // terminado se recoge igual para no dejar zombies.
    if (pid_ != 0 && !exitCode_.has_value() && !adopted_) {
        if (killOnDestroy_) {
            kill();
            wait(-1);
        } else {
            wait(0);
        }
    }
    pid_ = 0;
    exitCode_.reset();
    adopted_ = false;
    killOnDestroy_ = false;
}

bool Process::spawn(const ProcessOptions& opts, Process& out, std::string* err) {
    out.reset();
    CStrings argv = buildArgv(opts);
    CStrings envp = buildEnv(opts);
    const std::string workDir = opts.workDir.string();

#if defined(__linux__)
    // Archivo de salida: se abre ANTES del fork (en el hijo solo dup2).
    int outFd = -1;
    if (!opts.outputFile.empty()) {
        outFd = ::open(opts.outputFile.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (outFd < 0) {
            setErr(err, "open '" + opts.outputFile.string() + "': " + std::strerror(errno));
            return false;
        }
    }
    // Pipe de reporte de exec: el hijo escribe su errno si execve falla; si
    // execve funciona, CLOEXEC lo cierra y el padre lee EOF.
    int pipefd[2];
    if (::pipe2(pipefd, O_CLOEXEC) != 0) {
        setErr(err, std::string("pipe2: ") + std::strerror(errno));
        if (outFd >= 0) {
            ::close(outFd);
        }
        return false;
    }
    const pid_t parent = ::getpid();
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        if (outFd >= 0) {
            ::close(outFd);
        }
        setErr(err, std::string("fork: ") + std::strerror(errno));
        return false;
    }
    if (pid == 0) {
        // --- Hijo: solo llamadas async-signal-safe hasta el execve ---
        ::close(pipefd[0]);
        ::setpgid(0, 0);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        if (opts.killWithParent) {
            ::prctl(PR_SET_PDEATHSIG, SIGKILL);
            // Si el padre murio entre el fork y el prctl, la señal ya no llega.
            if (::getppid() != parent) {
                ::_exit(127);
            }
        }
        if (outFd >= 0) {
            // dup2 limpia CLOEXEC en el destino: 1 y 2 sobreviven al execve.
            ::dup2(outFd, 1);
            ::dup2(outFd, 2);
        }
        // Todo fd heredado >2 queda CLOEXEC: el hijo no se lleva sockets del
        // padre (p. ej. el listener del canal de control).
        ::syscall(SYS_close_range, 3U, ~0U, 4U /* CLOSE_RANGE_CLOEXEC */);
        if (!workDir.empty() && ::chdir(workDir.c_str()) != 0) {
            const int e = errno;
            (void)!::write(pipefd[1], &e, sizeof(e));
            ::_exit(127);
        }
        ::execve(argv.ptrs[0], argv.ptrs.data(), envp.ptrs.data());
        const int e = errno;
        (void)!::write(pipefd[1], &e, sizeof(e));
        ::_exit(127);
    }
    // --- Padre ---
    ::setpgid(pid, pid); // evita la carrera con el setpgid del hijo
    ::close(pipefd[1]);
    if (outFd >= 0) {
        ::close(outFd);
    }
    int childErr = 0;
    ssize_t n;
    do {
        n = ::read(pipefd[0], &childErr, sizeof(childErr));
    } while (n < 0 && errno == EINTR);
    ::close(pipefd[0]);
    if (n == sizeof(childErr)) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        setErr(err, "exec '" + argv.storage[0] + "': " + std::strerror(childErr));
        return false;
    }
    out.pid_ = static_cast<std::uint32_t>(pid);
    out.killOnDestroy_ = opts.killWithParent;
    return true;

#elif defined(__APPLE__)
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t none;
    sigemptyset(&none);
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setpgroup(&attr, 0);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF |
                                        POSIX_SPAWN_CLOEXEC_DEFAULT);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    // Con CLOEXEC_DEFAULT se cierra TODO salvo lo heredado explicitamente.
    posix_spawn_file_actions_addinherit_np(&fa, 0);
    if (!opts.outputFile.empty()) {
        posix_spawn_file_actions_addopen(&fa, 1, opts.outputFile.c_str(), O_WRONLY | O_CREAT | O_APPEND,
                                         0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
    } else {
        posix_spawn_file_actions_addinherit_np(&fa, 1);
        posix_spawn_file_actions_addinherit_np(&fa, 2);
    }
    if (!workDir.empty()) {
        posix_spawn_file_actions_addchdir_np(&fa, workDir.c_str());
    }

    pid_t pid = 0;
    const int rc = ::posix_spawn(&pid, argv.ptrs[0], &fa, &attr, argv.ptrs.data(), envp.ptrs.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        setErr(err, "posix_spawn '" + argv.storage[0] + "': " + std::strerror(rc));
        return false;
    }
    out.pid_ = static_cast<std::uint32_t>(pid);
    out.killOnDestroy_ = opts.killWithParent;
    return true;
#else
#error "Process_posix.cpp: plataforma POSIX no soportada"
#endif
}

bool Process::attach(std::uint32_t pid, Process& out) {
    out.reset();
    if (pid == 0 || ::kill(static_cast<pid_t>(pid), 0) != 0) {
        return false;
    }
    out.pid_ = pid;
    out.adopted_ = true;
    return true;
}

std::optional<int> Process::wait(int timeoutMs) {
    if (exitCode_.has_value()) {
        return exitCode_;
    }
    if (pid_ == 0) {
        return std::nullopt;
    }
    const pid_t pid = static_cast<pid_t>(pid_);
    if (adopted_) {
        // No es hijo nuestro: waitpid no aplica. Se sondea su existencia.
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
        for (;;) {
            if (::kill(pid, 0) != 0 && errno == ESRCH) {
                exitCode_ = -1; // codigo desconocido
                return exitCode_;
            }
            if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (timeoutMs < 0) {
        int status = 0;
        pid_t r;
        do {
            r = ::waitpid(pid, &status, 0);
        } while (r < 0 && errno == EINTR);
        if (r == pid) {
            exitCode_ = decodeStatus(status);
        }
        return exitCode_;
    }
    // Espera acotada: polling con WNOHANG y pausas crecientes (1 -> 10 ms).
    // El supervisor usa timeouts cortos; no justifica un hilo por hijo.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto pause = std::chrono::milliseconds(1);
    for (;;) {
        int status = 0;
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            exitCode_ = decodeStatus(status);
            return exitCode_;
        }
        if (r < 0 && errno != EINTR) {
            return std::nullopt;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(pause);
        if (pause < std::chrono::milliseconds(10)) {
            pause *= 2;
        }
    }
}

bool Process::terminate() {
    if (!valid()) {
        return false;
    }
    // Al grupo (pid negativo): llega tambien a los nietos.
    return ::kill(-static_cast<pid_t>(pid_), SIGTERM) == 0 || ::kill(static_cast<pid_t>(pid_), SIGTERM) == 0;
}

bool Process::kill() {
    if (!valid()) {
        return false;
    }
    return ::kill(-static_cast<pid_t>(pid_), SIGKILL) == 0 || ::kill(static_cast<pid_t>(pid_), SIGKILL) == 0;
}

void exitWhenParentDies() {
#if defined(__linux__)
    ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (::getppid() == 1) {
        // El padre ya habia muerto antes del prctl (reparentado a init).
        ::raise(SIGTERM);
    }
#elif defined(__APPLE__)
    const pid_t parent = ::getppid();
    if (parent <= 1) {
        ::raise(SIGTERM);
        return;
    }
    const int kq = ::kqueue();
    if (kq < 0) {
        return;
    }
    struct kevent ev;
    EV_SET(&ev, parent, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
    if (::kevent(kq, &ev, 1, nullptr, 0, nullptr) != 0) {
        ::close(kq);
        if (errno == ESRCH) {
            ::raise(SIGTERM); // el padre ya no existe
        }
        return;
    }
    // Hilo dedicado que duerme en el kqueue hasta que el padre salga.
    std::thread([kq]() {
        struct kevent out;
        int n;
        do {
            n = ::kevent(kq, nullptr, 0, &out, 1, nullptr);
        } while (n < 0 && errno == EINTR);
        ::raise(SIGTERM);
    }).detach();
#endif
}

void suppressCrashDialogs() {}

} // namespace ba::platform
