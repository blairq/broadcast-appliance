#include "carrusel/Conversion.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace ba::carrusel {
namespace {

/// Lo que LibreOffice abre y nosotros queremos. `.key` de Keynote NO está: lo
/// abre mal y media lámina sale en blanco, que es peor que decir que no.
const QStringList kExtensiones = {"pdf", "pptx", "ppt", "odp", "odg", "fodp"};

QString extension(const QString& archivo) { return QFileInfo(archivo).suffix().toLower(); }

/// Convertir tarda, pero no una hora: si LibreOffice no terminó en cinco
/// minutos es que se quedó esperando algo que nunca va a llegar (un diálogo
/// que en headless nadie puede contestar) y lo mejor es decirlo.
constexpr int kLimiteOfficeMs = 5 * 60 * 1000;

} // namespace

struct Conversor::Impl {
    QPointer<QProcess> proc;
    std::unique_ptr<QTemporaryDir> temporal;
    std::unique_ptr<QPdfDocument> pdf;
    QString destino;
    QStringList salida;
    int ancho = 1920;
    int pagina = 0;
    bool cancelado = false;
    bool corriendo = false;

    void limpiar() {
        if (proc) { proc->kill(); proc->deleteLater(); proc = nullptr; }
        pdf.reset();
        temporal.reset();
        corriendo = false;
    }
};

Conversor::Conversor(QObject* parent) : QObject(parent), d_(new Impl) {}

Conversor::~Conversor() {
    d_->limpiar();
    delete d_;
}

bool Conversor::esDocumento(const QString& archivo) {
    return kExtensiones.contains(extension(archivo));
}

bool Conversor::necesitaOffice(const QString& archivo) {
    return esDocumento(archivo) && extension(archivo) != "pdf";
}

QString Conversor::soffice() {
    // Primero lo que diga el entorno: es como los tests apuntan al de extern/
    // sin tener que instalar el bundle entero para probar una conversión.
    const QString env = qEnvironmentVariable("BA_SOFFICE");
    if (!env.isEmpty() && QFileInfo::exists(env)) return env;

    QStringList velas;
    const QString dir = QCoreApplication::applicationDirPath();
    for (const QString& base : {dir + "/libreoffice", dir + "/../libreoffice"})
        velas << base + "/program/soffice" << base + "/program/soffice.exe";
#ifdef BA_LIBREOFFICE_DIR
    // El árbol de extern/, para correr desde el directorio de compilación.
    velas << QString(BA_LIBREOFFICE_DIR) + "/program/soffice"
          << QString(BA_LIBREOFFICE_DIR) + "/program/soffice.exe";
#endif
    for (const QString& c : velas)
        if (QFileInfo(c).isExecutable()) return QFileInfo(c).absoluteFilePath();
    return {};
}

bool Conversor::ocupado() const { return d_->corriendo; }

void Conversor::cancelar() {
    if (!d_->corriendo) return;
    d_->cancelado = true;
    d_->limpiar();
    emit fallo("cancelado");
}

