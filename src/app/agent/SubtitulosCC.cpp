// SPDX-License-Identifier: GPL-2.0-or-later
#include "agent/SubtitulosCC.hpp"

#include "agent/FlujosTexto.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <nlohmann/json.hpp>

#include <fstream>

namespace ba::app::agent {

using json = nlohmann::json;

SubtitulosCC::SubtitulosCC(FlujosTexto& flujos, std::filesystem::path datos,
                           std::function<int(const QString&, double)> enviar608, QObject* parent)
    : QObject(parent), flujos_(flujos), datos_(std::move(datos)), enviar608_(std::move(enviar608)),
      red_(std::make_unique<QNetworkAccessManager>()) {
    std::ifstream f(datos_ / "Config" / "cc.json");
    const json j = json::parse(f, nullptr, false);
    if (j.is_object()) {
        activo_ = j.value("activo", false);
        flujo_ = QString::fromStdString(j.value("flujo", std::string()));
        metodo_ = QString::fromStdString(j.value("metodo", std::string("608")));
        url_ = QString::fromStdString(j.value("url", std::string()));
    }
    connect(&flujos_, &FlujosTexto::frase, this, &SubtitulosCC::alFrase);
}

SubtitulosCC::~SubtitulosCC() = default;

void SubtitulosCC::guardar() const {
    std::error_code ec;
    std::filesystem::create_directories(datos_ / "Config", ec);
    std::ofstream o(datos_ / "Config" / "cc.json");
    o << json{{"activo", activo_}, {"flujo", flujo_.toStdString()}, {"metodo", metodo_.toStdString()},
              {"url", url_.toStdString()}}
             .dump(1);
}

QVariantMap SubtitulosCC::estado() const {
    QStringList u(ultimas_.begin(), ultimas_.end());
    return {{"activo", activo_}, {"flujo", flujo_},           {"metodo", metodo_},
            {"url_youtube", !url_.isEmpty()}, {"enviadas", enviadas_}, {"ultimas", u}};
}

QString SubtitulosCC::activar(const QVariantMap& cfg) {
    const QString metodo = cfg.value("metodo", metodo_).toString();
    if (metodo != "608" && metodo != "youtube_http" && metodo != "ambos") return "metodo: 608, youtube_http o ambos";
    const QString url = cfg.value("url", url_).toString();
    if ((metodo == "youtube_http" || metodo == "ambos") && !url.startsWith("http"))
        return "falta la URL de subtítulos del evento (YouTube Studio > el vivo > Subtítulos > «HTTP POST»)";
    flujo_ = cfg.value("flujo", flujo_).toString();
    if (flujo_.isEmpty()) return "falta `flujo`: la salida de texto (texto_flujos)";
    metodo_ = metodo;
    url_ = url;
    activo_ = true;
    guardar();
    emit cambio();
    return {};
}

void SubtitulosCC::desactivar() {
    activo_ = false;
    guardar();
    emit cambio();
}

void SubtitulosCC::alFrase(const QString& flujo, const QString& texto) {
    if (!activo_ || flujo != flujo_ || texto.trimmed().isEmpty()) return;
    mandar(texto.trimmed());
}

void SubtitulosCC::mandar(const QString& texto) {
    // ~15 caracteres por segundo se leen cómodos; entre 2 y 7 s en pantalla.
    const double seg = std::clamp(texto.size() / 15.0, 2.0, 7.0);
    if (metodo_ == "608" || metodo_ == "ambos") {
        if (enviar608_) enviar608_(texto, seg);
    }
    if ((metodo_ == "youtube_http" || metodo_ == "ambos") && !url_.isEmpty()) {
        // La API de subtítulos de YouTube: la marca de tiempo y el texto, con número de secuencia.
        QUrl u(url_);
        QUrlQuery q(u);
        q.removeQueryItem("seq");
        q.addQueryItem("seq", QString::number(++seq_));
        u.setQuery(q);
        QNetworkRequest req(u);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
        const QString cuerpo =
            QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ss.zzz") + "\n" + texto + "\n";
        QNetworkReply* r = red_->post(req, cuerpo.toUtf8());
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    ++enviadas_;
    ultimas_.push_back(texto);
    while (ultimas_.size() > 8) ultimas_.pop_front();
    emit cambio();
}

QString SubtitulosCC::probar(const QString& texto) {
    if (metodo_ != "youtube_http" && enviar608_ && metodo_ != "ambos") {
        // 608: si no hay ninguna salida transmitiendo, se avisa.
        if (enviar608_(texto, 3.0) == 0) return "no hay ninguna transmisión activa para recibir los subtítulos 608";
        ++enviadas_;
        ultimas_.push_back(texto);
        emit cambio();
        return {};
    }
    mandar(texto);
    return {};
}

} // namespace ba::app::agent
