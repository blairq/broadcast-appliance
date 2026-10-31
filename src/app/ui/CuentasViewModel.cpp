// src/app/ui/CuentasViewModel.cpp — ver el .hpp

#include "ui/CuentasViewModel.hpp"

#include "arranque/AppRuntime.hpp"
#include "control/BusEventos.hpp"
#include "net/AccountGuides.hpp"
#include "net/HttpsClient.hpp"
#include "platform/Paths.hpp"
#include "plugins/Plugins.hpp"
#include "red/Red.hpp"
#include "ui/FacebookViewModel.hpp"
#include "ui/TwitchViewModel.hpp"
#include "ui/YouTubeViewModel.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QUrl>

namespace ba::app {

namespace {

/// Sin padre y devuelto a QML: que no se lo quede su recolector. Es del área.
QObject* deCpp(QObject* o) {
    if (o != nullptr) QQmlEngine::setObjectOwnership(o, QQmlEngine::CppOwnership);
    return o;
}

} // namespace

CuentasViewModel::CuentasViewModel(Partes p, QObject* parent) : QObject(parent), p_(std::move(p)) {
    // HTTPS propio (plataformas): raices de certificados del bundle primero.
    net::HttpsClient::setDefaultCaSearch(
        {(ba::platform::bundleRoot() / "data" / "cacert.pem").string(),
         (p_.rt.dataRoot() / "data" / "cacert.pem").string(),
         (QCoreApplication::applicationDirPath() + "/../../data/cacert.pem").toStdString()});
    youtube_ = std::make_unique<YouTubeViewModel>(p_.rt, p_.ventanilla);
    connect(youtube_.get(), &YouTubeViewModel::notify, this, &CuentasViewModel::notify);
    twitch_ = std::make_unique<TwitchViewModel>(p_.rt, p_.ventanilla);
    connect(twitch_.get(), &TwitchViewModel::notify, this, &CuentasViewModel::notify);
    facebook_ = std::make_unique<FacebookViewModel>(p_.rt, p_.ventanilla);
    connect(facebook_.get(), &FacebookViewModel::notify, this, &CuentasViewModel::notify);
    // La página web del evento muestra dónde verlo en vivo.
    connect(youtube_.get(), &YouTubeViewModel::stateChanged, this, &CuentasViewModel::webCambio);
    connect(twitch_.get(), &TwitchViewModel::stateChanged, this, &CuentasViewModel::webCambio);
    connect(twitch_.get(), &TwitchViewModel::channelChanged, this, &CuentasViewModel::webCambio);
    connect(facebook_.get(), &FacebookViewModel::stateChanged, this, &CuentasViewModel::webCambio);
    // Cada vivo con su URL pública (sin la clave), al bus y al histórico.
    const auto alVivo = [this](const QVariantMap& m) {
        control::BusEventos* bus = p_.bus ? p_.bus() : nullptr;
        if (bus == nullptr) return;
        bus->publicar("salida/vivo",
                      nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson().toStdString(), nullptr,
                                            false),
                      true);
    };
    connect(youtube_.get(), &YouTubeViewModel::vivo, this, alVivo);
    connect(twitch_.get(), &TwitchViewModel::vivo, this, alVivo);
    connect(facebook_.get(), &FacebookViewModel::vivo, this, alVivo);
}

CuentasViewModel::~CuentasViewModel() = default;

QObject* CuentasViewModel::youtubeQml() const { return deCpp(youtube_.get()); }
QObject* CuentasViewModel::twitchQml() const { return deCpp(twitch_.get()); }
QObject* CuentasViewModel::facebookQml() const { return deCpp(facebook_.get()); }

