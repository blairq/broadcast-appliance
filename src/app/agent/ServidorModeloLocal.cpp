// ============================================================================
// src/app/agent/ServidorModeloLocal.cpp — ver ServidorModeloLocal.hpp
// ============================================================================
#include "agent/ServidorModeloLocal.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace ba::app::agent {

ServidorModeloLocal& ServidorModeloLocal::el() {
    static ServidorModeloLocal* s = [] {
        auto* x = new ServidorModeloLocal();
        // Al cerrar la app, que no quede andando.
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, x, [x]() { x->parar(); });
        return x;
    }();
    return *s;
}

void ServidorModeloLocal::pedir(QObject* quien, const Pedido& p, std::function<void(quint16, QString)> listo,
                                std::function<void(QString)> estado) {
    usan_[quien] = true;
    // Si `quien` desaparece sin soltarlo, que no quede contado.
    connect(quien, &QObject::destroyed, this, [this, quien]() { soltar(quien); }, Qt::UniqueConnection);
    if (proc_ != nullptr && gguf_ == p.gguf) {
        if (listo_) {
            listo(puerto_, {});
            return;
        }
        esperan_[quien] = {std::move(listo), std::move(estado)}; // ya está cargando ese modelo
        return;
    }
    // Otro modelo (o ninguno): se arranca de nuevo, para todos los que lo usan.
    esperan_[quien] = {std::move(listo), std::move(estado)};
    arrancar(p);
}

void ServidorModeloLocal::soltar(QObject* quien) {
    usan_.erase(quien);
    esperan_.erase(quien);
    if (usan_.empty()) parar();
}

void ServidorModeloLocal::avisarListo(quint16 puerto, const QString& error) {
    auto esperan = std::move(esperan_);
    esperan_.clear();
    for (auto& [quien, f] : esperan)
        if (f.first) f.first(puerto, error);
}

void ServidorModeloLocal::arrancar(const Pedido& p) {
    parar();
    if (!QFileInfo::exists(p.ejecutable)) {
        avisarListo(0, "Falta llama-server en el programa (" + p.ejecutable + ").");
        return;
    }
    gguf_ = p.gguf;
    listo_ = false;
    {
        QTcpServer t; // un puerto libre de esta máquina (solo 127.0.0.1)
        t.listen(QHostAddress::LocalHost, 0);
        puerto_ = t.serverPort();
    }
    proc_ = new QProcess(this);
    QDir().mkpath(p.logs);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    proc_->setStandardOutputFile(p.logs + "/llama-server.log", QIODevice::Truncate);
    proc_->setWorkingDirectory(QFileInfo(p.ejecutable).absolutePath());
    // Todas las capas a la GPU (Vulkan en Windows y Linux, Metal en la Mac). Sin MTP, a
    // propósito (pedido del dueño, 2026-10-11): con borradores es más lento en texto libre.
    // Se entrenó para contestar sin razonar: razonando, se enrosca y no termina.
    proc_->start(p.ejecutable, {"-m", QDir::toNativeSeparators(p.gguf), "--host", "127.0.0.1", "--port",
                                QString::number(puerto_), "--jinja", "-ngl", QString::number(p.capasGpu), "-c",
                                QString::number(p.contexto), "--alias", "local", "--no-webui", "--reasoning", "off"});
    if (!proc_->waitForStarted(10000)) {
        const QString e = "No arrancó llama-server: " + proc_->errorString();
        parar();
        avisarListo(0, e);
        return;
    }
    for (auto& [quien, f] : esperan_)
        if (f.second) f.second("Cargando el modelo local…");
    // Listo cuando /health contesta 200 (el modelo cargado).
    auto* red = new QNetworkAccessManager(proc_);
    reloj_ = new QTimer(proc_);
    auto intentos = std::make_shared<int>(0);
    const int maximo = p.cargaS * 2; // cada 500 ms
    QProcess* esteProc = proc_;
    connect(reloj_, &QTimer::timeout, proc_, [this, red, intentos, maximo, esteProc]() {
        if (proc_ != esteProc) return;
        if (proc_->state() == QProcess::NotRunning) {
            parar();
            avisarListo(0, "llama-server se cerró al cargar el modelo (ver Logs/llama-server.log).");
            return;
        }
        if (++*intentos > maximo) {
            parar();
            avisarListo(0, "El modelo local no terminó de cargar a tiempo (ver Logs/llama-server.log).");
            return;
        }
        QNetworkReply* r = red->get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/health").arg(puerto_))));
        connect(r, &QNetworkReply::finished, proc_, [this, r, esteProc]() {
            r->deleteLater();
            if (proc_ != esteProc || listo_ || r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
                return;
            listo_ = true;
            if (reloj_ != nullptr) reloj_->stop();
            avisarListo(puerto_, {});
        });
    });
    reloj_->start(500);
}

void ServidorModeloLocal::parar() {
    listo_ = false;
    reloj_ = nullptr; // es hijo de proc_: se va con él
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
