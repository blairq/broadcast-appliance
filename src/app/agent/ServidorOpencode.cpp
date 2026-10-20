// ============================================================================
// src/app/agent/ServidorOpencode.cpp — ver ServidorOpencode.hpp
// ============================================================================
#include "agent/ServidorOpencode.hpp"

#include <QCoreApplication>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace ba::app::agent {

ServidorOpencode& ServidorOpencode::el() {
    static ServidorOpencode* s = [] {
        auto* x = new ServidorOpencode();
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, x, [x]() { x->parar(); });
        return x;
    }();
    return *s;
}

void ServidorOpencode::pedir(QObject* quien, const Pedido& p, std::function<void(QString, QString, QString)> listo) {
    // Si `quien` desaparece sin soltarlo, que no quede contado. Una vez por cada uno:
    // Qt::UniqueConnection no sirve con una lambda (no conectaba nada).
    if (usan_.emplace(quien, true).second)
        connect(quien, &QObject::destroyed, this, [this, quien]() { soltar(quien); });
    if (proc_ != nullptr && exe_ == p.ejecutable) {
        if (listo_) {
            listo(url_, clave_, {});
            return;
        }
        esperan_[quien] = std::move(listo);
        return;
    }
    esperan_[quien] = std::move(listo);
    arrancar(p);
}

void ServidorOpencode::soltar(QObject* quien) {
    if (usan_.erase(quien) > 0) disconnect(quien, &QObject::destroyed, this, nullptr);
    esperan_.erase(quien);
    if (usan_.empty()) parar();
}

void ServidorOpencode::avisar(const QString& error) {
    auto esperan = std::move(esperan_);
    esperan_.clear();
    for (auto& [quien, f] : esperan)
        if (f) error.isEmpty() ? f(url_, clave_, {}) : f({}, {}, error);
}

void ServidorOpencode::arrancar(const Pedido& p) {
    parar();
    exe_ = p.ejecutable;
    listo_ = false;
    quint16 puerto = 0;
    {
        QTcpServer t; // un puerto libre, solo 127.0.0.1
        t.listen(QHostAddress::LocalHost, 0);
        puerto = t.serverPort();
    }
    url_ = QString("http://127.0.0.1:%1").arg(puerto);
    // Una clave nueva en cada arranque: la API no le contesta a otro programa de la máquina.
    clave_.clear();
    for (int i = 0; i < 4; ++i) clave_ += QString::number(QRandomGenerator::system()->generate64(), 36);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : p.unsetEnv) env.remove(k);
    for (auto it = p.env.cbegin(); it != p.env.cend(); ++it) env.insert(it.key(), it.value());
    env.insert("OPENCODE_SERVER_PASSWORD", clave_);
    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(env);
    proc_->setWorkingDirectory(p.workDir);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    proc_->setStandardOutputFile(QProcess::nullDevice());
    proc_->start(p.ejecutable, {"serve", "--hostname", "127.0.0.1", "--port", QString::number(puerto)});
    if (!proc_->waitForStarted(10000)) {
        const QString e = "No arrancó el servidor de OpenCode: " + proc_->errorString();
        parar();
        avisar(e);
        return;
    }
    // Listo cuando la API contesta (401 sin clave también es «está andando»).
    auto* red = new QNetworkAccessManager(proc_);
    auto* reloj = new QTimer(proc_);
    auto intentos = std::make_shared<int>(0);
    QProcess* este = proc_;
    connect(reloj, &QTimer::timeout, proc_, [this, red, reloj, intentos, este]() {
        if (proc_ != este) return;
        if (proc_->state() == QProcess::NotRunning) {
            parar();
            avisar("El servidor de OpenCode se cerró al arrancar.");
            return;
        }
        if (++*intentos > 120) { // 60 s
            parar();
            avisar("El servidor de OpenCode no arrancó a tiempo.");
            return;
        }
        QNetworkReply* r = red->get(QNetworkRequest(QUrl(url_ + "/api/session")));
        connect(r, &QNetworkReply::finished, proc_, [this, r, reloj, este]() {
            r->deleteLater();
            const int st = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (proc_ != este || listo_ || st == 0) return;
            listo_ = true;
            reloj->stop();
            avisar({});
        });
    });
    reloj->start(500);
}

void ServidorOpencode::parar() {
    listo_ = false;
    if (proc_ == nullptr) return;
    QProcess* p = proc_;
    proc_ = nullptr;
    p->disconnect(this);
    if (p->state() != QProcess::NotRunning) {
        p->terminate();
        if (!p->waitForFinished(3000)) p->kill();
        p->waitForFinished(2000);
    }
    p->deleteLater();
}

} // namespace ba::app::agent
