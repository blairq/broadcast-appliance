// src/app/agent/VozPrompter.hpp — «Voz del teleprompter»: el guion, dicho en voz alta
//
// Una fuente de audio más (como «Voz del sistema»: en Audio y en la Matriz,
// con rack y ruteo; de fábrica sin pistas, en los auriculares). Dice el texto
// cargado en el teleprompter siguiendo SUS controles: el que marca el tiempo
// es el teleprompter (velocidad, pausa, saltos, el control remoto) y la voz
// se sincroniza lo más posible.
//
// Cómo: mira el estado del gateway (posición, ppm, corriendo, desde cuándo)
// varias veces por segundo. Con eso sabe cuándo el teleprompter va a llegar a
// cada frase; sintetiza un poco antes y la agenda para que empiece justo ahí
// (MonitorVoz::reproducirEn), a la velocidad que la hace durar lo que el
// teleprompter tarda en pasarla. Pausa → se calla; sigue → retoma desde la
// palabra en la que está; salto, cambio de velocidad o de texto → corta y
// vuelve a agendar desde ahí. Cada frase es un punto de resincronización.
// Las cuentas: VozPrompterPlan (con su test).

#pragma once

#include "agent/VozPrompterPlan.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>

namespace ba::app {
class AppRuntime;
class ConferenceViewModel;
} // namespace ba::app

namespace ba::app::audio {
class Habla;
class MonitorVoz;
} // namespace ba::app::audio

namespace ba::app::agent {

class Voz;

class VozPrompter : public QObject {
    Q_OBJECT
    /// Prendida: el teleprompter suena por «Voz del teleprompter».
    Q_PROPERTY(bool activa READ activa WRITE setActiva NOTIFY cambio)
    /// Qué está haciendo, para mostrar («Esperando que el teleprompter arranque»…).
    Q_PROPERTY(QString estado READ estado NOTIFY cambio)

   public:
    VozPrompter(AppRuntime& rt, Voz& voz, ConferenceViewModel* sala, QObject* parent = nullptr);
    ~VozPrompter() override;

    bool activa() const { return activa_; }
    void setActiva(bool on);
    QString estado() const { return estado_; }

   signals:
    void cambio();

   private:
    void consultar();
    void tick();
    void reiniciar(const QString& porque);
    void agendar(int desde, int hasta, double inicioMs);
    void revisarVoz();
    void setEstado(const QString& e);
    void guardar() const;

    AppRuntime& rt_;
    Voz& voz_;
    ConferenceViewModel* sala_;
    std::unique_ptr<audio::Habla> habla_;
    std::unique_ptr<audio::MonitorVoz> monitor_;
    QTimer reloj_;
    QString vozCargada_;
    bool activa_ = false;
    bool consultando_ = false;
    QString estado_;

    // Lo que informó el gateway, y la diferencia entre su reloj y el nuestro.
    std::string texto_;
    std::vector<prompter_voz::Palabra> palabras_;
    std::vector<prompter_voz::Frase> frases_;
    prompter_voz::Estado est_;
    double offsetMs_ = 0; ///< reloj local - reloj del gateway
    bool hayEstado_ = false;

    int siguiente_ = -1; ///< la próxima frase a agendar (-1: hay que ubicarse)
    /// Palabras por segundo de la voz a velocidad 1 (se aprende al hablar).
    std::shared_ptr<std::atomic<double>> naturalPps_;
};

} // namespace ba::app::agent
