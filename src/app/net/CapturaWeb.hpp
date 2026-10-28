// ============================================================================
// src/app/net/CapturaWeb.hpp — la foto de una página web del aparato
//
// Con chrome-headless-shell (el Chrome sin ventana de Chrome for Testing, en el
// bundle: bin/64bit/chrome-headless, ver tools/deps/chrome-headless.sh): la web del
// evento, la de los invitados, el teleprompter, la botonera… tal como se ven en un
// celular o en una computadora. Lo usan los manuales de usuario y el asistente para
// mirar lo que edita (web_captura).
//
// Por la línea de comandos (--screenshot), un proceso por foto: no hace falta hablarle
// por el protocolo de depuración. Su perfil vive en los datos (Cache/), nada en el
// sistema. Asincrónico: `listo` corre en el hilo de quien llamó.
// ============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

namespace ba::app {

class CapturaWeb : public QObject {
    Q_OBJECT
   public:
    struct Pedido {
        QString url;
        int ancho = 1440;
        int alto = 900;
        int esperaMs = 2500; ///< cuánto dejar correr la página (scripts, datos) antes de la foto
    };
    /// (imagen, "" | por qué no salió)
    using Listo = std::function<void(const QImage&, const QString&)>;

    explicit CapturaWeb(QObject* parent = nullptr);

    /// El ejecutable del bundle ("" si no está: el build no lo trajo).
    static QString ejecutable();
    /// ¿Se puede sacar la foto? "" o por qué no.
    static QString disponible();
    void capturar(const Pedido& p, Listo listo);
};

} // namespace ba::app
