// src/app/evento/APdf.cpp — ver APdf.hpp

#include "evento/APdf.hpp"

#include "carrusel/Conversion.hpp"
#include "platform/Paths.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <memory>

namespace ba::app {

namespace {

const QStringList kOffice = {"doc", "docx", "odt", "fodt", "rtf", "ppt", "pptx", "odp", "fodp",
                             "xls", "xlsx", "ods", "fods", "csv", "html", "htm", "xhtml"};
const QStringList kTexto = {"md", "markdown", "txt"};
const QStringList kImagen = {"png", "jpg", "jpeg", "webp", "gif", "bmp", "tif", "tiff"};

QString ext(const QString& archivo) { return QFileInfo(archivo).suffix().toLower(); }

/// Una carpeta temporal propia, en los datos (el pendrive), no en la del sistema.
QString temporal() {
    const QString base = QString::fromStdU16String((ba::platform::dataRoot() / "Runtime" / "tmp" / "pdf").u16string());
    const QString d = base + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(d);
    return d;
}

/// Escribe `doc` en un PDF A4 con márgenes. "" o el error.
QString imprimir(QTextDocument& doc, const QString& titulo, const QString& destino) {
    QDir().mkpath(QFileInfo(destino).absolutePath());
    {
        QPdfWriter w(destino);
        w.setPageSize(QPageSize(QPageSize::A4));
        w.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
        w.setTitle(titulo);
        w.setCreator("Broadcast Appliance");
        doc.print(&w);
    }
    return QFileInfo(destino).size() > 0 ? QString() : QString("no se pudo escribir %1").arg(destino);
}

/// Una imagen en una página de su forma (A4 vertical u horizontal), centrada.
QString imagen(const QString& origen, const QString& destino) {
    QImageReader r(origen);
    r.setAutoTransform(true);
    const QImage img = r.read();
    if (img.isNull()) return "no se pudo leer la imagen: " + r.errorString();
    QDir().mkpath(QFileInfo(destino).absolutePath());
    {
        QPdfWriter w(destino);
        w.setPageSize(QPageSize(QPageSize::A4));
        w.setPageOrientation(img.width() > img.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        w.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);
        w.setTitle(QFileInfo(origen).completeBaseName());
        w.setCreator("Broadcast Appliance");
        QPainter p(&w);
        const QRect area = p.viewport();
        const QSize tam = img.size().scaled(area.size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(area.x() + (area.width() - tam.width()) / 2, area.y() + (area.height() - tam.height()) / 2,
                          tam.width(), tam.height()),
                    img);
    }
    return QFileInfo(destino).size() > 0 ? QString() : QString("no se pudo escribir %1").arg(destino);
}

} // namespace

APdf::APdf(QObject* parent) : QObject(parent) {}

bool APdf::sabe(const QString& archivo) {
    const QString e = ext(archivo);
    return e == "pdf" || kOffice.contains(e) || kTexto.contains(e) || kImagen.contains(e);
}

bool APdf::necesitaOffice(const QString& archivo) { return kOffice.contains(ext(archivo)); }

QString APdf::formatos() { return (QStringList{"pdf"} + kOffice + kTexto + kImagen).join(", "); }

QString APdf::texto(const QString& contenido, const QString& formato, const QString& titulo, const QString& destino) {
    QTextDocument doc;
    const QString f = formato.toLower();
    if (f == "html")
        doc.setHtml(contenido);
    else if (f == "texto" || f == "txt")
        doc.setPlainText(contenido);
    else
        doc.setMarkdown(contenido);
    if (!titulo.isEmpty()) doc.setMetaInformation(QTextDocument::DocumentTitle, titulo);
    return imprimir(doc, titulo, destino);
}

void APdf::archivo(const QString& origen, const QString& destino, Listo listo) {
    const QString e = ext(origen);
    const auto avisar = [listo](const QString& err) {
        // Siempre después (nunca adentro de la llamada): el que llama ya terminó lo suyo.
        QTimer::singleShot(0, [listo, err]() { listo(err); });
    };
    if (!QFileInfo(origen).isFile()) return avisar("no existe el archivo " + origen);
    if (!sabe(origen)) return avisar("no sé convertir «." + e + "» a PDF (sé: " + formatos() + ")");
    QDir().mkpath(QFileInfo(destino).absolutePath());
    if (e == "pdf") {
        QFile::remove(destino);
        return avisar(QFile::copy(origen, destino) ? QString() : "no se pudo copiar a " + destino);
    }
    if (kImagen.contains(e)) return avisar(imagen(origen, destino));
    if (kTexto.contains(e)) {
        QFile f(origen);
        if (!f.open(QIODevice::ReadOnly)) return avisar("no se pudo leer " + origen);
        const QString contenido = QString::fromUtf8(f.readAll());
        return avisar(texto(contenido, e == "txt" ? "texto" : "markdown", QFileInfo(origen).completeBaseName(), destino));
    }
    // De oficina (y HTML): LibreOffice. Sin él, el HTML todavía sale con Qt.
    const QString bin = carrusel::Conversor::soffice();
    if (bin.isEmpty()) {
        if (e == "html" || e == "htm" || e == "xhtml") {
            QFile f(origen);
            if (!f.open(QIODevice::ReadOnly)) return avisar("no se pudo leer " + origen);
            QTextDocument doc;
            doc.setBaseUrl(QUrl::fromLocalFile(QFileInfo(origen).absolutePath() + "/")); // las imágenes relativas
            doc.setHtml(QString::fromUtf8(f.readAll()));
            return avisar(imprimir(doc, QFileInfo(origen).completeBaseName(), destino));
        }
        return avisar("este equipo no trae LibreOffice para convertir «." + e + "»");
    }
    const QString tmp = temporal();
    auto* p = new QProcess(this);
    carrusel::Conversor::prepararOffice(*p, bin, origen, tmp, tmp + "/perfil");
    p->setProcessChannelMode(QProcess::MergedChannels);
    auto hecho = std::make_shared<bool>(false);
    const QString esperado = tmp + "/" + QFileInfo(origen).completeBaseName() + ".pdf";
    const auto terminar = [p, tmp, hecho, listo](const QString& err) {
        if (*hecho) return;
        *hecho = true;
        p->deleteLater();
        QDir(tmp).removeRecursively();
        listo(err);
    };
    connect(p, &QProcess::finished, this, [p, esperado, destino, terminar](int, QProcess::ExitStatus) {
        // El código de salida miente (0 sin haber escrito nada): vale si el PDF está.
        if (!QFileInfo::exists(esperado)) {
            const QString dijo = QString::fromLocal8Bit(p->readAll()).trimmed();
            return terminar(dijo.isEmpty() ? QString("LibreOffice no pudo abrir el archivo (¿está dañado?)")
                                           : "LibreOffice: " + dijo.left(600));
        }
        QFile::remove(destino);
        terminar(QFile::rename(esperado, destino) || QFile::copy(esperado, destino) ? QString()
                                                                                    : "no se pudo escribir " + destino);
    });
    connect(p, &QProcess::errorOccurred, this, [terminar](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) terminar("no arrancó LibreOffice");
    });
    // Uno trabado no deja la conversión colgada para siempre.
    QTimer::singleShot(180000, p, [p, terminar]() {
        p->kill();
        terminar("LibreOffice tardó más de 3 minutos: se cortó");
    });
    p->start();
}

} // namespace ba::app
