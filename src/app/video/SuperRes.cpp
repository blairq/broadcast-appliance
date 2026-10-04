// src/app/video/SuperRes.cpp — ver SuperRes.hpp

#include "video/SuperRes.hpp"

#include "platform/Paths.hpp"
#include "plugins/Hijo.hpp"
#include "video/VideoGrafos.hpp"

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>

#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace ba::app::video {

namespace {
using json = nlohmann::json;
QString q8(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
QVariant qv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json::array({j}).dump()))
        .toVariant()
        .toList()
        .value(0);
}
// Lo que baja el laboratorio: versiones fijas (CPU/MPS; en Windows con NVIDIA
// se puede cambiar por la de CUDA a mano).
const QStringList kBibliotecas{"torch==2.14.1", "spandrel==0.4.2", "onnx==1.23.1"};
} // namespace

SuperRes::SuperRes(fs::path dataRoot, VideoGrafos* video, QObject* parent)
    : QObject(parent), root_(std::move(dataRoot)), video_(video) {
    std::error_code ec;
    fs::create_directories(carpetaModelos(), ec);
    trabajo_ = {{"corriendo", false}};
    // Un ONNX Runtime con GPU (NVIDIA: CUDA/TensorRT; AMD en Linux:
    // ROCm/MIGraphX) bajado a Datos/Herramientas/onnxruntime-gpu: baonnxsr lo
    // carga en vez del del programa (variable BA_ORT_GPU, antes de que
    // GStreamer abra una red).
    if (qEnvironmentVariableIsEmpty("BA_ORT_GPU")) {
        const fs::path base = root_ / "Herramientas" / "onnxruntime-gpu" / "lib";
#if defined(_WIN32)
        const fs::path lib = base / "onnxruntime.dll";
#else
        const fs::path lib = base / "libonnxruntime.so";
#endif
        std::error_code e2;
        if (fs::exists(lib, e2)) qputenv("BA_ORT_GPU", QByteArray::fromStdString(lib.string()));
    }
    // Las tarjetas guardan el id del plugin; al mandarlas a la cadena de la
    // entrada se traduce a lo de esta máquina (CoreML, ONNX…).
    if (video_ != nullptr)
        video_->setResolverPlugin(
            [this](const std::string& id, std::string* error) { return resolverPlugin(id, error); });
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
    if (video_ != nullptr) video_->setResolverPlugin({});
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

QStringList SuperRes::motores() const {
    return video_ != nullptr ? video_->motoresSuperres() : QStringList{};
}

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

std::string SuperRes::correr(const QString& que, const QString& entrada, const QStringList& args,
                             std::function<void(bool)> fin) {
    if (hijo_ && hijo_->corriendo())
        return "ya hay un trabajo andando: «" + trabajo_.value("que").toString().toStdString() + "»";
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
        if (!ok && !trabajo_.contains("error"))
            trabajo_["error"] = QString("terminó con código %1").arg(codigo);
        if (ok) trabajo_["pct"] = 100;
        emit trabajoCambio();
        emit cambio();
        auto f = std::move(fin_);
        fin_ = nullptr;
        if (f) QTimer::singleShot(0, this, [f = std::move(f), ok]() { f(ok); });
    });
    trabajo_ = {
        {"que", que}, {"entrada", entrada},     {"corriendo", true},
        {"pct", 0},   {"texto", "Arrancando…"}, {"desde", QDateTime::currentDateTime().toString("HH:mm:ss")}};
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
    for (const auto& [k, v] : m.items())
        trabajo_[QString::fromStdString(k)] = qv(v);
    if (m.value("ev", "") == "error")
        trabajo_["error"] = QString::fromStdString(m.value("error", std::string("falló")));
    emit trabajoCambio();
}

