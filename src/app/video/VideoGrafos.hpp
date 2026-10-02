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
// Una tarjeta «Salida» publica ese punto como una fuente `ba_video_salida`
// con nombre: `aEscena` la pone en una escena (y queda en app.json).
//
// Se guarda en Config/video-grafo.json. El formato de antes
// (Config/video-grafos.json, un grafo por entrada) se migra solo.

#pragma once

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
#include <string>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::video {

class VideoGrafos : public QObject {
    Q_OBJECT
    /// Las entradas de video que hay.
    Q_PROPERTY(QStringList entradas READ entradas NOTIFY cambio)
    /// La que se está editando.
    Q_PROPERTY(QString entrada READ entrada WRITE seleccionar NOTIFY cambio)
    /// Su sub-grafo: {nodos:[{id,tipo,titulo,etiqueta,grupo,x,y,params,externo,
    /// entradas:[{id,tipo,pedido}],salidas:[…]}], cables:[{desde,salida,hacia,entrada}], trivial}
    Q_PROPERTY(QVariantMap grafo READ grafo NOTIFY cambio)
    /// Todo el estudio (mismo formato).
    Q_PROPERTY(QVariantMap estudio READ estudio NOTIFY cambio)
    /// Las tarjetas propias: [{tipo,titulo,descripcion,params,ambito}]
    Q_PROPERTY(QVariantList catalogo READ catalogo CONSTANT)

   public:
    VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);

    QStringList entradas() const;
    QString entrada() const { return entrada_; }
    void seleccionar(const QString& entrada);
    QVariantMap grafo() const;
    QVariantMap estudio() const;
    static QVariantList catalogo();
    /// Los filtros de video de OBS que se pueden poner como tarjeta:
    /// [{id, nombre, propio (de esta app)}], por nombre.
    Q_INVOKABLE QVariantList filtrosObs() const;
    /// Los controles de una tarjeta de OBS (como PropertiesModel): [{name,
    /// label, kind, value, min, max, step, options, suffix, help}].
    Q_INVOKABLE QVariantList propiedades(const QString& id) const;

    // --- Editar (la interfaz; la entrada elegida). "" o el error. -------------
    Q_INVOKABLE QString agregar(const QString& tipo, const QString& despues);
    Q_INVOKABLE QString agregarFiltro(const QString& filtroId, const QString& despues);
    Q_INVOKABLE QString quitar(const QString& id);
    Q_INVOKABLE QString conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE QString desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE QString mover(const QString& id, int x, int y);
    Q_INVOKABLE QString ajustar(const QString& id, const QVariantMap& params);
    /// Un ajuste de una tarjeta de OBS (lo que cambia en su formulario).
    Q_INVOKABLE QString ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor);
    /// Pone la salida `id` en la escena (vacía = la que está al aire).
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

   public slots:
    /// Revisa entradas y filtros (al cambiar las entradas, y cada 1,5 s).
    void sincronizar();

   signals:
    void cambio();

   private:
    void aplicar(const std::string& entrada);
    /// A las salidas sin nombre les pone uno (único entre las fuentes).
    void nombrarSalidas(GrafoVideo& g) const;
    nlohmann::json conPublicadas(nlohmann::json d) const;
    void guardar() const;
    void cargar();

    AppRuntime& rt_;
    std::filesystem::path archivo_, archivoViejo_;
    GrafoVideo grafo_;
    std::map<std::string, std::string> aplicado_; ///< lo último que recibió cada filtro
    QStringList entradas_;
    QString entrada_;
    QTimer revision_;
};

} // namespace ba::app::video
