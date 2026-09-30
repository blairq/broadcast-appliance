// ============================================================================
// src/app/ui/GraficoViewModel.hpp — el editor de gráficos, para la interfaz
//
// La misma máquina que usa el asistente por MCP, con cara de pantalla: un
// motor, dos caras. Si fueran dos motores distintos, cada uno depuraría un
// fantasma diferente.
//
// Lo que le da a QML:
//   * la lista de gráficos y el que está abierto (su fuente y sus parámetros);
//   * una vista previa en el instante que se pida, como imagen;
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
    Q_PROPERTY(bool sucio READ sucio NOTIFY sucioChanged)
    /// Los parámetros que declara, con sus valores actuales.
    Q_PROPERTY(QVariantList parametros READ parametros NOTIFY abiertoChanged)
    Q_PROPERTY(QVariantMap valores READ valores NOTIFY valoresChanged)
    /// Los botones de prompt prearmado que declara su ficha (ver `acciones` en
    /// docs/GRAFICOS.md): lo que el que diseñó el gráfico ya sabe que le van a
    /// querer pedir al asistente cuando el gráfico esté en uso.
    Q_PROPERTY(QVariantList acciones READ acciones NOTIFY abiertoChanged)
    /// Vacío = la última vista previa salió bien.
    Q_PROPERTY(QString error READ error NOTIFY vistaChanged)
    Q_PROPERTY(int duracionMs READ duracionMs WRITE setDuracionMs NOTIFY vistaChanged)
    /// Dónde está parada la barra de tiempo.
    Q_PROPERTY(int momentoMs READ momentoMs WRITE setMomentoMs NOTIFY vistaChanged)
    /// Las fuentes «Gráfico animado» que hay en las escenas.
    Q_PROPERTY(QVariantList enEscena READ enEscena NOTIFY escenaChanged)

   public:
    GraficoViewModel(AppRuntime& rt, graficos::Emisor& emisor, QObject* parent = nullptr);
    ~GraficoViewModel() override;

    bool disponible() const;
    QVariantList graficos() const;
    QString abierto() const;
    QString fuente() const;
    void setFuente(const QString& f);
    bool declarativo() const;
    bool sucio() const;
    QVariantList parametros() const;
    QVariantMap valores() const;
    QVariantList acciones() const;
    QString error() const;
    int duracionMs() const;
    void setDuracionMs(int ms);
    int momentoMs() const;
    void setMomentoMs(int ms);
    QVariantList enEscena() const;

    Q_INVOKABLE void refrescar();
    Q_INVOKABLE bool abrir(const QString& nombre);
    Q_INVOKABLE QString crear(const QString& nombre, bool declarativo);
    Q_INVOKABLE bool guardar();
    /// Cambia el valor de un parámetro y redibuja.
    Q_INVOKABLE void setValor(const QString& clave, const QVariant& valor);

    /// Vuelve a dibujar la vista previa con lo que hay en el editor.
    Q_INVOKABLE void redibujar();
    /// La imagen de la vista previa, para mostrarla en QML (data: URI).
    Q_INVOKABLE QString vistaPreviaUrl() const;

    /// Lo saca al aire en esa fuente de escena.
    Q_INVOKABLE QString mostrarEnEscena(qulonglong fuente, bool repetir);
    Q_INVOKABLE QString ocultarDeEscena(qulonglong fuente);

   signals:
    void listaChanged();
    void abiertoChanged();
    void fuenteChanged();
    void sucioChanged();
    void valoresChanged();
    void vistaChanged();
    void escenaChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
