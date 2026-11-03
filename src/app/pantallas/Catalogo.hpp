// ============================================================================
// src/app/pantallas/Catalogo.hpp — el catálogo de «cosas que se pueden mostrar»
//
// Un panel del tablero, una multivista, una escena, el programa, lo que va de
// fondo en el tablero, una página entera, un cartel en un televisor: todo eso
// se puede poner en algún lado —en una ventana suelta, en un monitor de la
// máquina, en una TV de la red—. Antes cada lugar tenía su lista: Main.qml
// sabía qué paneles se podían sacar (y la barra de tarjetas los volvía a
// listar, y el MCP les escribía los títulos otra vez), la página Pantallas
// tenía la suya de contenidos para la TV y las ventanas de video otra más.
// Ahora hay UNA, y la leen todos.
//
// Cada cosa tiene:
//   id       corto y ESTABLE: es lo que se guarda (el monitor recuerda «escena:
//            Plano general», no un índice) y lo que se pide por MCP.
//   grupo    programa | escena | multiview | video | tarjeta | panel | pagina | fondo | web | registro
//            | distribucion
//   titulo   para leer.
//   dibujo   cómo se dibuja EN ESTA MÁQUINA (ventana o monitor), o vacío si no
//            se puede (un cartel de la TV es una página del gateway):
//              {modo:"video", claves:[…], distribucion}  — VideoWall
//              {modo:"multiview", id}                    — la multivista viva
//              {modo:"panel", panel:"padron"|"ptz:Cam 1"|"tarjeta:aire"…}
//              {modo:"pagina", pagina:<índice>, idPagina}
//              {modo:"distribucion", id, resumen, proporcion, piezas:[{nombre,
//               titulo, cosas, disposicion, x, y, ancho, alto}]}  — una distribución de pantallas guardada
//               (pantallas/Distribuciones.hpp) dibujada ENTERA en un solo lugar:
//               sus ventanas como rectángulos (0..1) de lo que la muestre
//   red      cómo se manda a una TV (tipo de contenido del cast y sus datos),
//            o vacío si no viaja.
//   tarjeta  qué tipo de tarjeta del tablero abre ESTA cosa cuando se la saca a
//            ventana (la del Mezclador abre el panel del Mezclador, más
//            completo que la tarjeta).
//
// LO QUE CAMBIA (escenas, multivistas, cámaras, el fondo del tablero) se pide
// por funciones (`Fuentes`): el catálogo no conoce al Studio, así se lo prueba
// sin interfaz ni motor. Quien sabe que algo cambió llama a `refrescar()`, y
// las filas se actualizan EN SU LUGAR (por id): una lista que se rearma entera
// hace saltar a las vistas que la miran.
// ============================================================================
#pragma once

#include "ui/ListaConClave.hpp"

#include <QQmlPropertyMap>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <utility>
#include <vector>

namespace ba::app {

/// Una cosa del catálogo.
struct CosaVisible {
    QString id, grupo, titulo, icono;
    QVariantMap dibujo;
    QString redTipo;
    QVariantMap redDatos;
    QString tarjeta;
    bool operator==(const CosaVisible&) const = default;
};

class CatalogoVisible : public ListaConClave<CosaVisible> {
    Q_OBJECT
    /// Solo lo que se puede dibujar en esta máquina (ventanas y monitores).
    Q_PROPERTY(QObject* locales READ locales CONSTANT)
    /// Solo lo que se puede mandar a una TV de la red.
    Q_PROPERTY(QObject* deRed READ deRed CONSTANT)
    /// Cuántas cosas hay (para «no hay nada» sin contar en QML).
    Q_PROPERTY(int cantidad READ cantidad NOTIFY cambio)
    /// {id: cosa} (los nombres de los roles): para que una vista se ENLACE a
    /// una cosa por su id (`Studio.catalogo.porId[id]`) y se actualice sola. Es un
    /// QQmlPropertyMap: cada id avisa solo cuando cambia ESA cosa, y leer una no convierte el
    /// catálogo entero (con 200 invitados, cada binding que miraba una cosa convertía ~450
    /// filas a JS en cada cambio: trabas de 2 s en la PC).
    Q_PROPERTY(QObject* porId READ porIdQml CONSTANT)

   public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        GrupoRole,
        GrupoTituloRole,
        TituloRole,
        EtiquetaRole,
        IconoRole,
        DibujoRole,
        LocalRole,
        RedRole,
        RedTipoRole,
        RedDatosRole,
        TarjetaRole,
    };

