// ============================================================================
// src/app/pantallas/Distribuciones.hpp — distribuciones de pantallas guardadas
//
// «Pantallas → Pantallas locales → Distribuciones». Una distribución es una foto
// con nombre de cómo quedan las ventanas en los monitores de esta máquina: en el
// de la izquierda dos escenas con las grillas de los remotos lado a lado, en el
// del escenario el programa a pantalla completa, en el de control el programa,
// el previo y la multivista. Se arma a mano (un esquema de los monitores con
// rectángulos que se arrastran) o, sobre todo, por MCP (una lista de ventanas en
// JSON), se guarda y se APLICA cuando se la necesita.
//
// UNA VENTANA MADRE POR MONITOR (un «lienzo»). Aplicar una distribución NO abre
// una ventana del sistema por cada cosa: arma, en cada monitor que usa, UNA sola
// ventana sin marco (a pantalla completa si la distribución ocupa el monitor
// entero; si no, del tamaño justo del rectángulo que encierra sus ventanas) y
// adentro dibuja cada «ventana» de la distribución como un rectángulo con su
// vista (VistaCosas, la misma de las ventanas sueltas y los monitores). Por qué:
//   - nada que el operador pueda mover, agrandar o cerrar con el mouse o con los
//     atajos del sistema (pedido del usuario: se cambian solo desde el menú
//     Pantallas o por MCP);
//   - posiciones exactas: el gestor de ventanas no reacomoda rectángulos internos
//     (sí reacomoda, a veces, ventanas sueltas sin marco, sobre todo en macOS);
//   - un solo contexto de render y una sola superficie para el compositor por
//     monitor, en vez de N;
//   - cambiar de distribución no abre ni cierra ventanas: el lienzo de cada
//     monitor se queda (su id es el del monitor) y cambian sus rectángulos.
// Y cada monitor con su lienzo propio respeta su escala y su frecuencia (una
// ventana que cruzara monitores no). Lo que de verdad necesite su propia ventana
// del sistema (algo que se captura por ventana) sigue siendo una ventana suelta
// (pantallas/Ventanas.hpp), que no es parte de una distribución.
//
// Las piezas son las de siempre: cosas del catálogo único (pantallas/Catalogo.hpp)
// y monitores con el id estable de Salidas → Pantallas (pantallas/Monitores.hpp).
// Aplicar deja EXACTAMENTE lo de la distribución: los lienzos se ponen en su lugar
// (ListaConClave: dos veces lo mismo no avisa nada) y las ventanas sueltas se
// cierran. Lo fijo de «Pantallas conectadas» (MonitoresModel) no se toca; si un
// monitor tiene las dos cosas, se avisa.
//
// Salida de emergencia: «Cerrar las ventanas de la distribución» (Pantallas →
// Distribuciones), Ctrl+Shift+F12 desde cualquier ventana de la app, o
// pantallas_distribucion_cerrar por MCP. Lo aplicado se recuerda: al volver la
// luz (o al reiniciar) vuelve sola la que estaba (o la marcada `alAbrir`).
//
// EL LUGAR VA RELATIVO AL MONITOR (0..1), para que sobreviva a un cambio de
// resolución: «la mitad izquierda» sigue siendo la mitad izquierda en un 4K o en
// un 1080p. Si quien la armó fijó píxeles (`absoluta`, relativa al borde del
// monitor), se usan esos mientras entren en el monitor; si no entran, la
// relativa.
//
// SI UN MONITOR NO ESTÁ, se avisa y su ventana va al que haya (el primero que no
// sea el principal; si no, el principal). Nunca se rompe ni se deja de aplicar el
// resto. Si después aparece (o se desenchufa otro), lo aplicado se vuelve a
// resolver solo. Distinto de «Pantallas conectadas» (MonitoresModel), que ESPERA
// al monitor que falta sin mudar su contenido.
//
// ES DEL EVENTO («Evento es todo»): <evento>/Config/distribuciones-pantallas.json.
// Una puede marcarse `alAbrir`: se aplica sola al cargar el evento.
// ============================================================================
#pragma once

#include "pantallas/Ventanas.hpp"
#include "ui/CapturaError.hpp"
#include "ui/ListaConClave.hpp"

