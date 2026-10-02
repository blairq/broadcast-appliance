// src/app/video/VideoGrafos.hpp — el grafo de video del estudio (Studio.video)
//
// UN grafo (video/GrafoVideo) con un sub-grafo por entrada de video (cámara,
// red, NDI) y nodos del estudio (el compositor). A cada entrada que no está
// en «entrada -> escena» se le pone el filtro `ba_video_grafo`
// (src/obs-module/ba_video_grafo.cpp) con lo que le toca
// (GrafoVideo::paraFiltro); lo corre la GPU. Va DESPUÉS de los filtros del
// PTZ (Mejorar imagen y PTZ digital).
//
// Las tarjetas pueden ser propias (color, croma, recorte, salida) o
// CUALQUIER filtro de video de OBS (`obs`, con su id y sus ajustes): el
// catálogo sale de los filtros registrados en libobs (filtrosObs) y sus
// controles, de sus propiedades (propiedades).
//
// O CUALQUIER módulo de video de GStreamer (`gst`, con su elemento y sus
// propiedades): el catálogo sale del registro de GStreamer del bundle
// (video/CatalogoGst), con la documentación en castellano de
// datos/gst-modulos.json, y el formulario, de la introspección de GObject.
//
// Las entradas virtuales (señal de ajuste, color, negro, o cualquier pipeline
// de origen de GStreamer) son fuentes `ba_gst_fuente`: cuentan como entradas.
//
// Una tarjeta «Salida» publica ese punto como una fuente `ba_video_salida`
// con nombre: `aEscena` la pone en una escena (y queda en app.json).
//
// Se guarda en Config/video-grafo.json. El formato de antes
// (Config/video-grafos.json, un grafo por entrada) se migra solo.

#pragma once

#include "video/CatalogoGst.hpp"
#include "video/GrafoVideo.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::video {

class VideoGrafos : public QObject {
    Q_OBJECT
    /// Las entradas de video que hay.
    Q_PROPERTY(QStringList entradas READ entradas NOTIFY cambio)
    /// Las mismas, agrupadas como en el audio: [{nombre, grupo («Cámaras»,
    /// «Red», «NDI»), tarjetas (cuántas tiene)}]
    Q_PROPERTY(QVariantList entradasInfo READ entradasInfo NOTIFY cambio)
    /// La que se está editando.
    Q_PROPERTY(QString entrada READ entrada WRITE seleccionar NOTIFY cambio)
    /// Su sub-grafo: {nodos:[{id,tipo,titulo,etiqueta,grupo,x,y,params,externo,
    /// entradas:[{id,tipo,pedido}],salidas:[…]}], cables:[{desde,salida,hacia,entrada}], trivial}
    Q_PROPERTY(QVariantMap grafo READ grafo NOTIFY cambio)
    /// Todo el estudio (mismo formato).
    Q_PROPERTY(QVariantMap estudio READ estudio NOTIFY cambio)
    /// Qué muestra la página: "racks" (una entrada) o "estudio" (todo).
    Q_PROPERTY(QString vista READ vista WRITE setVista NOTIFY vistaCambio)
    /// Las tarjetas propias: [{tipo,titulo,descripcion,params,ambito}]
    Q_PROPERTY(QVariantList catalogo READ catalogo CONSTANT)

