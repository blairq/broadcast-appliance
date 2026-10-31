// ============================================================================
// src/app/ui/EmisionViewModel.hpp — el área de la emisión: transmitir y grabar (F3-03, partición)
//
// Sale de StudioViewModel: los mandos de lo que sale de la por omisión y lo que lo rodea,
// sobre su modelo (AppRuntime y BroadcastEngine: las reglas son de ahí; acá se piden y
// se muestran):
//
//   - grabar (con el disco que queda), transmitir y cambiar de destino estando al aire;
//   - los destinos extra (respaldo, otras plataformas), la señal limpia, la grabación
//     ISO de cada entrada, el disco en apuros y el remux;
//   - el loudness de lo que sale y su objetivo (audio/ObjetivoLoudness), lo que oye
//     cada invitado (emision/RetornosInvitados);
//   - la salud de cada destino (kbps, perdidos, congestión, reconexiones), con su serie
//     de 10 minutos para la tarjeta, el MCP y el bus.
//
// Las pastillas (grabando, transmitiendo, tiempos) siguen en la raíz: son de toda la
// interfaz. En QML: Studio.emision.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class ConferenceViewModel;
} // namespace ba::app
namespace ba::app::audio {
class ObjetivoLoudness;
}
namespace ba::app::control {
class BusEventos;
}
namespace ba::app::emision {
class RetornosInvitados;
}

namespace ba::app {

class EmisionViewModel : public QObject {
    Q_OBJECT
    /// Salud de cada destino de transmisión (T27): [{id, activo, kbps, perdidos_red,
    /// perdidos_enc, congestion, reconexiones, kbps_historial[]}].
    Q_PROPERTY(QVariantList saludSalidas READ saludSalidas NOTIFY saludChanged)
   public:
    struct Partes {
        AppRuntime& rt;
        ConferenceViewModel& sala; ///< la fuente de un invitado (su lugar)
        /// Se graba ahora (lo que dicen las pastillas de la raíz).
        std::function<bool()> grabando;
    };
    explicit EmisionViewModel(Partes p, QObject* parent = nullptr);
    ~EmisionViewModel() override;

    /// Lo que oye cada invitado (modelo, Config/retornos.json).
    emision::RetornosInvitados& retornos() { return *retornos_; }
    /// El objetivo de loudness guardado (modelo, Config/loudness.json).
    audio::ObjetivoLoudness& objetivoLoudness() { return *objetivoLoudness_; }

