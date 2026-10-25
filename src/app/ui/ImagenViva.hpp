// ============================================================================
// src/app/ui/ImagenViva.hpp — una imagen que cambia seguido, sin parpadeo
//
// `ImagenViva { imagen: Studio.grafico.imagen }`
//
// POR QUÉ NO UNA Image. La vista previa del editor de gráficos se mostraba con
// una Image cuyo `source` era un data: URI con el PNG adentro, reasignado en
// cada cuadro. Cada `source` nuevo es una carga nueva: la Image se vacía
// mientras decodifica (en Qt 6.6 no hay «retener mientras carga») y vuelve a
// aparecer, o sea un cuadro en blanco entre dos dibujos. Al darle play eran
// doce cargas por segundo: el parpadeo. Encima, cada cuadro se comprimía a PNG
// y se pasaba a base64 para que la Image lo descomprimiera.
//
// Esto toma la QImage tal cual la tiene el modelo (propiedad con NOTIFY) y la
// sube como textura en el mismo nodo del scene graph: el cuadro viejo queda
// hasta que el nuevo está, sin cargas ni conversiones.
// ============================================================================
#pragma once

#include <QImage>
#include <QQuickItem>

namespace ba::app {

class ImagenViva : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QImage imagen READ imagen WRITE setImagen NOTIFY imagenChanged)
    /// Entera con barras (true, de fábrica) o estirada a la caja.
    Q_PROPERTY(bool ajustar READ ajustar WRITE setAjustar NOTIFY ajustarChanged)
    Q_PROPERTY(bool hayImagen READ hayImagen NOTIFY imagenChanged)

   public:
    explicit ImagenViva(QQuickItem* padre = nullptr);

    QImage imagen() const { return imagen_; }
    void setImagen(const QImage& i);
    bool ajustar() const { return ajustar_; }
    void setAjustar(bool a);
    bool hayImagen() const { return !imagen_.isNull(); }

   signals:
    void imagenChanged();
    void ajustarChanged();

   protected:
    QSGNode* updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) override;

   private:
    QImage imagen_;
    bool ajustar_ = true;
    bool cambio_ = false;
};

} // namespace ba::app
