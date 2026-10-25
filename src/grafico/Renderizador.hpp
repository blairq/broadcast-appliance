// ============================================================================
// src/grafico/Renderizador.hpp — dibuja un gráfico QML fuera de pantalla
//
// Carga un archivo QML, le pone los parámetros que le manden, adelanta el
// reloj de las animaciones hasta el instante pedido y devuelve la imagen, con
// transparencia, lista para componer sobre el video.
//
// TRANSPARENCIA. El gráfico se va a componer sobre el programa, así que el
// fondo es transparente y el suavizado del texto tiene que ser en escala de
// grises, no subpíxel: con subpíxel las letras salen con flecos de color en
// cuanto hay algo detrás que no es el gris que el sistema supuso.
//
// CADA CUADRO ARRANCA DE CERO. Para el instante `t` se carga el gráfico de
// nuevo y se avanza el reloj de una. Es más caro que dejarlo corriendo, pero
// garantiza que pedir 0, 400 y 200 ms en ese orden dé exactamente lo mismo que
// pedirlos ordenados — que es lo que hace que la vista previa sirva.
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QQuickRenderControl;
class QQuickWindow;
QT_END_NAMESPACE

namespace ba::grafico {

class RelojAnimaciones;

class Renderizador {
   public:
    Renderizador();
    ~Renderizador();

    Renderizador(const Renderizador&) = delete;
    Renderizador& operator=(const Renderizador&) = delete;

    /// Lo que salió de dibujar. `error` vacío = salió bien.
    struct Resultado {
        QImage imagen;
        QString error;
        /// Avisos que no impiden dibujar, pero que conviene arreglar (el
        /// típico: importar un módulo que en este Qt no existe).
        QStringList avisos;
    };

    /// Dibuja el gráfico en el instante `ms` de su animación.
    ///
    /// :param qml: ruta del archivo .qml.
    /// :param parametros: se asignan como propiedades del objeto raíz. Una
    ///        propiedad que el gráfico no declara es un ERROR, no un silencio:
    ///        casi siempre es un nombre mal escrito, y descubrirlo al aire es
    ///        tarde.
    /// :param tam: tamaño del lienzo.
    /// :param ms: instante de la animación.
    /// :param opcionales: como `parametros`, pero solo se asignan si el gráfico
    ///        las declara (y si no, no es error): `anteriores` y `sinEntrada`,
    ///        que el sistema le pasa a cualquier gráfico cuando le cambia los
    ///        valores al aire (ver docs/funciones/GRAFICOS.md, «Cambiar al aire»).
    Resultado dibujar(const QString& qml, const QJsonObject& parametros, QSize tam, int ms,
                      const QJsonObject& opcionales = {});

    /// Igual, pero con el QML en texto en vez de en un archivo. Es lo que usa
    /// el nivel declarativo (el QML se genera en el momento) y lo que va a
    /// usar el IDE mientras se edita, sin escribir a disco en cada tecla.
    ///
    /// :param base: dónde «vive» ese texto. Contra esto se resuelven las rutas
    ///        relativas que escriba el gráfico (`medios/bandera.gif`): la carpeta
    ///        del gráfico, aunque el texto no esté guardado todavía.
    Resultado dibujarFuente(const QString& fuente, const QJsonObject& parametros, QSize tam, int ms,
                            const QUrl& base = QUrl("grafico.qml"), const QJsonObject& opcionales = {});

    /// Solo carga el gráfico y devuelve los errores, sin dibujar. Es lo que
    /// corre antes de dejar que un gráfico llegue a una escena.
    Resultado validar(const QString& qml, const QJsonObject& parametros);

    /// Una TIRA de cuadros del mismo gráfico con UNA instancia: se crea con el primer
    /// momento y para cada uno de los siguientes se adelanta el reloj, como corre una
    /// animación de verdad (antes, cada cuadro creaba un motor QML, compilaba y
    /// dibujaba dos veces: ~350 ms por cuadro de 1920×1080). Si el pedido siguiente es
    /// el mismo gráfico (fuente, parámetros, tamaño) y empieza después del último
    /// momento, sigue con la misma instancia. `momentos` crecientes.
    /// :param url: el archivo (vacío si va `fuente`, el QML en texto).
    std::vector<Resultado> tira(const QUrl& url, const QString& fuente, const QJsonObject& parametros, QSize tam,
                                const std::vector<int>& momentos, const QJsonObject& opcionales = {});

   private:
    /// Los dos `dibujar` son el mismo camino: uno carga de archivo y el otro
    /// de texto.
    Resultado dibujarComun(const QUrl& url, const QString& fuente, const QJsonObject& parametros, QSize tam,
                           int ms, const QJsonObject& opcionales);

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::grafico
