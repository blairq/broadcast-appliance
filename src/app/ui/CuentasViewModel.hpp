// ============================================================================
// src/app/ui/CuentasViewModel.hpp — el área de las cuentas de las plataformas (F3-03, partición)
//
// Sale de StudioViewModel: YouTube, Twitch y Facebook (cada una su ViewModel, con su
// cliente HTTPS y su credencial cifrada), lo que de ellas ve el resto —«dónde verlo en
// vivo» de la página web del evento, cada vivo con su URL pública en el bus— y el
// formulario seguro de credenciales (las plataformas, las cámaras de la red y las claves
// de los plugins), con las guías para conectar cada plataforma (net/AccountGuides).
//
// En QML las cuentas siguen en Studio (Studio.youtube, Studio.twitch, Studio.facebook);
// lo propio del área en Studio.cuentas (accountGuide, saveAccountCredential).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>

namespace ba::app {
class AppRuntime;
class VentanillaMotor;
class YouTubeViewModel;
class TwitchViewModel;
class FacebookViewModel;
} // namespace ba::app
namespace ba::app::control {
class BusEventos;
}
namespace ba::app::red {
class Red;
}
namespace ba::app::plugins {
class Plugins;
}

namespace ba::app {

class CuentasViewModel : public QObject {
    Q_OBJECT
   public:
    /// Lo que se arma después de las cuentas se pide cada vez.
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla; ///< salir al aire con cada plataforma (F3-03)
        std::function<control::BusEventos*()> bus;  ///< cada vivo, al bus
        std::function<red::Red*()> red;             ///< las credenciales de las cámaras de la red
        std::function<plugins::Plugins*()> plugins; ///< las claves de los plugins
    };
    explicit CuentasViewModel(Partes p, QObject* parent = nullptr);
    ~CuentasViewModel() override;

    YouTubeViewModel& youtube() { return *youtube_; }
    TwitchViewModel& twitch() { return *twitch_; }
    FacebookViewModel& facebook() { return *facebook_; }
    QObject* youtubeQml() const;
    QObject* twitchQml() const;
    QObject* facebookQml() const;

    /// «Dónde verlo en vivo» de la página web del evento: {youtube, twitch, facebook}.
    nlohmann::json paraLaWeb() const;

    /// Pasos para conectar una plataforma (misma fuente que el MCP: AccountGuides).
    Q_INVOKABLE QVariantMap accountGuide(const QString& platform) const;
    /// Guarda la credencial de una plataforma desde el formulario seguro: campos
    /// del formulario, {"importFile": ruta} o {"fromDownloads": true}. También las de
    /// las cámaras de la red («camara:<ip>», «camaras:<ip>,<ip>») y las claves de un
    /// plugin («plugin:<id>»). "" o el error.
    Q_INVOKABLE QString saveAccountCredential(const QString& platform, const QVariantMap& values);

   signals:
    /// Cambió algo que muestra la página web del evento (el vivo, el canal).
    void webCambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    Partes p_;
    std::unique_ptr<YouTubeViewModel> youtube_;
    std::unique_ptr<TwitchViewModel> twitch_;
    std::unique_ptr<FacebookViewModel> facebook_;
};

} // namespace ba::app