    Q_INVOKABLE void toggleRecording();
    /// Graba aunque quede poco disco (lo confirmó el operador).
    Q_INVOKABLE void grabarIgual();
    Q_INVOKABLE void startStreaming(const QString& server, const QString& key);
    Q_INVOKABLE void stopStreaming();
    /// Pasa la transmisión a otro destino estando al aire (lo confirmó el operador).
    Q_INVOKABLE void cambiarDestinoStream(const QString& server, const QString& key);
    // Destinos adicionales (respaldo, otras plataformas): comparten los codificadores.
    Q_INVOKABLE QVariantList destinosUi() const;
    Q_INVOKABLE QString guardarDestinoUi(const QVariantMap& d);
    Q_INVOKABLE QString borrarDestinoUi(const QString& id);
    Q_INVOKABLE QString iniciarDestinoUi(const QString& id);
    Q_INVOKABLE void detenerDestinoUi(const QString& id);
    /// {libre_gb, horas, kbps, carpeta} y el remux: {archivo, destino, pct, andando, ok, error}.
    Q_INVOKABLE QVariantMap discoUi() const;
    Q_INVOKABLE QVariantMap remuxUi() const;
    /// El disco en apuros (BroadcastEngine::discoEstado): {apuros, atraso_s, registro_trabado_ms,
    /// prioridad, automatica, modo, pausadas, graba_programa, desde_s}.
    Q_INVOKABLE QVariantMap apurosDiscoUi() const;
    /// Darle el disco a la grabación del programa (pausa ISO, repetición, registro) o devolverlo.
    Q_INVOKABLE void priorizarProgramaUi(bool si);
    /// "preguntar", "auto" o "nunca". "" o el error.
    Q_INVOKABLE QString modoDiscoUi(const QString& modo);
    /// Grabación ISO (cada entrada a su archivo): [{fuente, archivo, mb, audioPropio, activa, pausada}].
    Q_INVOKABLE QVariantList isosUi() const;
    /// Graba o deja de grabar una fuente aparte (chequea el disco y el tope). "" o el error.
    Q_INVOKABLE QString isoUi(const QString& fuente, bool grabar);
    /// Grabar aparte la fuente de un invitado de la sala (id): avisa el resultado.
    Q_INVOKABLE void isoInvitadoUi(const QString& id);
    /// La fuente de un invitado de la sala («Invitado N» por su lugar). "" si no tiene.
    Q_INVOKABLE QString fuenteDeInvitado(const QString& participante) const;
    /// Todas las de video de la escena al aire (true) o ninguna (false). Devuelve los errores.
    Q_INVOKABLE QString isoTodasUi(bool grabar);
    /// La señal limpia: {activa, destino, archivo, cuadros, perdidos, reconectando}.
    Q_INVOKABLE QVariantMap limpiaUi() const;
    /// La arranca (a `destino`, id de un destino guardado, o a disco si vacío) o la corta. "" o el error.
    Q_INVOKABLE QString limpiaActivarUi(bool activar, const QString& destino);
    /// La grabación (T29): {activa, archivo, copia, copia_activa, calidad, encoder, pistas, encoder_respaldo_activo}
    /// y la repetición {replay: {activa, segundos, ultimo}}.
    Q_INVOKABLE QVariantMap grabacionUi() const;
    /// El loudness de lo que sale: {m, s, i, tp, lra, segundos, gr_db, objetivo, fuera_de_objetivo}.
    Q_INVOKABLE QVariantMap loudnessUi() const;
    Q_INVOKABLE void resetLoudnessUi();
    /// {perfil: r128|a85|plataforma16|plataforma14|custom, lufs?, tp?}. "" o el error.
    Q_INVOKABLE QString objetivoLoudnessUi(const QVariantMap& o);
    /// Lo que oye el invitado del slot: "mix_minus" (el programa sin su voz) o "programa".
    Q_INVOKABLE QString retornoInvitadoUi(int slot) const;
    Q_INVOKABLE QString setRetornoInvitadoUi(int slot, const QString& modo);

    QVariantList saludSalidas() const;
    /// La salud de ahora (la de la propiedad, en JSON).
    nlohmann::json saludActual() const { return saludActual_; }
    /// La serie de un destino en los últimos `minutos` (de a un punto por segundo).
    nlohmann::json saludSerie(const std::string& destino, int minutos) const;
    /// Una vez por segundo: la salud de cada destino, a la propiedad y al bus.
    void muestrearSalud(control::BusEventos& bus);

   signals:
    void saludChanged();
    void destinosChanged();
    /// Se pidió transmitir a otro destino estando al aire: que el operador confirme.
    void pedirCambioDestino(const QString& actual, const QString& nuevoServer, const QString& nuevoKey);
    /// Queda muy poco disco para grabar: que el operador confirme.
    void pedirConfirmarGrabacion(double horas);
    /// Algo cambió lo que sale: que las pastillas se miren ya (sin esperar el reloj).
    void refrescar();
    /// Cambió algo de lo que se graba aparte (las pastillas avisan).
    void estadoCambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    Partes p_;
    std::unique_ptr<emision::RetornosInvitados> retornos_;      ///< (modelo)
    std::unique_ptr<audio::ObjetivoLoudness> objetivoLoudness_; ///< (modelo)
    struct Salud {
        std::uint64_t bytesAntes = 0;
        int perdidosAntes = 0;
        int reconexiones = 0;
        bool reconectando = false;
        qint64 reconectandoDesde = 0;
        qint64 reconexionMs = 0; ///< lo que tardó la última reconexión
        std::deque<nlohmann::json> serie; ///< 600 puntos (10 min)
    };
    std::map<std::string, Salud> salud_;
    nlohmann::json saludActual_ = nlohmann::json::array();
    std::uint64_t saltadosAntes_ = 0;
};

} // namespace ba::app
