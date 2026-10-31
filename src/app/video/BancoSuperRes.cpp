// ============================================================================
// src/app/video/BancoSuperRes.cpp — ver BancoSuperRes.hpp
// ============================================================================
#include "video/BancoSuperRes.hpp"
#include "arranque/ArchivoConfig.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

#include <gst/gst.h>

#include "BancoSr.hpp"
#include "video/SuperRes.hpp"
#include "platform/Hilo.hpp"

namespace ba::app::video {

namespace fs = std::filesystem;

namespace {

QVariantMap aQv(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

nlohmann::json deQv(const QVariantMap& m) {
    return nlohmann::json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson().toStdString(), nullptr, false);
}

/// Los proveedores que trae el ONNX Runtime del programa (y vulkan si hay ncnn).
std::vector<std::string> proveedoresOnnx() {
    std::vector<std::string> out;
    GstElement* e = gst_element_factory_make("baonnxsr", nullptr);
    if (e == nullptr) return out;
    gchar* l = nullptr;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(e), "proveedores") != nullptr) g_object_get(e, "proveedores", &l, nullptr);
    std::string s = l != nullptr ? l : "";
    g_free(l);
    gst_object_unref(e);
    for (size_t i = 0; i <= s.size();) {
        const size_t j = s.find(',', i);
        const std::string p = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!p.empty()) out.push_back(p);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

std::string nombreAcelerador(const std::string& a) {
    static const std::map<std::string, std::string> n{
        {"ane", "Neural Engine"}, {"gpu", "GPU"},        {"cpu", QT_TRANSLATE_NOOP("superres", "procesador")},{"dml", "DirectML"},
        {"tensorrt", "TensorRT"}, {"cuda", "CUDA"},      {"migraphx", "MIGraphX"},  {"rocm", "ROCm"},
        {"openvino", "OpenVINO"}, {"vulkan", "Vulkan"},  {"coreml", "CoreML"},      {"gl", "OpenGL"},
        {"videotoolbox", "VideoToolbox"}};
    const auto it = n.find(a);
    return it == n.end() ? a : QCoreApplication::translate("superres", it->second.c_str()).toStdString();
}

} // namespace

BancoSuperRes::BancoSuperRes(fs::path dataRoot, SuperRes& sr, Entorno en, QObject* parent)
    : QObject(parent), archivo_(dataRoot / "Config" / "superres-banco.json"), sr_(sr), en_(std::move(en)) {
    if (const json j = config::leerObjeto(archivo_); !j.empty()) resultado_ = j;
}

BancoSuperRes::~BancoSuperRes() {
    cancelar_ = true;
    if (hilo_.joinable()) hilo_.join();
}

std::string BancoSuperRes::veredicto(double ms, double cuadroMs) {
    if (ms <= 0) return "no_corre";
    if (ms <= cuadroMs * 0.85) return "en_vivo";
    if (ms <= cuadroMs * 2.5) return "reducido";
    return "diferido";
}

