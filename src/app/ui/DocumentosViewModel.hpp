// ============================================================================
// src/app/ui/DocumentosViewModel.hpp — la documentación del evento, para la interfaz y el MCP
//
// Sobre evento::Documentos (el índice y los archivos en <evento>/docs/): al agregar un
// PDF lo lee con Qt PDF —cuántas páginas, su título, el texto de las primeras para
// sugerir la descripción— y guarda la carátula (la primera página) como JPG chico en
// docs/miniaturas/, UNA vez: la página web y la interfaz la usan directo. Una imagen
// (png, jpg) es su propia miniatura. Avisa `cambio` y la web del evento se republica.
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app {

class DocumentosViewModel : public QObject {
    Q_OBJECT
    /// [{id, archivo, titulo, descripcion, paginas, bytes, publico, puntos, secciones, miniatura, url_miniatura}]
    Q_PROPERTY(QVariantList lista READ listaUi NOTIFY cambio)

   public:
    using json = nlohmann::json;
    /// `evento`: la carpeta del evento abierto (la de su configuración).
    explicit DocumentosViewModel(std::function<std::filesystem::path()> evento, QObject* parent = nullptr);

    json lista() const;
    QVariantList listaUi() const;
    /// Agrega un archivo (PDF o imagen): lo copia, lo lee y hace su miniatura.
    /// `datos`: {titulo, descripcion, publico, puntos, secciones}. {id, documento} o {error}.
    json agregar(const std::filesystem::path& archivo, const json& datos);
    std::string editar(const std::string& id, const json& cambios);
    std::string quitar(const std::string& id);
    /// El texto del documento, de la página `desde` a `hasta` (1 en adelante; 0 = hasta el final).
    json texto(const std::string& id, int desde, int hasta) const;
    /// Los públicos, como los ve la web.
    json publicos() const;
    std::filesystem::path carpeta() const;

    /// Desde la interfaz: varios archivos (rutas o URL file://). Devuelve los errores ("" si ninguno).
    Q_INVOKABLE QString agregarUi(const QStringList& archivos);
    Q_INVOKABLE QString editarUi(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE QString quitarUi(const QString& id);
    /// Lo abre con el programa del sistema (el visor de PDF).
    Q_INVOKABLE void abrirUi(const QString& id);
    Q_INVOKABLE void abrirCarpetaUi();

   signals:
    void cambio();

   private:
    std::function<std::filesystem::path()> evento_;
};

} // namespace ba::app
