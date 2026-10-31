// ============================================================================
// src/app/audio/AudioViewModel.hpp — pestaña Audio: el patchbay de cada fuente
//
// Lo que ve y toca la interfaz: la lista de fuentes con audio, el grafo del
// rack de la fuente elegida (nodos con posicion, puertos y medidores; cables),
// el detalle del nodo elegido (parametros del modulo, o el analisis si es la
// entrada, la salida o un analizador) y las acciones: agregar modulos,
// conectar/desconectar cables, mover nodos, bypass, abrir la interfaz LSP.
// Mientras la pestaña esta a la vista refresca los medidores ~6 veces por segundo.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app::audio {

class RackManager;
class AudioRack;

class AudioViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    /// Estructura (nodos, puertos, posiciones, cables): cambia solo al editar.
    Q_PROPERTY(QVariantMap graph READ graph NOTIFY graphChanged)
    /// Medidores por nodo {id: {entradaDb, salidaDb, cambioDb...}}: cada tick.
    Q_PROPERTY(QVariantMap meters READ meters NOTIFY metersChanged)
    Q_PROPERTY(QString selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(QVariantList catalog READ catalog CONSTANT)
    /// Paso de la grilla del patchbay (posiciones de nodos y calles de cables).
    Q_PROPERTY(double gridStep READ gridStep CONSTANT)
    /// Salidas de audio del equipo, agrupadas por dispositivo:
    /// [{dispositivo, salidas: [{id, nombre}]}].
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY changed)
   public:
    explicit AudioViewModel(RackManager& mgr, QObject* parent = nullptr);

    bool available() const;
    QString error() const { return error_; }
    QVariantList sources() const { return sources_; }
    QString source() const { return source_; }
    QVariantMap graph() const { return graph_; }
    QVariantMap meters() const { return meters_; }
    QString selected() const { return selected_; }
    QVariantMap detail() const { return detail_; }
    QVariantList catalog() const;
    QVariantList outputDevices() const;
    double gridStep() const;

    Q_INVOKABLE void setActive(bool on);
    Q_INVOKABLE void refresh();
    /// Cambiaron las fuentes o las entradas de audio (lo avisa el runtime, lo
    /// haya pedido quien sea): la lista se rehace si quedó distinta.
    void revisarFuentes();
    Q_INVOKABLE void selectSource(const QString& name);
    /// La vista general de todo el audio (RackManager::estudio): {nodos, cables, franjas}.
    Q_INVOKABLE QVariantMap estudio() const;
    Q_INVOKABLE void select(const QString& node);
    /// Modulo del catalogo ("gate", "eq"...) o id LSP. "" o el error.
    Q_INVOKABLE QString addModule(const QString& moduleOrId, const QString& after = {});
    Q_INVOKABLE QString addAnalyzer();
    Q_INVOKABLE QString addGenerator();
    /// Tarjeta ENTRADA: trae el audio de otra fuente a este grafo.
    Q_INVOKABLE QString addEntrada(const QString& fuente);
    Q_INVOKABLE QString setEntradaFuente(const QString& node, const QString& fuente);
    /// Tarjeta TEXTO A VOZ (dice una salida de texto o lo que se le pide).
    Q_INVOKABLE QString addHabla();
    /// {flujo?, voz?, hablante?, velocidad?}
    Q_INVOKABLE QString setHablaOpciones(const QString& node, const QVariantMap& cambios);
    /// Tarjeta TRADUCIR: traduce una salida de texto a otro idioma (la usa
    /// Studio.traductor; se ajusta con Studio.traductor.ajustarUi(«<fuente>#<nodo>»)).
    Q_INVOKABLE QString addTraduccion();
    /// Grabar todo lo que le llega a la tarjeta de salida (.wav).
    Q_INVOKABLE QString setGrabar(bool grabar);
    /// {grabar, grabando, archivo, segundos, bytes, conCable, carpeta}.
    Q_INVOKABLE QVariantMap grabacion() const;
    /// Agrega (o reapunta) la salida de audio de esta fuente: una tarjeta mas
    /// en el grafo, con el nombre del dispositivo elegido.
    Q_INVOKABLE QString addOutput(const QString& device);
    /// Nodo de voz a texto en esta fuente ("dictado" o "continuo"; si ya hay, cambia el modo).
    /// Agrega OTRA voz a texto (cada una con su salida de texto con nombre).
    Q_INVOKABLE QString addTexto(const QString& modo);
    /// Cambia una voz a texto: {modo?, flujo?, salidas?: {archivo, rotulo}}.
    Q_INVOKABLE QString setTextoOpciones(const QString& node, const QVariantMap& cambios);
    /// Las fuentes de texto (para subtítulos en un rótulo).
    Q_INVOKABLE QStringList fuentesDeTexto() const;
    /// El catálogo de filtros LSP por familia (datos/lsp-catalogo.json, en
    /// castellano) con sus variantes reales: [{clave, nombre, categoria,
    /// ilustracion, vivo, esFiltro, descripcion, usoVivo, etiquetas,
    /// descripcionOriginal, variantes: [{id, etiqueta, estereo, sidechain}]}].
    Q_INVOKABLE QVariantList catalogoLsp() const;

    // --- Plantillas -------------------------------------------------------------
    Q_INVOKABLE QVariantList plantillas() const;
    Q_INVOKABLE QString guardarPlantilla(const QString& nombre);
    Q_INVOKABLE QString aplicarPlantilla(const QString& nombre);
    Q_INVOKABLE QString borrarPlantilla(const QString& nombre);
    Q_INVOKABLE QString plantillaPorDefecto(const QString& nombre, const QString& tipo);
    /// El tipo de la fuente elegida (para marcar la predeterminada).
    Q_INVOKABLE QString tipoDeFuente() const;
    /// Reordena los nodos en la grilla del patchbay.
    Q_INVOKABLE void tidy();
    Q_INVOKABLE void removeNode(const QString& node);
    Q_INVOKABLE void removeRack();
    /// Conecta una salida con una entrada. `outCh`/`inCh`: -1 = el puerto
    /// entero, 0 = canal izquierdo, 1 = derecho.
    Q_INVOKABLE QString connectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in, int outCh = -1, int inCh = -1);
    Q_INVOKABLE void disconnectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in, int outCh = -1, int inCh = -1);
    Q_INVOKABLE void moveNode(const QString& node, double x, double y);
    Q_INVOKABLE void setBypass(const QString& node, bool bypass);
    /// Saca un módulo del bypass automático del vigía.
    Q_INVOKABLE void rearmar(const QString& node);
    Q_INVOKABLE QString openEditor(const QString& node);
    /// Cierra las ventanas LSP y vuelve su configuración de pantalla a la de
    /// fábrica (con copia). Para cuando una interfaz LSP no aparece.
    Q_INVOKABLE QString restablecerInterfazLsp();
    /// Parametro del catalogo o nombre LSP; `band` >= 0 para parametros por banda.
    Q_INVOKABLE QString setParam(const QString& node, const QString& key, const QVariant& value,
                                 int band = -1);
    Q_INVOKABLE QString applyPreset(const QString& node, const QString& preset);
    /// Generador: tipo de señal, frecuencia y nivel.
    Q_INVOKABLE void setGenerator(const QString& node, const QString& kind, double freq, double levelDb);
    /// Otros plugins LSP por texto: [{id, nombre}].
    Q_INVOKABLE QVariantList search(const QString& text) const;

   signals:
    void changed();
    void graphChanged();
    void metersChanged();
    void selectionChanged();
    void detailChanged();

   private:
    AudioRack* rack(bool create = false);
    void rebuildGraph();
    void rebuildDetail();
    void autoLayout();

    RackManager& mgr_;
    QTimer tick_;
    bool active_ = false;
    QString error_;
    QVariantList sources_;
    QString source_;
    QVariantMap graph_;
    QVariantMap meters_;
    QString selected_;
    QVariantMap detail_;
};

} // namespace ba::app::audio
