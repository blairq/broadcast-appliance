// ============================================================================
// src/app/ui/ListModels.hpp — Modelos de lista para la vista (capa ViewModel)
//
// QAbstractListModel sobre AppRuntime. La vista (QML) solo los muestra y llama
// sus comandos; aca no hay nada de presentacion ni de QML.
//
//   ScenesModel       escenas (nombre, al aire)
//   SourcesModel      fuentes de UNA escena (la que se edita)
//   MixerModel        fuentes con audio: nivel, volumen, silencio
//   SourceTypesModel  tipos de fuente disponibles para "Agregar"
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ba::app {

class AppRuntime;

class ScenesModel : public QAbstractListModel {
    Q_OBJECT
   public:
    /// `live` = esta AL AIRE. `armada` = esta en el bus de preview, o sea la
    /// que va a salir con el proximo TAKE. Son dos cosas distintas y por eso
    /// son dos roles: una mesa de switcher las muestra en dos filas y el
    /// operador mira las dos.
    enum Roles { NameRole = Qt::UserRole + 1, LiveRole, FolderRole, ProtegidaRole, ArmadaRole };
    explicit ScenesModel(AppRuntime& rt, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// Relee del modelo (tras agregar/quitar/renombrar escenas).
    void reload();
    /// Avisa que cambio la escena al aire.
    void liveChanged();
    /// La escena ARMADA (bus de preview): la que sale con el proximo TAKE.
    /// Vacia = no hay ninguna armada.
    QString armada() const { return armada_; }
    void setArmada(const QString& n);

   private:
    AppRuntime& rt_;
    std::vector<std::string> names_;
    QString armada_;
};

class SourcesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString scene READ scene WRITE setScene NOTIFY sceneChanged)
   public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        TypeIdRole,
        TypeNameRole,
        VisibleRole,
        IsVideoRole,
        IsAudioRole,
        RectXRole,
        RectYRole,
        RectWRole,
        RectHRole
    };
    explicit SourcesModel(AppRuntime& rt, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString scene() const { return scene_; }
    void setScene(const QString& s);
    void reload();

    Q_INVOKABLE bool setVisible(const QString& name, bool visible);
    /// Una capa por posicion: {nombre, visible}. Vacio si no existe. Igual que
    /// `MixerModel::canal`: la vista no pide datos por NUMERO de rol.
    Q_INVOKABLE QVariantMap capa(int fila) const;
    Q_INVOKABLE bool move(const QString& name, int delta);
    Q_INVOKABLE bool remove(const QString& name);
    Q_INVOKABLE bool setRect(const QString& name, double x, double y, double w, double h);
    /// Transparencia, borde difuso y sombra de la fuente en esta escena (ver EfectosCapa::aJson).
    Q_INVOKABLE QVariantMap efectos(const QString& name) const;
    /// Cambia solo lo que traiga `cambios` (las mismas claves; la sombra, anidada).
    Q_INVOKABLE bool setEfectos(const QString& name, const QVariantMap& cambios);
    /// Agrega una fuente nueva de tipo `typeId` con nombre unico derivado de
    /// `baseName`. :returns: el nombre final ("" si fallo).
    Q_INVOKABLE QString add(const QString& typeId, const QString& baseName);
    /// Lo mismo, con sus ajustes ya puestos (un «Código QR» con el QR elegido, …).
    Q_INVOKABLE QString addConAjustes(const QString& typeId, const QString& baseName, const QVariantMap& ajustes);
    /// Invitado de la sala (camara o pantalla compartida del slot): reusa la
    /// fuente si ya existe en otra escena (misma fuente, otra posicion).
    Q_INVOKABLE QString addGuest(int slot, bool share);
    /// Fuentes que existen en OTRAS escenas y no estan en esta, como
    /// [{name, typeName}]. Son candidatas a compartir: una camara o una
    /// capturadora no se puede abrir dos veces, pero SI puede estar en
    /// varias escenas siendo la misma fuente.
    Q_INVOKABLE QVariantList otherSources() const;
    /// Agrega a esta escena una fuente que YA existe (la misma, compartida).
    /// :returns: su nombre ("" si fallo).
    Q_INVOKABLE QString addExisting(const QString& name);
    Q_PROPERTY(int guestSlots READ guestSlots CONSTANT)
    int guestSlots() const;

   signals:
    void sceneChanged();
    void error(const QString& message);

   private:
    AppRuntime& rt_;
    QString scene_;
    std::vector<std::string> names_; // de abajo hacia arriba
    struct TypeInfo {
        QString name;
        bool video = false;
        bool audio = false;
    };
    std::map<std::string, TypeInfo> types_; // cache de inputTypes()
    int rowOf(const std::string& name) const;
};

class MixerModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PeakDbRole,
        VolumeDbRole,
        MutedRole,
        RetardoMsRole,
        GrupoRole,          ///< "" suelta; si no, la clave del grupo (ver grupoDe)
        PrimeroDeGrupoRole, ///< la primera tira del grupo (lleva el botón de compactar)
        TamGrupoRole,       ///< cuántas tiras tiene su grupo
        CompactoRole,       ///< su grupo está compactado: sin fader, solo los botones
        NombreGrupoRole,    ///< el nombre que se muestra: «Conferencia», «Asamblea» o la clave
        GananciaGrupoDbRole ///< el volumen general de su grupo (VCA), en dB
    };
    explicit MixerModel(AppRuntime& rt, QObject* parent = nullptr);
    /// El grupo de una tira: BroadcastEngine::grupoDeAudio («Invitado 3 (audio)» -> «Invitado
    /// (audio)»; los dos «Remoto con la palabra…», el mismo). Tiras SEGUIDAS con la misma
    /// clave, dos o más, son un grupo.
    /// El motor cambió el audio de una fuente (volumen, mute, retardo…: BroadcastEngine::
    /// alCambiarAudioFuente), lo haya pedido quien sea. Desde cualquier hilo; los de una
    /// misma vuelta se juntan en un dataChanged por fila, en el hilo del modelo.
    void avisarCambio(const std::string& fuente);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reload();
    /// Refresca los niveles (lo llama el tick de la UI).
    void refreshLevels();

    Q_INVOKABLE bool setVolumeDb(const QString& name, double db);
    Q_INVOKABLE bool setMuted(const QString& name, bool muted);
    /// Un canal por posicion: {nombre, volumenDb, mudo}. Vacio si no existe.
    /// Esta aca para que la vista NO tenga que pedir datos por NUMERO de rol:
    /// los roles son un enum que corre solo al agregar uno, y una vista que
    /// los escribe a mano se rompe en silencio (pasa: se pidio el pico
    /// creyendo pedir el volumen).
    Q_INVOKABLE QVariantMap canal(int fila) const;
    /// Compactar un grupo: sus tiras sin fader, solo con los botones (con muchos invitados
    /// no entran). Lo ven todas las vistas del modelo (la tarjeta y la ventana del mezclador).
    Q_INVOKABLE void setCompacto(const QString& grupo, bool compacto);
    /// Las tiras de un grupo, como modelo propio (los mismos roles; se actualiza solo): la
    /// grilla del grupo compactado. Es del MixerModel, uno por grupo.
    Q_INVOKABLE QAbstractItemModel* delGrupo(const QString& grupo);
    /// Silenciar (o no) todas las tiras de un grupo.
    Q_INVOKABLE void setMutedGrupo(const QString& grupo, bool muted);
    /// El volumen general de un grupo (VCA), de -60 a +12 dB: mueve el grupo entero sin tocar
    /// el fader de cada uno (las diferencias entre ellos quedan); 0 dB = como si no estuviera.
    Q_INVOKABLE bool setGananciaGrupoDb(const QString& grupo, double db);
    /// Los grupos de ahora: [{grupo, tiras: [nombres], compacto}] (MCP).
    QVariantList grupos() const;
    /// El pico (dBFS, con caída suave) de una fuente por nombre; -100 si no es de audio.
    double picoDe(const std::string& nombre) const;

   private:
    AppRuntime& rt_;
    std::vector<std::string> names_;
    std::vector<float> peaks_;
    std::vector<std::string> grupos_; ///< por fila: "" suelta, o la clave de su grupo
    std::map<std::string, std::string> nombresGrupo_; ///< clave -> lo que se muestra
    std::set<std::string> compactos_; ///< los grupos compactados (Config/mezclador-vista.json)
    std::map<QString, QAbstractItemModel*> delGrupo_; ///< los filtros por grupo (hijos de este)
    std::mutex cambiosM_;
    std::set<std::string> cambios_; ///< avisarCambio: las fuentes a refrescar en la próxima vuelta
};

class SourceTypesModel : public QAbstractListModel {
    Q_OBJECT
   public:
    enum Roles { IdRole = Qt::UserRole + 1, DisplayNameRole, VideoRole, AudioRole };
    explicit SourceTypesModel(AppRuntime& rt, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

   private:
    struct Row {
        QString id;
        QString name;
        bool video;
        bool audio;
    };
    std::vector<Row> rows_;
};

} // namespace ba::app