void Conversor::convertir(const QString& archivo, const QString& destino, int anchoPx) {
    if (d_->corriendo) { emit fallo("ya hay una conversión en curso"); return; }
    if (!QFileInfo::exists(archivo)) { emit fallo("no existe: " + archivo); return; }
    if (!esDocumento(archivo)) { emit fallo("no sé desarmar " + extension(archivo)); return; }
    if (!QDir().mkpath(destino)) { emit fallo("no puedo escribir en " + destino); return; }

    d_->cancelado = false;
    d_->corriendo = true;
    d_->destino = destino;
    d_->salida.clear();
    d_->ancho = anchoPx > 0 ? anchoPx : 1920;
    d_->pagina = 0;

    if (!necesitaOffice(archivo)) { arrancarPaginas(archivo); return; }

    const QString bin = soffice();
    if (bin.isEmpty()) {
        d_->corriendo = false;
        emit fallo("este equipo no trae LibreOffice: convertí la presentación a PDF y traé el PDF");
        return;
    }

    d_->temporal = std::make_unique<QTemporaryDir>();
    if (!d_->temporal->isValid()) {
        d_->corriendo = false;
        emit fallo("no pude crear una carpeta temporal");
        return;
    }
    const QString tmp = d_->temporal->path();

    emit avance("Convirtiendo la presentación", 0, 0);

    d_->proc = new QProcess(this);
    // El perfil va al temporal: sin esto, dos conversiones a la vez chocan por
    // el mismo ~/.config y la segunda muere sin decir por qué.
    d_->proc->setArguments({"--headless", "--norestore", "--invisible",
                            "-env:UserInstallation=" + QUrl::fromLocalFile(tmp + "/perfil").toString(),
                            "--convert-to", "pdf", "--outdir", tmp, archivo});
    d_->proc->setProgram(bin);

    const QString esperado = tmp + "/" + QFileInfo(archivo).completeBaseName() + ".pdf";
    connect(d_->proc, &QProcess::finished, this, [this, esperado](int, QProcess::ExitStatus) {
        if (d_->cancelado) return;
        // El código de salida miente: LibreOffice contesta 0 aunque no haya
        // escrito nada. Lo que vale es si el PDF está.
        if (!QFileInfo::exists(esperado)) {
            d_->limpiar();
            emit fallo("LibreOffice no pudo abrir el archivo (¿está dañado?)");
            return;
        }
        arrancarPaginas(esperado);
    });
    connect(d_->proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (d_->cancelado) return;
        d_->limpiar();
        emit fallo("no se pudo arrancar LibreOffice");
    });
    QTimer::singleShot(kLimiteOfficeMs, this, [this]() {
        if (d_->corriendo && d_->proc) {
            d_->limpiar();
            emit fallo("LibreOffice tardó demasiado y lo corté");
        }
    });
    d_->proc->start();
}

void Conversor::arrancarPaginas(const QString& rutaPdf) {
    d_->pdf = std::make_unique<QPdfDocument>();
    if (d_->pdf->load(rutaPdf) != QPdfDocument::Error::None) {
        d_->limpiar();
        emit fallo("el PDF no se pudo leer");
        return;
    }
    if (d_->pdf->pageCount() <= 0) {
        d_->limpiar();
        emit fallo("el PDF no tiene páginas");
        return;
    }
    emit avance("Armando las láminas", 0, d_->pdf->pageCount());
    QTimer::singleShot(0, this, &Conversor::rendirUna);
}

void Conversor::rendirUna() {
    if (d_->cancelado || !d_->pdf) return;
    const int total = d_->pdf->pageCount();
    const int i = d_->pagina;

    // Cada página con SU proporción: un PDF puede mezclar apaisado y vertical,
    // y forzar un alto fijo deformaría la mitad.
    const QSizeF pt = d_->pdf->pagePointSize(i);
    const double prop = pt.height() > 0 ? pt.height() / pt.width() : 9.0 / 16.0;
    const QSize px(d_->ancho, qMax(1, int(d_->ancho * prop)));

    const QImage img = d_->pdf->render(i, px, QPdfDocumentRenderOptions{});
    const QString nombre = QString("lamina-%1.png").arg(i + 1, 3, 10, QChar('0'));
    const QString ruta = d_->destino + "/" + nombre;
    if (img.isNull() || !img.save(ruta, "PNG")) {
        d_->limpiar();
        emit fallo(QString("no pude escribir la lámina %1").arg(i + 1));
        return;
    }
    d_->salida << ruta;
    d_->pagina++;
    emit avance("Armando las láminas", d_->pagina, total);

    if (d_->pagina >= total) {
        const QStringList hechas = d_->salida;
        d_->limpiar();
        emit listo(hechas);
        return;
    }
    // Volver por el bucle de eventos y no por un `for`: así la ventana sigue
    // respondiendo y el botón de cancelar sirve para algo.
    QTimer::singleShot(0, this, &Conversor::rendirUna);
}

} // namespace ba::carrusel
