// ============================================================================
// src/app/agent/ModelosLocales.cpp — ver ModelosLocales.hpp
// ============================================================================
#include "i18n/I18n.hpp"
#include "agent/ModelosLocales.hpp"
#include "ajustes/Ajustes.hpp"

#include <QRegularExpression>
#include "platform/Paths.hpp"

#include "agent/ArgumentosLocales.hpp"
#include "agent/VozIdioma.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>
#include <regex>
#include <utility>

#include "net/Descargas.hpp"
#include "net/HttpsClient.hpp"

#include <chrono>
#include <set>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ba::app::agent {

namespace {

constexpr int kPruebasGuardadas = 20;

QString deRuta(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

/// Un nombre de archivo del repo dentro de una URL: cada tramo codificado
/// (las voces de piper traen cosas como «espeak-ng-data/voices/!v/Mr (serious)»).
std::string enUrl(const std::string& nombre) {
    QStringList tramos = QString::fromStdString(nombre).split('/');
    for (QString& t : tramos)
        t = QString::fromLatin1(QUrl::toPercentEncoding(t));
    return tramos.join('/').toStdString();
}

/// Un nombre UTF-8 (de la API) como ruta, también en Windows.
fs::path desdeUtf8(const std::string& s) { return fs::path(QString::fromStdString(s).toStdWString()); }

QString sha256De(const QString& ruta) {
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

bool escribir(const QString& ruta, const QByteArray& datos) {
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(datos) == datos.size();
}

QVariantMap aVariant(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

/// Un archivo ya bajado está bien si coincide su hash (el de LFS o el de blob).
bool verificado(const fs::path& p, const ArchivoHf& a) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != a.tamano) return false;
    if (!a.sha256.empty()) return sha256De(deRuta(p)).toStdString() == a.sha256;
    if (!a.blob.empty()) return shaBlobGit(p) == a.blob;
    return false; // sin hash publicado no se usa
}

} // namespace

// --- funciones sueltas (las prueba test_modelos_locales) ----------------------

RepoHf leerRepoHf(const std::string& cuerpo) {
    RepoHf r;
    const json j = json::parse(cuerpo, nullptr, false);
    if (!j.is_object()) return r;
    r.commit = j.value("sha", std::string());
    r.privado = j.value("private", false);
    for (const json& s : j.value("siblings", json::array())) {
        if (!s.is_object()) continue;
        ArchivoHf a;
        a.nombre = s.value("rfilename", std::string());
        a.tamano = s.value("size", std::uint64_t{0});
        a.blob = s.value("blobId", std::string());
        if (s.contains("lfs") && s["lfs"].is_object()) a.sha256 = s["lfs"].value("sha256", std::string());
        // Nada que salga de la carpeta: el nombre lo pone el repo, no nosotros.
        if (a.nombre.empty() || a.nombre.find("..") != std::string::npos || a.nombre.front() == '/') continue;
        r.archivos.push_back(std::move(a));
    }
    return r;
}

std::string shaBlobGit(const fs::path& archivo) {
    QFile f(deRuta(archivo));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    const QByteArray cab = "blob " + QByteArray::number(f.size()) + '\0';
    h.addData(cab);
    h.addData(&f);
    return h.result().toHex().toStdString();
}

std::string carpetaDeRepo(const std::string& repo) {
    std::string s;
    for (char c : repo)
        s += (c == '/' || c == '\\' || c == ':') ? std::string("__") : std::string(1, c);
    return s;
}

std::string huellaEsquema(const json& inputSchema) {
    // nlohmann ordena las claves y, sin sangría, no pone espacios: lo mismo que
    // json.dumps(sort_keys=True, separators=(',', ':')) del lado de la fábrica.
    const QByteArray h =
        QCryptographicHash::hash(QByteArray::fromStdString(inputSchema.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)), QCryptographicHash::Sha256)
            .toHex();
    return h.left(16).toStdString();
}

json compararFicha(const json& mj, const json& app) {
    json r = json::object();
    const json eps = mj.value("endpoints", json::array());
    json iguales = json::array(), cambiaron = json::array(), noEstan = json::array(), porCat = json::object();
    for (const json& e : eps) {
        if (!e.is_object()) continue;
        const std::string n = e.value("nombre", std::string());
        const std::string cat = e.value("categoria", std::string("otras"));
        porCat[cat] = porCat.value(cat, 0) + 1;
        if (!app.contains(n))
            noEstan.push_back(n);
        else if (huellaEsquema(app[n]) == e.value("esquema_huella", std::string()))
            iguales.push_back(n);
        else
            cambiaron.push_back(n);
    }
    const int enApp = app.is_object() ? static_cast<int>(app.size()) : 0;
    const int usables = static_cast<int>(iguales.size() + cambiaron.size());
    r["entrenadas"] = eps.size();
    r["iguales"] = iguales.size();
    r["cambiaron"] = cambiaron;
    r["no_estan"] = noEstan;
    r["herramientas_app"] = enApp;
    r["sin_entrenar"] = std::max(0, enApp - usables);
    r["cobertura"] = enApp > 0 ? static_cast<double>(iguales.size()) / enApp : 0.0;
    r["por_categoria"] = porCat;
    r["lista"] = iguales; // las que el modelo conoce tal cual
    const json m = mj.value("modelo", json::object());
    for (const char* k : {"build", "version", "fecha", "base", "perfil", "commit"})
        if (m.contains(k)) r[k] = m[k];
    if (mj.contains("inventario_mcp")) r["inventario_entrenamiento"] = mj["inventario_mcp"];
    return r;
}

std::string plataformaCatalogo() {
    const QString arch = QSysInfo::currentCpuArchitecture();
    const std::string a = (arch == "arm64" || arch == "aarch64") ? "arm64" : "x64";
#if defined(_WIN32)
    return "win-" + a;
#elif defined(__APPLE__)
    return "mac-" + a;
#else
    return "linux-" + a;
#endif
}

// --- ModelosLocales ------------------------------------------------------------

ModelosLocales::ModelosLocales(fs::path raizDatos, QString dirBinarios, QObject* parent)
    : QObject(parent), raiz_(std::move(raizDatos)), dirBinarios_(std::move(dirBinarios)) {
    QFile f(":/agent/modelos-catalogo.json");
    if (f.open(QIODevice::ReadOnly)) catalogo_ = json::parse(f.readAll().toStdString(), nullptr, false);
    if (!catalogo_.is_object()) catalogo_ = json::object();
    leerAjustes();
    registrarActualizaciones();
}

ModelosLocales::~ModelosLocales() {
    // Las descargas de modelos se cortan y se espera a que suelten (usan carpetas
    // de acá); lo que espera en la cola sale sin correr.
    cancelarDescarga();
    if (hiloDisco_.joinable()) hiloDisco_.join();
    for (int i = 0; i < 100 && std::any_of(enCurso_.begin(), enCurso_.end(), [](const QString& r) {
                        return net::Descargas::la().ocupado("modelo:" + r.toStdString());
                    });
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (auto& h : hilosFicha_)
        if (h.joinable()) h.join();
    if (proc_ != nullptr) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(3000);
    }
}

fs::path ModelosLocales::carpetaModelos() const { return ba::platform::dataRoot() / "Modelos"; }

fs::path ModelosLocales::carpetaDe(const QString& repo) const {
    return carpetaModelos() / carpetaDeRepo(repo.toStdString());
}

bool ModelosLocales::instaladoEn(const fs::path& carpetaModelos, const std::string& repo) {
    std::error_code ec;
    return fs::exists(carpetaModelos / carpetaDeRepo(repo) / ".ok", ec);
}

