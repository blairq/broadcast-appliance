// src/app/video/SuperRes.cpp — ver SuperRes.hpp

#include "video/SuperRes.hpp"

#include "platform/Paths.hpp"
#include "plugins/Hijo.hpp"
#include "video/VideoGrafos.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>

#include <fstream>

namespace fs = std::filesystem;

namespace ba::app::video {

namespace {
using json = nlohmann::json;
QString q8(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
QVariant qv(const json& j) { return QJsonDocument::fromJson(QByteArray::fromStdString(json::array({j}).dump())).toVariant().toList().value(0); }
// Lo que baja el laboratorio: versiones fijas (CPU/MPS; en Windows con NVIDIA
// se puede cambiar por la de CUDA a mano).
const QStringList kBibliotecas{"torch==2.14.1", "spandrel==0.4.2", "onnx==1.23.1"};
} // namespace

SuperRes::SuperRes(fs::path dataRoot, VideoGrafos* video, QObject* parent) : QObject(parent), root_(std::move(dataRoot)), video_(video) {
    std::error_code ec;
    fs::create_directories(carpetaModelos(), ec);
    trabajo_ = {{"corriendo", false}};
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
    if (hijo_) {
        hijo_->disconnect(this);
        hijo_->terminar();
    }
}

fs::path SuperRes::carpetaModelos() const { return root_ / "Modelos" / "superres"; }
fs::path SuperRes::libs() const { return root_ / "Herramientas" / "superres-libs"; }

fs::path SuperRes::python() const {
#if defined(_WIN32)
    return ba::platform::bundleRoot() / "python" / "python.exe";
#else
    return ba::platform::bundleRoot() / "python" / "bin" / "python3";
#endif
}

bool SuperRes::laboratorioListo() const {
    std::error_code ec;
    return fs::exists(libs() / ".ok", ec);
}

QStringList SuperRes::motores() const { return video_ != nullptr ? video_->motoresSuperres() : QStringList{}; }

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

std::string SuperRes::correr(const QString& que, const QString& entrada, const QStringList& args, std::function<void(bool)> fin) {
    if (hijo_ && hijo_->corriendo()) return "ya hay un trabajo andando: «" + trabajo_.value("que").toString().toStdString() + "»";
    std::error_code ec;
    if (!fs::exists(python(), ec)) return "el programa no trae Python";
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString& k : env.keys())
        if (k.startsWith("PYTHON")) env.remove(k);
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
        if (!ok && !trabajo_.contains("error")) trabajo_["error"] = QString("terminó con código %1").arg(codigo);
        if (ok) trabajo_["pct"] = 100;
        emit trabajoCambio();
        emit cambio();
        auto f = std::move(fin_);
        fin_ = nullptr;
        if (f) QTimer::singleShot(0, this, [f = std::move(f), ok]() { f(ok); });
    });
    trabajo_ = {{"que", que}, {"entrada", entrada}, {"corriendo", true}, {"pct", 0}, {"texto", "Arrancando…"},
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
            emit trabajoCambio();
        }
        return;
    }
    for (const auto& [k, v] : m.items()) trabajo_[QString::fromStdString(k)] = qv(v);
    if (m.value("ev", "") == "error") trabajo_["error"] = QString::fromStdString(m.value("error", std::string("falló")));
    emit trabajoCambio();
}

std::string SuperRes::prepararLaboratorio() {
    if (laboratorioListo()) return {};
    std::error_code ec;
    fs::create_directories(libs(), ec);
    return correr("Preparar el laboratorio (PyTorch, ~800 MB)", {},
                  QStringList{"-m", "pip", "install", "--disable-pip-version-check", "--no-warn-script-location", "--progress-bar", "off",
                              "--target", q8(libs())} +
                      kBibliotecas,
                  [this](bool ok) {
                      if (ok) {
                          std::ofstream(libs() / ".ok") << "listo\n";
                          trabajo_["texto"] = "Laboratorio listo";
                          emit trabajoCambio();
                          emit cambio();
                      }
                      if (ok && despues_) {
                          // En la vuelta siguiente: acá todavía se está avisando el fin del proceso anterior.
                          QTimer::singleShot(0, this, [this, d = std::move(despues_)]() { d(); });
                      }
                      despues_ = nullptr;
                  });
}

std::string SuperRes::convertir(const std::string& cual) {
    if (cual != "span" && cual != "esrgan") return "se convierte «span» o «esrgan»";
    const auto lanzar = [this, cual]() {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        return correr(cual == "span" ? "Convertir SPAN" : "Convertir Real-ESRGAN", {},
                      {q8(lab), QString::fromStdString(cual), "--libs", q8(libs()), "--salida",
                       q8(carpetaModelos() / (cual == "span" ? "span-x2.onnx" : "real-esrgan-x4.onnx"))},
                      [this](bool) { emit cambio(); });
    };
    if (!laboratorioListo()) {
        despues_ = [lanzar]() { lanzar(); };
        return prepararLaboratorio();
    }
    return lanzar();
}

std::string SuperRes::entrenar(const std::string& entrada, double minutos, int segundos, bool aplicarAlFin) {
    if (video_ == nullptr) return "no hay video";
    const std::string uri = video_->uriDe(entrada);
    if (uri.empty()) return "«" + entrada + "» no es una entrada de red (se entrena grabando de su dirección)";
    minutos = std::clamp(minutos, 1.0, 120.0);
    std::string seguro;
    for (char c : entrada) seguro += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '_';
    const fs::path salida = carpetaModelos() / ("chica-" + seguro + ".onnx");
    const auto lanzar = [=, this]() {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        QStringList a{q8(lab), "chica", "--libs", q8(libs()), "--uri", QString::fromStdString(uri), "--segundos", QString::number(segundos),
                      "--minutos", QString::number(minutos), "--salida", q8(salida), "--nombre",
                      QString::fromStdString("Red chica de «" + entrada + "»")};
        std::error_code ec;
        if (fs::exists(salida.string() + ".pt", ec)) a << "--desde" << q8(salida);
        return correr("Entrenar con lo que ve «" + QString::fromStdString(entrada) + "»", QString::fromStdString(entrada), a,
                      [this, entrada, salida, aplicarAlFin](bool ok) {
                          if (ok && aplicarAlFin) {
                              const std::string e = aplicar(entrada, "onnx", salida.string());
                              trabajo_["texto"] = e.empty() ? QString("Listo y aplicado a «%1»").arg(QString::fromStdString(entrada))
                                                            : QString::fromStdString("Listo, pero no se pudo aplicar: " + e);
                              emit trabajoCambio();
                          }
                          emit cambio();
                      });
    };
    if (!laboratorioListo()) {
        despues_ = [lanzar]() { lanzar(); };
        return prepararLaboratorio();
    }
    return lanzar();
}

void SuperRes::cancelar() {
    despues_ = nullptr;
    if (hijo_ && hijo_->corriendo()) {
        trabajo_["error"] = "cancelado";
        hijo_->terminar();
    }
}

std::string SuperRes::aplicar(const std::string& entrada, const std::string& motor, const std::string& modelo) {
    if (video_ == nullptr) return "no hay video";
    std::string ruta = modelo;
    if (motor == "onnx" && !modelo.empty() && modelo.find('/') == std::string::npos && modelo.find('\\') == std::string::npos)
        ruta = (carpetaModelos() / modelo).string();
    std::error_code ec;
    if (motor == "onnx" && !fs::exists(ruta, ec)) return "no existe el modelo «" + modelo + "» (superres_estado lista los que hay)";
    return video_->superresMotor(entrada, motor, motor == "onnx" ? ruta : std::string());
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

} // namespace ba::app::video
