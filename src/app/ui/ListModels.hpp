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
    Q_INVOKABLE bool move(const QString& name, int delta);
    Q_INVOKABLE bool remove(const QString& name);
    Q_INVOKABLE bool setRect(const QString& name, double x, double y, double w, double h);
    /// Agrega una fuente nueva de tipo `typeId` con nombre unico derivado de
    /// `baseName`. :returns: el nombre final ("" si fallo).
    Q_INVOKABLE QString add(const QString& typeId, const QString& baseName);
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
    enum Roles { NameRole = Qt::UserRole + 1, PeakDbRole, VolumeDbRole, MutedRole };
    explicit MixerModel(AppRuntime& rt, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reload();
    /// Refresca los niveles (lo llama el tick de la UI).
    void refreshLevels();

    Q_INVOKABLE bool setVolumeDb(const QString& name, double db);
    Q_INVOKABLE bool setMuted(const QString& name, bool muted);

   private:
    AppRuntime& rt_;
    std::vector<std::string> names_;
    std::vector<float> peaks_;
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
