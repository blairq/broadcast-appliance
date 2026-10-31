// src/app/video/SuperRes.cpp — ver SuperRes.hpp

#include "video/SuperRes.hpp"

#include "platform/Paths.hpp"
#include "plugins/Hijo.hpp"
#include "ui/VideoViewModel.hpp"
#include "video/VideoGrafos.hpp"
#include "net/Descargas.hpp"

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

#include <QCryptographicHash>
#include <QDateTime>
#include <QCoreApplication>
#include <QImage>
#include <QPointer>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>

#include "platform/Herramientas.hpp"
#include <QTimer>

#include <fstream>
#include <iterator>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace ba::app::video {

namespace {
using json = nlohmann::json;
QString q8(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
QVariant qv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json::array({j}).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .toVariant()
        .toList()
        .value(0);
}
// Lo que baja el laboratorio: versiones fijas (CPU/MPS; en Windows con NVIDIA
// se puede cambiar por la de CUDA a mano).
const QStringList kBibliotecas{"torch==2.14.1", "spandrel==0.4.2", "onnx==1.23.1"};

std::string placaDeVideo(); // más abajo

/// En Windows con AMD o Intel, PyTorch entrena en la GPU con DirectML
/// (torch-directml): no hay ROCm ni CUDA ahí. torch-directml exige SU versión de
/// PyTorch (medido en la RX 5700 XT: 2.4.1 con torch-directml 0.2.5), así que
/// torch no se fija: lo trae pip con la que pide. Sin esto el laboratorio
/// entrenaba en la CPU (inusable) y la sección de AMD no andaba en Windows.
bool conDirectml() {
#if defined(_WIN32)
    const std::string p = placaDeVideo();
    return p == "amd" || p == "intel";
#else
    return false;
#endif
}

QStringList bibliotecas() {
    if (conDirectml()) return {"torch-directml", "spandrel==0.4.2", "onnx==1.23.1"};
    return kBibliotecas;
}

/// Los pesos de terceros que convierte el laboratorio (los mismos de su PESOS),
/// con su sha256: los baja la cola de descargas, verificados.
struct Pesos {
    const char* url;
    const char* archivo;
    const char* sha256;
};
const Pesos* pesosDe(const std::string& cual) {
    static const Pesos span{
        "https://objectstorage.us-phoenix-1.oraclecloud.com/n/ax6ygfvpvzka/b/open-modeldb-files/o/2x-spanx2-ch48.pth",
        "2x-spanx2-ch48.pth", "0b20caeea7af5507ec3bdc63ff5a141a28506279d2cfec909c417ecb308d3f93"};
    static const Pesos esrgan{"https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth",
                              "realesr-general-x4v3.pth",
                              "8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292"};
    return cual == "span" ? &span : cual == "esrgan" ? &esrgan : nullptr;
}

std::string sha256De(const fs::path& p) {
    QFile f(q8(p));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return h.result().toHex().toStdString();
}

/// En el hilo de la cola: baja `url` a `destino` (si no está ya, con ese hash) y lo verifica.
std::string bajarVerificado(net::Descargas::Contexto& ctx, const std::string& url, const fs::path& destino,
                            const std::string& sha) {
    std::error_code ec;
    if (fs::exists(destino, ec) && sha256De(destino) == sha) return {};
    const std::string e = ctx.bajar(url, destino);
    if (!e.empty()) return e;
    ctx.avance(0.95, QCoreApplication::translate("superres", "Verificando %1…")
                         .arg(QString::fromStdString(destino.filename().string()))
                         .toStdString());
    if (sha256De(destino) != sha) {
        fs::remove(destino, ec);
        return QCoreApplication::translate("superres", "el archivo bajado (%1) no coincide con el sha256 publicado")
            .arg(QString::fromStdString(destino.filename().string()))
            .toStdString();
    }
    return {};
}
} // namespace

SuperRes::SuperRes(fs::path dataRoot, VideoViewModel* video, QObject* parent)
    : QObject(parent), root_(std::move(dataRoot)), video_(video) {
    std::error_code ec;
    fs::create_directories(carpetaModelos(), ec);
    trabajo_ = {{"corriendo", false}};
    // Un ONNX Runtime con GPU (NVIDIA: CUDA/TensorRT; AMD en Linux:
    // ROCm/MIGraphX) bajado a Datos/Herramientas/onnxruntime-gpu: baonnxsr lo
    // carga en vez del del programa (variable BA_ORT_GPU, antes de que
    // GStreamer abra una red).
    if (qEnvironmentVariableIsEmpty("BA_ORT_GPU")) {
        const fs::path base = ba::platform::dataRoot() / "Herramientas" / "onnxruntime-gpu" / "lib";
#if defined(_WIN32)
        const fs::path lib = base / "onnxruntime.dll";
#else
        const fs::path lib = base / "libonnxruntime.so";
#endif
        std::error_code e2;
        if (fs::exists(lib, e2)) qputenv("BA_ORT_GPU", QByteArray::fromStdString(lib.string()));
    }
    // Las tarjetas guardan el id del plugin; al mandarlas a la cadena de la entrada se
    // traduce a lo de esta máquina (CoreML, ONNX…): VideoGrafos llama a resolverPlugin
    // desde el dueño (lo engancha ui/ImagenViewModel).
    // GStreamer arranca en otro hilo: hasta que registra los elementos, no hay
    // motores. Se mira cada 2 s hasta que aparecen (la interfaz se actualiza).
    auto* t = new QTimer(this);
    t->setInterval(2000);
    connect(t, &QTimer::timeout, this, [this, t]() {
        if (motores().isEmpty()) return;
        t->stop();
        t->deleteLater();
        emit cambio();
    });
    t->start();
}

SuperRes::~SuperRes() {
    // Lo que la cola de descargas espera de acá termina (cancelado): si no, su
    // hilo se quedaba esperando para siempre.
    for (auto& l : listosVivos_) l(tr("cancelada").toStdString());
    listosVivos_.clear();
    if (hijo_) {
        hijo_->disconnect(this);
        hijo_->terminar();
    }
}

const json& SuperRes::catalogo() {
    // El catálogo de recetas (datos/superres-catalogo.json, recurso de Qt): no cambia.
    static const json cat = []() {
        QFile f(QStringLiteral(":/video/superres-catalogo.json"));
        if (!f.open(QIODevice::ReadOnly)) return json::array();
        const json c = json::parse(f.readAll().toStdString(), nullptr, false);
        return c.is_object() && c.contains("plugins") && c["plugins"].is_array() ? c["plugins"] : json::array();
    }();
    return cat;
}

fs::path SuperRes::carpetaModelos() { return ba::platform::dataRoot() / "Modelos" / "superres"; }
fs::path SuperRes::libs() const { return ba::platform::dataRoot() / "Herramientas" / "superres-libs"; }

fs::path SuperRes::python() const {
#if defined(_WIN32)
    return ba::platform::bundleRoot() / "python" / "python.exe";
#else
    return ba::platform::bundleRoot() / "python" / "bin" / "python3";
#endif
}

