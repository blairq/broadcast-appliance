#include "carrusel/Conversion.hpp"
#include "ajustes/Ajustes.hpp"

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

#ifdef Q_OS_UNIX
#include <csignal>
#endif

namespace ba::carrusel {
namespace {

/// Lo que LibreOffice abre y nosotros queremos. `.key` de Keynote NO está: lo
/// abre mal y media lámina sale en blanco, que es peor que decir que no.
const QStringList kExtensiones = {"pdf", "pptx", "ppt", "odp", "odg", "fodp"};

QString extension(const QString& archivo) { return QFileInfo(archivo).suffix().toLower(); }

/// Convertir tarda, pero no una hora: si LibreOffice no terminó en cinco
/// minutos es que se quedó esperando algo que nunca va a llegar (un diálogo
/// que en headless nadie puede contestar) y lo mejor es decirlo.
inline int kLimiteOfficeMs() { return ba::ajustes::entero("carrusel.limiteOfficeMin", 5) * 60 * 1000; }

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
    // En macOS es una .app. SIEMPRE la que viaja con nosotros: nunca la de
    // /Applications, que en otra Mac puede no estar (el aparato es portable).
    for (const QString& base : {dir + "/libreoffice", dir + "/../libreoffice"})
        velas << base + "/program/soffice" << base + "/program/soffice.exe"
              << base + "/LibreOffice.app/Contents/MacOS/soffice";
#ifdef BA_LIBREOFFICE_DIR
    // El árbol de extern/, para correr desde el directorio de compilación.
    velas << QString(BA_LIBREOFFICE_DIR) + "/program/soffice"
          << QString(BA_LIBREOFFICE_DIR) + "/program/soffice.exe"
          << QString(BA_LIBREOFFICE_DIR) + "/LibreOffice.app/Contents/MacOS/soffice";
#endif
    for (const QString& c : velas)
        if (QFileInfo(c).isExecutable()) return QFileInfo(c).absoluteFilePath();
    return {};
}

void Conversor::prepararOffice(QProcess& p, const QString& bin, const QString& archivo, const QString& salida,
                               const QString& perfil) {
#ifdef Q_OS_UNIX
    // EL HIJO ARRANCA CON LAS SEÑALES LIMPIAS. LibreOffice se abre en dos
    // procesos (soffice lanza soffice.bin) y espera a su propio hijo; nosotros
    // corremos adentro de una app que tiene SIGCHLD y compañía tocados —libobs
    // y Qt los manejan a su manera— y eso el hijo lo HEREDA. Con la máscara
    // heredada, LibreOffice moría con `RuntimeException` de UNO y no escribía
    // nada: desde una terminal, con el mismo comando y las mismas variables,
    // convertía perfecto. Eso fue lo que lo delató.
    p.setChildProcessModifier([] {
        sigset_t ninguna;
        sigemptyset(&ninguna);
        sigprocmask(SIG_SETMASK, &ninguna, nullptr);
        struct sigaction porDefecto {};
        porDefecto.sa_handler = SIG_DFL;
        sigemptyset(&porDefecto.sa_mask);
        for (int s : {SIGCHLD, SIGPIPE, SIGTERM, SIGINT, SIGHUP, SIGUSR1, SIGUSR2})
            sigaction(s, &porDefecto, nullptr);
    });
#endif
    // El perfil va al temporal: sin esto, dos conversiones a la vez chocan por
    // el mismo ~/.config y la segunda muere sin decir por qué.
    p.setArguments(
        {"--headless", "--norestore", "--invisible",
         // CODIFICADA. `toString()` deja los espacios tal cual y la
         // carpeta temporal lleva el nombre de la app: «Broadcast
         // Appliance», con espacio. LibreOffice recibia
         // `file:///tmp/Broadcast Appliance-xxxx/perfil`, no podia armar
         // el perfil y se moria con un `RuntimeException` de UNO sin decir
         // una palabra. Desde una terminal andaba, y el test tambien,
         // porque ahi el nombre del programa no tiene espacios.
         "-env:UserInstallation=" + QUrl::fromLocalFile(perfil).toString(QUrl::FullyEncoded),
         "--convert-to", "pdf", "--outdir", salida, archivo});
    p.setProgram(bin);

    // ENTORNO LIMPIO, Y A PROPOSITO. El hijo heredaba el de la app entera
    // —variables de libobs, de Qt, de cachés redirigidos al bundle— y eso
    // alcanzaba para que LibreOffice muriera con `RuntimeException` de UNO sin
    // escribir nada. Desde una terminal, mismo comando, convertía perfecto.
    // Acá se le pasa lo mínimo que necesita cualquier programa y nada más:
    // convertir un archivo no tiene por qué depender de cómo arrancó el
    // aparato.
    QProcessEnvironment limpio;
    const QProcessEnvironment heredado = QProcessEnvironment::systemEnvironment();
    for (const QString& k : {QStringLiteral("PATH"), QStringLiteral("HOME"), QStringLiteral("USER"),
                             QStringLiteral("LOGNAME"), QStringLiteral("TMPDIR"), QStringLiteral("LANG")})
        if (heredado.contains(k)) limpio.insert(k, heredado.value(k));
    if (!limpio.contains("HOME")) limpio.insert("HOME", perfil);
    // Sin pantalla: es headless y pedirle una X que puede no estar es una
    // fuente de fallas que no necesitamos.
    limpio.insert("SAL_USE_VCLPLUGIN", "svp");
    p.setProcessEnvironment(limpio);
    // Todo por el mismo caño: LibreOffice reparte sus quejas entre salida y
    // error sin criterio claro, y perder la mitad al diagnosticar es peor que
    // mezclarlas.
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
        emit fallo("ya hay una conversión en curso");
        return;
    }
    if (!QFileInfo::exists(archivo)) {
        emit fallo("no existe: " + archivo);
        return;
    }
    if (!esDocumento(archivo)) {
        emit fallo("no sé desarmar " + extension(archivo));
        return;
    }
    if (!QDir().mkpath(destino)) {
        emit fallo("no puedo escribir en " + destino);
        return;
    }