#include <QRect>
#include <QRectF>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <vector>

namespace ba::app {

class CatalogoVisible;
class MonitoresModel;

/// Una ventana de una distribución.
struct VentanaDistribucion {
    QString nombre;      ///< para leer («Remotos A»); vacío = el título de lo que muestra
    QStringList cosas;   ///< ids del catálogo (una, o varias en grilla)
    QString disposicion = QStringLiteral("grid"); ///< con varias cosas: grid | focus | pip | row
    QString monitor;     ///< id estable (MonitoresModel::idDe); vacío = el primero que no sea el principal
    QRectF relativa{0, 0, 1, 1}; ///< dentro del monitor, 0..1
    QRect absoluta;      ///< en píxeles desde el borde del monitor; vacía = usar la relativa
    bool pantallaCompleta = false;
    bool encima = false; ///< siempre encima de las demás
    bool operator==(const VentanaDistribucion&) const = default;
};

/// Una distribución guardada.
struct Distribucion {
    QString id;
    QString nombre;
    bool alAbrir = false; ///< se aplica sola al cargar el evento
    std::vector<VentanaDistribucion> ventanas;
    bool operator==(const Distribucion&) const = default;
};

/// Un rectángulo de un lienzo: una «ventana» de la distribución ya ubicada.
struct Pieza {
    QString nombre;
    QStringList cosas;
    QString disposicion = QStringLiteral("grid");
    QRectF rect;           ///< 0..1 dentro del lienzo
    bool pantallaCompleta = false; ///< ocupa el monitor entero (para volver a capturarla igual)
    bool operator==(const Pieza&) const = default;
};

/// La ventana madre de un monitor: una sola ventana sin marco con sus piezas adentro.
struct Lienzo {
    QString id;        ///< el id del monitor (uno por monitor: cambiar de distribución no la recrea)
    QRect geometria;   ///< en píxeles del escritorio
    bool pantallaCompleta = false; ///< ocupa el monitor entero
    bool encima = false;
    int orden = 0;     ///< el número del monitor (para leer)
    std::vector<Pieza> piezas;
    bool operator==(const Lienzo&) const = default;
};

/// Los lienzos puestos ahora: el modelo del Instantiator de Main.qml (VentanaLienzo.qml).
class LienzosModel : public ListaConClave<Lienzo> {
    Q_OBJECT
    Q_PROPERTY(int cantidad READ cantidad NOTIFY cambio)

   public:
    enum Roles {
        LienzoIdRole = Qt::UserRole + 1, ///< el id del monitor
        GeometriaRole,
        PantallaCompletaRole,
        EncimaRole,
        OrdenRole,
        PiezasRole, ///< [{nombre, cosas, disposicion, x, y, ancho, alto}] (0..1 en el lienzo)
    };
    explicit LienzosModel(QObject* parent = nullptr) : ListaConClave(parent) {}
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int cantidad() const { return rowCount(); }
    const std::vector<Lienzo>& lienzos() const { return filas_; }
    /// Deja exactamente estos (en su lugar, por monitor). true si cambió algo.
    bool poner(std::vector<Lienzo> nuevos);
    /// Los saca todos. Devuelve cuántos había.
    int cerrarTodo();

   signals:
    void cambio();