bool SuperRes::laboratorioListo() const {
    std::error_code ec;
    if (!fs::exists(libs() / ".ok", ec)) return false;
    if (!conDirectml()) return true;
    // Uno armado antes sin DirectML (entrenaba en la CPU) se rehace.
    std::ifstream f(libs() / ".ok");
    const std::string marca((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return marca.find("torch-directml") != std::string::npos;
}

QStringList SuperRes::motores() const { return VideoGrafos::motoresSuperres(); }

json SuperRes::modelos() const {
    json out = json::array();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(carpetaModelos(), ec)) {
        if (e.path().extension() != ".onnx") continue;
        json m = json::object();
        std::ifstream f(e.path().string() + ".json");
        if (f) {
            json d = json::parse(f, nullptr, false);
            if (d.is_object()) m = d;
        }
        m["archivo"] = e.path().filename().string();
        m["ruta"] = e.path().string();
        m["tamano_mb"] = static_cast<double>(fs::file_size(e.path(), ec)) / 1048576.0;
        if (!m.contains("nombre")) m["nombre"] = e.path().stem().string();
        out.push_back(m);
    }
    return out;
}

QVariantList SuperRes::modelosQml() const { return qv(modelos()).toList(); }

std::string SuperRes::correr(const char* tipo, const QString& que, const QString& entrada, const QStringList& args,
                             std::function<void(bool)> fin) {
    if (hijo_ && hijo_->corriendo())
        return tr("ya hay un trabajo andando: «%1»").arg(trabajo_.value("que").toString()).toStdString();
    std::error_code ec;
    if (!fs::exists(python(), ec)) return tr("el programa no trae Python").toStdString();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : env.keys())
        if (k.startsWith("PYTHON")) env.remove(k);
    // El ffmpeg y el ffprobe del programa (herramientas/): el laboratorio los busca
    // en el PATH si no se los dicen, y en Windows ese PATH no los tiene.
    for (const auto& [var, nombre] : {std::pair{"BA_FFMPEG", "ffmpeg"}, std::pair{"BA_FFPROBE", "ffprobe"}}) {
        const fs::path h = ba::platform::herramienta(nombre);
        if (!h.empty()) env.insert(var, q8(h));
    }
    hijo_ = std::make_unique<plugins::Hijo>();
    connect(hijo_.get(), &plugins::Hijo::salida, this, [this]() {
        pendiente_ += hijo_->leerSalida();
        int i;
        while ((i = pendiente_.indexOf('\n')) >= 0) {
            linea(pendiente_.left(i));
            pendiente_.remove(0, i + 1);
        }
    });
    connect(hijo_.get(), &plugins::Hijo::errores, this, [this]() { hijo_->leerErrores(); });
    connect(hijo_.get(), &plugins::Hijo::termino, this, [this](int codigo, bool) {
        const bool ok = codigo == 0 && !trabajo_.contains("error");
        trabajo_["corriendo"] = false;
        if (!ok && !trabajo_.contains("error"))
            trabajo_["error"] = tr("terminó con código %1").arg(codigo);
        if (ok) trabajo_["pct"] = 100;
        emit trabajoCambio();
        emit cambio();
        auto f = std::move(fin_);
        fin_ = nullptr;
        if (f) QTimer::singleShot(0, this, [f = std::move(f), ok]() { f(ok); });
    });
    trabajo_ = {
        {"tipo", QString::fromLatin1(tipo)},
        {"que", que},
        {"entrada", entrada},
        {"corriendo", true},
        {"pct", 0},
        {"texto", tr("Arrancando…")},
        {"desde", QDateTime::currentDateTime().toString("HH:mm:ss")}};
    fin_ = std::move(fin);
    pendiente_.clear();
    QString err;
    if (!hijo_->iniciar(q8(python()), QStringList{"-I", "-B"} + args, env, q8(root_), false, &err)) {
        trabajo_["corriendo"] = false;
        trabajo_["error"] = err;
        emit trabajoCambio();
        return err.toStdString();
    }
    emit trabajoCambio();
    return {};
}

void SuperRes::linea(const QByteArray& l) {
    const json m = json::parse(l.toStdString(), nullptr, false);
    if (!m.is_object()) {
        // pip y compañía: la última línea como texto.
        const QString t = QString::fromUtf8(l).trimmed();
        if (!t.isEmpty()) {
            trabajo_["texto"] = t.left(200);
            if (ctxCola_ != nullptr) ctxCola_->avance(0, t.left(200).toStdString());
            emit trabajoCambio();
        }
        return;
    }
    for (const auto& [k, v] : m.items())
        trabajo_[QString::fromStdString(k)] = qv(v);
    if (ctxCola_ != nullptr)
        ctxCola_->avance(trabajo_.value("pct").toDouble() / 100.0, trabajo_.value("texto").toString().toStdString());
    if (m.value("ev", "") == "error")
        trabajo_["error"] = m.contains("error") ? QString::fromStdString(m.value("error", std::string())) : tr("falló");
    emit trabajoCambio();
}

