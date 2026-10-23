// ============================================================================
// src/app/ui/SystemViewModel.cpp — ver SystemViewModel.hpp
// ============================================================================

#include "ui/SystemViewModel.hpp"
#include "arranque/InicioConElSistema.hpp"
#include "video/ElementosGst.hpp"

#include <QJsonArray>

#include <QJsonDocument>
#include <QJsonObject>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#include <filesystem>

#include "platform/Paths.hpp"
#include "security/CredentialStore.hpp"

namespace ba::app {

namespace {

#ifdef _WIN32
const char* kLauncher = "BroadcastAppliance.exe";
const char* kApp = "bin/64bit/ba-app.exe";
#else
const char* kLauncher = "BroadcastAppliance";
const char* kApp = "bin/64bit/ba-app";
#endif

QString root() { return QString::fromStdWString(ba::platform::dataRoot().wstring()); }
/// El programa (bin/, el launcher, la actualización anterior). En Windows y
/// Linux es la misma carpeta que los datos; en macOS no (los datos van al lado
/// de la .app).
QString programa() { return QString::fromStdWString(ba::platform::bundleRoot().wstring()); }
/// El launcher: en la raíz del programa, o en Contents/MacOS de la .app.
QString launcherPath() {
#ifdef __APPLE__
    const QString mac = programa() + "/../MacOS/" + kLauncher;
    if (QFileInfo::exists(mac)) return QDir::cleanPath(mac);
#endif
    return programa() + "/" + kLauncher;
}

} // namespace

QString localPath(const QString& folderOrUrl) {
    const QUrl u(folderOrUrl);
    return u.isLocalFile() ? u.toLocalFile() : folderOrUrl;
}

SystemViewModel::SystemViewModel(QObject* parent) : QObject(parent) {}

QString SystemViewModel::dataRoot() const { return QDir::toNativeSeparators(root()); }

QString SystemViewModel::version() const {
    const QFileInfo fi(programa() + "/" + kApp);
    const QString built = fi.exists() ? fi.lastModified().toString("yyyy-MM-dd HH:mm") : QString();
#ifdef BA_APP_VERSION
    return built.isEmpty() ? QStringLiteral(BA_APP_VERSION)
                           : QStringLiteral(BA_APP_VERSION " · programa del ") + built;
#else
    return built;
#endif
}

QStringList SystemViewModel::credentials() const {
    QStringList out;
    for (const auto& n : ba::security::CredentialStore::bundle().list())
        out << QString::fromStdString(n);
    return out;
}

QVariantList SystemViewModel::backups() const {
    QVariantList out;
    QDir d(root() + "/Respaldos"); // el launcher respalda en la carpeta de datos
    const auto list = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo& fi : list)
        out << QVariantMap{{"name", fi.fileName()}, {"path", fi.absoluteFilePath()}};
    return out;
}

bool SystemViewModel::canRollback() const {
    return QFileInfo(programa() + "/.actualizacion-anterior").isDir();
}

QVariantMap SystemViewModel::inicioEstado() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(inicio::estado().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QVariantList SystemViewModel::elementosQueFaltan() const {
    nlohmann::json faltan = nlohmann::json::array();
    for (const auto& e : video::revisarElementosGst())
        if (!e.value("esta", true)) faltan.push_back(e);
    return QJsonDocument::fromJson(QByteArray::fromStdString(faltan.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

QString SystemViewModel::inicioPoner(bool activo) { return QString::fromStdString(inicio::poner(activo)); }

void SystemViewModel::refresh() { emit changed(); }

QString SystemViewModel::checkUpdate(const QString& folder) const {
    const QString src = QDir::cleanPath(localPath(folder));
    if (src.isEmpty()) return "Elegi la carpeta de la version nueva.";
    if (!QFileInfo::exists(src + "/" + kLauncher) || !QFileInfo::exists(src + "/" + kApp))
        return QString("Esa carpeta no es una version de Broadcast Appliance (tiene que tener %1 y %2).")
            .arg(kLauncher, kApp);
    const QString r = QDir::cleanPath(QFileInfo(programa()).canonicalFilePath());
    const QString s = QFileInfo(src).canonicalFilePath();
    if (s == r || s.startsWith(r + "/"))
        return "La version nueva tiene que estar fuera de la carpeta del sistema.";
    return {};
}

QString SystemViewModel::launch(const QStringList& args, const QString& aviso) {
    const QString exe = launcherPath();
    if (!QFileInfo::exists(exe)) return "No encuentro el launcher (" + exe + ").";
    qint64 pid = 0;
    if (!QProcess::startDetached(exe, args, programa(), &pid)) return "No se pudo lanzar el actualizador.";
    emit quitForUpdate(aviso);
    return {};
}

QString SystemViewModel::startUpdate(const QString& folder) {
    const QString err = checkUpdate(folder);
    if (!err.isEmpty()) return err;
    return launch({"--update", QDir::cleanPath(localPath(folder)), "--start"});
}

QString SystemViewModel::startRollback() {
    if (!canRollback()) return "No hay una version anterior guardada.";
    return launch({"--update-rollback", "--start"});
}

void SystemViewModel::openFolder(const QString& sub) const {
    QDesktopServices::openUrl(QUrl::fromLocalFile(sub.isEmpty() ? root() : root() + "/" + sub));
}

} // namespace ba::app
