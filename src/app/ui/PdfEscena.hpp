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

#include <functional>
#include <map>
#include <memory>
#include "ui/CapturaError.hpp"

class QPdfDocument;

namespace ba::app {

class AppRuntime;
class VentanillaMotor;

class PdfEscena : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// [{nombre, documento, archivo, titulo, pagina, paginas, zoom, x, y, error}]
    Q_PROPERTY(QVariantList fuentes READ fuentes NOTIFY cambio)
    /// Los PDF de la documentación del evento: [{id, titulo, paginas, url_miniatura}]
    Q_PROPERTY(QVariantList documentos READ documentos NOTIFY documentosCambio)

   public:
    using json = nlohmann::json;
    PdfEscena(AppRuntime& rt, VentanillaMotor& ventanilla, QObject* parent = nullptr);
    ~PdfEscena() override;

    QVariantList fuentes() const;
    QVariantList documentos() const;
    /// Lo mismo, para el MCP.
    json estado() const;

    /// Muestra en la fuente `fuente` ("" = la primera) el documento `id` de la
    /// documentación del evento, desde la página 1. El error va por la señal del modelo.
    Q_INVOKABLE void elegir(const QString& fuente, const QString& id);
    /// Ídem con un PDF cualquiera (ruta o URL file://).
    Q_INVOKABLE void elegirArchivo(const QString& fuente, const QString& ruta);
    /// A la página `pagina` (1 = la primera; se acota a las que tiene).
    Q_INVOKABLE void irA(const QString& fuente, int pagina);
    /// `delta` páginas para adelante (o para atrás, negativo).
    Q_INVOKABLE void paso(const QString& fuente, int delta);
    /// El zoom (1 = la página entera; hasta 8) y el centro de lo que se ve (x, y de 0 a 1
    /// sobre la página); fuera de rango se acota. Un valor < 0 deja el que estaba.
    Q_INVOKABLE void encuadre(const QString& fuente, double zoom, double x, double y);
    /// Corre lo que se ve (`dx`, `dy`: fracciones de lo visible; positivo = derecha / abajo),
    /// sin salirse de la página. Para el joystick.
    Q_INVOKABLE void mover(const QString& fuente, double dx, double dy);
    /// Arma la escena `nombre` con una fuente PDF a pantalla completa (si no existe):
    /// lo pide al dueño del motor; el error vuelve con `notify` (y la fuente, en `fuentes`).
    Q_INVOKABLE void crearEscena(const QString& nombre);
    /// Lo mismo, con lo que vuelva ("" o el error) en `listo` (el MCP).
    void crearEscena(const QString& nombre, std::function<void(const QString&)> listo);
    /// La documentación cambió (agregaron o quitaron un PDF).
    void refrescarDocumentos();

   signals:
    void cambio();
    void documentosCambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    /// EN EL DUEÑO DEL MOTOR: la escena `esc` con su fuente PDF (mostrando `documento`). "" o el error.
    static QString armarEscena(AppRuntime& rt, const std::string& esc, const std::string& documento);
    void tic();
    /// El nombre de la fuente pedida ("" = la primera), o "" si no hay ninguna.
    std::string resolver(const QString& fuente, QString* error) const;
    /// La ruta del PDF que muestra una fuente con estos ajustes ("" si ninguno).
    QString rutaDe(const json& ajustes) const;
    QPdfDocument* abrir(const QString& ruta);
    int paginasDe(const QString& ruta) const;
    /// La página en el lienzo con ese zoom: su tamaño escalado (W, H) y lo visible (vw, vh).
    struct Geometria {
        double W = 0, H = 0, vw = 0, vh = 0;
    };
    Geometria geometria(const nlohmann::json& ajustes);

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    QTimer timer_;
    std::map<std::uint64_t, std::size_t> firmas_; ///< lo último que se pintó, por fuente
    std::size_t firmaLista_ = 0;                  ///< para avisar `cambio` solo si cambió algo
    std::map<QString, std::unique_ptr<QPdfDocument>> docs_; ///< abiertos, por ruta
    QVariantList documentos_;
};

} // namespace ba::app
