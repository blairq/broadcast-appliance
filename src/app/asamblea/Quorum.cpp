// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/Quorum.hpp"
#include "asamblea/Votos.hpp"
#include "i18n/I18n.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace ba::app::asamblea {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().value("v").toVariant();
}
const std::set<std::string> kMayorias{"simple", "absoluta", "dos_tercios_presentes", "dos_tercios_miembros"};

std::string minus(std::string s) {
    // Minúsculas sin tildes (las de los votos: abstención).
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < s.size()) {
            const unsigned char d = static_cast<unsigned char>(s[++i]);
            o += (d == 0xB3 || d == 0x93) ? 'o' : (d == 0xA1 || d == 0x81) ? 'a' : (d == 0xA9 || d == 0x89) ? 'e'
                 : (d == 0xAD || d == 0x8D) ? 'i' : (d == 0xBA || d == 0x9A) ? 'u' : '?';
            continue;
        }
        o += static_cast<char>(std::tolower(c));
    }
    return o;
}

} // namespace

// Va al acta: en el idioma del evento.
std::string Quorum::nombreMayoria(const std::string& m) {
    if (m == "absoluta") return ba::i18n::traducirEvento("asamblea", "mayoría absoluta");
    if (m == "dos_tercios_presentes") return ba::i18n::traducirEvento("asamblea", "dos tercios de los presentes");
    if (m == "dos_tercios_miembros") return ba::i18n::traducirEvento("asamblea", "dos tercios de los miembros");
    return ba::i18n::traducirEvento("asamblea", "mayoría simple");
}

Quorum::Quorum(const fs::path& datos, std::function<int()> presentes, std::function<int()> miembrosDelPadron, QObject* parent)
    : QObject(parent),
      archivoRegla_(datos / "Config" / "quorum.json"),
      archivoOficiales_(datos / "Config" / "votaciones-oficiales.json"),
      presentes_(std::move(presentes)),
      miembrosDelPadron_(std::move(miembrosDelPadron)) {
    {
        std::ifstream f(archivoRegla_);
        const json j = json::parse(f, nullptr, false);
        if (j.is_object()) {
            miembros_ = j.value("miembros", 0);
            minimo_ = j.contains("minimo") ? j["minimo"] : json("mitad_mas_uno");
            mayoria_ = j.value("mayoria", std::string("simple"));
        }
    }
    std::ifstream f(archivoOficiales_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_array()) oficiales_ = j;
}

