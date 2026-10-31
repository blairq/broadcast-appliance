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
// EL MOTOR ES DEL PLANO DE CONTROL (F3-03, docs/arquitectura/05-motor-del-plano.md): lo
// que se muestra son PROPIEDADES que salen de la foto del motor (la parte Emisión: destinos,
// grabación, limpia, ISO, disco en apuros, remux, retornos; las Medidas: salud y loudness),
// cada una con su aviso, que sale solo si cambió su porción de la foto (los contadores de la
// señal limpia o de las ISO no despiertan a los destinos); lo que se hace se PIDE por la
// ventanilla y lo que vuelve llega como señal (notify, pedirConfirmarGrabacion,
// pedirCambioDestino, destinoGuardado): las acciones no devuelven nada. El disco que queda
// (lee el disco: puede tardar) lo pide el modelo cada tanto y llega en `disco`.
//
// Las pastillas (grabando, transmitiendo, tiempos) son de Studio.aire (ui/EstadoAireViewModel).
// En QML: Studio.emision.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class ConferenceViewModel;
class VentanillaMotor;
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
    /// Los destinos extra: [{id, nombre, url, respaldo, salida, tiene_clave, activo, reconectando, perdidos…}].
    Q_PROPERTY(QVariantList destinos READ destinosUi NOTIFY destinosCambio)
    /// Grabación ISO (cada entrada a su archivo): [{fuente, archivo, mb, audioPropio, activa, pausada}].
    Q_PROPERTY(QVariantList isos READ isosUi NOTIFY isosCambio)
    /// La señal limpia: {activa, destino, archivo, cuadros, perdidos, reconectando}.
    Q_PROPERTY(QVariantMap limpia READ limpiaUi NOTIFY limpiaCambio)
    /// La grabación (T29): {activa, archivo, copia, copia_activa, calidad, encoder, pistas,
    /// encoder_respaldo_activo} y la repetición {replay: {activa, segundos, ultimo}}.
    Q_PROPERTY(QVariantMap grabacion READ grabacionUi NOTIFY grabacionCambio)
    /// El disco en apuros (BroadcastEngine::discoEstado): {apuros, atraso_s, registro_trabado_ms,
    /// prioridad, automatica, modo, pausadas, graba_programa, desde_s}.
    Q_PROPERTY(QVariantMap apurosDisco READ apurosDiscoUi NOTIFY apurosDiscoCambio)
    /// El remux: {archivo, destino, pct, andando, ok, error}.
    Q_PROPERTY(QVariantMap remux READ remuxUi NOTIFY remuxCambio)
    /// Lo que oye cada invitado: {slot: "mix_minus" | "programa"}.
    Q_PROPERTY(QVariantMap retornos READ retornos NOTIFY retornosCambio)
    /// El disco que queda para grabar: {libre_gb, horas, kbps, carpeta, …, eventos: {libre_gb,
    /// total_gb} (el de la carpeta de los eventos)}. Lo pide el modelo al dueño cada tanto
    /// (cada 5 s grabando, cada 30 s si no) y al cambiar la grabación. Es EL modelo del disco:
    /// quien lo muestre (la tarjeta, el histórico) escucha discoCambio.
    Q_PROPERTY(QVariantMap disco READ discoUi NOTIFY discoCambio)
   public:
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla; ///< pedir y leer la foto del motor
        ConferenceViewModel& sala;   ///< la fuente de un invitado (su lugar)
    };
    explicit EmisionViewModel(Partes p, QObject* parent = nullptr);
    ~EmisionViewModel() override;

    /// Lo que oye cada invitado (modelo, Config/retornos.json): se usa en el dueño.
    emision::RetornosInvitados& retornosInvitados() { return *retornos_; }
    /// El objetivo de loudness guardado (modelo, Config/loudness.json): se usa en el dueño.
    audio::ObjetivoLoudness& objetivoLoudness() { return *objetivoLoudness_; }

    // Lo que sigue se PIDE al dueño: lo que vuelve (el error, lo que hay que confirmar)
    // llega como señal (notify, destinoGuardado, pedirConfirmarGrabacion…).
    Q_INVOKABLE void toggleRecording();
    /// Graba aunque quede poco disco (lo confirmó el operador).
    Q_INVOKABLE void grabarIgual();
    Q_INVOKABLE void startStreaming(const QString& server, const QString& key);
    Q_INVOKABLE void stopStreaming();
    /// Pasa la transmisión a otro destino estando al aire (lo confirmó el operador).
    Q_INVOKABLE void cambiarDestinoStream(const QString& server, const QString& key);
    // Destinos adicionales (respaldo, otras plataformas): comparten los codificadores.
    QVariantList destinosUi() const;
    /// Lo guarda; el resultado llega con destinoGuardado.
    Q_INVOKABLE void guardarDestinoUi(const QVariantMap& d);
    Q_INVOKABLE void borrarDestinoUi(const QString& id);
    Q_INVOKABLE void iniciarDestinoUi(const QString& id);
    Q_INVOKABLE void detenerDestinoUi(const QString& id);
    QVariantMap discoUi() const;
    /// Lo mismo, en JSON (para los demás modelos: el histórico).
    const nlohmann::json& disco() const { return disco_; }
    /// La carpeta de los eventos (su disco va en `disco.eventos`).
    void setRaizEventos(const std::filesystem::path& raiz);
    /// Pide ya el disco que queda (lee el disco, en el dueño): llega en `disco`. El modelo
    /// lo pide solo cada tanto; esto es para quien lo necesita al día (el MCP).
    void pedirDisco();
    QVariantMap remuxUi() const;
    QVariantMap apurosDiscoUi() const;
    /// Darle el disco a la grabación del programa (pausa ISO, repetición, registro) o devolverlo.
    Q_INVOKABLE void priorizarProgramaUi(bool si);
    /// "preguntar", "auto" o "nunca".
    Q_INVOKABLE void modoDiscoUi(const QString& modo);
    QVariantList isosUi() const;
    /// Graba o deja de grabar una fuente aparte (chequea el disco y el tope).
    Q_INVOKABLE void isoUi(const QString& fuente, bool grabar);
    /// Grabar aparte la fuente de un invitado de la sala (id): avisa el resultado.
    Q_INVOKABLE void isoInvitadoUi(const QString& id);
    /// La fuente de un invitado de la sala («Invitado N» por su lugar). "" si no tiene.
    Q_INVOKABLE QString fuenteDeInvitado(const QString& participante) const;
    /// Todas las de video de la escena al aire (true) o ninguna (false). Los errores, con `notify`.
    Q_INVOKABLE void isoTodasUi(bool grabar);
    QVariantMap limpiaUi() const;
    /// La arranca (a `destino`, id de un destino guardado, o a disco si vacío) o la corta.
    Q_INVOKABLE void limpiaActivarUi(bool activar, const QString& destino);
    QVariantMap grabacionUi() const;
    /// El loudness de lo que sale por `salida` ("" = la por omisión; el Mezclador pasa la
    /// activa): {m, s, i, tp, lra, segundos, gr_db, objetivo, fuera_de_objetivo}. De las
    /// medidas de la foto (la última vuelta del motor): se lee cuando se lo necesita.
    Q_INVOKABLE QVariantMap loudnessUi(const QString& salida = {}) const;
    Q_INVOKABLE void resetLoudnessUi(const QString& salida = {});
    /// {perfil: r128|a85|plataforma16|plataforma14|custom, lufs?, tp?} de esa salida.
    Q_INVOKABLE void objetivoLoudnessUi(const QVariantMap& o, const QString& salida = {});
    /// Lo que oye el invitado del slot: "mix_minus" (el programa sin su voz) o "programa"
    /// (en QML: retornos[slot]).
    QString retornoInvitadoUi(int slot) const;
    Q_INVOKABLE void setRetornoInvitadoUi(int slot, const QString& modo);
    QVariantMap retornos() const;

    QVariantList saludSalidas() const;
    /// La salud de ahora (la de la propiedad, en JSON).
    nlohmann::json saludActual() const { return saludActual_; }
    /// La serie de un destino en los últimos `minutos` (de a un punto por segundo).
    nlohmann::json saludSerie(const std::string& destino, int minutos) const;
    /// Una vez por segundo: la salud de cada destino (de la foto), a la propiedad y al bus.
    void muestrearSalud(control::BusEventos& bus);

   signals:
    void saludChanged();
    /// Cambió la LISTA de destinos (no su estado de cada vuelta): la fila de cada salida.
    void destinosChanged();
    // Cada propiedad de la parte Emisión, cuando cambia su porción de la foto.
    void destinosCambio();
    void isosCambio();
    void limpiaCambio();
    void grabacionCambio();
    void apurosDiscoCambio();
    void remuxCambio();
    void retornosCambio();
    void discoCambio();
    /// Volvió guardarDestinoUi: "" o el error.
    void destinoGuardado(const QString& error);
    /// Se pidió transmitir a otro destino estando al aire: que el operador confirme.
    void pedirCambioDestino(const QString& actual, const QString& nuevoServer, const QString& nuevoKey);
    /// Queda muy poco disco para grabar: que el operador confirme.
    void pedirConfirmarGrabacion(double horas);
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    /// Lo que cambió en la foto del motor.
    void alCambiar(unsigned partes);
    /// Pide algo que devuelve "" o el error (que vuelve como `notify`, con `nivel`).
    void pedir(const char* que, std::function<std::string(AppRuntime&)> orden, std::function<void()> alVolver = {},
               const QString& nivel = "error");

    Partes p_;
    /// La parte Emisión de la foto anterior (para avisar solo lo que cambió).
    nlohmann::json destinosAntes_ = nlohmann::json::array(), isosAntes_ = nlohmann::json::array();
    nlohmann::json limpiaAntes_ = nlohmann::json::object(), grabacionAntes_ = nlohmann::json::object();
    nlohmann::json apurosAntes_ = nlohmann::json::object(), remuxAntes_ = nlohmann::json::object();
    nlohmann::json retornosAntes_ = nlohmann::json::object();
    QTimer discoReloj_; ///< cada cuánto se pide el disco (5 s grabando, 30 s si no)
    std::unique_ptr<emision::RetornosInvitados> retornos_;      ///< (modelo)
    std::unique_ptr<audio::ObjetivoLoudness> objetivoLoudness_; ///< (modelo)
    nlohmann::json disco_ = nlohmann::json::object(); ///< lo último que volvió de pedirDisco
    std::filesystem::path raizEventos_;
    std::uint64_t listaDestinosAntes_ = 0; ///< para avisar destinosChanged solo si cambió la lista
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