json SuperRes::laboratorio() const {
    std::error_code ec;
    std::uintmax_t bytes = 0;
    if (fs::is_directory(libs(), ec))
        for (auto it = fs::recursive_directory_iterator(libs(), fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
            if (!ec && it->is_regular_file(ec)) bytes += it->file_size(ec);
    json bib = json::array();
    for (const QString& b : bibliotecas()) bib.push_back(b.toStdString());
    return {{"listo", laboratorioListo()},
            {"directml", conDirectml()},
            {"carpeta", libs().string()},
            {"mb", static_cast<double>(bytes / 1048576)},
            {"bibliotecas", bib}};
}

std::string SuperRes::borrarLaboratorio() {
    if (hijo_ && hijo_->corriendo()) return tr("hay un trabajo andando: cancelalo primero").toStdString();
    std::error_code ec;
    fs::remove_all(libs(), ec);
    emit cambio();
    return ec ? tr("no se pudo borrar: %1").arg(q(ec.message())).toStdString() : std::string();
}

// --- Lo que se baja: por la cola de descargas (net/Descargas) ------------------------------

std::string SuperRes::encolar(net::Descargas::Pedido p, std::function<std::string(net::Descargas::Contexto&)> bajar,
                              std::function<std::string(std::function<void(bool)>)> lanzar) {
    QPointer<SuperRes> yo(this);
    p.tipo = "superres";
    const std::string id = p.id;
    p.trabajo = [yo, bajar, lanzar, id](net::Descargas::Contexto& ctx) -> std::string {
        if (bajar) {
            const std::string e = bajar(ctx);
            if (!e.empty()) return e;
        }
        if (!lanzar || ctx.cancelado()) return ctx.cancelado() ? tr("cancelada").toStdString() : std::string();
        net::Descargas::Contexto* c = &ctx;
        return ctx.enLaInterfaz(
            [yo, lanzar, c, id](std::function<void(const std::string&)> listo) {
                if (!yo) return listo(tr("cancelada").toStdString());
                // Para avisarle si esto se destruye antes (listo no avisa dos veces).
                yo->listosVivos_.push_back(listo);
                yo->esperarYLanzar(lanzar, listo, c, id);
            },
            [yo, c]() {
                if (yo && yo->ctxCola_ == c) yo->cortarHijo(); // solo si es el suyo el que anda
            });
    };
    const auto r = net::Descargas::la().pedir(std::move(p));
    return r == net::Descargas::Resultado::Invalida ? tr("pedido inválido").toStdString() : std::string();
}

void SuperRes::esperarYLanzar(std::function<std::string(std::function<void(bool)>)> lanzar,
                              std::function<void(const std::string&)> listo, net::Descargas::Contexto* c,
                              const std::string& id) {
    if (c->cancelado()) return listo(tr("cancelada").toStdString());
    // El laboratorio corre de a un proceso: si está entrenando (u otro), espera.
    if (hijo_ && hijo_->corriendo()) {
        c->avance(0, tr("Esperando que termine «%1»").arg(trabajo_.value("que").toString()).toStdString());
        QTimer::singleShot(2000, this, [this, lanzar, listo, c, id]() { esperarYLanzar(lanzar, listo, c, id); });
        return;
    }
    ctxCola_ = c;
    idCola_ = id;
    listoCola_ = listo;
    const std::string err = lanzar([this](bool ok) {
        auto l = std::move(listoCola_);
        listoCola_ = nullptr;
        ctxCola_ = nullptr;
        idCola_.clear();
        const QString e = trabajo_.value("error").toString();
        if (l) l(ok ? std::string() : (e.isEmpty() ? std::string("falló") : e.toStdString()));
    });
    if (!err.empty()) {
        listoCola_ = nullptr;
        ctxCola_ = nullptr;
        idCola_.clear();
        listo(err);
    }
}

std::string SuperRes::prepararLaboratorio() { return prepararLaboratorio({}); }

std::string SuperRes::prepararLaboratorio(std::function<void()> despues) {
    if (laboratorioListo()) {
        if (despues) QTimer::singleShot(0, this, despues);
        return {};
    }
    if (despues) trasLaboratorio_.push_back(std::move(despues));
    const QStringList bib = bibliotecas();
    const fs::path dir = libs();
    net::Descargas::Pedido p;
    p.id = "superres:laboratorio";
    p.titulo = (conDirectml() ? tr("Laboratorio de superresolución (PyTorch con DirectML, ~800 MB)")
                              : tr("Laboratorio de superresolución (PyTorch, ~800 MB)"))
                   .toStdString();
    p.version = bib.join(" ").toStdString();
    QPointer<SuperRes> yo(this);
    p.yaEsta = [yo]() { return yo && yo->laboratorioListo(); };
    p.alTerminar = [yo](bool ok, const std::string&) {
        if (!yo) return;
        auto tras = std::move(yo->trasLaboratorio_);
        yo->trasLaboratorio_.clear();
        if (ok)
            for (auto& f : tras) f();
    };
    return encolar(
        std::move(p),
        [dir](net::Descargas::Contexto& ctx) -> std::string {
            // Si había uno a medias o de otra combinación (p. ej. sin DirectML), de
            // cero: pip --target no desinstala lo que pisa y quedaban dos PyTorch
            // mezclados. En el hilo de la cola: son cientos de MB para borrar.
            ctx.avance(0, tr("Preparando la carpeta…").toStdString());
            std::error_code ec;
            if (fs::exists(dir, ec)) fs::remove_all(dir, ec);
            fs::create_directories(dir, ec);
            return {};
        },
        [yo, bib](std::function<void(bool)> alFin) -> std::string {
            if (!yo) return tr("cancelada").toStdString();
            SuperRes* s = yo.data();
            return s->correr("laboratorio", conDirectml() ? tr("Preparar el laboratorio (PyTorch con DirectML para la GPU, ~800 MB)")
                                           : tr("Preparar el laboratorio (PyTorch, ~800 MB)"),
                             {},
                             QStringList{"-m", "pip", "install", "--disable-pip-version-check",
                                         "--no-warn-script-location", "--progress-bar", "off", "--target",
                                         q8(s->libs())} +
                                 bib,
                             [yo, bib, alFin](bool ok) {
                                 if (yo && ok) {
                                     std::ofstream(yo->libs() / ".ok")
                                         << "listo\n" << bib.join(" ").toStdString() << "\n";
                                     yo->trabajo_["texto"] = tr("Laboratorio listo");
                                     emit yo->trabajoCambio();
                                     emit yo->cambio();
                                 }
                                 alFin(ok);
                             });
        });
}

std::string SuperRes::convertir(const std::string& cual) {
    const Pesos* pesos = pesosDe(cual);
    if (pesos == nullptr) return tr("se convierte «span» o «esrgan»").toStdString();
    const bool hayQuePreparar = !laboratorioListo();
    if (hayQuePreparar) {
        const std::string e = prepararLaboratorio();
        if (!e.empty()) return e;
    }
    const fs::path salida = carpetaModelos() / (cual == "span" ? "span-x2.onnx" : "real-esrgan-x4.onnx");
    const fs::path archivo = carpetaModelos() / pesos->archivo;
    net::Descargas::Pedido p;
    p.id = "superres:convertir:" + cual;
    p.titulo = (cual == "span" ? tr("SPAN x2 (superresolución)") : tr("Real-ESRGAN x4 (superresolución)")).toStdString();
    p.version = std::string(pesos->sha256).substr(0, 12);
    if (hayQuePreparar) p.despuesDe = {"superres:laboratorio"};
    QPointer<SuperRes> yo(this);
    const std::string url = pesos->url, sha = pesos->sha256;
    return encolar(
        std::move(p),
        [url, archivo, sha](net::Descargas::Contexto& ctx) { return bajarVerificado(ctx, url, archivo, sha); },
        [yo, cual, salida](std::function<void(bool)> alFin) -> std::string {
            if (!yo) return tr("cancelada").toStdString();
            if (!yo->laboratorioListo())
                return tr("no está el laboratorio (falló su descarga): probá de nuevo").toStdString();
            const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
            return yo->correr("convertir", cual == "span" ? tr("Convertir SPAN") : tr("Convertir Real-ESRGAN"), {},
                              {q8(lab), QString::fromStdString(cual), "--libs", q8(yo->libs()), "--salida", q8(salida)},
                              [yo, alFin](bool ok) {
                                  if (yo) emit yo->cambio();
                                  alFin(ok);
                              });
        });
}

std::string SuperRes::origen(const std::string& entrada, int segundos, std::function<void(QStringList)> listo) {
    const std::string uri = video_->uriDe(entrada);
    if (!uri.empty()) {
        listo({"--uri", QString::fromStdString(uri)});
        return {};
    }
    if (!video_->esEntrada(entrada)) return tr("«%1» no es una entrada de video").arg(q(entrada)).toStdString();
    if (captura_.isActive()) return tr("ya se están capturando cuadros de otra entrada").toStdString();
    // Una cámara local: cuatro cuadros por segundo de lo que sale de su grafo, a una
    // carpeta (el laboratorio la toma como «--entrada»).
    std::string seguro;
    for (char c : entrada)
        seguro += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '_';
    const fs::path carpeta = carpetaModelos() / (".captura-" + seguro);
    std::error_code ec;
    fs::remove_all(carpeta, ec);
    fs::create_directories(carpeta, ec);
    capturados_ = 0;
    aCapturar_ = std::clamp(segundos, 10, 300) * 4;
    trabajo_ = QVariantMap{{"tipo", "capturar"}, {"que", tr("Capturar cuadros de «%1»").arg(q(entrada))},
                           {"entrada", QString::fromStdString(entrada)}, {"corriendo", true}, {"pct", 0},
                           {"texto", tr("Capturando lo que ve la cámara…")}};
    emit trabajoCambio();
    captura_.disconnect();
    captura_.setInterval(250);
    pidiendoCuadro_ = false;
    connect(&captura_, &QTimer::timeout, this, [this, entrada, carpeta, listo]() {
        // El cuadro llega aparte (la interfaz no lo espera); uno por vez.
        if (pidiendoCuadro_) return;
        pidiendoCuadro_ = true;
        QPointer<SuperRes> yo(this);
        video_->capturarCuadroAparte(entrada, [yo, carpeta, listo](QImage img) {
            if (!yo) return;
            yo->pidiendoCuadro_ = false;
            if (!yo->captura_.isActive()) return; // ya terminó o se canceló
            if (!img.isNull()) {
                img.save(QString::fromStdString(
                             (carpeta / QString("%1.jpg").arg(yo->capturados_, 5, 10, QChar('0')).toStdString()).string()),
                         "JPG", 95);
                ++yo->capturados_;
            }
            yo->trabajo_["pct"] = 100 * yo->capturados_ / std::max(1, yo->aCapturar_);
            yo->trabajo_["texto"] = tr("Capturando cuadros: %1 de %2").arg(yo->capturados_).arg(yo->aCapturar_);
            emit yo->trabajoCambio();
            if (yo->capturados_ >= yo->aCapturar_) {
                yo->captura_.stop();
                yo->trabajo_["corriendo"] = false;
                listo({"--entrada", QString::fromStdString(carpeta.string())});
            }
        });
    });
    captura_.start();
    return {};
}

std::string SuperRes::entrenar(const std::string& entrada, double minutos, int segundos, bool aplicarAlFin) {
    if (video_ == nullptr) return tr("no hay video").toStdString();
    minutos = std::clamp(minutos, 1.0, 120.0);
    std::string seguro;
    for (char c : entrada)
        seguro += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '_';
    const fs::path salida = carpetaModelos() / ("chica-" + seguro + ".onnx");
    const auto lanzar = [=, this](const QStringList& desde) {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        QStringList a{q8(lab),      "chica",
                      "--libs",     q8(libs())};
        a << desde;
        a << QStringList{"--segundos", QString::number(segundos),
                         "--minutos",  QString::number(minutos),
                         "--salida",   q8(salida),
                         "--nombre",   tr("Red chica de «%1»").arg(q(entrada))};
        std::error_code ec;
        if (fs::exists(salida.string() + ".pt", ec)) a << "--desde" << q8(salida);
        return correr(
            "entrenar", tr("Entrenar con lo que ve «%1»").arg(q(entrada)),
            QString::fromStdString(entrada), a, [this, entrada, salida, aplicarAlFin](bool ok) {
                if (ok && aplicarAlFin) {
                    const auto alAplicar = [this, entrada](const std::string& e) {
                        trabajo_["texto"] = e.empty() ? tr("Listo y aplicado a «%1»").arg(QString::fromStdString(entrada))
                                                      : tr("Listo, pero no se pudo aplicar: %1").arg(q(e));
                        emit trabajoCambio();
                    };
                    if (const std::string e = aplicar(entrada, "onnx", salida.string(), alAplicar); !e.empty()) alAplicar(e);
                }
                emit cambio();
            });
    };
    // Primero de dónde graba (la dirección, o cuadros de la cámara local); después el laboratorio.
    const auto conOrigen = [=, this]() {
        return origen(entrada, segundos, [this, lanzar](const QStringList& desde) {
            const std::string e = lanzar(desde);
            if (!e.empty()) {
                trabajo_["error"] = QString::fromStdString(e);
                emit trabajoCambio();
            }
        });
    };
    if (!laboratorioListo()) {
        return prepararLaboratorio([conOrigen]() { conOrigen(); });
    }
    return conOrigen();
}

std::string SuperRes::destilar(const std::string& entrada, const std::string& maestro, double minutos,
                               bool aplicarAlFin, bool cpu) {
    if (video_ == nullptr) return tr("no hay video").toStdString();
    json receta;
    for (const auto& r : catalogo())
        if (r.value("id", "") == (maestro.empty() ? std::string("realesr-general-x4v3") : maestro)) receta = r;
    if (!receta.is_object() || receta.value("tipo", "") != "red")
        return tr("el maestro tiene que ser una red del catálogo (superres_plugins: realesr-general-x4v3, "
                  "lsdir-compact-x4…)")
            .toStdString();
    minutos = std::clamp(minutos, 1.0, 240.0);
    std::string seguro;
    for (char c : entrada)
        seguro += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '_';
    const fs::path salida = carpetaModelos() / ("destilada-" + seguro + ".onnx");
    const fs::path archivoReceta = carpetaModelos() / (".maestro-" + seguro + ".json");
    std::ofstream(archivoReceta) << receta.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    const int segundos = 60;
    const auto lanzar = [=, this](const QStringList& desde) {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        QStringList a{q8(lab), "destilar", "--libs", q8(libs()), "--receta", q8(archivoReceta)};
        a << desde;
        a << QStringList{"--segundos", "60",               "--minutos",  QString::number(minutos),
                      "--salida",  q8(salida),          "--pesos",    q8(carpetaModelos() / ".pesos"),
                      "--nombre",  tr("Destilada de %1 para «%2»").arg(q(receta.value("nombre", maestro)), q(entrada))};
        if (cpu) a << "--cpu";
        std::error_code ec;
        if (fs::exists(salida.string() + ".pt", ec)) a << "--desde" << q8(salida);
        return correr("destilar", tr("Destilar «%1» para «%2»").arg(q(receta.value("nombre", maestro)), q(entrada)),
                      QString::fromStdString(entrada), a, [this, entrada, salida, aplicarAlFin](bool ok) {
                          if (ok && aplicarAlFin) {
                              const auto alAplicar = [this, entrada](const std::string& e) {
                                  trabajo_["texto"] = e.empty() ? tr("Lista y aplicada a «%1»").arg(q(entrada))
                                                                : tr("Lista, pero no se pudo aplicar: %1").arg(q(e));
                                  emit trabajoCambio();
                              };
                              if (const std::string e = aplicar(entrada, "onnx", salida.string(), alAplicar); !e.empty())
                                  alAplicar(e);
                          }
                          emit cambio();
                      });
    };
    // Primero de dónde graba (la dirección, o cuadros de la cámara local); después el laboratorio.
    const auto conOrigen = [=, this]() {
        return origen(entrada, segundos, [this, lanzar](const QStringList& desde) {
            const std::string e = lanzar(desde);
            if (!e.empty()) {
                trabajo_["error"] = QString::fromStdString(e);
                emit trabajoCambio();
            }
        });
    };
    if (!laboratorioListo()) {
        return prepararLaboratorio([conOrigen]() { conOrigen(); });
    }
    return conOrigen();
}

void SuperRes::cancelar() {
    trasLaboratorio_.clear();
    // Si lo que anda es una descarga de la cola, se cancela en la cola (corta el proceso).
    if (!idCola_.empty()) {
        net::Descargas::la().cancelar(QString::fromStdString(idCola_));
        return;
    }
    cortarHijo();
}

void SuperRes::cortarHijo() {
    if (hijo_ && hijo_->corriendo()) {
        trabajo_["error"] = tr("cancelado");
        hijo_->terminar();
    }
}

std::string SuperRes::aplicar(const std::string& entrada, const std::string& motor,
                              const std::string& modelo, std::function<void(const std::string&)> listo) {
    if (video_ == nullptr) return tr("no hay video").toStdString();
    std::string ruta = modelo;
    if (motor == "onnx" && !modelo.empty() && modelo.find('/') == std::string::npos &&
        modelo.find('\\') == std::string::npos)
        ruta = (carpetaModelos() / modelo).string();
    std::error_code ec;
    if (motor == "onnx" && !fs::exists(ruta, ec))
        return tr("no existe el modelo «%1» (superres_estado lista los que hay)").arg(q(modelo)).toStdString();
    // Relativa a los datos: la carpeta se puede mover y la tarjeta sigue andando. El grafo es
    // del dueño del motor: se le pide.
    video_->pedir("superres_aplicar",
                  [entrada, motor, m = motor == "onnx" ? ba::platform::rutaGuardable(ruta) : std::string()](VideoGrafos& g) {
                      return g.superresMotor(entrada, motor, m);
                  },
                  std::move(listo));
    return {};
}

namespace {
const char* kPlataforma =
#if defined(__APPLE__)
    "mac";
#elif defined(_WIN32)
    "windows";
#else
    "linux";
#endif
} // namespace

json SuperRes::plugins() {
    json out = json::array();
    std::set<std::string> vistos;
    const QStringList motores = VideoGrafos::motoresSuperres();
    auto leerPlugin = [](const std::string& id) -> json {
        std::ifstream f(carpetaModelos() / id / "plugin.json");
        if (!f) return nullptr;
        const json j = json::parse(f, nullptr, false);
        return j.is_object() ? j : json(nullptr);
    };
    for (json r : catalogo()) {
        const std::string id = r.value("id", std::string());
        vistos.insert(id);
        const json inst = leerPlugin(id);
        bool aca = false;
        for (const auto& p : r.value("plataformas", json::array()))
            if (p == kPlataforma) aca = true;
        if (r.value("tipo", "") == "integrado") {
            r["instalado"] = true;
            aca = aca && motores.contains(QString::fromStdString(r.value("motor", std::string())));
        } else {
            r["instalado"] = inst.is_object();
            if (inst.is_object())
                for (const char* k : {"ms_coreml", "ms_coreml_tamano", "arquitectura", "archivos", "instalado"})
                    if (inst.contains(k)) r[k == std::string("instalado") ? "fecha_instalado" : k] = inst[k];
        }
        if (r.value("tipo", "") == "glsl") aca = motores.contains("glsl"); // baglsl (OpenGL)
        r["aca"] = aca;
        r.erase("origen");
        out.push_back(r);
    }
    // Las instaladas que no están en el catálogo: la red propia (entrenada acá) y otras.
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(carpetaModelos(), ec)) {
        std::string id;
        json m = json::object();
        if (e.is_directory()) {
            id = e.path().filename().string();
            const json p = leerPlugin(id);
            if (!p.is_object()) continue;
            m = p;
        } else if (e.path().extension() == ".onnx") {
            id = e.path().stem().string();
            std::ifstream f(e.path().string() + ".json");
            if (f) {
                const json d = json::parse(f, nullptr, false);
                if (d.is_object()) m = d;
            }
            m["tipo"] = "red";
            m["archivos"] = {{"onnx", e.path().filename().string()}};
        } else {
            continue;
        }
        if (vistos.count(id)) continue;
        vistos.insert(id);
        m["id"] = id;
        if (!m.contains("nombre")) m["nombre"] = id;
        m["instalado"] = true;
        m["aca"] = true; // ONNX corre en cualquier sistema
        m["propio"] = true;
        out.push_back(m);
    }
    return out;
}

QVariantList SuperRes::pluginsUi() const { return qv(plugins()).toList(); }

std::string SuperRes::instalarPlugin(const std::string& id) {
    json receta;
    for (const auto& r : catalogo())
        if (r.value("id", "") == id) receta = r;
    if (!receta.is_object())
        return tr("no hay un plugin «%1» en el catálogo (superres_plugins los lista)").arg(q(id)).toStdString();
    if (receta.value("tipo", "") == "integrado")
        return tr("«%1» viene con el programa: no hace falta instalarlo").arg(q(id)).toStdString();
    const fs::path dir = carpetaModelos() / id;
    const fs::path archivoReceta = carpetaModelos() / (".receta-" + id + ".json");
    std::ofstream(archivoReceta) << receta.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    // Los tamaños de las cámaras del estudio (lo que llega de cada una, antes de
    // los filtros): CoreML compila la red para tamaños fijos, y se suman estos
    // a los comunes. Una cámara que se agregue después: volver a instalar.
    std::string tamanos;
    if (video_ != nullptr)
        for (const QString& e : video_->entradas()) {
            const json st = video_->estadoEntrada(e.toStdString());
            const json o = st.value("gstreamer", json::object()).value("origen", json::object());
            const int w = o.value("ancho", 0), h = o.value("alto", 0);
            if (w > 0 && h > 0) tamanos += (tamanos.empty() ? "" : ",") + std::to_string(w) + "x" + std::to_string(h);
        }
    const bool hayQuePreparar = receta.value("tipo", "") == "red" && !laboratorioListo();
    if (hayQuePreparar) { // una red necesita el laboratorio (PyTorch); un shader, no
        const std::string e = prepararLaboratorio();
        if (!e.empty()) return e;
    }
    net::Descargas::Pedido p;
    p.id = "superres:plugin:" + id;
    p.titulo = tr("%1 (superresolución)").arg(q(receta.value("nombre", id))).toStdString();
    const json origen = receta.value("origen", json::object());
    const std::string url = origen.value("url", std::string()), sha = origen.value("sha256", std::string());
    p.version = sha.substr(0, 12);
    if (hayQuePreparar) p.despuesDe = {"superres:laboratorio"};
    QPointer<SuperRes> yo(this);
    const bool esRed = receta.value("tipo", "") == "red";
    // El archivo de la receta lo baja la cola (verificado, sigue si se corta) donde
    // lo busca el laboratorio: la carpeta del plugin, con el nombre de la URL.
    const fs::path archivo = dir / url.substr(url.find_last_of('/') + 1, url.find('?') == std::string::npos
                                                                             ? std::string::npos
                                                                             : url.find('?') - url.find_last_of('/') - 1);
    return encolar(
        std::move(p),
        [url, sha, archivo](net::Descargas::Contexto& ctx) -> std::string {
            if (url.empty() || sha.size() != 64) return {}; // lo resuelve el laboratorio (y falla si no hay hash)
            return bajarVerificado(ctx, url, archivo, sha);
        },
        [yo, receta, dir, archivoReceta, tamanos, esRed](std::function<void(bool)> alFin) -> std::string {
            if (!yo) return tr("cancelada").toStdString();
            if (esRed && !yo->laboratorioListo())
                return tr("no está el laboratorio (falló su descarga): probá de nuevo").toStdString();
            const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
            return yo->correr("instalar", tr("Instalar «%1»").arg(q(receta.value("nombre", std::string()))), {},
                              {q8(lab), "instalar", "--receta", q8(archivoReceta), "--salida", q8(dir), "--libs",
                               q8(yo->libs()), "--tamanos", QString::fromStdString(tamanos)},
                              [yo, archivoReceta, alFin](bool ok) {
                                  std::error_code ec;
                                  fs::remove(archivoReceta, ec);
                                  if (yo) emit yo->cambio();
                                  alFin(ok);
                              });
        });
}

namespace {
/// "WxH=ruta;…" con las variantes del plugin que tengan `clave` (coreml, onnx,
/// onnx_nv12): la propiedad `variantes` de los motores.
std::string variantesPara(const fs::path& dir, const char* clave) {
    std::string out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir / "variantes", ec)) {
        std::ifstream f(e.path() / "variante.json");
        const json v = json::parse(f, nullptr, false);
        if (!v.is_object()) continue;
        const json a = v.value("archivos", json::object());
        if (!a.contains(clave) || !a[clave].is_string()) continue;
        const json en = v.value("entrada", json::object());
        const int w = en.value("ancho", 0), h = en.value("alto", 0);
        if (w <= 0 || h <= 0) continue;
        const fs::path ruta = e.path() / a[clave].get<std::string>();
        if (!fs::exists(ruta, ec)) continue;
        out += (out.empty() ? "" : ";") + std::to_string(w) + "x" + std::to_string(h) + "=" + ruta.string();
    }
    return out;
}
} // namespace

