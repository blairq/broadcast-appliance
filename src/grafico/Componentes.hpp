// ============================================================================
// src/grafico/Componentes.hpp — lo que un gráfico puede usar además de QtQuick
//
// `import BA.Grafico` trae:
//
//   Medio  — un GIF animado, una imagen o una SECUENCIA DE CUADROS (una carpeta
//            con PNG, o los cuadros crudos que deja un plugin) con el tiempo
//            ATADO AL DEL GRÁFICO.
//   Torta  — gráfico de torta o de anillo con datos (qml/Torta.qml).
//   Barras — barras con datos (qml/Barras.qml).
//   Datos  — Datos.normalizar(x): la lista [{etiqueta, valor, color}] a partir
//            de lo que venga (ver normalizarDatos en Declarativo.hpp).
//
// POR QUÉ UN MEDIO PROPIO Y NO AnimatedImage NI MediaPlayer. Los dos corren con
// su propio reloj de pared (un QTimer adentro de QMovie, el reloj del sistema
// de audio): no se enteran del reloj de las animaciones que adelanta el
// renderizador (RelojAnimaciones), así que «el cuadro a los 380 ms» saldría
// distinto cada vez y la vista previa no mostraría lo que va a salir al aire.
// Medio lee su tiempo de una animación de Qt (corre con NUESTRO reloj, como un
// NumberAnimation) y de ahí saca qué cuadro va: determinista, para atrás y para
// adelante.
//
// POR QUÉ LOS VIDEOS SE IMPORTAN (y no se decodifican acá). Un WebM VP9 con alfa
// o un ProRes 4444 se pasan una vez a cuadros PNG con su alfa
// (app: graficos/Medios, con GStreamer) y acá se leen como cualquier secuencia.
// Decodificar en cada cuadro pediría buscar con precisión de cuadro adentro de
// un video en cada pedido —lento hacia atrás, que es justo lo que hace la barra
// del editor— y meter GStreamer en este proceso. Un gráfico dura segundos: los
// cuadros en disco son baratos.
// ============================================================================
#pragma once

#include <memory>

#include <QImage>
#include <QJsonArray>
#include <QObject>
#include <QQuickItem>
#include <QUrl>
#include <QVariant>

namespace ba::grafico {

class FuenteDeCuadros;

class Medio : public QQuickItem {
    Q_OBJECT
    /// El archivo (GIF, PNG, WebP, JPG) o la carpeta de la secuencia. Relativo
    /// al gráfico: «medios/bandera.gif».
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    /// En qué milisegundo del gráfico arranca. Antes de eso no se ve.
    Q_PROPERTY(int inicio READ inicio WRITE setInicio NOTIFY inicioChanged)
    /// 1 = a su velocidad; 0.5 = a la mitad.
    Q_PROPERTY(qreal velocidad READ velocidad WRITE setVelocidad NOTIFY velocidadChanged)
    /// Al terminar vuelve a empezar (true) o se queda en el último cuadro.
    Q_PROPERTY(bool bucle READ bucle WRITE setBucle NOTIFY bucleChanged)
    /// Cuadros por segundo de una carpeta de cuadros sin secuencia.json.
    Q_PROPERTY(qreal fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(Ajuste ajuste READ ajuste WRITE setAjuste NOTIFY ajusteChanged)
    /// Lo que dura el medio, en ms (0 = una imagen quieta).
    Q_PROPERTY(int duracion READ duracion NOTIFY cargado)
    Q_PROPERTY(int cuadros READ cuadros NOTIFY cargado)
    /// Vacío = se cargó bien.
    Q_PROPERTY(QString error READ error NOTIFY cargado)
    /// El milisegundo DEL MEDIO que se está mostrando (-1 = todavía no arrancó).
    Q_PROPERTY(int tiempo READ tiempo NOTIFY tiempoChanged)

   public:
    enum Ajuste { Ajustar, Llenar, Estirar };
    Q_ENUM(Ajuste)

    explicit Medio(QQuickItem* padre = nullptr);
    ~Medio() override;

    QUrl source() const { return source_; }
    void setSource(const QUrl& u);
    int inicio() const { return inicio_; }
    void setInicio(int ms);
    qreal velocidad() const { return velocidad_; }
    void setVelocidad(qreal v);
    bool bucle() const { return bucle_; }
    void setBucle(bool b);
    qreal fps() const { return fps_; }
    void setFps(qreal f);
    Ajuste ajuste() const { return ajuste_; }
    void setAjuste(Ajuste a);
    int duracion() const;
    int cuadros() const;
    QString error() const { return error_; }
    int tiempo() const { return tiempo_; }

    /// Lo llama su reloj (una animación de Qt): `ms` desde que se creó el gráfico.
    void alAvanzar(int ms);

    /// Qué milisegundo del medio corresponde al ms `t` del gráfico (-1 = no se
    /// ve todavía). Puro: es lo que prueban los tests.
    static int tiempoDelMedio(int t, int inicio, qreal velocidad, int duracion, bool bucle);

   signals:
    void sourceChanged();
    void inicioChanged();
    void velocidadChanged();
    void bucleChanged();
    void fpsChanged();
    void ajusteChanged();
    void cargado();
    void tiempoChanged();

   protected:
    void componentComplete() override;
    QSGNode* updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) override;

   private:
    void cargar();
    void elegirCuadro();

    QUrl source_;
    int inicio_ = 0;
    qreal velocidad_ = 1.0;
    bool bucle_ = true;
    qreal fps_ = 30.0;
    Ajuste ajuste_ = Ajustar;
    QString error_;
    int ahora_ = 0;   ///< ms del gráfico
    int tiempo_ = -1; ///< ms del medio
    int indice_ = -1; ///< el cuadro que se muestra
    QImage imagen_;
    bool cambio_ = false;
    std::unique_ptr<FuenteDeCuadros> fuente_;
    class Reloj;
    Reloj* reloj_ = nullptr;
};

/// `Datos.normalizar(x)` para los QML: la misma lectura que las capas declarativas.
class DatosQml : public QObject {
    Q_OBJECT
   public:
    using QObject::QObject;
    Q_INVOKABLE QVariantList normalizar(const QVariant& v) const;
};

/// Registra `BA.Grafico` (Medio, Torta, Barras, Datos). Una vez, antes del primer gráfico.
void registrarComponentes();

} // namespace ba::grafico
