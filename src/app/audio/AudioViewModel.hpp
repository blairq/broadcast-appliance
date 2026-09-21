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
    /// Salidas de audio del sistema para escuchar el rack: [{id, nombre}].
    Q_PROPERTY(QVariantList monitorDevices READ monitorDevices NOTIFY changed)
    /// Salida elegida ("" = sin escucha aparte).
    Q_PROPERTY(QString monitorDevice READ monitorDevice NOTIFY changed)
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
    QVariantList monitorDevices() const;
    QString monitorDevice() const;

    Q_INVOKABLE void setActive(bool on);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectSource(const QString& name);
    Q_INVOKABLE void select(const QString& node);
    /// Modulo del catalogo ("gate", "eq"...) o id LSP. "" o el error.
    Q_INVOKABLE QString addModule(const QString& moduleOrId, const QString& after = {});
    Q_INVOKABLE QString addAnalyzer();
    Q_INVOKABLE QString addGenerator();
    /// Nodo de escucha: se patchea desde donde se quiera oir.
    Q_INVOKABLE QString addMonitor();
    Q_INVOKABLE QString setMonitorDevice(const QString& id);
    Q_INVOKABLE void removeNode(const QString& node);
    Q_INVOKABLE void removeRack();
    Q_INVOKABLE QString connectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in);
    Q_INVOKABLE void disconnectPorts(const QString& from, const QString& out, const QString& to,
                                     const QString& in);
    Q_INVOKABLE void moveNode(const QString& node, double x, double y);
    Q_INVOKABLE void setBypass(const QString& node, bool bypass);
    Q_INVOKABLE QString openEditor(const QString& node);
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