    d_->cancelado = false;
    d_->corriendo = true;
    d_->destino = destino;
    d_->salida.clear();
    d_->ancho = anchoPx > 0 ? anchoPx : 1920;
    d_->pagina = 0;

    if (!necesitaOffice(archivo)) {
        arrancarPaginas(archivo);
        return;
    }

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
    prepararOffice(*d_->proc, bin, archivo, tmp, tmp + "/perfil");

    const QString esperado = tmp + "/" + QFileInfo(archivo).completeBaseName() + ".pdf";
    connect(d_->proc, &QProcess::finished, this, [this, esperado](int, QProcess::ExitStatus) {
        // Sin proceso ya se aviso (se corto por tiempo): leer su salida seria
        // usar un puntero nulo.
        if (d_->cancelado || !d_->proc) return;
        // El código de salida miente: LibreOffice contesta 0 aunque no haya
        // escrito nada. Lo que vale es si el PDF está.
        if (!QFileInfo::exists(esperado)) {
            // Lo que haya dicho LibreOffice va en el motivo. Sin esto el aviso
            // es «no pudo» a secas y no hay por donde empezar a mirar: pasó, y
            // averiguarlo costó una tarde.
            QString dijo = QString::fromLocal8Bit(d_->proc->readAllStandardError()).trimmed();
            if (dijo.isEmpty()) dijo = QString::fromLocal8Bit(d_->proc->readAllStandardOutput()).trimmed();
            d_->limpiar();
            emit fallo(dijo.isEmpty() ? QString("LibreOffice no pudo abrir el archivo (¿está dañado?)")
                                      : "LibreOffice: " + dijo.left(600));
            return;
        }
        arrancarPaginas(esperado);
    });
    connect(d_->proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        // Si se cayo, despues llega finished, que avisa con lo que dijo
        // LibreOffice. Limpiar aca borraba el proceso que finished iba a leer
        // (se vio con Wine, pero un LibreOffice que se cae tumbaba la app igual).
        if (d_->cancelado || e == QProcess::Crashed) return;
        d_->limpiar();
        emit fallo("no se pudo arrancar LibreOffice");
    });
    QTimer::singleShot(kLimiteOfficeMs(), this, [this]() {
        if (d_->corriendo && d_->proc) {
            d_->limpiar();
            emit fallo("LibreOffice tardó demasiado y lo corté");
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
