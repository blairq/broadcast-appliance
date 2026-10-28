// src/app/net/CapturaWeb.cpp — ver el .hpp

#include "net/CapturaWeb.hpp"

#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#endif

#include <algorithm>
#include <memory>

namespace ba::app {

CapturaWeb::CapturaWeb(QObject* parent) : QObject(parent) {}

QString CapturaWeb::ejecutable() {
#if defined(Q_OS_WIN)
    const QString nombre = "chrome-headless-shell.exe";
#else
    const QString nombre = "chrome-headless-shell";
#endif
    // BA_CHROME_HEADLESS: otro (las pruebas usan el de extern/, el build no lo copia al lado).
    const QString otro = qEnvironmentVariable("BA_CHROME_HEADLESS");
    const QString ruta = !otro.isEmpty() ? otro : QDir(QCoreApplication::applicationDirPath()).filePath("chrome-headless/" + nombre);
    return QFileInfo(ruta).isExecutable() ? ruta : QString();
}

QString CapturaWeb::disponible() {
    if (ejecutable().isEmpty())
        return QString::fromStdString(ba::i18n::traducir("captura-web", "Falta chrome-headless-shell en la instalación (bin/64bit/chrome-headless): este build no lo trajo."));
    return {};
}

void CapturaWeb::capturar(const Pedido& p, Listo listo) {
    if (const QString e = disponible(); !e.isEmpty()) return listo({}, e);
    auto tmp = std::make_shared<QTemporaryDir>();
    if (!tmp->isValid()) return listo({}, QString::fromStdString(ba::i18n::traducir("captura-web", "No se pudo crear una carpeta temporal para la foto.")));
    const QString png = tmp->filePath("captura.png");
    // El perfil, en los datos: el aparato es portátil, nada en la carpeta del usuario.
    const QString perfil = QString::fromStdU16String((ba::platform::dataRoot() / "Cache" / "chrome-headless").u16string());
    QDir().mkpath(perfil);
    const int ancho = std::clamp(p.ancho, 320, 3840), alto = std::clamp(p.alto, 320, 4000);
    const QString idioma = !p.idioma.isEmpty() ? p.idioma : QString::fromStdString(ba::i18n::actual().codigo);
    QStringList args{"--headless",
                           "--disable-gpu",
                           "--hide-scrollbars",
                           "--no-first-run",
                           "--no-default-browser-check",
                           "--disable-extensions",
                           "--mute-audio",
                           // Las páginas del aparato van por HTTPS con su propio certificado.
                           "--ignore-certificate-errors",
                           "--user-data-dir=" + perfil,
                           // Las páginas eligen su idioma por el del navegador: el del manual, no el de Chrome.
                           "--lang=" + idioma,
                           "--accept-lang=" + idioma,
                           QString("--window-size=%1,%2").arg(ancho).arg(alto),
                           QString("--virtual-time-budget=%1").arg(std::clamp(p.esperaMs, 0, 20000)),
                           "--screenshot=" + png};
    auto* proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#if defined(Q_OS_LINUX)
    // Como root (un contenedor, las pruebas en Docker) Chrome no arranca con su caja de arena:
    // sin ella solo en ese caso. Una máquina de verdad no corre el programa como root.
    if (::geteuid() == 0) args << "--no-sandbox";
    // Las bibliotecas que no están en la base viajan al lado (tools/deps/chrome-headless.sh).
    const QString lib = QFileInfo(ejecutable()).absolutePath() + "/lib";
    env.insert("LD_LIBRARY_PATH", lib + (env.contains("LD_LIBRARY_PATH") ? ":" + env.value("LD_LIBRARY_PATH") : QString()));
#endif
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    auto hecho = std::make_shared<bool>(false);
    const auto terminar = [proc, tmp, png, listo, hecho](const QString& error) {
        if (*hecho) return;
        *hecho = true;
        QImage img;
        if (error.isEmpty() && !img.load(png)) {
            const QString salida = QString::fromUtf8(proc->readAll()).trimmed().right(300);
            listo({}, QString::fromStdString(ba::i18n::traducir("captura-web", "Chrome no sacó la foto.")) + (salida.isEmpty() ? QString() : " " + salida));
        } else {
            listo(error.isEmpty() ? img : QImage(), error);
        }
        proc->deleteLater();
    };
    connect(proc, &QProcess::finished, this, [terminar](int, QProcess::ExitStatus) { terminar({}); });
    connect(proc, &QProcess::errorOccurred, this, [terminar, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) terminar(QString::fromStdString(ba::i18n::traducir("captura-web", "No se pudo arrancar chrome-headless-shell: ")) + proc->errorString());
    });
    // Una página que no termina nunca no traba nada: a los 40 s se corta.
    QPointer<QProcess> vigilado(proc);
    QTimer::singleShot(40000, this, [vigilado, terminar]() {
        if (!vigilado || vigilado->state() == QProcess::NotRunning) return;
        vigilado->kill();
        terminar(QString::fromStdString(ba::i18n::traducir("captura-web", "La página tardó demasiado (40 s) y se cortó.")));
    });
    args << p.url; // la dirección, al final (después de las opciones)
    proc->start(ejecutable(), args);
}

} // namespace ba::app
