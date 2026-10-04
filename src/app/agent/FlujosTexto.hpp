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

    /// Una frase nueva en la salida `flujo`.
    void publicar(const QString& flujo, const QString& texto, const QString& origen, double segundos = 0);

    QVariantList flujos() const;
    /// Las últimas `n` frases de una salida ("" = de todas).
    Q_INVOKABLE QVariantList ultimas(const QString& flujo, int n = 30) const;
    /// Las frases posteriores a `seq` (para quien las va leyendo de a poco).
    std::vector<Frase> desde(std::uint64_t seq) const;
    std::uint64_t ultimoSeq() const { return seq_; }
    /// Dónde se guardan los archivos.
    QString carpeta() const;

   signals:
    void cambio();
    void frase(const QString& flujo, const QString& texto);

   private:
    struct Archivo;
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