bool ModelosLocales::instalado(const QString& repo) const { return instaladoEn(carpetaModelos(), repo.toStdString()); }

nlohmann::json ModelosLocales::entradaCatalogo(const QString& repo) const {
    for (const json& m : catalogo_.value("modelos", json::array()))
        if (m.value("repo", std::string()) == repo.toStdString()) return m;
    return json::object();
}

std::filesystem::path ModelosLocales::carpetaInstalada(const QString& repo) const {
    return instalado(repo) ? carpetaDe(repo) : fs::path();
}

QString ModelosLocales::ggufParaChat(const QString& pedido, QString* falta, QString* repoParaBajar) const {
    const auto archivoDe = [this](const QString& repo) -> QString {
        const json e = entradaCatalogo(repo);
        if (e.value("formato", std::string()) != "gguf" || !e.value("archivo", json()).is_string() || !instalado(repo))
            return {};
        const fs::path f = carpetaDe(repo) / e["archivo"].get<std::string>();
        std::error_code ec;
        return fs::exists(f, ec) ? QString::fromStdWString(f.wstring()) : QString();
    };
    // El que se eligió en el Asistente (si está en el catálogo y es GGUF).
    if (!pedido.isEmpty()) {
        const json e = entradaCatalogo(pedido);
        if (e.empty() || e.value("formato", std::string()) != "gguf") {
            if (falta != nullptr) *falta = tr("«%1» no es un modelo GGUF del catálogo.").arg(pedido);
            return {};
        }
        if (const QString f = archivoDe(pedido); !f.isEmpty()) return f;
        if (falta != nullptr) *falta = tr("El modelo «%1» todavía no está: se baja ahora.").arg(pedido);
        if (repoParaBajar != nullptr) *repoParaBajar = pedido;
        return {};
    }
    // El activo, o (sin activo) el primer reactivo instalado de esta plataforma.
    QString repo = activo_;
    if (repo.isEmpty()) repo = instaladoDeTipo("reactivo");
    if (repo.isEmpty()) {
        if (falta != nullptr) *falta = tr("No hay un modelo local: bajalo en Configuración → Modelos locales.");
        return {};
    }
    if (const QString f = archivoDe(repo); !f.isEmpty()) return f;
    // MLX (la Mac): su versión GGUF, si el catálogo la tiene.
    QString hermano = repo;
    hermano.replace(QRegularExpression("-mlx-q4$"), "-GGUF");
    if (hermano != repo && !entradaCatalogo(hermano).empty()) {
        if (const QString f = archivoDe(hermano); !f.isEmpty()) return f;
        if (falta != nullptr)
            *falta = tr("El chat del modelo local usa su versión GGUF (%1), que todavía no está: se baja ahora.")
                         .arg(hermano);
        if (repoParaBajar != nullptr) *repoParaBajar = hermano;
        return {};
    }
    // Sin versión GGUF del activo: un GGUF que ya esté, o el más nuevo de 4B del catálogo.
    QString elegido;
    int mejor = -1;
    for (const json& m : catalogo_.value("modelos", json::array())) {
        if (m.value("formato", std::string()) != "gguf" || m.value("tipo", std::string("reactivo")) != "reactivo") continue;
        const QString r = QString::fromStdString(m.value("repo", std::string()));
        if (const QString f = archivoDe(r); !f.isEmpty()) return f;
        if (!r.contains("-4b-")) continue;
        const QRegularExpressionMatch v = QRegularExpression("-v(\\d+)-").match(r);
        const int n = v.hasMatch() ? v.captured(1).toInt() : 1;
        if (n > mejor) mejor = n, elegido = r;
    }
    if (!elegido.isEmpty()) {
        if (falta != nullptr)
            *falta = tr("El chat del modelo local usa un modelo GGUF (%1), que todavía no está: se baja ahora.")
                         .arg(elegido);
        if (repoParaBajar != nullptr) *repoParaBajar = elegido;
        return {};
    }
    if (falta != nullptr) *falta = tr("El modelo local activo (%1) no tiene versión GGUF para el chat.").arg(repo);
    return {};
}

QString ModelosLocales::mmprojPara(const QString& gguf, QString* repoParaBajar) const {
    if (gguf.isEmpty()) return {};
    // La base del modelo de ese archivo (por su nombre en el catálogo).
    const std::string nombre = fs::path(gguf.toStdWString()).filename().string();
    std::string base;
    for (const json& m : catalogo_.value("modelos", json::array()))
        if (m.value("formato", std::string()) == "gguf" && m.value("archivo", json()).is_string() &&
            m["archivo"].get<std::string>() == nombre && m.value("tipo", std::string("reactivo")) != "vision") {
            base = m.value("base", std::string());
            break;
        }
    if (base.empty()) return {};
    const std::string plat = plataformaCatalogo();
    for (const json& m : catalogo_.value("modelos", json::array())) {
        if (m.value("tipo", std::string()) != "vision" || m.value("base", std::string()) != base) continue;
        const json plats = m.value("plataformas", json::array());
        if (std::find(plats.begin(), plats.end(), plat) == plats.end()) continue;
        const QString repo = QString::fromStdString(m.value("repo", std::string()));
        if (instalado(repo) && m.value("archivo", json()).is_string()) {
            const fs::path f = carpetaDe(repo) / m["archivo"].get<std::string>();
            std::error_code ec;
            if (fs::exists(f, ec)) return QString::fromStdWString(f.wstring());
        }
        if (repoParaBajar != nullptr && repoParaBajar->isEmpty()) *repoParaBajar = repo;
    }
    return {};
}

QString ModelosLocales::instaladoDeTipo(const QString& tipo) const {
    for (const QVariant& v : catalogo()) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() == tipo && m.value("instalado").toBool())
            return m.value("repo").toString();
    }
    return {};
}

QString ModelosLocales::vozPara(const std::string& idioma, const QString& preferida) const {
    const QString lengua = QString::fromStdString(voz_idioma::lengua(idioma));
    std::vector<std::pair<QString, QString>> bajadas; // {repo, lengua}, en el orden del catálogo
    for (const QVariant& v : catalogo()) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() == "tts" && m.value("instalado").toBool())
            bajadas.emplace_back(m.value("repo").toString(), m.value("idioma").toString());
    }
    const auto lenguaDe = [&](const QString& r) -> std::optional<QString> {
        for (const auto& [repo, l] : bajadas)
            if (repo == r) return l;
        return std::nullopt;
    };
    const std::optional<QString> pref = preferida.isEmpty() ? std::nullopt : lenguaDe(preferida);
    if (pref && (lengua.isEmpty() || *pref == lengua)) return preferida;
    for (const std::string& r : voz_idioma::voces(idioma))
        if (lenguaDe(QString::fromStdString(r))) return QString::fromStdString(r);
    for (const auto& [repo, l] : bajadas)
        if (l == lengua) return repo;
    if (pref) return preferida;
    return bajadas.empty() ? QString() : bajadas.front().first;
}

QVariantList ModelosLocales::catalogo() const {
    QVariantList out;
    const std::string plat = plataformaCatalogo();
    for (const json& m : catalogo_.value("modelos", json::array())) {
        const json plats = m.value("plataformas", json::array());
        if (std::find(plats.begin(), plats.end(), plat) == plats.end()) continue;
        QVariantMap v = aVariant(m);
        if (!v.contains("tipo")) v["tipo"] = "reactivo"; // los de la fábrica
        const QString repo = QString::fromStdString(m.value("repo", std::string()));
        v["instalado"] = instalado(repo);
        v["activo"] = repo == activo_;
        out.push_back(v);
    }
    return out;
}

