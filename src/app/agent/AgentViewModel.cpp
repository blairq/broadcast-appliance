// ============================================================================
// src/app/agent/AgentViewModel.cpp — ver AgentViewModel.hpp
// ============================================================================

#include "agent/AgentViewModel.hpp"

#if defined(_WIN32)
#include <intrin.h>
#elif defined(__x86_64__)
#include <cpuid.h>
#endif

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcess>
#include <QSysInfo>
#include <QVersionNumber>
#include <QTimer>
#include <QUrl>

#include <fstream>

#include "agent/AlertEngine.hpp"
#include "AppRuntime.hpp"
#include "net/Credentials.hpp"
#include "net/HttpsClient.hpp"

namespace ba::app::agent {

using json = nlohmann::json;

namespace {

/// AVX2 disponible (CPU y sistema operativo). Los binarios de Bun (opencode)
/// comunes lo exigen; sin AVX2 se usa su variante "baseline".
bool cpuHasAvx2() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned a = 0, b = 0, c = 0, d = 0;
#ifdef _WIN32
    int r[4];
    __cpuid(r, 1);
    c = static_cast<unsigned>(r[2]);
    const bool osxsave = (c & (1u << 27)) != 0 && (c & (1u << 28)) != 0;
    if (!osxsave || (_xgetbv(0) & 0x6) != 0x6) return false;
    __cpuidex(r, 7, 0);
    b = static_cast<unsigned>(r[1]);
    (void)a;
    (void)d;
#else
    if (__get_cpuid(1, &a, &b, &c, &d) == 0) return false;
    if ((c & (1u << 27)) == 0 || (c & (1u << 28)) == 0) return false;
    unsigned lo = 0, hi = 0;
    __asm__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    if ((lo & 0x6) != 0x6) return false;
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d) == 0) return false;
#endif
    return (b & (1u << 5)) != 0;
#else
    return true; // ARM: no aplica
#endif
}

struct Provider {
    const char* id;
    const char* runtime; ///< claude | gemini | opencode
    const char* name;
    const char* auth;    ///< cuenta | api
    const char* keyName; ///< credencial (vacio si no usa clave)
    const char* envKey;  ///< variable de entorno de la clave
    const char* keyUrl;  ///< donde se saca la clave / la cuenta
    const char* help;
    QStringList models;
};

const std::vector<Provider>& providerTable() {
    static const std::vector<Provider> t = {
        {"claude-cuenta",
         "claude",
         "Claude — suscripción Pro/Max",
         "cuenta",
         "",
         "",
         "https://claude.ai/upgrade",
         "Sin clave de API: la primera vez escribí /login en la terminal y elegí tu cuenta de Claude (se "
         "abre el "
         "navegador). La sesión queda guardada en la carpeta portable.",
         {"opus", "sonnet", "haiku"}},
        {"claude-api",
         "claude",
         "Claude — clave de API",
         "api",
         "anthropic-api-key",
         "ANTHROPIC_API_KEY",
         "https://console.anthropic.com/settings/keys",
         "Pegá una clave de API de Anthropic (se paga por uso). La primera vez Claude Code pregunta si "
         "usarla: "
         "respondé que sí.",
         {"opus", "sonnet", "haiku"}},
        {"gemini-cuenta",
         "gemini",
         "Gemini — cuenta de Google",
         "cuenta",
         "",
         "",
         "https://gemini.google.com/",
         "Sin clave de API: al arrancar elegí «Sign in with Google» y la cuenta (se abre el navegador). Con "
         "Gemini "
         "Pro/Ultra (o el plan gratis) según tu cuenta.",
         {"gemini-2.5-pro", "gemini-2.5-flash"}},
        {"gemini-api",
         "gemini",
         "Gemini — clave de API",
         "api",
         "gemini-api-key",
         "GEMINI_API_KEY",
         "https://aistudio.google.com/apikey",
         "Pegá una clave de Google AI Studio.",
         {"gemini-2.5-pro", "gemini-2.5-flash"}},
        {"opencode-go",
         "opencode",
         "OpenCode Go",
         "cuenta",
         "",
         "",
         "https://opencode.ai/",
         "La primera vez escribí /connect, elegí «OpenCode Go» y pegá la clave de tu suscripción; después "
         "/models "
         "para elegir el modelo. Queda guardado en la carpeta portable.",
         {}},
        {"openrouter",
         "opencode",
         "OpenRouter",
         "api",
         "openrouter-api-key",
         "OPENROUTER_API_KEY",
         "https://openrouter.ai/settings/keys",
         "Pegá una clave de OpenRouter y elegí el modelo (formato openrouter/<proveedor>/<modelo>).",
         {"openrouter/anthropic/claude-sonnet-4.5", "openrouter/google/gemini-2.5-pro",
          "openrouter/openai/gpt-5"}},
    };
    return t;
}

