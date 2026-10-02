// src/app/video/VideoGrafos.hpp — los grafos de video de las entradas (Studio.video)
//
// Cada entrada de video (cámara, red, NDI) tiene su grafo de tarjetas
// (video/GrafoVideo). Si es más que «entrada -> escena», se le pone el filtro
// `ba_video_grafo` (src/obs-module/ba_video_grafo.cpp) con el grafo en sus
// ajustes; lo corre la GPU. Va DESPUÉS de los filtros del PTZ (Mejorar imagen
// y PTZ digital): la entrada del grafo es la cámara ya encuadrada.
//
// Una tarjeta «Salida» publica ese punto del grafo como una fuente
// `ba_video_salida` con nombre: `aEscena` la pone en una escena (y queda en
// app.json como cualquier fuente).
//
// Se guarda en Config/video-grafos.json (por nombre de entrada: si la entrada
// se borra y vuelve con el mismo nombre, recupera su grafo).

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
    /// Su grafo: {nodos:[{id,tipo,titulo,etiqueta,x,y,params,entradas,salidas}], cables:[{desde,hacia}], trivial}
    Q_PROPERTY(QVariantMap grafo READ grafo NOTIFY cambio)
    /// Las tarjetas que se pueden agregar: [{tipo,titulo,descripcion,params}]
    Q_PROPERTY(QVariantList catalogo READ catalogo CONSTANT)

   public:
    VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);

    QStringList entradas() const;
    QString entrada() const { return entrada_; }
    void seleccionar(const QString& entrada);
    QVariantMap grafo() const;
    static QVariantList catalogo();

    // --- Editar la entrada elegida (la interfaz). "" o el error. -------------
    Q_INVOKABLE QString agregar(const QString& tipo, const QString& despues);
    Q_INVOKABLE QString quitar(const QString& id);
    Q_INVOKABLE QString conectar(const QString& desde, const QString& hacia);
    Q_INVOKABLE QString desconectar(const QString& desde, const QString& hacia);
    Q_INVOKABLE QString mover(const QString& id, int x, int y);
    Q_INVOKABLE QString ajustar(const QString& id, const QVariantMap& params);
    /// Pone la salida `id` en la escena (vacía = la que está al aire).
    Q_INVOKABLE QString aEscena(const QString& id, const QString& escena);
    /// Vuelve a «entrada -> escena».
    Q_INVOKABLE QString limpiar();

    // --- Para MCP: cualquier entrada ------------------------------------------
    /// El grafo de `entrada` (describir()), o {"error"}.
    nlohmann::json describir(const std::string& entrada) const;
    /// Edita el grafo de `entrada` con `f` (que devuelve "" o el error); si
    /// cambió, se guarda y se aplica.
    std::string editar(const std::string& entrada, const std::function<std::string(GrafoVideo&)>& f);
    std::string ponerEnEscena(const std::string& entrada, const std::string& id, const std::string& escena);

   public slots:
    /// Revisa entradas y filtros (al cambiar las entradas, y cada 1,5 s).
    void sincronizar();

   signals:
    void cambio();

   private:
    bool esEntrada(const std::string& nombre) const;
    void aplicar(const std::string& entrada);
    /// A las salidas sin nombre les pone uno (único entre las fuentes).
    void nombrarSalidas(const std::string& entrada, GrafoVideo& g) const;
    void guardar() const;
    void cargar();

    AppRuntime& rt_;
    std::filesystem::path archivo_;
    std::map<std::string, GrafoVideo> grafos_;
    std::map<std::string, std::string> aplicado_; ///< lo último que recibió cada filtro
    QStringList entradas_;
    QString entrada_;
    QTimer revision_;
};

} // namespace ba::app::video
