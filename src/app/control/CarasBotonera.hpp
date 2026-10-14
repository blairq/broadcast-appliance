// ============================================================================
// src/app/control/CarasBotonera.hpp — la cara de un botón: foto o video corto
// ============================================================================
//
// Un botón de la botonera puede llevar de cara una foto o un video de hasta
// 5 s (como las teclas animadas de un Stream Deck). Sale de un archivo
// (imagen, GIF o video, que se decodifica con GStreamer) o de una cámara en
// vivo (foto, o video de hasta 5 s a 8 cuadros/s).
//
// Se guarda en Media/botonera/<id>/: los cuadros cuadrados de 288 px
// (0000.jpg…, a 8 cuadros/s; una foto es un solo cuadro) y `medio.<ext>`, el
// archivo grande (el original importado o la foto en 1280 px), para que el
// botón lo pueda poner en una escena con medio_poner. Cada toma tiene un id
// nuevo: los archivos no cambian nunca (se cachean para siempre).
//
// La celda lo nombra como `estilo.imagen = "medio:<id>"`. Al dibujar viaja
// `imagen` (el primer cuadro en 96 px, data: URL, para quien no sepa más) y
// `cara: {id, cuadros, fps}`, liviano, con el que cada superficie trae los
// cuadros a su medida:
//   - la tablet: GET /botonera/caras/<id>/0000.jpg… (el gateway, con caché);
//   - el Stream Deck (SatelliteServidor): los lee del disco y los escala a su
//     tecla (72/96/120/144 px);
//   - la grilla de la app: file:// (info().base).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QImage>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::control {

class CarasBotonera : public QObject {
    Q_OBJECT
    /// Algo en curso (importar, grabar; pueden ser varias): la interfaz muestra `estado`.
    Q_PROPERTY(bool ocupada READ ocupada NOTIFY cambio)
    Q_PROPERTY(QString estado READ estado NOTIFY cambio)
    /// file:// de Media/botonera/ (con «/»): la grilla arma <raizUrl><id>/NNNN.jpg.
    Q_PROPERTY(QString raizUrl READ raizUrl CONSTANT)

   public:
    static constexpr int kFps = 8;
    static constexpr int kMaxSegundos = 5;
    static constexpr int kLado = 288; // nítido en una tecla de 144 px y en la tablet

    CarasBotonera(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);

    bool ocupada() const { return enCurso_ > 0; }
    QString estado() const { return estado_; }
    QString raizUrl() const;

    // Cada toma recibe su id al empezar y termina con `lista(id, error)`.
    // Devuelven {id} o {error} (si ni empezó).
    /// Un archivo (ruta o file:// URL): imagen, GIF animado o video (los primeros 5 s).
    Q_INVOKABLE QVariantMap importar(const QString& archivo);
    /// Lo que muestra la entrada ahora.
    Q_INVOKABLE QVariantMap capturarFoto(const QString& entrada);
    /// `segundos` (1-5) de la entrada a 8 cuadros/s.
    Q_INVOKABLE QVariantMap grabar(const QString& entrada, double segundos);

    /// {id, url (el primer cuadro), base (file:// de la carpeta, con «/»),
    /// cuadros, video, medio (ruta relativa a los datos, para medio_poner)} o {}.
    Q_INVOKABLE QVariantMap info(const QString& id) const;

    /// Para la botonera: el primer cuadro de «medio:<id>» (ruta) y la
    /// referencia liviana {id, cuadros, fps} (null si no existe).
    std::string rutaPoster(const std::string& id) const;
    nlohmann::json cara(const std::string& id) const;
    /// Media/botonera/<id>/<n>.jpg (lo usan el Stream Deck y el gateway).
    static std::string archivoCuadro(int n);

   signals:
    void cambio();
    /// Terminó la toma `id` (la que devolvió importar/capturarFoto/grabar):
    /// error vacío = lista para usar como «medio:<id>».
    void lista(const QString& id, const QString& error);

   private:
    std::filesystem::path carpeta(const QString& id) const;
    QString nuevoId() const;
    /// Recorta al cuadrado del centro y escribe los cuadros; `grande` (si no es
    /// nula) queda como medio.jpg. Devuelve el error o "".
    QString guardar(const QString& id, const QVector<QImage>& cuadros, const QImage& grande,
                    const QString& original = {});
    void empezar(const QString& texto);
    void terminar(const QString& id, const QString& error);

    AppRuntime& rt_;
    std::filesystem::path raiz_;
    int enCurso_ = 0;
    QString estado_;
    mutable std::map<std::string, int> cuantos_; // id -> cuadros (no cambian)
};

} // namespace ba::app::control
