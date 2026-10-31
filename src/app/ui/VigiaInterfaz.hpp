// ============================================================================
// src/app/ui/VigiaInterfaz.hpp — el vigía del bucle de eventos, puesto en Qt
//
// La parte de Qt de diag/VigiaBucle (que es sin Qt y se testea con el reloj
// inyectado): el temporizador de 50 ms (preciso, para que su propio retraso sea
// el del bucle y no el redondeo de Qt) y un filtro de eventos en la aplicación
// que anota cada evento que el hilo de la interfaz despacha (la clase del
// destino y el tipo: sin armar textos, son miles por segundo). La herramienta
// MCP en curso la anota McpCore. Lo que mide sale en `sistema_recursos` y en
// Studio.latidoUi() (`bucle_ui`).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

namespace ba::app {

class VigiaInterfaz : public QObject {
   public:
    /// Se instala en `app` (el hilo de la interfaz) y empieza a medir.
    explicit VigiaInterfaz(QObject* app);

   protected:
    bool eventFilter(QObject* destino, QEvent* e) override;

   private:
    QTimer timer_;
};

} // namespace ba::app