   public:
    VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);

    QString vista() const { return vista_; }
    void setVista(const QString& v) {
        if (v == vista_ || (v != "racks" && v != "estudio")) return;
        vista_ = v;
        emit vistaCambio();
    }
    QStringList entradas() const;
    QVariantList entradasInfo() const;
    QString entrada() const { return entrada_; }
    void seleccionar(const QString& entrada);
    QVariantMap grafo() const;
    QVariantMap estudio() const;
    static QVariantList catalogo();
    /// Los filtros de video de OBS que se pueden poner como tarjeta:
    /// [{id, nombre, propio (de esta app)}], por nombre.
    Q_INVOKABLE QVariantList filtrosObs() const;
    /// Los controles de una tarjeta de OBS o de GStreamer (como
    /// PropertiesModel): [{name, label, kind, value, min, max, step, options,
    /// suffix, help}].
    Q_INVOKABLE QVariantList propiedades(const QString& id) const;
    /// Los módulos de video de GStreamer (CatalogoGst::modulos).
    Q_INVOKABLE QVariantList modulosGst() const;
    /// Las propiedades de un módulo, para el detalle del catálogo.
    Q_INVOKABLE QVariantList propiedadesModulo(const QString& elemento) const;
    /// "GStreamer 1.28.7", o por qué no está.
    Q_INVOKABLE QString versionGst() const;
    /// Las entradas virtuales que se pueden crear: [{id, nombre, descripcion, pipeline}].
    Q_INVOKABLE static QVariantList virtuales();

    // --- Editar (la interfaz; la entrada elegida). "" o el error. -------------
    Q_INVOKABLE QString agregar(const QString& tipo, const QString& despues);
    Q_INVOKABLE QString agregarFiltro(const QString& filtroId, const QString& despues);
    /// Un módulo de GStreamer como tarjeta (`elemento`, p. ej. "videobalance").
    Q_INVOKABLE QString agregarModulo(const QString& elemento, const QString& despues);
    /// Crea una entrada virtual en la escena (vacía = la del aire): `preset` es
    /// un id de virtuales() o, si empieza con «pipeline:», el pipeline de origen.
    Q_INVOKABLE QString crearVirtual(const QString& nombre, const QString& preset, const QString& escena);
    Q_INVOKABLE QString quitar(const QString& id);
    Q_INVOKABLE QString conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE QString desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE QString mover(const QString& id, int x, int y);
    Q_INVOKABLE QString ajustar(const QString& id, const QVariantMap& params);
    /// Hace un controlador con la tarjeta `id` (la misma corrección para varias entradas).
    Q_INVOKABLE QString controlar(const QString& id);
    /// Agrega a `entrada` una tarjeta manejada por el controlador `control`.
    Q_INVOKABLE QString agregarControlada(const QString& control, const QString& entrada);
    /// Suelta la tarjeta de su controlador (se queda con los valores que tenía).
    Q_INVOKABLE QString soltarControl(const QString& id);
    /// Un ajuste de una tarjeta de OBS o de GStreamer (lo que cambia en su formulario).
    Q_INVOKABLE QString ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor);
    /// Pone la salida o el compositor `id` en la escena (vacía = la que está al aire).
    Q_INVOKABLE QString aEscena(const QString& id, const QString& escena);
    /// La entrada elegida vuelve a «entrada -> escena».
    Q_INVOKABLE QString limpiar();

    // --- Para MCP --------------------------------------------------------------
    /// El sub-grafo de `entrada` (o todo el estudio si `entrada` es vacía), o {"error"}.
    nlohmann::json describir(const std::string& entrada) const;
    /// Edita el grafo con `f` (que devuelve "" o el error); si cambió, se
    /// guarda y se aplica.
    std::string editar(const std::function<std::string(GrafoVideo&)>& f);
    std::string ponerEnEscena(const std::string& id, const std::string& escena);
    bool esEntrada(const std::string& nombre) const;
    /// El nombre legible de un filtro de OBS ("" si no existe).
    static std::string nombreFiltro(const std::string& filtroId);
    /// El catálogo de GStreamer (vacío, con el motivo, hasta que está listo).
    const CatalogoGst& gst() const;
    /// Agrega un módulo de GStreamer a `grupo` (MCP y la interfaz): el id o "".
    std::string agregarModuloEn(GrafoVideo& g, const std::string& elemento, const std::string& grupo,
                                const std::string& despues, const nlohmann::json& propiedades, std::string* error) const;
    /// Ajusta `id`; si la maneja un controlador, el ajuste va al controlador
    /// (salvo la etiqueta). "" o el error.
    static std::string ajustarEn(GrafoVideo& g, const std::string& id, nlohmann::json params);
    /// Crea una entrada virtual; "" o el error.
    std::string crearEntradaVirtual(const std::string& nombre, const std::string& preset, const std::string& escena);

   public slots:
    /// Revisa entradas y filtros (al cambiar las entradas, y cada 1,5 s).
    void sincronizar();

   signals:
    void cambio();
    void vistaCambio();
    /// El catálogo de GStreamer quedó listo.
    void gstListo();

   private:
    void aplicar(const std::string& entrada);
    /// Los compositores publicados al día, y las entradas que alimentan otra
    /// parte del estudio, activas.
    void actualizarEstudio();
    /// A las salidas sin nombre les pone uno (único entre las fuentes).
    void nombrarSalidas(GrafoVideo& g) const;
    nlohmann::json conPublicadas(nlohmann::json d) const;
    /// Los errores de las tarjetas de `entrada` (de su filtro): {id: motivo}.
    nlohmann::json erroresDe(const std::string& entrada) const;
    void guardar() const;
    void cargar();

    AppRuntime& rt_;
    std::filesystem::path archivo_, archivoViejo_;
    GrafoVideo grafo_;
    std::map<std::string, std::string> aplicado_; ///< lo último que recibió cada filtro
    QStringList entradas_;
    QString entrada_;
    QString vista_ = QStringLiteral("racks");
    QTimer revision_;
    std::set<std::string> mostradas_; ///< entradas que este objeto mantiene activas
    std::string erroresVistos_;       ///< los errores de las tarjetas, la última vez
    /// El catálogo se arma en un hilo al arrancar (GStreamer revisa sus
    /// plugins; la primera vez tarda): mientras tanto, uno vacío.
    std::shared_ptr<const CatalogoGst> gst_ = std::make_shared<CatalogoGst>();
};

} // namespace ba::app::video
