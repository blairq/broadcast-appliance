// ============================================================================
// src/app/asamblea/InformesResultado.hpp — el resultado de la sesión en PDF, con plantillas
//
// Una plantilla es Markdown con campos que se rellenan con lo que pasó en la sesión:
// {{cuerpo}}, {{quorum.presentes}}, y listas que se repiten, {{#puntos}}…{{/puntos}}
// (o lo de adentro de {{^puntos}}…{{/puntos}} si no hay ninguno). Los campos están en
// campos(). Hay varias por evento (Config/informes/<id>.md, con sus nombres en
// Config/informes/plantillas.json); la primera vez se crea una de fábrica en el
// idioma del evento (plantillas/informe-resultado.md). Generar rellena la elegida y
// la pasa a PDF en la carpeta del evento (informes/<id>-<fecha>.pdf, y el .md al lado).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app::asamblea {

class InformesResultado : public QObject {
    Q_OBJECT
    /// [{id, nombre}]
    Q_PROPERTY(QVariantList plantillas READ plantillasUi NOTIFY cambio)
    /// [{campo, que}]: lo que se puede poner en una plantilla.
    Q_PROPERTY(QVariantList campos READ camposUi CONSTANT)
   public:
    using json = nlohmann::json;
    /// Markdown a PDF: (texto, título, destino) -> "" o el error (APdf::texto).
    using APdf = std::function<QString(const QString& markdown, const QString& titulo, const QString& destino)>;
    /// `datos`: la carpeta de datos. `contenido`: lo de la sesión para rellenar.
    /// `salida`: la carpeta del evento abierto (vacía si no hay).
    InformesResultado(const std::filesystem::path& datos, std::function<json()> contenido,
                      std::function<std::filesystem::path()> salida, APdf aPdf, QObject* parent = nullptr);

    /// Rellena una plantilla (ver arriba). Los campos que no están quedan vacíos.
    static std::string rellenar(const std::string& plantilla, const json& datos);
    static json campos();

    json plantillas() const;
    /// El texto de una plantilla ("" si no existe).
    std::string texto(const std::string& id) const;
    /// Una nueva con ese nombre (con el texto dado o, vacío, el de fábrica). Su id, o "" y `error`.
    std::string crear(const std::string& nombre, const std::string& texto, std::string* error = nullptr);
    /// Cambia el texto (y el nombre, si no está vacío). "" o el error.
    std::string guardar(const std::string& id, const std::string& texto, const std::string& nombre = {});
    std::string quitar(const std::string& id);
    /// La plantilla rellenada con lo de ahora, en Markdown.
    std::string vista(const std::string& id) const;
    /// Rellena y pasa a PDF: {archivo, markdown} o {error}.
    json generar(const std::string& id);

    QVariantList plantillasUi() const;
    QVariantList camposUi() const;
    Q_INVOKABLE QString textoUi(const QString& id) const { return QString::fromStdString(texto(id.toStdString())); }
    /// {id} o {error}.
    Q_INVOKABLE QVariantMap crearUi(const QString& nombre);
    Q_INVOKABLE QString guardarUi(const QString& id, const QString& texto, const QString& nombre);
    Q_INVOKABLE QString quitarUi(const QString& id) { return QString::fromStdString(quitar(id.toStdString())); }
    Q_INVOKABLE QString vistaUi(const QString& id) const { return QString::fromStdString(vista(id.toStdString())); }
    /// {archivo} o {error}.
    Q_INVOKABLE QVariantMap generarUi(const QString& id);

   signals:
    void cambio();

   private:
    std::filesystem::path carpeta() const { return carpeta_; }
    json indice() const;
    void guardarIndice(const json& i) const;
    /// La de fábrica, si todavía no hay ninguna.
    void asegurarUna();
    /// Lo de crear() sin avisar (cambio): para asegurarUna, que corre al leer.
    std::string agregar(const std::string& nombre, const std::string& texto, std::string* error);
    std::filesystem::path carpeta_;
    std::function<json()> contenido_;
    std::function<std::filesystem::path()> salida_;
    APdf aPdf_;
};

} // namespace ba::app::asamblea
