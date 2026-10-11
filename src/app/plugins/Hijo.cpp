// src/app/plugins/Hijo.cpp — ver Hijo.hpp

#include "plugins/Hijo.hpp"

#include <QSocketNotifier>
#include <QTimer>

#if defined(_WIN32)
#include <QProcess>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <memory>
#include <string>
#include <vector>

namespace ba::app::plugins {

Hijo::Hijo(QObject* parent) : QObject(parent) {}

Hijo::~Hijo() {
#if defined(_WIN32)
    if (proc_ != nullptr) {
        proc_->disconnect(this);
        if (job_ != nullptr) TerminateJobObject(job_, 1);
        proc_->kill();
        proc_->waitForFinished(2000);
    }
#else
    if (corriendo()) {
        ::kill(-static_cast<pid_t>(pid_), SIGKILL);
        ::kill(static_cast<pid_t>(pid_), SIGKILL);
        int st = 0;
        ::waitpid(static_cast<pid_t>(pid_), &st, 0);
    }
#endif
    cerrarTodo();
}

void Hijo::cerrarTodo() {
#if defined(_WIN32)
    if (job_ != nullptr) {
        CloseHandle(job_);
        job_ = nullptr;
    }
#else
    delete nOut_;
    delete nErr_;
    nOut_ = nErr_ = nullptr;
    for (int* fd : {&in_, &out_, &err_})
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
#endif
}

bool Hijo::iniciar(const QString& programa, const QStringList& args, const QProcessEnvironment& env,
                   const QString& carpeta, bool unirErrores, QString* error) {
#if defined(_WIN32)
    proc_ = new QProcess(this);
    proc_->setProgram(programa);
    proc_->setArguments(args);
    proc_->setProcessEnvironment(env);
    if (!carpeta.isEmpty()) proc_->setWorkingDirectory(carpeta);
    proc_->setProcessChannelMode(unirErrores ? QProcess::MergedChannels : QProcess::SeparateChannels);
    // Sin ventana de consola.
    proc_->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
        bufOut_ += proc_->readAllStandardOutput();
        emit salida();
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        bufErr_ += proc_->readAllStandardError();
        emit errores();
    });
    connect(proc_, &QProcess::finished, this, [this](int codigo, QProcess::ExitStatus st) {
        if (termino_) return;
        bufOut_ += proc_->readAllStandardOutput();
        bufErr_ += proc_->readAllStandardError();
        termino_ = true;
        if (!bufOut_.isEmpty()) emit salida();
        if (!bufErr_.isEmpty()) emit errores();
        emit termino(st == QProcess::NormalExit ? codigo : -1, st != QProcess::NormalExit);
    });
    proc_->start(QIODevice::ReadWrite | QIODevice::Unbuffered);
    if (!proc_->waitForStarted(10000)) {
        if (error) *error = QStringLiteral("no se pudo lanzar «%1»: %2").arg(programa, proc_->errorString());
        delete proc_;
        proc_ = nullptr;
        return false;
    }
    // Al job: lo que lance nace adentro, y matar() o que se caiga la app se lleva a todos.
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_ != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &li, sizeof li);
        HANDLE hp = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(proc_->processId()));
        if (hp != nullptr) {
            AssignProcessToJobObject(job_, hp);
            CloseHandle(hp);
        }
    }
    pid_ = proc_->processId();
    termino_ = false;
    return true;
