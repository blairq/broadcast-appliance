// ============================================================================
// src/app/graficos/Medios.hpp — los GIF, imágenes y videos de un gráfico
//
// Un gráfico puede llevar adentro un GIF animado, una imagen o un video con
// canal alfa (WebM VP9 con alfa, ProRes 4444, una secuencia de PNG), con su
// tiempo atado a la línea de tiempo del gráfico (la capa `medio` del nivel
// declarativo, o `Medio` en un QML: src/grafico/Componentes.hpp).
//
// Viven en la carpeta del gráfico, en `medios/`, así que viajan con él. Esto
// los pone ahí:
//   * un GIF, PNG, JPG o WebP se copia tal cual;
//   * una carpeta de PNG (una secuencia) se copia entera;
//   * un VIDEO se pasa una vez a cuadros PNG con su alfa (GStreamer, en otro
//     hilo), en `medios/<nombre>/` con un `secuencia.json` que dice a cuántos
//     cuadros por segundo va. El renderizador lee cuadros, no decodifica video:
//     así cualquier cuadro se pide al instante, para atrás y para adelante, que
//     es lo que hace la barra del editor (ver el porqué en Componentes.hpp).
//
// Lo usan el editor (botón «Agregar medio») y el asistente
// (grafico_medio_agregar, grafico_medios): una sola implementación.
// ============================================================================
#pragma once

#include <functional>
#include <memory>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace ba::app::graficos {

class Catalogo;

class Medios : public QObject {
    Q_OBJECT

   public:
    explicit Medios(Catalogo& catalogo, QObject* padre = nullptr);
    ~Medios() override;

    /// Los medios de un gráfico: [{archivo: "medios/x.gif", nombre, tipo:
    /// "animado"|"imagen"|"secuencia", cuadros, fps, alfa}].
    QJsonArray lista(const QString& grafico) const;

    /// Lo agrega a `<carpeta del gráfico>/medios/`. Las imágenes y las carpetas
    /// se copian en el momento; un video se convierte en otro hilo. `listo` va
    /// en el hilo de la interfaz con {archivo, tipo, cuadros, fps, alfa, ancho,
    /// alto} (lo que hay que poner en la capa) o con el error.
    ///
    /// :param opciones: {"nombre": como se llama adentro (si no, el del
    ///        archivo), "ancho": achicar los cuadros de un video a ese ancho,
    ///        "maxSegundos": cuánto de un video (20 de fábrica)}.
    void importar(const QString& grafico, const QString& archivo, const QJsonObject& opciones,
                  std::function<void(QJsonObject resultado, QString error)> listo);

    /// Lo saca del gráfico (a la papelera del gráfico: medios/.papelera). "" o el error.
    QString quitar(const QString& grafico, const QString& archivo);

    /// Hay un video convirtiéndose.
    bool importando() const;

    /// Convierte un video a cuadros PNG en `destino` (bloquea: la usan importar,
    /// en su hilo, y los tests). {cuadros, fps, alfa, ancho, alto} o el error.
    static QJsonObject videoACuadros(const QString& video, const QString& destino, int ancho, int maxSegundos,
                                     QString* error);

    static bool esVideo(const QString& archivo);

   signals:
    /// Cambiaron los medios de ese gráfico (se agregó o se quitó uno).
    void cambio(const QString& grafico);
    void importandoChanged();

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::graficos
