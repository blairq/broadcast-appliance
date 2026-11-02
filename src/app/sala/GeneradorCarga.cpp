// src/app/sala/GeneradorCarga.cpp — quien genera los invitados de prueba (ver el .hpp)

#include "sala/GeneradorCarga.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTimer>

namespace ba::app::sala {

GeneradorHinchada::GeneradorHinchada(QString ejecutable, QString grabado, QObject* parent)
    : GeneradorCarga(parent), exe_(std::move(ejecutable)), grabado_(std::move(grabado)) {}

GeneradorHinchada::~GeneradorHinchada() {
    // Al cerrar: se cortan YA (son hijos nuestros).
    parando_ = true;
    for (const QPointer<QProcess>& p : procs_)
        if (p && p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(2000);
        }
}

bool GeneradorHinchada::disponible() const { return QFileInfo(exe_).isExecutable(); }

QString GeneradorHinchada::lanzar(const Lote& l) {
    if (!disponible()) return tr("el probador (hinchada) no viene en este programa");
    if (l.cantidad < 1) return {};
    parando_ = false;
    QDir().mkpath(l.salida);
    if (!grabado_.isEmpty()) QDir().mkpath(QFileInfo(grabado_).absolutePath());
    auto* proc = new QProcess(this);
    procs_.emplace_back(proc);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HINCHADA_PIN", l.credencial); // nunca en la línea de comandos
    proc->setProcessEnvironment(env);
    // Su consola a un archivo: sin tuberías que se llenen y lo traben.
    proc->setStandardOutputFile(l.salida + "/consola.txt");
    proc->setStandardErrorFile(l.salida + "/consola.txt", QIODevice::Append);
    QStringList args{"--servidor", "gateway", "--url", l.url, "--participantes", QString::number(l.cantidad),
                     // Nadie codifica: cada uno manda en loop el patrón con su número.
                     "--modo", "grabado", "--hablan", "2",
                     // Lo corta la prueba; esto es por si la app se va sin cortarlo.
                     "--durante", "20m", "--entrada-cada", QString::number(l.entradaCadaMs) + "ms", "--salida", l.salida};
    if (!grabado_.isEmpty()) args << "--grabado" << grabado_;
    if (!l.prefijo.isEmpty()) args << "--prefijo" << l.prefijo;
    if (l.inseguro) args << "--inseguro";
    QPointer<GeneradorHinchada> yo(this);
    connect(proc, &QProcess::finished, this, [yo, proc](int codigo, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!yo) return;
        if (!yo->parando_) emit yo->seCayo(codigo);
    });
    connect(proc, &QProcess::errorOccurred, this, [yo](QProcess::ProcessError e) {
        if (!yo || e != QProcess::FailedToStart || yo->parando_) return;
        emit yo->seCayo(-1);
    });
    proc->start(exe_, args);
    return {};
}

void GeneradorHinchada::parar() {
    parando_ = true;
    for (const QPointer<QProcess>& p : procs_) {
        if (!p || p->state() == QProcess::NotRunning) continue;
        p->terminate();
        QPointer<QProcess> h = p;
        QTimer::singleShot(3000, this, [h]() {
            if (h && h->state() != QProcess::NotRunning) h->kill();
        });
    }
    procs_.clear();
}

std::vector<std::uint32_t> GeneradorHinchada::pids() const {
    std::vector<std::uint32_t> out;
    for (const QPointer<QProcess>& p : procs_)
        if (p && p->state() == QProcess::Running && p->processId() > 0) out.push_back(static_cast<std::uint32_t>(p->processId()));
    return out;
}

bool GeneradorHinchada::andando() const {
    for (const QPointer<QProcess>& p : procs_)
        if (p && p->state() != QProcess::NotRunning) return true;
    return false;
}

} // namespace ba::app::sala