#else
    int pin[2], pout[2], perr[2] = {-1, -1};
    if (::pipe(pin) != 0) {
        if (error) *error = "no se pudo crear un pipe";
        return false;
    }
    if (::pipe(pout) != 0 || (!unirErrores && ::pipe(perr) != 0)) {
        if (error) *error = "no se pudo crear un pipe";
        return false;
    }
    // Las puntas de la app: no se heredan, y no bloquean.
    for (int fd : {pin[1], pout[0], perr[0]}) {
        if (fd < 0) continue;
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pin[0], 0);
    posix_spawn_file_actions_adddup2(&fa, pout[1], 1);
    posix_spawn_file_actions_adddup2(&fa, unirErrores ? pout[1] : perr[1], 2);
    const QByteArray dir = carpeta.toLocal8Bit();
    if (!carpeta.isEmpty()) posix_spawn_file_actions_addchdir_np(&fa, dir.constData());
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    short flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT; // macOS: solo 0, 1 y 2 pasan al hijo
#endif
    posix_spawnattr_setflags(&at, flags);
    posix_spawnattr_setpgroup(&at, 0);
    sigset_t vacio, defecto;
    sigemptyset(&vacio);
    posix_spawnattr_setsigmask(&at, &vacio);
    sigemptyset(&defecto);
    for (int s : {SIGPIPE, SIGCHLD, SIGINT, SIGTERM, SIGHUP, SIGQUIT})
        sigaddset(&defecto, s);
    posix_spawnattr_setsigdefault(&at, &defecto);

    const QByteArray prog = programa.toLocal8Bit();
    std::vector<QByteArray> a8;
    a8.push_back(prog);
    for (const QString& x : args)
        a8.push_back(x.toLocal8Bit());
    std::vector<char*> argv;
    for (auto& x : a8)
        argv.push_back(x.data());
    argv.push_back(nullptr);
    std::vector<QByteArray> e8;
    for (const QString& x : env.toStringList())
        e8.push_back(x.toLocal8Bit());
    std::vector<char*> envp;
    for (auto& x : e8)
        envp.push_back(x.data());
    envp.push_back(nullptr);

    pid_t pid = -1;
    const int r = posix_spawn(&pid, prog.constData(), &fa, &at, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    ::close(pin[0]);
    ::close(pout[1]);
    if (perr[1] >= 0) ::close(perr[1]);
    if (r != 0) {
        ::close(pin[1]);
        ::close(pout[0]);
        if (perr[0] >= 0) ::close(perr[0]);
        if (error)
            *error = QStringLiteral("no se pudo lanzar «%1»: %2")
                         .arg(programa, QString::fromLocal8Bit(strerror(r)));
        return false;
    }
    pid_ = pid;
    termino_ = false;
    in_ = pin[1];
    out_ = pout[0];
    err_ = perr[0];
    nOut_ = new QSocketNotifier(out_, QSocketNotifier::Read, this);
    connect(nOut_, &QSocketNotifier::activated, this, [this]() { leer(out_, bufOut_, true); });
    if (err_ >= 0) {
        nErr_ = new QSocketNotifier(err_, QSocketNotifier::Read, this);
        connect(nErr_, &QSocketNotifier::activated, this, [this]() { leer(err_, bufErr_, false); });
    }
    // La red de seguridad: aunque no llegue ningún aviso, cada 200 ms se lee
    // lo que haya y se mira si terminó.
    revision_ = new QTimer(this);
    connect(revision_, &QTimer::timeout, this, &Hijo::revisar);
    revision_->start(200);
    return true;
#endif
}

void Hijo::leer(int fd, QByteArray& buf, bool esSalida) {
#if !defined(_WIN32)
    if (fd < 0) return;
    bool algo = false;
    char tmp[65536];
    for (;;) {
        const ssize_t n = ::read(fd, tmp, sizeof tmp);
        if (n > 0) {
            buf.append(tmp, static_cast<int>(n));
            algo = true;
            continue;
        }
        if (n == 0) {
            // Cerró su punta: no hay más (el notificador se apaga para no girar).
            if (esSalida && nOut_) nOut_->setEnabled(false);
            if (!esSalida && nErr_) nErr_->setEnabled(false);
        }
        break; // EAGAIN o cerrado
    }
    if (algo) emit esSalida ? salida() : errores();
#else
    (void)fd;
    (void)buf;
    (void)esSalida;
#endif
}

