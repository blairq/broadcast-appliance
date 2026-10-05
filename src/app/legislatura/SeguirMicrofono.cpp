// SPDX-License-Identifier: GPL-2.0-or-later
#include "legislatura/SeguirMicrofono.hpp"

#include "control/BusEventos.hpp"
#include "legislatura/Bancas.hpp"
#include "legislatura/Cuerpo.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <fstream>

namespace ba::app::legislatura {

using json = nlohmann::json;
namespace fs = std::filesystem;

SeguirMicrofono::SeguirMicrofono(const fs::path& datos, control::BusEventos& bus, Cuerpo& cuerpo, Bancas& bancas,
                                 QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "seguir-microfono.json"), bus_(bus), cuerpo_(cuerpo), bancas_(bancas) {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_object()) {
        activo_ = j.value("activo", false);
        modo_ = j.value("modo", std::string("proponer"));
        fuente_ = j.value("fuente", std::string("niveles"));
        umbralDb_ = j.value("umbral_db", -30.0);
        sostenerMs_ = j.value("sostener_ms", 800);
        soltarMs_ = j.value("soltar_ms", 2500);
    }
    connect(&reloj_, &QTimer::timeout, this, &SeguirMicrofono::mirar);
    reloj_.start(100);
    // El sistema de conferencia del recinto (un plugin publica «conferencia/microfono» {banca, abierto}).
    sub_ = bus_.suscribir("plugin/+/conferencia/microfono", [this](const std::string&, const json& v) {
        if (!v.is_object()) return;
        const std::string banca = v.value("banca", std::string());
        if (!banca.empty()) conferencia_[banca] = {v.value("abierto", false), QDateTime::currentMSecsSinceEpoch()};
    });
}

SeguirMicrofono::~SeguirMicrofono() { bus_.desuscribir(sub_); }

void SeguirMicrofono::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    std::ofstream o(archivo_);
    o << json{{"activo", activo_}, {"modo", modo_}, {"fuente", fuente_}, {"umbral_db", umbralDb_}, {"sostener_ms", sostenerMs_},
              {"soltar_ms", soltarMs_}}
             .dump(1);
}

json SeguirMicrofono::estado() const {
    json abiertos = json::array();
    for (const auto& [f, m] : mics_)
        if (m.abierto) abiertos.push_back(f);
    return {{"activo", activo_},     {"modo", modo_},  {"fuente", fuente_}, {"umbral_db", umbralDb_}, {"sostener_ms", sostenerMs_},
            {"soltar_ms", soltarMs_}, {"propuesta", propuesta_}, {"abiertos", abiertos}};
}

std::string SeguirMicrofono::configurar(const json& c) {
    if (c.contains("modo")) {
        const std::string m = c.value("modo", std::string());
        if (m != "proponer" && m != "automatico") return "modo: proponer o automatico";
        modo_ = m;
    }
    if (c.contains("fuente")) {
        const std::string f = c.value("fuente", std::string());
        if (f != "niveles" && f != "conferencia") return "fuente: niveles o conferencia";
        fuente_ = f;
    }
    if (c.contains("umbral_db")) umbralDb_ = std::clamp(c.value("umbral_db", -30.0), -60.0, 0.0);
    if (c.contains("sostener_ms")) sostenerMs_ = std::clamp(c.value("sostener_ms", 800), 100, 10000);
    if (c.contains("soltar_ms")) soltarMs_ = std::clamp(c.value("soltar_ms", 2500), 200, 30000);
    if (c.contains("activo")) {
        activo_ = c.value("activo", false);
        if (!activo_) propuesta_ = nullptr;
    }
    guardar();
    emit cambio();
    return {};
}

std::string SeguirMicrofono::aceptar() {
    if (!propuesta_.is_object()) return "no hay ninguna propuesta";
    const QString e = bancas_.alAire(QString::fromStdString(propuesta_.value("banca", std::string())), "armar");
    if (!e.isEmpty()) return e.toStdString();
    descartada_ = propuesta_.value("fuente", std::string());
    propuesta_ = nullptr;
    bus_.publicar("oradores/propuesta", nullptr);
    emit cambio();
    return {};
}

void SeguirMicrofono::descartar() {
    if (!propuesta_.is_object()) return;
    descartada_ = propuesta_.value("fuente", std::string());
    propuesta_ = nullptr;
    bus_.publicar("oradores/propuesta", nullptr);
    emit cambio();
}