const Provider* findProvider(const QString& id) {
    for (const auto& p : providerTable())
        if (id == p.id) return &p;
    return nullptr;
}

QString sha256File(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

bool writeText(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(data);
    return true;
}

QString findFile(const QString& root, const QString& name) {
    QDirIterator it(root, {name}, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

} // namespace

AgentViewModel::AgentViewModel(AppRuntime& rt, AlertEngine& alerts, QObject* parent)
    : QObject(parent), rt_(rt), alerts_(alerts), session_(std::make_unique<term::TerminalSession>()) {
    loadConfig();
    connect(session_.get(), &term::TerminalSession::runningChanged, this, &AgentViewModel::runningChanged);
    connect(session_.get(), &term::TerminalSession::finished, this, [this](int code) {
        const bool expected = expectedStop_;
        expectedStop_ = false;
        if (!expected && code != 0) setError(QString("El agente termino (codigo %1).").arg(code));
    });
    connect(&alerts_, &AlertEngine::fired, this, &AgentViewModel::onAlert);
    // Arranque automatico cuando todo este listo (GuiMode conecta el MCP despues).
    QTimer::singleShot(2500, this, [this]() {
        if (autoStart_ && installed() && mcpReady() && !running()) start();
    });
}

AgentViewModel::~AgentViewModel() {
    cancelInstall_ = true;
    if (installThread_.joinable()) installThread_.join();
    session_->stop();
}

// --- Configuracion -------------------------------------------------------------------

void AgentViewModel::loadConfig() {
    std::ifstream in(rt_.dataRoot() / "Config" / "asistente.json");
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    const QString p = QString::fromStdString(j.value("provider", std::string("claude-cuenta")));
    if (findProvider(p) != nullptr) provider_ = p;
    const json models = j.value("models", json::object());
    model_ = QString::fromStdString(models.value(provider_.toStdString(), std::string()));
    alertsToChat_ = j.value("alertsToChat", true);
    autoStart_ = j.value("autoStart", false);
    fontSize_ = j.value("fontSize", 15);
}

void AgentViewModel::saveConfig() const {
    const auto path = rt_.dataRoot() / "Config" / "asistente.json";
    std::ifstream in(path);
    json j = json::parse(in, nullptr, false);
    if (!j.is_object()) j = json::object();
    in.close();
    j["provider"] = provider_.toStdString();
    j["models"][provider_.toStdString()] = model_.toStdString();
    j["alertsToChat"] = alertsToChat_;
    j["autoStart"] = autoStart_;
    j["fontSize"] = fontSize_;
    std::ofstream out(path, std::ios::trunc);
    out << j.dump(2);
}

QVariantList AgentViewModel::providers() const {
    QVariantList out;
    for (const auto& p : providerTable())
        out << QVariantMap{{"id", p.id},
                           {"runtime", p.runtime},
                           {"name", p.name},
                           {"auth", p.auth},
                           {"needsKey", QString(p.keyName) != ""},
                           {"keyUrl", p.keyUrl},
                           {"help", p.help},
                           {"models", p.models}};
    return out;
}

QVariantMap AgentViewModel::current() const {
    for (const QVariant& v : providers())
        if (v.toMap().value("id").toString() == provider_) {
            QVariantMap m = v.toMap();
            const json pin = pins().value(runtimeOf(provider_).toStdString(), json::object());
            m["agentName"] = QString::fromStdString(pin.value("nombre", std::string()));
            m["version"] = QString::fromStdString(pin.value("version", std::string()));
            return m;
        }
    return {};
}

void AgentViewModel::setProvider(const QString& id) {
    if (findProvider(id) == nullptr || id == provider_) return;
    const bool wasRunning = running();
    if (wasRunning) stop();
    provider_ = id;
    std::ifstream in(rt_.dataRoot() / "Config" / "asistente.json");
    const json j = json::parse(in, nullptr, false);
    model_ =
        j.is_object()
            ? QString::fromStdString(j.value("models", json::object()).value(id.toStdString(), std::string()))
            : QString();
    saveConfig();
    emit configChanged();
    emit installChanged();
}

void AgentViewModel::setModel(const QString& model) {
    if (model.trimmed() == model_) return;
    model_ = model.trimmed();
    saveConfig();
    emit configChanged();
}

bool AgentViewModel::hasKey() const {
    const Provider* p = findProvider(provider_);
    if (p == nullptr || QString(p->keyName).isEmpty()) return true;
    return cred::exists(QString::fromStdWString((rt_.dataRoot() / "Credenciales").wstring()), p->keyName);
}

QString AgentViewModel::saveKey(const QString& key) {
    const Provider* p = findProvider(provider_);
    if (p == nullptr || QString(p->keyName).isEmpty()) return "Este proveedor no usa clave.";
    const QString k = key.trimmed();
    if (k.size() < 20) return "La clave parece incompleta.";
    if (!cred::write(QString::fromStdWString((rt_.dataRoot() / "Credenciales").wstring()), p->keyName,
                     k.toUtf8()))
        return "No se pudo guardar la clave.";
    emit configChanged();
    return {};
}

void AgentViewModel::clearKey() {
    const Provider* p = findProvider(provider_);
    if (p == nullptr || QString(p->keyName).isEmpty()) return;
    cred::remove(QString::fromStdWString((rt_.dataRoot() / "Credenciales").wstring()), p->keyName);
    emit configChanged();
}

void AgentViewModel::setAlertsToChat(bool on) {
    alertsToChat_ = on;
    saveConfig();
    emit configChanged();
}

void AgentViewModel::setAutoStart(bool on) {
    autoStart_ = on;
    saveConfig();
    emit configChanged();
}

void AgentViewModel::setFontSize(int px) {
    fontSize_ = std::clamp(px, 10, 30);
    saveConfig();
    emit configChanged();
}

void AgentViewModel::setError(const QString& e) {
    lastError_ = e;
    emit errorChanged();
    if (!e.isEmpty()) emit notify(e, "warn");
}

// --- Instalacion ---------------------------------------------------------------------

json AgentViewModel::pins() const {
    QFile f(":/agent/agentes.json");
    if (!f.open(QIODevice::ReadOnly)) return json::object();
    return json::parse(f.readAll().toStdString(), nullptr, false);
}

QString AgentViewModel::runtimeOf(const QString& provider) const {
    const Provider* p = findProvider(provider);
    return p != nullptr ? QString(p->runtime) : QString();
}

QString AgentViewModel::platformKey() const {
    const QString arch = QSysInfo::currentCpuArchitecture();
    const QString a = (arch == "arm64" || arch == "aarch64") ? "arm64" : "x64";
#if defined(_WIN32)
    return "win-" + a;
#elif defined(__APPLE__)
    return "mac-" + a;
#else
    return "linux-" + a;
#endif
}

nlohmann::json AgentViewModel::platformPin(const json& pin) const {
    const json plats = pin.value("plataformas", json::object());
    const std::string key = platformKey().toStdString();
    if (!cpuHasAvx2() && plats.contains(key + "-baseline")) return plats[key + "-baseline"];
    return plats.value(key, json::object());
}

QString AgentViewModel::installDir(const QString& runtime) const {
    const json pin = pins().value(runtime.toStdString(), json::object());
    return QString::fromStdWString((rt_.dataRoot() / "Agentes").wstring()) + "/" + runtime + "/" +
           QString::fromStdString(pin.value("version", std::string("?")));
}

QString AgentViewModel::usableDir(const QString& runtime, QString* exeOut) const {
    if (runtime.isEmpty()) return {};
    const json pin = pins().value(runtime.toStdString(), json::object());
    const QString base =
        QString::fromStdWString((rt_.dataRoot() / "Agentes").wstring()) + "/" + runtime;
    const QString fijada = QString::fromStdString(pin.value("version", std::string()));

    // El `.ok` guarda {version, exe} desde esta version; los viejos tenian solo
    // una fecha y para esos se cae al `exe` del pin, que es lo que habia.
    const auto leerOk = [&](const QString& dir, QString* exe) {
        QFile f(dir + "/.ok");
        if (!f.open(QIODevice::ReadOnly)) return false;
        const json j = json::parse(f.readAll().toStdString(), nullptr, false);
        if (exe != nullptr && j.is_object())
            *exe = QString::fromStdString(j.value("exe", std::string()));
        return true;
    };

    QString exe;
    if (!fijada.isEmpty() && leerOk(base + "/" + fijada, &exe)) {
        if (exeOut != nullptr)
            *exeOut = exe.isEmpty() ? QString::fromStdString(platformPin(pin).value("exe", "")) : exe;
        return base + "/" + fijada;
    }
    // Quedo una instalacion de otra version (p. ej. se actualizo el sistema):
    // sirve igual. Se usa la mas nueva por orden de version.
    QStringList versiones = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(versiones.begin(), versiones.end(), [](const QString& a, const QString& b) {
        return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
    });
    for (auto it = versiones.crbegin(); it != versiones.crend(); ++it) {
        QString e;
        if (leerOk(base + "/" + *it, &e)) {
            if (exeOut != nullptr)
                *exeOut = e.isEmpty() ? QString::fromStdString(platformPin(pin).value("exe", "")) : e;
            return base + "/" + *it;
        }
    }
    return {};
}

bool AgentViewModel::installed() const { return !usableDir(runtimeOf(provider_)).isEmpty(); }

QString AgentViewModel::installedVersion() const {
    const QString d = usableDir(runtimeOf(provider_));
    return d.isEmpty() ? QString() : QFileInfo(d).fileName();
}

bool AgentViewModel::updateAvailable() const {
    const QString v = installedVersion();
    if (v.isEmpty()) return false;
    const json pin = pins().value(runtimeOf(provider_).toStdString(), json::object());
    return v != QString::fromStdString(pin.value("version", std::string()));
}

void AgentViewModel::install() {
    if (installing_) return;
    const QString runtime = runtimeOf(provider_);
    const json pin = pins().value(runtime.toStdString(), json::object());
    const json plat = platformPin(pin);
    if (plat.empty()) {
        setError("No hay una version de " + runtime + " para esta plataforma (" + platformKey() + ").");
        return;
    }
    struct Item {
        std::string url, sha, kind, dest;
    };
    std::vector<Item> items;
    const QString dir = installDir(runtime);
    if (runtime == "gemini") {
        items.push_back(
            {plat.value("url", ""), plat.value("sha256", ""), "archivo", (dir + "/node").toStdString()});
        const json pkg = pin.value("paquete", json::object());
        items.push_back(
            {pkg.value("url", ""), pkg.value("sha256", ""), "archivo", (dir + "/gemini").toStdString()});
    } else {
        items.push_back({plat.value("url", ""), plat.value("sha256", ""), plat.value("tipo", "archivo"),
                         plat.value("tipo", "") == "binario"
                             ? (dir + "/" + QString::fromStdString(plat.value("exe", ""))).toStdString()
                             : dir.toStdString()});
    }
    installing_ = true;
    installProgress_ = 0;
    installText_ = "Descargando " + QString::fromStdString(pin.value("nombre", "")) + " " +
                   QString::fromStdString(pin.value("version", "")) + "…";
    cancelInstall_ = false;
    emit installChanged();
    if (installThread_.joinable()) installThread_.join();
    const QString exeName = QString::fromStdString(plat.value("exe", ""));
    // Se baja y se descomprime en una carpeta aparte y recien al final se pone
    // en su lugar: si la descarga falla (sin red, hash que no coincide, se
    // cancela), la instalacion que YA andaba queda intacta. Antes el camino de
    // error borraba `dir`, que podia ser una instalacion buena.
    const QString staging = QFileInfo(dir).absolutePath() + "/.instalando-" + QFileInfo(dir).fileName();
    installThread_ = std::thread([this, items, dir, staging, runtime, exeName]() {
        QString err;
        QDir(staging).removeRecursively(); // restos de un intento anterior
        QDir().mkpath(staging);
        const int n = static_cast<int>(items.size());
        for (int i = 0; i < n && err.isEmpty(); ++i) {
            const Item& it = items[static_cast<std::size_t>(i)];
            const QString tmp = staging + QString("/.descarga-%1").arg(i);
            // Los destinos se armaron sobre `dir`: van a la carpeta de staging.
            const auto enStaging = [&dir, &staging](const std::string& p) {
                QString d = QString::fromStdString(p);
                if (d.startsWith(dir)) d.replace(0, dir.size(), staging);
                return d;
            };
            net::HttpRequest req;
            req.url = it.url;
            req.responseFile = tmp.toStdString();
            req.timeoutMs = 60000;
            req.cancel = &cancelInstall_;
            req.downloadProgress = [this, i, n](std::uint64_t got, std::uint64_t total) {
                const double frac = total > 0 ? static_cast<double>(got) / static_cast<double>(total) : 0.0;
                const double overall = (i + frac * 0.95) / n;
                const QString mb = QString::number(static_cast<double>(got) / 1048576.0, 'f', 0);
                const QString tot =
                    total > 0 ? " / " + QString::number(static_cast<double>(total) / 1048576.0, 'f', 0)
                              : QString();
                QMetaObject::invokeMethod(this, [this, overall, mb, tot]() {
                    installProgress_ = overall;
                    installText_ = "Descargando… " + mb + tot + " MB";
                    emit installChanged();
                });
            };
            const net::HttpResponse res = net::HttpsClient::download(req);
            if (!res.error.empty()) {
                err = cancelInstall_ ? "Instalacion cancelada."
                                     : "No se pudo descargar: " + QString::fromStdString(res.error);
                break;
            }
            QMetaObject::invokeMethod(this, [this]() {
                installText_ = "Verificando (SHA-256)…";
                emit installChanged();
            });
            if (sha256File(tmp) != QString::fromStdString(it.sha)) {
                err = "La descarga no coincide con la version fijada (SHA-256). No se instalo nada.";
                QFile::remove(tmp);
                break;
            }
            if (it.kind == "binario") {
                const QString dest = enStaging(it.dest);
                QFile::remove(dest);
                if (!QFile::rename(tmp, dest)) err = "No se pudo guardar " + dest;
                QFile(dest).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                           QFile::ReadGroup | QFile::ExeGroup);
            } else {
                QMetaObject::invokeMethod(this, [this]() {
                    installText_ = "Descomprimiendo…";
                    emit installChanged();
                });
                const QString dest = enStaging(it.dest);
                QDir().mkpath(dest);
                QProcess tar;
#ifdef _WIN32
                // El bsdtar de Windows (10 1803+ / 11) abre .zip y .tgz. NUNCA el
                // "tar" del PATH: el GNU tar de Git para Windows no abre .zip.
                tar.start(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe"),
                          {"-xf", QDir::toNativeSeparators(tmp), "-C", QDir::toNativeSeparators(dest)});
#else
                tar.start("tar", {"-xf", tmp, "-C", dest});
#endif
                if (!tar.waitForFinished(600000) || tar.exitCode() != 0)
                    err = "No se pudo descomprimir: " +
                          QString::fromLocal8Bit(tar.readAllStandardError()).trimmed();
                QFile::remove(tmp);
            }
        }
        if (err.isEmpty()) {
            // Ejecutable en su lugar (algunos archivos lo traen en una subcarpeta).
            if (runtime != "gemini" && !QFileInfo::exists(staging + "/" + exeName)) {
                const QString found = findFile(staging, exeName);
                if (!found.isEmpty()) QFile::rename(found, staging + "/" + exeName);
            }
            // El `.ok` registra que version quedo y con que ejecutable, para
            // poder seguir usandola si una version nueva del sistema fija otra.
            const json ok{{"version", QFileInfo(dir).fileName().toStdString()},
                          {"exe", exeName.toStdString()},
                          {"fecha", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()}};
            writeText(staging + "/.ok", QByteArray::fromStdString(ok.dump(2)));
            // Recien ahora se reemplaza lo que hubiera: hasta aca la instalacion
            // anterior siguio entera.
            QDir(dir).removeRecursively();
            if (!QDir().rename(staging, dir)) {
                QDir(staging).removeRecursively();
                err = "No se pudo dejar la instalacion en su lugar (" + dir + ").";
            }
        }
        if (!err.isEmpty()) {
            QDir(staging).removeRecursively(); // nunca se toca `dir`
        } else {
            // Versiones viejas del mismo agente: afuera (liberan cientos de MB).
            QDir parent(QFileInfo(dir).absolutePath());
            for (const QString& v : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (parent.absoluteFilePath(v) != QFileInfo(dir).absoluteFilePath())
                    QDir(parent.absoluteFilePath(v)).removeRecursively();
        }
        QMetaObject::invokeMethod(this, [this, err]() {
            installing_ = false;
            installProgress_ = err.isEmpty() ? 1.0 : 0.0;
            installText_ = err.isEmpty() ? "Instalado." : err;
            emit installChanged();
            if (!err.isEmpty())
                setError(err);
            else
                emit notify("Agente instalado en la carpeta portable.", "ok");
        });
    });
}

void AgentViewModel::cancelInstall() { cancelInstall_ = true; }

// --- Lanzar ---------------------------------------------------------------------------

QString AgentViewModel::bridgePath() const {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/ba-mcp.exe";
#else
    return QCoreApplication::applicationDirPath() + "/ba-mcp";
#endif
}

QString AgentViewModel::prepareWorkspace(QString* error) {
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    const QString ws = root + "/Runtime/agente";
    QDir().mkpath(ws);
    const QString prompt = systemPrompt_ ? systemPrompt_() : QString();
    const QByteArray p = prompt.toUtf8();
    for (const char* f : {"CLAUDE.md", "GEMINI.md", "AGENTS.md"})
        if (!writeText(ws + "/" + f, p)) {
            if (error != nullptr) *error = "No se pudo escribir " + ws;
            return {};
        }
    const std::string bridge = bridgePath().toStdString();
    const json mcpStdio{
        {"command", bridge}, {"args", json::array()}, {"env", {{"BA_DATA_ROOT", root.toStdString()}}}};
    const QString runtime = runtimeOf(provider_);
    const Provider* prov = findProvider(provider_);
    if (runtime == "claude") {
        json m = mcpStdio;
        m["type"] = "stdio";
        writeText(ws + "/mcp.json",
                  QByteArray::fromStdString(json{{"mcpServers", {{"broadcast", m}}}}.dump(2)));
    } else if (runtime == "opencode") {
        json cfg{{"$schema", "https://opencode.ai/config.json"},
                 {"autoupdate", false},
                 {"mcp",
                  {{"broadcast",
                    {{"type", "local"},
                     {"command", json::array({bridge})},
                     {"environment", {{"BA_DATA_ROOT", root.toStdString()}}},
                     {"enabled", true}}}}},
                 {"permission", {{"edit", "deny"}, {"bash", "deny"}}},
                 {"instructions", json::array({"AGENTS.md"})}};
        if (!model_.isEmpty()) cfg["model"] = model_.toStdString();
        writeText(ws + "/opencode.json", QByteArray::fromStdString(cfg.dump(2)));
    } else if (runtime == "gemini") {
        // Ajustes del usuario de Gemini (en su HOME portable), sin pisar lo que ya tenga.
        const QString home = root + "/Credenciales/agentes/gemini";
        const QString path = home + "/.gemini/settings.json";
        json s = json::object();
        {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) s = json::parse(f.readAll().toStdString(), nullptr, false);
            if (!s.is_object()) s = json::object();
        }
        s["mcpServers"]["broadcast"] = mcpStdio;
        s["mcpServers"]["broadcast"]["trust"] = true;
        s["security"]["auth"]["selectedType"] =
            prov != nullptr && QString(prov->auth) == "api" ? "gemini-api-key" : "oauth-personal";
        s["tools"]["exclude"] = json::array({"run_shell_command", "write_file", "replace"});
        s["general"]["disableAutoUpdate"] = true;
        writeText(path, QByteArray::fromStdString(s.dump(2)));
    }
    return ws;
}

term::Pty::Options AgentViewModel::launchOptions(QString* error) {
    term::Pty::Options o;
    const QString runtime = runtimeOf(provider_);
    const Provider* prov = findProvider(provider_);
    const json pin = pins().value(runtime.toStdString(), json::object());
    const json plat = platformPin(pin);
    // La instalacion que HAY (puede ser de otra version si se actualizo el
    // sistema), no la que fija esta version.
    QString exeInstalado;
    const QString dir = usableDir(runtime, &exeInstalado);
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    const QString profile = root + "/Credenciales/agentes/" + runtime;
    QDir().mkpath(profile);
    const QString ws = prepareWorkspace(error);
    if (ws.isEmpty()) return o;
    o.workDir = ws;
    o.env["BA_DATA_ROOT"] = root;
    o.env["TERM"] = "xterm-256color";
    o.unsetEnv = {"ANTHROPIC_API_KEY",  "ANTHROPIC_AUTH_TOKEN", "GEMINI_API_KEY", "GOOGLE_API_KEY",
                  "OPENROUTER_API_KEY", "CLAUDECODE",           "LD_LIBRARY_PATH"};
    if (prov != nullptr && !QString(prov->keyName).isEmpty()) {
        QByteArray key;
        if (!cred::read(root + "/Credenciales", prov->keyName, key)) {
            if (error != nullptr) *error = "Falta la clave de API (pegala en el panel de la derecha).";
            return {};
        }
        o.env[prov->envKey] = QString::fromUtf8(key).trimmed();
    }
    if (runtime == "claude") {
        o.program = dir + "/" + (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "claude")) : exeInstalado);
        // Solo el MCP: nada de consola ni archivos (en Windows la consola es la
        // herramienta PowerShell, que Claude usa si no hay Git Bash).
        o.args = {"--mcp-config",   ws + "/mcp.json", "--strict-mcp-config",
                  "--allowedTools", "mcp__broadcast", "--disallowedTools",
                  "Bash",           "PowerShell",     "Edit",
                  "Write",          "NotebookEdit",   "MultiEdit"};
        if (!model_.isEmpty()) o.args << "--model" << model_;
        o.env["CLAUDE_CONFIG_DIR"] = profile;
        o.env["DISABLE_AUTOUPDATER"] = "1";
    } else if (runtime == "gemini") {
        o.program = dir + "/node/" + (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "")) : exeInstalado);
        o.args = {dir + "/gemini/" +
                  QString::fromStdString(pin.value("paquete", json::object()).value("script", ""))};
        if (!model_.isEmpty()) o.args << "-m" << model_;
        o.env["HOME"] = profile;
        o.env["USERPROFILE"] = profile;
        o.env["NO_UPDATE_NOTIFIER"] = "1";
    } else if (runtime == "opencode") {
        o.program = dir + "/" + (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "opencode")) : exeInstalado);
        o.env["XDG_CONFIG_HOME"] = profile + "/config";
        o.env["XDG_DATA_HOME"] = profile + "/data";
        o.env["XDG_STATE_HOME"] = profile + "/state";
        o.env["XDG_CACHE_HOME"] = root + "/Cache/opencode";
        o.env["OPENCODE_DISABLE_AUTOUPDATE"] = "1";
    }
    return o;
}

