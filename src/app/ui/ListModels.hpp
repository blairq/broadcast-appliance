// ============================================================================
// src/app/ui/ListModels.hpp — Modelos de lista para la vista (capa ViewModel)
//
// QAbstractListModel sobre el motor. La vista (QML) solo los muestra y llama
// sus comandos; aca no hay nada de presentacion ni de QML.
//
//   ScenesModel       escenas (nombre, al aire): de la foto del motor (F3-03)
//   SourcesModel      fuentes de UNA escena (la que se edita): la foto y la ventanilla
//   MixerModel        fuentes con audio: nivel, volumen, silencio
//   SourceTypesModel  tipos de fuente disponibles para "Agregar"
// ============================================================================

#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include "emision/FotoMotor.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ba::app {

class AppRuntime;
class VentanillaMotor;
struct SourceSpec;

/// Las escenas (de la foto del motor, F3-03): se actualiza solo con lo que avisa la
/// ventanilla (la lista cambió: se rehace; una carpeta, una protección, la escena al aire o
/// la armada de su salida: dataChanged de esos roles).
class ScenesModel : public QAbstractListModel {
    Q_OBJECT
   public:
    /// `live` = esta AL AIRE. `armada` = esta en el bus de preview, o sea la
    /// que va a salir con el proximo TAKE. Son dos cosas distintas y por eso
    /// son dos roles: una mesa de switcher las muestra en dos filas y el
    /// operador mira las dos.
    enum Roles { NameRole = Qt::UserRole + 1, LiveRole, FolderRole, ProtegidaRole, ArmadaRole };
    explicit ScenesModel(VentanillaMotor& ventanilla, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// La escena ARMADA (bus de preview) de su salida: la que sale con el proximo TAKE.
    /// Vacia = no hay ninguna armada.
    QString armada() const;
    /// De qué salida son `live` y `armada` (PLAN-SALIDAS: la activa de la interfaz; "" = la
    /// por omisión).
    void setSalida(const std::string& salida);

   private:
    /// Lo que cambió en la foto (ver arriba).
    void alCambiar(unsigned partes);
    const FotoMotor::Escena* escenaDe(int fila) const;

    VentanillaMotor& v_;
    std::shared_ptr<const FotoMotor> foto_; ///< la que muestra (la de la última vuelta)
    std::vector<std::string> names_;
    std::string salida_;
};

/// Las fuentes de UNA escena (la que se edita), de arriba hacia abajo, de la foto del motor;
/// lo que se cambia se le pide al dueño (la ventanilla). Lo pedido se ve ya (el lugar, si se
/// ve, los efectos) y queda así hasta que llega la respuesta, que ya trae la foto con el
/// cambio: soltar una capa no la hace saltar a donde estaba.
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
    SourcesModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString scene() const { return scene_; }
    void setScene(const QString& s);

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
    /// `baseName`. :returns: el nombre final ("" si no se pudo pedir; si falla después,
    /// avisa `error`).
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
    /// :returns: su nombre ("" si no existe).
    Q_INVOKABLE QString addExisting(const QString& name);
    Q_PROPERTY(int guestSlots READ guestSlots NOTIFY guestSlotsChanged)
    int guestSlots() const;

   signals:
    void sceneChanged();
    void guestSlotsChanged();
    /// Cambiaron los efectos de alguna capa (la foto, o lo que se acaba de pedir).
    void efectosCambio();
    void error(const QString& message);

   private:
    /// Lo que cambió en la foto: la lista (se rehace solo si quedó distinta, para no cortar
    /// una selección ni un arrastre) o cómo está cada capa (dataChanged).
    void alCambiar(unsigned partes);
    void releer();
    const FotoMotor::Fuente* fuenteDe(const std::string& name) const;
    /// Un nombre que no use ninguna fuente: «Cámara», «Cámara 2»… (de la foto).
    std::string nombreLibre(const std::string& base) const;
    /// Pide agregar `spec` a la escena (con el error de vuelta en `error`); con
    /// `elegirCamara`, una cámara nueva con la primera conectada.
    void pedirAgregar(SourceSpec spec, bool elegirCamara = false);
    /// Pide algo que devuelve "" o el error; lo pendiente de `name` se suelta al volver.
    void pedir(const char* que, const std::string& name, std::function<std::string(AppRuntime&)> orden);
    int rowOf(const std::string& name) const;