nlohmann::json BancoSuperRes::candidatos() const {
    json out = json::array();
    const QStringList motores = sr_.motores();
    const auto agregar = [&](const std::string& metodo, const std::string& nombre, const std::string& acel, const json& props,
                             bool soloChico = false, const std::string& elemento = "basuperres") {
        out.push_back(json{{"id", metodo + "@" + acel},
                           {"metodo", metodo},
                           {"nombre", nombre},
                           {"acelerador", acel},
                           {"acelerador_nombre", nombreAcelerador(acel)},
                           {"elemento", elemento},
                           {"propiedades", props},
                           {"solo_chico", soloChico}});
    };
    if (motores.contains("fsr")) agregar("fsr", "FSR 1 (AMD)", "gl", {{"motor", "fsr"}});
    if (motores.contains("apple")) agregar("apple", "Apple (VideoToolbox)", "videotoolbox", {{"motor", "apple"}});
    const std::vector<std::string> provs = proveedoresOnnx();
    for (const auto& p : sr_.plugins()) {
        const std::string id = p.value("id", std::string());
        if (id.empty() || id == "fsr" || id == "apple" || !p.value("instalado", false) || !p.value("aca", true)) continue;
        std::string err;
        const json r = sr_.resolverPlugin(id, &err);
        if (!r.is_object() || r.empty()) continue;
        const std::string nombre = p.value("nombre", id);
        if (r.contains("elemento")) {
            // Un elemento propio: solo si trabaja en memoria del sistema (los de CUDA y
            // D3D12 necesitan su subida; se miden en la tarjeta, con la cámara).
            if (!r.value("memoria", std::string()).empty()) continue;
            json props = json::object();
            const json pr = r.value("propiedades", json::object()); // (no .items() de un temporal)
            for (const auto& [k, v] : pr.items()) props[k] = v.is_string() ? v.get<std::string>() : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            agregar(id, nombre, "propio", props, false, r.value("elemento", std::string()));
            continue;
        }
        const std::string motor = r.value("motor", std::string());
        json base{{"motor", motor}};
        if (!r.value("modelo", std::string()).empty()) base["modelo"] = r["modelo"];
        if (!r.value("variantes", std::string()).empty()) base["variantes"] = r["variantes"];
        if (r.value("escala_red", 2) > 2) base["escala-red"] = std::to_string(r.value("escala_red", 2));
        if (motor == "glsl") {
            agregar(id, nombre, "gl", base);
        } else if (motor == "coreml") {
            // Neural Engine y GPU (una que en FP16 se desborda solo va en la GPU).
            if (r.value("unidades", std::string("auto")) != "gpu") {
                json a = base;
                a["unidades"] = "2";
                agregar(id, nombre, "ane", a);
            }
            json g = base;
            g["unidades"] = "3";
            agregar(id, nombre, "gpu", g);
        } else if (motor == "onnx") {
            for (const std::string& pr : provs) {
                if (pr == "coreml" && motores.contains("coreml")) continue; // en la Mac, la red CoreML ya está
                json a = base;
                a["proveedor"] = pr;
                if (pr == "vulkan" && r.contains("modelo_float")) {
                    a["modelo"] = r["modelo_float"];
                    a.erase("variantes"); // las variantes son de la NV12
                }
                agregar(id, nombre, pr, a, pr == "cpu"); // el procesador, solo a 540p (si no, minutos)
            }
        }
    }
    return out;
}

std::string BancoSuperRes::correr(const json& op) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (estado_.value("corriendo", false)) return tr("ya está midiendo").toStdString();
    }
    if (!op.value("forzar", false) && en_.alAire && en_.alAire())
        return tr("hay una transmisión o una grabación andando: medir le quita placa (y lo medido sale peor). Con "
                  "`forzar` mide igual")
            .toStdString();
    json cands = candidatos();
    if (op.contains("metodos") && op["metodos"].is_array() && !op["metodos"].empty()) {
        std::set<std::string> m;
        for (const auto& x : op["metodos"]) m.insert(x.get<std::string>());
        json f = json::array();
        for (const auto& c : cands)
            if (m.count(c.value("metodo", std::string())) || m.count(c.value("id", std::string()))) f.push_back(c);
        cands = f;
    }
    if (cands.empty()) return tr("no hay nada para medir (¿ninguna superresolución instalada?)").toStdString();
    std::vector<std::pair<int, int>> tamanos;
    const auto agregarTam = [&](int w, int h) {
        w &= ~1, h &= ~1;
        if (w < 64 || h < 64 || w > 7680 || h > 4320) return;
        if (std::find(tamanos.begin(), tamanos.end(), std::make_pair(w, h)) == tamanos.end()) tamanos.push_back({w, h});
    };
    if (op.contains("resoluciones") && op["resoluciones"].is_array() && !op["resoluciones"].empty()) {
        for (const auto& r : op["resoluciones"]) {
            int w = 0, h = 0;
            if (r.is_string() && std::sscanf(r.get<std::string>().c_str(), "%dx%d", &w, &h) == 2) agregarTam(w, h);
        }
    } else {
        agregarTam(960, 540);
        agregarTam(1280, 720);
        agregarTam(1920, 1080);
        if (en_.tamanosCamaras)
            for (const auto& [w, h] : en_.tamanosCamaras()) agregarTam(w, h);
    }
    if (tamanos.empty()) return tr("ninguna resolución válida («1280x720»)").toStdString();
    std::sort(tamanos.begin(), tamanos.end(), [](auto a, auto b) { return a.first * a.second < b.first * b.second; });
    const double segundos = std::clamp(op.value("segundos", 4.0), 1.0, 30.0);
    const auto [fpsN, fpsD] = en_.fps ? en_.fps() : std::make_pair(30, 1);
    int total = 0;
    for (const auto& c : cands) total += c.value("solo_chico", false) ? 1 : static_cast<int>(tamanos.size());
    {
        std::lock_guard<std::mutex> l(mu_);
        estado_ = {{"corriendo", true}, {"hechas", 0}, {"total", total}, {"actual", ""}, {"parciales", json::array()},
                   {"desde_ms", QDateTime::currentMSecsSinceEpoch()}};
    }
    cancelar_ = false;
    if (hilo_.joinable()) hilo_.join();
    hilo_ = std::thread([this, cands, tamanos, segundos, fpsN = fpsN, fpsD = fpsD]() {
        ba::platform::nombrarHilo("banco-superres");
        // Nada del banco puede tirar abajo la app: una excepción termina la medición y se dice.
        try {
            trabajar(cands, tamanos, segundos, fpsN, fpsD);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> l(mu_);
            estado_["corriendo"] = false;
            estado_["actual"] = tr("error: %1").arg(QString::fromUtf8(e.what())).toStdString();
        }
    });
    emit cambio();
    return {};
}