namespace {
/// El ejecutable del motor: el mismo nombre y el mismo protocolo en las tres
/// plataformas (MLX en la Mac, src/modelo-mac; llama.cpp en Windows y Linux,
/// src/modelo-llama).
QString exeMotor(const QString& dir) {
#if defined(_WIN32)
    return dir + "/ba-modelo.exe";
#else
    return dir + "/ba-modelo";
#endif
}
} // namespace

bool ModelosLocales::hayMotor() const {
#if defined(_WIN32)
    return QFileInfo::exists(exeMotor(dirBinarios_));
#else
    return QFileInfo(exeMotor(dirBinarios_)).isExecutable();
#endif
}

QString ModelosLocales::motor() const {
#if defined(__APPLE__)
    return hayMotor() ? "MLX (ba-modelo)" : tr("falta ba-modelo en bin/64bit (make modelo-mac)");
#else
    return hayMotor() ? "llama.cpp (ba-modelo)" : tr("falta ba-modelo en bin/64bit (tools/deps/llamacpp-lib.sh)");
#endif
}

// --- ajustes ---------------------------------------------------------------------

void ModelosLocales::leerAjustes() {
    QFile f(deRuta(carpetaModelos() / "ajustes.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const json j = json::parse(f.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) return;
    activo_ = QString::fromStdString(j.value("activo", std::string()));
    sesgo_ = j.value("sesgo", 0.0);
    mtp_ = QString::fromStdString(j.value("mtp", std::string("no")));
    alArrancar_ = j.value("al_arrancar", false);
    if (!instalado(activo_)) activo_.clear();
}

void ModelosLocales::guardarAjustes() const {
    const json j{{"activo", activo_.toStdString()},
                 {"sesgo", sesgo_},
                 {"mtp", mtp_.toStdString()},
                 {"al_arrancar", alArrancar_}};
    escribir(deRuta(carpetaModelos() / "ajustes.json"), QByteArray::fromStdString(j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
}

void ModelosLocales::setSesgo(double s) {
    s = std::clamp(s, -8.0, 8.0);
    if (s == sesgo_) return;
    sesgo_ = s;
    guardarAjustes();
    emit cambio();
}

void ModelosLocales::setMtp(const QString& m) {
    if (m == mtp_ || !QStringList{"auto", "q8", "fp16", "no"}.contains(m)) return;
    mtp_ = m;
    guardarAjustes();
    emit cambio();
}

void ModelosLocales::setAlArrancar(bool on) {
    if (on == alArrancar_) return;
    alArrancar_ = on;
    guardarAjustes();
    emit cambio();
}

void ModelosLocales::arrancarSiCorresponde() {
    if (alArrancar_ && !activo_.isEmpty() && hayMotor()) activar(activo_);
}

void ModelosLocales::abrirCarpeta() const {
    QDir().mkpath(deRuta(carpetaModelos()));
    QDesktopServices::openUrl(QUrl::fromLocalFile(deRuta(carpetaModelos())));
}

// --- con qué herramientas se entrenó (modelo.json) ----------------------------------

namespace {
/// Los pesos de un repo según su API (?blobs=true): nombre -> sha256 de LFS.
std::map<std::string, std::string> pesosDe(const RepoHf& r, const std::string& soloGguf) {
    std::map<std::string, std::string> m;
    for (const ArchivoHf& a : r.archivos) {
        const auto termina = [&a](const char* x) {
            const std::string e(x);
            return a.nombre.size() >= e.size() &&
                   a.nombre.compare(a.nombre.size() - e.size(), e.size(), e) == 0;
        };
        if (termina(".safetensors") || (termina(".gguf") && (soloGguf.empty() || a.nombre == soloGguf ||
                                                             a.nombre.rfind("mmproj", 0) == 0)))
            m[a.nombre] = a.sha256;
    }
    return m;
}

net::HttpResponse pedirHf(const std::string& url) {
    net::HttpRequest req;
    req.url = url;
    req.timeoutMs = ba::ajustes::sAMs("modelos.timeoutHfS", 20);
    // Siguiendo las redirecciones: Hugging Face contesta `resolve/<commit>/…` con un
    // 307 a /api/resolve-cache/… (desde 2026-10; antes daba el archivo directo) y,
    // sin seguirlo, cada modelo.json parecía «no publicado».
    net::HttpResponse res;
    for (int salto = 0; salto < 6; ++salto) {
        res = net::HttpsClient::request(req);
        if (res.status < 300 || res.status >= 400) break;
        std::string loc = res.header("Location");
        if (loc.empty()) break;
        if (loc.rfind("http", 0) != 0) { // relativa
            const auto esquema = req.url.find("://");
            const auto barra = req.url.find('/', esquema == std::string::npos ? 0 : esquema + 3);
            loc = req.url.substr(0, barra) + (loc.front() == '/' ? "" : "/") + loc;
        }
        req.url = loc;
    }
    return res;
}
} // namespace

json ModelosLocales::ficha(const QString& repo) const {
    const auto it = fichasJson_.find(repo);
    return it == fichasJson_.end() ? json::object() : it->second;
}

void ModelosLocales::ponerFicha(const QString& repo, json f) {
    fichasJson_[repo] = f;
    fichas_[repo] = aVariant(f);
    emit fichasCambio();
}

void ModelosLocales::revisarHerramientas(const QString& repo, bool forzar) {
    const json entrada = entradaCatalogo(repo);
    if (entrada.empty() || (entrada.contains("tipo") && entrada.value("tipo", std::string()) != "reactivo"))
        return;
    const std::string est = ficha(repo).value("estado", std::string());
    if (!forzar && (est == "ok" || est == "sin_ficha" || est == "buscando")) return;
    ponerFicha(repo, {{"estado", "buscando"}});
    const std::string r = repo.toStdString();
    const std::string revision = entrada.value("revision", std::string());
    const std::string archivo =
        entrada.value("archivo", json()).is_string() ? entrada["archivo"].get<std::string>() : "";
    const fs::path bajado = carpetaDe(repo) / "modelo.json";
    const fs::path guardado = carpetaModelos() / ".fichas" / (carpetaDeRepo(r) + ".json");
    const bool estaBajado = instalado(repo);
    QPointer<ModelosLocales> yo(this);
    hilosFicha_.emplace_back([yo, repo, r, revision, archivo, bajado, guardado, estaBajado, forzar]() {
        json mj, f = json::object();
        std::string nota, de;
        const auto leer = [](const fs::path& p) {
            QFile q(deRuta(p));
            return q.open(QIODevice::ReadOnly) ? json::parse(q.readAll().toStdString(), nullptr, false)
                                               : json();
        };
        std::error_code ec;
        if (estaBajado && fs::exists(bajado, ec)) {
            mj = leer(bajado);
            de = tr("el modelo bajado").toStdString();
        } else if (!forzar && fs::exists(guardado, ec)) {
            const json g = leer(guardado);
            // Un «no hay» guardado vale 6 horas: el repo pudo publicarlo después (o la
            // consulta falló por otra cosa). Lo que sí se encontró, vale siempre.
            const std::int64_t edad =
                QDateTime::currentSecsSinceEpoch() - (g.is_object() ? g.value("cuando", std::int64_t{0}) : 0);
            if (g.is_object() && (!g.value("modelo_json", json()).is_null() || edad < ba::ajustes::entero("modelos.cacheFichasHoras", 6) * 3600)) {
                mj = g.value("modelo_json", json());
                nota = g.value("nota", std::string());
                de = g.value("de", std::string("Hugging Face"));
                if (mj.is_null()) f = {{"estado", "sin_ficha"}, {"nota", nota}};
            }
        }
        if (mj.is_null() && f.empty()) {
            const std::string rev = revision.empty() ? "main" : revision;
            const net::HttpResponse res =
                pedirHf("https://huggingface.co/" + r + "/resolve/" + rev + "/modelo.json");
            if (!res.error.empty()) {
                f = {{"estado", "error"}, {"nota", tr("Sin conexión con Hugging Face: %1").arg(QString::fromStdString(res.error)).toStdString()}};
            } else if (res.status == 200) {
                mj = json::parse(res.body, nullptr, false);
                de = "Hugging Face (" + rev.substr(0, 10) + ")";
            } else if (rev != "main") {
                // El commit fijado en el catálogo es de antes del modelo.json: vale
                // el de ahora solo si los pesos son los MISMOS (mismo sha256).
                const net::HttpResponse a =
                    pedirHf("https://huggingface.co/api/models/" + r + "/revision/" + rev + "?blobs=true");
                const net::HttpResponse b = pedirHf("https://huggingface.co/api/models/" + r + "?blobs=true");
                const auto pa = pesosDe(leerRepoHf(a.body), archivo),
                           pb = pesosDe(leerRepoHf(b.body), archivo);
                bool mismos = !pa.empty();
                for (const auto& [n, h] : pa)
                    mismos = mismos && !h.empty() && pb.count(n) && pb.at(n) == h;
                if (mismos) {
                    const net::HttpResponse m =
                        pedirHf("https://huggingface.co/" + r + "/resolve/main/modelo.json");
                    if (m.status == 200) {
                        mj = json::parse(m.body, nullptr, false);
                        de = "Hugging Face (main)";
                        nota = tr("El modelo.json se publicó después del commit fijado en el catálogo; los "
                                  "pesos son los mismos, así que le corresponde.")
                                   .toStdString();
                    }
                } else if (a.status == 200 && b.status == 200) {
                    nota = tr("El repo tiene un modelo.json más nuevo, pero los pesos cambiaron desde el commit "
                              "fijado: no corresponde a esta versión.")
                               .toStdString();
                }
                if (mj.is_null() && nota.empty()) nota = tr("El repo todavía no publica modelo.json.").toStdString();
            } else {
                const net::HttpResponse a = pedirHf("https://huggingface.co/api/models/" + r);
                nota = (a.status == 200 ? tr("El repo todavía no publica modelo.json.")
                                        : tr("El repo es privado o ya no existe en Hugging Face."))
                           .toStdString();
            }
            if (mj.is_discarded()) {
                mj = json();
                nota = tr("El modelo.json del repo no es un JSON válido.").toStdString();
            }
            if (f.empty()) {
                // Se guarda para mirarlo sin red la próxima vez (también el «no hay»).
                QDir().mkpath(deRuta(guardado.parent_path()));
                escribir(deRuta(guardado), QByteArray::fromStdString(
                                               json{{"modelo_json", mj},
                                                    {"nota", nota},
                                                    {"de", de},
                                                    {"cuando", QDateTime::currentSecsSinceEpoch()}}
                                                   .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)));
            }
        }
        if (f.empty() && (mj.is_null() || !mj.is_object() || !mj.contains("endpoints")))
            f = {{"estado", "sin_ficha"}, {"nota", nota}};
        QMetaObject::invokeMethod(qApp, [yo, repo, mj, f, nota, de]() {
            if (!yo) return;
            if (!f.empty()) return yo->ponerFicha(repo, f);
            json r = compararFicha(mj, yo->herramientas_ ? yo->herramientas_() : json::object());
            r["estado"] = "ok";
            r["de"] = de;
            if (!nota.empty()) r["nota"] = nota;
            // Lo que decía el catálogo, para ver si coincide con lo que dice el modelo.
            const json e = yo->entradaCatalogo(repo);
            if (e.contains("herramientas")) r["catalogo_dice"] = e["herramientas"];
            yo->ponerFicha(repo, r);
        });
    });
}

// --- descarga ----------------------------------------------------------------------

namespace {
/// El commit del modelo instalado (su .ok), o "".
std::string commitInstalado(const fs::path& carpeta) {
    QFile f(deRuta(carpeta / ".ok"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const json j = json::parse(f.readAll().toStdString(), nullptr, false);
    return j.is_object() ? j.value("commit", std::string()) : std::string();
}
} // namespace

QStringList ModelosLocales::enCola() const {
    QStringList out;
    for (const QString& r : enCurso_) out << r;
    return out;
}

QString ModelosLocales::descargar(const QString& repo) {
    const auto p = pedidoDe(repo, false);
    if (!p) return QString::fromUtf8(errorPedido_.c_str());
    const auto r = net::Descargas::la().pedir(*p);
    if (r == net::Descargas::Resultado::Encolada) {
        enCurso_.insert(repo);
        if (textoDescarga_.isEmpty() || !descargando_) {
            progreso_ = 0;
            textoDescarga_ = tr("En la cola: %1").arg(repo);
        }
        descargando_ = true;
        emit descargaCambio();
    }
    return QString::fromStdString(ba::i18n::traducir("descargas", net::Descargas::texto(r)));
}

std::optional<net::Descargas::Pedido> ModelosLocales::pedidoDe(const QString& repo, bool actualizacion) {
    const json entrada = entradaCatalogo(repo);
    if (entrada.empty()) {
        errorPedido_ = tr("Ese modelo no está en el catálogo.").toStdString();
        emit notify(QString::fromStdString(errorPedido_), "error");
        return std::nullopt;
    }
    // GGUF: un repo puede traer varias cuantizaciones; se baja la del catálogo.
    const std::string archivo =
        entrada.value("archivo", json()).is_string() ? entrada["archivo"].get<std::string>() : "";
    const std::string soloGguf = entrada.contains("url") ? std::string() : archivo;
    // Modelos de terceros (los de voz): fijados por commit y solo los archivos
    // que hacen falta, o un archivo suelto por URL con su sha256.
    const std::string revision = entrada.value("revision", std::string());
    std::vector<std::string> soloArchivos;
    for (const json& a : entrada.value("archivos", json::array()))
        if (a.is_string()) soloArchivos.push_back(a.get<std::string>());
    // El sha256 de cada archivo, fijo en el catálogo (si lo trae): el de Hugging
    // Face tiene que coincidir, y los que no son LFS (tokens.txt) se verifican con este.
    std::map<std::string, std::string> shaFijo;
    const json shas = entrada.value("sha256_archivos", json::object());
    for (const auto& [nombre, sha] : shas.items())
        if (sha.is_string()) shaFijo[nombre] = sha.get<std::string>();
    ArchivoHf directo;
    if (entrada.contains("url")) {
        directo.nombre = archivo;
        directo.url = entrada.value("url", std::string());
        directo.sha256 = entrada.value("sha256", std::string());
        directo.tamano = entrada.value("bytes", std::uint64_t{0});
        if (directo.nombre.empty() || directo.sha256.size() != 64 || directo.tamano == 0) {
            errorPedido_ = tr("La entrada del catálogo no trae archivo, sha256 y bytes: no se baja.").toStdString();
            emit notify(QString::fromStdString(errorPedido_), "error");
            return std::nullopt;
        }
    }
    const fs::path destino = carpetaDe(repo);
    const fs::path staging = carpetaModelos() / (".bajando-" + carpetaDeRepo(repo.toStdString()));
    const fs::path fichas = carpetaModelos() / ".fichas";
    const std::string r = repo.toStdString();
    const std::string fijo = !directo.url.empty() ? std::string("url") : revision;

    net::Descargas::Pedido p;
    p.id = "modelo:" + r;
    p.tipo = "modelo";
    p.titulo = entrada.value("nombre", r);
    p.version = fijo.empty() ? "main" : fijo.substr(0, 12);
    // Fijado (por commit o por URL): si el instalado es ese, ya está. Los que
    // siguen «main» (los nuestros) se consultan: si no cambió, no se rebaja nada.
    if (!fijo.empty() && !actualizacion)
        p.yaEsta = [destino, fijo]() { return commitInstalado(destino) == fijo; };
    QPointer<ModelosLocales> yo(this);
    const QString qrepo = repo;
    p.trabajo = [yo, r, soloGguf, revision, soloArchivos, shaFijo, directo, destino, staging, fichas](
                    net::Descargas::Contexto& ctx) -> std::string {
        // Lo que muestran Configuración y el panel de traducción (de a un modelo).
        const auto avisar = [&ctx, yo](double p, const QString& t) {
            ctx.avance(p, t.toStdString());
            if (yo)
                QMetaObject::invokeMethod(yo.data(), [yo, p, t]() {
                    if (!yo) return;
                    yo->progreso_ = p;
                    yo->textoDescarga_ = t;
                    emit yo->descargaCambio();
                });
        };
        avisar(0, tr("Consultando Hugging Face…"));
        QString err;
        RepoHf repo;
        if (!directo.url.empty()) {
            repo.commit = "url";
            repo.archivos = {directo};
        } else {
            net::HttpRequest req;
            req.url = "https://huggingface.co/api/models/" + r +
                      (revision.empty() ? "" : "/revision/" + revision) + "?blobs=true";
            req.timeoutMs = ba::ajustes::sAMs("modelos.timeoutHfS", 20) * 3 / 2;
            req.cancel = ctx.banderaCancelar();
            const net::HttpResponse res = net::HttpsClient::request(req);
            if (!res.error.empty())
                err = tr("Sin conexión con Hugging Face: %1").arg(QString::fromStdString(res.error));
            else if (res.status == 401 || res.status == 404)
                err = tr("El modelo no está publicado (o es privado) en Hugging Face.");
            else if (res.status != 200)
                err = tr("Hugging Face contestó %1.").arg(res.status);
            else
                repo = leerRepoHf(res.body);
            if (err.isEmpty() && (repo.commit.empty() || repo.archivos.empty()))
                err = tr("No se entendió la respuesta de Hugging Face.");
            if (err.isEmpty() && !revision.empty() && repo.commit != revision)
                err = tr("Hugging Face no devolvió el commit fijado en el catálogo.");
        }
        if (!err.isEmpty()) return err.toStdString();
        if (ctx.cancelado()) return "cancelada";
        // Ya está esa versión (los que siguen «main»): nada que bajar.
        if (commitInstalado(destino) == repo.commit) {
            avisar(1.0, tr("Ya estaba al día."));
            return {};
        }
        std::vector<ArchivoHf> archivos;
        std::uint64_t total = 0;
        for (const ArchivoHf& a : repo.archivos) {
            if (a.nombre == ".gitattributes") continue;
            const bool gguf = a.nombre.size() > 5 && a.nombre.substr(a.nombre.size() - 5) == ".gguf";
            if (gguf && !soloGguf.empty() && a.nombre != soloGguf) continue;
            // `archivos` del catálogo: nombres exactos, o carpetas si terminan en
            // «/» (las voces de piper traen espeak-ng-data/ entera).
            const auto pedido = [&a](const std::string& f) {
                return f == a.nombre || (!f.empty() && f.back() == '/' && a.nombre.rfind(f, 0) == 0);
            };
            if (!soloArchivos.empty() && std::none_of(soloArchivos.begin(), soloArchivos.end(), pedido))
                continue;
            ArchivoHf conSha = a;
            if (const auto f = shaFijo.find(a.nombre); f != shaFijo.end()) {
                if (!a.sha256.empty() && a.sha256 != f->second)
                    return tr("El sha256 de %1 no es el del catálogo: no se baja.").arg(QString::fromStdString(a.nombre)).toStdString();
                conSha.sha256 = f->second;
            }
            archivos.push_back(conSha);
            total += a.tamano;
        }
        for (const std::string& f : soloArchivos)
            if (std::none_of(archivos.begin(), archivos.end(), [&f](const ArchivoHf& a) {
                    return a.nombre == f || (!f.empty() && f.back() == '/' && a.nombre.rfind(f, 0) == 0);
                }))
                return tr("Al repo le falta %1 (en el commit fijado).").arg(QString::fromStdString(f)).toStdString();
        // La carpeta a medio bajar se conserva entre intentos (cada archivo sigue
        // donde se cortó) mientras sea del MISMO commit; si es de otro, de cero.
        std::error_code ec;
        if (commitInstalado(staging) != repo.commit) fs::remove_all(staging, ec);
        fs::create_directories(staging, ec);
        {
            QFile m(deRuta(staging / ".ok"));
            if (m.open(QIODevice::WriteOnly | QIODevice::Truncate))
                m.write(QByteArray::fromStdString(json{{"commit", repo.commit}, {"a_medias", true}}.dump()));
        }
        // Dónde buscar archivos ya bajados: el modelo que se reemplaza primero y
        // después los demás instalados (los que tienen .ok).
        std::vector<fs::path> instalados{destino};
        for (const fs::directory_entry& e : fs::directory_iterator(staging.parent_path(), ec)) {
            const std::string n = e.path().filename().string();
            if (!n.empty() && n[0] != '.' && e.path() != destino && fs::exists(e.path() / ".ok", ec))
                instalados.push_back(e.path());
        }
        std::uint64_t hecho = 0;
        for (const ArchivoHf& a : archivos) {
            // Al cerrar la app (o cancelar) se corta acá, sin esperar a bajar el
            // siguiente: el cierre esperaba la bajada entera y el launcher lo
            // tomaba por un cuelgue (y reabría la app).
            if (ctx.cancelado()) return "cancelada";
            const fs::path final = staging / desdeUtf8(a.nombre);
            const QString nombre = QString::fromStdString(a.nombre);
            // Ya bajado en un intento anterior y verificado: queda.
            if (verificado(final, a)) {
                hecho += a.tamano;
                continue;
            }
            fs::create_directories(final.parent_path(), ec);
            // Lo que ya está bajado y verificado no se vuelve a bajar: se enlaza
            // (sin copiar gigas) desde el modelo instalado o desde otro que trae el
            // mismo archivo (las voces de piper comparten espeak-ng-data/ entera:
            // cientos de archivos chicos que se bajaban uno por uno).
            bool reusado = false;
            for (const fs::path& otro : instalados) {
                const fs::path previo = otro / desdeUtf8(a.nombre);
                if (fs::file_size(previo, ec) != a.tamano || ec || !verificado(previo, a)) continue;
                fs::remove(final, ec);
                fs::create_hard_link(previo, final, ec);
                if (ec) fs::copy_file(previo, final, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    reusado = true;
                    break;
                }
            }
            if (reusado) {
                hecho += a.tamano;
                continue;
            }
            // Por commit, no por «main»: todos los archivos de la MISMA versión.
            const std::string url = !a.url.empty()
                                        ? a.url
                                        : "https://huggingface.co/" + r + "/resolve/" + repo.commit + "/" + enUrl(a.nombre);
            const std::string e = ctx.bajar(url, final, hecho, total, a.nombre);
            if (!e.empty()) return ctx.cancelado() ? std::string("cancelada") : e;
            avisar(static_cast<double>(hecho + a.tamano) / static_cast<double>(std::max<std::uint64_t>(total, 1)) * 0.98,
                   tr("Verificando %1…").arg(nombre));
            if (!verificado(final, a)) {
                fs::remove(final, ec); // éste sí de cero
                return tr("El archivo %1 no coincide con el hash publicado. No se instaló nada.").arg(QString::fromStdString(a.nombre)).toStdString();
            }
            hecho += a.tamano;
        }
        const json ok{{"repo", r},
                      {"commit", repo.commit},
                      {"archivos", archivos.size()},
                      {"fecha", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()}};
        escribir(deRuta(staging / ".ok"),
                 QByteArray::fromStdString(ok.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
        // Si el commit fijado no traía modelo.json pero se validó uno con los
        // MISMOS pesos (ver revisarHerramientas), viaja con el modelo.
        if (!fs::exists(staging / "modelo.json", ec)) {
            QFile g(deRuta(fichas / (carpetaDeRepo(r) + ".json")));
            if (g.open(QIODevice::ReadOnly)) {
                const json gj = json::parse(g.readAll().toStdString(), nullptr, false);
                if (gj.is_object() && gj.value("modelo_json", json()).is_object())
                    escribir(deRuta(staging / "modelo.json"),
                             QByteArray::fromStdString(
                                 gj["modelo_json"].dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
            }
        }
        // Recién ahora se reemplaza el que hubiera (los enlaces duros
        // mantienen vivos los archivos reusados).
        fs::remove_all(destino, ec);
        fs::rename(staging, destino, ec);
        if (ec) return tr("No se pudo dejar el modelo en su lugar: %1").arg(QString::fromStdString(ec.message())).toStdString();
        return {};
    };
    p.alTerminar = [yo, qrepo](bool ok, const std::string& error) {
        if (!yo) return;
        yo->enCurso_.erase(qrepo);
        if (ok) yo->revisarHerramientas(qrepo, true);
        yo->descargando_ = !yo->enCurso_.empty();
        const QString e = error == "cancelada" ? tr("Descarga cancelada.") : QString::fromStdString(error);
        if (!yo->descargando_) {
            yo->progreso_ = ok ? 1.0 : 0.0;
            yo->textoDescarga_ = ok ? tr("Listo.") : e;
        }
        emit yo->descargaCambio();
        emit yo->cambio();
        emit yo->notify(ok ? tr("Modelo bajado y verificado: %1").arg(qrepo) : e, ok ? "ok" : "error");
        yo->revisarDisco();
    };
    return p;
}

void ModelosLocales::cancelarDescarga(const QString& repo) {
    for (const QString& r : std::set<QString>(enCurso_))
        if (repo.isEmpty() || r == repo) net::Descargas::la().cancelar("modelo:" + r);
}

void ModelosLocales::registrarActualizaciones() {
    // Los modelos instalados que tienen algo más nuevo: los fijados, si el
    // catálogo fija otro commit; los que siguen «main», si Hugging Face tiene otro.
    QPointer<ModelosLocales> yo(this);
    struct Inst {
        QString repo;
        std::string commit, fijo, titulo;
        std::optional<net::Descargas::Pedido> pedido;
    };
    net::Descargas::la().registrarBuscador("modelo", [yo]() {
        std::vector<Inst> lista;
        if (!yo) return std::vector<net::Descargas::Actualizacion>{};
        // Lo del catálogo y del disco se lee en el hilo de la interfaz.
        QMetaObject::invokeMethod(
            yo.data(),
            [yo, &lista]() {
                if (!yo) return;
                for (const QVariant& v : yo->catalogo()) {
                    const QVariantMap m = v.toMap();
                    const QString repo = m["repo"].toString();
                    const std::string c = commitInstalado(yo->carpetaDe(repo));
                    if (c.empty()) continue;
                    const json e = yo->entradaCatalogo(repo);
                    Inst i{repo, c, e.contains("url") ? std::string("url") : e.value("revision", std::string()),
                           e.value("nombre", repo.toStdString()), yo->pedidoDe(repo, true)};
                    if (i.pedido) lista.push_back(std::move(i));
                }
            },
            Qt::BlockingQueuedConnection);
        std::vector<net::Descargas::Actualizacion> out;
        for (auto& i : lista) {
            std::string nueva;
            if (!i.fijo.empty()) {
                if (i.fijo != i.commit) nueva = i.fijo;
            } else {
                net::HttpRequest req;
                req.url = "https://huggingface.co/api/models/" + i.repo.toStdString();
                req.timeoutMs = ba::ajustes::sAMs("modelos.timeoutHfS", 20) * 3 / 4;
                const net::HttpResponse res = net::HttpsClient::request(req);
                const json j = json::parse(res.body, nullptr, false);
                if (res.status == 200 && j.is_object()) {
                    const std::string sha = j.value("sha", std::string());
                    if (!sha.empty() && sha != i.commit) nueva = sha;
                }
            }
            if (nueva.empty()) continue;
            net::Descargas::Actualizacion a;
            a.actual = i.commit.substr(0, 12);
            a.detalle = tr("Hay una versión más nueva de %1").arg(QString::fromStdString(i.titulo)).toStdString();
            a.pedido = std::move(*i.pedido);
            a.pedido.version = nueva.substr(0, 12);
            out.push_back(std::move(a));
        }
        return out;
    });
}

void ModelosLocales::borrar(const QString& repo) {
    if (repo == activo_) apagar();
    std::error_code ec;
    fs::remove_all(carpetaDe(repo), ec);
    if (repo == activo_) {
        activo_.clear();
        guardarAjustes();
    }
    emit cambio();
    revisarDisco();
}

// --- lo que hay en el disco ------------------------------------------------------

namespace {

std::uintmax_t bytesDe(const fs::path& dir) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), fin;
         !ec && it != fin; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

/// La familia de un repo: sin la versión («-v3»). «…-4b-mlx-q4», «…-4b-v2-mlx-q4» y
/// «…-4b-v3-mlx-q4» son la misma; «…-4b-mtp-ud-mlx-q4» es otra.
std::pair<std::string, int> familiaYVersion(const std::string& repo) {
    static const std::regex v("-v([0-9]+)(?=-|$)");
    std::smatch m;
    if (std::regex_search(repo, m, v)) return {m.prefix().str() + m.suffix().str(), std::stoi(m[1].str())};
    return {repo, 1};
}

std::string tipoPorNombre(const std::string& repo) {
    if (repo.rfind("argos/", 0) == 0) return "traduccion";
    if (repo.find("vits-piper") != std::string::npos) return "tts";
    if (repo.find("silero") != std::string::npos) return "vad";
    if (repo.find("nemotron") != std::string::npos && repo.find("streaming") != std::string::npos) return "voz_streaming";
    if (repo.find("mmproj") != std::string::npos) return "vision";
    if (repo.find("sherpa") != std::string::npos || repo.find("parakeet") != std::string::npos) return "voz";
    return "reactivo";
}

} // namespace

void ModelosLocales::revisarDisco() {
    // En un hilo aparte: recorre gigas (miles de archivos con las voces) y con un
    // disco USB trabado colgaba la interfaz. Si ya hay una vuelta andando, se
    // repite al terminar.
    if (discoAndando_) {
        discoOtraVez_ = true;
        return;
    }
    discoAndando_ = true;
    if (hiloDisco_.joinable()) hiloDisco_.join();
    std::set<std::string> bajando; // las que están bajando ahora no son restos
    for (const QString& r : enCurso_) bajando.insert(".bajando-" + carpetaDeRepo(r.toStdString()));
    const QString activo = activo_;
    QPointer<ModelosLocales> yo(this);
    hiloDisco_ = std::thread([this, yo, bajando, activo]() {
        auto [lista, restos] = calcularDisco(bajando, activo);
        QMetaObject::invokeMethod(this, [yo, lista, restos]() {
            if (!yo) return;
            yo->instalados_ = lista;
            yo->restos_ = restos;
            yo->discoAndando_ = false;
            emit yo->discoCambio();
            if (yo->discoOtraVez_) {
                yo->discoOtraVez_ = false;
                yo->revisarDisco();
            }
        });
    });
}

std::pair<QVariantList, QVariantMap> ModelosLocales::calcularDisco(const std::set<std::string>& bajando,
                                                                   const QString& activo) const {
    QVariantList lista;
    int restos = 0;
    std::uintmax_t bytesRestos = 0;
    std::error_code ec;
    const fs::path raiz = carpetaModelos();
    struct Fila {
        QVariantMap m;
        std::string familia, formato;
        int version = 1;
    };
    std::vector<Fila> filas;
    for (const fs::directory_entry& d : fs::directory_iterator(raiz, ec)) {
        if (!d.is_directory(ec)) continue;
        const std::string nombre = d.path().filename().string();
        if (nombre.rfind(".bajando-", 0) == 0) {
            // La que está bajando ahora no es un resto.
            if (bajando.count(nombre) == 0) {
                ++restos;
                bytesRestos += bytesDe(d.path());
            }
            continue;
        }
        // Solo las carpetas de un repo («dueño__nombre»): lo demás (superres/,
        // .fichas) es de otros y no se toca desde acá.
        if (nombre.empty() || nombre[0] == '.' || nombre.find("__") == std::string::npos) continue;
        std::string repo;
        std::string fecha;
        bool completo = false;
        {
            QFile f(deRuta(d.path() / ".ok"));
            if (f.open(QIODevice::ReadOnly)) {
                completo = true;
                const json j = json::parse(f.readAll().toStdString(), nullptr, false);
                if (j.is_object()) {
                    repo = j.value("repo", std::string());
                    fecha = j.value("fecha", std::string());
                }
            }
        }
        if (repo.empty()) {
            repo = nombre;
            const auto p = repo.find("__");
            repo.replace(p, 2, "/");
        }
        const json e = entradaCatalogo(QString::fromStdString(repo));
        const bool enCatalogo = !e.empty();
        Fila f;
        f.m = QVariantMap{{"carpeta", QString::fromStdString(nombre)},
                          {"repo", QString::fromStdString(repo)},
                          {"tipo", QString::fromStdString(enCatalogo ? e.value("tipo", std::string("reactivo"))
                                                                     : tipoPorNombre(repo))},
                          {"bytes", static_cast<double>(bytesDe(d.path()))},
                          {"enCatalogo", enCatalogo},
                          {"completo", completo},
                          {"activo", QString::fromStdString(repo) == activo},
                          {"fecha", QString::fromStdString(fecha.substr(0, 10))}};
        std::tie(f.familia, f.version) = familiaYVersion(repo);
        f.formato = enCatalogo ? e.value("formato", std::string()) : std::string();
        filas.push_back(std::move(f));
    }
    for (Fila& f : filas) {
        QString motivo;
        const bool reactivo = f.m["tipo"].toString() == "reactivo";
        if (!f.m["completo"].toBool()) {
            motivo = tr("quedó a medio instalar");
        } else if (reactivo && !f.m["activo"].toBool()) {
            if (!f.m["enCatalogo"].toBool()) motivo = tr("ya no está en el catálogo");
            for (const Fila& o : filas)
                if (&o != &f && o.familia == f.familia && o.version > f.version && o.m["completo"].toBool())
                    motivo = tr("está bajada la v%1").arg(o.version);
        }
        f.m["viejo"] = !motivo.isEmpty();
        f.m["motivo"] = motivo;
        lista.push_back(f.m);
    }
    std::sort(lista.begin(), lista.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap()["bytes"].toDouble() > b.toMap()["bytes"].toDouble();
    });
    return {lista, QVariantMap{{"cuantas", restos}, {"bytes", static_cast<double>(bytesRestos)}}};
}

bool ModelosLocales::borrarCarpeta(const QString& carpeta) {
    const std::string n = carpeta.toStdString();
    const bool resto = n.rfind(".bajando-", 0) == 0;
    if (n.empty() || n.find('/') != std::string::npos || n.find('\\') != std::string::npos || n == "." ||
        n == ".." || (!resto && (n[0] == '.' || n.find("__") == std::string::npos)))
        return false;
    if (resto && descargando_) return false; // podría ser la que está bajando
    std::error_code ec;
    const fs::path dir = carpetaModelos() / n;
    if (!fs::is_directory(dir, ec)) return false;
    // Si es el activo, primero se apaga (el motor tiene los archivos abiertos).
    for (const QVariant& v : instalados_) {
        const QVariantMap m = v.toMap();
        if (m["carpeta"].toString() == carpeta && m["repo"].toString() == activo_) {
            apagar();
            activo_.clear();
            guardarAjustes();
            emit cambio();
        }
    }
    fs::remove_all(dir, ec);
    emit cambio();
    revisarDisco();
    return !ec;
}

QVariantMap ModelosLocales::limpiarViejos() {
    // Con lo de la última revisión (revisarDisco va en otro hilo).
    QStringList borrados;
    double bytes = 0;
    for (const QVariant& v : QVariantList(instalados_)) {
        const QVariantMap m = v.toMap();
        if (!m["viejo"].toBool() || m["activo"].toBool()) continue;
        std::error_code ec;
        fs::remove_all(carpetaModelos() / m["carpeta"].toString().toStdString(), ec);
        if (ec) continue;
        borrados << m["repo"].toString();
        bytes += m["bytes"].toDouble();
    }
    emit cambio();
    revisarDisco();
    return {{"borrados", borrados}, {"bytes", bytes}};
}

QVariantMap ModelosLocales::limpiarRestos() {
    int borrados = 0;
    double bytes = 0;
    if (!descargando_) {
        std::error_code ec;
        for (const fs::directory_entry& d : fs::directory_iterator(carpetaModelos(), ec)) {
            const std::string nombre = d.path().filename().string();
            if (!d.is_directory(ec) || nombre.rfind(".bajando-", 0) != 0) continue;
            bytes += static_cast<double>(bytesDe(d.path()));
            std::error_code e2;
            fs::remove_all(d.path(), e2);
            if (!e2) ++borrados;
        }
    }
    revisarDisco();
    return {{"borrados", borrados}, {"bytes", bytes}};
}

// --- el motor -------------------------------------------------------------------

void ModelosLocales::setEstado(const QString& estado, const QString& detalle) {
    estado_ = estado;
    detalle_ = detalle;
    emit cambio();
}

void ModelosLocales::activar(const QString& repo) {
    if (entradaCatalogo(repo).value("tipo", std::string("reactivo")) != "reactivo")
        return; // la voz se usa sola
    if (!instalado(repo)) {
        emit notify(tr("Primero hay que bajar el modelo."), "error");
        return;
    }
    if (!hayMotor()) {
        setEstado("error", motor());
        return;
    }
    apagar();
    activo_ = repo;
    guardarAjustes();
    info_.clear();
    setEstado("cargando", tr("Cargando el modelo y leyendo las fichas…"));

    proc_ = new QProcess(this);
    proc_->setProgram(exeMotor(dirBinarios_));
    proc_->setArguments({"--modelo", deRuta(carpetaDe(repo)), "--mtp", mtp_});
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
        pendiente_ += proc_->readAllStandardOutput();
        int i;
        while ((i = pendiente_.indexOf('\n')) >= 0) {
            const QByteArray l = pendiente_.left(i);
            pendiente_.remove(0, i + 1);
            if (!l.trimmed().isEmpty()) linea(l);
        }
    });
    // stderr: diagnóstico del motor; se guarda lo último por si se cae.
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray e = proc_->readAllStandardError().trimmed();
        if (!e.isEmpty()) detalleError_ = QString::fromUtf8(e.right(400));
    });
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const bool esperado = estado_ == "apagado";
        fallanTodos(tr("el motor se cerró"));
        proc_->deleteLater();
        proc_ = nullptr;
        if (!esperado)
            setEstado("error", tr("El motor se cerró (código %1). %2").arg(code).arg(detalleError_));
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) setEstado("error", tr("No se pudo lanzar ba-modelo."));
    });
    proc_->start();
}