json SuperRes::resolverPlugin(const std::string& id, std::string* error) const {
    auto fallar = [&](const std::string& e) {
        if (error) *error = e;
        return json::object();
    };
    if (id == "apple" || id == "fsr") {
        if (!motores().contains(QString::fromStdString(id)))
            return fallar(tr("este sistema no tiene «%1»").arg(q(id)).toStdString());
        return json{{"motor", id}, {"modelo", ""}, {"escala_red", 2}};
    }
    const fs::path dir = carpetaModelos() / id;
    std::ifstream f(dir / "plugin.json");
    if (!f) {
        // Una red suelta (las de antes: <id>.onnx).
        std::error_code ec;
        if (fs::exists(carpetaModelos() / (id + ".onnx"), ec))
            return json{{"motor", "onnx"}, {"modelo", (carpetaModelos() / (id + ".onnx")).string()}, {"escala_red", 2}};
        return fallar(tr("el plugin «%1» no está instalado (superres_instalar)").arg(q(id)).toStdString());
    }
    const json p = json::parse(f, nullptr, false);
    if (!p.is_object()) return fallar(tr("plugin.json de «%1» roto").arg(q(id)).toStdString());
    const json archivos = p.value("archivos", json::object());
    if (p.value("tipo", "") == "elemento") {
        // CONTRATO ABIERTO: un elemento de GStreamer cualquiera (de GStreamer, o
        // propio en <plugin>/gst/: una red compilada para la placa, p. ej. con
        // TensorRT y memory:CUDAMemory). plugin.json:
        //   {"tipo": "elemento", "elemento": "mitrt", "propiedades": {...},
        //    "memoria": "CUDAMemory"|"GLMemory"|"D3D11Memory"|"D3D12Memory"|"",
        //    "escala": 2, "medio": "video"|"audio", "latencia_ms": 12,
        //    "plataformas": ["windows", "linux"]}
        // Las propiedades pueden usar {dir} (la carpeta del plugin).
        const std::string el = p.value("elemento", std::string());
        if (el.empty()) return fallar(tr("plugin.json de «%1» sin `elemento`").arg(q(id)).toStdString());
#if defined(BA_HAVE_GST)
        std::error_code e3;
        if (fs::is_directory(dir / "gst", e3)) gst_registry_scan_path(gst_registry_get(), (dir / "gst").string().c_str());
        GstElementFactory* fac = gst_element_factory_find(el.c_str());
        if (fac == nullptr)
            return fallar(tr("GStreamer no tiene el elemento «%1» (¿falta su biblioteca en %2?)")
                              .arg(q(el), q((dir / "gst").string()))
                              .toStdString());
        gst_object_unref(fac);
#endif
        json props = json::object();
        for (const auto obj_ = p.value("propiedades", json::object()); const auto& [k, v] : obj_.items()) {
            std::string t = v.is_string() ? v.get<std::string>() : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            for (size_t i = 0; (i = t.find("{dir}", i)) != std::string::npos;) t.replace(i, 5, dir.string());
            props[k] = t;
        }
        json r{{"elemento", el},
               {"propiedades", props},
               {"memoria", p.value("memoria", std::string())},
               {"escala_red", p.value("escala", 2)}};
        if (p.contains("memoria_salida")) r["memoria_salida"] = p["memoria_salida"];
        return r;
    }
    if (p.value("tipo", "") == "glsl") {
        if (!archivos.contains("glsl")) return fallar(tr("plugin.json de «%1» sin shader").arg(q(id)).toStdString());
        if (!motores().contains("glsl"))
            return fallar(tr("este sistema no tiene el motor de shaders (baglsl)").toStdString());
        return json{{"motor", "glsl"}, {"modelo", (dir / archivos["glsl"].get<std::string>()).string()}, {"escala_red", 2}};
    }
    const int escala = p.value("escala", 2);
    const int escalaRed = escala == 4 ? 4 : 2; // una x4 entra a la mitad y sale x2
#if defined(__APPLE__)
    if (archivos.contains("coreml") && motores().contains("coreml"))
        // FP32 (redes que se desbordan en FP16, como SPAN): en la GPU, el Neural Engine no la corre.
        return json{{"motor", "coreml"}, {"modelo", (dir / archivos["coreml"].get<std::string>()).string()},
                    {"variantes", variantesPara(dir, "coreml")},
                    {"escala_red", escalaRed},
                    {"unidades", p.value("coreml_precision", std::string("fp16")) == "fp32" ? "gpu" : "auto"}};
#endif
#if !defined(__APPLE__)
    // NVIDIA con el ONNX Runtime de GPU: la red NV12 va directo como elemento
    // con memory:CUDAMemory (el decodificador de la placa le entrega el cuadro
    // sin bajarlo; baonnxsr lo copia de placa a placa y entrega en sistema).
    if (archivos.contains("onnx_nv12") && runtimeGpu().value("placa", std::string()) == "nvidia" &&
        !qEnvironmentVariableIsEmpty("BA_ORT_GPU"))
        return json{{"elemento", "baonnxsr"},
                    {"propiedades", {{"modelo", (dir / archivos["onnx_nv12"].get<std::string>()).string()},
                                     {"variantes", variantesPara(dir, "onnx_nv12")},
                                     {"proveedor", "auto"}}},
                    {"memoria", "CUDAMemory"},
                    {"memoria_salida", ""},
                    {"escala_red", 2}};
#endif
    // La versión NV12 (todo en la placa: color, mitad, vuelta a NV12) si está;
    // si no, la float (baonnxsr igual le da la mitad a una x4).
    for (const char* clave : {"onnx_nv12", "onnx"})
        if (archivos.contains(clave) && motores().contains("onnx")) {
            json r{{"motor", "onnx"}, {"modelo", (dir / archivos[clave].get<std::string>()).string()},
                   {"variantes", variantesPara(dir, clave)},
                   {"escala_red", 2}};
            // La float también: Vulkan (ncnn) no corre las NV12 del laboratorio.
            if (std::string(clave) == "onnx_nv12" && archivos.contains("onnx"))
                r["modelo_float"] = (dir / archivos["onnx"].get<std::string>()).string();
            return r;
        }
    return fallar(tr("«%1» no tiene un modelo que corra en esta máquina").arg(q(id)).toStdString());
}