QString BancoSuperRes::correrUi(const QVariantMap& op) { return QString::fromStdString(correr(deQv(op))); }

void BancoSuperRes::cancelar() { cancelar_ = true; }

void BancoSuperRes::trabajar(json cands, std::vector<std::pair<int, int>> tamanos, double segundos, int fpsN, int fpsD) {
    const double cuadroMs = 1000.0 * fpsD / std::max(1, fpsN);
    json resultados = json::array();
    QPointer<BancoSuperRes> self(this);
    const auto avisar = [self]() {
        QMetaObject::invokeMethod(qApp, [self]() { if (self) emit self->cambio(); }, Qt::QueuedConnection);
    };
    int hechas = 0;
    for (const auto& c : cands) {
        for (const auto& [w, h] : tamanos) {
            if (cancelar_) break;
            if (c.value("solo_chico", false) && (w != tamanos.front().first || h != tamanos.front().second)) continue;
            {
                std::lock_guard<std::mutex> l(mu_);
                estado_["actual"] = c.value("nombre", std::string()) + " · " + c.value("acelerador_nombre", std::string()) + " · " +
                                    std::to_string(w) + "×" + std::to_string(h);
            }
            avisar();
            motor::CandidatoSr cs;
            cs.elemento = c.value("elemento", std::string("basuperres"));
            cs.sincronico = c.value("acelerador", std::string()) == "gl"; // FSR y los shaders
            const json pr = c.value("propiedades", json::object()); // (no .items() de un temporal)
            for (const auto& [k, v] : pr.items()) cs.propiedades.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            // El procesador puede tardar en dar el primer cuadro: se le da menos espera.
            const double espera = c.value("acelerador", std::string()) == "cpu" ? 30.0 : 90.0;
            const motor::MedidaSr m = motor::medirSuperRes(cs, w, h, fpsN, fpsD, segundos, espera, &cancelar_);
            if (cancelar_) break;
            const std::string v = m.error.empty() ? veredicto(m.ms, cuadroMs) : "no_corre";
            json r{{"id", c["id"]},
                   {"metodo", c["metodo"]},
                   {"nombre", c["nombre"]},
                   {"acelerador", c["acelerador"]},
                   {"acelerador_nombre", c["acelerador_nombre"]},
                   {"ancho", w},
                   {"alto", h},
                   {"ms", std::round(m.ms * 10) / 10},
                   {"fps", m.ms > 0 ? std::round(10000.0 / m.ms) / 10 : 0.0},
                   {"veredicto", v},
                   {"simultaneas", v == "en_vivo" ? static_cast<int>(cuadroMs * 0.85 / m.ms) : 0},
                   {"como", m.como},
                   {"abrir_s", std::round(m.abrirSeg * 10) / 10}};
            if (!m.acelerador.empty()) r["acelerador_usado"] = m.acelerador;
            if (!m.error.empty()) r["error"] = m.error;
            resultados.push_back(r);
            ++hechas;
            {
                std::lock_guard<std::mutex> l(mu_);
                estado_["hechas"] = hechas;
                estado_["parciales"] = resultados;
            }
            avisar();
        }
        if (cancelar_) break;
    }
    // Lo mejor para cada tamaño: lo más rápido que corra en vivo; si nada, lo más rápido.
    json recomendado = json::object();
    for (const auto& [w, h] : tamanos) {
        const json* mejor = nullptr;
        for (const auto& r : resultados) {
            if (r.value("ancho", 0) != w || r.value("alto", 0) != h || r.value("ms", 0.0) <= 0) continue;
            // Entre lo que corre en vivo, el de más calidad: una red, después Apple o un
            // shader, después FSR; si nada va en vivo, lo más rápido.
            const auto puntaje = [&](const json& x) {
                const bool vivo = x.value("veredicto", std::string()) == "en_vivo";
                const std::string met = x.value("metodo", std::string()), acel = x.value("acelerador", std::string());
                const double calidad = met == "fsr" ? 1.0 : (met == "apple" || acel == "gl") ? 2.0 : 3.0;
                return (vivo ? 10.0 + calidad : 0.0) - x.value("ms", 0.0) / 10000.0;
            };
            if (mejor == nullptr || puntaje(r) > puntaje(*mejor)) mejor = &r;
        }
        if (mejor != nullptr)
            recomendado[std::to_string(w) + "x" + std::to_string(h)] = json{{"id", (*mejor)["id"]},
                                                                           {"nombre", (*mejor)["nombre"]},
                                                                           {"acelerador", (*mejor)["acelerador_nombre"]},
                                                                           {"ms", (*mejor)["ms"]},
                                                                           {"veredicto", (*mejor)["veredicto"]}};
    }
    const bool cancelado = cancelar_.load();
    json res{{"fecha_ms", QDateTime::currentMSecsSinceEpoch()},
             {"fecha", QDateTime::currentDateTime().toString("dd/MM/yyyy HH:mm").toStdString()},
             {"maquina",
              {{"sistema", QSysInfo::prettyProductName().toStdString()},
               {"arquitectura", QSysInfo::currentCpuArchitecture().toStdString()},
               {"nombre", QSysInfo::machineHostName().toStdString()},
               {"placa", sr_.runtimeGpu().value("placa", std::string())}}},
             {"fps", static_cast<double>(fpsN) / std::max(1, fpsD)},
             {"cuadro_ms", std::round(cuadroMs * 10) / 10},
             {"segundos_por_prueba", segundos},
             {"completo", !cancelado},
             {"resultados", resultados},
             {"recomendado", recomendado}};
    if (!cancelado || !resultados.empty()) {
        config::guardar(archivo_, res);
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!cancelado || !resultados.empty()) resultado_ = res;
        estado_["corriendo"] = false;
        estado_["actual"] = (cancelado ? tr("cancelado") : tr("listo")).toStdString();
    }
    avisar();
}

