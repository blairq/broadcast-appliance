// ============================================================================
// src/app/ui/WebEventoViewModel.hpp — la página web del evento en vivo
//
// El sitio del evento abierto (Eventos/<x>/Web/, evento/EventoWeb.hpp): generarlo,
// editarlo (sitio.json y cada página), el aviso de arriba y lo que se le manda a la sala
// para que lo sirva (en qué sección va, el horario, la asistencia, las preguntas…).
//
// Lo del disco (qué páginas hay, sitio.json, la documentación pública) se lee en un hilo
// de trabajo: `web` es la caché y avisa al llegar. Lo que se publica se arma también en
// el fondo y se manda solo si cambió.
// ============================================================================
#pragma once

#include <functional>
#include <map>

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>
#include "ui/CapturaError.hpp"

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;
class CorridaViewModel;
class EventoAbierto;
class EventosListaViewModel;
class PadronViewModel;

class WebEventoViewModel : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// {hay, carpeta, paginas:[{pagina, nombre, existe}], sitio:{titulo, bajada, color, fondo,
    /// subdominio, barra, direccion_al_iniciar}, aviso}.
    Q_PROPERTY(QVariantMap web READ web NOTIFY webChanged)

   public:
    WebEventoViewModel(EventoAbierto& abierto, CorridaViewModel& corrida, PadronViewModel& padron, EventosListaViewModel& lista,
                       AppRuntime& rt, ConferenceViewModel* sala, QObject* parent = nullptr);
    ~WebEventoViewModel() override;

    void setPlataformas(std::function<nlohmann::json()> f) { plataformas_ = std::move(f); }
    void setSala(std::function<nlohmann::json()> f) { datosSala_ = std::move(f); }
    void setPublico(std::function<nlohmann::json()> preguntas, std::function<int()> enLaSala) {
        preguntas_ = std::move(preguntas);
        enLaSala_ = std::move(enLaSala);
    }

    QVariantMap web() const { return web_; }
    /// Genera el sitio en Web/. No pisa lo que ya existe salvo `sobrescribir`. {escritas, salteadas, error}.
    Q_INVOKABLE QVariantMap generarWeb(bool sobrescribir = false);
    /// Cambia sitio.json (título, bajada, color, fondo, subdominio, barra…). El error va por la señal del modelo.
    Q_INVOKABLE void setWebSitio(const QVariantMap& campos);
    /// Una página por nombre («portada», «cierre», el id/nombre/número de una sección, o un
    /// .html/.css propio). Leer es del MCP (evento_web).
    QString leerPaginaWeb(const QString& pagina) const;
    Q_INVOKABLE QString escribirPaginaWeb(const QString& pagina, const QString& contenido);
    /// Le manda a la sala lo de la página del evento (si cambió).
    Q_INVOKABLE void publicarWeb();
    /// El aviso que se ve arriba de la página («Arrancamos en 5 minutos»). `nivel`: info |
    /// urgente. Vacío lo saca. No se guarda en el evento (es del momento).
    Q_INVOKABLE void setAvisoWeb(const QString& texto, const QString& nivel = "info");
    QVariantMap avisoWeb() const;
    /// Las páginas que falten del sitio (no pisa nada): al crear, abrir y guardar.
    void asegurarBase();
    /// La sala (el gateway) volvió a arrancar: no tiene nada, se le manda todo de nuevo
    /// (republicador «evento-web» de ConferenceClient, con el «epoch» de la sala).
    void salaReiniciada();
    /// Si el abierto tiene página con «dirección al iniciar»: su subdominio (al ponerlo en curso).
    std::string subdominioAlIniciar(const std::string& carpeta) const;

   signals:
    void webChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    QString carpetaWebAbs() const;
    /// Relee lo que se muestra del sitio (en el fondo).
    void leerSitio();
    void armarEnElFondo(const QString& dir);
    void publicarCuerpo(const nlohmann::json& cuerpo);
    void cambio();

    EventoAbierto& ab_;
    CorridaViewModel& corrida_;
    PadronViewModel& padron_;
    EventosListaViewModel& lista_;
    AppRuntime& rt_;
    ConferenceViewModel* sala_ = nullptr;
    std::function<nlohmann::json()> plataformas_, preguntas_, datosSala_;
    std::function<int()> enLaSala_;

    QVariantMap web_;
    bool hayPortada_ = false;
    int genSitio_ = 0;
    qint64 arranqueMs_ = 0;
    int version_ = 1;
    QByteArray ultimo_;
    bool armando_ = false, otraVez_ = false; ///< se arma en el fondo, de a uno
    QThreadPool fondo_;                      ///< un hilo; se espera al irse
    nlohmann::json aviso_ = nlohmann::json::object();
    std::map<int, qint64> inicioReal_; ///< cuándo empezó de verdad cada sección (índice → ms)
};

} // namespace ba::app
