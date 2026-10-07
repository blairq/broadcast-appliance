// ============================================================================
// src/app/agent/AgentViewModel.cpp — ver AgentViewModel.hpp
// ============================================================================

#include "agent/AgentViewModel.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>

#include "agent/Actualizacion.hpp"
#include "agent/ModeloPropio.hpp"

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
#include <QJsonObject>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcess>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

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

/// La carpeta de datos, para las funciones ESTATICAS: la lista de modelos del
/// modelo propio sale de un archivo, y `modelosDe` no tiene a quien
/// preguntarle. La deja el constructor; hay un solo AgentViewModel por proceso.
std::filesystem::path g_raiz;

/// El canal que corresponde a lo elegido ("propio-<id>/<modelo>"), o el
/// primero si no eligieron ninguno. nullptr si no hay canales.
const Canal* canalElegido(const ModeloPropio& mp, const QString& modelo) {
    if (mp.canales.empty()) return nullptr;
    for (const Canal& c : mp.canales)
        if (modelo == QString::fromStdString("propio-" + c.id + "/" + mp.modelo)) return &c;
    return &mp.canales.front();
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
        // El de la casa: un servidor propio que habla el protocolo de OpenAI.
        // La direccion, el modelo y los parametros extra no van en esta tabla
        // —son tuyos y se configuran— sino en Config/modelo-propio.json; ver
        // ModeloPropio.hpp. Aca solo queda dicho que existe y que usa clave.
        {"local",
         "local",
         "Modelo local (en esta máquina, sin internet)",
         "cuenta",
         "",
         "",
         "",
         "El modelo de Configuración → Modelos locales. No abre una terminal: lo que se le pide (escrito, "
         "dictado o un disparador) pasa por el mismo camino que la voz: elige la acción, completa los "
         "datos con el estado del aparato y la propone en la barra del asistente (o la hace, si la dejaste "
         "automática). Rápido para lo sencillo; para lo complejo, mejor un asistente grande.",
         {}},
        {"propio",
         "opencode",
         "Modelo propio (API compatible con OpenAI)",
         "api",
         "modelo-propio-api-key",
         "BA_MODELO_PROPIO_KEY",
         "",
         "Tu propio servidor. Configurale la direccion (baseURL), que modelo pedirle y los "
         "parametros extra que entienda, y pega su clave. Lo maneja opencode: este aparato no le "
         "habla al modelo, le arma la configuracion.",
         {}},
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
    g_raiz = rt.dataRoot(); // ver g_raiz: `modelosDe` es estatica
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
    if (checkThread_.joinable()) checkThread_.join();
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
                           // Por `modelosDe` y no por la tabla: el modelo
                           // propio no tiene una lista escrita, sus modelos
                           // son los canales que haya configurados.
                           {"models", modelosDe(p.id)}};
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
    // ELEGIR NO ES UN COMANDO. Antes, tocar este desplegable paraba el agente
    // que estaba corriendo y lo dejaba parado: aparecía el cartel de «Iniciar»
    // encima de la pantalla del agente anterior, y volver a elegir el de antes
    // NO lo devolvía —porque ya no estaba corriendo—. O sea que una lista de
    // opciones hacía algo destructivo y sin vuelta atrás.
    //
    // Ahora se conserva el ESTADO: si estaba corriendo, se cambia y arranca el
    // nuevo. Volver a elegir el anterior vuelve a donde estabas, que es lo que
    // espera cualquiera que se arrepiente a mitad de camino.
    const bool wasRunning = running();
    if (wasRunning) stop();
    // Y la pantalla se limpia: lo que dejó el agente anterior no es «lo de
    // antes», es de otro programa. Verlo ahí, debajo del cartel de arrancar,
    // hacía parecer que el cambio no había pasado.
    session_->limpiar();
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
    // Arrancar al toque no anda: `stop()` mata el proceso y el hijo tarda en
    // irse. Es la misma espera que usa `restart()`.
    if (wasRunning) QTimer::singleShot(300, this, &AgentViewModel::start);
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

QVariantMap AgentViewModel::modeloPropio() const {
    const ModeloPropio mp = ModeloPropio::leer(rt_.dataRoot() / "Config" / "modelo-propio.json");
    const Canal* c = canalElegido(mp, model_);
    QVariantMap m;
    m["url"] = QString::fromStdString(mp.url);
    m["modelo"] = QString::fromStdString(mp.modelo);
    m["canal"] = QString::fromStdString(c != nullptr ? c->id : std::string("broadcast"));
    m["nombre"] = QString::fromStdString(c != nullptr ? c->nombre : std::string());
    // Indentado: esto se edita a mano en una caja de texto, y un JSON en una
    // sola línea no se puede leer ni corregir.
    m["extra"] =
        QString::fromStdString(c != nullptr && c->extra.is_object() ? c->extra.dump(2) : std::string("{}"));
    m["instrucciones"] = QString::fromStdString(c != nullptr ? c->instrucciones : std::string());
    m["piensa"] = c != nullptr ? c->piensa : true;
    return m;
}

