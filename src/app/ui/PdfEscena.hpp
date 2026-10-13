// ============================================================================
// src/app/ui/PdfEscena.hpp — los PDF en las escenas: pintarlos y pasarles las páginas
//
// Pinta las fuentes «PDF» (emision/FuentePdf, ba_pdf): la página que diga cada una,
// entera y sin deformar, sobre su fondo, al tamaño de su lienzo. Y es el modelo de
// quien las maneja —la tarjeta «PDF» del tablero, el MCP (pdf_escena_*)—: qué
// fuentes PDF hay, qué muestra cada una (documento, página de cuántas), qué PDF se
// pueden elegir (la documentación del evento) y las órdenes: elegir uno, ir a una
// página, la siguiente / la anterior, armar la escena «PDF».
//
// Lo que se elige queda en los AJUSTES de la fuente (viaja con la escena y vuelve
// igual tras reiniciar); este modelo lo lee de ahí y avisa (`cambio`) cuando cambia,
// venga de donde venga (la tarjeta, el MCP, Propiedades).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <map>
#include <memory>

class QPdfDocument;

namespace ba::app {

class AppRuntime;

class PdfEscena : public QObject {
    Q_OBJECT
    /// [{nombre, documento, archivo, titulo, pagina, paginas, error}]
    Q_PROPERTY(QVariantList fuentes READ fuentes NOTIFY cambio)
    /// Los PDF de la documentación del evento: [{id, titulo, paginas, url_miniatura}]
    Q_PROPERTY(QVariantList documentos READ documentos NOTIFY documentosCambio)

   public:
    using json = nlohmann::json;
    explicit PdfEscena(AppRuntime& rt, QObject* parent = nullptr);
    ~PdfEscena() override;

    QVariantList fuentes() const;
    QVariantList documentos() const;
    /// Lo mismo, para el MCP.
    json estado() const;

    /// Muestra en la fuente `fuente` ("" = la primera) el documento `id` de la
    /// documentación del evento, desde la página 1. "" o el error.
    Q_INVOKABLE QString elegir(const QString& fuente, const QString& id);
    /// Ídem con un PDF cualquiera (ruta o URL file://).
    Q_INVOKABLE QString elegirArchivo(const QString& fuente, const QString& ruta);
    /// A la página `pagina` (1 = la primera; se acota a las que tiene).
    Q_INVOKABLE QString irA(const QString& fuente, int pagina);
    /// `delta` páginas para adelante (o para atrás, negativo).
    Q_INVOKABLE QString paso(const QString& fuente, int delta);
    /// Arma la escena `nombre` con una fuente PDF a pantalla completa (si no
    /// existe). Devuelve el error, o "" (y la fuente queda lista para elegir).
    Q_INVOKABLE QString crearEscena(const QString& nombre);
    /// La documentación cambió (agregaron o quitaron un PDF).
    Q_INVOKABLE void refrescarDocumentos();

   signals:
    void cambio();
    void documentosCambio();

   private:
    void tic();
    /// El nombre de la fuente pedida ("" = la primera), o "" si no hay ninguna.
    std::string resolver(const QString& fuente, QString* error) const;
    /// La ruta del PDF que muestra una fuente con estos ajustes ("" si ninguno).
    QString rutaDe(const json& ajustes) const;
    QPdfDocument* abrir(const QString& ruta);
    int paginasDe(const QString& ruta) const;

    AppRuntime& rt_;
    QTimer timer_;
    std::map<std::uint64_t, std::size_t> firmas_; ///< lo último que se pintó, por fuente
    std::size_t firmaLista_ = 0;                  ///< para avisar `cambio` solo si cambió algo
    std::map<QString, std::unique_ptr<QPdfDocument>> docs_; ///< abiertos, por ruta
    QVariantList documentos_;
};

} // namespace ba::app