namespace {
/// La placa de video de esta máquina, por lo que deja ver el sistema (sin
/// cargar nada): nvidia, amd, intel o "".
std::string placaDeVideo() {
    std::error_code ec;
#if defined(_WIN32)
    const fs::path sys = fs::path(qEnvironmentVariable("SystemRoot", "C:\\Windows").toStdWString()) / "System32";
    if (fs::exists(sys / "nvcuda.dll", ec)) return "nvidia";
    if (fs::exists(sys / "amdhip64.dll", ec) || fs::exists(sys / "atiadlxx.dll", ec)) return "amd";
    if (fs::exists(sys / "igdumdim64.dll", ec) || fs::exists(sys / "igd10iumd64.dll", ec)) return "intel";
    return {};
#elif defined(__linux__)
    if (fs::exists("/proc/driver/nvidia/version", ec)) return "nvidia";
    std::string otra;
    for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
        std::ifstream f(e.path() / "device" / "vendor");
        std::string v;
        if (!(f >> v)) continue;
        if (v == "0x10de") return "nvidia";
        if (v == "0x1002") otra = "amd";
        if (v == "0x8086" && otra.empty()) otra = "intel";
    }
    return otra;
#else
    return {};
#endif
}
} // namespace

json SuperRes::runtimeGpu() const {
    json out{{"placa", placaDeVideo()}};
    const fs::path dir = ba::platform::dataRoot() / "Herramientas" / "onnxruntime-gpu";
    std::ifstream f(dir / "runtime.json");
    if (f) {
        const json r = json::parse(f, nullptr, false);
        if (r.is_object()) out["instalado"] = r;
    }
    const std::string placa = out["placa"];
#if defined(__APPLE__)
    out["sugerido"] = "";
    out["nota"] = tr("en la Mac el ONNX Runtime del programa ya usa CoreML (GPU y Neural Engine), y las redes "
                     "instaladas corren en CoreML nativo: no hace falta bajar nada")
                      .toStdString();
#elif defined(_WIN32)
    out["sugerido"] = placa == "nvidia" ? "nvidia" : placa == "intel" ? "intel" : "";
    out["nota"] = placa == "amd" || placa.empty()
                      ? tr("con AMD (también las integradas Ryzen) el programa ya usa DirectML: no hace falta bajar "
                           "nada")
                            .toStdString()
                      : tr("DirectML ya anda; el runtime de %1 es más rápido (TensorRT compila la red para la placa)")
                            .arg(q(placa))
                            .toStdString();
#else
    out["sugerido"] = placa;
    if (placa == "amd")
        out["nota"] =
            tr("AMD en Linux usa ROCm/MIGraphX: necesita el controlador amdgpu y ROCm 6.4 del sistema").toStdString();
#endif
    out["activo"] = !qEnvironmentVariableIsEmpty("BA_ORT_GPU");
    return out;
}

