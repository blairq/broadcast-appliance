// ============================================================================
// src/app/term/Pty_posix.cpp — Pty con forkpty (Linux y macOS)
// ============================================================================

#include "term/Pty.hpp"

#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

namespace ba::app::term {

struct Pty::Impl {
    int master = -1;
    pid_t pid = -1;
    QSocketNotifier* notifier = nullptr;
    /// Lo que todavía no entró en la TTY: se reintenta cada pocos milisegundos hasta que
    /// el programa lo lea (sin trabar el hilo de la interfaz, ver Pty::write). Un
    /// temporizador que corre solo mientras hay cola: simple y igual en todos los sistemas.
    QTimer* reintento = nullptr;
    QByteArray pendiente;
    bool exited = false;
};

Pty::Pty(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {}

Pty::~Pty() { terminate(); }

bool Pty::start(const Options& opt, QString* error) {
    terminate();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : opt.unsetEnv)
        env.remove(k);
    for (auto it = opt.env.begin(); it != opt.env.end(); ++it)
        env.insert(it.key(), it.value());
    if (!env.contains("TERM")) env.insert("TERM", "xterm-256color");
    env.insert("COLORTERM", "truecolor");

    // Todo lo que el hijo necesita, preparado ANTES de fork (despues de fork
    // solo se puede usar lo async-signal-safe).
    std::vector<QByteArray> envStore;
    for (const QString& kv : env.toStringList())
        envStore.push_back(kv.toLocal8Bit());
    std::vector<char*> envp;
    for (auto& e : envStore)
        envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<QByteArray> argStore{opt.program.toLocal8Bit()};
    for (const QString& a : opt.args)
        argStore.push_back(a.toLocal8Bit());
    std::vector<char*> argv;
    for (auto& a : argStore)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    const QByteArray cwd = opt.workDir.toLocal8Bit();

    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(opt.cols);
    ws.ws_row = static_cast<unsigned short>(opt.rows);
    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0) {
        if (error != nullptr) *error = QString("forkpty: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        // Hijo: señales por defecto (Qt/libobs pueden haber cambiado alguna).
        for (int s : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD})
            ::signal(s, SIG_DFL);
        if (!cwd.isEmpty() && ::chdir(cwd.constData()) != 0) _exit(126);
        ::execve(argv[0], argv.data(), envp.data());
        const char msg[] = "\r\nNo se pudo ejecutar el programa.\r\n";
        (void)!::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(127);
    }
    ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    d_->master = master;
    d_->pid = pid;
    d_->exited = false;
    d_->pendiente.clear();
    if (d_->reintento == nullptr) {
        d_->reintento = new QTimer(this);
        d_->reintento->setSingleShot(true);
        d_->reintento->setInterval(4);
        connect(d_->reintento, &QTimer::timeout, this, [this]() { escribirPendiente(); });
    }
    d_->notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
    connect(d_->notifier, &QSocketNotifier::activated, this, [this]() {
        char buf[65536];
        QByteArray chunk;
        for (;;) {
            const ssize_t n = ::read(d_->master, buf, sizeof(buf));
            if (n > 0) {
                chunk.append(buf, static_cast<qsizetype>(n));
                if (chunk.size() > (1 << 20)) break;
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
            // EOF / EIO: el programa termino (o cerro la TTY).
            if (!chunk.isEmpty()) emit output(chunk);
            chunk.clear();
            d_->notifier->setEnabled(false);
            int status = 0;
            int code = -1;
            if (::waitpid(d_->pid, &status, 0) == d_->pid)
                code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            if (d_->reintento != nullptr) d_->reintento->stop();
            d_->pendiente.clear();
            ::close(d_->master);
            d_->master = -1;
            d_->pid = -1;
            d_->exited = true;
            d_->notifier->deleteLater();
            d_->notifier = nullptr;
            emit finished(code);
            return;
        }
        if (!chunk.isEmpty()) emit output(chunk);
    });
    return true;
}

// NUNCA espera. Antes, con la TTY llena, se quedaba reintentando en el hilo de la
// interfaz; pero el programa deja de leer lo que se le escribe cuando no puede escribir
// lo suyo, y lo suyo lo lee ese mismo hilo: con un texto largo (pegar un encargo de
// 13.000 letras al asistente) se trababan los dos y la app se colgaba. Ahora lo que no
// entra queda en cola y se reintenta enseguida, sin esperar (Impl::reintento).
void Pty::write(const QByteArray& data) {
    if (d_->master < 0 || data.isEmpty()) return;
    d_->pendiente.append(data);
    escribirPendiente();
}

void Pty::escribirPendiente() {
    while (d_->master >= 0 && !d_->pendiente.isEmpty()) {
        const ssize_t n = ::write(d_->master, d_->pendiente.constData(), static_cast<size_t>(d_->pendiente.size()));
        if (n > 0) {
            d_->pendiente.remove(0, static_cast<qsizetype>(n));
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN) {
            break; // llena: sigue cuando haya lugar
        } else {
            d_->pendiente.clear(); // la TTY se cerró: no hay a quién mandarle
            break;
        }
    }
    if (d_->reintento != nullptr && d_->master >= 0 && !d_->pendiente.isEmpty() && !d_->reintento->isActive())
        d_->reintento->start();
}

void Pty::resize(int cols, int rows) {
    if (d_->master < 0 || cols <= 0 || rows <= 0) return;
    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ::ioctl(d_->master, TIOCSWINSZ, &ws);
}

void Pty::terminate() {
    if (d_->pid <= 0) return;
    const pid_t pid = d_->pid;
    if (d_->notifier != nullptr) {
        d_->notifier->setEnabled(false);
        d_->notifier->deleteLater();
        d_->notifier = nullptr;
    }
    if (d_->reintento != nullptr) d_->reintento->stop();
    d_->pendiente.clear();
    ::close(d_->master); // SIGHUP a la sesion del hijo
    d_->master = -1;
    ::kill(-pid, SIGHUP);
    ::kill(pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 30; ++i) {
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            d_->pid = -1;
            emit finished(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            return;
        }
        ::usleep(20000);
    }
    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
    d_->pid = -1;
    emit finished(137);
}

bool Pty::running() const { return d_->pid > 0; }

qint64 Pty::pid() const { return d_->pid; }

} // namespace ba::app::term

#include "moc_Pty.cpp"