void Quorum::guardar() const {
    std::error_code ec;
    fs::create_directories(archivoRegla_.parent_path(), ec);
    {
        std::ofstream o(archivoRegla_);
        o << regla().dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    std::ofstream o(archivoOficiales_);
    o << oficiales_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

json Quorum::regla() const { return {{"miembros", miembros_}, {"minimo", minimo_}, {"mayoria", mayoria_}}; }

std::string Quorum::setRegla(const json& r) {
    if (r.contains("miembros")) {
        const int m = r.value("miembros", 0);
        if (m < 0 || m > 1000) return ba::i18n::traducir("asamblea", "miembros entre 0 y 1000");
        miembros_ = m;
    }
    if (r.contains("minimo")) {
        const json& m = r["minimo"];
        if (!(m.is_string() && m.get<std::string>() == "mitad_mas_uno") && !(m.is_number_integer() && m.get<int>() >= 1))
            return ba::i18n::traducir("asamblea", "mínimo: \"mitad_mas_uno\" o un número");
        minimo_ = m;
    }
    if (r.contains("mayoria")) {
        const std::string m = r.value("mayoria", std::string());
        if (!kMayorias.count(m))
            return ba::i18n::traducir("asamblea", "mayoría: simple, absoluta, dos_tercios_presentes o dos_tercios_miembros");
        mayoria_ = m;
    }
    guardar();
    emit cambio();
    return {};
}

int Quorum::miembros() const {
    if (miembros_ > 0) return miembros_;
    return miembrosDelPadron_ ? std::max(0, miembrosDelPadron_()) : 0;
}

json Quorum::estado() const {
    const int presentes = presentes_ ? presentes_() : 0;
    const int m = miembros();
    int necesarios = 0;
    if (minimo_.is_number_integer()) necesarios = minimo_.get<int>();
    else if (m > 0) necesarios = m / 2 + 1;
    return {{"hay_quorum", necesarios > 0 && presentes >= necesarios},
            {"presentes", presentes},
            {"necesarios", necesarios},
            {"miembros", m},
            {"miembros_fijos", miembros_},
            {"minimo", minimo_},
            {"mayoria", mayoria_},
            {"configurado", necesarios > 0}};
}

int Quorum::necesarios(const std::string& m, int votantes, int presentes, int miembros) {
    // Simple: más afirmativos que negativos (las abstenciones no cuentan) -> se
    // expresa como «más de la mitad de los votos emitidos por sí o por no».
    if (m == "absoluta") return presentes / 2 + 1;
    if (m == "dos_tercios_presentes") return static_cast<int>(std::ceil(presentes * 2.0 / 3.0));
    if (m == "dos_tercios_miembros") return static_cast<int>(std::ceil(miembros * 2.0 / 3.0));
    return votantes / 2 + 1;
}

json Quorum::cargarOficial(const json& r) {
    json res{{"asunto", r.value("asunto", std::string())}, {"punto", r.value("punto", 0)}};
    int af = r.value("afirmativos", 0), ne = r.value("negativos", 0), ab = r.value("abstenciones", 0);
    json nominal = r.value("nominal", json::array());
    if (r.contains("csv")) {
        af = ne = ab = 0;
        nominal = json::array();
        std::istringstream is(r.value("csv", std::string()));
        std::string l;
        while (std::getline(is, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            const auto sep = l.find_first_of(";,\t");
            if (sep == std::string::npos) continue;
            const std::string nombre = l.substr(0, sep);
            const std::string voto = minus(l.substr(sep + 1));
            std::string v;
            if (voto.find("afirm") != std::string::npos || voto == "si" || voto == "sí" || voto == "a favor") v = "afirmativo";
            else if (voto.find("negat") != std::string::npos || voto == "no" || voto == "en contra") v = "negativo";
            else if (voto.find("absten") != std::string::npos) v = "abstencion";
            else if (voto.find("ausen") != std::string::npos) v = "ausente";
            else continue; // encabezado u otra cosa
            if (v == "afirmativo") ++af;
            if (v == "negativo") ++ne;
            if (v == "abstencion") ++ab;
            nominal.push_back({{"nombre", nombre}, {"voto", v}});
        }
        if (nominal.empty())
            return {{"error", ba::i18n::traducir("asamblea", "el CSV no tiene votos («nombre;afirmativo|negativo|abstención|ausente»)")}};
    }
    if (af < 0 || ne < 0 || ab < 0) return {{"error", ba::i18n::traducir("asamblea", "los votos no pueden ser negativos")}};
    const std::string may = r.value("mayoria", mayoria_);
    if (!kMayorias.count(may)) return {{"error", ba::i18n::traducir("asamblea", "mayoría desconocida")}};
    const int presentes = r.value("presentes", std::max(af + ne + ab, presentes_ ? presentes_() : 0));
    const int nec = necesarios(may, af + ne, presentes, miembros());
    res.update({{"afirmativos", af},
                {"negativos", ne},
                {"abstenciones", ab},
                {"presentes", presentes},
                {"mayoria", may},
                {"necesarios", nec},
                {"aprobado", af >= nec && (may != "simple" || af > ne)},
                {"nominal", nominal},
                {"ms", QDateTime::currentMSecsSinceEpoch()}});
    if (may == "dos_tercios_miembros" && miembros() <= 0)
        res["aviso"] = ba::i18n::traducir("asamblea", "falta la cantidad de miembros del cuerpo (quorum_regla) para los dos "
                                                      "tercios de los miembros");
    oficiales_.push_back(res);
    guardar();
    emit cambio();
    return res;
}

void Quorum::limpiarOficiales() {
    oficiales_ = json::array();
    guardar();
    emit cambio();
}

std::string Quorum::anexoActa() const {
    // El acta es del evento: va en su idioma (se arma cada vez que se pide).
    using ba::i18n::llenar;
    using ba::i18n::traducirEvento;
    std::string t;
    const json e = estado();
    if (e.value("configurado", false)) {
        const std::string pres = std::to_string(e.value("presentes", 0));
        t += "## " + traducirEvento("asamblea", "Quórum") + "\n\n- " +
             llenar(traducirEvento("asamblea", "Miembros: %1"), {std::to_string(e.value("miembros", 0))}) + "\n- " +
             llenar(traducirEvento("asamblea", "Necesarios para sesionar: %1"), {std::to_string(e.value("necesarios", 0))}) +
             "\n- " +
             (e.value("hay_quorum", false) ? llenar(traducirEvento("asamblea", "Presentes al cerrar el acta: %1 (hay quórum)"), {pres})
                                           : llenar(traducirEvento("asamblea", "Presentes al cerrar el acta: %1 (SIN quórum)"), {pres})) +
             "\n\n";
    }
    if (!oficiales_.empty()) {
        t += "## " + traducirEvento("asamblea", "Votaciones oficiales") + "\n\n";
        int n = 0;
        for (const auto& v : oficiales_) {
            ++n;
            const std::string asunto = v.value("asunto", std::string());
            t += "### " + std::to_string(n) + ". " + (asunto.empty() ? traducirEvento("asamblea", "(sin asunto)") : asunto) + "\n\n";
            t += llenar(traducirEvento("asamblea", "Afirmativos **%1**, negativos **%2**, abstenciones **%3** (presentes %4). "
                                                   "Requería %5 (%6 votos): **%7**."),
                        {std::to_string(v.value("afirmativos", 0)), std::to_string(v.value("negativos", 0)),
                         std::to_string(v.value("abstenciones", 0)), std::to_string(v.value("presentes", 0)),
                         nombreMayoria(v.value("mayoria", std::string())), std::to_string(v.value("necesarios", 0)),
                         v.value("aprobado", false) ? traducirEvento("asamblea", "APROBADO")
                                                    : traducirEvento("asamblea", "RECHAZADO")}) +
                 "\n\n";
            const json nom = v.value("nominal", json::array());
            if (!nom.empty()) {
                t += "| " + traducirEvento("asamblea", "Integrante") + " | " + traducirEvento("asamblea", "Voto") +
                     " |\n|---|---|\n";
                for (const auto& x : nom)
                    t += "| " + x.value("nombre", std::string()) + " | " + etiquetaDeVoto(x.value("voto", std::string())) + " |\n";
                t += "\n";
            }
        }
    }
    return t;
}

QVariantMap Quorum::estadoUi() const { return aQv(estado()).toMap(); }
QVariantList Quorum::oficialesUi() const { return aQv(oficiales_).toList(); }
QString Quorum::setReglaUi(const QVariantMap& r) {
    const json j = json::parse(QJsonDocument(QJsonObject::fromVariantMap(r)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    return QString::fromStdString(setRegla(j));
}
QString Quorum::cargarOficialUi(const QString& asunto, int af, int ne, int ab, const QString& mayoria) {
    json r{{"asunto", asunto.toStdString()}, {"afirmativos", af}, {"negativos", ne}, {"abstenciones", ab}};
    if (!mayoria.isEmpty()) r["mayoria"] = mayoria.toStdString();
    const json res = cargarOficial(r);
    return QString::fromStdString(res.value("error", std::string()));
}
QString Quorum::cargarCsvUi(const QString& asunto, const QString& csv, const QString& mayoria) {
    json r{{"asunto", asunto.toStdString()}, {"csv", csv.toStdString()}};
    if (!mayoria.isEmpty()) r["mayoria"] = mayoria.toStdString();
    const json res = cargarOficial(r);
    return QString::fromStdString(res.value("error", std::string()));
}

} // namespace ba::app::asamblea