std::string SuperRes::instalarRuntimeGpu(const std::string& tipoPedido, bool conTensorRt) {
#if defined(__APPLE__)
    (void)tipoPedido;
    (void)conTensorRt;
    return tr("en la Mac no hace falta: CoreML ya viene").toStdString();
#else
    const std::string tipo = tipoPedido.empty() ? runtimeGpu().value("sugerido", std::string()) : tipoPedido;
    if (tipo != "nvidia" && tipo != "amd" && tipo != "intel")
        return tr("no hay un runtime de GPU para bajar acá (tipo: nvidia, amd o intel)").toStdString();
    const fs::path script = ba::platform::bundleRoot() / "superres" / "runtime_gpu.py";
    QStringList args{q8(script), "--tipo", QString::fromStdString(tipo), "--salida",
                     q8(ba::platform::dataRoot() / "Herramientas" / "onnxruntime-gpu")};
    if (!conTensorRt) args << "--sin-tensorrt";
    net::Descargas::Pedido p;
    p.id = "superres:runtime-gpu:" + tipo;
    p.titulo = tr("ONNX Runtime de GPU (%1)").arg(q(tipo)).toStdString();
    p.version = conTensorRt ? tr("con TensorRT").toStdString() : std::string();
    QPointer<SuperRes> yo(this);
    return encolar(std::move(p), {}, [yo, tipo, args](std::function<void(bool)> alFin) -> std::string {
        if (!yo) return tr("cancelada").toStdString();
        return yo->correr("onnx", tr("Bajar el ONNX Runtime de GPU (%1)").arg(q(tipo)), {}, args,
                          [yo, alFin](bool ok) {
                              if (yo) emit yo->cambio();
                              alFin(ok);
                          });
    });
#endif
}

