// SPDX-License-Identifier: GPL-2.0-or-later
#include "emision/AvisoFueraDeUi.hpp"

#include "agent/Notificador.hpp"
#include "ui/ConferenceClient.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

#include <QDateTime>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

namespace ba::app {

using json = nlohmann::json;

AvisoFueraDeUi::AvisoFueraDeUi(std::filesystem::path datos) : datos_(std::move(datos)) {
    hilo_.setObjectName("avisos-fuera-de-ui");
    contexto_ = new QObject();
    contexto_->moveToThread(&hilo_);
    // Lo que se creó en el hilo se borra en el hilo (al terminar su bucle).
    QObject::connect(&hilo_, &QThread::finished, contexto_, [this]() {
        delete red_;
        red_ = nullptr;
    }, Qt::DirectConnection);
    QObject::connect(&hilo_, &QThread::finished, contexto_, &QObject::deleteLater, Qt::DirectConnection);
    hilo_.start();
}

AvisoFueraDeUi::~AvisoFueraDeUi() {
    if (latido_ != nullptr) latido_->ponerAviso(nullptr); // espera al aviso en curso
    hilo_.quit();
    hilo_.wait();
}

json AvisoFueraDeUi::mensajeControlWeb(const Alarma& a) {
    return {{"type", "alarma"},
            {"id", a.id},
            {"activa", a.activa},
            {"titulo", a.titulo},
            {"texto", a.texto},
            {"ms", QDateTime::currentMSecsSinceEpoch()}};
}

void AvisoFueraDeUi::escuchar(LatidoAire& latido) {
    latido_ = &latido;
    latido.ponerAviso([this, &latido](bool trabada, std::uint64_t ms, bool motorVivo) {
        avisar(uiTrabada(trabada, ms, motorVivo, latido.politica()));
    });
}

AvisoFueraDeUi::Alarma AvisoFueraDeUi::uiTrabada(bool trabada, std::uint64_t ms, bool motorVivo, const LatidoAire::Politica& p) {
    using ba::i18n::traducir;
    Alarma a;
    a.id = "ui_trabada";
    a.activa = trabada;
    a.titulo = trabada ? "⚠ " + traducir("supervisor", "La interfaz no responde y hay aire") : traducir("supervisor", "La interfaz volvió");
    a.texto = trabada ? (motorVivo ? traducir("supervisor", "El motor sigue: lo que sale no se cortó. Manejá desde el control web o esperá a que vuelva.")
                                   : traducir("supervisor", "El motor tampoco avanza: el supervisor va a relanzar la app."))
                      : ba::i18n::llenar(traducir("supervisor", "Estuvo trabada %1 s."), {std::to_string((ms + 500) / 1000)});
    a.notificarTodos = p.notificarTodos;
    a.notificar = p.notificar;
    return a;
}

void AvisoFueraDeUi::avisar(Alarma a) {
    QMetaObject::invokeMethod(contexto_, [this, a = std::move(a)]() { enHilo(a); }, Qt::QueuedConnection);
}

void AvisoFueraDeUi::enHilo(const Alarma& a) {
    if (red_ == nullptr) red_ = new QNetworkAccessManager();
    // 1) Los aparatos del control web, por el gateway (B: otro proceso, no depende de la interfaz).
    int puerto = 0;
    QString token;
    if (a.controlWeb && ConferenceClient::leerAdmin(puerto, token)) {
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/api/v1/control/enviar").arg(puerto)));
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(3000);
        const json cuerpo{{"todas", true}, {"msg", mensajeControlWeb(a)}};
        QNetworkReply* r = red_->post(req, QByteArray::fromStdString(cuerpo.dump(-1, ' ', false, json::error_handler_t::replace)));
        QObject::connect(r, &QNetworkReply::finished, r, [r]() {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError)
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "supervisor", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("supervisor", "la alarma no llegó al control web: %1"),
                                                            {r->errorString().toStdString()}));
        });
    }
    // 2) Los notificadores: uno propio, leído ahora (lo que cargó la interfaz hasta este momento).
    auto* n = new agent::Notificador(datos_);
    std::vector<std::string> ids = a.notificar;
    if (a.notificarTodos) {
        ids.clear();
        for (const auto& x : n->lista()) ids.push_back(x.value("id", std::string()));
    }
    if (ids.empty()) {
        delete n;
        return;
    }
    auto faltan = std::make_shared<std::size_t>(ids.size());
    for (const auto& id : ids)
        n->enviar(id, a.titulo, a.texto, [n, faltan, id, resultado = a.resultado](bool ok, const std::string& detalle) {
            if (!ok)
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "supervisor", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("supervisor", "la alarma no salió por «%1»: %2"), {id, detalle}));
            if (resultado) resultado(id, ok, detalle);
            if (--*faltan == 0) n->deleteLater();
        });
}

} // namespace ba::app
