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
// DE DONDE SALE LA VOZ: de las fuentes con un nodo «Voz a texto» en modo
// dictado (sección Audio, como una salida). Al apretar se junta lo de todas y
// se transcribe la que más habló. Si ninguna tiene el nodo, se le pone solo al
// micrófono del operador (Configuración -> Audio y voz). Sin racks de audio,
// se toma el micrófono del operador directo.
//
// TRANSCRIPCIÓN CONTINUA: las fuentes con el nodo en modo «continuo» se
// transcriben sin parar (subtítulos, acta): frase por frase, 1-3 s después.
// Lo último queda en `transcripcion` y por MCP.
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

#include <nlohmann/json.hpp>

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ba::app {
class AppRuntime;
namespace audio {
class Escucha;
class Habla;
class MonitorVoz;
class RackManager;
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
    // --- Micrófono y transcripción ----------------------------------------------
    /// El micrófono del operador (la misma fuente que oye el lobby).
    Q_PROPERTY(QString microfono READ microfono NOTIFY cambio)
    /// Fuentes con voz a texto: [{fuente, modo}].
    Q_PROPERTY(QVariantList fuentesTexto READ fuentesTexto NOTIFY cambio)
    /// Lo último transcripto en continuo: [{fuente, texto, hora, segundos}], lo más nuevo al final.
    Q_PROPERTY(QVariantList transcripcion READ transcripcion NOTIFY transcripcionCambio)
    /// Lo que el modelo local propone hacer con lo dictado, esperando que el
    /// operador confirme: {frase, herramienta, titulo, prob, args, resumen,
    /// notas, bloqueos, puedeHacer}. Vacío si no hay nada pendiente.
    Q_PROPERTY(QVariantMap propuesta READ propuesta NOTIFY propuestaCambio)
    /// Lo dictado pasa primero por el modelo local (si hay uno listo).
    Q_PROPERTY(bool usarModeloLocal READ usarModeloLocal WRITE setUsarModeloLocal NOTIFY cambio)
    /// Contestar por voz (solo en los auriculares del operador): lo hecho, lo
    /// que no se pudo, y la respuesta a lo que se preguntó.
    Q_PROPERTY(bool contestarConVoz READ contestarConVoz WRITE setContestarConVoz NOTIFY cambio)
    /// La salida de los auriculares del operador (id; "" = la del sistema) y
    /// las que hay: [{dispositivo, salidas: [{id, nombre}]}].
    Q_PROPERTY(QString auriculares READ auriculares NOTIFY cambio)
    Q_PROPERTY(QVariantList salidasAudio READ salidasAudio NOTIFY cambio)
    /// Las herramientas que se hacen sin preguntar: [{herramienta, titulo}].
    Q_PROPERTY(QVariantList automaticas READ automaticas NOTIFY cambio)

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

    /// Los racks de audio (si esta compilación los tiene): de ahí sale la voz.
    void setRacks(audio::RackManager* racks);
    /// Las salidas de texto: cada frase transcripta se publica ahí con el
    /// nombre de su tarjeta.
    void setFlujos(class FlujosTexto* flujos) { flujos_ = flujos; }
    QString microfono() const;
    /// Elegir el micrófono del operador entre las fuentes con audio. Devuelve el error o "".
    Q_INVOKABLE QString setMicrofono(const QString& fuente);
    QVariantList fuentesTexto() const;
    /// Poner o quitar voz a texto en una fuente: modo "dictado", "continuo" o "" (quitar).
    Q_INVOKABLE QString setTexto(const QString& fuente, const QString& modo);
    QVariantList transcripcion() const { return transcripcion_; }
    /// Lo transcripto de una fuente (o de todas, vacío), las últimas `n` frases.
    QVariantList transcripcionDe(const QString& fuente, int n) const;
    Q_INVOKABLE void borrarTranscripcion();

    // --- Lo dictado, por el modelo local ---------------------------------------------
    // El modelo local elige la herramienta (no sus datos). Lo que elige se le
    // propone al operador; lo que el operador dejó «sin volver a preguntar» se
    // hace solo, como en los arneses de agentes. Sin datos, se hace acá mismo;
    // con datos (qué fuente, cuánto), el asistente recibe la frase con la
    // herramienta ya elegida y solo los completa. `derivar`, o sin modelo, va
    // al asistente como antes.

    /// La ficha de una herramienta del MCP (la pone McpService).
    struct FichaHerramienta {
        bool existe = false;
        QString titulo;
        bool requiereDatos = false; ///< tiene parámetros (los completa el modelo local)
        bool lectura = false;       ///< solo lee (una pregunta): se contesta con lo que devuelve
        nlohmann::json ficha;       ///< {title, description, inputSchema} como en tools/list
    };
    using Ficha = std::function<FichaHerramienta(const QString& herramienta)>;
    /// Hace una herramienta con sus argumentos: `fin(ok, texto)`.
    using Ejecutor = std::function<void(const QString& herramienta, const nlohmann::json& args,
                                        std::function<void(bool, const QString&)> fin)>;
    /// El estado del aparato para completar argumentos (escenas, fuentes, audio…).
    using Contexto = std::function<void(std::function<void(nlohmann::json)> listo)>;
    void setHerramientas(Ficha ficha, Ejecutor ejecutor, Contexto contexto);

    QVariantMap propuesta() const { return propuesta_; }
    bool usarModeloLocal() const { return usarModeloLocal_; }
    void setUsarModeloLocal(bool on);
    QVariantList automaticas() const;
    bool contestarConVoz() const { return contestarConVoz_; }
    /// La voz cargada, para otras fuentes que hablan (el teleprompter):
    /// {modelo, tokens, datos, hablante, repo}. Vacío si no hay voz.
    QVariantMap vozParaOtros() const { return vozParaOtros_; }
    QString auriculares() const;
    QVariantList salidasAudio() const;
    /// Elige los auriculares ("" = la salida del sistema). Devuelve el error o "".
    Q_INVOKABLE QString setAuriculares(const QString& id);
    void setContestarConVoz(bool on);
    /// Hacer lo propuesto; con `noVolverAPreguntar`, la herramienta pasa a automática.
    Q_INVOKABLE void confirmar(bool noVolverAPreguntar);
    /// No hacer nada con lo propuesto.
    Q_INVOKABLE void descartar();
    /// Que lo resuelva el asistente (la frase tal cual, sin la propuesta).
    Q_INVOKABLE void alAsistente();
    Q_INVOKABLE void setAutomatica(const QString& herramienta, bool on);
    /// Lo que hace con una frase dictada (también sirve para probar escribiendo).
    Q_INVOKABLE void atender(const QString& frase);
    /// Lo que le llega al MODELO LOCAL como motor de un agente o del asistente
    /// (proveedor «local»): `quien` es el rol o "asistente". Una vigilancia se
    /// dice primero en voz alta y sus acciones solo se PROPONEN (nunca se hacen
    /// solas, aunque estén entre las automáticas: el modelo toma un aviso como
    /// una orden, y «terminó el video» le sugiere cortar la grabación).
    void atenderDesde(const QString& texto, const QString& quien, bool vigilancia);

   signals:
    void cambio();
    void transcripcionCambio();
    /// Una frase nueva de la transcripción continua.
    void frase(const QString& fuente, const QString& texto);
    /// Lo que dijo el operador, para la barra del asistente.
    void dictado(const QString& texto);
    void notify(const QString& text, const QString& kind);
    void propuestaCambio();

   protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

   private:
    void revisarModelos();
    void revisarVoz();
    void revisarCorrientes();
    /// Lo que manda el rack (hilo de audio).
    void alAudio(const std::string& fuente, int modo, const float* m, std::uint32_t n, std::uint32_t f);
    void setEstado(const QString& e);
    void leerAjustes();
    void guardarAjustes() const;

    /// Completa los argumentos con el modelo local (generar) y los valida;
    /// después decide: hacer (si es automática y válida) o proponer.
    void completar(int id, const QString& frase, const QString& herramienta, const FichaHerramienta& f);
    void decidir(int id, const QString& frase, const QString& herramienta, const FichaHerramienta& f,
                 const nlohmann::json& args, const QStringList& notas, const QStringList& bloqueos);
    /// Hace la herramienta con sus argumentos (sin preguntar).
    void hacer(int id, const QString& frase, const QString& herramienta, const QString& titulo,
               const nlohmann::json& args);
    /// Lo dice por los auriculares (si está «contestar con voz») y lo registra.
    void responder(int id, const QString& texto);
    /// Una pregunta: lo que devolvió la herramienta, en una oración para decir.
    void contestarLectura(int id, const QString& frase, const QString& herramienta, const QString& datos);
    /// Atiende una frase con su origen ("dictado", "manual"…) y lo que se sepa
    /// de ella (fuente, segundos de habla, ms de transcripción).
    void atenderDe(const QString& frase, const QString& origen, const nlohmann::json& meta);
    /// Una línea en Logs/voz-modelo.jsonl: cada paso de una frase (lo
    /// reconocido, lo pedido al modelo, lo que contestó, lo propuesto, lo
    /// decidido, lo hecho), con el mismo `id`, para analizarlo después.
    void bitacora(int id, const std::string& evento, nlohmann::json datos) const;

    AppRuntime& rt_;
    ModelosLocales& modelos_;
    Ficha ficha_;
    Ejecutor ejecutor_;
    Contexto contexto_;
    std::string sistemaArgs_; ///< las reglas para completar argumentos (datos/voz-argumentos.txt)
    QVariantMap propuesta_;
    bool usarModeloLocal_ = true;
    bool contestarConVoz_ = true;
    QVariantMap vozParaOtros_;
    QStringList automaticas_;
    int ultimoId_ = 0;       ///< el `id` de la última frase atendida (bitácora)
    QSet<int> soloProponer_; ///< frases (id) cuyas acciones no se hacen solas (vigilancias)
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

    audio::RackManager* racks_ = nullptr;
    class FlujosTexto* flujos_ = nullptr;
    std::atomic<bool> tomando_{false}; ///< el hilo de audio lo lee
    std::mutex tomasLock_;
    struct Toma {
        std::vector<float> audio;
        std::uint32_t frecuencia = 48000;
        double energia = 0;
    };
    std::map<std::string, Toma> tomas_; ///< por fuente, mientras la tecla está apretada
    std::set<std::string> continuas_;   ///< las corrientes abiertas
    QVariantList transcripcion_;
};

} // namespace ba::app::agent
