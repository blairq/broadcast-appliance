// ============================================================================
// src/app/agent/Voz.hpp — hablarle al aparato: push-to-talk -> texto -> asistente
//
// LA DECISION QUE ORDENA TODO (docs/AUDIO-TTS.md): es para quien está sentado
// en la cabina. Hablar es OTRA FORMA DE ESCRIBIR en la barra del asistente:
// el texto entra por ahí, con el contexto de la página, y hereda la misma
// política de aprobación. Sin palabra de activación: push-to-talk.
//
// LA TECLA HACE DOS COSAS A LA VEZ. Mientras está apretada, el micrófono del
// operador (OperatorFeed) va al reconocedor y el lobby recibe silencio: el
// invitado no puede oír lo que se le pide al asistente. Al soltarla, vuelve.
//
// LA TECLA. F9 por defecto, mientras la ventana de la app tiene el foco, y el
// botón del micrófono de la barra (mantener apretado). Se atrapa con un filtro
// de eventos de la aplicación: anda en cualquier página sin pelear con el foco
// de la QML, y las repeticiones del teclado no cuentan.
//
// LOS MODELOS se bajan a pedido (Configuración -> Modelos locales): un
// reconocedor (tipo "voz") y Silero VAD ("vad"). Apenas están los dos, se
// cargan solos.
//
// Y CONTESTA HABLANDO: una voz de piper (tipo "tts") elegida en la interfaz,
// frase por frase y SOLO al monitoreo (audio/MonitorVoz). Si el operador
// aprieta la tecla, se calla: está hablando él.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace ba::app {
class AppRuntime;
namespace audio {
class Escucha;
class Habla;
class MonitorVoz;
} // namespace audio
} // namespace ba::app

namespace ba::app::agent {

class ModelosLocales;

class Voz : public QObject {
    Q_OBJECT
    /// Si esta compilación trae el motor de voz (sherpa-onnx).
    Q_PROPERTY(bool disponible READ disponible CONSTANT)
    /// Modelos cargados: se puede hablar.
    Q_PROPERTY(bool lista READ lista NOTIFY cambio)
    Q_PROPERTY(bool grabando READ grabando NOTIFY cambio)
    Q_PROPERTY(bool transcribiendo READ transcribiendo NOTIFY cambio)
    /// Qué falta o qué pasó, para mostrarlo tal cual.
    Q_PROPERTY(QString estado READ estado NOTIFY cambio)
    Q_PROPERTY(QString ultimo READ ultimo NOTIFY cambio)
    Q_PROPERTY(QString tecla READ tecla CONSTANT)
    // --- Contestar hablando ---------------------------------------------------
    /// Las voces del catálogo: {repo, nombre, acento, genero, calidad, instalado, elegida}.
    Q_PROPERTY(QVariantList voces READ voces NOTIFY cambio)
    Q_PROPERTY(QString vozElegida READ vozElegida NOTIFY cambio)
    /// Hablante dentro de la voz (algunas traen dos) y cuántos hay.
    Q_PROPERTY(int hablante READ hablante WRITE setHablante NOTIFY cambio)
    Q_PROPERTY(int hablantes READ hablantes NOTIFY cambio)
    /// 0.7 (más lento) … 1.5 (más rápido).
    Q_PROPERTY(double velocidad READ velocidad WRITE setVelocidad NOTIFY cambio)
    Q_PROPERTY(bool vozLista READ vozLista NOTIFY cambio)
    Q_PROPERTY(bool hablando READ hablando NOTIFY cambio)

   public:
    Voz(AppRuntime& rt, ModelosLocales& modelos, QObject* parent = nullptr);
    ~Voz() override;

    bool disponible() const;
    bool lista() const { return lista_; }
    bool grabando() const { return grabando_; }
    bool transcribiendo() const { return transcribiendo_; }
    QString estado() const { return estado_; }
    QString ultimo() const { return ultimo_; }
    QString tecla() const { return "F9"; }

    Q_INVOKABLE void apretar();
    Q_INVOKABLE void soltar();
    Q_INVOKABLE void cancelar();

    QVariantList voces() const;
    QString vozElegida() const { return vozElegida_; }
    int hablante() const { return hablante_; }
    void setHablante(int h);
    int hablantes() const { return hablantes_; }
    double velocidad() const { return velocidad_; }
    void setVelocidad(double v);
    bool vozLista() const { return vozLista_; }
    bool hablando() const { return hablando_; }
    /// Elegir la voz que contesta (tiene que estar bajada).
    Q_INVOKABLE void elegirVoz(const QString& repo);
    /// Decir algo por los auriculares del operador (solo monitoreo).
    Q_INVOKABLE void decir(const QString& texto);
    /// Una frase de prueba con la voz elegida.
    Q_INVOKABLE void probarVoz();
    Q_INVOKABLE void callar();

   signals:
    void cambio();
    /// Lo que dijo el operador, para la barra del asistente.
    void dictado(const QString& texto);
    void notify(const QString& text, const QString& kind);

   protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

   private:
    void revisarModelos();
    void revisarVoz();
    void setEstado(const QString& e);
    void leerAjustes();
    void guardarAjustes() const;

    AppRuntime& rt_;
    ModelosLocales& modelos_;
    std::unique_ptr<audio::Escucha> escucha_;
    QString cargados_; ///< los repos con los que se cargó (para recargar si cambian)
    bool lista_ = false;
    bool grabando_ = false;
    bool transcribiendo_ = false;
    QString estado_;
    QString ultimo_;

    std::unique_ptr<audio::Habla> habla_;
    std::unique_ptr<audio::MonitorVoz> monitor_;
    QString vozElegida_;
    QString vozCargada_;
    int hablante_ = 0;
    int hablantes_ = 0;
    double velocidad_ = 1.0;
    bool vozLista_ = false;
    bool hablando_ = false;
    int pendientes_ = 0; ///< frases encargadas que todavía no terminaron
};

} // namespace ba::app::agent