   protected:
    QList<int> rolesQueCambian(const Lienzo& a, const Lienzo& b) const override;
};

/// Un monitor conectado, con lo justo para ubicar (de MonitoresModel::conectadosEnOrden).
struct MonitorVisto {
    QString id;
    QRect geometria;
    bool principal = false;
    int orden = 0;
    QString nombre;
};

namespace distribucion {
/// De relativa (0..1 dentro del monitor) a píxeles del escritorio.
QRect aAbsoluta(const QRectF& relativa, const QRect& monitor);
/// De píxeles del escritorio a relativa al monitor (recortada a 0..1).
QRectF aRelativa(const QRect& escritorio, const QRect& monitor);
/// Dónde va esa ventana en ese monitor, en píxeles del escritorio: la absoluta si
/// la hay y entra (corrida para que quede adentro), si no la relativa.
QRect geometriaEn(const VentanaDistribucion& v, const QRect& monitor);
/// El monitor donde va: el suyo si está; si no (o si no dice), el primero que no
/// sea el principal, o el principal. `falta` = tenía uno y no está. -1 si no hay ninguno.
int monitorPara(const VentanaDistribucion& v, const std::vector<MonitorVisto>& hay, bool* falta = nullptr);
/// Una ventana desde un mapa (MCP, QML). "" o el error (en el idioma del operador).
/// `resolverMonitor` traduce «2» / «DELL…» / el id a un id estable ("" = no se conoce).
QString ventanaDeMapa(const QVariantMap& m, VentanaDistribucion& v,
                      const std::function<QString(const QString&)>& resolverMonitor);
/// La ventana como mapa (lo que guarda el archivo y devuelve el MCP).
QVariantMap mapaDeVentana(const VentanaDistribucion& v);
} // namespace distribucion

class DistribucionesModel : public ListaConClave<Distribucion> {
    Q_OBJECT
    /// {id: distribución como mapa}: para que el editor se ENLACE a la elegida.
    /// Cada ventana trae además `enMonitor` (dónde iría ahora), `faltaMonitor`,
    /// `dibujo` ({x,y,width,height} 0..1 en el esquema de todos los monitores) y `titulo`.
    Q_PROPERTY(QVariantMap porId READ porId NOTIFY cambio)
    Q_PROPERTY(int cantidad READ cantidad NOTIFY cambio)
    /// Lo que pasó al aplicar la última vez (monitores que faltaban, cosas que ya no
    /// existen), o vacío.
    Q_PROPERTY(QString aviso READ aviso NOTIFY cambio)
    /// La última que se aplicó (vacío si ninguna, o si se cerraron sus ventanas).
    Q_PROPERTY(QString aplicada READ aplicada NOTIFY cambio)
    /// Los monitores conectados para el esquema del editor: [{id, orden, nombre,
    /// principal, ancho, alto, dibujo:{x,y,width,height}}] (0..1 en el rectángulo de todos).
    Q_PROPERTY(QVariantList monitores READ monitores NOTIFY cambio)
    /// Ancho/alto del rectángulo de todos los monitores (para el esquema).
    Q_PROPERTY(double proporcion READ proporcion NOTIFY cambio)
    /// Las plantillas de un clic: [{id, titulo, ayuda}].
    Q_PROPERTY(QVariantList plantillas READ plantillas CONSTANT)
    /// Las ventanas madre puestas ahora (LienzosModel).
    Q_PROPERTY(QObject* lienzos READ lienzosQml CONSTANT)

   public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NombreRole,
        AlAbrirRole,
        VentanasRole, ///< QVariantList de mapas (como porId)
        CantidadVentanasRole,
        ResumenRole, ///< para leer: «3 ventanas en 2 monitores»
        AplicadaRole,
    };

