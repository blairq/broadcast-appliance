// ============================================================================
// src/app/ui/Seguidor.hpp — analizar las caras de una cámara y seguir a una
//
// Lo usa la ventana del PTZ (cards/PtzContent.qml) y el asistente (ptz_caras_ubicar,
// ptz_seguir):
//   - analizar(entrada): las caras que hay ahora, dónde (0..1 del cuadro) y,
//     si la reconoce, de quién (la libreta de Personas);
//   - seguir(entrada, objetivo): mantiene a esa cara en el centro.
//
// CÓMO SIGUE. En el PIPELINE de la entrada: la tarjeta Rostros (barostros) ve
// las caras, la tarjeta Seguimiento (baseguir) elige a quién y decide el
// encuadre, y el PTZ digital (baptz) va hacia ahí; se hablan con eventos de
// GStreamer («ba/rostros», «ba/encuadre», «ba/ptz»), en cualquier orden. Seguir
// a alguien es decirle a la tarjeta Seguimiento a quién (`objetivo`): por
// persona (la vuelve a encontrar si se cruza con otra), por un punto de la
// imagen (un clic), la más grande o la del centro.
// Con una cámara MOTORIZADA (VISCA, ONVIF), que no está en el pipeline, la
// tarjeta avisa por el bus («ba-seguir») y acá se manda un PULSO de pan/tilt
// proporcional a lo que falta hasta el centro (uno continuo se pasa de largo:
// entre que se mueve y la imagen llega pasan cientos de ms).
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
namespace video {
class VideoGrafos;
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
    /// Sigue a `objetivo`: {id} (una persona reconocida), {cara} (el número que le
    /// puso Rostros), {regla: "mayor"|"centro"} o {x, y} (la cara más cercana a ese
    /// punto de la TOMA entera, 0..1).
    Q_INVOKABLE QString seguir(const QString& entrada, const QVariantMap& objetivo);
    /// Un clic en la imagen de la entrada (u, v en 0..1 de lo que se VE, después
    /// del PTZ digital): sigue a la cara más cercana a ese punto.
    Q_INVOKABLE QString seguirPunto(const QString& entrada, double u, double v);
    void setGrafos(video::VideoGrafos* g) { grafos_ = g; }
    Q_INVOKABLE void dejar(const QString& entrada);

   signals:
    void cambio();
    void analizado(const QString& entrada, const QString& error);

   private:
    struct Estado;
    /// Lo que dice la tarjeta Seguimiento de una entrada (su mensaje «ba-seguir»).
    void alSeguir(const QString& entrada, const std::string& json);
    void corregir(const QString& entrada, Estado& e, double cx, double cy, double alto);

    QPointer<PersonasViewModel> personas_;
    QPointer<ptz::ControlCamaras> camaras_;
    video::VideoGrafos* grafos_ = nullptr;
    int escucha_ = 0; ///< motor::escucharElementos
    QVariantMap caras_;
    std::map<QString, std::unique_ptr<Estado>> seguidas_;
};

} // namespace ba::app