void Hijo::revisar() {
#if !defined(_WIN32)
    leer(out_, bufOut_, true);
    leer(err_, bufErr_, false);
    if (termino_ || pid_ <= 0) return;
    int st = 0;
    const pid_t r = ::waitpid(static_cast<pid_t>(pid_), &st, WNOHANG);
    if (r == 0) return;
    // Terminó (o alguien más lo levantó: ECHILD): lo último que quedó en los pipes y avisar.
    leer(out_, bufOut_, true);
    leer(err_, bufErr_, false);
    termino_ = true;
    revision_->stop();
    int codigo = -1;
    bool caido = true;
    if (r > 0 && WIFEXITED(st)) {
        codigo = WEXITSTATUS(st);
        caido = false;
    }
    if (in_ >= 0) {
        ::close(in_);
        in_ = -1;
    }
    emit termino(codigo, caido);
#endif
}

void Hijo::escribir(const QByteArray& datos) {
#if !defined(_WIN32)
    if (in_ < 0) return;
    qsizetype hecho = 0;
    int vueltas = 0;
    while (hecho < datos.size() && vueltas < 200) {
        const ssize_t n = ::write(in_, datos.constData() + hecho, static_cast<size_t>(datos.size() - hecho));
        if (n > 0) {
            hecho += n;
            continue;
        }
        if (n < 0 && errno == EAGAIN) {
            ++vueltas; // el pipe está lleno: el hijo no lee (no se espera más de ~20 ms)
            ::usleep(100);
            continue;
        }
        break; // cerrado
    }
#else
    if (proc_ != nullptr && proc_->state() == QProcess::Running) {
        proc_->write(datos);
    }
#endif
}

void Hijo::cerrarEntrada() {
#if defined(_WIN32)
    if (proc_ != nullptr) proc_->closeWriteChannel();
#else
    if (in_ >= 0) {
        ::close(in_);
        in_ = -1;
    }
#endif
}

void Hijo::terminar() {
#if defined(_WIN32)
    if (corriendo()) {
        cerrarEntrada();
        QTimer::singleShot(3000, this, [this]() { matar(); });
    }
#else
    if (corriendo()) {
        ::kill(-static_cast<pid_t>(pid_), SIGTERM);
        ::kill(static_cast<pid_t>(pid_), SIGTERM);
    }
#endif
}

void Hijo::matar() {
#if defined(_WIN32)
    if (corriendo()) {
        if (job_ != nullptr) TerminateJobObject(job_, 1);
        if (proc_ != nullptr) proc_->kill();
    }
#else
    if (corriendo()) {
        ::kill(-static_cast<pid_t>(pid_), SIGKILL);
        ::kill(static_cast<pid_t>(pid_), SIGKILL);
    }
#endif
}

QByteArray Hijo::leerSalida() {
    QByteArray b;
    b.swap(bufOut_);
    return b;
}

QByteArray Hijo::leerErrores() {
    QByteArray b;
    b.swap(bufErr_);
    return b;
}

QPointer<Hijo> Hijo::correr(QObject* dueno, const QString& programa, const QStringList& args,
                            const QProcessEnvironment& env, int topeMs,
                            std::function<void(int, QByteArray, QByteArray)> listo) {
    auto* h = new Hijo(dueno);
    QString e;
    if (!h->iniciar(programa, args, env, {}, false, &e)) {
        h->deleteLater();
        listo(-1, {}, e.toUtf8());
        return nullptr;
    }
    auto out = std::make_shared<QByteArray>(), err = std::make_shared<QByteArray>();
    connect(h, &Hijo::salida, h, [h, out]() { out->append(h->leerSalida()); });
    connect(h, &Hijo::errores, h, [h, err]() { err->append(h->leerErrores()); });
    if (topeMs > 0) QTimer::singleShot(topeMs, h, [h]() { h->matar(); });
    connect(h, &Hijo::termino, h, [h, out, err, listo](int codigo, bool caido) {
        out->append(h->leerSalida());
        err->append(h->leerErrores());
        h->deleteLater();
        listo(caido ? -1 : codigo, *out, *err);
    });
    return h;
}

} // namespace ba::app::plugins