void SeguirMicrofono::mirar() {
    if (!activo_) return;
    if (fuente_ == "conferencia") {
        // El sistema dice qué micrófono está abierto: no hay que medir niveles.
        const std::string alAire = bancas_.oradorParaZocalo().value("banca", std::string());
        std::string mejor;
        std::int64_t desde = 0;
        bool cambio_ = false;
        for (const auto& [banca, e] : conferencia_) {
            Mic& m = mics_["conferencia:" + banca];
            if (m.abierto != e.first) cambio_ = true;
            m.abierto = e.first;
            if (!e.first) {
                if (descartada_ == "conferencia:" + banca) descartada_.clear();
                if (propuesta_.is_object() && propuesta_.value("banca", std::string()) == banca) {
                    propuesta_ = nullptr;
                    bus_.publicar("oradores/propuesta", nullptr);
                    cambio_ = true;
                }
                continue;
            }
            // El último que abrió (el que pidió la palabra más recientemente).
            if (banca != alAire && "conferencia:" + banca != descartada_ && e.second > desde) {
                mejor = banca;
                desde = e.second;
            }
        }
        if (!mejor.empty() && (!propuesta_.is_object() || propuesta_.value("banca", std::string()) != mejor)) {
            const json i = cuerpo_.buscar(mejor);
            propuesta_ = {{"banca", i.value("banca", mejor)},
                          {"nombre", i.value("nombre", std::string("Banca ") + mejor)},
                          {"bloque", i.value("bloque", std::string())},
                          {"fuente", "conferencia:" + mejor},
                          {"desde_ms", desde}};
            bus_.publicar("oradores/propuesta", propuesta_);
            cambio_ = true;
            if (modo_ == "automatico") aceptar();
        }
        if (cambio_) emit cambio();
        return;
    }
    // Micrófono -> integrante (el que tenga esa fuente como micrófono de su banca).
    std::map<std::string, json> porFuente;
    for (const auto& i : cuerpo_.integrantes()) {
        const std::string f = i.value("microfono", json::object()).value("fuente", std::string());
        if (!f.empty() && !i.value("banca", std::string()).empty()) porFuente[f] = i;
    }
    if (porFuente.empty()) return;
    const std::int64_t ahora = QDateTime::currentMSecsSinceEpoch();
    const std::string alAire = bancas_.oradorParaZocalo().value("banca", std::string());
    std::string mejor;
    double mejorDb = -1000;
    bool cambio_ = false;
    for (const auto& [fuente, integrante] : porFuente) {
        const json u = bus_.ultimo("audio/nivel/" + fuente);
        if (!u.is_object()) continue;
        const json v = u["valor"];
        const double db = v.value("mudo", false) ? -100.0 : v.value("pico_db", -100.0);
        Mic& m = mics_[fuente];
        if (db >= umbralDb_) {
            m.abajoDesde = 0;
            if (m.arribaDesde == 0) m.arribaDesde = ahora;
            if (!m.abierto && ahora - m.arribaDesde >= sostenerMs_) {
                m.abierto = true;
                cambio_ = true;
            }
        } else {
            m.arribaDesde = 0;
            if (m.abajoDesde == 0) m.abajoDesde = ahora;
            if (m.abierto && ahora - m.abajoDesde >= soltarMs_) {
                m.abierto = false;
                cambio_ = true;
                if (descartada_ == fuente) descartada_.clear();
                if (propuesta_.is_object() && propuesta_.value("fuente", std::string()) == fuente) {
                    propuesta_ = nullptr;
                    bus_.publicar("oradores/propuesta", nullptr);
                }
            }
        }
        // El más fuerte de los abiertos que no es el que ya está al aire.
        if (m.abierto && integrante.value("banca", std::string()) != alAire && fuente != descartada_ && db > mejorDb) {
            mejor = fuente;
            mejorDb = db;
        }
    }
    if (!mejor.empty() && (!propuesta_.is_object() || propuesta_.value("fuente", std::string()) != mejor)) {
        const json& i = porFuente[mejor];
        propuesta_ = {{"banca", i.value("banca", std::string())},
                      {"nombre", i.value("nombre", std::string())},
                      {"bloque", i.value("bloque", std::string())},
                      {"fuente", mejor},
                      {"desde_ms", ahora}};
        bus_.publicar("oradores/propuesta", propuesta_);
        cambio_ = true;
        if (modo_ == "automatico") aceptar();
    }
    if (cambio_) emit cambio();
}

QVariantMap SeguirMicrofono::estadoUi() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(estado().dump())).object().toVariantMap();
}

QString SeguirMicrofono::configurarUi(const QVariantMap& c) {
    const json j = json::parse(QJsonDocument(QJsonObject::fromVariantMap(c)).toJson(QJsonDocument::Compact).toStdString(),
                               nullptr, false);
    return QString::fromStdString(configurar(j));
}

} // namespace ba::app::legislatura
