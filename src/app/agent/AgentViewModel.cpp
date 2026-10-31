// ============================================================================
// src/app/agent/AgentViewModel.cpp — ver AgentViewModel.hpp
// ============================================================================

#include "agent/AgentViewModel.hpp"
#include "agent/ServidorModeloLocal.hpp"
#include "agent/ServidorOpencode.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Paths.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>

#include "agent/Actualizacion.hpp"
#include "agent/ControlSistema.hpp"
#include "agent/ModeloPropio.hpp"
#include "agent/ModelosLocales.hpp"

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
#include <QUuid>
#include <QFile>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMetaObject>
#include <QProcess>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>

#include <fstream>

#include "agent/AlertEngine.hpp"
#include "arranque/AppRuntime.hpp"
#include "carrusel/Conversion.hpp"
#include "i18n/I18n.hpp"
#include "net/Credentials.hpp"
#include "net/Descargas.hpp"
#include "net/HttpsClient.hpp"
#include "platform/Hilo.hpp"

#include <QPointer>
#include <chrono>
#include <set>

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
    const char* runtime; ///< claude | antigravity | opencode | pi
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
         QT_TRANSLATE_NOOP("asistente", "Claude — suscripción Pro/Max"),
         "cuenta",
         "",
         "",
         "https://claude.ai/upgrade",
         QT_TRANSLATE_NOOP("asistente", "Sin clave de API: la primera vez escribí /login en la terminal y elegí tu cuenta de Claude (se "
         "abre el "
         "navegador). La sesión queda guardada en la carpeta portable."),
         {"opus", "sonnet", "haiku"}},
        {"claude-api",
         "claude",
         QT_TRANSLATE_NOOP("asistente", "Claude — clave de API"),
         "api",
         "anthropic-api-key",
         "ANTHROPIC_API_KEY",
         "https://console.anthropic.com/settings/keys",
         QT_TRANSLATE_NOOP("asistente", "Pegá una clave de API de Anthropic (se paga por uso). La primera vez Claude Code pregunta si "
         "usarla: "
         "respondé que sí."),
         {"opus", "sonnet", "haiku"}},
        // Gemini con Antigravity CLI (el agente de Google que reemplazó al Gemini CLI).
        {"gemini-cuenta",
         "antigravity",
         QT_TRANSLATE_NOOP("asistente", "Gemini — cuenta de Google (Antigravity)"),
         "cuenta",
         "",
         "",
         "https://antigravity.google/",
         QT_TRANSLATE_NOOP("asistente", "Sin clave de API: la primera vez elegí «Google OAuth», abrí el enlace, entrá con tu cuenta de "
         "Google y pegá el código en la terminal. La sesión queda en la carpeta portable (nunca en el "
         "llavero del sistema). El modelo se elige con /model."),
         {}},
        {"gemini-api",
         "antigravity",
         QT_TRANSLATE_NOOP("asistente", "Gemini — clave de API (Antigravity)"),
         "api",
         "gemini-api-key",
         "GEMINI_API_KEY",
         "https://aistudio.google.com/apikey",
         QT_TRANSLATE_NOOP("asistente", "Pegá una clave de Google AI Studio: Antigravity va directo a la API de Gemini, sin iniciar sesión."),
         {}},
        {"opencode-go",
         "opencode",
         "OpenCode Go",
         "cuenta",
         "",
         "",
         "https://opencode.ai/",
         QT_TRANSLATE_NOOP("asistente", "La primera vez escribí /connect, elegí «OpenCode Go» y pegá la clave de tu suscripción; después "
         "/models "
         "para elegir el modelo. Queda guardado en la carpeta portable."),
         {}},
        {"openrouter",
         "opencode",
         "OpenRouter",
         "api",
         "openrouter-api-key",
         "OPENROUTER_API_KEY",
         "https://openrouter.ai/settings/keys",
         QT_TRANSLATE_NOOP("asistente", "Pegá una clave de OpenRouter y elegí el modelo (formato openrouter/<proveedor>/<modelo>)."),
         {"openrouter/anthropic/claude-sonnet-4.5", "openrouter/google/gemini-2.5-pro",
          "openrouter/openai/gpt-5"}},
        // El de la casa: un servidor propio que habla el protocolo de OpenAI.
        // La direccion, el modelo y los parametros extra no van en esta tabla
        // —son tuyos y se configuran— sino en Config/modelo-propio.json; ver
        // ModeloPropio.hpp. Aca solo queda dicho que existe y que usa clave.
        {"local",
         "pi",
         QT_TRANSLATE_NOOP("asistente", "Modelo local (en esta máquina, sin internet)"),
         "cuenta",
         "",
         "",
         "",
         QT_TRANSLATE_NOOP("asistente", "El modelo de Configuración → Modelos locales, con chat: pi (un agente de consola, como "
         "opencode) le habla al llama-server del programa y maneja el aparato por el MCP. Los "
         "encargos y los disparadores siguen por el camino de la voz (elige la acción y la propone en "
         "la barra). Rápido para lo sencillo; para lo complejo, mejor un asistente grande."),
         {}},
        {"propio",
         "opencode",
         QT_TRANSLATE_NOOP("asistente", "Modelo propio (API compatible con OpenAI)"),
         "api",
         "modelo-propio-api-key",
         "BA_MODELO_PROPIO_KEY",
         "",
         QT_TRANSLATE_NOOP("asistente", "Tu propio servidor. Configurale la direccion (baseURL), que modelo pedirle y los "
         "parametros extra que entienda, y pega su clave. Lo maneja opencode: este aparato no le "
         "habla al modelo, le arma la configuracion."),
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

AgentViewModel::AgentViewModel(AppRuntime& rt, AlertEngine& alerts, const QString& instancia, QObject* parent)
    : QObject(parent), rt_(rt), alerts_(alerts), session_(std::make_unique<term::TerminalSession>()),
      instancia_(instancia) {
    g_raiz = rt.dataRoot(); // ver g_raiz: `modelosDe` es estatica
    loadConfig();
    connect(&net::Descargas::la(), &net::Descargas::cambio, this, &AgentViewModel::seguirInstalacion);
    // El buscador de versiones nuevas es uno («agente»): lo registra el del evento.
    if (instancia_ == "evento") registrarActualizaciones();
    connect(session_.get(), &term::TerminalSession::runningChanged, this, &AgentViewModel::runningChanged);
    connect(session_.get(), &term::TerminalSession::finished, this, [this](int code) {
        const bool expected = expectedStop_;
        expectedStop_ = false;
        if (!expected && code != 0) setError(tr("El agente termino (codigo %1).").arg(code));
        pararServidorLocal(); // el chat del modelo local terminó: su llama-server también
    });
    // Las alertas: solo el de automatización (el del evento ya no las recibe).
    if (instancia_ == "automatizacion") connect(&alerts_, &AlertEngine::fired, this, &AgentViewModel::onAlert);
    // Lo que va pasando en el evento: a los dos, en tandas (ver mandarNovedades).
    tandaNovedades_ = new QTimer(this);
    tandaNovedades_->setSingleShot(true);
    tandaNovedades_->setInterval(ba::ajustes::entero("asistente.tandaNovedadesMs", 15000));
    connect(tandaNovedades_, &QTimer::timeout, this, &AgentViewModel::mandarNovedades);
    connect(&alerts_, &AlertEngine::novedad, this, [this](const QString& t) {
        if (!running() || !ba::ajustes::siNo("asistente.novedades", true)) return;
        novedades_ << t;
        if (!tandaNovedades_->isActive()) tandaNovedades_->start();
    });
    // Arranque automatico cuando todo este listo (GuiMode conecta el MCP despues).
    QTimer::singleShot(ba::ajustes::entero("asistente.demoraArranqueMs", 2500), this, [this]() {
        if (autoStart_ && reanudar_.isEmpty() && installed() && mcpReady() && !running()) start(); // (retomar: reanudarAlArrancar)
    });
}

AgentViewModel::~AgentViewModel() {
    // Las instalaciones en la cola se cortan y se espera a que suelten.
    cancelInstall();
    for (int i = 0; i < 100 && std::any_of(instalando_.begin(), instalando_.end(), [](const std::string& id) {
                        return net::Descargas::la().ocupado(id);
                    });
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (checkThread_.joinable()) checkThread_.join();
    session_->stop();
    pararServidorLocal(); // que el llama-server no quede andando al cerrar la app
}

// --- Configuracion -------------------------------------------------------------------

std::filesystem::path AgentViewModel::archivoConfig() const {
    return rt_.dataRoot() / "Config" /
           (instancia_ == "evento" ? std::string("asistente.json") : "asistente-" + instancia_.toStdString() + ".json");
}

void AgentViewModel::loadConfig() {
    // Las alertas van SOLO al asistente de automatización (siempre levantado): nunca
    // más se pegan en la charla del evento.
    alertsToChat_ = instancia_ == "automatizacion";
    if (instancia_ == "automatizacion") autoStart_ = true;
    std::ifstream in(archivoConfig());
    json j = json::parse(in, nullptr, false);
    // El de automatización sin configuración propia: arranca con el motor que ya usa el
    // del evento (no con uno de fábrica que capaz no está instalado ni con sesión).
    if (!j.is_object() && instancia_ != "evento") {
        std::ifstream ev(rt_.dataRoot() / "Config" / "asistente.json");
        const json e = json::parse(ev, nullptr, false);
        if (e.is_object()) j = json{{"provider", e.value("provider", std::string())}, {"models", e.value("models", json::object())}};
    }
    if (!j.is_object()) return;
    const QString p = QString::fromStdString(j.value("provider", std::string("claude-cuenta")));
    if (findProvider(p) != nullptr) provider_ = p;
    const json models = j.value("models", json::object());
    model_ = QString::fromStdString(models.value(provider_.toStdString(), std::string()));
    if (instancia_ == "automatizacion") alertsToChat_ = j.value("alertsToChat", true);
    autoStart_ = j.value("autoStart", instancia_ == "automatizacion");
    fontSize_ = j.value("fontSize", 15);
}

void AgentViewModel::saveConfig() const {
    const auto path = archivoConfig();
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
    out << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
}

QVariantList AgentViewModel::providers() const {
    QVariantList out;
    for (const auto& p : providerTable())
        out << QVariantMap{{"id", p.id},
                           {"runtime", p.runtime},
                           {"name", QCoreApplication::translate("asistente", p.name)},
                           {"auth", p.auth},
                           {"needsKey", QString(p.keyName) != ""},
                           {"keyUrl", p.keyUrl},
                           {"help", QCoreApplication::translate("asistente", p.help)},
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
    return cred::exists(QString::fromStdWString((ba::platform::credentialsDir()).wstring()), p->keyName);
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
        QString::fromStdString(c != nullptr && c->extra.is_object() ? c->extra.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) : std::string("{}"));
    m["instrucciones"] = QString::fromStdString(c != nullptr ? c->instrucciones : std::string());
    m["piensa"] = c != nullptr ? c->piensa : true;
    return m;
}

void AgentViewModel::probarModeloPropio() {
    const ModeloPropio mp = ModeloPropio::leer(rt_.dataRoot() / "Config" / "modelo-propio.json");
    if (mp.url.empty()) {
        emit notify(tr("Falta la dirección del servidor."), "aviso");
        return;
    }
    QByteArray clave;
    const QString root = QString::fromStdWString(ba::platform::dataRoot().wstring()); // de la máquina
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

    emit notify(tr("Probando %1…").arg(QString::fromStdString(url)), "info");
    // En un hilo suelto: ver el comentario del header.
    std::thread([this, url, modelo, autorizacion]() {
        ba::platform::nombrarHilo("agente-probar");
        net::HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.headers = {{"Authorization", autorizacion}, {"Accept", "application/json"}};
        req.timeoutMs = ba::ajustes::entero("asistente.probarMs", 8000);
        const net::HttpResponse r = net::HttpsClient::request(req);
        QString texto;
        QString nivel = "error";
        if (r.status == 0) {
            texto = tr("No contesta: %1").arg(QString::fromStdString(r.error));
        } else if (r.status == 401 || r.status == 403) {
            texto = tr("El servidor contesta, pero rechaza la clave (%1).").arg(r.status);
        } else if (r.status >= 400) {
            texto = tr("El servidor contestó %1.").arg(r.status);
        } else {
            // Que el modelo que vamos a pedir esté en la lista: un servidor
            // que anda pero no sirve ESE modelo falla recién al primer pedido,
            // y ahí el error no dice que el nombre estaba mal escrito.
            const bool estaEnLaLista = r.body.find("\"" + modelo + "\"") != std::string::npos;
            texto = (estaEnLaLista ? tr("Anda: el servidor contesta y sirve «%1».")
                                   : tr("El servidor contesta y la clave sirve, pero no veo el modelo «%1» en su "
                                        "lista. Revisá el nombre."))
                        .arg(QString::fromStdString(modelo));
            nivel = estaEnLaLista ? "ok" : "aviso";
        }
        QMetaObject::invokeMethod(
            this, [this, texto, nivel]() { emit notify(texto, nivel); }, Qt::QueuedConnection);
    }).detach();
}

QString AgentViewModel::guardarModeloPropio(const QString& url, const QString& modelo, const QString& nombre,
                                            const QString& extraJson, const QString& instrucciones,
                                            bool piensa) {
    if (url.trimmed().isEmpty()) return tr("Falta la dirección del servidor (baseURL).");
    if (modelo.trimmed().isEmpty()) return tr("Falta el nombre del modelo que pide tu servidor.");
    json extra = json::object();
    const std::string texto = extraJson.trimmed().toStdString();
    if (!texto.empty()) {
        // Se valida ACA y no se deja para cuando arranque el agente: un JSON
        // roto escrito en la configuración se descubre recién al arrancar, con
        // un error de opencode que no dice qué campo estaba mal.
        try {
            extra = json::parse(texto);
        } catch (const json::exception& e) {
            return tr("Los parámetros extra no son un JSON válido: %1").arg(QString::fromUtf8(e.what()));
        }
        if (!extra.is_object()) return tr("Los parámetros extra tienen que ser un objeto { ... }.");
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
    if (!mp.guardar(archivo)) return tr("No se pudo guardar la configuración.");
    emit configChanged(); // la lista de modelos cambia con los canales
    return {};
}

QString AgentViewModel::saveKey(const QString& key) {
    const Provider* p = findProvider(provider_);
    if (p == nullptr || QString(p->keyName).isEmpty()) return tr("Este proveedor no usa clave.");
    const QString k = key.trimmed();
    if (k.size() < 20) return tr("La clave parece incompleta.");
    if (!cred::write(QString::fromStdWString((ba::platform::credentialsDir()).wstring()), p->keyName,
                     k.toUtf8()))
        return tr("No se pudo guardar la clave.");
    emit configChanged();
    return {};
}

void AgentViewModel::clearKey() {
    const Provider* p = findProvider(provider_);
    if (p == nullptr || QString(p->keyName).isEmpty()) return;
    cred::remove(QString::fromStdWString((ba::platform::credentialsDir()).wstring()), p->keyName);
    emit configChanged();
}

void AgentViewModel::setAlertsToChat(bool on) {
    alertsToChat_ = on && instancia_ == "automatizacion"; // la charla del evento, nunca
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
    return QString::fromStdWString((ba::platform::dataRoot() / "Agentes" / "versiones.json").wstring());
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
        ba::platform::nombrarHilo("agente-version");
        QString err;
        json nueva = fetchLatest(runtime, base, &err);
        QMetaObject::invokeMethod(this, [this, runtime, nueva, err]() {
            checking_ = false;
            if (!err.isEmpty() || !nueva.is_object()) {
                emit latestChanged();
                setError(tr("No se pudo buscar la actualizacion: %1").arg(err));
                return;
            }
            latest_[runtime] = nueva;
            if (platformPin(nueva).empty()) {
                latest_.erase(runtime);
                emit latestChanged();
                setError(tr("La version nueva no se publico con hash para esta plataforma (%1): no se ofrece.")
                             .arg(platformKey()));
                return;
            }
            emit latestChanged();
            const QString v = QString::fromStdString(nueva.value("version", std::string()));
            if (newerAvailable())
                emit notify(tr("Hay una version nueva: %1.").arg(v), "ok");
            else
                emit notify(tr("Esta al dia (la ultima publicada es %1).").arg(v), "ok");
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
    if (!writeText(chosenFile(), QByteArray::fromStdString(elegidas.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)))) {
        setError(tr("No se pudo guardar %1").arg(chosenFile()));
        return;
    }
    emit configChanged();
    emit latestChanged();
    install();
}

void AgentViewModel::installNewest() {
    if (installing_ || checking_) return;
    const QString runtime = runtimeOf(provider_);
    if (runtime.isEmpty()) return;
    const json base = pinnedPins().value(runtime.toStdString(), json::object());
    const QString nombre = QString::fromStdString(base.value("nombre", runtime.toStdString()));
    // Mientras pregunta, la tarjeta ya esta «instalando» (y se puede cancelar).
    buscandoParaInstalar_ = true;
    installing_ = true;
    installProgress_ = 0;
    installText_ = tr("Buscando la última versión de %1…").arg(nombre);
    emit installChanged();
    if (checkThread_.joinable()) checkThread_.join();
    checkThread_ = std::thread([this, runtime, base, nombre]() {
        ba::platform::nombrarHilo("agente-instalar");
        QString err;
        json nueva = fetchLatest(runtime, base, &err);
        QMetaObject::invokeMethod(this, [this, runtime, nueva, err, nombre]() {
            if (!buscandoParaInstalar_) return; // se cancelo mientras preguntaba
            buscandoParaInstalar_ = false;
            installing_ = false;
            const std::string v = nueva.is_object() ? nueva.value("version", std::string()) : std::string();
            if (!err.isEmpty() || v.empty() || platformPin(nueva).empty()) {
                // Sin red o sin hash para esta plataforma: la que fija el sistema.
                const QString fijada = pinnedVersion();
                emit notify(err.isEmpty() ? tr("No se pudo saber la última versión de %1: se instala la %2.")
                                                .arg(nombre, fijada)
                                          : tr("No se pudo saber la última versión de %1 (%2): se instala la %3.")
                                                .arg(nombre, err, fijada),
                            "warn");
                install();
                return;
            }
            latest_[runtime] = nueva;
            if (QString::fromStdString(v) == current().value("version").toString())
                install(); // la ultima es la que ya fija el sistema
            else
                installLatest();
        });
    });
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
    writeText(chosenFile(), QByteArray::fromStdString(elegidas.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
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
    return QString::fromStdWString((ba::platform::dataRoot() / "Agentes").wstring()) + "/" + runtime + "/" +
           QString::fromStdString(pin.value("version", std::string("?")));
}

QString AgentViewModel::usableDir(const QString& runtime, QString* exeOut) const {
    if (runtime.isEmpty()) return {};
    const json pin = pins().value(runtime.toStdString(), json::object());
    const QString base = QString::fromStdWString((ba::platform::dataRoot() / "Agentes").wstring()) + "/" + runtime;
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
    // sirve igual, la mas nueva por orden de version, PERO SOLO DE LA MISMA VERSION
    // MAYOR que la fijada. Con otra mayor la forma de lanzarlo puede no servir (pasó:
    // opencode 1.x no entiende el --standalone de la 2 y se caía una y otra vez):
    // entonces no hay instalación usable y se pide instalar la fijada.
    const int mayorFijada = QVersionNumber::fromString(fijada).majorVersion();
    QStringList versiones = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(versiones.begin(), versiones.end(), [](const QString& a, const QString& b) {
        return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
    });
    for (auto it = versiones.crbegin(); it != versiones.crend(); ++it) {
        if (!fijada.isEmpty() && QVersionNumber::fromString(*it).majorVersion() != mayorFijada) continue;
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
    // El modelo local: lo que se instala acá es su chat (pi); el modelo se baja en
    // Configuración → Modelos locales.
    const QString dir = usableDir(runtimeOf(provider_));
    if (dir.isEmpty()) return false;
    // pi sin jiti (instalado antes de la guarda) no carga extensiones: a reinstalar.
    if (runtimeOf(provider_) == "pi" && !QFileInfo::exists(dir + "/pi/package/node_modules/jiti/package.json"))
        return false;
    return true;
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

namespace {
/// El Python que trae el programa (como los plugins): ahí están zipfile y tarfile.
std::filesystem::path pythonDelPrograma() {
#if defined(_WIN32)
    return ba::platform::bundleRoot() / "python" / "python.exe";
#else
    return ba::platform::bundleRoot() / "python" / "bin" / "python3";
#endif
}

/// Descomprime .zip, .tgz/.tar.gz y .tar.xz en la carpeta pedida (sin dejar que un
/// nombre del archivo se escape de ella). Los ejecutables quedan ejecutables.
const char* kDescomprimir = R"PY(import os, sys, stat, tarfile, zipfile
src, dst = sys.argv[1], os.path.realpath(sys.argv[2])
def adentro(p):
    r = os.path.realpath(os.path.join(dst, p))
    if r != dst and not r.startswith(dst + os.sep):
        raise SystemExit("un archivo se sale de la carpeta: " + p)
    return r
if zipfile.is_zipfile(src):
    with zipfile.ZipFile(src) as z:
        for i in z.infolist():
            adentro(i.filename)
            z.extract(i, dst)
            modo = i.external_attr >> 16
            if modo & 0o111:
                p = os.path.join(dst, i.filename)
                os.chmod(p, os.stat(p).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
else:
    with tarfile.open(src) as t:
        for m in t.getmembers():
            adentro(m.name)
            if m.islnk() or m.issym():
                adentro(os.path.join(os.path.dirname(m.name), m.linkname))
        try:
            t.extractall(dst, filter="tar")
        except TypeError:
            t.extractall(dst)
)PY";
} // namespace

void AgentViewModel::install() {
    const QString runtime = runtimeOf(provider_);
    const json pin = pins().value(runtime.toStdString(), json::object());
    const json plat = platformPin(pin);
    if (plat.empty()) {
        setError(tr("No hay una version de %1 para esta plataforma (%2).").arg(runtime, platformKey()));
        return;
    }
    struct Item {
        std::string url, sha, kind, dest;
        std::string sha512 = {}; ///< base64, como lo publica npm (si no hay sha256)
    };
    std::vector<Item> items;
    const QString dir = installDir(runtime);
    // Los que son un paquete de npm (Gemini, pi): el Node fijado de la plataforma
    // más el paquete, en <dir>/node y <dir>/<motor>.
    const bool conPaquete = pin.contains("paquete");
    if (conPaquete) {
        items.push_back(
            {plat.value("url", ""), plat.value("sha256", ""), "archivo", (dir + "/node").toStdString()});
        const json pkg = pin.value("paquete", json::object());
        items.push_back({pkg.value("url", ""), pkg.value("sha256", ""), "archivo",
                         (dir + "/" + runtime).toStdString(), pkg.value("sha512", "")});
        // Sus dependencias de npm que el paquete no trae empaquetadas (pi: jiti),
        // en node_modules de su paquete. El .tgz trae una carpeta «package/».
        for (const json& dep : pin.value("dependencias", json::array()))
            items.push_back({dep.value("url", ""), dep.value("sha256", ""), "npm",
                             (dir + "/" + runtime + "/package/node_modules/" +
                              QString::fromStdString(dep.value("nombre", std::string()))).toStdString()});
    } else {
        // SHA-256 (Claude, agentes.json) o SHA-512 en base64 (opencode desde npm, Antigravity).
        items.push_back({plat.value("url", ""), plat.value("sha256", ""), plat.value("tipo", "archivo"),
                         plat.value("tipo", "") == "binario"
                             ? (dir + "/" + QString::fromStdString(plat.value("exe", ""))).toStdString()
                             : dir.toStdString(),
                         plat.value("sha512", "")});
    }
    const QString exeName = QString::fromStdString(plat.value("exe", ""));
    // Gemini y pi corren un script de su paquete de npm: si una version nueva lo
    // movio, que falle la instalacion (y quede la anterior) y no el arranque.
    const QString geminiScript =
        conPaquete ? QString::fromStdString(pin.value("paquete", json::object()).value("script", std::string()))
                   : QString();
    // Se baja y se descomprime en una carpeta aparte y recien al final se pone
    // en su lugar: si la descarga falla (sin red, hash que no coincide, se
    // cancela), la instalacion que YA andaba queda intacta. Antes el camino de
    // error borraba `dir`, que podia ser una instalacion buena.
    const QString staging = QFileInfo(dir).absolutePath() + "/.instalando-" + QFileInfo(dir).fileName();
    // Lo bajado, aparte y hasta que se instala: una descarga cortada sigue donde
    // quedó (la cola de descargas, con Range).
    const QString bajado = QFileInfo(dir).absolutePath() + "/.bajando-" + QFileInfo(dir).fileName();
    const std::string version = pin.value("version", std::string());
    net::Descargas::Pedido p;
    p.id = "agente:" + runtime.toStdString() + "@" + version;
    p.tipo = "agente";
    p.titulo = pin.value("nombre", runtime.toStdString()) + " " + version;
    p.version = version;
    p.yaEsta = [dir]() { return QFileInfo::exists(dir + "/.ok"); };
    QPointer<AgentViewModel> yo(this);
    p.trabajo = [yo, items, dir, staging, bajado, runtime, exeName, geminiScript, version = QString::fromStdString(version)](
                    net::Descargas::Contexto& ctx) -> std::string {
        QString err;
        QDir(staging).removeRecursively(); // restos de un intento anterior
        QDir().mkpath(staging);
        const auto aviso = [&ctx, yo](const QString& t) {
            ctx.avance(0.97, t.toStdString());
            if (yo)
                QMetaObject::invokeMethod(yo.data(), [yo, t]() {
                    if (!yo) return;
                    yo->installText_ = t;
                    emit yo->installChanged();
                });
        };
        const int n = static_cast<int>(items.size());
        for (int i = 0; i < n && err.isEmpty(); ++i) {
            const Item& it = items[static_cast<std::size_t>(i)];
            const QString tmp = bajado + QString("/descarga-%1").arg(i);
            // Los destinos se armaron sobre `dir`: van a la carpeta de staging.
            const auto enStaging = [&dir, &staging](const std::string& p) {
                QString d = QString::fromStdString(p);
                if (d.startsWith(dir)) d.replace(0, dir.size(), staging);
                return d;
            };
            // Ya bajado y verificado en un intento anterior: no se rebaja.
            if (!QFileInfo::exists(tmp)) {
                const std::string e = ctx.bajar(it.url, tmp.toStdString(), 0, 0,
                                                QString("%1 %2 (%3/%4)").arg(runtime, version).arg(i + 1).arg(n).toStdString());
                if (!e.empty()) {
                    err = ctx.cancelado() ? tr("Instalacion cancelada.") : tr("No se pudo descargar: %1").arg(QString::fromStdString(e));
                    break;
                }
            }
            aviso(tr("Verificando…"));
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
                err = tr("La descarga no coincide con el hash publicado. No se instalo nada.");
                QFile::remove(tmp);
                break;
            }
            if (it.kind == "binario") {
                const QString dest = enStaging(it.dest);
                QFile::remove(dest);
                if (!QFile::rename(tmp, dest)) err = tr("No se pudo guardar %1").arg(dest);
                QFile(dest).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                           QFile::ReadGroup | QFile::ExeGroup);
            } else {
                aviso(tr("Descomprimiendo…"));
                const bool npm = it.kind == "npm";
                const QString dest = npm ? enStaging(it.dest) + ".tmp" : enStaging(it.dest);
                QDir().mkpath(dest);
                // Con el Python DEL PROGRAMA (zipfile/tarfile): nada de afuera de la
                // carpeta del sistema (antes, el tar.exe de Windows o el tar del PATH).
                QProcess py;
                py.start(QString::fromStdString(pythonDelPrograma().string()),
                         {"-I", "-B", "-c", kDescomprimir, QDir::toNativeSeparators(tmp), QDir::toNativeSeparators(dest)});
                if (!py.waitForFinished(ba::ajustes::entero("asistente.descomprimirMin", 10) * 60000) || py.exitStatus() != QProcess::NormalExit || py.exitCode() != 0)
                    err = (py.error() == QProcess::FailedToStart
                               ? tr("No se pudo descomprimir: %1 (no arrancó el Python del programa)")
                               : tr("No se pudo descomprimir: %1"))
                              .arg(QString::fromUtf8(py.readAllStandardError()).trimmed().right(400));
                QFile::remove(tmp);
                if (npm && err.isEmpty()) { // <dest>.tmp/package → <dest>
                    const QString final = enStaging(it.dest);
                    QDir(final).removeRecursively();
                    if (!QDir().rename(dest + "/package", final)) err = tr("No se pudo dejar %1").arg(final);
                    QDir(dest).removeRecursively();
                }
            }
        }
        if (err.isEmpty() && !geminiScript.isEmpty() &&
            !QFileInfo::exists(staging + "/" + runtime + "/" + geminiScript))
            err = tr("El paquete de %1 no trae %2: no se instalo (queda la version anterior).").arg(runtime, geminiScript);
        if (err.isEmpty()) {
            // Ejecutable en su lugar (algunos archivos lo traen en una subcarpeta).
            if (geminiScript.isEmpty() && !QFileInfo::exists(staging + "/" + exeName)) {
                const QString found = findFile(staging, exeName);
                if (!found.isEmpty()) QFile::rename(found, staging + "/" + exeName);
            }
            // El `.ok` registra que version quedo y con que ejecutable, para
            // poder seguir usandola si una version nueva del sistema fija otra.
            const json ok{{"version", QFileInfo(dir).fileName().toStdString()},
                          {"exe", exeName.toStdString()},
                          {"fecha", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()}};
            writeText(staging + "/.ok", QByteArray::fromStdString(ok.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
            // Recien ahora se reemplaza lo que hubiera: hasta aca la instalacion
            // anterior siguio entera.
            QDir(dir).removeRecursively();
            if (!QDir().rename(staging, dir)) {
                QDir(staging).removeRecursively();
                err = tr("No se pudo dejar la instalacion en su lugar (%1).").arg(dir);
            }
        }
        if (!err.isEmpty()) {
            QDir(staging).removeRecursively(); // nunca se toca `dir`
        } else {
            QDir(bajado).removeRecursively();
            // Versiones viejas del mismo agente: afuera (liberan cientos de MB).
            QDir parent(QFileInfo(dir).absolutePath());
            for (const QString& v : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (parent.absoluteFilePath(v) != QFileInfo(dir).absoluteFilePath())
                    QDir(parent.absoluteFilePath(v)).removeRecursively();
        }
        return err.toStdString();
    };
    const std::string id = p.id;
    p.alTerminar = [yo, id](bool ok, const std::string& error) {
        if (!yo) return;
        yo->instalando_.erase(id);
        yo->installing_ = !yo->instalando_.empty();
        const QString e = error == "cancelada" ? tr("Instalacion cancelada.") : QString::fromStdString(error);
        yo->installProgress_ = ok ? 1.0 : 0.0;
        yo->installText_ = ok ? tr("Instalado.") : e;
        emit yo->installChanged();
        emit yo->latestChanged(); // newerAvailable mira la version instalada
        emit yo->configChanged();
        if (!ok)
            yo->setError(e);
        else
            emit yo->notify(tr("Agente instalado en la carpeta portable."), "ok");
    };
    const auto r = net::Descargas::la().pedir(std::move(p));
    if (r == net::Descargas::Resultado::Encolada) {
        instalando_.insert(id);
        installing_ = true;
        installProgress_ = 0;
        installText_ = tr("Instalando %1, versión %2…")
                           .arg(QString::fromStdString(pin.value("nombre", "")), QString::fromStdString(version));
        emit installChanged();
    } else if (r == net::Descargas::Resultado::YaEsta) {
        emit notify(tr("Esa versión ya está instalada."), "ok");
    }
}

void AgentViewModel::registrarActualizaciones() {
    // El agente elegido (Claude Code, OpenCode, Gemini): si se publicó una versión
    // nueva con hash para esta plataforma. Actualizar = lo de «Instalar la última».
    QPointer<AgentViewModel> yo(this);
    net::Descargas::la().registrarBuscador("agente", [yo]() {
        std::vector<net::Descargas::Actualizacion> out;
        QString runtime, usada, nombre;
        json base;
        const auto enInterfaz = [&yo](auto f) {
            if (yo) QMetaObject::invokeMethod(yo.data(), f, Qt::BlockingQueuedConnection);
        };
        enInterfaz([&]() {
            if (!yo) return;
            runtime = yo->runtimeOf(yo->provider_);
            if (runtime.isEmpty() || !yo->installed()) return;
            base = yo->pinnedPins().value(runtime.toStdString(), json::object());
            usada = yo->installedVersion();
            nombre = QString::fromStdString(base.value("nombre", runtime.toStdString()));
        });
        if (base.empty() || usada.isEmpty()) return out;
        QString err;
        const json nueva = fetchLatest(runtime, base, &err);
        const std::string v = nueva.is_object() ? nueva.value("version", std::string()) : std::string();
        if (v.empty() || QString::fromStdString(v) == usada) return out;
        bool hayPin = false;
        enInterfaz([&]() { hayPin = yo && !yo->platformPin(nueva).empty(); });
        if (!hayPin) return out;
        net::Descargas::Actualizacion a;
        a.actual = usada.toStdString();
        a.detalle = tr("Hay una versión nueva de %1").arg(nombre).toStdString();
        a.pedido.id = "agente:" + runtime.toStdString() + "@" + v;
        a.pedido.tipo = "agente";
        a.pedido.titulo = nombre.toStdString() + " " + v;
        a.pedido.version = v;
        a.aplicar = [yo, runtime, nueva]() {
            if (!yo || yo->runtimeOf(yo->provider_) != runtime) return;
            yo->latest_[runtime] = nueva;
            yo->installLatest();
        };
        out.push_back(std::move(a));
        return out;
    });
}

void AgentViewModel::cancelInstall() {
    if (buscandoParaInstalar_) { // todavia preguntando cual es la ultima
        buscandoParaInstalar_ = false;
        installing_ = !instalando_.empty();
        installText_ = tr("Instalacion cancelada.");
        emit installChanged();
    }
    for (const std::string& id : std::set<std::string>(instalando_)) net::Descargas::la().cancelar(QString::fromStdString(id));
}

void AgentViewModel::seguirInstalacion() {
    // El avance de la instalación (la cola de descargas lo lleva) para la tarjeta del Asistente.
    for (const std::string& id : instalando_) {
        double f = 0;
        std::string t;
        if (net::Descargas::la().avanceDe(id, &f, &t)) {
            if (f != installProgress_ || QString::fromStdString(t) != installText_) {
                installProgress_ = f;
                installText_ = QString::fromStdString(t);
                emit installChanged();
            }
            return;
        }
    }
}

// --- Lanzar ---------------------------------------------------------------------------

QString AgentViewModel::bridgePath() const {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/ba-mcp.exe";
#else
    return QCoreApplication::applicationDirPath() + "/ba-mcp";
#endif
}

QString AgentViewModel::controlPath() const {
#ifdef _WIN32
    return QCoreApplication::applicationDirPath() + "/ba-control.exe";
#else
    return QCoreApplication::applicationDirPath() + "/ba-control";
#endif
}

QVariantMap AgentViewModel::controlSistema() const {
    const ControlSistema c = ControlSistema::leer(ba::platform::dataRoot());
    QVariantMap m = QJsonDocument::fromJson(QByteArray::fromStdString(c.comoJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
    QVariantList caps;
    for (const auto& [k, etiqueta] : ControlSistema::capacidades())
        caps << QVariantMap{{"clave", QString::fromStdString(k)}, {"etiqueta", QCoreApplication::translate("controlsistema", etiqueta.c_str())}};
    m["capacidades"] = caps;
    // En qué sistema está y si su motor ya está hecho (PLAN-CONTROL-SISTEMA: Windows
    // primero; macOS y Linux en camino). Las herramientas son las mismas en los tres.
#if defined(_WIN32)
    m["sistema"] = "Windows";
    m["soportado"] = true;
#elif defined(__APPLE__)
    m["sistema"] = "macOS";
    m["soportado"] = true;
#else
    m["sistema"] = "Linux";
    m["soportado"] = false;
#endif
    return m;
}

QString AgentViewModel::setControlSistema(const QVariantMap& cambios) {
    ControlSistema c = ControlSistema::leer(ba::platform::dataRoot());
    c.aplicar(json::parse(QJsonDocument(QJsonObject::fromVariantMap(cambios)).toJson().toStdString(), nullptr, false));
    if (!c.guardar(ba::platform::dataRoot())) return tr("No se pudo guardar la configuración.");
    return {};
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
    // El modelo local (el chat con pi): los .gguf del catálogo (los del aparato
    // y los de chat, como OxCoder 9B), en cualquier plataforma: llama-server los
    // carga todos (la Mac también). Vacío = el del modelo local activo. Los MLX
    // todavía no: hace falta un servidor de MLX que hable el protocolo de OpenAI.
    // Solo los INSTALADOS (la regla es de ModelosLocales): los demás se bajan
    // desde Modelos locales.
    if (proveedor == "local") {
        const auto instalado = [](const std::string& repo) {
            return ModelosLocales::instaladoEn(ba::platform::dataRoot() / "Modelos", repo);
        };
        QStringList out;
        QFile f(":/agent/modelos-catalogo.json");
        if (f.open(QIODevice::ReadOnly)) {
            const json c = json::parse(f.readAll().toStdString(), nullptr, false);
            if (c.is_object())
                for (const json& m : c.value("modelos", json::array())) {
                    const std::string tipo = m.value("tipo", std::string("reactivo"));
                    const std::string repo = m.value("repo", std::string());
                    if (m.value("formato", std::string()) == "gguf" && (tipo == "reactivo" || tipo == "chat") &&
                        instalado(repo))
                        out << QString::fromStdString(repo);
                }
        }
        return out;
    }
    const Provider* p = findProvider(proveedor);
    return p == nullptr ? QStringList() : p->models;
}

QString AgentViewModel::prepareWorkspace(QString* error) {
    const QString root = QString::fromStdWString(ba::platform::dataRoot().wstring()); // de la máquina
    const QString prompt = systemPrompt_ ? systemPrompt_() : QString();
    // Con su juego de herramientas (ver Roles::juegoDelAsistente) y CON
    // consola: es el asistente del operador, no un agente de un tiro.
    // El control del sistema solo para el asistente del operador (los roles
    // automáticos corren solos, sin nadie que apruebe cada paso).
    // El de automatización: su carpeta, y lo que es (atiende avisos; no la charla del evento).
    if (instancia_ == "automatizacion")
        return prepareWorkspaceEn(
            root + "/Runtime/agente-automatizacion", provider_, model_, {"--rol", "asistente"},
            prompt + "\n\n" + QString::fromStdString(ba::i18n::leer("prompts/automatizacion.md")),
            error, true, true);
    return prepareWorkspaceEn(root + "/Runtime/agente", provider_, model_, {"--rol", "asistente"}, prompt,
                              error, true, true);
}

namespace {
/// Los MCP de WordCraft, DeckCraft y GridCraft: el asistente arma documentos de Word,
/// presentaciones y planillas con gráficos. WordCraft y DeckCraft, encerrados en `carpeta`
/// (la de la configuración: los eventos, sus medios y su documentación); GridCraft no tiene
/// cómo encerrarse. Sus carpetas de usuario, en <datos>/Runtime/craft (portable).
/// {nombre: {command, args, env}}, solo los que viajan.
json mcpOficina(const QString& carpeta, const QString& datosMaquina) {
    json out = json::object();
    const std::string casa = (datosMaquina + "/Runtime/craft").toStdString();
    QDir().mkpath(QString::fromStdString(casa));
    const json env{{"HOME", casa}, {"USERPROFILE", casa}, {"APPDATA", casa}, {"LOCALAPPDATA", casa},
                   {"XDG_CONFIG_HOME", casa}, {"XDG_DATA_HOME", casa}, {"XDG_CACHE_HOME", casa}};
    for (const char* app : {"wordcraft", "deckcraft", "gridcraft"}) {
        const QString bin = ba::carrusel::Conversor::herramienta(app);
        if (bin.isEmpty()) continue;
        json args = json::array({"mcp"});
        if (std::string(app) != "gridcraft")
            for (const char* f : {"--automation-read-root", "--automation-write-root"}) {
                args.push_back(f);
                args.push_back(carpeta.toStdString());
            }
        out[app] = json{{"command", bin.toStdString()}, {"args", args}, {"env", env}};
    }
    return out;
}

/// Lo que el asistente tiene que saber de esos MCP (va al final de su prompt): un archivo
/// del idioma del operador, prompts/oficina.md.
QByteArray oficinaPrompt() { return QByteArray::fromStdString("\n\n" + ba::i18n::leer("prompts/oficina.md")); }
} // namespace

QString AgentViewModel::prepareWorkspaceEn(const QString& ws, const QString& proveedor, const QString& modelo,
                                           const QStringList& bridgeArgs, const QString& prompt,
                                           QString* error, bool consola, bool conControl) {
    const QString root = QString::fromStdWString(ba::platform::dataRoot().wstring()); // de la máquina
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
    // Los MCP de oficina, para el asistente del operador y el de automatización (los que
    // manejan el evento); un rol de un tiro no los necesita.
    const json oficina = conControl ? mcpOficina(QString::fromStdU16String(rt_.dataRoot().u16string()), root) : json::object();
    if (!oficina.empty()) p += oficinaPrompt();
    for (const char* f : {"CLAUDE.md", "GEMINI.md", "AGENTS.md"})
        if (!writeText(ws + "/" + f, p)) {
            if (error != nullptr) *error = tr("No se pudo escribir %1").arg(ws);
            return {};
        }
    const std::string bridge = bridgePath().toStdString();
    json args = json::array();
    for (const QString& a : bridgeArgs)
        args.push_back(a.toStdString());
    const json mcpStdio{{"command", bridge}, {"args", args}, {"env", {{"BA_DATA_ROOT", root.toStdString()}}}};
    // El control del escritorio (ba-control): solo si está prendido en
    // Configuración y es el asistente del operador. Con `preguntar`, el cliente
    // le pide permiso al operador antes de cada acción (no va en las permitidas).
    const ControlSistema ctl = ControlSistema::leer(ba::platform::dataRoot());
    const bool control = conControl && ctl.habilitado && QFile::exists(controlPath());
    const json mcpControl{{"command", controlPath().toStdString()},
                          {"args", json::array()},
                          {"env", {{"BA_DATA_ROOT", root.toStdString()}}}};
    const QString runtime = runtimeOf(proveedor);
    const Provider* prov = findProvider(proveedor);
    if (runtime == "claude") {
        json m = mcpStdio;
        m["type"] = "stdio";
        json servidores{{"broadcast", m}};
        if (control) {
            json c = mcpControl;
            c["type"] = "stdio";
            servidores["control"] = c;
        }
        for (const auto& [nombre, m] : oficina.items()) {
            servidores[nombre] = m;
            servidores[nombre]["type"] = "stdio";
        }
        writeText(ws + "/mcp.json", QByteArray::fromStdString(json{{"mcpServers", servidores}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
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
        if (control && !ctl.preguntar) {
            // opencode no le pregunta al operador antes de una herramienta MCP: el
            // control del escritorio va solo si en Configuración apagaron «preguntar».
            json ccmd = json::array({controlPath().toStdString()});
            cfg["mcp"]["control"] = {{"type", "local"},
                                     {"command", ccmd},
                                     {"environment", {{"BA_DATA_ROOT", root.toStdString()}}},
                                     {"enabled", true}};
        }
        for (const auto& [nombre, m] : oficina.items()) {
            json ocmd = json::array({m["command"]});
            for (const auto& a : m["args"]) ocmd.push_back(a);
            cfg["mcp"][nombre] = {{"type", "local"}, {"command", ocmd}, {"environment", m["env"]}, {"enabled", true}};
        }
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
        writeText(ws + "/opencode.json", QByteArray::fromStdString(cfg.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
    } else if (runtime == "pi") {
        // pi lee los servidores MCP de <PI_CODING_AGENT_DIR>/mcp.json (su perfil
        // portable), con el mismo formato que Claude.
        json servidores{{"broadcast", mcpStdio}};
        servidores["broadcast"]["description"] = "Maneja el aparato de emisión (escenas, audio, salidas, grabación…).";
        // Este chat es para CONVERSAR y preguntar cómo se hace algo (el asistente
        // rápido, el de la voz, ya maneja las ~80 de todos los días): directas,
        // solo la ayuda y lo que mira sin tocar; las más de 400 restantes, con
        // tool_search. Poco contexto y nada que lo tiente a actuar solo.
        servidores["broadcast"]["exposure"] = "deferred";
        json directas = json::object();
        for (const char* n : {"help", "help_search", "status", "scene_list", "escena_fuentes_ver", "mixer_list",
                              "input_status", "audio_devices", "video_devices", "stream_salud", "record_disco",
                              "room_status", "remotos_fila_ver", "alert_list", "logs_tail", "system_info"})
            directas[n] = "direct";
        servidores["broadcast"]["toolExposure"] = directas;
        if (control) servidores["control"] = mcpControl;
        for (const auto& [nombre, m] : oficina.items()) {
            servidores[nombre] = m;
            servidores[nombre]["exposure"] = "deferred"; // con tool_search, como las del aparato
        }
        writeText(root + "/Credenciales/agentes/pi/mcp.json",
                  QByteArray::fromStdString(json{{"mcpServers", servidores}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
    } else if (runtime == "antigravity") {
        // Antigravity lee los MCP de <HOME>/.gemini/config/mcp_config.json (el formato de
        // Claude) y sus ajustes de <HOME>/.gemini/antigravity-cli/settings.json; HOME es su
        // perfil portable.
        const QString home = root + "/Credenciales/agentes/antigravity";
        json servidores{{"broadcast", mcpStdio}};
        servidores["broadcast"]["disabled"] = false;
        if (control) {
            servidores["control"] = mcpControl;
            servidores["control"]["disabled"] = false;
        }
        for (const auto& [nombre, m] : oficina.items()) {
            servidores[nombre] = m;
            servidores[nombre]["disabled"] = false;
        }
        writeText(home + "/.gemini/config/mcp_config.json",
                  QByteArray::fromStdString(json{{"mcpServers", servidores}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
        // Con clave de API: modelProvider «gemini» (y GEMINI_API_KEY en el entorno); con la
        // cuenta, el login de Google. Sin pisar lo demás que ya tenga.
        const QString path = home + "/.gemini/antigravity-cli/settings.json";
        json st = json::object();
        {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) st = json::parse(f.readAll().toStdString(), nullptr, false);
            if (!st.is_object()) st = json::object();
        }
        if (prov != nullptr && QString(prov->auth) == "api")
            st["modelProvider"] = "gemini";
        else
            st.erase("modelProvider");
        writeText(path, QByteArray::fromStdString(st.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
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
    const QString root = QString::fromStdWString(ba::platform::dataRoot().wstring()); // de la máquina
    // Las credenciales son POR MOTOR, no por rol: un solo login sirve para el
    // interactivo y para todos los roles que usen ese motor.
    const QString profile = root + "/Credenciales/agentes/" + runtime;
    QDir().mkpath(profile);
    o.workDir = ws;
    o.env["BA_DATA_ROOT"] = root;
    o.env["TERM"] = "xterm-256color";
    // PYTHONHOME/PYTHONPATH son los del Python de los plugins: heredados, rompen
    // cualquier python que corra el agente en su consola.
    o.unsetEnv = {"ANTHROPIC_API_KEY",  "ANTHROPIC_AUTH_TOKEN", "GEMINI_API_KEY",  "GOOGLE_API_KEY",
                  "OPENROUTER_API_KEY", "CLAUDECODE",           "LD_LIBRARY_PATH", "BA_MODELO_PROPIO_KEY",
                  "PYTHONHOME",         "PYTHONPATH"};
    // Lo heredado de OTRO Claude Code (si la app se abrió desde su terminal): con
    // CLAUDE_CODE_CHILD_SESSION el asistente se cree una sesión hija y no guarda el
    // historial («Transcript saving is off»), y con su SESSION_ID/SOCKET habla con aquel.
    for (const QString& k : QProcessEnvironment::systemEnvironment().keys())
        if (k.startsWith("CLAUDE_") || k == "AI_AGENT") o.unsetEnv << k;
    if (prov != nullptr && !QString(prov->keyName).isEmpty()) {
        QByteArray key;
        if (!cred::read(root + "/Credenciales", prov->keyName, key)) {
            if (error != nullptr) *error = tr("Falta la clave de API (pegala en el panel de la derecha).");
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
    } else if (runtime == "antigravity") {
        o.program =
            dir + "/" +
            (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "antigravity")) : exeInstalado);
        if (!modelo.isEmpty()) o.args << "--model" << modelo;
        // La versión es la fijada y verificada: que no se reemplace sola.
        o.env["AGY_CLI_DISABLE_AUTO_UPDATE"] = "1";
        // EL LLAVERO, NUNCA (los tres sistemas): con una «sesión SSH» Antigravity guarda el
        // token de Google en un archivo de su HOME (el perfil portable) y ni mira el llavero
        // ni el Administrador de credenciales de Windows, que no se aíslan con variables.
        // Probado en la PC de Windows (2026-10-19): «Using file-based token storage because
        // SSH session detected»; sin esto leía la sesión que había en el Administrador.
        o.env["SSH_CONNECTION"] = "127.0.0.1 0 127.0.0.1 0";
        o.env["SSH_CLIENT"] = "127.0.0.1 0 0";
#ifdef __APPLE__
        // Y además, en la Mac: lo demás que guarda (los secretos de sus plugins) también va
        // por go-keyring, que llama a /usr/bin/security por su ruta (el `security` propio del
        // PATH no sirve). Dentro de un sandbox que no lo deja ejecutar, todo cae al archivo
        // del perfil. Probado (2026-10-19): «OS credential manager unavailable … falling
        // back to file-based secret storage».
        {
            const QString sb = profile + "/sin-llavero.sb";
            // Solo eso: el resto del sistema (red, certificados, terminal) queda como siempre.
            writeText(sb, "(version 1)\n(allow default)\n(deny process-exec (literal \"/usr/bin/security\"))\n");
            o.args.prepend(o.program);
            o.args.prepend(sb);
            o.args.prepend("-f");
            o.program = "/usr/bin/sandbox-exec";
        }
#elif !defined(_WIN32)
        // Linux: el llavero es el Secret Service por D-Bus; sin el bus, al archivo.
        o.unsetEnv << "DBUS_SESSION_BUS_ADDRESS";
#endif
    } else if (runtime == "pi") {
        // pi (el chat del modelo local) con el Node fijado; le habla al llama-server
        // que levantó start() (ver arrancarServidorLocal). Todo en su perfil portable.
        o.program = dir + "/node/" +
                    (exeInstalado.isEmpty() ? QString::fromStdString(plat.value("exe", "")) : exeInstalado);
        // El servidor es de un solo modelo (alias «local»): para pi es un endpoint
        // compatible con OpenAI, en su models.json (el puerto cambia en cada arranque).
        const json modelos{{"providers",
                            {{"modelo-local",
                              {{"baseUrl", "http://127.0.0.1:" + std::to_string(puertoServidor_) + "/v1"},
                               {"api", "openai-completions"},
                               {"apiKey", "local"},
                               // Lo que recomienda Qwen sin razonar, y presence_penalty para que
                               // no repita la misma llamada en un bucle (pasaba con tool_search).
                               {"models", json::array({{{"id", "local"},
                                                        {"name", "Modelo local"},
                                                        {"contextWindow", rt_.config().modeloContexto},
                                                        {"maxTokens", ba::ajustes::entero("modelo.maxTokens", 4096)},
                                                        {"samplingParams",
                                                         {{"temperature", ba::ajustes::numero("modelo.temperatura", 0.7)}, {"top_p", 0.8}, {"top_k", 20},
                                                          {"presence_penalty", ba::ajustes::numero("modelo.penalidadRepeticion", 1.5)}}}}})}}}}}};
        writeText(profile + "/models.json",
                  QByteArray::fromStdString(modelos.dump(2, ' ', false, nlohmann::json::error_handler_t::replace)));
        // El prompt propio (el de pi es para programar) y SOLO el programa: la
        // extensión de MCP (sin ella pi no habla MCP), nada de consola ni archivos
        // (las suyas: read, bash, edit, write; un modelo chico con consola instaló
        // paquetes del sistema), y del MCP solo lo nuestro más tool_search.
        // El prompt es un archivo del idioma del operador (prompts/modelo-local.md).
        const QString prompt = QString::fromStdString(ba::i18n::leer("prompts/modelo-local.md"));
        // La guarda (agent/pi-guarda.mjs): confirma cada cambio en la terminal, corta
        // las llamadas repetidas y los bucles. Se deja en el perfil en cada arranque.
        const QString guarda = profile + "/guarda.mjs";
        {
            QFile f(":/agent/pi-guarda.mjs");
            if (f.open(QIODevice::ReadOnly)) writeText(guarda, f.readAll());
        }
        o.args = {dir + "/pi/" + QString::fromStdString(pin.value("paquete", json::object()).value("script", "")),
                  "--provider", "modelo-local", "--model", "local", "--offline", "--approve",
                  "--no-extensions", "-e", "builtin:mcp", "-e", guarda, "--no-context-files", "--no-skills",
                  "--no-prompt-templates", "--tools",
                  "tool_search,mcp__broadcast__*,mcp__control__*,mcp__wordcraft__*,mcp__deckcraft__*,mcp__gridcraft__*",
                  "--system-prompt", prompt};
        o.env["PI_CODING_AGENT_DIR"] = profile;
        o.env["HOME"] = profile;
        o.env["USERPROFILE"] = profile;
        o.env["PI_OFFLINE"] = "1";
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
        // OpenCode 2 arranca un servicio de fondo que queda corriendo; con --standalone usa
        // un servidor propio que muere con él (nada del agente queda andando en el sistema).
        o.args << "--standalone";
    } else if (error != nullptr) {
        *error = tr("Proveedor desconocido: %1").arg(proveedor);
    }
    if (!o.program.isEmpty()) aislarEnLaCarpetaDeDatos(o, runtime, profile, root);
    return o;
}

// NADA DEL AGENTE QUEDA EN EL SISTEMA: ni su login ni sus cachés ni sus registros.
// Antes Claude Code guardaba su sesión en el llavero de macOS («Claude Code-
// credentials-<hash>») y sus registros en ~/Library/Caches/claude-cli-nodejs; Gemini CLI
// usa el llavero si lo encuentra. Con HOME en el perfil (dentro de Credenciales/) no
// encuentran el llavero del usuario (`security` lo busca desde $HOME) y guardan todo en
// archivos del perfil; Gemini, además, por su variable. XDG y AppData para Linux y
// Windows. Probado con `claude auth status` (2026-10-12): sin llavero lee y guarda la
// sesión en <CLAUDE_CONFIG_DIR>/.credentials.json.
void AgentViewModel::aislarEnLaCarpetaDeDatos(term::Pty::Options& o, const QString& runtime, const QString& profile,
                                              const QString& root) {
    const auto poner = [&o](const QString& k, const QString& v) {
        if (!o.env.contains(k)) o.env[k] = v;
    };
    o.env["HOME"] = profile;
    o.env["USERPROFILE"] = profile;
    poner("XDG_CONFIG_HOME", profile + "/config");
    poner("XDG_DATA_HOME", profile + "/data");
    poner("XDG_STATE_HOME", profile + "/state");
    poner("XDG_CACHE_HOME", root + "/Cache/agentes/" + runtime);
#ifdef _WIN32
    o.env["APPDATA"] = profile + "/AppData/Roaming";
    o.env["LOCALAPPDATA"] = profile + "/AppData/Local";
    QDir().mkpath(profile + "/AppData/Roaming");
    QDir().mkpath(profile + "/AppData/Local");
#endif
    o.env["NPM_CONFIG_CACHE"] = root + "/Cache/npm";
    // LOS TEMPORALES, TAMBIÉN EN LOS DATOS (el pendrive): Claude Code usa /tmp salvo
    // CLAUDE_CODE_TMPDIR (salidas de tareas, PDF, vistas previas), y lo que corre en su
    // consola, TMPDIR (Unix) o TMP/TEMP (Windows). Los sockets del sistema siguen en
    // runtimeDir(), que no puede ir a un FAT/exFAT.
    {
        const QString tmp = root + "/Runtime/tmp/" + runtime;
        QDir().mkpath(tmp);
        o.env["CLAUDE_CODE_TMPDIR"] = tmp;
        o.env["TMPDIR"] = tmp;
        o.env["TMP"] = tmp;
        o.env["TEMP"] = tmp;
    }
#ifdef __APPLE__
    // EL LLAVERO DE macOS, NUNCA. Claude Code guarda el login con `security` y solo si
    // falla pasa a <perfil>/.credentials.json. Con el HOME del perfil no hay llavero
    // por defecto y `security` no falla: abre un cartel («no se encuentra un llavero
    // para guardar…») y espera; Claude lo toma como llavero bloqueado, no pasa al
    // archivo y dice «Couldn't save your login». Un `security` propio adelante en el
    // PATH contesta al instante «no está» (leer, borrar) y «no se pudo» (guardar): el
    // login queda en el archivo del perfil, en la carpeta de datos (el pendrive).
    {
        const QString bin = profile + "/bin";
        QDir().mkpath(bin);
        const QString sec = bin + "/security";
        const QByteArray guion = "#!/bin/sh\n"
                                 "# Broadcast Appliance: los agentes no usan el llavero de macOS (todo queda en la\n"
                                 "# carpeta de datos). Ver AgentViewModel::aislarEnLaCarpetaDeDatos.\n"
                                 "case \"$1\" in\n"
                                 "  add-generic-password|add-internet-password) exit 1 ;;\n"
                                 "  *) exit 44 ;;\n"
                                 "esac\n";
        QFile f(sec);
        if (!f.exists() || (f.open(QIODevice::ReadOnly) && f.readAll() != guion)) {
            f.close();
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(guion);
        }
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                         QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
        const QString path = o.env.contains("PATH") ? o.env["PATH"] : qEnvironmentVariable("PATH");
        o.env["PATH"] = bin + (path.isEmpty() ? QString() : ":" + path);
    }
#endif
}

term::Pty::Options AgentViewModel::launchOptions(QString* error) {
    const QString ws = prepareWorkspace(error);
    if (ws.isEmpty()) return {};
    term::Pty::Options o = baseLaunch(provider_, model_, ws, error);
    if (o.program.isEmpty()) return o;
    // Su ba-mcp lo hereda y lo dice en cada pedido (X-BA-Asistente): ver OperatorMessages::setQuienPide.
    o.env["BA_ASISTENTE"] = instancia_;
    if (runtimeOf(provider_) == "claude") {
        // El MCP y la consola del sistema: se le pide arreglar cosas de la
        // maquina, no solo manejar la emision. En Windows la consola es la
        // herramienta PowerShell; Bash es la de Linux (y la de Git Bash).
        // El limite lo pone el prompt del sistema: con el aire encendido, todo
        // lo que pueda afectarlo se consulta antes (ver McpHelp.cpp).
        // Write y Edit también (pedido del dueño): el asistente arregla archivos
        // de configuración y scripts de la máquina directamente.
        // EN VIVO, sin consola ni archivos: un comando del sistema no pasa por las guardas
        // de aire del MCP (ver McpAire). Al terminar el aire se relanza con ella.
        conConsola_ = !(hayAire_ && hayAire_());
        // El control del escritorio (mcp__control): permitido de antemano solo si
        // en Configuración apagaron «preguntar»; si no, Claude le pide permiso al
        // operador en su consola antes de cada acción.
        const ControlSistema ctl = ControlSistema::leer(ba::platform::dataRoot());
        // Los de oficina (documentos, presentaciones, planillas), permitidos: trabajan en la
        // carpeta de la configuración y no tocan el aire.
        QStringList permitidas{"mcp__broadcast", "mcp__wordcraft", "mcp__deckcraft", "mcp__gridcraft"};
        if (ctl.habilitado && !ctl.preguntar) permitidas << "mcp__control";
        // Fuera del aire: consola (PowerShell, Bash) y archivos (Write, Edit).
        if (conConsola_)
            o.args << "--allowedTools" << permitidas << "Bash" << "PowerShell" << "Write" << "Edit"
                   << "--disallowedTools" << "NotebookEdit";
        else
            o.args << "--allowedTools" << permitidas << "--disallowedTools" << "Bash" << "PowerShell"
                   << "Edit" << "Write" << "NotebookEdit";
        // La conversación con su id: retomar la pedida (reinicio de la app, relanzamiento
        // por el aire) o empezar una nueva con un id propio, para poder retomarla después.
        if (!reanudar_.isEmpty()) {
            sesion_ = reanudar_;
            o.args << "--resume" << sesion_;
            if (!seguir_.isEmpty()) o.args << seguir_; // el primer mensaje (claude [opciones] [prompt])
        } else {
            sesion_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
            o.args << "--session-id" << sesion_;
        }
        reanudar_.clear();
        seguir_.clear();
    } else if (runtimeOf(provider_) == "antigravity") {
        // Retomar la pedida (reinicio de la app). El id de una nueva lo pone Antigravity:
        // sesionParaReanudar lo busca en su historial.
        inicioMs_ = QDateTime::currentMSecsSinceEpoch();
        sesion_ = reanudar_;
        if (!sesion_.isEmpty()) {
            o.args << "--conversation" << sesion_;
            if (!seguir_.isEmpty()) o.args << "--prompt-interactive" << seguir_;
        }
        reanudar_.clear();
        seguir_.clear();
    } else if (runtimeOf(provider_) == "pi") {
        conConsola_ = false; // pi con el modelo local: nunca consola (ver baseLaunch)
    } else if (runtimeOf(provider_) == "opencode" && !urlOpencode_.isEmpty()) {
        // Al servidor compartido (ver start y ServidorOpencode), no a uno propio.
        o.args.removeAll("--standalone");
        o.args << "--server" << urlOpencode_;
        o.env["OPENCODE_SERVER_PASSWORD"] = claveOpencode_;
    }
    return o;
}

Motores AgentViewModel::motores() {
    Motores m;
    m.local = [this](const Lanzamiento& l, const std::string& origen, std::string* resumen) {
        const QString proveedor = l.proveedor.isEmpty() ? provider_ : l.proveedor;
        if (proveedor != "local") return false; // encargos y disparadores: el camino de la voz
        if (!local_) {
            *resumen = tr("el modelo local no está disponible en esta versión").toStdString();
            return true;
        }
        local_(l.rol, l.encargo, origen.rfind("disparador", 0) == 0);
        *resumen = tr("Lo atendió el modelo local: lo dijo por los auriculares y, si había algo para hacer, "
                      "lo propuso en la barra del asistente.")
                       .toStdString();
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
    if (runtime == "antigravity") {
        // Antigravity tiene modo no interactivo (-p, --output-format stream-json), pero no
        // está probado con el MCP ni con su salida: hasta entonces, los roles no lo usan.
        if (error != nullptr) *error = tr("Los roles automaticos todavia no pueden usar Gemini (Antigravity).");
        return {};
    }
    if (runtime.isEmpty()) {
        if (error != nullptr) *error = tr("Proveedor desconocido: %1").arg(proveedor);
        return {};
    }
    const QString root = QString::fromStdWString(ba::platform::dataRoot().wstring()); // de la máquina
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
        o.args = {"run", "--standalone", "--format", "json"};
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
    if (!installed()) {
        setError(tr("Primero instala el agente (boton Instalar)."));
        return;
    }
    if (!mcpReady()) {
        setError(
            tr("El servidor MCP esta apagado: activalo en Configuracion -> Asistente (MCP) y reinicia la app."));
        return;
    }
    // El chat del modelo local: primero su llama-server (cargar el modelo tarda);
    // pi arranca cuando el servidor contesta.
    if (runtimeOf(provider_) == "pi") {
        arrancarServidorLocal([this]() { lanzarSesion(); });
        return;
    }
    // OpenCode: UN servidor para las dos pestañas (ver ServidorOpencode); la interfaz
    // se le conecta cuando contesta.
    if (runtimeOf(provider_) == "opencode") {
        QString err;
        const QString ws = prepareWorkspace(&err);
        const term::Pty::Options base = ws.isEmpty() ? term::Pty::Options{} : baseLaunch(provider_, model_, ws, &err);
        if (base.program.isEmpty()) {
            setError(err.isEmpty() ? tr("No se pudo preparar el agente.") : err);
            return;
        }
        QPointer<AgentViewModel> yo(this);
        ServidorOpencode::el().pedir(this, {base.program, base.env, base.unsetEnv, ws},
                                     [yo](const QString& url, const QString& clave, const QString& error) {
                                         if (!yo) return;
                                         if (!error.isEmpty()) {
                                             yo->setError(error);
                                             return;
                                         }
                                         yo->urlOpencode_ = url;
                                         yo->claveOpencode_ = clave;
                                         yo->lanzarSesion();
                                     });
        return;
    }
    lanzarSesion();
}

// --- El servidor del modelo local (para pi) ---------------------------------------------

QString AgentViewModel::rutaServidorLocal() const { return ServidorModeloLocal::ejecutable(); }

void AgentViewModel::arrancarServidorLocal(std::function<void()> listo) {
    QString falta, repoParaBajar;
    const QString gguf = ggufChat_ ? ggufChat_(model_, &falta, &repoParaBajar) : QString();
    ggufActual_ = gguf;
    if (gguf.isEmpty()) {
        setError(falta.isEmpty() ? tr("No hay un modelo local para el chat.") : falta);
        if (!repoParaBajar.isEmpty() && bajarModelo_) bajarModelo_(repoParaBajar);
        return;
    }
    // UN llama-server para los dos asistentes (evento y automatización): si el otro ya
    // lo tiene cargado con este modelo, se usa el mismo (ver ServidorModeloLocal).
    ServidorModeloLocal::Pedido pedido;
    pedido.ejecutable = rutaServidorLocal();
    pedido.gguf = gguf;
    pedido.capasGpu = rt_.config().modeloCapasGpu;
    pedido.contexto = rt_.config().modeloContexto; // Configuración → Modelos locales
    pedido.logs = QString::fromStdWString((ba::platform::dataRoot() / "Logs").wstring());
    pedido.cargaS = ba::ajustes::entero("modelo.cargaS", 180);
    QPointer<AgentViewModel> yo(this);
    ServidorModeloLocal::el().pedir(
        this, pedido,
        [yo, listo](quint16 puerto, const QString& error) {
            if (!yo) return;
            if (!error.isEmpty()) {
                yo->setError(error);
                return;
            }
            yo->puertoServidor_ = puerto;
            yo->installText_ = tr("Modelo local listo.");
            emit yo->installChanged();
            listo();
        },
        [yo](const QString& t) {
            if (!yo) return;
            yo->installText_ = t;
            emit yo->installChanged();
        });
}

void AgentViewModel::pararServidorLocal() {
    // Los servidores compartidos que este asistente estuviera usando (el modelo local,
    // OpenCode): se sueltan; el último que suelta los apaga.
    ServidorModeloLocal::el().soltar(this);
    ServidorOpencode::el().soltar(this);
    urlOpencode_.clear();
    claveOpencode_.clear();
}

void AgentViewModel::lanzarSesion() {
    QString err;
    term::Pty::Options o = launchOptions(&err);
    if (!err.isEmpty() || o.program.isEmpty()) {
        setError(err.isEmpty() ? tr("No se pudo preparar el agente.") : err);
        pararServidorLocal();
        return;
    }
    if (!session_->start(o, &err)) {
        setError(tr("No se pudo iniciar el agente: %1").arg(err));
        pararServidorLocal();
    }
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
    pararServidorLocal();
    emit runningChanged();
}

void AgentViewModel::restart() {
    // Relanzar (el aire prende o apaga la consola) sigue la misma conversación.
    const QString seguir = sesionParaReanudar();
    stop();
    reanudar_ = seguir;
    QTimer::singleShot(300, this, &AgentViewModel::start);
}

QString AgentViewModel::sesionParaReanudar() const {
    if (!session_->running()) return {};
    const QString root = QString::fromStdWString(ba::platform::dataRoot().wstring());
    if (runtimeOf(provider_) == "antigravity") {
        // La última conversación escrita desde esta carpeta de trabajo (cada asistente tiene
        // la suya) desde que arrancó: cada pedido queda en su historial con workspace y
        // conversationId. Si todavía no escribió nada, la que se retomó al arrancar.
        const QString ws = QDir::cleanPath(root + (instancia_ == "automatizacion" ? "/Runtime/agente-automatizacion"
                                                                                   : "/Runtime/agente"));
        QFile f(root + "/Credenciales/agentes/antigravity/.gemini/antigravity-cli/history.jsonl");
        if (f.open(QIODevice::ReadOnly)) {
            f.seek(std::max<qint64>(0, f.size() - 256 * 1024));
            const QList<QByteArray> lineas = f.readAll().split('\n');
            for (auto it = lineas.crbegin(); it != lineas.crend(); ++it) {
                const json j = json::parse(it->toStdString(), nullptr, false);
                if (!j.is_object() || j.value("timestamp", qint64{0}) < inicioMs_) continue;
                const QString id = QString::fromStdString(j.value("conversationId", ""));
                if (!id.isEmpty() && !QUuid::fromString(id).isNull() &&
                    QDir::cleanPath(QString::fromStdString(j.value("workspace", ""))) == ws)
                    return id;
            }
        }
        return sesion_;
    }
    if (sesion_.isEmpty() || runtimeOf(provider_) != "claude") return {};
    // Solo si ya guardó algo (<perfil>/projects/<carpeta>/<id>.jsonl): `--resume` de una
    // conversación sin mensajes no la encuentra y el asistente no arrancaría.
    QDirIterator it(root + "/Credenciales/agentes/claude/projects", {sesion_ + ".jsonl"}, QDir::Files,
                    QDirIterator::Subdirectories);
    return it.hasNext() ? sesion_ : QString();
}

void AgentViewModel::reanudarAlArrancar(const QString& id, const QString& proveedor, const QString& modelo,
                                        const QString& seguir) {
    if (id.isEmpty() || QUuid::fromString(id).isNull()) return;
    // El evento abierto sigue con el motor de la conversación (queda en su configuración).
    if (findProvider(proveedor) != nullptr && (proveedor != provider_ || modelo != model_)) {
        provider_ = proveedor;
        model_ = modelo;
        saveConfig();
        emit configChanged();
        emit installChanged();
    }
    reanudar_ = id;
    seguir_ = seguir;
    // Cuando el MCP esté listo (como el arranque automático), aunque no esté prendido.
    QTimer::singleShot(ba::ajustes::entero("asistente.demoraArranqueMs", 2500), this, [this]() {
        if (reanudar_.isEmpty() || running() || !installed() || !mcpReady()) return;
        start();
        emit mostrarseAlReanudar();
    });
}

void AgentViewModel::setHayAire(std::function<bool()> fn) {
    hayAire_ = std::move(fn);
    if (vigiaAire_ != nullptr) return;
    vigiaAire_ = new QTimer(this);
    vigiaAire_->setInterval(ba::ajustes::entero("asistente.vigiaAireMs", 2000));
    connect(vigiaAire_, &QTimer::timeout, this, [this]() {
        if (!session_->running() || !hayAire_) return;
        const bool aire = hayAire_();
        // Salió al aire con la consola prendida: se relanza sin ella.
        if (aire && conConsola_) {
            setError(tr("Al aire: el asistente se relanzó sin consola (Bash/PowerShell) hasta que termine la emisión."));
            restart();
        }
    });
    vigiaAire_->start();
}

void AgentViewModel::send(const QString& text) {
    if (text.trimmed().isEmpty()) return;
    if (!running()) {
        setError(tr("El agente no esta corriendo."));
        return;
    }
    if (localActivo_) {
        if (local_) local_("asistente", text.trimmed(), false);
        return;
    }
    session_->submit(text.trimmed());
}

QString AgentViewModel::faltaParaEncargo() const {
    if (findProvider(provider_) == nullptr) return tr("No hay ningún asistente elegido: elegí uno en la pestaña Asistente.");
    if (!installed()) return tr("El asistente «%1» no está instalado: instalalo en la pestaña Asistente.").arg(current().value("name").toString());
    if (!hasKey()) return tr("Al asistente «%1» le falta su clave: cargala en la pestaña Asistente.").arg(current().value("name").toString());
    if (!mcpReady()) return tr("El servidor MCP no está prendido: sin él el asistente no puede manejar el programa (Configuración → Asistente (MCP)).");
    return {};
}

void AgentViewModel::encargar(const QString& texto) {
    if (texto.trimmed().isEmpty()) return;
    emit pideVerse();
    if (running()) {
        send(texto);
        return;
    }
    // Arranca y manda cuando la terminal ya está: unos segundos después de que corre
    // (Claude Code tarda en mostrar su entrada; antes, lo escrito se perdía).
    encargoPendiente_ = texto;
    QPointer<AgentViewModel> yo(this);
    auto* c = new QMetaObject::Connection;
    *c = connect(this, &AgentViewModel::runningChanged, this, [yo, c]() {
        if (!yo || !yo->running()) return;
        QObject::disconnect(*c);
        delete c;
        QTimer::singleShot(ba::ajustes::entero("asistente.esperaEncargoMs", 6000), yo, [yo]() {
            if (!yo || yo->encargoPendiente_.isEmpty()) return;
            const QString t = yo->encargoPendiente_;
            yo->encargoPendiente_.clear();
            yo->send(t);
        });
    });
    if (installed() && mcpReady()) start();
    else setError(tr("El asistente no está listo (instalado y con el servidor MCP prendido): el pedido queda para cuando arranque."));
}

// --- Alertas ------------------------------------------------------------------------------

QVariantList AgentViewModel::alerts() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(alerts_.list().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).array().toVariantList();
}

QVariantList AgentViewModel::events() const {
    QVariantList out = QJsonDocument::fromJson(QByteArray::fromStdString(alerts_.events(0, 30).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
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
    // Las descripciones vienen marcadas (QT_TRANSLATE_NOOP("alertas", …)): se traducen acá.
    json tipos = AlertEngine::types();
    const auto traducir = [](json& v) {
        if (v.is_string()) v = ba::i18n::traducir("alertas", v.get<std::string>().c_str());
    };
    for (json& t : tipos) {
        if (t.contains("summary")) traducir(t["summary"]);
        if (t.contains("params") && t["params"].is_object())
            for (auto it = t["params"].begin(); it != t["params"].end(); ++it) traducir(it.value());
    }
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", tipos}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object()
        .value("v")
        .toArray()
        .toVariantList();
}

QString AgentViewModel::createAlertUi(const QVariantMap& spec) {
    const json j = json::parse(QJsonDocument(QJsonObject::fromVariantMap(spec)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    if (!j.is_object()) return tr("no se entiende el disparador");
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
    pendingAlerts_ << QString::fromStdString(
        ba::i18n::llenar(ba::i18n::traducir("asistente", "[%1] %2 (disparador %3)"),
                         {t.toStdString(), ev.value("llmText", ev.value("text", "")), ev.value("ruleId", "")}));
    if (!flushScheduled_) {
        flushScheduled_ = true;
        // Si llegan varias juntas, van en un solo mensaje.
        QTimer::singleShot(ba::ajustes::entero("asistente.agruparAlertasMs", 1500), this, &AgentViewModel::flushAlerts);
    }
}

void AgentViewModel::deliverNote(const QString& text) {
    if (!running()) return;
    pendingAlerts_ << text;
    if (!flushScheduled_) {
        flushScheduled_ = true;
        QTimer::singleShot(ba::ajustes::entero("asistente.agruparNotasMs", 600), this, &AgentViewModel::flushAlerts);
    }
}

void AgentViewModel::flushAlerts() {
    flushScheduled_ = false;
    if (pendingAlerts_.isEmpty() || !running()) {
        pendingAlerts_.clear();
        return;
    }
    // La marca del principio es fija (no se traduce): el prompt del asistente la nombra.
    const QString msg =
        "[AVISO AUTOMATICO de Broadcast Appliance] " +
        QString::fromStdString(ba::i18n::llenar(
            ba::i18n::traducir("asistente", "%1 — Atendelo segun lo acordado (si no hay nada acordado, revisa con "
                                            "las herramientas, contame que paso y propone que hacer)."),
            {pendingAlerts_.join(" | ").toStdString()}));
    pendingAlerts_.clear();
    session_->submit(msg);
}

void AgentViewModel::mandarNovedades() {
    if (novedades_.isEmpty() || !running()) {
        novedades_.clear();
        return;
    }
    const QString lista = novedades_.join(" | ");
    novedades_.clear();
    const std::string l = lista.toStdString();
    session_->submit(QString::fromStdString(
        instancia_ == "automatizacion"
            ? ba::i18n::llenar(ba::i18n::traducir("asistente",
                                                  "[NOVEDADES del evento] %1 — Solo para que sepas qué está pasando. "
                                                  "Hacé algo únicamente si está acordado para esto o si algo quedó "
                                                  "mal; si no, contestá solo «ok»."),
                               {l})
            : ba::i18n::llenar(ba::i18n::traducir("asistente",
                                                  "[NOVEDADES del evento, solo para tu contexto] %1 — No hagas nada "
                                                  "ni lo comentes: contestá solo «ok». De esto se ocupa el asistente "
                                                  "de automatización; tenelo en cuenta cuando el operador te hable."),
                               {l})));
}

void AgentViewModel::openAgentsFolder() const {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromStdWString((ba::platform::dataRoot() / "Agentes").wstring())));
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
                "debug", "backups", "history.jsonl", "statsig", "telemetry", "cache", "Library/Caches"};
    if (runtime == "antigravity")
        return {".gemini/antigravity-cli/cache", ".gemini/antigravity-cli/crashes", ".gemini/antigravity-cli/implicit",
                ".gemini/antigravity-cli/conversation_summaries.db", ".gemini/antigravity", "Library/Caches"};
    if (runtime == "pi") return {"sessions", "mcp.log", "mcp.log.1"};
    if (runtime == "opencode")
        return {"data/opencode/storage", "data/opencode/log", "data/opencode/snapshot", "state"};
    return {};
}

const char* const kMotores[] = {"claude", "antigravity", "opencode", "pi"};

} // namespace

QString AgentViewModel::ocupadoParaLimpiar() const {
    if (running()) return tr("Primero pará el asistente (la consola está corriendo).");
    if (corridasEnCurso_ && corridasEnCurso_() > 0) return tr("Hay encargos de agentes corriendo: esperá que terminen.");
    if (installing_) return tr("Hay una instalación en curso.");
    return {};
}

void AgentViewModel::revisarLimpieza() {
    QVariantList out;
    const fs::path root = ba::platform::dataRoot(); // de la máquina
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
    const fs::path root = ba::platform::dataRoot(); // de la máquina
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
        for (const fs::directory_entry& d : fs::directory_iterator(ba::platform::dataRoot() / "Agentes" / rt, ec)) {
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
    if (!conocido) return tr("Motor desconocido: %1").arg(runtime);
    const fs::path root = ba::platform::dataRoot(); // de la máquina
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
