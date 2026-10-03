// src/app/red/Red.hpp — encontrar equipos de video en la red y conectarlos bien
//
// ESCANEAR (todo en paralelo, sin trabar la interfaz):
//   1. ONVIF: WS-Discovery por multicast (ptz/OnvifBusqueda).
//   2. La subred de cada placa (hasta /24): conexión TCP a los puertos típicos
//      de video (RTSP 554/8554, web 80/88/8080, SDK de Hikvision 8000 y de
//      Dahua 37777, RTMP 1935, NDI 5960).
//   3. La tabla ARP del sistema: la MAC de cada uno y su fabricante (OUI).
//   4. La página web: el encabezado Server y el título.
//   5. La marca (red/Biblioteca) y sus URLs RTSP típicas, probadas con un
//      DESCRIBE (con la clave guardada de ese equipo, si hay).
// Cada equipo sale con lo que se sabe y el SIGUIENTE PASO concreto.
//
// CREDENCIALES: por equipo, cifradas (Credenciales/camara-<ip>). Nunca se
// devuelven: solo si hay.
//
// FICHA (con credenciales): ONVIF completo —el equipo, los perfiles de video
// (códec, tamaño, fps, bitrate y su URL exacta), el PTZ (rangos, presets,
// posición de inicio), la imagen (brillo, infrarrojo, exposición, foco)— y el
// análisis de GStreamer del stream que conviene (lo que de verdad manda). De
// ahí sale el PLAN (red/Plan).
//
// AGREGAR: la entrada de red sincronizada (video/VideoGrafos), con la
// normalización que haga falta, y el PTZ ONVIF configurado.
#pragma once

#include <nlohmann/json.hpp>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "red/Biblioteca.hpp"

class QNetworkAccessManager;

namespace ba::app {
namespace video {
class VideoGrafos;
}
namespace ptz {
class ControlCamaras;
}
} // namespace ba::app

namespace ba::app::red {

class Red : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool escaneando READ escaneando NOTIFY cambio)
    /// El último escaneo, para la interfaz (lo mismo que red_escanear).
    Q_PROPERTY(QVariantList equipos READ equiposQml NOTIFY cambio)
    Q_PROPERTY(QString progreso READ progreso NOTIFY cambio)

   public:
    using Listo = std::function<void(nlohmann::json)>;

    Red(const std::filesystem::path& datos, const QString& dirCredenciales, video::VideoGrafos* video,
        ptz::ControlCamaras* camaras, QObject* parent = nullptr);
    ~Red() override;

    bool escaneando() const { return escaneando_; }
    QString progreso() const { return progreso_; }
    QVariantList equiposQml() const;
    /// El último escaneo: [{ip, …}].
    const nlohmann::json& equipos() const { return equipos_; }
    nlohmann::json equipo(const std::string& ip) const;

    /// Escanea (unos 10 s). `listo` recibe {equipos, subredes, segundos}.
    void escanear(Listo listo);
    Q_INVOKABLE void escanear() {
        escanear([](nlohmann::json) {});
    }

    // --- Credenciales (cifradas, por equipo) ---------------------------------
    bool guardarClave(const std::string& ip, const QString& usuario, const QString& clave);
    bool tieneClave(const std::string& ip) const;
    bool clave(const std::string& ip, QString& usuario, QString& clave) const;
    /// Para la interfaz: guarda y vuelve a probar sus URLs. "" o el error.
    Q_INVOKABLE QString guardarCredenciales(const QString& ip, const QString& usuario, const QString& clave);

    /// Vuelve a probar las URLs de un equipo (con su clave si hay).
    void probarUrls(const std::string& ip, Listo listo);
    /// La ficha ONVIF + análisis + plan. Hace falta la clave si ONVIF la pide.
    void ficha(const std::string& ip, Listo listo);
    Q_INVOKABLE void pedirFicha(const QString& ip);
    /// Lo último de ficha() para la interfaz.
    Q_INVOKABLE QVariantMap fichaDe(const QString& ip) const;
    /// Analiza una URL con GStreamer (GstDiscoverer): códec, tamaño, fps,
    /// entrelazado, colorimetría, audio. En otro hilo.
    void analizar(const QString& uri, Listo listo);
    /// Agrega la cámara al estudio según `opciones` ({nombre, escena, url |
    /// perfil, normalizar, latencia_ms, ptz}). `hecho` dice qué se hizo.
    std::string agregar(const std::string& ip, const nlohmann::json& opciones, nlohmann::json* hecho);
    Q_INVOKABLE QString agregarDesdeUi(const QString& ip, const QString& nombre, const QString& escena);
    /// Las entradas que ya toman video de esta cámara: [{entrada, url}].
    Q_INVOKABLE QVariantList entradasDe(const QString& ip) const;
    /// Solo el PTZ de una entrada que ya existe (la clave llegó después). "" o el error.
    Q_INVOKABLE QString ptzDesdeUi(const QString& ip, const QString& entrada);
    /// La misma entrada (sin otro stream) en otra escena. "" o el error.
    Q_INVOKABLE QString reusarDesdeUi(const QString& ip, const QString& escena);
    /// Si la entrada es una cámara de red: {ip, marca, modelo, onvif, ptz (true/false/null = no se sabe),
    /// tieneClave, usuario, notas}. Vacío si no lo es. Para la página PTZ.
    Q_INVOKABLE QVariantMap camaraDeEntrada(const QString& entrada) const;
    /// El PTZ real de una cámara de red desde la página PTZ: guarda usuario y clave (si vienen), la
    /// consulta por ONVIF y, si tiene PTZ, lo configura. El resultado llega por `ptzListo`.
    Q_INVOKABLE void activarPtzReal(const QString& entrada, const QString& usuario, const QString& clave);

    Biblioteca& biblioteca() { return biblioteca_; }
    const Oui& oui() const { return oui_; }

   signals:
    void cambio();
    void ptzListo(const QString& entrada, bool ok, const QString& texto);

   private:
    struct Escaneo;
    void sondearRtsp(const std::string& ip, nlohmann::json candidatas, Listo listo);
    void terminarBarrido();
    void reconocer(const std::string& ip);
    void trabajoListo();
    void cerrarEscaneo();
    nlohmann::json siguientePaso(const nlohmann::json& e) const;

    video::VideoGrafos* video_;
    QPointer<ptz::ControlCamaras> camaras_;
    QString dirCred_;
    Biblioteca biblioteca_;
    Oui oui_;
    QNetworkAccessManager* http_ = nullptr;
    bool escaneando_ = false;
    QString progreso_;
    nlohmann::json equipos_ = nlohmann::json::array();
    std::map<std::string, nlohmann::json> fichas_;
    std::unique_ptr<Escaneo> esc_;
};

} // namespace ba::app::red