    /// `archivo` vacío = no se guarda (tests, modo seguro). `catalogo` valida las
    /// cosas; `monitores` y `ventanas` pueden faltar en los tests de solo modelo.
    DistribucionesModel(std::filesystem::path archivo, CatalogoVisible* catalogo, MonitoresModel* monitores,
                        VentanasModel* ventanas, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantMap porId() const;
    int cantidad() const { return rowCount(); }
    QString aviso() const { return aviso_; }
    QString aplicada() const { return aplicada_; }
    QVariantList monitores() const;
    double proporcion() const;
    static QVariantList plantillas();
    QObject* lienzosQml() const;
    LienzosModel& lienzos() { return lienzos_; }
    const std::vector<Distribucion>& lista() const { return filas_; }
    const Distribucion* buscar(const QString& idONombre) const;

    // --- Armar: "" o el error (avisado a la vista o a quien capture) ---------------
    /// Crea una con esas ventanas (mapas como los de `ventanaDeMapa`). Devuelve su
    /// id, o "" si no se pudo.
    Q_INVOKABLE QString crear(const QString& nombre, const QVariantList& ventanas);
    /// Cambia lo que venga en `cambios`: nombre, alAbrir, ventanas (la lista entera).
    Q_INVOKABLE bool editar(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE QString duplicar(const QString& id, const QString& nombre = {});
    Q_INVOKABLE bool renombrar(const QString& id, const QString& nombre);
    Q_INVOKABLE bool borrar(const QString& id);
    /// La que se abre sola al cargar el evento (una sola: marcar una desmarca la otra).
    Q_INVOKABLE bool setAlAbrir(const QString& id, bool si);
    /// Desde una plantilla (ver `plantillas`): en ese monitor (vacío = el primero que
    /// no sea el principal) y con esas cosas (vacío = las de la plantilla).
    Q_INVOKABLE QString crearDePlantilla(const QString& plantilla, const QString& nombre, const QString& monitor = {},
                                         const QStringList& cosas = {});

    // --- Sus ventanas -----------------------------------------------------------
    Q_INVOKABLE bool agregarVentana(const QString& id, const QVariantMap& ventana);
    Q_INVOKABLE bool quitarVentana(const QString& id, int indice);
    /// Cambia lo que venga (como `ventanaDeMapa`, sobre la que ya está).
    Q_INVOKABLE bool setVentana(const QString& id, int indice, const QVariantMap& cambios);
    /// Desde el esquema del editor: el rectángulo en 0..1 del rectángulo de TODOS
    /// los monitores. Va al monitor donde cae su centro, relativa a él.
    Q_INVOKABLE bool moverEnDibujo(const QString& id, int indice, double x, double y, double ancho, double alto);

    // --- Usarla ---------------------------------------------------------------
    /// Deja las ventanas exactamente como dice. Lo que no se pudo (un monitor que
    /// falta, una escena borrada) queda en `aviso`, sin dejar de aplicar el resto.
    Q_INVOKABLE bool aplicar(const QString& id);
    /// Guarda lo que hay abierto ahora: como una nueva (`id` vacío) o encima de esa.
    /// Devuelve el id, o "".
    Q_INVOKABLE QString capturar(const QString& nombre, const QString& id = {});
    /// La salida de emergencia: saca las ventanas madre de la distribución. Devuelve
    /// cuántas eran.
    Q_INVOKABLE int cerrarVentanas();
    /// Al cargar el evento: aplica la marcada `alAbrir` o, si no hay, la que estaba
    /// aplicada (un corte de luz no tiene que dejar los monitores vacíos).
    void aplicarAlAbrir();

    /// Los conectados, como los usa el modelo (de MonitoresModel, o los de prueba).
    std::vector<MonitorVisto> monitoresVistos() const;
    /// Para los tests sin MonitoresModel.
    void setMonitoresDePrueba(std::vector<MonitorVisto> m);
    /// Los lienzos que pondría `aplicar` (para tests y para «qué haría»).
    std::vector<Lienzo> resolver(const Distribucion& d, QStringList* avisos = nullptr) const;
    /// Lo que hay puesto ahora (los lienzos y las ventanas sueltas), como
    /// distribución (sin id ni nombre).
    Distribucion foto() const;

   signals:
    void cambio();
    /// Para el operador (ya traducido): errores de las acciones y lo que dejó aplicar.
    void notify(const QString& texto, const QString& tipo);

   private:
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    bool sinError(const QString& e) {
        avisarError(e);
        return e.isEmpty();
    }
    int filaDe(const QString& id) const;
    QString idNuevo(const QString& nombre) const;
    QString resolverMonitor(const QString& quien) const;
    QString parsearVentanas(const QVariantList& lista, std::vector<VentanaDistribucion>& out) const;
    QString validarCosas(QStringList& cosas) const;
    /// Pone la fila nueva (o la cambia) y guarda.
    void poner(Distribucion d);
    QVariantMap mapaDe(const Distribucion& d) const;
    QRect limites() const;
    /// Lo aplicado se vuelve a resolver (cambiaron los monitores, se editó).
    void reaplicar();
    void cargar();
    void guardar() const;

    std::filesystem::path archivo_;
    CatalogoVisible* catalogo_ = nullptr;
    MonitoresModel* monitores_ = nullptr;
    VentanasModel* ventanas_ = nullptr;
    bool hayPrueba_ = false;
    std::vector<MonitorVisto> prueba_;
    QString aviso_;
    QString aplicada_;
    LienzosModel lienzos_;
};

} // namespace ba::app