void ModelosLocales::apagar() {
    if (proc_ == nullptr) {
        if (estado_ != "apagado") setEstado("apagado", {});
        return;
    }
    estado_ = "apagado";
    proc_->write("{\"op\":\"salir\"}\n");
    if (!proc_->waitForFinished(2000)) proc_->kill();
    setEstado("apagado", {});
}

void ModelosLocales::linea(const QByteArray& l) {
    const json j = json::parse(l.toStdString(), nullptr, false);
    if (!j.is_object()) return; // el motor solo escribe JSON; lo demás se ignora
    const std::string ev = j.value("ev", std::string());
    if (ev == "listo") {
        info_ = aVariant(j);
        QString d = tr("Listo · prefijo de %1 tokens en %2 s")
                        .arg(j.value("tokens_prefijo", 0))
                        .arg(j.value("ms_prefijo", 0.0) / 1000.0, 0, 'f', 1);
        const std::string m = j.value("mtp", std::string("no"));
        d += " · " + (m == "no" ? tr("sin MTP") : tr("cabeza MTP %1").arg(QString::fromStdString(m)));
        d += " · " + (j.value("vision", false) ? tr("con visión") : tr("solo texto"));
        setEstado("listo", d);
        return;
    }
    if (ev == "aviso") {
        emit notify(tr("Modelo local: %1").arg(QString::fromStdString(j.value("aviso", std::string()))), "warn");
        return;
    }
    if (ev == "error") {
        setEstado("error", QString::fromStdString(j.value("error", std::string())));
        return;
    }
    if (!j.contains("id") || !j["id"].is_number_integer()) return;
    const auto it = esperas_.find(j["id"].get<std::int64_t>());
    if (it == esperas_.end()) return;
    if (j.contains("delta")) {
        if (it->second.delta) it->second.delta(QString::fromStdString(j.value("delta", std::string())));
        return;
    }
    Respuesta fin = std::move(it->second.fin);
    esperas_.erase(it);
    if (fin) fin(j);
}

