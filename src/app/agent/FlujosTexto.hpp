// src/app/agent/FlujosTexto.hpp — las SALIDAS DE TEXTO del sistema (Studio.flujos)
//
// Una salida de texto es un chorro de frases con nombre: hoy, lo que
// transcribe cada tarjeta «Voz a texto» de un rack (el nombre lo elige el
// operador: «Orador», «Sala»…). Cualquier parte del sistema la usa por ese
// nombre:
//   * los Disparadores («si en Orador aparece "cuarto intermedio"…»),
//   * la tarjeta «Texto a voz» (decir lo que llega, en otra voz o idioma),
//   * MCP (texto_flujos, voz_transcripcion).
// Además, cada salida puede:
//   * guardarse en archivo: Recordings/Transcripciones/<nombre>-<fecha>.txt
//     (con hora) y .srt (subtítulos con tiempos),
//   * mostrarse como subtítulo en un rótulo: el texto de una fuente de texto
//     de la escena (text_ft2_source_v2) pasa a ser la última frase.
// La configuración (archivo, rótulo) vive en la tarjeta (se guarda con el
// rack y viaja con las plantillas); esto la consulta por `setConfig`.

#pragma once

#include <QDateTime>
#include <QStringList>
#include <QTimer>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

class QFile;

namespace ba::app::agent {

class FlujosTexto : public QObject {
    Q_OBJECT
    /// [{nombre, origen, ultima, hora, cantidad, archivo, rotulo}]
    Q_PROPERTY(QVariantList flujos READ flujos NOTIFY cambio)

   public:
    struct Frase {
        std::uint64_t seq = 0; ///< creciente en todo el sistema
        QString flujo, texto, origen, hora;
        double segundos = 0; ///< duración del audio (para el .srt)
        QDateTime cuando;
    };

    FlujosTexto(std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~FlujosTexto() override;

    /// La configuración de una salida (archivo, rótulo) y de dónde viene:
    /// {fuente, salidas: {archivo, rotulo}} o null si no existe.
    using Config = std::function<nlohmann::json(const QString& flujo)>;
    void setConfig(Config c) { config_ = std::move(c); }
    /// Las salidas que existen ahora (aunque todavía no hayan dicho nada).
    using Lista = std::function<nlohmann::json()>;
    void setLista(Lista l) { lista_ = std::move(l); }

    /// Una frase nueva en la salida `flujo`. Si la corrección está prendida y
    /// la frase es de una transcripción («Voz a texto…»), espera en la cola
    /// hasta que alguien la apruebe, la corrija o se cumpla la demora.
    void publicar(const QString& flujo, const QString& texto, const QString& origen, double segundos = 0);

    // --- Corrección humana antes del aire (T44) ------------------------------------
    /// Prende o apaga la cola y su demora (ms; 0 = sin cola). Config/correccion-subtitulos.json.
    void setCorreccion(bool activa, int demoraMs);
    bool correccionActiva() const { return correccionActiva_; }
    int demoraMs() const { return demoraMs_; }
    /// Las que esperan: [{id, flujo, texto, origen, vence_ms, resta_ms}].
    nlohmann::json pendientes() const;
    /// Corrige (y suelta ya) la frase `id`; `descartar` la tira. "" o el error.
    std::string corregir(std::uint64_t id, const QString& texto, bool descartar);
    Q_INVOKABLE QVariantList pendientesUi() const;
    Q_INVOKABLE QString corregirUi(qulonglong id, const QString& texto, bool descartar = false);
    Q_INVOKABLE void setCorreccionUi(bool activa, int demoraMs) { setCorreccion(activa, demoraMs); }
    Q_INVOKABLE bool correccionActivaUi() const { return correccionActiva_; }
    Q_INVOKABLE int demoraMsUi() const { return demoraMs_; }
    /// Los archivos de transcripción de esta sesión (txt, srt, vtt).
    QStringList archivosDeSesion() const;

    QVariantList flujos() const;
    /// Las últimas `n` frases de una salida ("" = de todas).
    Q_INVOKABLE QVariantList ultimas(const QString& flujo, int n = 30) const;
    /// Las frases posteriores a `seq` (para quien las va leyendo de a poco).
    std::vector<Frase> desde(std::uint64_t seq) const;
    std::uint64_t ultimoSeq() const { return seq_; }
    /// Dónde se guardan los archivos.
    QString carpeta() const;
    /// La carpeta del evento en curso: con una, las transcripciones van a
    /// <evento>/Transcripciones (histórico, H4). Vacía = Recordings/Transcripciones.
    void setCarpetaDelEvento(std::function<std::filesystem::path()> fn) { carpetaDelEvento_ = std::move(fn); }

   signals:
    void cambio();
    void frase(const QString& flujo, const QString& texto);
    /// La misma frase con su origen («Voz a texto…», «Traducción de…»): para el rótulo de traducción.
    void fraseConOrigen(const QString& flujo, const QString& texto, const QString& origen);
    void pendientesCambio();

   private:
    std::function<std::filesystem::path()> carpetaDelEvento_;
    struct Archivo;
    struct Pendiente {
        std::uint64_t id = 0;
        QString flujo, texto, origen;
        double segundos = 0;
        qint64 venceMs = 0;
    };
    void entregar(const QString& flujo, const QString& texto, const QString& origen, double segundos);
    void soltarVencidas();
    std::deque<Pendiente> pendientes_;
    std::uint64_t proxPendiente_ = 0;
    bool correccionActiva_ = false;
    int demoraMs_ = 4000;
    QTimer* reloj_ = nullptr;
    void escribirArchivo(const Frase& f);
    void ponerRotulo(const QString& rotulo, const QString& texto);

    std::filesystem::path root_;
    Config config_;
    Lista lista_;
    std::deque<Frase> frases_; ///< las últimas de todas (tope)
    std::uint64_t seq_ = 0;
    std::map<QString, std::unique_ptr<Archivo>> archivos_;
};

} // namespace ba::app::agent