nlohmann::json CuentasViewModel::paraLaWeb() const {
    nlohmann::json yt{{"liveId", youtube_->liveBroadcastId().toStdString()},
                      {"canalId", youtube_->account().value("id").toString().toStdString()},
                      {"handle", youtube_->account().value("customUrl").toString().toStdString()}};
    nlohmann::json tw{{"login", twitch_->user().value("login").toString().toStdString()},
                      {"enVivo", twitch_->streamingHere() || !twitch_->stream().isEmpty()}};
    nlohmann::json fb{{"liveId", facebook_->liveId().toStdString()}, {"pageId", facebook_->pageId().toStdString()}};
    return nlohmann::json{{"youtube", yt}, {"twitch", tw}, {"facebook", fb}};
}

QVariantMap CuentasViewModel::accountGuide(const QString& platform) const {
    const nlohmann::json g = accounts::guide(platform.toStdString(), facebook_->redirectUri().toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(g.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QString CuentasViewModel::saveAccountCredential(const QString& platform, const QVariantMap& v) {
    red::Red* red = p_.red ? p_.red() : nullptr;
    // Una cámara de la red (camara:<ip>): usuario y clave, cifrados.
    if (platform.startsWith("camara:")) {
        if (red == nullptr) return tr("El escáner de red no está disponible.");
        return red->guardarCredenciales(platform.mid(7), v.value("usuario").toString(), v.value("clave").toString());
    }
    // Varias cámaras en un formulario: «usuario|<ip>» y «clave|<ip>». Las que
    // quedan sin clave se saltean (se pueden completar después).
    if (platform.startsWith("camaras:")) {
        if (red == nullptr) return tr("El escáner de red no está disponible.");
        int guardadas = 0;
        for (const QString& ip : platform.mid(8).split(',', Qt::SkipEmptyParts)) {
            const QString clave = v.value("clave|" + ip).toString();
            if (clave.isEmpty()) continue;
            QString usuario = v.value("usuario|" + ip).toString().trimmed();
            if (usuario.isEmpty())
                usuario = QString::fromStdString(red->equipo(ip.toStdString()).value("usuario_de_fabrica", std::string("admin")));
            const QString e = red->guardarCredenciales(ip, usuario, clave);
            if (!e.isEmpty()) return ip + ": " + e;
            ++guardadas;
        }
        return guardadas == 0 ? tr("No escribiste ninguna clave.") : QString();
    }
    // Las claves de un plugin (plugin:<id>): un campo por cada una que declara.
    if (platform.startsWith("plugin:")) {
        plugins::Plugins* plugins = p_.plugins ? p_.plugins() : nullptr;
        if (plugins == nullptr) return tr("Los plugins no están disponibles.");
        int guardadas = 0;
        for (auto it = v.begin(); it != v.end(); ++it) {
            const QString valor = it.value().toString();
            if (valor.isEmpty()) continue;
            const QString e = plugins->guardarSecreto(platform.mid(7), it.key(), valor);
            if (!e.isEmpty()) return e;
            ++guardadas;
        }
        return guardadas == 0 ? tr("No escribiste ninguna clave.") : QString();
    }
    const std::string pl = accounts::normalize(platform.toStdString());
    if (pl == "youtube") {
        QString path = v.value("importFile").toString();
        if (v.value("fromDownloads").toBool()) {
            QString why;
            path = accounts::findGoogleClientJson(&why);
            if (path.isEmpty()) return why;
        }
        if (!path.isEmpty()) {
            const QUrl u(path);
            QFile f(u.isLocalFile() ? u.toLocalFile() : path);
            if (!f.open(QIODevice::ReadOnly)) return tr("No se pudo leer %1.").arg(f.fileName());
            return youtube_->saveClient(QString::fromUtf8(f.readAll()));
        }
        return youtube_->saveClient(v.value("clientId").toString().trimmed(), v.value("clientSecret").toString().trimmed());
    }
    if (pl == "twitch")
        return twitch_->saveClient(v.value("clientId").toString().trimmed(), v.value("clientSecret").toString().trimmed());
    if (pl == "facebook")
        return facebook_->saveClient(v.value("appId").toString().trimmed(), v.value("appSecret").toString().trimmed(),
                                     facebook_->client().port());
    return tr("Plataforma desconocida: %1.").arg(platform);
}

} // namespace ba::app