void ModelosLocales::pedir(json pedido, Respuesta r, std::function<void(const QString&)> delta) {
    if (proc_ == nullptr || estado_ != "listo") {
        if (r) r(json{{"error", tr("sin modelo").toStdString()}});
        return;
    }
    const std::int64_t id = proximoId_++;
    pedido["id"] = id;
    esperas_[id] = Espera{std::move(r), std::move(delta)};
    proc_->write(QByteArray::fromStdString(pedido.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) + '\n');
}

void ModelosLocales::fallanTodos(const QString& error) {
    auto esperas = std::move(esperas_);
    esperas_.clear();
    for (auto& [id, e] : esperas)
        if (e.fin) e.fin(json{{"error", error.toStdString()}});
}

void ModelosLocales::elegir(const QString& frase, Respuesta r) {
    pedir(json{{"op", "elegir"}, {"frase", frase.toStdString()}, {"sesgo", sesgo_}}, std::move(r));
}

void ModelosLocales::llamar(const QString& estadoJson, const QString& frase, Respuesta r) {
    json pedido{{"op", "llamar"}, {"frase", frase.toStdString()}, {"sesgo", sesgo_}, {"max", 256}};
    if (!estadoJson.isEmpty()) pedido["estado"] = estadoJson.toStdString();
    pedir(std::move(pedido), std::move(r));
}

