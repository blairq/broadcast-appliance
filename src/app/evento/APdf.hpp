// ============================================================================
// src/app/evento/APdf.hpp — convertir a PDF lo que se publica
//
// La web del evento publica PDF (la documentación, por punto y por sección): lo
// que trae la gente (un Word, una planilla, una presentación, una página web, un
// texto, una foto) se convierte acá, con lo que viaja en el bundle y nada del
// sistema:
//
//   - Word, OpenDocument texto, RTF y HTML: WordCraft; PowerPoint (.pptx): DeckCraft;
//     Excel, OpenDocument planilla y CSV: GridCraft. Sus herramientas de línea de
//     comandos, las del carrusel (carrusel::Conversor::herramienta, tools/deps/craft.sh).
//   - Markdown y texto (o un texto que manda el asistente): Qt (QTextDocument a
//     QPdfWriter), con las fuentes del sistema para cualquier idioma y escritura.
//   - Imágenes: una página del tamaño justo (Qt).
//   - Un PDF: se copia.
//
// SIN CHROME: en macOS, Chrome sin pantalla pide el llavero (su «Safe Storage») y
// no es del bundle. Los temporales van a <datos>/Runtime/tmp/pdf (el pendrive).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace ba::app {

class APdf : public QObject {
    Q_OBJECT
   public:
    /// "" si salió bien; si no, por qué.
    using Listo = std::function<void(const QString& error)>;

    explicit APdf(QObject* parent = nullptr);

    /// ¿Sabe convertir este archivo? (por la extensión)
    static bool sabe(const QString& archivo);
    /// ¿Pasa por WordCraft, DeckCraft o GridCraft? (los de oficina y el HTML)
    static bool necesitaConvertir(const QString& archivo);
    /// Las extensiones que sabe convertir, para mostrar.
    static QString formatos();

    /// Convierte `origen` a `destino` (.pdf). Asincrónico (un proceso aparte): `listo`
    /// se llama una vez, en el hilo de la interfaz.
    void archivo(const QString& origen, const QString& destino, Listo listo);

    /// Un texto a PDF, en el momento. `formato`: markdown, html o texto. "" o el error.
    static QString texto(const QString& contenido, const QString& formato, const QString& titulo, const QString& destino);
};

} // namespace ba::app
