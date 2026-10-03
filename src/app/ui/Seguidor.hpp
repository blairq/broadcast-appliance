// ============================================================================
// src/app/ui/Seguidor.hpp — analizar las caras de una cámara y seguir a una
//
// Lo usa la ventana del PTZ (cards/PtzContent.qml) y el asistente (ptz_rostros,
// ptz_seguir):
//   - analizar(entrada): las caras que hay ahora, dónde (0..1 del cuadro) y,
//     si la reconoce, de quién (la libreta de Personas);
//   - seguir(entrada, objetivo): mantiene a esa cara en el centro.
//
// CÓMO SIGUE. Cada ~300 ms mira la cámara (el reconocimiento corre en su
// hilo) y busca al objetivo: por persona si está reconocida (aunque cambie de
// lugar o se cruce con otra), si no por cercanía a donde estaba. Con el PTZ
// óptico manda un PULSO de pan/tilt proporcional a lo que falta hasta el
// centro (no un movimiento continuo: entre que la cámara se mueve y la imagen
// llega pasan cientos de ms, y uno continuo se pasa de largo); con el
// digital, corre el recorte. Una zona muerta en el medio evita que tiemble.
// Si la pierde más de 2 s, para y queda esperando a que vuelva.
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <map>
#include <memory>

namespace ba::app {
class PersonasViewModel;
namespace ptz {
class ControlCamaras;
}

class Seguidor : public QObject {
    Q_OBJECT
    /// entrada -> [{x, y, ancho, alto (0..1), id, nombre, parecido}] (lo último que se analizó)
    Q_PROPERTY(QVariantMap caras READ caras NOTIFY cambio)
    /// entrada -> {id, nombre, estado («siguiendo», «centrada», «perdida…»), x, y}
    Q_PROPERTY(QVariantMap siguiendo READ siguiendo NOTIFY cambio)
    Q_PROPERTY(bool disponible READ disponible CONSTANT)
   public:
    Seguidor(PersonasViewModel* personas, ptz::ControlCamaras* camaras, QObject* parent = nullptr);
    ~Seguidor() override;

    QVariantMap caras() const { return caras_; }
    QVariantMap siguiendo() const;
    bool disponible() const;

    /// Mira la cámara una vez. El resultado llega a `caras` (y por `analizado`).
    Q_INVOKABLE void analizar(const QString& entrada);
    /// Sigue a `objetivo`: {id} (una persona reconocida) o {x, y} (una cara sin nombre, por dónde está).
    Q_INVOKABLE QString seguir(const QString& entrada, const QVariantMap& objetivo);
    Q_INVOKABLE void dejar(const QString& entrada);

   signals:
    void cambio();
    void analizado(const QString& entrada, const QString& error);

   private:
    struct Estado;
    void vuelta(const QString& entrada);
    void corregir(const QString& entrada, Estado& e, double cx, double cy, double alto);

    QPointer<PersonasViewModel> personas_;
    QPointer<ptz::ControlCamaras> camaras_;
    QVariantMap caras_;
    std::map<QString, std::unique_ptr<Estado>> seguidas_;
};

} // namespace ba::app
