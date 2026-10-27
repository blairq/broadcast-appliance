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
//
// LA VOZ es la del idioma del EVENTO (el guion es para el público): la del sistema
// si habla esa lengua; si no, la del perfil del idioma (agent/VozIdioma). Cambia
// si cambia el idioma del evento (Voz::idiomaEventoCambio). El ritmo de arranque
// de cada voz (palabras por segundo) también sale del perfil.

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
    /// Prendida pero sin poder leer (sin voz o el teleprompter no contesta): lo que
    /// mira EstadoIA, que no compara el texto del estado (sale en el idioma del operador).
    bool noPuedeLeer() const { return noPuedeLeer_; }

   signals:
    void cambio();

   private:
    void consultar();
    void tick();
    void reiniciar(const QString& porque);
    void agendar(int desde, int hasta, double inicioMs);
    /// Elige la voz (y la carga si cambió). `siempre`: aunque la del sistema sea la misma.
    void revisarVoz(bool siempre);
    void setEstado(const QString& e, bool noPuedeLeer = false);
    void guardar() const;

    AppRuntime& rt_;
    Voz& voz_;
    ConferenceViewModel* sala_;
    std::unique_ptr<audio::Habla> habla_;
    std::unique_ptr<audio::MonitorVoz> monitor_;
    QTimer reloj_;
    QString vozCargada_;
    QString vozSistema_; ///< la voz del sistema la última vez que se eligió
    bool activa_ = false;
    bool consultando_ = false;
    QString estado_;
    bool noPuedeLeer_ = false;

    // Lo que informó el gateway, y la diferencia entre su reloj y el nuestro.
    std::string texto_;
    std::vector<prompter_voz::Palabra> palabras_;
    std::vector<prompter_voz::Frase> frases_;
    prompter_voz::Estado est_;
    double offsetMs_ = 0; ///< reloj local - reloj del gateway
    bool hayEstado_ = false;

    int siguiente_ = -1; ///< la próxima frase a agendar (-1: hay que ubicarse)
    /// Palabras por segundo de la voz a velocidad 1: la del perfil al cargarla, y se
    /// aprende al hablar.
    std::shared_ptr<std::atomic<double>> naturalPps_;
};

} // namespace ba::app::agent
