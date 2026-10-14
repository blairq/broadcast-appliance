// test_apdf: lo que la web publica, pasado a PDF con lo del bundle (evento/APdf):
// un texto en varios idiomas y escrituras, un Markdown, una imagen, un HTML y una
// presentación (estos dos, con el LibreOffice vendoreado). Cada PDF se vuelve a leer:
// que tenga páginas y, el de texto, las palabras.
#include "evento/APdf.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

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
#ifdef BA_LIBREOFFICE_DIR
    // 5. Un HTML (LibreOffice).
    {
        QFile f(d + "/pagina.html");
        f.open(QIODevice::WriteOnly);
        f.write("<html><body><h1>Documento</h1><p>Texto de la página web.</p><table border=1><tr><td>a</td><td>b</td></tr></table>"
                "</body></html>");
        f.close();
        const QString e = convertir(c, d + "/pagina.html", d + "/pagina.pdf");
        QString t;
        const int n = paginas(d + "/pagina.pdf", &t);
        chequear(e.isEmpty() && n >= 1 && t.contains("Documento"), "HTML (LibreOffice)", e.isEmpty() ? QString::number(n) + " pág." : e);
    }
    // 6. Una planilla (CSV: Calc).
    {
        QFile f(d + "/planilla.csv");
        f.open(QIODevice::WriteOnly);
        f.write("Punto,Votos\nPresupuesto,12\nObras,9\n");
        f.close();
        const QString e = convertir(c, d + "/planilla.csv", d + "/planilla.pdf");
        QString t;
        const int n = paginas(d + "/planilla.pdf", &t);
        chequear(e.isEmpty() && n >= 1 && t.contains("Presupuesto"), "planilla .csv (LibreOffice Calc)", e.isEmpty() ? QString::number(n) + " pág." : e);
    }
    // 7. Una presentación (LibreOffice).
    {
        const QString e = convertir(c, QString(BA_TEST_DATOS) + "/tres-laminas.pptx", d + "/laminas.pdf");
        const int n = paginas(d + "/laminas.pdf");
        chequear(e.isEmpty() && n == 3, "presentación .pptx (LibreOffice)", e.isEmpty() ? QString::number(n) + " pág." : e);
    }
#else
    std::printf("(sin LibreOffice en extern/: HTML y .pptx no se prueban)\n");
#endif
    // Nada quedó en los temporales.
    const QStringList quedan = QDir(d + "/Runtime/tmp/pdf").entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
    chequear(quedan.isEmpty(), "temporales limpios", quedan.join(", "));
    std::printf("test_apdf: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
