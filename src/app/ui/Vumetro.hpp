// ============================================================================
// src/app/ui/Vumetro.hpp — un vúmetro dibujado en la GPU (QML: Vumetro)
//
// Un solo nodo de geometría con colores por vértice: el fondo, la barra del RMS
// por zonas (verde, amarillo, rojo), la línea del pico, la marca de retención y
// las líneas de la escala, para 1 o 2 canales. Sin Timers de JS ni un Rectangle
// por segmento: con treinta vúmetros a 30 Hz en otra pantalla, el costo es un
// puñado de vértices por cuadro. Los valores vienen del modelo (audio::Mezclador).
// ============================================================================
#pragma once

#include <QColor>
#include <QQuickItem>

namespace ba::app {

class Vumetro : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal picoL READ picoL WRITE setPicoL NOTIFY cambio)
    Q_PROPERTY(qreal picoR READ picoR WRITE setPicoR NOTIFY cambio)
    Q_PROPERTY(qreal rmsL READ rmsL WRITE setRmsL NOTIFY cambio)
    Q_PROPERTY(qreal rmsR READ rmsR WRITE setRmsR NOTIFY cambio)
    Q_PROPERTY(qreal retencionL READ retencionL WRITE setRetencionL NOTIFY cambio)
    Q_PROPERTY(qreal retencionR READ retencionR WRITE setRetencionR NOTIFY cambio)
    /// 1 (mono: una barra ancha) o 2.
    Q_PROPERTY(int canales READ canales WRITE setCanales NOTIFY cambio)
    /// La escala, en dB (de fábrica -60 … +6).
    Q_PROPERTY(qreal minDb READ minDb WRITE setMinDb NOTIFY cambio)
    Q_PROPERTY(qreal maxDb READ maxDb WRITE setMaxDb NOTIFY cambio)
    /// Dónde empiezan el amarillo y el rojo (de fábrica -18 y -6 dBFS).
    Q_PROPERTY(qreal amarilloDb READ amarilloDb WRITE setAmarilloDb NOTIFY cambio)
    Q_PROPERTY(qreal rojoDb READ rojoDb WRITE setRojoDb NOTIFY cambio)
    /// Apagado (muteado): los colores bajan.
    Q_PROPERTY(bool apagado READ apagado WRITE setApagado NOTIFY cambio)

   public:
    explicit Vumetro(QQuickItem* parent = nullptr);

    qreal picoL() const { return v_[0]; }
    qreal picoR() const { return v_[1]; }
    qreal rmsL() const { return v_[2]; }
    qreal rmsR() const { return v_[3]; }
    qreal retencionL() const { return v_[4]; }
    qreal retencionR() const { return v_[5]; }
    void setPicoL(qreal x) { poner(0, x); }
    void setPicoR(qreal x) { poner(1, x); }
    void setRmsL(qreal x) { poner(2, x); }
    void setRmsR(qreal x) { poner(3, x); }
    void setRetencionL(qreal x) { poner(4, x); }
    void setRetencionR(qreal x) { poner(5, x); }
    int canales() const { return canales_; }
    void setCanales(int c);
    qreal minDb() const { return min_; }
    qreal maxDb() const { return max_; }
    void setMinDb(qreal x);
    void setMaxDb(qreal x);
    qreal amarilloDb() const { return amarillo_; }
    qreal rojoDb() const { return rojo_; }
    void setAmarilloDb(qreal x);
    void setRojoDb(qreal x);
    bool apagado() const { return apagado_; }
    void setApagado(bool a);

    /// Dónde cae un nivel (0 abajo … 1 arriba): para la escala dibujada en QML.
    Q_INVOKABLE qreal fraccion(qreal db) const;

   signals:
    void cambio();

   protected:
    QSGNode* updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) override;

   private:
    void poner(int i, qreal x);

    qreal v_[6] = {-100, -100, -100, -100, -100, -100};
    int canales_ = 2;
    qreal min_ = -60, max_ = 6, amarillo_ = -18, rojo_ = -6;
    bool apagado_ = false;
};

} // namespace ba::app