std::string SuperRes::tarjetaConPlugin(const std::string& entrada, const std::string& id, const json& extra,
                                       std::function<void(const std::string&)> listo) {
    if (video_ == nullptr) return tr("no hay video").toStdString();
    std::string err;
    if (resolverPlugin(id, &err).empty()) return err;
    video_->pedir("superres_tarjeta", [entrada, id, extra](VideoGrafos& g) { return g.superresPlugin(entrada, id, extra); },
                  std::move(listo));
    return {};
}

QVariantMap SuperRes::runtimeGpuUi() const { return qv(runtimeGpu()).toMap(); }

std::string SuperRes::guardarPluginElemento(const json& p) {
    std::string id = p.value("id", std::string());
    for (char& c : id)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '-';
    if (id.empty()) return tr("falta el id (letras, números y guiones)").toStdString();
    const std::string el = p.value("elemento", std::string());
    if (el.empty()) return tr("falta el elemento de GStreamer").toStdString();
    for (const auto& r : catalogo())
        if (r.value("id", "") == id) return tr("«%1» es un plugin del catálogo: elegí otro id").arg(q(id)).toStdString();
    const std::string mem = p.value("memoria", std::string());
    if (!mem.empty() && mem != "CUDAMemory" && mem != "GLMemory" && mem != "D3D11Memory" && mem != "D3D12Memory")
        return tr("memoria: vacía (sistema), CUDAMemory, GLMemory, D3D11Memory o D3D12Memory").toStdString();
    json props = json::object();
    const json propsIn = p.value("propiedades", json::object()); // en una variable: items() no alarga la vida del temporal
    for (const auto& [k, v] : propsIn.items())
        props[k] = v.is_string() ? v.get<std::string>() : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    json out{{"id", id},
             {"nombre", p.value("nombre", id)},
             {"tipo", "elemento"},
             {"elemento", el},
             {"propiedades", props},
             {"memoria", mem},
             {"escala", std::clamp(p.value("escala", 2), 1, 4)},
             {"medio", "video"},
             {"descripcion", p.value("descripcion", std::string())},
             {"plataformas", p.value("plataformas", json::array({"mac", "windows", "linux"}))},
             {"instalado", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm").toStdString()}};
    if (p.contains("memoria_salida")) out["memoria_salida"] = p["memoria_salida"];
    const fs::path dir = carpetaModelos() / id;
    std::error_code ec;
    fs::create_directories(dir / "gst", ec);
    std::ofstream f(dir / "plugin.json");
    if (!f) return tr("no se pudo escribir %1").arg(q((dir / "plugin.json").string())).toStdString();
    f << out.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    f.close();
    // Que ande acá ya (si el elemento no está, lo dice: puede faltar su biblioteca en gst/).
    std::string err;
    resolverPlugin(id, &err);
    emit cambio();
    // Sin traducir: PanelSuperres.qml lo distingue de un error porque empieza con «guardado».
    return err.empty() ? std::string() : "guardado, pero: " + err;
}

std::string SuperRes::borrarPlugin(const std::string& id) {
    if (id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos || id[0] == '.')
        return tr("id inválido").toStdString();
    const fs::path dir = carpetaModelos() / id;
    std::error_code ec;
    if (!fs::exists(dir / "plugin.json", ec)) return tr("no hay un plugin instalado «%1»").arg(q(id)).toStdString();
    fs::remove_all(dir, ec);
    emit cambio();
    return ec ? ec.message() : std::string();
}

QString SuperRes::guardarPluginElementoUi(const QVariantMap& p) {
    const json j = json::parse(QJsonDocument::fromVariant(p).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    return q(guardarPluginElemento(j.is_object() ? j : json::object()));
}

QVariantList SuperRes::pluginsElementoUi() const {
    json out = json::array();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(carpetaModelos(), ec)) {
        if (!e.is_directory()) continue;
        std::ifstream f(e.path() / "plugin.json");
        if (!f) continue;
        const json p = json::parse(f, nullptr, false);
        if (p.is_object() && p.value("tipo", "") == "elemento") {
            json x = p;
            x["id"] = e.path().filename().string();
            x["carpeta"] = e.path().string();
            std::string err;
            x["anda"] = !resolverPlugin(x["id"], &err).empty();
            if (!err.empty()) x["error"] = err;
            out.push_back(x);
        }
    }
    return qv(out).toList();
}

QString SuperRes::borrarModelo(const QString& archivo) {
    const fs::path p = carpetaModelos() / archivo.toStdString();
    if (p.parent_path() != carpetaModelos() || p.extension() != ".onnx") return "no";
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(p.string() + ".json", ec);
    fs::remove(p.string() + ".pt", ec);
    emit cambio();
    return ec ? QString::fromStdString(ec.message()) : QString();
}

// --- Variantes: la red compilada para un tamaño fijo -----------------------------------
// <plugin>/variantes/<id>/{variante.json, modelo.mlmodelc | modelo.onnx | modelo-nv12.onnx}.
// Las hace assets/superres/variantes.py con el Python del programa (coremltools
// y onnx, sin PyTorch); el motor usa la del tamaño que le llega.

json SuperRes::variantes(const std::string& plugin) const {
    json out = json::array();
    std::error_code ec;
    const auto deUno = [&](const fs::path& dir) {
        for (const auto& e : fs::directory_iterator(dir / "variantes", ec)) {
            std::ifstream f(e.path() / "variante.json");
            json v = json::parse(f, nullptr, false);
            if (!v.is_object()) continue;
            v["carpeta"] = e.path().string();
            if (!v.contains("plugin")) v["plugin"] = dir.filename().string();
            out.push_back(std::move(v));
        }
    };
    if (!plugin.empty()) {
        deUno(carpetaModelos() / plugin);
    } else {
        for (const auto& e : fs::directory_iterator(carpetaModelos(), ec))
            if (e.is_directory()) deUno(e.path());
    }
    return out;
}

std::vector<std::string> SuperRes::tamanosSinVariante(const std::string& plugin) const {
    std::set<std::string> hay;
    for (const auto& v : variantes(plugin)) {
        const json en = v.value("entrada", json::object());
        hay.insert(std::to_string(en.value("ancho", 0)) + "x" + std::to_string(en.value("alto", 0)));
    }
    std::vector<std::string> out;
    if (video_ == nullptr) return out;
    for (const QString& e : video_->entradas()) {
        const json st = video_->estadoEntrada(e.toStdString());
        const json o = st.value("gstreamer", json::object()).value("origen", json::object());
        const int w = o.value("ancho", 0), h = o.value("alto", 0);
        if (w <= 0 || h <= 0) continue;
        const std::string t = std::to_string(w - w % 2) + "x" + std::to_string(h - h % 2);
        if (!hay.count(t) && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
    }
    return out;
}

std::string SuperRes::crearVariantes(const std::string& plugin, const std::vector<std::string>& tamanos,
                                     const std::string& id, const std::string& motores, const std::string& para,
                                     const std::string& descripcion, bool reemplazar) {
    const fs::path dir = carpetaModelos() / plugin;
    std::error_code ec;
    if (plugin.empty() || !fs::exists(dir / "plugin.json", ec))
        return tr("el plugin «%1» no está instalado (superres_plugins los lista)").arg(q(plugin)).toStdString();
    if (tamanos.empty()) return tr("falta el tamaño (ancho y alto, o `faltantes`)").toStdString();
    if (!id.empty() && tamanos.size() > 1) return tr("`id` es para una sola variante").toStdString();
    std::string lista;
    for (const auto& t : tamanos) {
        const auto x = t.find('x');
        if (x == std::string::npos || std::atoi(t.c_str()) < 16 || std::atoi(t.c_str() + x + 1) < 16)
            return tr("tamaño inválido: «%1» (ancho x alto, p. ej. 1024x576)").arg(q(t)).toStdString();
        lista += (lista.empty() ? "" : ",") + t;
    }
    const fs::path script = ba::platform::bundleRoot() / "superres" / "variantes.py";
    QStringList args{q8(script), "crear", "--plugin", q8(dir), "--tamanos", QString::fromStdString(lista)};
    if (!id.empty()) args << "--id" << QString::fromStdString(id);
    if (!motores.empty()) args << "--motores" << QString::fromStdString(motores);
    if (!para.empty()) args << "--para" << QString::fromStdString(para);
    if (!descripcion.empty()) args << "--descripcion" << QString::fromStdString(descripcion);
    if (reemplazar) args << "--reemplazar";
    return correr("variantes", tr("Variantes de «%1» (%2)").arg(q(plugin), q(lista)),
                  {}, args, [this](bool ok) {
                      if (ok && video_ != nullptr) // las tarjetas la usan ya
                          video_->pedir("superres_variantes", [](VideoGrafos& g) {
                              g.reaplicarEntradas();
                              return std::string();
                          });
                      emit cambio();
                  });
}

std::string SuperRes::borrarVariante(const std::string& plugin, const std::string& id) {
    if (id.empty() || id.find('/') != std::string::npos || id.find('\\') != std::string::npos || id == "..")
        return tr("id inválido").toStdString();
    const fs::path d = carpetaModelos() / plugin / "variantes" / id;
    std::error_code ec;
    if (!fs::exists(d / "variante.json", ec))
        return tr("no hay una variante «%1» de «%2»").arg(q(id), q(plugin)).toStdString();
    fs::remove_all(d, ec);
    if (ec) return ec.message();
    if (video_ != nullptr)
        video_->pedir("superres_variantes", [](VideoGrafos& g) {
            g.reaplicarEntradas();
            return std::string();
        });
    emit cambio();
    return {};
}

std::string SuperRes::editarVariante(const std::string& plugin, const std::string& id, const json& cambios) {
    const fs::path j = carpetaModelos() / plugin / "variantes" / id / "variante.json";
    std::ifstream f(j);
    json v = json::parse(f, nullptr, false);
    f.close();
    if (!v.is_object()) return tr("no hay una variante «%1» de «%2»").arg(q(id), q(plugin)).toStdString();
    for (const char* k : {"para", "descripcion", "nombre"})
        if (cambios.contains(k) && cambios[k].is_string()) v[k] = cambios[k];
    std::ofstream o(j);
    o << v.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (!o) return tr("no se pudo guardar %1").arg(q(j.string())).toStdString();
    emit cambio();
    return {};
}

QVariantList SuperRes::variantesUi(const QString& plugin) const { return qv(variantes(plugin.toStdString())).toList(); }

QString SuperRes::crearVarianteUi(const QString& plugin, int ancho, int alto, const QString& para,
                                  const QString& motores) {
    return q(crearVariantes(plugin.toStdString(), {std::to_string(ancho) + "x" + std::to_string(alto)}, {},
                            motores.toStdString(), para.toStdString(), {}, false));
}

QString SuperRes::crearFaltantesUi(const QString& plugin) {
    const auto t = tamanosSinVariante(plugin.toStdString());
    if (t.empty()) return tr("todas las cámaras del estudio ya tienen su variante");
    return q(crearVariantes(plugin.toStdString(), t, {}, {}, {}, {}, false));
}

QStringList SuperRes::tamanosSinVarianteUi(const QString& plugin) const {
    QStringList out;
    for (const auto& t : tamanosSinVariante(plugin.toStdString())) out << QString::fromStdString(t);
    return out;
}

QString SuperRes::editarVarianteUi(const QString& plugin, const QString& id, const QString& para,
                                   const QString& descripcion) {
    return q(editarVariante(plugin.toStdString(), id.toStdString(),
                            json{{"para", para.toStdString()}, {"descripcion", descripcion.toStdString()}}));
}

} // namespace ba::app::video