std::string SuperRes::prepararLaboratorio() {
    if (laboratorioListo()) return {};
    std::error_code ec;
    fs::create_directories(libs(), ec);
    return correr("Preparar el laboratorio (PyTorch, ~800 MB)", {},
                  QStringList{"-m", "pip", "install", "--disable-pip-version-check",
                              "--no-warn-script-location", "--progress-bar", "off", "--target", q8(libs())} +
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
    if (uri.empty())
        return "«" + entrada + "» no es una entrada de red (se entrena grabando de su dirección)";
    minutos = std::clamp(minutos, 1.0, 120.0);
    std::string seguro;
    for (char c : entrada)
        seguro += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '_';
    const fs::path salida = carpetaModelos() / ("chica-" + seguro + ".onnx");
    const auto lanzar = [=, this]() {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        QStringList a{q8(lab),      "chica",
                      "--libs",     q8(libs()),
                      "--uri",      QString::fromStdString(uri),
                      "--segundos", QString::number(segundos),
                      "--minutos",  QString::number(minutos),
                      "--salida",   q8(salida),
                      "--nombre",   QString::fromStdString("Red chica de «" + entrada + "»")};
        std::error_code ec;
        if (fs::exists(salida.string() + ".pt", ec)) a << "--desde" << q8(salida);
        return correr(
            "Entrenar con lo que ve «" + QString::fromStdString(entrada) + "»",
            QString::fromStdString(entrada), a, [this, entrada, salida, aplicarAlFin](bool ok) {
                if (ok && aplicarAlFin) {
                    const std::string e = aplicar(entrada, "onnx", salida.string());
                    trabajo_["texto"] =
                        e.empty() ? QString("Listo y aplicado a «%1»").arg(QString::fromStdString(entrada))
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

std::string SuperRes::destilar(const std::string& entrada, const std::string& maestro, double minutos,
                               bool aplicarAlFin, bool cpu) {
    if (video_ == nullptr) return "no hay video";
    const std::string uri = video_->uriDe(entrada);
    if (uri.empty()) return "«" + entrada + "» no es una entrada de red (se destila grabando de su dirección)";
    json receta;
    for (const auto& r : catalogo_)
        if (r.value("id", "") == (maestro.empty() ? std::string("realesr-general-x4v3") : maestro)) receta = r;
    if (!receta.is_object() || receta.value("tipo", "") != "red")
        return "el maestro tiene que ser una red del catálogo (superres_plugins: realesr-general-x4v3, lsdir-compact-x4…)";
    minutos = std::clamp(minutos, 1.0, 240.0);
    std::string seguro;
    for (char c : entrada)
        seguro += (std::isalnum(static_cast<unsigned char>(c)) || c == '-') ? c : '_';
    const fs::path salida = carpetaModelos() / ("destilada-" + seguro + ".onnx");
    const fs::path archivoReceta = carpetaModelos() / (".maestro-" + seguro + ".json");
    std::ofstream(archivoReceta) << receta.dump(1);
    const auto lanzar = [=, this]() {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        QStringList a{q8(lab),     "destilar",          "--libs",     q8(libs()),
                      "--receta",  q8(archivoReceta),   "--uri",      QString::fromStdString(uri),
                      "--segundos", "60",               "--minutos",  QString::number(minutos),
                      "--salida",  q8(salida),          "--pesos",    q8(carpetaModelos() / ".pesos"),
                      "--nombre",  QString::fromStdString("Destilada de " + receta.value("nombre", maestro) +
                                                           " para «" + entrada + "»")};
        if (cpu) a << "--cpu";
        std::error_code ec;
        if (fs::exists(salida.string() + ".pt", ec)) a << "--desde" << q8(salida);
        return correr("Destilar «" + QString::fromStdString(receta.value("nombre", maestro)) + "» para «" +
                          QString::fromStdString(entrada) + "»",
                      QString::fromStdString(entrada), a, [this, entrada, salida, aplicarAlFin](bool ok) {
                          if (ok && aplicarAlFin) {
                              const std::string e = aplicar(entrada, "onnx", salida.string());
                              trabajo_["texto"] = e.empty() ? QString("Lista y aplicada a «%1»")
                                                                  .arg(QString::fromStdString(entrada))
                                                            : QString::fromStdString("Lista, pero no se pudo aplicar: " + e);
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

std::string SuperRes::aplicar(const std::string& entrada, const std::string& motor,
                              const std::string& modelo) {
    if (video_ == nullptr) return "no hay video";
    std::string ruta = modelo;
    if (motor == "onnx" && !modelo.empty() && modelo.find('/') == std::string::npos &&
        modelo.find('\\') == std::string::npos)
        ruta = (carpetaModelos() / modelo).string();
    std::error_code ec;
    if (motor == "onnx" && !fs::exists(ruta, ec))
        return "no existe el modelo «" + modelo + "» (superres_estado lista los que hay)";
    return video_->superresMotor(entrada, motor, motor == "onnx" ? ruta : std::string());
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

json SuperRes::plugins() const {
    json out = json::array();
    std::set<std::string> vistos;
    const QStringList motores = this->motores();
    auto leerPlugin = [this](const std::string& id) -> json {
        std::ifstream f(carpetaModelos() / id / "plugin.json");
        if (!f) return nullptr;
        const json j = json::parse(f, nullptr, false);
        return j.is_object() ? j : json(nullptr);
    };
    for (json r : catalogo_) {
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
    for (const auto& r : catalogo_)
        if (r.value("id", "") == id) receta = r;
    if (!receta.is_object()) return "no hay un plugin «" + id + "» en el catálogo (superres_plugins los lista)";
    if (receta.value("tipo", "") == "integrado") return "«" + id + "» viene con el programa: no hace falta instalarlo";
    const fs::path dir = carpetaModelos() / id;
    const fs::path archivoReceta = carpetaModelos() / (".receta-" + id + ".json");
    std::ofstream(archivoReceta) << receta.dump(1);
    const auto lanzar = [this, receta, dir, archivoReceta]() {
        const fs::path lab = ba::platform::bundleRoot() / "superres" / "laboratorio.py";
        return correr("Instalar «" + QString::fromStdString(receta.value("nombre", std::string())) + "»", {},
                      {q8(lab), "instalar", "--receta", q8(archivoReceta), "--salida", q8(dir), "--libs", q8(libs())},
                      [this, archivoReceta](bool) {
                          std::error_code ec;
                          fs::remove(archivoReceta, ec);
                          emit cambio();
                      });
    };
    // Una red necesita el laboratorio (PyTorch); un shader, no.
    if (receta.value("tipo", "") == "red" && !laboratorioListo()) {
        despues_ = [lanzar]() { lanzar(); };
        return prepararLaboratorio();
    }
    return lanzar();
}

json SuperRes::resolverPlugin(const std::string& id, std::string* error) const {
    auto fallar = [&](const std::string& e) {
        if (error) *error = e;
        return json::object();
    };
    if (id == "apple" || id == "fsr") {
        if (!motores().contains(QString::fromStdString(id))) return fallar("este sistema no tiene «" + id + "»");
        return json{{"motor", id}, {"modelo", ""}, {"escala_red", 2}};
    }
    const fs::path dir = carpetaModelos() / id;
    std::ifstream f(dir / "plugin.json");
    if (!f) {
        // Una red suelta (las de antes: <id>.onnx).
        std::error_code ec;
        if (fs::exists(carpetaModelos() / (id + ".onnx"), ec))
            return json{{"motor", "onnx"}, {"modelo", (carpetaModelos() / (id + ".onnx")).string()}, {"escala_red", 2}};
        return fallar("el plugin «" + id + "» no está instalado (superres_instalar)");
    }
    const json p = json::parse(f, nullptr, false);
    if (!p.is_object()) return fallar("plugin.json de «" + id + "» roto");
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
        if (el.empty()) return fallar("plugin.json de «" + id + "» sin `elemento`");
#if defined(BA_HAVE_GST)
        std::error_code e3;
        if (fs::is_directory(dir / "gst", e3)) gst_registry_scan_path(gst_registry_get(), (dir / "gst").string().c_str());
        GstElementFactory* fac = gst_element_factory_find(el.c_str());
        if (fac == nullptr) return fallar("GStreamer no tiene el elemento «" + el + "» (¿falta su biblioteca en " +
                                          (dir / "gst").string() + "?)");
        gst_object_unref(fac);
#endif
        json props = json::object();
        for (const auto obj_ = p.value("propiedades", json::object()); const auto& [k, v] : obj_.items()) {
            std::string t = v.is_string() ? v.get<std::string>() : v.dump();
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
        if (!archivos.contains("glsl")) return fallar("plugin.json de «" + id + "» sin shader");
        if (!motores().contains("glsl")) return fallar("este sistema no tiene el motor de shaders (baglsl)");
        return json{{"motor", "glsl"}, {"modelo", (dir / archivos["glsl"].get<std::string>()).string()}, {"escala_red", 2}};
    }
    const int escala = p.value("escala", 2);
    const int escalaRed = escala == 4 ? 4 : 2; // una x4 entra a la mitad y sale x2
#if defined(__APPLE__)
    if (archivos.contains("coreml") && motores().contains("coreml"))
        // FP32 (redes que se desbordan en FP16, como SPAN): en la GPU, el Neural Engine no la corre.
        return json{{"motor", "coreml"}, {"modelo", (dir / archivos["coreml"].get<std::string>()).string()},
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
                                     {"proveedor", "auto"}}},
                    {"memoria", "CUDAMemory"},
                    {"memoria_salida", ""},
                    {"escala_red", 2}};
#endif
    // La versión NV12 (todo en la placa: color, mitad, vuelta a NV12) si está;
    // si no, la float (baonnxsr igual le da la mitad a una x4).
    for (const char* clave : {"onnx_nv12", "onnx"})
        if (archivos.contains(clave) && motores().contains("onnx"))
            return json{{"motor", "onnx"}, {"modelo", (dir / archivos[clave].get<std::string>()).string()},
                        {"escala_red", 2}};
    return fallar("«" + id + "» no tiene un modelo que corra en esta máquina");
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
    const fs::path dir = root_ / "Herramientas" / "onnxruntime-gpu";
    std::ifstream f(dir / "runtime.json");
    if (f) {
        const json r = json::parse(f, nullptr, false);
        if (r.is_object()) out["instalado"] = r;
    }
    const std::string placa = out["placa"];
#if defined(__APPLE__)
    out["sugerido"] = "";
    out["nota"] = "en la Mac el ONNX Runtime del programa ya usa CoreML (GPU y Neural Engine), y las redes "
                  "instaladas corren en CoreML nativo: no hace falta bajar nada";
#elif defined(_WIN32)
    out["sugerido"] = placa == "nvidia" ? "nvidia" : placa == "intel" ? "intel" : "";
    out["nota"] = placa == "amd" || placa.empty()
                      ? "con AMD (también las integradas Ryzen) el programa ya usa DirectML: no hace falta bajar nada"
                      : "DirectML ya anda; el runtime de " + placa + " es más rápido (TensorRT compila la red para la placa)";
#else
    out["sugerido"] = placa;
    if (placa == "amd")
        out["nota"] = "AMD en Linux usa ROCm/MIGraphX: necesita el controlador amdgpu y ROCm 6.4 del sistema";
#endif
    out["activo"] = !qEnvironmentVariableIsEmpty("BA_ORT_GPU");
    return out;
}

std::string SuperRes::instalarRuntimeGpu(const std::string& tipoPedido, bool conTensorRt) {
#if defined(__APPLE__)
    (void)tipoPedido;
    (void)conTensorRt;
    return "en la Mac no hace falta: CoreML ya viene";
#else
    const std::string tipo = tipoPedido.empty() ? runtimeGpu().value("sugerido", std::string()) : tipoPedido;
    if (tipo != "nvidia" && tipo != "amd" && tipo != "intel")
        return "no hay un runtime de GPU para bajar acá (tipo: nvidia, amd o intel)";
    const fs::path script = ba::platform::bundleRoot() / "superres" / "runtime_gpu.py";
    QStringList args{q8(script), "--tipo", QString::fromStdString(tipo), "--salida",
                     q8(root_ / "Herramientas" / "onnxruntime-gpu")};
    if (!conTensorRt) args << "--sin-tensorrt";
    return correr("Bajar el ONNX Runtime de GPU (" + QString::fromStdString(tipo) + ")", {}, args, [](bool) {});
#endif
}

std::string SuperRes::tarjetaConPlugin(const std::string& entrada, const std::string& id) {
    if (video_ == nullptr) return "no hay video";
    std::string err;
    if (resolverPlugin(id, &err).empty()) return err;
    return video_->superresPlugin(entrada, id);
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
