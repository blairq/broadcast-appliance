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
                           : tr("%1 · programa del %2").arg(QStringLiteral(BA_APP_VERSION), built);
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

nlohmann::json SystemViewModel::info() const {
    nlohmann::json creds = nlohmann::json::array();
    for (const QString& n : credentials()) creds.push_back(n.toStdString());
    nlohmann::json respaldos = nlohmann::json::array();
    for (const QVariant& v : backups()) {
        const QVariantMap m = v.toMap();
        respaldos.push_back({{"name", m.value("name").toString().toStdString()}, {"path", m.value("path").toString().toStdString()}});
    }
    return {{"dataRoot", dataRoot().toStdString()},
            {"programDate", version().toStdString()},
            {"credentials", creds},
            {"backups", respaldos},
            {"canRollback", canRollback()}};
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

QVariantList SystemViewModel::preparacion() const {
    QVariantList out = inicioEstado().value("preparacion").toList();
    if (equipo_) out += equipo_(); // lo del equipo, después de lo del arranque (F4-07)
    return out;
}

QString SystemViewModel::inicioPoner(bool activo) {
    const QString err = QString::fromStdString(inicio::poner(activo));
    emit changed(); // la preparación tiene «arranca con la sesión»
    return err;
}

void SystemViewModel::refresh() { emit changed(); }

QString SystemViewModel::checkUpdate(const QString& folder) const {
    const QString src = QDir::cleanPath(localPath(folder));
    if (src.isEmpty()) return tr("Elegi la carpeta de la version nueva.");
    if (!QFileInfo::exists(src + "/" + kLauncher) || !QFileInfo::exists(src + "/" + kApp))
        return tr("Esa carpeta no es una version de Broadcast Appliance (tiene que tener %1 y %2).")
            .arg(kLauncher, kApp);
    const QString r = QDir::cleanPath(QFileInfo(programa()).canonicalFilePath());
    const QString s = QFileInfo(src).canonicalFilePath();
    if (s == r || s.startsWith(r + "/"))
        return tr("La version nueva tiene que estar fuera de la carpeta del sistema.");
    return {};
}

QString SystemViewModel::launch(const QStringList& args, const QString& aviso) {
    const QString exe = launcherPath();
    if (!QFileInfo::exists(exe)) return tr("No encuentro el launcher (%1).").arg(exe);
    qint64 pid = 0;
    if (!QProcess::startDetached(exe, args, programa(), &pid)) return tr("No se pudo lanzar el actualizador.");
    emit quitForUpdate(aviso);
    return {};
}

QString SystemViewModel::startUpdate(const QString& folder) {
    const QString err = checkUpdate(folder);
    if (!err.isEmpty()) return err;
    return launch({"--update", QDir::cleanPath(localPath(folder)), "--start"});
}

QString SystemViewModel::startRollback() {
    if (!canRollback()) return tr("No hay una version anterior guardada.");
    return launch({"--update-rollback", "--start"});
}

void SystemViewModel::openFolder(const QString& sub) const {
    QDesktopServices::openUrl(QUrl::fromLocalFile(sub.isEmpty() ? root() : root() + "/" + sub));
}

} // namespace ba::app