    AppRuntime& rt_;
    VentanillaMotor& v_;
    QString scene_;
    std::shared_ptr<const FotoMotor> foto_;
    std::vector<std::string> names_; // de abajo hacia arriba
    /// Lo pedido que todavía no volvió, por fuente: se muestra esto y no lo de la foto.
    struct Pendiente {
        int pedidos = 0;
        std::optional<bool> visible;
        std::optional<FotoMotor::Lugar> lugar;
        std::optional<nlohmann::json> efectos;
    };
    std::map<std::string, Pendiente> pendientes_;
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
    /// Las tiras salen de la foto del motor (la parte Mezcla) y se actualizan solas con lo
    /// que avisa la ventanilla: el audio de cada fuente lo cambie quien lo cambie (el
    /// Mezclador, el asistente, el AFV, una escena que se carga). Lo que se mueve se PIDE
    /// (F3-03) y se ve como pendiente hasta que vuelve. Los picos, del canal de niveles.
    /// El grupo de una tira: BroadcastEngine::grupoDeAudio («Invitado 3 (audio)» -> «Invitado
    /// (audio)»; los dos «Remoto con la palabra…», el mismo). Tiras SEGUIDAS con la misma
    /// clave, dos o más, son un grupo.
    MixerModel(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// Rehace las tiras con la foto.
    void reload();
    /// Refresca los niveles (lo llama el tick de la UI): del canal de niveles, sin el motor.
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
    /// Silenciar (o no) todas las tiras de un grupo (en la salida que muestra).
    Q_INVOKABLE void setMutedGrupo(const QString& grupo, bool muted);
    /// Lo mismo en la mezcla de `salida` ("" = la por omisión): el MCP va a la que pide.
    /// `listo` (si hay) corre en la interfaz cuando está hecho (la foto ya lo tiene).
    void setMutedGrupoEn(const std::string& grupo, bool muted, const std::string& salida,
                         std::function<void()> listo = {});
    /// El volumen general de un grupo (VCA), de -60 a +12 dB: mueve el grupo entero sin tocar
    /// el fader de cada uno (las diferencias entre ellos quedan); 0 dB = como si no estuviera.
    /// En la salida que muestra (cada una tiene los suyos).
    Q_INVOKABLE bool setGananciaGrupoDb(const QString& grupo, double db);
    /// Los grupos de ahora: [{grupo, nombre, tiras: [nombres], compacto, volumenDb, mudo}] (la
    /// ventana del mezclador, que arma sus tiras con esto). `mudo` (todas sus tiras) y
    /// `volumenDb`, en la salida que muestra.
    Q_INVOKABLE QVariantList grupos() const { return gruposEn(salida_); }
    /// Lo mismo con el mudo y el VCA de `salida` (el MCP).
    QVariantList gruposEn(const std::string& salida) const;
    /// {pico_db, volumen_db, mudo} de la fila en la mezcla de `salida` (redondeados: lo que
    /// publica el BusEventos en audio/nivel/<fuente> y audio/<salida>/nivel/<fuente>).
    nlohmann::json nivelEn(int fila, const std::string& salida) const;
    /// El pico (dBFS, con caída suave) de una fuente por nombre; -100 si no es de audio.
    double picoDe(const std::string& nombre) const;
    /// La mezcla de qué salida muestra y mueve (volumen y mute de cada tira; PLAN-SALIDAS:
    /// cada salida tiene la suya), y el VCA de sus grupos. "" = la por omisión. Lo demás
    /// (picos, retardo, qué grupos hay) es de la fuente: igual en todas.
    void setSalida(const std::string& salida);
    /// `listo` cuando el dueño hizo todo lo pedido hasta ahora (la foto ya lo tiene).
    void despues(std::function<void()> listo);

   private:
    /// Llegó una foto: si cambiaron las tiras, se rehace; si no, solo las filas que cambiaron.
    void alCambiar(unsigned partes);
    /// Pide `orden` al dueño; lo pendiente de `clave` (una fuente o «grupo:<g>») se suelta al volver.
    void pedir(const char* que, const std::string& clave, std::function<void(AppRuntime&)> orden,
               std::function<void()> listo = {});
    void soltarPendiente(const std::string& clave);
    int filaDe(const std::string& nombre) const;

    AppRuntime& rt_;
    VentanillaMotor& v_;
    std::shared_ptr<const FotoMotor> foto_;
    std::string salida_;
    std::vector<std::string> names_;
    std::vector<float> peaks_;
    std::vector<std::string> grupos_; ///< por fila: "" suelta, o la clave de su grupo
    std::map<std::string, std::string> nombresGrupo_; ///< clave -> lo que se muestra
    std::set<std::string> compactos_; ///< los grupos compactados (Config/mezclador-vista.json)
    std::map<QString, QAbstractItemModel*> delGrupo_; ///< los filtros por grupo (hijos de este)
    /// Lo pedido que todavía no volvió (en la salida que se muestra): se muestra esto.
    struct Pendiente {
        int pedidos = 0;
        std::optional<float> volumen; ///< el multiplicador
        std::optional<bool> mudo;
        std::optional<float> vca;     ///< «grupo:<g>»
    };
    std::map<std::string, Pendiente> pendientes_;
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
