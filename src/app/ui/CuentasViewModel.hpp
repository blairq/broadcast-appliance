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
// lo propio del área en Studio.cuentas (accountGuide, saveAccountCredential) y las cuentas
// de cada plataforma y los vivos de cada salida en Studio.cuentas.vivos (ui/VivosViewModel).
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
class GrabacionesModel;
class PlataformaViewModel;
class EmisionViewModel;
class VivosViewModel;
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
    /// Varias cuentas por plataforma y los vivos de cada salida (ui/VivosViewModel).
    Q_PROPERTY(QObject* vivos READ vivosQml CONSTANT)
   public:
    /// Lo que se arma después de las cuentas se pide cada vez.
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla; ///< salir al aire con cada plataforma (F3-03)
        std::function<control::BusEventos*()> bus;  ///< cada vivo, al bus
        std::function<red::Red*()> red;             ///< las credenciales de las cámaras de la red
        std::function<plugins::Plugins*()> plugins; ///< las claves de los plugins
        std::function<EmisionViewModel*()> emision; ///< el bitrate de cada destino (los vivos de las salidas)
    };
    explicit CuentasViewModel(Partes p, QObject* parent = nullptr);
    ~CuentasViewModel() override;

    YouTubeViewModel& youtube() { return *youtube_; }
    TwitchViewModel& twitch() { return *twitch_; }
    FacebookViewModel& facebook() { return *facebook_; }
    VivosViewModel& vivos() { return *vivos_; }
    QObject* vivosQml() const;
    QObject* youtubeQml() const;
    QObject* twitchQml() const;
    QObject* facebookQml() const;
    /// La plataforma por su id ("youtube", "twitch", "facebook"; acepta los alias de
    /// accounts::normalize) o nullptr.
    PlataformaViewModel* plataforma(const QString& id);

    /// «Dónde verlo en vivo» de la página web del evento: {youtube, twitch, facebook}.
    nlohmann::json paraLaWeb() const;

    /// Pasos para conectar una plataforma (misma fuente que el MCP: AccountGuides).
    Q_INVOKABLE QVariantMap accountGuide(const QString& platform) const;
    /// Guarda la credencial de una plataforma desde el formulario seguro: campos
    /// del formulario, {"importFile": ruta} o {"fromDownloads": true}. También las de
    /// las cámaras de la red («camara:<ip>», «camaras:<ip>,<ip>») y las claves de un
    /// plugin («plugin:<id>»). El error, por el aviso (quien llama desde C++ lo toma con
    /// errorDe, ui/CapturaError.hpp).
    Q_INVOKABLE bool saveAccountCredential(const QString& platform, const QVariantMap& values);

    /// El JSON del cliente de Google que piden {"importFile": ruta} o {"fromDownloads": true}
    /// (el client_secret_….json más nuevo de Descargas). Solo lee el disco: de cualquier
    /// hilo (el MCP lo lee en el pool y guarda con {"clientJson": contenido}).
    struct ArchivoDeCredencial {
        QString ruta;
        QString contenido;
        QString error; ///< "" = leído
    };
    static ArchivoDeCredencial leerArchivoDeCredencial(const QVariantMap& values);

   signals:
    /// Cambió algo que muestra la página web del evento (el vivo, el canal).
    void webCambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    /// saveAccountCredential: "" o el error.
    QString guardarCredencial(const QString& platform, const QVariantMap& values);
    void avisarError(const QString& e);

    Partes p_;
    std::unique_ptr<GrabacionesModel> grabaciones_; ///< la comparten YouTube y Facebook (antes que ellos)
    std::unique_ptr<YouTubeViewModel> youtube_;
    std::unique_ptr<TwitchViewModel> twitch_;
    std::unique_ptr<FacebookViewModel> facebook_;
    std::unique_ptr<VivosViewModel> vivos_; ///< las cuentas de cada plataforma y los vivos de cada salida
};

} // namespace ba::app
