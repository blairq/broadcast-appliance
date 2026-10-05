// SPDX-License-Identifier: GPL-2.0-or-later
#include "legislatura/ConferenciaDcn.hpp"

#include "control/BusEventos.hpp"
#include "plugins/Plugins.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <filesystem>
#include <fstream>

namespace ba::app::legislatura {

using json = nlohmann::json;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).object().value("v").toVariant();
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

ConferenciaDcn::ConferenciaDcn(std::function<plugins::Plugins*()> plugins, control::BusEventos& bus, QObject* parent)
    : QObject(parent), plugins_(std::move(plugins)), bus_(bus) {
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
QVariantMap ConferenciaDcn::configUi() const { return aQv(config_).toMap(); }

void ConferenciaDcn::control(const std::string& nombre, const json& args, Listo listo, int timeoutMs) {
    plugins::Plugins* p = plugins_ ? plugins_() : nullptr;
    if (p == nullptr) return listo(false, {}, "los plugins no están disponibles");
    if (p->estado(kPlugin).value("estado", std::string()) != "corriendo")
        return listo(false, {}, "el control de la central (plugin bosch-dcn) no está corriendo: conectalo en "
                                "Legislatura → Conferencia (o dcn_conectar)");
    p->pedir(kPlugin, "control", {{"nombre", nombre}, {"args", args}}, [listo](json r) {
        if (!r.value("ok", false)) return listo(false, {}, limpio(r.value("error", std::string("falló"))));
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

} // namespace ba::app::legislatura
