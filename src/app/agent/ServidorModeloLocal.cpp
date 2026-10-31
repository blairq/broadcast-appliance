// ============================================================================
// src/app/agent/ServidorModeloLocal.cpp — ver ServidorModeloLocal.hpp
// ============================================================================
#include "agent/ServidorModeloLocal.hpp"
#include "agent/HijosDeLaApp.hpp"
#include "fault/ModoSeguro.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QString ServidorModeloLocal::ejecutable() {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/llama-server.exe";
#else
    return QCoreApplication::applicationDirPath() + "/llama/llama-server";
#endif
}

QString ServidorModeloLocal::mmprojPara(const QString& gguf) const {
    const QString m = mmprojPara_ ? mmprojPara_(gguf) : QString();
    return !m.isEmpty() && QFileInfo::exists(m) ? m : QString();
}

void ServidorModeloLocal::pedir(QObject* quien, const Pedido& pedido, std::function<void(quint16, QString)> listo,
                                std::function<void(QString)> estado) {
    // Modo seguro (F1-10): sin modelos locales en esta corrida.
    if (ba::fault::delProceso().apagada(ba::fault::Pieza::Modelos)) {
        listo(0, tr("Modo seguro: los modelos locales están apagados hasta el arranque normal."));
        return;
    }
    Pedido p = pedido;
    if (p.mmproj.isEmpty()) p.mmproj = mmprojPara(p.gguf);
    // Si `quien` desaparece sin soltarlo, que no quede contado. Una vez por cada uno:
    // Qt::UniqueConnection no sirve con una lambda (no conectaba nada).
    if (usan_.emplace(quien, true).second)
        connect(quien, &QObject::destroyed, this, [this, quien]() { soltar(quien); });
    if (proc_ != nullptr && gguf_ != p.gguf && p.noReiniciar) {
        // Anda con otro modelo (el que eligió alguien para el chat): no se le cambia.
        if (usan_.erase(quien) > 0) disconnect(quien, &QObject::destroyed, this, nullptr);
        listo(0, tr("El modelo local está cargado con otro modelo (el del chat)."));
        return;
    }
    // El mismo modelo: se usa el que anda (si hace falta la visión y no la cargó, se recarga con ella).
    if (proc_ != nullptr && gguf_ == p.gguf && (mmproj_ == p.mmproj || p.mmproj.isEmpty())) {
        if (listo_) {
            listo(puerto_, {});
            return;
        }
        esperan_[quien] = {std::move(listo), std::move(estado)}; // ya está cargando ese modelo
        return;
    }
    // Otro modelo (o ninguno, o dormido por calor): se arranca de nuevo, para todos los que lo usan.
    esperan_[quien] = {std::move(listo), std::move(estado)};
    arrancar(p);
}

void ServidorModeloLocal::soltar(QObject* quien) {
    if (usan_.erase(quien) > 0) disconnect(quien, &QObject::destroyed, this, nullptr);
    esperan_.erase(quien);
    if (usan_.empty()) {
        parar();
        dormido_ = false; // sin nadie, no hay a quién despertar
    }
}

void ServidorModeloLocal::porCalor(bool on) {
    calor_ = on;
    if (on) {
        // Mira cada pocos segundos: si justo está contestando, espera a que termine.
        if (vigiaCalor_ == nullptr) {
            vigiaCalor_ = new QTimer(this);
            vigiaCalor_->setInterval(5000);
            connect(vigiaCalor_, &QTimer::timeout, this, &ServidorModeloLocal::dormirSiOcioso);
        }
        vigiaCalor_->start();
        dormirSiOcioso();
        return;
    }
    if (vigiaCalor_ != nullptr) vigiaCalor_->stop();
    // Se alivió: si alguien lo sigue usando, vuelve (en el mismo puerto: los chats no se enteran).
    if (dormido_ && !usan_.empty()) arrancar(ultimo_);
}

void ServidorModeloLocal::dormirSiOcioso() {
    if (!calor_ || proc_ == nullptr || !listo_) return; // cargando: se mira en la vuelta siguiente
    auto* red = new QNetworkAccessManager(proc_);
    QNetworkReply* r = red->get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/slots").arg(puerto_))));
    QProcess* esteProc = proc_;
    connect(r, &QNetworkReply::finished, proc_, [this, r, red, esteProc]() {
        r->deleteLater();
        red->deleteLater();
        if (proc_ != esteProc || !calor_) return;
        // Sin /slots (no se sabe si atiende) no se lo toca: cortarle una respuesta a medias es peor.
        if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return;
        const QJsonDocument d = QJsonDocument::fromJson(r->readAll());
        if (!d.isArray()) return;
        for (const QJsonValue& s : d.array())
            if (s.toObject().value("is_processing").toBool()) return;
        const quint16 puerto = puerto_;
        parar();
        puerto_ = puerto;
        dormido_ = true;
    });
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
        avisarListo(0, tr("Falta llama-server en el programa (%1).").arg(p.ejecutable));
        return;
    }
    gguf_ = p.gguf;
    mmproj_ = p.mmproj;
    ultimo_ = p;
    listo_ = false;
    // Despierta del calor en el mismo puerto (el que tienen los chats); si no, uno libre.
    if (!dormido_ || puerto_ == 0) {
        QTcpServer t; // un puerto libre de esta máquina (solo 127.0.0.1)
        t.listen(QHostAddress::LocalHost, 0);
        puerto_ = t.serverPort();
    }
    dormido_ = false;
    proc_ = new QProcess(this);
    QDir().mkpath(p.logs);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    proc_->setStandardOutputFile(p.logs + "/llama-server.log", QIODevice::Truncate);
    proc_->setWorkingDirectory(QFileInfo(p.ejecutable).absolutePath());
    // Todas las capas a la GPU (Vulkan en Windows y Linux, Metal en la Mac). Sin MTP, a
    // propósito (pedido del dueño, 2026-10-11): con borradores es más lento en texto libre.
    // Se entrenó para contestar sin razonar: razonando, se enrosca y no termina.
    QStringList args{"-m", QDir::toNativeSeparators(p.gguf), "--host", "127.0.0.1", "--port",
                     QString::number(puerto_), "--jinja", "-ngl", QString::number(p.capasGpu), "-c",
                     QString::number(p.contexto), "--alias", "local", "--no-webui", "--reasoning", "off"};
    // La visión (el proyector del modelo base): las imágenes en /v1/chat/completions.
    if (!p.mmproj.isEmpty()) args << "--mmproj" << QDir::toNativeSeparators(p.mmproj);
    hijos::preparar(*proc_); // que no quede andando si la app muere sin pararlo
    proc_->start(p.ejecutable, args);
    if (!proc_->waitForStarted(10000)) {
        const QString e = tr("No arrancó llama-server: %1").arg(proc_->errorString());
        parar();
        avisarListo(0, e);
        return;
    }
    hijos::anotar(*proc_, "llama-server");
    for (auto& [quien, f] : esperan_)
        if (f.second) f.second(tr("Cargando el modelo local…"));
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
            avisarListo(0, tr("llama-server se cerró al cargar el modelo (ver Logs/llama-server.log)."));
            return;
        }
        if (++*intentos > maximo) {
            parar();
            avisarListo(0, tr("El modelo local no terminó de cargar a tiempo (ver Logs/llama-server.log)."));
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
    hijos::parar(*p, "llama-server");
    p->deleteLater();
}

} // namespace ba::app::agent
