// test_apdf: lo que la web publica, pasado a PDF con lo del bundle (evento/APdf):
// un texto en varios idiomas y escrituras, un Markdown, una imagen, y con WordCraft,
// DeckCraft y GridCraft vendoreados un HTML, una planilla y una presentación. Cada PDF
// se vuelve a leer: que tenga páginas y, el de texto, las palabras.
#include "evento/APdf.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

#include "tests/RutasPrueba.h"

namespace {
int fallas = 0;

void chequear(bool ok, const char* que, const QString& detalle = {}) {
    std::printf("%-46s %s %s\n", que, ok ? "ok" : "FALLA", detalle.toUtf8().constData());
    if (!ok) ++fallas;
}

/// Las páginas del PDF (0 si no se puede leer) y el texto de la primera.
int paginas(const QString& pdf, QString* texto = nullptr) {
    QPdfDocument d;
    if (d.load(pdf) != QPdfDocument::Error::None) return 0;
    if (texto != nullptr && d.pageCount() > 0) *texto = d.getAllText(0).text();
    return d.pageCount();
}

/// La primera de `candidatas` que existe ("" si ninguna).
QString primera(const QStringList& candidatas) {
    for (const QString& c : candidatas)
        if (!c.isEmpty() && QFileInfo::exists(c)) return QDir::cleanPath(c);
    return {};
}

/// APdf::archivo, esperando que termine (con tope).
QString convertir(ba::app::APdf& c, const QString& origen, const QString& destino) {
    QString error = "no terminó";
    QEventLoop lazo;
    c.archivo(origen, destino, [&](const QString& e) {
        error = e;
        lazo.quit();
    });
    QTimer::singleShot(200000, &lazo, &QEventLoop::quit);
    lazo.exec();
    return error;
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Lo que el test lee, buscado como en la app y no solo con la ruta del árbol donde se
    // compiló (en Docker, /trabajo/ba: copiado a bin\64bit de una PC con Windows no existe):
    // la variable de entorno, al lado del ejecutable (el bundle: <bin>/../../data/fonts) y el
    // árbol del build.
    const QString exe = QString::fromLocal8Bit(ba_prueba_carpeta_exe());
    // Las tipografías del bundle (extern/fuentes-texto → data/fonts). Con la plataforma
    // offscreen, Qt en Windows lee las fuentes de <qt>/lib/fonts (que Qt ya no trae) y el PDF
    // del Markdown salía sin texto; la app usa las de la máquina y suma las del bundle.
    const QString fuentes = primera({qEnvironmentVariable("BA_FUENTES_TEXTO"), exe + "/../../data/fonts",
#ifdef BA_TEST_FUENTES
                                     QStringLiteral(BA_TEST_FUENTES),
#endif
                                     QString()});
    if (!fuentes.isEmpty() && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) qputenv("QT_QPA_FONTDIR", fuentes.toLocal8Bit());
    const QString datosPrueba = primera({qEnvironmentVariable("BA_TEST_DATOS"), exe + "/datos", QStringLiteral(BA_TEST_DATOS)});
    std::printf("tipografías: %s; datos: %s\n", fuentes.isEmpty() ? "(las de la máquina)" : fuentes.toUtf8().constData(),
                datosPrueba.isEmpty() ? "(no están: BA_TEST_DATOS=<carpeta con tres-laminas.pptx>)" : datosPrueba.toUtf8().constData());
    QTemporaryDir datos;
    qputenv("BA_DATA_ROOT", datos.path().toUtf8()); // los temporales de APdf, acá
    QGuiApplication app(argc, argv);
    const QString d = datos.path();
    ba::app::APdf c;

    // 1. Un texto (Markdown) en varios idiomas.
    {
        const QString md = "# Orden del día\n\nPunto **uno**: presupuesto.\n\n- Español: «sesión»\n- English: session\n"
                           "- Ελληνικά: σύνοδος\n- Русский: заседание\n";
        const QString e = ba::app::APdf::texto(md, "markdown", "Prueba", d + "/texto.pdf");
        QString t;
        const int n = paginas(d + "/texto.pdf", &t);
        chequear(e.isEmpty() && n == 1 && t.contains("presupuesto") && t.contains("sesión"), "texto Markdown (varios idiomas)",
                 e.isEmpty() ? QString::number(n) + " pág." : e);
    }
    // 2. Un archivo .md.
    {
        QFile f(d + "/nota.md");
        f.open(QIODevice::WriteOnly);
        f.write("# Nota\n\nUna línea con *énfasis*.\n");
        f.close();
        const QString e = convertir(c, d + "/nota.md", d + "/nota.pdf");
        chequear(e.isEmpty() && paginas(d + "/nota.pdf") == 1, "archivo .md", e);
    }
    // 3. Una imagen.
    {
        QImage img(800, 400, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        img.save(d + "/foto.png");
        const QString e = convertir(c, d + "/foto.png", d + "/foto.pdf");
        chequear(e.isEmpty() && paginas(d + "/foto.pdf") == 1, "imagen", e);
    }
    // 4. Lo que no sabe: lo dice.
    {
        const QString e = convertir(c, d + "/foto.png", d + "/x.pdf"); // existe; ahora uno raro
        QFile f(d + "/raro.xyz");
        f.open(QIODevice::WriteOnly);
        f.write("x");
        f.close();
        const QString e2 = convertir(c, d + "/raro.xyz", d + "/raro.pdf");
        chequear(e.isEmpty() && !e2.isEmpty(), "formato desconocido: error claro", e2);
    }
#ifdef BA_CRAFT_DIR
    // 5. Un HTML (WordCraft).
    {
        QFile f(d + "/pagina.html");
        f.open(QIODevice::WriteOnly);
        f.write("<html><body><h1>Documento</h1><p>Texto de la página web.</p><table border=1><tr><td>a</td><td>b</td></tr></table>"
                "</body></html>");
        f.close();
        const QString e = convertir(c, d + "/pagina.html", d + "/pagina.pdf");
        QString t;
        const int n = paginas(d + "/pagina.pdf", &t);
        chequear(e.isEmpty() && n >= 1 && t.contains("Documento"), "HTML (WordCraft)", e.isEmpty() ? QString::number(n) + " pág." : e);
    }
    // 6. Una planilla (CSV: GridCraft).
    {
        QFile f(d + "/planilla.csv");
        f.open(QIODevice::WriteOnly);
        f.write("Punto,Votos\nPresupuesto,12\nObras,9\n");
        f.close();
        const QString e = convertir(c, d + "/planilla.csv", d + "/planilla.pdf");
        QString t;
        const int n = paginas(d + "/planilla.pdf", &t);
        chequear(e.isEmpty() && n >= 1 && t.contains("Presupuesto"), "planilla .csv (GridCraft)", e.isEmpty() ? QString::number(n) + " pág." : e);
    }
    // 7. Una presentación (DeckCraft).
    {
        const QString e = convertir(c, datosPrueba + "/tres-laminas.pptx", d + "/laminas.pdf");
        const int n = paginas(d + "/laminas.pdf");
        chequear(e.isEmpty() && n == 3, "presentación .pptx (DeckCraft)", e.isEmpty() ? QString::number(n) + " pág." : e);
    }
#else
    std::printf("(sin extern/craft: HTML, planilla y .pptx no se prueban)\n");
#endif
    // Nada quedó en los temporales.
    const QStringList quedan = QDir(d + "/Runtime/tmp/pdf").entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
    chequear(quedan.isEmpty(), "temporales limpios", quedan.join(", "));
    std::printf("test_apdf: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
