// ============================================================================
// src/app/ui/VigiaInterfaz.hpp — el vigía del bucle de eventos, puesto en Qt
//
// La parte de Qt de diag/VigiaBucle (que es sin Qt y se testea con el reloj
// inyectado): el temporizador de 50 ms (preciso, para que su propio retraso sea
// el del bucle y no el redondeo de Qt) y un filtro de eventos en la aplicación
// que anota cada evento que el hilo de la interfaz despacha (la clase del
// destino y el tipo: sin armar textos, son miles por segundo). La herramienta
// MCP en curso la anota McpCore. Lo que mide sale en `sistema_recursos` (`bucle_ui`) y
// en Recursos.
//
// Para que una traba diga en qué estaba, además:
//   - los mensajes nativos del sistema (un filtro nativo: en Windows, el número del
//     MSG), así una traba adentro de un mensaje de Windows (un bucle modal, un
//     WM_PAINT) no queda como «?»;
//   - las tres marcas de la sincronización del scene graph de cada QQuickWindow
//     (afterAnimating en la interfaz, before/afterSynchronizing en el hilo de
//     render): entre la primera y la última la interfaz está PARADA esperando al
//     hilo de render (diag::VigiaBucle::kQsg*);
//   - el vigía de las trabas (diag/VigiaTraba): pasado `diag.trabaInterfazMs`
//     saca las pilas de todos los hilos MIENTRAS sigue trabada.
// ============================================================================
#pragma once

#include "diag/VigiaTraba.hpp"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QTimer>

namespace ba::app {

class VigiaInterfaz : public QObject, public QAbstractNativeEventFilter {
   public:
    /// Se instala en `app` (el hilo de la interfaz) y empieza a medir.
    explicit VigiaInterfaz(QObject* app);
    ~VigiaInterfaz() override;

   protected:
    bool eventFilter(QObject* destino, QEvent* e) override;
    bool nativeEventFilter(const QByteArray& tipo, void* mensaje, qintptr* resultado) override;

   private:
    /// Las marcas del scene graph, una vez por ventana (al mostrarse).
    void engancharVentana(QObject* ventana);

    QTimer timer_;
    diag::VigiaTraba traba_;
};

} // namespace ba::app