void ModelosLocales::preparar(const QString& estadoJson, Respuesta r) {
    pedir(json{{"op", "preparar"}, {"estado", estadoJson.toStdString()}}, std::move(r));
}

void ModelosLocales::generar(const QString& texto, bool conPrefijo, int maxTokens,
                             std::function<void(const QString&)> delta, Respuesta fin) {
    pedir(
        json{{"op", "generar"}, {"texto", texto.toStdString()}, {"prefijo", conPrefijo}, {"max", maxTokens}},
        std::move(fin), std::move(delta));
}

void ModelosLocales::ver(const QString& imagen, const QString& pregunta, int maxTokens,
                         std::function<void(const QString&)> delta, Respuesta fin) {
    if (listo() && !conVision()) {
        if (fin) fin(json{{"error", tr("el modelo activo no trae visión").toStdString()}});
        return;
    }
    pedir(json{{"op", "ver"},
               {"imagen", imagen.toStdString()},
               {"texto", pregunta.toStdString()},
               {"max", maxTokens}},
          std::move(fin), std::move(delta));
}

void ModelosLocales::probarVision(const QString& imagen, const QString& pregunta) {
    const QString ruta = imagen.startsWith("file:") ? QUrl(imagen).toLocalFile() : imagen;
    const QString p = pregunta.trimmed().isEmpty() ? "¿Qué se ve? Una o dos oraciones." : pregunta.trimmed();
    ver(ruta, p, 160, {}, [this, ruta, p](const json& j) {
        QVariantMap v = aVariant(j);
        v["frase"] = "🖼 " + QFileInfo(ruta).fileName() + ": " + p;
        v["vision"] = true;
        pruebas_.prepend(v);
        while (pruebas_.size() > kPruebasGuardadas)
            pruebas_.removeLast();
        emit pruebasCambio();
    });
}

