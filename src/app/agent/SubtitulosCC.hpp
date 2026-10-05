// SPDX-License-Identifier: GPL-2.0-or-later
//
// SubtitulosCC: subtítulos OCULTOS (closed captions) de una salida de texto
// (la transcripción o una traducción): el espectador los prende o apaga en su
// reproductor. Dos métodos:
//   - "608": CEA-608 canal 1 dentro del video de la transmisión (y de cada
//     destino adicional), con obs_output_output_caption_text2;
//   - "youtube_http": la API de subtítulos de YouTube (la URL de ingesta de
//     subtítulos del evento, de YouTube Studio), por HTTP.
// Los subtítulos quemados (cuadro «Subtítulos») son otra cosa y siguen aparte.
// Config/cc.json.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <deque>
#include <filesystem>
#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace ba::app::agent {

class FlujosTexto;

class SubtitulosCC : public QObject {
    Q_OBJECT
   public:
    /// `enviar608(texto, segundos)` -> a cuántas salidas llegó.
    SubtitulosCC(FlujosTexto& flujos, std::filesystem::path datos, std::function<int(const QString&, double)> enviar608,
                 QObject* parent = nullptr);
    ~SubtitulosCC() override;

    /// {activo, flujo, metodo, url_youtube(tiene), enviadas, ultimas[]}.
    Q_INVOKABLE QVariantMap estado() const;
    /// {flujo, metodo: 608|youtube_http|ambos, url?}. "" o el error.
    Q_INVOKABLE QString activar(const QVariantMap& cfg);
    Q_INVOKABLE void desactivar();
    /// Manda un texto de prueba por lo configurado. "" o el error.
    Q_INVOKABLE QString probar(const QString& texto);

   signals:
    void cambio();

   private:
    void alFrase(const QString& flujo, const QString& texto);
    void mandar(const QString& texto);
    void guardar() const;
    FlujosTexto& flujos_;
    std::filesystem::path datos_;
    std::function<int(const QString&, double)> enviar608_;
    std::unique_ptr<QNetworkAccessManager> red_;
    bool activo_ = false;
    QString flujo_, metodo_ = "608", url_;
    qint64 seq_ = 0;
    int enviadas_ = 0;
    std::deque<QString> ultimas_;
};

} // namespace ba::app::agent