void AgentViewModel::probarModeloPropio() {
    const ModeloPropio mp = ModeloPropio::leer(rt_.dataRoot() / "Config" / "modelo-propio.json");
    if (mp.url.empty()) {
        emit notify("Falta la dirección del servidor.", "aviso");
        return;
    }
    QByteArray clave;
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    cred::read(root + "/Credenciales", "modelo-propio-api-key", clave);

    // `/models` es lo que toda API compatible con OpenAI contesta, y no gasta
    // ni un token. Preguntar generando texto para saber si el servidor está
    // sería pagar por un ping.
    std::string url = mp.url;
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    url += "/models";
    const std::string modelo = mp.modelo;
    const std::string autorizacion = "Bearer " + clave.trimmed().toStdString();

    emit notify("Probando " + QString::fromStdString(url) + "…", "info");
    // En un hilo suelto: ver el comentario del header.
    std::thread([this, url, modelo, autorizacion]() {
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers = {{"Authorization", autorizacion}, {"Accept", "application/json"}};
        req.timeoutMs = 8000;
        const net::HttpResponse r = net::HttpsClient::request(req);
        QString texto;
        QString nivel = "error";
        if (r.status == 0) {
            texto = "No contesta: " + QString::fromStdString(r.error);
        } else if (r.status == 401 || r.status == 403) {
            texto = "El servidor contesta, pero rechaza la clave (" + QString::number(r.status) + ").";
        } else if (r.status >= 400) {
            texto = "El servidor contestó " + QString::number(r.status) + ".";
        } else {
            // Que el modelo que vamos a pedir esté en la lista: un servidor
            // que anda pero no sirve ESE modelo falla recién al primer pedido,
            // y ahí el error no dice que el nombre estaba mal escrito.
            const bool estaEnLaLista = r.body.find("\"" + modelo + "\"") != std::string::npos;
            texto = estaEnLaLista
                        ? "Anda: el servidor contesta y sirve «" + QString::fromStdString(modelo) + "»."
                        : "El servidor contesta y la clave sirve, pero no veo el modelo «" +
                              QString::fromStdString(modelo) + "» en su lista. Revisá el nombre.";
            nivel = estaEnLaLista ? "ok" : "aviso";
        }
        QMetaObject::invokeMethod(
            this, [this, texto, nivel]() { emit notify(texto, nivel); }, Qt::QueuedConnection);
    }).detach();
}