void AgentViewModel::start() {
    setError({});
    if (!installed()) {
        setError("Primero instala el agente (boton Instalar).");
        return;
    }
    if (!mcpReady()) {
        setError(
            "El servidor MCP esta apagado: activalo en Configuracion -> Asistente (MCP) y reinicia la app.");
        return;
    }
    QString err;
    term::Pty::Options o = launchOptions(&err);
    if (!err.isEmpty() || o.program.isEmpty()) {
        setError(err.isEmpty() ? "No se pudo preparar el agente." : err);
        return;
    }
    if (!session_->start(o, &err)) setError("No se pudo iniciar el agente: " + err);
    emit runningChanged();
}

void AgentViewModel::stop() {
    if (session_->running()) expectedStop_ = true;
    session_->stop();
    emit runningChanged();
}

void AgentViewModel::restart() {
    stop();
    QTimer::singleShot(300, this, &AgentViewModel::start);
}

void AgentViewModel::send(const QString& text) {
    if (text.trimmed().isEmpty()) return;
    if (!running()) {
        setError("El agente no esta corriendo.");
        return;
    }
    session_->submit(text.trimmed());
}

// --- Alertas ------------------------------------------------------------------------------

QVariantList AgentViewModel::alerts() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(alerts_.list().dump())).array().toVariantList();
}

