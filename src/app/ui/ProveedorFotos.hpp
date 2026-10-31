// ============================================================================
// src/app/ui/ProveedorFotos.hpp — image://personas/<clave>: la foto de cada conectado
//
// Las da ui/FotosPersonas (una por persona, con su versión en la URL: «?v=N», que no es
// parte de la clave). El motor de QML puede pedirlas desde otro hilo: FotosPersonas::jpeg
// tiene su candado.
// ============================================================================
#pragma once

#include <QQuickImageProvider>

namespace ba::app {

class ProveedorFotos : public QQuickImageProvider {
   public:
    ProveedorFotos() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& pedido) override;
};

} // namespace ba::app
