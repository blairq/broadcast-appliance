// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/ConferenciaDcn.hpp"

#include "control/BusEventos.hpp"
#include "plugins/Plugins.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <filesystem>
#include <fstream>

namespace ba::app::asamblea {

using json = nlohmann::json;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}
json deQv(const QVariantMap& m) {
    return json::parse(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(),
                       nullptr, false);
}
/// El SDK devuelve «RuntimeError: …»: el operador no tiene por qué ver el tipo de excepción.
std::string limpio(std::string e) {
    for (const char* p : {"RuntimeError: ", "ValueError: ", "KeyError: "})
        if (e.rfind(p, 0) == 0) return e.substr(std::string(p).size());
    return e;
}
} // namespace

ConferenciaDcn::ConferenciaDcn(std::function<plugins::Plugins*()> plugins, control::BusEventos& bus,
                               std::filesystem::path datos, QObject* parent)
    : QObject(parent), plugins_(std::move(plugins)), bus_(bus), archivo_(datos / "Config" / "conferencia.json") {
    {
        std::ifstream f(archivo_);
        const json j = json::parse(f, nullptr, false);
        if (j.is_object()) presentesCentral_ = j.value("presentes_de_la_central", false);
    }
    bus_.suscribir(std::string("plugin/") + kPlugin + "/dcn/estado", [this](const std::string&, const json& v) {
        estado_ = v.is_object() ? v : json::object();
        emit cambio();
    });
    // El último que quedó en el bus (si el plugin ya estaba corriendo).
    const json u = bus_.ultimo(std::string("plugin/") + kPlugin + "/dcn/estado");
    if (u.is_object() && u.value("valor", json()).is_object()) estado_ = u["valor"];
    refrescar();
}

void ConferenciaDcn::refrescar() {
    plugins::Plugins* p = plugins_ ? plugins_() : nullptr;
    if (p == nullptr) return;
    std::ifstream f(p->carpetaDe(kPlugin) / "plugin.json");
    const json man = json::parse(f, nullptr, false);
    config_ = man.is_object() ? man.value("config", json::object()) : json::object();
    emit cambio();
}

bool ConferenciaDcn::tieneUnidad(const std::string& banca) const {
    for (const auto& u : estado_.value("unidades", json::array()))
        if (u.value("banca", std::string()) == banca && !banca.empty()) return true;
    return false;
}

QString ConferenciaDcn::pluginUi() const {
    plugins::Plugins* p = plugins_ ? plugins_() : nullptr;
    if (p == nullptr || !std::filesystem::exists(p->carpetaDe(kPlugin) / "plugin.json")) return "no_instalado";
    return QString::fromStdString(p->estado(kPlugin).value("estado", std::string("apagado")));
}

QVariantMap ConferenciaDcn::estadoUi() const { return aQv(estado_).toMap(); }