QVariantList AgentViewModel::events() const {
    QVariantList out = QJsonDocument::fromJson(QByteArray::fromStdString(alerts_.events(0, 30).dump()))
                           .array()
                           .toVariantList();
    std::reverse(out.begin(), out.end());
    return out;
}

void AgentViewModel::setAlertEnabled(const QString& id, bool on) {
    alerts_.update(id.toStdString(), json{{"enabled", on}});
    emit alertsChanged();
}

void AgentViewModel::deleteAlert(const QString& id) {
    alerts_.remove(id.toStdString());
    emit alertsChanged();
}

void AgentViewModel::onAlert(const json& ev) {
    emit alertsChanged();
    bool llm = false;
    for (const auto& n : ev.value("notify", json::array()))
        if (n == "llm") llm = true;
    if (!llm || !alertsToChat_ || !running()) return;
    const QString t = QDateTime::fromMSecsSinceEpoch(ev.value("atMs", std::int64_t{0})).toString("HH:mm:ss");
    // Sin texto de invitados/publico (llmText): leerlo es decision del asistente
    // con una herramienta, y eso activa la salvaguarda de su sesion.
    pendingAlerts_ << "[" + t + "] " + QString::fromStdString(ev.value("llmText", ev.value("text", ""))) +
                          " (vigilancia " + QString::fromStdString(ev.value("ruleId", "")) + ")";
    if (!flushScheduled_) {
        flushScheduled_ = true;
        // Si llegan varias juntas, van en un solo mensaje.
        QTimer::singleShot(1500, this, &AgentViewModel::flushAlerts);
    }
}

void AgentViewModel::deliverNote(const QString& text) {
    if (!running()) return;
    pendingAlerts_ << text;
    if (!flushScheduled_) {
        flushScheduled_ = true;
        QTimer::singleShot(600, this, &AgentViewModel::flushAlerts);
    }
}

void AgentViewModel::flushAlerts() {
    flushScheduled_ = false;
    if (pendingAlerts_.isEmpty() || !running()) {
        pendingAlerts_.clear();
        return;
    }
    const QString msg =
        "[AVISO AUTOMATICO de Broadcast Appliance] " + pendingAlerts_.join(" | ") +
        " — Atendelo segun lo acordado (si no hay nada acordado, revisa con las herramientas, "
        "contame que paso y propone que hacer).";
    pendingAlerts_.clear();
    session_->submit(msg);
}

void AgentViewModel::openAgentsFolder() const {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdWString((rt_.dataRoot() / "Agentes").wstring())));
}

} // namespace ba::app::agent
