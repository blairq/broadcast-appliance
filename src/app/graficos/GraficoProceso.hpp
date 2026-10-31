// ============================================================================
// src/app/graficos/GraficoProceso.hpp — hablar con `ba-grafico`
//
// El renderizador de gráficos corre en OTRO proceso a propósito (ver
// src/grafico/main.cpp): un gráfico es código que se está escribiendo, o sea
// código roto todo el tiempo, y no puede colgar el programa que está al aire.
//
// Esta clase es el puente. Lo único que le importa al resto de la app:
//
//   * Si el proceso no está, se levanta solo en el primer pedido.
//   * Si se muere —y se va a morir, esa es la idea—, el pedido siguiente lo
//     levanta de nuevo. Quien llama se entera por el error, no por un cuelgue.
//   * Cada pedido tiene TIEMPO LÍMITE. Un gráfico con un bucle infinito deja
//     al renderizador sin responder; pasado el límite se lo mata y se sigue.
//     Sin esto, un `while(true)` en un QML congelaría la interfaz, que es
//     justo lo que el proceso aparte venía a evitar.
//
// Es SÍNCRONO: quien pide espera. Los pedidos sueltos (las vistas previas del
// IDE y del asistente) traen la imagen en el JSON; una tira de cuadros
// (`tira`, la animación que sale al aire) los trae crudos por memoria compartida
// (platform/Segmento: el pedido lleva "shm": true), no en base64.
// ============================================================================
#pragma once

#include <memory>

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace ba::app::graficos {

/// Un cuadro de una tira.
struct Cuadro {
    int ms = 0;
    QImage imagen;
};

class GraficoProceso : public QObject {
    Q_OBJECT

   public:
    /// :param binario: ruta de `ba-grafico`. Vacía = se busca al lado de la
    ///        app (bin/64bit), que es donde lo deja el bundle.
    explicit GraficoProceso(QString binario = {}, QObject* padre = nullptr);
    ~GraficoProceso() override;

    /// true si el renderizador existe y se puede usar. Con false, la app
    /// tiene que decir que no hay gráficos en vez de fallar callada.
    bool disponible() const;
    QString binario() const;

    /// Carga el gráfico y devuelve los errores, sin dibujar. Vacío = está bien.
    /// `pedido` lleva `qml`, `fuente` o `declarativo`, y `parametros`.
    QString validar(const QJsonObject& pedido);

    /// Un cuadro en el instante `ms`. Con error, la imagen viene nula.
    QImage dibujar(QJsonObject pedido, int ms, QString* error = nullptr);

    /// Varios cuadros de una. Es lo que hace falta para juzgar una animación:
    /// con uno solo no se ve si el texto entra tarde.
    QVector<Cuadro> tira(QJsonObject pedido, const QVector<int>& momentos, QString* error = nullptr);

    /// El catálogo de capas y efectos del nivel declarativo, tal como lo
    /// declara el propio motor (así la ayuda no se desincroniza del código).
    QJsonObject repertorio(QString* error = nullptr);

    /// Cierra el proceso. El pedido siguiente lo vuelve a levantar.
    void detener();

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::graficos
