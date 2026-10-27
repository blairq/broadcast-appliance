#include "carrusel/Conversion.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace ba::carrusel {
namespace {

/// Lo que DeckCraft abre (y un PDF, que no pasa por nadie). El .ppt viejo (binario)
/// y el .odp NO: DeckCraft no los lee; se exportan a .pptx o a PDF. `.deckcraft` es
/// lo que arma el asistente con el MCP de DeckCraft.
const QStringList kExtensiones = {"pdf", "pptx", "deckcraft"};

QString extension(const QString& archivo) { return QFileInfo(archivo).suffix().toLower(); }

/// Convertir tarda milisegundos, no una hora: si no terminó en cinco minutos se
/// trabó y lo mejor es decirlo.
inline int kLimiteConversionMs() { return ba::ajustes::entero("carrusel.limiteOfficeMin", 5) * 60 * 1000; }

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
        if (proc) {
            proc->kill();
            proc->deleteLater();
            proc = nullptr;
        }
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

bool Conversor::esDocumento(const QString& archivo) { return kExtensiones.contains(extension(archivo)); }

bool Conversor::necesitaConvertir(const QString& archivo) {
    return esDocumento(archivo) && extension(archivo) != "pdf";
}

QString Conversor::herramienta(const QString& nombre) {
#ifdef Q_OS_WIN
    const QString exe = nombre + "-cli.exe";
#else
    const QString exe = nombre + "-cli";
#endif
    QStringList velas;
    // Primero lo que diga el entorno: es como los tests apuntan al de extern/ sin
    // instalar el bundle entero.
    const QString env = qEnvironmentVariable("BA_CRAFT_DIR");
    if (!env.isEmpty()) velas << env + "/" + exe;
    // SIEMPRE la que viaja con nosotros (bin/craft, al lado de bin/64bit): nunca una
    // del sistema, que en otra máquina puede no estar (el aparato es portable).
    const QString dir = QCoreApplication::applicationDirPath();
    velas << dir + "/craft/" + exe << dir + "/../craft/" + exe;
#ifdef BA_CRAFT_DIR
    velas << QString(BA_CRAFT_DIR) + "/" + exe; // el de extern/, corriendo desde la compilación
#endif
    for (const QString& c : velas)
        if (QFileInfo(c).isExecutable()) return QFileInfo(c).absoluteFilePath();
    return {};
}

void Conversor::prepararConversion(QProcess& p, const QString& bin, const QString& archivo, const QString& salida) {
    p.setProgram(bin);
    p.setArguments({"convert", archivo, salida});
    // Sus carpetas de usuario, en los datos: convirtiendo no escriben nada, pero si un
    // día guardan un log o una caché, que quede en el pendrive y no en el sistema.
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    const QString casa = QString::fromStdU16String((ba::platform::dataRoot() / "Runtime" / "craft").u16string());
    QDir().mkpath(casa);
    for (const QString& k : {QStringLiteral("HOME"), QStringLiteral("USERPROFILE"), QStringLiteral("APPDATA"),
                             QStringLiteral("LOCALAPPDATA"), QStringLiteral("XDG_CONFIG_HOME"),
                             QStringLiteral("XDG_DATA_HOME"), QStringLiteral("XDG_CACHE_HOME"),
                             QStringLiteral("XDG_STATE_HOME")})
        e.insert(k, casa);
    p.setProcessEnvironment(e);
    // Lo que diga (un archivo que no pudo abrir, una lámina que dejó afuera), junto.
    p.setProcessChannelMode(QProcess::MergedChannels);
}

bool Conversor::ocupado() const { return d_->corriendo; }

void Conversor::cancelar() {
    if (!d_->corriendo) return;
    d_->cancelado = true;
    d_->limpiar();
    emit fallo("cancelado");
}

void Conversor::convertir(const QString& archivo, const QString& destino, int anchoPx) {
    if (d_->corriendo) {
        emit fallo(tr("ya hay una conversión en curso"));
        return;
    }
    if (!QFileInfo::exists(archivo)) {
        emit fallo(tr("no existe: %1").arg(archivo));
        return;
    }
    if (!esDocumento(archivo)) {
        emit fallo(tr("no sé desarmar %1").arg(extension(archivo)));
        return;
    }
    if (!QDir().mkpath(destino)) {
        emit fallo(tr("no puedo escribir en %1").arg(destino));
        return;
    }

    d_->cancelado = false;
    d_->corriendo = true;
    d_->destino = destino;
    d_->salida.clear();
    d_->ancho = anchoPx > 0 ? anchoPx : 1920;
    d_->pagina = 0;

    if (!necesitaConvertir(archivo)) {
        arrancarPaginas(archivo);
        return;
    }

    const QString bin = herramienta("deckcraft");
    if (bin.isEmpty()) {
        d_->corriendo = false;
        emit fallo(tr("este equipo no trae DeckCraft: convertí la presentación a PDF y traé el PDF"));
        return;
    }

    d_->temporal = std::make_unique<QTemporaryDir>();
    if (!d_->temporal->isValid()) {
        d_->corriendo = false;
        emit fallo(tr("no pude crear una carpeta temporal"));
        return;
    }
    const QString tmp = d_->temporal->path();

    emit avance(tr("Convirtiendo la presentación"), 0, 0);

    d_->proc = new QProcess(this);
    const QString esperado = tmp + "/" + QFileInfo(archivo).completeBaseName() + ".pdf";
    prepararConversion(*d_->proc, bin, archivo, esperado);
    connect(d_->proc, &QProcess::finished, this, [this, esperado](int, QProcess::ExitStatus) {
        // Sin proceso ya se aviso (se corto por tiempo): leer su salida seria
        // usar un puntero nulo.
        if (d_->cancelado || !d_->proc) return;
        // Vale si el PDF está (no solo el código de salida). Lo que haya dicho va en el
        // motivo: «no pudo» a secas no dice por dónde empezar a mirar.
        if (!QFileInfo::exists(esperado)) {
            const QString dijo = QString::fromLocal8Bit(d_->proc->readAll()).trimmed();
            d_->limpiar();
            emit fallo(dijo.isEmpty() ? tr("DeckCraft no pudo abrir el archivo (¿está dañado?)")
                                      : "DeckCraft: " + dijo.left(600));
            return;
        }
        arrancarPaginas(esperado);
    });
    connect(d_->proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        // Si se cayó, después llega finished, que avisa con lo que dijo. Limpiar acá
        // borraba el proceso que finished iba a leer.
        if (d_->cancelado || e == QProcess::Crashed) return;
        d_->limpiar();
        emit fallo(tr("no se pudo arrancar DeckCraft"));
    });
    QTimer::singleShot(kLimiteConversionMs(), this, [this]() {
        if (d_->corriendo && d_->proc) {
            d_->limpiar();
            emit fallo(tr("DeckCraft tardó demasiado y lo corté"));
        }
    });
    // Se le cierra la entrada apenas arranca: un hijo con un stdin abierto que
    // nadie va a escribir se queda esperando. Es la misma leccion del
    // despachador de agentes, que se colgaba por exactamente esto.
    connect(d_->proc, &QProcess::started, this, [this]() {
        if (d_->proc) d_->proc->closeWriteChannel();
    });
    d_->proc->start();
}

void Conversor::arrancarPaginas(const QString& rutaPdf) {
    d_->pdf = std::make_unique<QPdfDocument>();
    if (d_->pdf->load(rutaPdf) != QPdfDocument::Error::None) {
        d_->limpiar();
        emit fallo(tr("el PDF no se pudo leer"));
        return;
    }
    if (d_->pdf->pageCount() <= 0) {
        d_->limpiar();
        emit fallo(tr("el PDF no tiene páginas"));
        return;
    }
    emit avance(tr("Armando las láminas"), 0, d_->pdf->pageCount());
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
        emit fallo(tr("no pude escribir la lámina %1").arg(i + 1));
        return;
    }
    d_->salida << ruta;
    d_->pagina++;
    emit avance(tr("Armando las láminas"), d_->pagina, total);

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
