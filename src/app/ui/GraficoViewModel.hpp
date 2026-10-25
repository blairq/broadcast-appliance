// ============================================================================
// src/app/ui/GraficoViewModel.hpp — el editor de gráficos, para la interfaz
//
// La misma máquina que usa el asistente por MCP, con cara de pantalla: un
// motor, dos caras. Si fueran dos motores distintos, cada uno depuraría un
// fantasma diferente.
//
// Lo que le da a QML:
//   * la lista de gráficos y el que está abierto (su fuente y sus parámetros);
//   * una vista previa en el instante que se pida, como QImage (se muestra con
//     ImagenViva: ver ahí por qué no una Image con un data: URI);
//   * la reproducción (play), que la lleva el modelo con el reloj de verdad;
//   * los medios del gráfico (GIF, videos con alfa) y cómo agregarlos;
//   * los errores, con el consejo que trae el renderizador;
//   * las fuentes de gráfico que hay en las escenas, para poder sacarlo al
//     aire desde acá.
//
// La vista previa se pide con el texto que hay en el editor, no con el archivo
// en disco: se ve lo que se está escribiendo, sin guardar en cada tecla.
// ============================================================================
#pragma once

#include <memory>

#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

class AppRuntime;
namespace graficos {
class Emisor;
}

class GraficoViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool disponible READ disponible CONSTANT)
    Q_PROPERTY(QVariantList graficos READ graficos NOTIFY listaChanged)
    Q_PROPERTY(QString abierto READ abierto NOTIFY abiertoChanged)
    /// El texto que se está editando (QML, o el JSON de la descripción).
    Q_PROPERTY(QString fuente READ fuente WRITE setFuente NOTIFY fuenteChanged)
    Q_PROPERTY(bool declarativo READ declarativo NOTIFY abiertoChanged)
    /// Lo dibuja un plugin ("" si no): no tiene texto que editar, su código es el del plugin.
    Q_PROPERTY(QString plugin READ plugin NOTIFY abiertoChanged)
    Q_PROPERTY(bool sucio READ sucio NOTIFY sucioChanged)
    /// Los parámetros que declara, con sus valores actuales.
    Q_PROPERTY(QVariantList parametros READ parametros NOTIFY abiertoChanged)
    Q_PROPERTY(QVariantMap valores READ valores NOTIFY valoresChanged)
    /// Los botones de prompt prearmado que declara su ficha (ver `acciones` en
    /// docs/funciones/GRAFICOS.md): lo que el que diseñó el gráfico ya sabe que le van a
    /// querer pedir al asistente cuando el gráfico esté en uso.
    Q_PROPERTY(QVariantList acciones READ acciones NOTIFY abiertoChanged)
    /// El cuadro de la vista previa. Con un error queda el último bueno.
    Q_PROPERTY(QImage imagen READ imagen NOTIFY imagenChanged)
    /// Vacío = la última vista previa salió bien.
    Q_PROPERTY(QString error READ error NOTIFY vistaChanged)
    Q_PROPERTY(int duracionMs READ duracionMs WRITE setDuracionMs NOTIFY vistaChanged)
    /// Dónde está parada la barra de tiempo.
    Q_PROPERTY(int momentoMs READ momentoMs WRITE setMomentoMs NOTIFY vistaChanged)
    /// Corriendo la animación en la vista previa (con el reloj de verdad).
    Q_PROPERTY(bool reproduciendo READ reproduciendo WRITE setReproduciendo NOTIFY reproduciendoChanged)
    /// Los medios del gráfico abierto: [{archivo, nombre, tipo, cuadros, fps, alfa}].
    Q_PROPERTY(QVariantList medios READ medios NOTIFY mediosChanged)
    /// Se está convirtiendo un video a cuadros.
    Q_PROPERTY(bool importando READ importando NOTIFY importandoChanged)
    /// Las fuentes «Gráfico animado» que hay en las escenas.
    Q_PROPERTY(QVariantList enEscena READ enEscena NOTIFY escenaChanged)
    /// El video en vivo que lleva (VideoEnGrafico.hpp), donde va en el momento de la barra:
    /// [{fuente, x, y, ancho, alto, alfa, arriba}] en fracciones del gráfico. La vista previa
    /// dibuja ahí un recuadro (el video no se dibuja: es en vivo).
    Q_PROPERTY(QVariantList recuadros READ recuadros NOTIFY recuadrosChanged)
    /// Ancho/alto del cuadro de la vista previa (para ubicar los recuadros encima).
    Q_PROPERTY(double aspecto READ aspecto NOTIFY imagenChanged)

   public:
    GraficoViewModel(AppRuntime& rt, graficos::Emisor& emisor, QObject* parent = nullptr);
    ~GraficoViewModel() override;

    bool disponible() const;
    QVariantList graficos() const;
    QString abierto() const;
    QString fuente() const;
    void setFuente(const QString& f);
    bool declarativo() const;
    QString plugin() const;
    bool sucio() const;
    QVariantList parametros() const;
    QVariantMap valores() const;
    QVariantList acciones() const;
    QImage imagen() const;
    QString error() const;
    int duracionMs() const;
    void setDuracionMs(int ms);
    int momentoMs() const;
    void setMomentoMs(int ms);
    bool reproduciendo() const;
    void setReproduciendo(bool r);
    QVariantList medios() const;
    bool importando() const;
    QVariantList enEscena() const;
    QVariantList recuadros() const;
    double aspecto() const;

    Q_INVOKABLE void refrescar();
    Q_INVOKABLE bool abrir(const QString& nombre);
    Q_INVOKABLE QString crear(const QString& nombre, bool declarativo);
    Q_INVOKABLE bool guardar();
    /// Cambia el valor de un parámetro y redibuja.
    Q_INVOKABLE void setValor(const QString& clave, const QVariant& valor);

    /// Vuelve a dibujar la vista previa con lo que hay en el editor.
    Q_INVOKABLE void redibujar();

    /// Agrega un GIF, una imagen, una carpeta de PNG o un video (con alfa) al
    /// gráfico abierto. El resultado llega por `notify`.
    Q_INVOKABLE void agregarMedio(const QUrl& archivo);
    Q_INVOKABLE void quitarMedio(const QString& archivo);

    /// Lo saca al aire en esa fuente de escena.
    Q_INVOKABLE QString mostrarEnEscena(qulonglong fuente, bool repetir);
    Q_INVOKABLE QString ocultarDeEscena(qulonglong fuente);

   signals:
    void listaChanged();
    void abiertoChanged();
    void fuenteChanged();
    void sucioChanged();
    void valoresChanged();
    void imagenChanged();
    void vistaChanged();
    void reproduciendoChanged();
    void mediosChanged();
    void importandoChanged();
    void escenaChanged();
    void recuadrosChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    void avanzarReproduccion();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