    /// De dónde sale lo que cambia. Cualquiera puede faltar (queda vacío).
    struct Fuentes {
        /// Los nombres de las escenas, en el orden del operador.
        std::function<QStringList()> escenas;
        /// Las escenas con su id estable (id, nombre), en el orden del operador: si está,
        /// manda sobre `escenas` y cada una es «escena:<id>» (escenas/RefEscena.hpp), así un
        /// renombre no deja colgado al monitor o la ventana que la muestra.
        std::function<std::vector<std::pair<std::string, std::string>>()> escenasConId;
        /// Las multivistas: [{id, nombre}].
        std::function<QVariantList()> multiviews;
        /// Lo que va de fondo en el tablero: claves de video y su distribución.
        std::function<QStringList()> fondo;
        std::function<QString()> fondoDistribucion;
        /// Las entradas con PTZ (un panel por cámara).
        std::function<QStringList()> camarasPtz;
        /// Las páginas: [{page, id, titulo, icon}] (RielConfig::todas).
        std::function<QVariantList()> paginas;
        /// Los contenidos de la TV: [{id, titulo, ayuda}] (PantallasViewModel).
        std::function<QVariantList()> contenidosRed;
        /// Las fuentes de video sueltas (un invitado, su pantalla, una entrada):
        /// [{clave, titulo}] con las claves de FrameService («guest:1»,
        /// «entrada:Cámara 1»). Van como «video:<clave>».
        std::function<QVariantList()> videos;
        /// Los tótems de registro (ui/TotemsViewModel): [{id, nombre, urlPantalla}].
        /// Van como «registro:<id>» y solo a la TV: la página con ?pantalla=1
        /// muestra el QR para registrarse desde el celular.
        std::function<QVariantList()> totems;
        /// Las distribuciones de pantallas guardadas (DistribucionesModel::paraCatalogo):
        /// [{id, nombre, resumen, piezas}]. Van como «distribucion:<id>»: un monitor o una
        /// ventana suelta puede mostrar una distribución entera (no a una TV).
        std::function<QVariantList()> distribuciones;
    };

    using Cosa = CosaVisible;

    explicit CatalogoVisible(Fuentes fuentes, QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* locales() const { return locales_; }
    QObject* deRed() const { return deRed_; }
    int cantidad() const { return rowCount(); }
    /// Lo mismo como mapa, para el C++ y las pruebas.
    QVariantMap porId() const;
    QObject* porIdQml() const;

    /// Vuelve a pedirle todo a las fuentes y actualiza las filas en su lugar (ya).
    void refrescar();
    /// Lo mismo, juntando una ráfaga de avisos en una sola pasada (a los 50 ms): para lo que
    /// cambia seguido (las claves y los rótulos de los invitados).
    void pedirRefresco();
    /// Solo las distribuciones (lo único que cambia al crear, editar, mover o borrar una):
    /// las demás filas ni se miran.
    void refrescarDistribuciones();

    /// La cosa de ese id, como mapa (los mismos nombres que los roles), o vacío.
    Q_INVOKABLE QVariantMap item(const QString& id) const;
    Q_INVOKABLE bool existe(const QString& id) const { return buscar(id) != nullptr; }
    /// El título para leer; si ya no existe (una escena borrada), lo dice.
    Q_INVOKABLE QString tituloDe(const QString& id) const;
    /// Qué se abre al sacar a ventana la tarjeta `kind` del tablero (el panel
    /// propio si lo tiene; la PTZ, la de la primera cámara). Vacío si nada.
    Q_INVOKABLE QString ventanaDeTarjeta(const QString& kind) const;
    /// Por su clave; una escena también por la clave de antes («escena:<nombre>»).
    const Cosa* buscar(const QString& id) const;
    /// La clave de hoy de lo que dice `id` (una escena guardada por el nombre pasa a su id);
    /// si no está, `id` igual.
    QString claveDe(const QString& id) const;
    const std::vector<Cosa>& cosas() const { return filas_; }

    /// El nombre legible de cada grupo («Escena», «Página»…).
    static QString tituloDeGrupo(const QString& grupo);

   signals:
    void cambio();

   private:
    std::vector<Cosa> armar() const;
    std::vector<Cosa> armarDistribuciones() const;
    void avisar(); ///< cambió algo: `porId` se rearma en la próxima lectura y se avisa

    Fuentes fuentes_;
    QSortFilterProxyModel* locales_ = nullptr;
    QSortFilterProxyModel* deRed_ = nullptr;
    QTimer juntar_;
    mutable QVariantMap porId_;
    mutable bool porIdSucio_ = true;
    QQmlPropertyMap* mapa_ = nullptr; ///< porId para la QML (ver arriba)
    void sincronizarMapa();
};

} // namespace ba::app
