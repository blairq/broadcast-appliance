// ============================================================================
// src/app/agent/ModelosLocales.cpp — ver ModelosLocales.hpp
// ============================================================================
#include "agent/ModelosLocales.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

#include "net/HttpsClient.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ba::app::agent {

namespace {

constexpr int kPruebasGuardadas = 20;

QString deRuta(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

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
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
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
}

ModelosLocales::~ModelosLocales() {
    cancelar_ = true;
    if (hiloDescarga_.joinable()) hiloDescarga_.join();
    if (proc_ != nullptr) {
        proc_->disconnect(this);
        proc_->kill();
        proc_->waitForFinished(3000);
    }
}

fs::path ModelosLocales::carpetaModelos() const { return raiz_ / "Modelos"; }

fs::path ModelosLocales::carpetaDe(const QString& repo) const {
    return carpetaModelos() / carpetaDeRepo(repo.toStdString());
}

bool ModelosLocales::instalado(const QString& repo) const {
    std::error_code ec;
    return fs::exists(carpetaDe(repo) / ".ok", ec);
}

nlohmann::json ModelosLocales::entradaCatalogo(const QString& repo) const {
    for (const json& m : catalogo_.value("modelos", json::array()))
        if (m.value("repo", std::string()) == repo.toStdString()) return m;
    return json::object();
}

QVariantList ModelosLocales::catalogo() const {
    QVariantList out;
    const std::string plat = plataformaCatalogo();
    for (const json& m : catalogo_.value("modelos", json::array())) {
        const json plats = m.value("plataformas", json::array());
        if (std::find(plats.begin(), plats.end(), plat) == plats.end()) continue;
        QVariantMap v = aVariant(m);
        const QString repo = QString::fromStdString(m.value("repo", std::string()));
        v["instalado"] = instalado(repo);
        v["activo"] = repo == activo_;
        out.push_back(v);
    }
    return out;
}

bool ModelosLocales::hayMotor() const {
#if defined(__APPLE__)
    return QFileInfo(dirBinarios_ + "/ba-modelo").isExecutable();
#else
    return false;
#endif
}

QString ModelosLocales::motor() const {
#if defined(__APPLE__)
    return hayMotor() ? "MLX (ba-modelo)" : "falta ba-modelo en bin/64bit (make modelo-mac)";
#else
    return "llama.cpp: todavía no viaja en el bundle de esta plataforma";
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
    escribir(deRuta(carpetaModelos() / "ajustes.json"), QByteArray::fromStdString(j.dump(1)));
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

// --- descarga ----------------------------------------------------------------------

void ModelosLocales::descargar(const QString& repo) {
    if (descargando_) return;
    const json entrada = entradaCatalogo(repo);
    if (entrada.empty()) {
        emit notify("Ese modelo no está en el catálogo.", "error");
        return;
    }
    // GGUF: un repo puede traer varias cuantizaciones; se baja la del catálogo.
    const std::string soloGguf =
        entrada.value("archivo", json()).is_string() ? entrada["archivo"].get<std::string>() : "";
    descargando_ = true;
    cancelar_ = false;
    progreso_ = 0;
    textoDescarga_ = "Consultando Hugging Face…";
    emit descargaCambio();
    if (hiloDescarga_.joinable()) hiloDescarga_.join();
    const fs::path destino = carpetaDe(repo);
    const fs::path staging = carpetaModelos() / (".bajando-" + carpetaDeRepo(repo.toStdString()));
    const std::string r = repo.toStdString();

    hiloDescarga_ = std::thread([this, r, soloGguf, destino, staging]() {
        const auto avisar = [this](double p, const QString& t) {
            QMetaObject::invokeMethod(this, [this, p, t]() {
                progreso_ = p;
                textoDescarga_ = t;
                emit descargaCambio();
            });
        };
        QString err;
        RepoHf repo;
        {
            net::HttpRequest req;
            req.url = "https://huggingface.co/api/models/" + r + "?blobs=true";
            req.timeoutMs = 30000;
            const net::HttpResponse res = net::HttpsClient::request(req);
            if (!res.error.empty())
                err = "Sin conexión con Hugging Face: " + QString::fromStdString(res.error);
            else if (res.status == 401 || res.status == 404)
                err = "El modelo no está publicado (o es privado) en Hugging Face.";
            else if (res.status != 200)
                err = QString("Hugging Face contestó %1.").arg(res.status);
            else
                repo = leerRepoHf(res.body);
            if (err.isEmpty() && (repo.commit.empty() || repo.archivos.empty()))
                err = "No se entendió la respuesta de Hugging Face.";
        }
        std::vector<ArchivoHf> archivos;
        std::uint64_t total = 0;
        for (const ArchivoHf& a : repo.archivos) {
            if (a.nombre == ".gitattributes") continue;
            const bool gguf = a.nombre.size() > 5 && a.nombre.substr(a.nombre.size() - 5) == ".gguf";
            if (gguf && !soloGguf.empty() && a.nombre != soloGguf) continue;
            archivos.push_back(a);
            total += a.tamano;
        }
        std::error_code ec;
        fs::remove_all(staging, ec);
        fs::create_directories(staging, ec);
        std::uint64_t hecho = 0;
        for (const ArchivoHf& a : archivos) {
            if (!err.isEmpty()) break;
            const fs::path final = staging / desdeUtf8(a.nombre);
            fs::create_directories(final.parent_path(), ec);
            // Lo que ya está bajado y verificado no se vuelve a bajar: se enlaza
            // (sin copiar gigas) desde el modelo instalado.
            const fs::path previo = destino / desdeUtf8(a.nombre);
            if (verificado(previo, a)) {
                fs::create_hard_link(previo, final, ec);
                if (ec) fs::copy_file(previo, final, fs::copy_options::overwrite_existing, ec);
                if (!ec) {
                    hecho += a.tamano;
                    continue;
                }
            }
            net::HttpRequest req;
            // Por commit, no por «main»: todos los archivos de la MISMA versión.
            req.url = "https://huggingface.co/" + r + "/resolve/" + repo.commit + "/" + a.nombre;
            req.responseFile = final.string() + ".parte";
            req.timeoutMs = 60000;
            req.cancel = &cancelar_;
            const std::uint64_t base = hecho;
            const QString nombre = QString::fromStdString(a.nombre);
            req.downloadProgress = [&avisar, base, total, nombre](std::uint64_t got, std::uint64_t) {
                const double p = total > 0 ? static_cast<double>(base + got) / static_cast<double>(total) : 0;
                avisar(p * 0.98, QString("Bajando %1… %2 / %3 MB")
                                     .arg(nombre)
                                     .arg(static_cast<double>(base + got) / 1048576.0, 0, 'f', 0)
                                     .arg(static_cast<double>(total) / 1048576.0, 0, 'f', 0));
            };
            const net::HttpResponse res = net::HttpsClient::download(req);
            if (!res.error.empty() || res.status / 100 != 2) {
                err = cancelar_ ? "Descarga cancelada."
                                : "No se pudo bajar " + nombre + ": " +
                                      (res.error.empty() ? QString("HTTP %1").arg(res.status)
                                                         : QString::fromStdString(res.error));
                break;
            }
            fs::rename(final.string() + ".parte", final, ec);
            avisar(static_cast<double>(hecho + a.tamano) /
                       static_cast<double>(std::max<std::uint64_t>(total, 1)) * 0.98,
                   "Verificando " + nombre + "…");
            if (!verificado(final, a)) {
                err = "El archivo " + nombre + " no coincide con el hash publicado. No se instaló nada.";
                break;
            }
            hecho += a.tamano;
        }
        if (err.isEmpty()) {
            const json ok{{"repo", r},
                          {"commit", repo.commit},
                          {"archivos", archivos.size()},
                          {"fecha", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()}};
            escribir(deRuta(staging / ".ok"), QByteArray::fromStdString(ok.dump(1)));
            // Recién ahora se reemplaza el que hubiera (los enlaces duros
            // mantienen vivos los archivos reusados).
            fs::remove_all(destino, ec);
            fs::rename(staging, destino, ec);
            if (ec) err = "No se pudo dejar el modelo en su lugar: " + QString::fromStdString(ec.message());
        }
        if (!err.isEmpty()) fs::remove_all(staging, ec);
        QMetaObject::invokeMethod(this, [this, err]() {
            descargando_ = false;
            progreso_ = err.isEmpty() ? 1.0 : 0.0;
            textoDescarga_ = err.isEmpty() ? "Listo." : err;
            emit descargaCambio();
            emit cambio();
            emit notify(err.isEmpty() ? "Modelo bajado y verificado." : err, err.isEmpty() ? "ok" : "error");
        });
    });
}

void ModelosLocales::cancelarDescarga() { cancelar_ = true; }

void ModelosLocales::borrar(const QString& repo) {
    if (repo == activo_) apagar();
    std::error_code ec;
    fs::remove_all(carpetaDe(repo), ec);
    if (repo == activo_) {
        activo_.clear();
        guardarAjustes();
    }
    emit cambio();
}

// --- el motor -------------------------------------------------------------------

void ModelosLocales::setEstado(const QString& estado, const QString& detalle) {
    estado_ = estado;
    detalle_ = detalle;
    emit cambio();
}

void ModelosLocales::activar(const QString& repo) {
    if (!instalado(repo)) {
        emit notify("Primero hay que bajar el modelo.", "error");
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
    setEstado("cargando", "Cargando el modelo y leyendo las fichas…");

    proc_ = new QProcess(this);
    proc_->setProgram(dirBinarios_ + "/ba-modelo");
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
        fallanTodos("el motor se cerró");
        proc_->deleteLater();
        proc_ = nullptr;
        if (!esperado)
            setEstado("error", QString("El motor se cerró (código %1). %2").arg(code).arg(detalleError_));
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) setEstado("error", "No se pudo lanzar ba-modelo.");
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
        QString d = QString("Listo · prefijo de %1 tokens en %2 s")
                        .arg(j.value("tokens_prefijo", 0))
                        .arg(j.value("ms_prefijo", 0.0) / 1000.0, 0, 'f', 1);
        const std::string m = j.value("mtp", std::string("no"));
        d += m == "no" ? " · sin MTP" : " · cabeza MTP " + QString::fromStdString(m);
        d += j.value("vision", false) ? " · con visión" : " · solo texto";
        setEstado("listo", d);
        return;
    }
    if (ev == "aviso") {
        emit notify("Modelo local: " + QString::fromStdString(j.value("aviso", std::string())), "warn");
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
        if (r) r(json{{"error", "sin modelo"}});
        return;
    }
    const std::int64_t id = proximoId_++;
    pedido["id"] = id;
    esperas_[id] = Espera{std::move(r), std::move(delta)};
    proc_->write(QByteArray::fromStdString(pedido.dump()) + '\n');
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

void ModelosLocales::generar(const QString& texto, bool conPrefijo, int maxTokens,
                             std::function<void(const QString&)> delta, Respuesta fin) {
    pedir(
        json{{"op", "generar"}, {"texto", texto.toStdString()}, {"prefijo", conPrefijo}, {"max", maxTokens}},
        std::move(fin), std::move(delta));
}

void ModelosLocales::ver(const QString& imagen, const QString& pregunta, int maxTokens,
                         std::function<void(const QString&)> delta, Respuesta fin) {
    if (listo() && !conVision()) {
        if (fin) fin(json{{"error", "el modelo activo no trae visión"}});
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

void ModelosLocales::probar(const QString& frase) {
    const QString f = frase.trimmed();
    if (f.isEmpty()) return;
    elegir(f, [this, f](const json& j) {
        QVariantMap v = aVariant(j);
        v["frase"] = f;
        pruebas_.prepend(v);
        while (pruebas_.size() > kPruebasGuardadas)
            pruebas_.removeLast();
        emit pruebasCambio();
    });
}

} // namespace ba::app::agent
