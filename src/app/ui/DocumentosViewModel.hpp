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

#include "evento/APdf.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include "ui/CapturaError.hpp"

namespace ba::app {

class DocumentosViewModel : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// Para las acciones que dicen si se hicieron: el error a quien mira y true si no hubo.
    bool sinError(const QString& e) {
        avisarError(e);
        return e.isEmpty();
    }
    bool sinError(const std::string& e) { return sinError(QString::fromStdString(e)); }
    /// [{id, archivo, titulo, descripcion, paginas, bytes, publico, puntos, secciones, miniatura, url_miniatura}]
    Q_PROPERTY(QVariantList lista READ listaUi NOTIFY cambio)

   public:
    using json = nlohmann::json;
    /// `evento`: la carpeta del evento abierto (la de su configuración).
    explicit DocumentosViewModel(std::function<std::filesystem::path()> evento, QObject* parent = nullptr);

    json lista() const;
    /// Uno (null si no existe).
    json documento(const std::string& id) const;
    QVariantList listaUi() const;
    /// Agrega un archivo (PDF o imagen): lo copia, lo lee y hace su miniatura.
    /// `datos`: {titulo, descripcion, publico, puntos, secciones}. {id, documento} o {error}.
    json agregar(const std::filesystem::path& archivo, const json& datos);
    /// Lo mismo para cualquier cosa que se pueda pasar a PDF (Word, planilla,
    /// presentación, HTML, Markdown, texto: ver APdf): primero la convierte y después
    /// la agrega. Asincrónico: `listo` recibe lo de agregar ({id, documento} o {error}).
    void agregarConvirtiendo(const std::filesystem::path& archivo, const json& datos, std::function<void(json)> listo);
    /// El conversor (lo usa también la herramienta pdf_convertir).
    APdf& aPdf() { return *aPdf_; }
    std::string editar(const std::string& id, const json& cambios);
    std::string quitar(const std::string& id);
    /// El texto del documento, de la página `desde` a `hasta` (1 en adelante; 0 = hasta el final).
    json texto(const std::string& id, int desde, int hasta) const;
    /// Los públicos, como los ve la web.
    json publicos() const;
    std::filesystem::path carpeta() const;

    /// Desde la interfaz: varios archivos (rutas o URL file://). Devuelve los errores ("" si
    /// ninguno); los que hay que convertir avisan después (notify).
    Q_INVOKABLE void agregarUi(const QStringList& archivos);
    Q_INVOKABLE void editarUi(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE bool quitarUi(const QString& id);
    /// Lo abre con el programa del sistema (el visor de PDF).
    Q_INVOKABLE void abrirUi(const QString& id);
    Q_INVOKABLE void abrirCarpetaUi();

   signals:
    void cambio();
    /// Un aviso para la interfaz (una conversión que terminó mal, o que está en curso).
    void notify(const QString& texto, const QString& tipo);

   private:
    std::function<std::filesystem::path()> evento_;
    APdf* aPdf_ = nullptr;
    /// listaUi armada una vez por cambio (y por evento): la QML la recorre (sort, filter) y
    /// cada acceso a un elemento vuelve a leer la propiedad; leía el índice del disco cada vez
    /// (44 lecturas al abrir Asamblea → Orden con el detector de acoples).
    mutable QVariantList listaCache_;
    mutable std::filesystem::path listaDe_;
    mutable bool listaSucia_ = true;
};

} // namespace ba::app