QString AgentViewModel::guardarModeloPropio(const QString& url, const QString& modelo, const QString& nombre,
                                            const QString& extraJson, const QString& instrucciones,
                                            bool piensa) {
    if (url.trimmed().isEmpty()) return "Falta la dirección del servidor (baseURL).";
    if (modelo.trimmed().isEmpty()) return "Falta el nombre del modelo que pide tu servidor.";
    json extra = json::object();
    const std::string texto = extraJson.trimmed().toStdString();
    if (!texto.empty()) {
        // Se valida ACA y no se deja para cuando arranque el agente: un JSON
        // roto escrito en la configuración se descubre recién al arrancar, con
        // un error de opencode que no dice qué campo estaba mal.
        try {
            extra = json::parse(texto);
        } catch (const json::exception& e) {
            return QString("Los parámetros extra no son un JSON válido: ") + e.what();
        }
        if (!extra.is_object()) return "Los parámetros extra tienen que ser un objeto { ... }.";
    }
    const std::filesystem::path archivo = rt_.dataRoot() / "Config" / "modelo-propio.json";
    ModeloPropio mp = ModeloPropio::leer(archivo);
    mp.url = url.trimmed().toStdString();
    mp.modelo = modelo.trimmed().toStdString();
    const Canal* actual = canalElegido(mp, model_);
    Canal c;
    c.id = actual != nullptr ? actual->id : std::string("broadcast");
    c.limite = actual != nullptr ? actual->limite : json::object();
    c.nombre = nombre.trimmed().toStdString();
    c.extra = extra;
    c.instrucciones = instrucciones.toStdString();
    c.piensa = piensa;
    bool reemplazado = false;
    for (Canal& v : mp.canales)
        if (v.id == c.id) {
            v = c;
            reemplazado = true;
        }
    if (!reemplazado) mp.canales.push_back(c);
    if (!mp.guardar(archivo)) return "No se pudo guardar la configuración.";
    emit configChanged(); // la lista de modelos cambia con los canales
    return {};
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

json AgentViewModel::pinnedPins() const {
    QFile f(":/agent/agentes.json");
    if (!f.open(QIODevice::ReadOnly)) return json::object();
    return json::parse(f.readAll().toStdString(), nullptr, false);
}

QString AgentViewModel::chosenFile() const {
    return QString::fromStdWString((rt_.dataRoot() / "Agentes" / "versiones.json").wstring());
}

json AgentViewModel::pins() const {
    json out = pinnedPins();
    // Las elegidas desde la pestaña (checkUpdate + installLatest) pisan a la
    // fijada, agente por agente. Viven en el bundle, no en el sistema.
    QFile f(chosenFile());
    if (out.is_object() && f.open(QIODevice::ReadOnly)) {
        const json elegidas = json::parse(f.readAll().toStdString(), nullptr, false);
        if (elegidas.is_object())
            for (const auto& [k, v] : elegidas.items())
                if (v.is_object() && v.contains("version") && v.contains("plataformas")) out[k] = v;
    }
    return out;
}

// --- Actualizar desde la fuente oficial ---------------------------------------------------
//
// Cada agente publica su ultima version con un hash por plataforma; se arma con
// eso una entrada con la MISMA forma que agentes.json y la instalacion la
// verifica igual que a la fijada. Si la fuente no publica hash, no se instala.

json AgentViewModel::fetchLatest(const QString& runtime, const json& base, QString* err) {
    std::string e;
    json out = ultimaPublicada(runtime.toStdString(), base, &e);
    if (err != nullptr) *err = QString::fromStdString(e);
    return out;
}

QString AgentViewModel::latestVersion() const {
    const auto it = latest_.find(runtimeOf(provider_));
    return it == latest_.end() ? QString()
                               : QString::fromStdString(it->second.value("version", std::string()));
}

bool AgentViewModel::newerAvailable() const {
    const QString ultima = latestVersion();
    if (ultima.isEmpty()) return false;
    const QString usada = installed() ? installedVersion() : current().value("version").toString();
    return QVersionNumber::fromString(ultima) > QVersionNumber::fromString(usada);
}

QString AgentViewModel::pinnedVersion() const {
    const json pin = pinnedPins().value(runtimeOf(provider_).toStdString(), json::object());
    return QString::fromStdString(pin.value("version", std::string()));
}

bool AgentViewModel::usingChosenVersion() const {
    return current().value("version").toString() != pinnedVersion();
}

void AgentViewModel::checkUpdate() {
    if (checking_ || installing_) return;
    const QString runtime = runtimeOf(provider_);
    if (runtime.isEmpty()) return;
    const json base = pinnedPins().value(runtime.toStdString(), json::object());
    checking_ = true;
    emit latestChanged();
    if (checkThread_.joinable()) checkThread_.join();
    checkThread_ = std::thread([this, runtime, base]() {
        QString err;
        json nueva = fetchLatest(runtime, base, &err);
        QMetaObject::invokeMethod(this, [this, runtime, nueva, err]() {
            checking_ = false;
            if (!err.isEmpty() || !nueva.is_object()) {
                emit latestChanged();
                setError("No se pudo buscar la actualizacion: " + err);
                return;
            }
            latest_[runtime] = nueva;
            if (platformPin(nueva).empty()) {
                latest_.erase(runtime);
                emit latestChanged();
                setError("La version nueva no se publico con hash para esta plataforma (" + platformKey() +
                         "): no se ofrece.");
                return;
            }
            emit latestChanged();
            const QString v = QString::fromStdString(nueva.value("version", std::string()));
            if (newerAvailable())
                emit notify("Hay una version nueva: " + v + ".", "ok");
            else
                emit notify("Esta al dia (la ultima publicada es " + v + ").", "ok");
        });
    });
}

void AgentViewModel::installLatest() {
    if (installing_) return;
    const QString runtime = runtimeOf(provider_);
    const auto it = latest_.find(runtime);
    if (it == latest_.end()) return;
    // Queda elegida en el bundle y desde ahi la toman install() y usableDir().
    QFile f(chosenFile());
    json elegidas = json::object();
    if (f.open(QIODevice::ReadOnly)) {
        elegidas = json::parse(f.readAll().toStdString(), nullptr, false);
        f.close();
        if (!elegidas.is_object()) elegidas = json::object();
    }
    elegidas[runtime.toStdString()] = it->second;
    QDir().mkpath(QFileInfo(chosenFile()).absolutePath());
    if (!writeText(chosenFile(), QByteArray::fromStdString(elegidas.dump(2)))) {
        setError("No se pudo guardar " + chosenFile());
        return;
    }
    emit configChanged();
    emit latestChanged();
    install();
}

void AgentViewModel::revertToPinned() {
    if (installing_) return;
    const QString runtime = runtimeOf(provider_);
    QFile f(chosenFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    json elegidas = json::parse(f.readAll().toStdString(), nullptr, false);
    f.close();
    if (!elegidas.is_object() || !elegidas.contains(runtime.toStdString())) return;
    elegidas.erase(runtime.toStdString());
    writeText(chosenFile(), QByteArray::fromStdString(elegidas.dump(2)));
    emit configChanged();
    emit latestChanged();
    install(); // la fijada: se baja y, cuando esta, reemplaza a la elegida
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
    const QString base = QString::fromStdWString((rt_.dataRoot() / "Agentes").wstring()) + "/" + runtime;
    const QString fijada = QString::fromStdString(pin.value("version", std::string()));

    // El `.ok` guarda {version, exe} desde esta version; los viejos tenian solo
    // una fecha y para esos se cae al `exe` del pin, que es lo que habia.
    const auto leerOk = [&](const QString& dir, QString* exe) {
        QFile f(dir + "/.ok");
        if (!f.open(QIODevice::ReadOnly)) return false;
        const json j = json::parse(f.readAll().toStdString(), nullptr, false);
        if (exe != nullptr && j.is_object()) *exe = QString::fromStdString(j.value("exe", std::string()));
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

bool AgentViewModel::installed() const {
    // El modelo local no se instala acá: se baja en Configuración → Modelos locales.
    if (runtimeOf(provider_) == "local") return static_cast<bool>(local_);
    return !usableDir(runtimeOf(provider_)).isEmpty();
}

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
        std::string sha512 = {}; ///< base64, como lo publica npm (si no hay sha256)
    };
    std::vector<Item> items;
    const QString dir = installDir(runtime);
    if (runtime == "gemini") {
        items.push_back(
            {plat.value("url", ""), plat.value("sha256", ""), "archivo", (dir + "/node").toStdString()});
        const json pkg = pin.value("paquete", json::object());
        items.push_back({pkg.value("url", ""), pkg.value("sha256", ""), "archivo",
                         (dir + "/gemini").toStdString(), pkg.value("sha512", "")});
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
    // Gemini corre un script del paquete de npm: si una version nueva lo movio,
    // que falle la instalacion (y quede la anterior) y no el arranque.
    const QString geminiScript =
        runtime == "gemini"
            ? QString::fromStdString(pin.value("paquete", json::object()).value("script", std::string()))
            : QString();
    // Se baja y se descomprime en una carpeta aparte y recien al final se pone
    // en su lugar: si la descarga falla (sin red, hash que no coincide, se
    // cancela), la instalacion que YA andaba queda intacta. Antes el camino de
    // error borraba `dir`, que podia ser una instalacion buena.
    const QString staging = QFileInfo(dir).absolutePath() + "/.instalando-" + QFileInfo(dir).fileName();
    installThread_ = std::thread([this, items, dir, staging, runtime, exeName, geminiScript]() {
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
            // SHA-256 (agentes.json, GitHub, Claude) o SHA-512 (npm). Sin
            // ninguno no se instala: nunca un archivo sin verificar.
            bool coincide = false;
            if (!it.sha.empty()) {
                coincide = sha256File(tmp) == QString::fromStdString(it.sha);
            } else if (!it.sha512.empty()) {
                QFile bajado(tmp);
                QCryptographicHash h(QCryptographicHash::Sha512);
                if (bajado.open(QIODevice::ReadOnly)) h.addData(&bajado);
                coincide = h.result().toBase64() == QByteArray::fromStdString(it.sha512);
            }
            if (!coincide) {
                err = "La descarga no coincide con el hash publicado. No se instalo nada.";
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
        if (err.isEmpty() && !geminiScript.isEmpty() &&
            !QFileInfo::exists(staging + "/gemini/" + geminiScript))
            err = "El paquete de Gemini no trae " + geminiScript +
                  ": no se instalo (queda la version anterior).";
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
            emit latestChanged(); // newerAvailable mira la version instalada
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

QString AgentViewModel::runtimeDe(const QString& proveedor) {
    const Provider* p = findProvider(proveedor);
    return p == nullptr ? QString() : QString(p->runtime);
}

QStringList AgentViewModel::modelosDe(const QString& proveedor) {
    // El modelo propio no tiene una lista escrita: sus modelos son los canales
    // que haya configurados, y eso solo se sabe leyendo su configuración.
    if (proveedor == "propio") {
        QStringList out;
        for (const auto& m :
             ModeloPropio::leer(g_raiz / "Config" / "modelo-propio.json").comoLosLlamaOpencode())
            out << QString::fromStdString(m);
        return out;
    }
    const Provider* p = findProvider(proveedor);
    return p == nullptr ? QStringList() : p->models;
}

QString AgentViewModel::prepareWorkspace(QString* error) {
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    const QString prompt = systemPrompt_ ? systemPrompt_() : QString();
    // Con su juego de herramientas (ver Roles::juegoDelAsistente) y CON
    // consola: es el asistente del operador, no un agente de un tiro.
    return prepareWorkspaceEn(root + "/Runtime/agente", provider_, model_, {"--rol", "asistente"}, prompt,
                              error, true);
}

QString AgentViewModel::prepareWorkspaceEn(const QString& ws, const QString& proveedor, const QString& modelo,
                                           const QStringList& bridgeArgs, const QString& prompt,
                                           QString* error, bool consola) {
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    QDir().mkpath(ws);
    QByteArray p = prompt.toUtf8();
    // Lo que este modelo en particular necesita que le digan, al final del
    // prompt del sistema. Ver `Canal::instrucciones`: un modelo chico suele
    // necesitar las reglas más cortas y más terminantes que uno grande.
    if (proveedor == "propio") {
        const ModeloPropio mp = ModeloPropio::leer(rt_.dataRoot() / "Config" / "modelo-propio.json");
        const Canal* c = canalElegido(mp, modelo);
        if (c != nullptr && !c->instrucciones.empty()) {
            p += "\n\n";
            p += QByteArray::fromStdString(c->instrucciones);
        }
    }
    for (const char* f : {"CLAUDE.md", "GEMINI.md", "AGENTS.md"})
        if (!writeText(ws + "/" + f, p)) {
            if (error != nullptr) *error = "No se pudo escribir " + ws;
            return {};
        }
    const std::string bridge = bridgePath().toStdString();
    json args = json::array();
    for (const QString& a : bridgeArgs)
        args.push_back(a.toStdString());
    const json mcpStdio{{"command", bridge}, {"args", args}, {"env", {{"BA_DATA_ROOT", root.toStdString()}}}};
    const QString runtime = runtimeOf(proveedor);
    const Provider* prov = findProvider(proveedor);
    if (runtime == "claude") {
        json m = mcpStdio;
        m["type"] = "stdio";
        writeText(ws + "/mcp.json",
                  QByteArray::fromStdString(json{{"mcpServers", {{"broadcast", m}}}}.dump(2)));
    } else if (runtime == "opencode") {
        json cmd = json::array({bridge});
        for (const auto& a : args)
            cmd.push_back(a);
        json cfg{{"$schema", "https://opencode.ai/config.json"},
                 {"autoupdate", false},
                 {"mcp",
                  {{"broadcast",
                    {{"type", "local"},
                     {"command", cmd},
                     {"environment", {{"BA_DATA_ROOT", root.toStdString()}}},
                     {"enabled", true}}}}},
                 // Consola permitida (igual que en Claude Code): el limite lo
                 // pone el prompt del sistema, no la configuracion del agente.
                 // Un agente de un tiro (con rol) no la tiene: solo el MCP.
                 // La consola se decide APARTE, y no mirando si hay argumentos
                 // en el puente. Estaba atada a `bridgeArgs.isEmpty()`, asi que
                 // darle un juego de herramientas al asistente le habria apagado
                 // la consola de rebote, sin que nadie lo pidiera ni lo notara.
                 {"permission", {{"edit", "deny"}, {"bash", consola ? "allow" : "deny"}}},
                 {"instructions", json::array({"AGENTS.md"})}};
        if (!modelo.isEmpty()) cfg["model"] = modelo.toStdString();
        // El modelo de la casa: se le arma a opencode el proveedor compatible
        // con OpenAI y se le dice que use ESE. Ver ModeloPropio.hpp —este
        // aparato no le habla al modelo, le escribe la configuración a quien
        // sí sabe hablarle—.
        if (proveedor == "propio") {
            const ModeloPropio mp = ModeloPropio::leer(rt_.dataRoot() / "Config" / "modelo-propio.json");
            if (mp.completo()) {
                // UN PROVEEDOR POR CANAL. Los canales son el MISMO modelo del
                // mismo servidor con otros parámetros, y esos parámetros van
                // en las opciones del proveedor, no del modelo: para tener
                // dos juegos hay que declarar dos proveedores. Es lo mismo que
                // hace a mano cualquiera que use varios canales.
                json provs = json::object();
                for (const Canal& c : mp.canales) {
                    json opciones{{"baseURL", mp.url}};
                    // La clave va por entorno y no escrita en el archivo: el
                    // workspace es una carpeta que se puede copiar o mirar, y
                    // una clave ahí adentro es una clave filtrada.
                    opciones["apiKey"] = "{env:BA_MODELO_PROPIO_KEY}";
                    // MEDIDO: `options.extraBody` del proveedor NO llega al
                    // servidor. Se probó poniendo `max_tokens: 16` ahí y el
                    // modelo contestó párrafos enteros; con los mismos
                    // parámetros por curl, el servidor obedece. Los parámetros
                    // extra viajan en las opciones DEL MODELO, que es lo que
                    // opencode manda en el cuerpo del pedido. Se deja también
                    // en el proveedor por si una versión lo usa: sobra, no
                    // molesta.
                    if (c.extra.is_object() && !c.extra.empty()) opciones["extraBody"] = c.extra;
                    json m = json::object();
                    m["name"] = c.nombre.empty() ? c.id : c.nombre;
                    // Lo que opencode necesita saber del modelo. `reasoning`
                    // tiene que coincidir con `enable_thinking`: si dice que
                    // razona y el servidor no manda pensamiento, la interfaz
                    // se queda esperando un bloque que no llega.
                    m["tool_call"] = true;
                    m["temperature"] = true;
                    m["reasoning"] = c.piensa;
                    // Que acepta imágenes: sin esto opencode no le manda
                    // ninguna, y el agente que mira no tendría qué mirar.
                    m["attachment"] = true;
                    m["modalities"] =
                        json{{"input", json::array({"text", "image"})}, {"output", json::array({"text"})}};
                    if (c.limite.is_object() && !c.limite.empty()) m["limit"] = c.limite;
                    if (c.extra.is_object() && !c.extra.empty()) m["options"] = c.extra;
                    provs["propio-" + c.id] = json{{"npm", "@ai-sdk/openai-compatible"},
                                                   {"name", c.nombre.empty() ? c.id : c.nombre},
                                                   {"options", opciones},
                                                   {"models", {{mp.modelo, m}}}};
                }
                cfg["provider"] = provs;
                // El canal elegido, o el primero si no eligieron ninguno.
                const std::string pedido = modelo.toStdString();
                const auto todos = mp.comoLosLlamaOpencode();
                cfg["model"] =
                    std::find(todos.begin(), todos.end(), pedido) != todos.end() ? pedido : todos.front();
            }
        }
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

term::Pty::Options AgentViewModel::baseLaunch(const QString& proveedor, const QString& modelo,
                                              const QString& ws, QString* error) {
    term::Pty::Options o;
    const QString runtime = runtimeOf(proveedor);
    const Provider* prov = findProvider(proveedor);
    const json pin = pins().value(runtime.toStdString(), json::object());
    const json plat = platformPin(pin);
    // La instalacion que HAY (puede ser de otra version si se actualizo el
    // sistema), no la que fija esta version.
    QString exeInstalado;
    const QString dir = usableDir(runtime, &exeInstalado);
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    // Las credenciales son POR MOTOR, no por rol: un solo login sirve para el
    // interactivo y para todos los roles que usen ese motor.
    const QString profile = root + "/Credenciales/agentes/" + runtime;
    QDir().mkpath(profile);
    o.workDir = ws;
    o.env["BA_DATA_ROOT"] = root;
    o.env["TERM"] = "xterm-256color";
    o.unsetEnv = {"ANTHROPIC_API_KEY",  "ANTHROPIC_AUTH_TOKEN", "GEMINI_API_KEY",  "GOOGLE_API_KEY",
                  "OPENROUTER_API_KEY", "CLAUDECODE",           "LD_LIBRARY_PATH", "BA_MODELO_PROPIO_KEY"};
    if (prov != nullptr && !QString(prov->keyName).isEmpty()) {
        QByteArray key;
        if (!cred::read(root + "/Credenciales", prov->keyName, key)) {
            if (error != nullptr) *error = "Falta la clave de API (pegala en el panel de la derecha).";
            return {};
        }
        o.env[prov->envKey] = QString::fromUtf8(key).trimmed();
    }
    if (runtime == "claude") {
        // El ejecutable sale de la instalacion que HAY (puede ser de otra
        // version si se actualizo el sistema).
        o.program =
            dir + "/" +
            (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "claude")) : exeInstalado);
        o.args = {"--mcp-config", ws + "/mcp.json", "--strict-mcp-config"};
        if (!modelo.isEmpty()) o.args << "--model" << modelo;
        o.env["CLAUDE_CONFIG_DIR"] = profile;
        o.env["DISABLE_AUTOUPDATER"] = "1";
    } else if (runtime == "gemini") {
        o.program = dir + "/node/" +
                    (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "")) : exeInstalado);
        o.args = {dir + "/gemini/" +
                  QString::fromStdString(pin.value("paquete", json::object()).value("script", ""))};
        if (!modelo.isEmpty()) o.args << "-m" << modelo;
        o.env["HOME"] = profile;
        o.env["USERPROFILE"] = profile;
        o.env["NO_UPDATE_NOTIFIER"] = "1";
    } else if (runtime == "opencode") {
        o.program =
            dir + "/" +
            (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "opencode")) : exeInstalado);
        o.env["XDG_CONFIG_HOME"] = profile + "/config";
        o.env["XDG_DATA_HOME"] = profile + "/data";
        o.env["XDG_STATE_HOME"] = profile + "/state";
        o.env["XDG_CACHE_HOME"] = root + "/Cache/opencode";
        o.env["OPENCODE_DISABLE_AUTOUPDATE"] = "1";
    } else if (error != nullptr) {
        *error = "Proveedor desconocido: " + proveedor;
    }
    return o;
}

term::Pty::Options AgentViewModel::launchOptions(QString* error) {
    const QString ws = prepareWorkspace(error);
    if (ws.isEmpty()) return {};
    term::Pty::Options o = baseLaunch(provider_, model_, ws, error);
    if (o.program.isEmpty()) return o;
    if (runtimeOf(provider_) == "claude") {
        // El MCP y la consola del sistema: se le pide arreglar cosas de la
        // maquina, no solo manejar la emision. En Windows la consola es la
        // herramienta PowerShell; Bash es la de Linux (y la de Git Bash).
        // El limite lo pone el prompt del sistema: con el aire encendido, todo
        // lo que pueda afectarlo se consulta antes (ver McpHelp.cpp).
        // Edit/Write siguen fuera: los archivos se tocan por consola, que es
        // donde el operador ve el comando exacto.
        // EN VIVO, sin consola: un comando del sistema no pasa por las guardas
        // de aire del MCP (ver McpAire). Al terminar el aire se relanza con ella.
        conConsola_ = !(hayAire_ && hayAire_());
        if (conConsola_)
            o.args << "--allowedTools" << "mcp__broadcast" << "Bash" << "PowerShell" << "--disallowedTools"
                   << "Edit" << "Write" << "NotebookEdit";
        else
            o.args << "--allowedTools" << "mcp__broadcast" << "--disallowedTools" << "Bash" << "PowerShell"
                   << "Edit" << "Write" << "NotebookEdit";
    }
    return o;
}

Motores AgentViewModel::motores() {
    Motores m;
    m.local = [this](const Lanzamiento& l, const std::string& origen, std::string* resumen) {
        const QString proveedor = l.proveedor.isEmpty() ? provider_ : l.proveedor;
        if (runtimeOf(proveedor) != "local") return false;
        if (!local_) {
            *resumen = "el modelo local no está disponible en esta versión";
            return true;
        }
        local_(l.rol, l.encargo, origen.rfind("disparador", 0) == 0);
        *resumen = "Lo atendió el modelo local: lo dijo por los auriculares y, si había algo para hacer, "
                   "lo propuso en la barra del asistente.";
        return true;
    };
    m.lanzar = [this](const Lanzamiento& l, QString* err) { return launchOptionsHeadless(l, err); };
    m.proveedores = [this]() { return providers(); };
    m.modelosDe = [](const QString& p) { return modelosDe(p); };
    return m;
}

term::Pty::Options AgentViewModel::launchOptionsHeadless(const Lanzamiento& l, QString* error) {
    const QString proveedor = l.proveedor.isEmpty() ? provider_ : l.proveedor;
    const QString runtime = runtimeOf(proveedor);
    if (runtime == "gemini") {
        // Gemini CLI tiene modo no interactivo, pero no lo probamos con el
        // MCP ni con su salida: hasta entonces, los roles no lo usan.
        if (error != nullptr) *error = "Los roles automaticos todavia no pueden usar Gemini.";
        return {};
    }
    if (runtime.isEmpty()) {
        if (error != nullptr) *error = "Proveedor desconocido: " + proveedor;
        return {};
    }
    const QString root = QString::fromStdWString(rt_.dataRoot().wstring());
    const QString ws =
        // La consola (y con ella los archivos y la red) la decide EL ROL, no
        // esta linea: antes iba `false` fijo para todos.
        prepareWorkspaceEn(root + "/Runtime/agentes/" + l.rol, proveedor, l.modelo, {"--rol", l.rol},
                           l.prompt, error, l.consola);
    if (ws.isEmpty()) return {};
    term::Pty::Options o = baseLaunch(proveedor, l.modelo, ws, error);
    if (o.program.isEmpty()) return o;
    if (runtime == "claude") {
        // Un tiro: el encargo por argumento, cada paso como JSON por stdout
        // (--verbose es lo que hace que stream-json emita los pasos y no
        // solo el final). Solo el MCP: sin consola, sin archivos, sin web.
        o.args << "-p" << l.encargo << "--output-format" << "stream-json" << "--verbose" << "--allowedTools"
               << "mcp__broadcast" << "--disallowedTools" << "Bash" << "PowerShell" << "Edit" << "Write"
               << "NotebookEdit" << "WebFetch" << "WebSearch";
    } else if (runtime == "opencode") {
        o.args = {"run", "--format", "json"};
        if (!l.modelo.isEmpty()) o.args << "--model" << l.modelo;
        // El mensaje PRIMERO y los adjuntos al final. `-f` toma una LISTA, así
        // que si va antes se traga el mensaje como si fuera otro archivo:
        // medido, `opencode` respondió «File not found: ENCARGO: Mirá la
        // imagen…». Ver `Lanzamiento::adjuntos`.
        o.args << l.encargo;
        for (const QString& f : l.adjuntos)
            o.args << "-f" << f;
    }
    return o;
}

void AgentViewModel::start() {
    setError({});
    // El modelo local no abre terminal: queda «andando» y atiende lo que llega por send().
    if (runtimeOf(provider_) == "local") {
        if (!local_) {
            setError("El modelo local no está disponible en esta versión.");
            return;
        }
        localActivo_ = true;
        emit runningChanged();
        return;
    }
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
    if (localActivo_) {
        localActivo_ = false;
        emit runningChanged();
        return;
    }
    if (session_->running()) expectedStop_ = true;
    session_->stop();
    emit runningChanged();
}

void AgentViewModel::restart() {
    stop();
    QTimer::singleShot(300, this, &AgentViewModel::start);
}

void AgentViewModel::setHayAire(std::function<bool()> fn) {
    hayAire_ = std::move(fn);
    if (vigiaAire_ != nullptr) return;
    vigiaAire_ = new QTimer(this);
    vigiaAire_->setInterval(2000);
    connect(vigiaAire_, &QTimer::timeout, this, [this]() {
        if (!session_->running() || !hayAire_) return;
        const bool aire = hayAire_();
        // Salió al aire con la consola prendida: se relanza sin ella.
        if (aire && conConsola_) {
            setError("Al aire: el asistente se relanzó sin consola (Bash/PowerShell) hasta que termine la emisión.");
            restart();
        }
    });
    vigiaAire_->start();
}

void AgentViewModel::send(const QString& text) {
    if (text.trimmed().isEmpty()) return;
    if (!running()) {
        setError("El agente no esta corriendo.");
        return;
    }
    if (localActivo_) {
        if (local_) local_("asistente", text.trimmed(), false);
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

void AgentViewModel::setAlertSilenciada(const QString& id, bool silenciada) {
    alerts_.update(id.toStdString(), json{{"silenciada", silenciada}});
    emit alertsChanged();
}

void AgentViewModel::deleteAlert(const QString& id) {
    alerts_.remove(id.toStdString());
    emit alertsChanged();
}

QVariantList AgentViewModel::alertTypesUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", AlertEngine::types()}}.dump()))
        .object()
        .value("v")
        .toArray()
        .toVariantList();
}

QString AgentViewModel::createAlertUi(const QVariantMap& spec) {
    const json j = json::parse(QJsonDocument(QJsonObject::fromVariantMap(spec)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    if (!j.is_object()) return "no se entiende el disparador";
    const json r = alerts_.create(j);
    emit alertsChanged();
    return QString::fromStdString(r.value("error", std::string()));
}

void AgentViewModel::onAlert(const json& ev) {
    emit alertsChanged();
    // Si un rol activo lo reclama, lo atiende un agente de un tiro: esta
    // conversacion no se interrumpe.
    if (reclamado_ && reclamado_(ev)) return;
    bool llm = false;
    for (const auto& n : ev.value("notify", json::array()))
        if (n == "llm") llm = true;
    if (!llm || !alertsToChat_ || !running()) return;
    const QString t = QDateTime::fromMSecsSinceEpoch(ev.value("atMs", std::int64_t{0})).toString("HH:mm:ss");
    // Sin texto de invitados/publico (llmText): leerlo es decision del asistente
    // con una herramienta, y eso activa la salvaguarda de su sesion.
    pendingAlerts_ << "[" + t + "] " + QString::fromStdString(ev.value("llmText", ev.value("text", ""))) +
                          " (disparador " + QString::fromStdString(ev.value("ruleId", "")) + ")";
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

// --- Limpieza ----------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

std::uintmax_t bytesDe(const fs::path& p) {
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return fs::file_size(p, ec);
    std::uintmax_t total = 0;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), fin;
         !ec && it != fin; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

/// Lo que cada motor guarda como conversaciones dentro de su perfil (relativo a
/// Credenciales/agentes/<motor>). NO está: el login (.claude.json,
/// .credentials.json, oauth_creds.json, auth.json), los ajustes, plugins y skills.
std::vector<std::string> chatsDe(const std::string& runtime) {
    if (runtime == "claude")
        return {"projects", "sessions", "session-env", "shell-snapshots", "todos", "file-history", "paste-cache",
                "debug", "backups", "history.jsonl", "statsig", "telemetry", "cache"};
    if (runtime == "gemini") return {".gemini/tmp", ".gemini/history", ".gemini/logs"};
    if (runtime == "opencode")
        return {"data/opencode/storage", "data/opencode/log", "data/opencode/snapshot", "state"};
    return {};
}

const char* const kMotores[] = {"claude", "gemini", "opencode"};

} // namespace

QString AgentViewModel::ocupadoParaLimpiar() const {
    if (running()) return "Primero pará el asistente (la consola está corriendo).";
    if (corridasEnCurso_ && corridasEnCurso_() > 0) return "Hay encargos de agentes corriendo: esperá que terminen.";
    if (installing_) return "Hay una instalación en curso.";
    return {};
}

void AgentViewModel::revisarLimpieza() {
    QVariantList out;
    const fs::path root = rt_.dataRoot();
    for (const char* rt : kMotores) {
        const fs::path prog = root / "Agentes" / rt;
        const fs::path perfil = root / "Credenciales" / "agentes" / rt;
        std::error_code ec;
        if (!fs::exists(prog, ec) && !fs::exists(perfil, ec)) continue;
        const QString enUsoDir = usableDir(rt);
        QVariantList versiones;
        double bytesPrograma = 0;
        for (const fs::directory_entry& d : fs::directory_iterator(prog, ec)) {
            if (!d.is_directory(ec)) continue;
            const double b = static_cast<double>(bytesDe(d.path()));
            bytesPrograma += b;
            const QString dir = QString::fromStdWString(d.path().wstring());
            versiones.push_back(QVariantMap{{"version", QString::fromStdString(d.path().filename().string())},
                                            {"bytes", b},
                                            {"enUso", QDir::cleanPath(dir) == QDir::cleanPath(enUsoDir)}});
        }
        double bytesChats = 0;
        for (const std::string& c : chatsDe(rt)) bytesChats += static_cast<double>(bytesDe(perfil / c));
        out.push_back(QVariantMap{{"runtime", QString(rt)},
                                  {"versiones", versiones},
                                  {"bytesPrograma", bytesPrograma},
                                  {"bytesChats", bytesChats},
                                  {"bytesPerfil", static_cast<double>(bytesDe(perfil))},
                                  {"enUso", runtimeOf(provider_) == rt}});
    }
    // Los espacios de trabajo (Runtime/agente, Runtime/agentes/<rol>): se rehacen al lanzar.
    double bytesTrabajo = static_cast<double>(bytesDe(root / "Runtime" / "agente")) +
                          static_cast<double>(bytesDe(root / "Runtime" / "agentes"));
    out.push_back(QVariantMap{{"runtime", QString("espacios")}, {"bytesChats", bytesTrabajo}});
    limpieza_ = out;
    emit limpiezaChanged();
}

QVariantMap AgentViewModel::borrarChats(const QString& runtime) {
    const QString ocupado = ocupadoParaLimpiar();
    if (!ocupado.isEmpty()) return {{"ok", false}, {"error", ocupado}};
    const fs::path root = rt_.dataRoot();
    double bytes = 0;
    const auto borrar = [&](const fs::path& p) {
        std::error_code ec;
        if (!fs::exists(p, ec)) return;
        bytes += static_cast<double>(bytesDe(p));
        fs::remove_all(p, ec);
    };
    for (const char* rt : kMotores) {
        if (!runtime.isEmpty() && runtime != rt) continue;
        for (const std::string& c : chatsDe(rt)) borrar(root / "Credenciales" / "agentes" / rt / c);
    }
    if (runtime.isEmpty()) {
        borrar(root / "Runtime" / "agente");
        borrar(root / "Runtime" / "agentes");
    }
    revisarLimpieza();
    return {{"ok", true}, {"bytes", bytes}};
}

QVariantMap AgentViewModel::borrarVersionesViejas(const QString& runtime) {
    const QString ocupado = ocupadoParaLimpiar();
    if (!ocupado.isEmpty()) return {{"ok", false}, {"error", ocupado}};
    double bytes = 0;
    QStringList borradas;
    for (const char* rt : kMotores) {
        if (!runtime.isEmpty() && runtime != rt) continue;
        const QString enUso = QDir::cleanPath(usableDir(rt));
        if (enUso.isEmpty()) continue; // sin una que ande no se borra nada: podría ser la única
        std::error_code ec;
        for (const fs::directory_entry& d : fs::directory_iterator(rt_.dataRoot() / "Agentes" / rt, ec)) {
            if (!d.is_directory(ec) || QDir::cleanPath(QString::fromStdWString(d.path().wstring())) == enUso) continue;
            bytes += static_cast<double>(bytesDe(d.path()));
            std::error_code e2;
            fs::remove_all(d.path(), e2);
            if (!e2) borradas << QString(rt) + " " + QString::fromStdString(d.path().filename().string());
        }
    }
    revisarLimpieza();
    return {{"ok", true}, {"bytes", bytes}, {"borradas", borradas}};
}

QString AgentViewModel::reinstalar(const QString& runtime, bool deCero) {
    const QString ocupado = ocupadoParaLimpiar();
    if (!ocupado.isEmpty()) return ocupado;
    bool conocido = false;
    for (const char* rt : kMotores) conocido = conocido || runtime == rt;
    if (!conocido) return "Motor desconocido: " + runtime;
    const fs::path root = rt_.dataRoot();
    std::error_code ec;
    fs::remove_all(root / "Agentes" / runtime.toStdString(), ec);
    if (deCero) {
        fs::remove_all(root / "Credenciales" / "agentes" / runtime.toStdString(), ec);
        fs::remove_all(root / "Runtime" / "agente", ec);
        fs::remove_all(root / "Runtime" / "agentes", ec);
    }
    emit installChanged();
    revisarLimpieza();
    // Se vuelve a bajar el que usa el asistente; otro motor se baja cuando se lo elija.
    if (runtimeOf(provider_) == runtime) {
        install();
        return {};
    }
    return {};
}

} // namespace ba::app::agent