void ModelosLocales::probarFrase(const QString& frase, Respuesta r) {
    if (formato() != "tc" || !probadorTc_) return elegir(frase, std::move(r));
    probadorTc_(frase, [r](const json& j) {
        if (j.contains("error")) return r(j);
        json out{{"tipo", j.value("tipo", std::string())},
                 {"crudo", j.value("crudo", std::string())},
                 {"prob", j.value("prob", 0.0)},
                 {"ms", j.value("ms", 0.0)}};
        const argumentos::LlamadaTc l = argumentos::leerTc(j.value("crudo", std::string()));
        if (!l.hay) {
            out["herramienta"] = "";
            out["derivar"] = false;
            out["resumen"] = "🗣 " + j.value("texto", std::string());
            return r(out);
        }
        std::string args;
        for (const auto& [k, v] : l.parametros)
            args += (args.empty() ? "" : ", ") + k + ": " + v;
        out["herramienta"] = l.funcion;
        out["derivar"] = l.funcion == "derivar";
        out["resumen"] = l.funcion + "(" + args + ")";
        r(out);
    });
}

void ModelosLocales::probar(const QString& frase) {
    const QString f = frase.trimmed();
    if (f.isEmpty()) return;
    probarFrase(f, [this, f](const json& j) { anotarPrueba(f, j); });
}

void ModelosLocales::anotarPrueba(const QString& frase, const json& j) {
    QVariantMap v = aVariant(j);
    v["frase"] = frase;
    pruebas_.prepend(v);
    while (pruebas_.size() > kPruebasGuardadas)
        pruebas_.removeLast();
    emit pruebasCambio();
}

} // namespace ba::app::agent