nlohmann::json BancoSuperRes::estado() const {
    std::lock_guard<std::mutex> l(mu_);
    json e = estado_;
    if (e.value("corriendo", false) && e.value("hechas", 0) > 0) {
        const double paso = (QDateTime::currentMSecsSinceEpoch() - e.value("desde_ms", 0LL)) / 1000.0 / e.value("hechas", 1);
        e["eta_s"] = static_cast<int>(paso * (e.value("total", 0) - e.value("hechas", 0)));
    }
    return e;
}

nlohmann::json BancoSuperRes::resultado() const {
    std::lock_guard<std::mutex> l(mu_);
    return resultado_;
}

QVariantMap BancoSuperRes::estadoUi() const { return aQv(estado()); }
QVariantMap BancoSuperRes::resultadoUi() const { return aQv(resultado()); }

double BancoSuperRes::estimarMs(const std::string& plugin, int ancho, int alto) const {
    std::lock_guard<std::mutex> l(mu_);
    if (ancho <= 0 || alto <= 0) return 0;
    double mejor = 0, mejorDif = 1e18;
    const double px = static_cast<double>(ancho) * alto;
    for (const auto& r : resultado_.value("resultados", json::array())) {
        if (r.value("metodo", std::string()) != plugin || r.value("ms", 0.0) <= 0) continue;
        const double rpx = static_cast<double>(r.value("ancho", 0)) * r.value("alto", 0);
        if (rpx <= 0) continue;
        const double dif = std::abs(std::log(px / rpx));
        const double estimado = r.value("ms", 0.0) * px / rpx; // a más píxeles, más tiempo
        // El tamaño más parecido; entre los del mismo tamaño, el acelerador más rápido.
        if (dif < mejorDif - 1e-9 || (std::abs(dif - mejorDif) < 1e-9 && estimado < mejor)) {
            mejorDif = dif;
            mejor = estimado;
        }
    }
    return mejor;
}

} // namespace ba::app::video
