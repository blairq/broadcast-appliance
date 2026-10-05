// SPDX-License-Identifier: GPL-2.0-or-later
//
// AlarmaSonora: el sonido de los disparadores, para el operador (nunca va al
// programa: sale por el dispositivo de escucha elegido, por defecto el del
// sistema). Dos niveles:
//   - aviso: un tono doble, una vez;
//   - critico: un tono que se repite hasta que alguien reconoce la alarma.
// Los tonos se generan al arrancar (WAV en Runtime/): no hacen falta archivos.
// Volumen y dispositivo en Config/alarmas.json.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <filesystem>
#include <memory>

class QSoundEffect;

namespace ba::app::agent {

class AlarmaSonora : public QObject {
    Q_OBJECT
   public:
    explicit AlarmaSonora(std::filesystem::path datos, QObject* parent = nullptr);
    ~AlarmaSonora() override;

    /// "aviso" o "critico" ("ninguno": nada).
    void sonar(const QString& nivel);
    /// La crítica sigue sonando mientras `hay` sea true.
    void criticaActiva(bool hay);

    Q_INVOKABLE QVariantMap ajustesUi() const;
    /// {volumen 0..1, dispositivo ("" = el del sistema)}. "" o el error.
    Q_INVOKABLE QString ponerAjustesUi(const QVariantMap& a);
    Q_INVOKABLE QStringList dispositivosUi() const;
    Q_INVOKABLE void probar(const QString& nivel) { sonar(nivel); }

   private:
    void aplicar();
    void guardar() const;
    std::filesystem::path datos_;
    std::unique_ptr<QSoundEffect> aviso_, critico_;
    double volumen_ = 0.8;
    QString dispositivo_;
};

} // namespace ba::app::agent