void ConferenciaDcn::setPresentesDeLaCentral(bool si) {
    if (si == presentesCentral_) return;
    presentesCentral_ = si;
    std::error_code ec;
    std::filesystem::create_directories(archivo_.parent_path(), ec);
    std::ofstream(archivo_) << json{{"presentes_de_la_central", si}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    emit cambio();
}

int ConferenciaDcn::presentesCentral() const {
    if (!conectada()) return -1;
    const json a = estado_.value("asistencia", json::object());
    return a.contains("presentes") ? a.value("presentes", -1) : -1;
}

json ConferenciaDcn::pasarAQuorum(const std::string& asunto) {
    if (!aQuorum_) return {{"error", "Quórum no está disponible"}};
    const json v = estado_.value("votacion", json::object());
    if (v.value("estado", std::string()) != "cerrada" || !v.contains("totales"))
        return {{"error", "no hay una votación cerrada en la central (cerrala primero)"}};
    const json t = v["totales"];
    json nominal = json::array();
    for (const auto& n : v.value("nominal", json::array())) {
        std::string quien = n.value("nombre", std::string());
        if (quien.empty()) quien = n.value("banca", std::string()).empty() ? "unidad " + std::to_string(n.value("unidad", 0))
                                                                              : "Banca " + n.value("banca", std::string());
        nominal.push_back({{"nombre", quien}, {"banca", n.value("banca", std::string())}, {"voto", n.value("voto", std::string())}});
    }
    json r{{"asunto", asunto.empty() ? v.value("tema", std::string()) : asunto},
           {"afirmativos", t.value("afirmativo", 0)},
           {"negativos", t.value("negativo", 0)},
           {"abstenciones", t.value("abstencion", 0)},
           {"presentes", v.value("presentes", 0)},
           {"nominal", nominal},
           {"origen", "central Bosch DCN"}};
    const json res = aQuorum_(r);
    if (res.contains("error")) return res;
    return {{"resultado", res}};
}

QString ConferenciaDcn::pasarAQuorumUi() {
    const json r = pasarAQuorum();
    return QString::fromStdString(r.value("error", std::string()));
}
QVariantMap ConferenciaDcn::configUi() const { return aQv(config_).toMap(); }

void ConferenciaDcn::control(const std::string& nombre, const json& args, Listo listo, int timeoutMs) {
    plugins::Plugins* p = plugins_ ? plugins_() : nullptr;
    if (p == nullptr) return listo(false, {}, "los plugins no están disponibles");
    if (p->estado(kPlugin).value("estado", std::string()) != "corriendo")
        return listo(false, {}, "el control de la central (plugin bosch-dcn) no está corriendo: conectalo en "
                                "Asamblea → Conferencia (o dcn_conectar)");
    QPointer<ConferenciaDcn> yo(this);
    p->pedir(kPlugin, "control", {{"nombre", nombre}, {"args", args}}, [listo, nombre, yo](json r) {
        if (!r.value("ok", false)) return listo(false, {}, limpio(r.value("error", std::string("falló"))));
        // La votación que contestó la orden ya es la de ahora (el estado del bus llega después).
        if (yo && (nombre == "votacion" || nombre == "votacion_resultado") && r.value("resultado", json()).is_object()) {
            yo->estado_["votacion"] = r["resultado"];
            emit yo->cambio();
        }
        listo(true, r.value("resultado", json::object()), "");
    }, timeoutMs);
}

void ConferenciaDcn::conectar(const json& config, Listo listo) {
    plugins::Plugins* p = plugins_ ? plugins_() : nullptr;
    if (p == nullptr) return listo(false, {}, "los plugins no están disponibles");
    if (!std::filesystem::exists(p->carpetaDe(kPlugin) / "plugin.json")) {
        const std::string e = p->desdeEjemplo(kPlugin, kPlugin);
        if (!e.empty()) return listo(false, {}, "no se pudo instalar el plugin bosch-dcn: " + e);
    }
    QPointer<ConferenciaDcn> yo(this);
    const auto pedirConectar = [yo, config, listo]() {
        if (!yo) return;
        yo->control("conectar", config, [yo, listo](bool ok, const json& r, const std::string& e) {
            if (yo) yo->refrescar();
            listo(ok, r, e);
        });
    };
    if (p->estado(kPlugin).value("estado", std::string()) == "corriendo") return pedirConectar();
    p->activar(kPlugin, true, [pedirConectar, listo](json est) {
        if (est.contains("error")) return listo(false, {}, est.value("error", std::string()));
        pedirConectar();
    });
}

void ConferenciaDcn::pedirUi(const QString& que, const QString& nombre, const QVariantMap& args) {
    QPointer<ConferenciaDcn> yo(this);
    json a = deQv(args);
    a["quien"] = "interfaz";
    control(nombre.toStdString(), a, [yo, que](bool ok, const json& r, const std::string& e) {
        if (yo) emit yo->hecho(que, ok, aQv(r), QString::fromStdString(e));
    });
}

void ConferenciaDcn::conectarUi(const QVariantMap& config) {
    QPointer<ConferenciaDcn> yo(this);
    conectar(deQv(config), [yo](bool ok, const json& r, const std::string& e) {
        if (yo) emit yo->hecho("conectar", ok, aQv(r), QString::fromStdString(e));
    });
}

} // namespace ba::app::asamblea
