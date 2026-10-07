// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/Bancas.hpp"

#include "asamblea/Cuerpo.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace ba::app::asamblea {

using json = nlohmann::json;

Bancas::Bancas(Cuerpo& cuerpo, Ganchos g, QObject* parent) : QObject(parent), cuerpo_(cuerpo), g_(std::move(g)) {}

json Bancas::estadoJson() const {
    json cola = json::array();
    for (const auto& id : cola_) {
        const json i = cuerpo_.buscar(id);
        if (!i.empty()) cola.push_back(i);
    }
    const json alAire = alAire_.empty() ? json() : cuerpo_.buscar(alAire_);
    const long long ahora = QDateTime::currentMSecsSinceEpoch();
    json bancas = json::array();
    for (const auto& i : cuerpo_.integrantes())
        if (!i.value("banca", std::string()).empty()) bancas.push_back(i);
    return {{"cola", cola},
            {"al_aire", alAire},
            {"desde_ms", desdeMs_},
            {"usado_s", alAire_.empty() ? 0 : (ahora - desdeMs_) / 1000},
            {"asignado_s", alAire_.empty() ? asignadoS_ : asignadoTurnoS_},
            {"asignado_fabrica_s", asignadoS_},
            {"bancas", bancas},
            {"aviso", aviso_}};
}

QVariantMap Bancas::estado() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(estadoJson().dump())).object().toVariantMap();
}

QString Bancas::agregar(const QString& clave) {
    const json i = cuerpo_.buscar(clave.toStdString());
    if (i.empty()) return "no hay nadie con esa banca, id o nombre en el cuerpo";
    const std::string id = i.value("id", std::string());
    if (id == alAire_ || std::find(cola_.begin(), cola_.end(), id) != cola_.end()) return {};
    cola_.push_back(id);
    avisarFila();
    emit cambio();
    return {};
}

void Bancas::avisarFila() {
    if (g_.filaCambio) g_.filaCambio(cola_, alAire_);
}

void Bancas::setAsignado(int segundos) {
    asignadoS_ = std::clamp(segundos, 10, 4 * 3600);
    emit cambio();
}

void Bancas::sumarTiempo(int segundos) {
    if (alAire_.empty()) return;
    asignadoTurnoS_ = std::max(10, asignadoTurnoS_ + segundos);
    if (g_.esRemoto && g_.palabraRemota && g_.esRemoto(alAire_)) {
        const long long usado = (QDateTime::currentMSecsSinceEpoch() - desdeMs_) / 1000;
        g_.palabraRemota(alAire_, true, static_cast<int>(std::max(0LL, asignadoTurnoS_ - usado)));
    }
    emit cambio();
}

void Bancas::quitar(const QString& clave) {
    const json i = cuerpo_.buscar(clave.toStdString());
    const std::string id = i.value("id", clave.toStdString());
    cola_.erase(std::remove(cola_.begin(), cola_.end(), id), cola_.end());
    avisarFila();
    emit cambio();
}

QString Bancas::alAire(const QString& clave, const QString& modo, int segundos) {
    const json i = cuerpo_.buscar(clave.toStdString());
    if (i.empty()) return "no hay nadie con esa banca, id o nombre en el cuerpo";
    aviso_.clear();
    const std::string id = i.value("id", std::string());
    // El micrófono de la anterior se cierra (en la central y en la mezcla).
    if (!alAire_.empty() && alAire_ != id) {
        if (g_.esRemoto && g_.palabraRemota && g_.esRemoto(alAire_)) g_.palabraRemota(alAire_, false, 0);
        const json ant = cuerpo_.buscar(alAire_);
        if (g_.microfonoBanca) g_.microfonoBanca(ant.value("banca", std::string()), false, true);
        const std::string mic = ant.value("microfono", json::object()).value("fuente", std::string());
        if (!mic.empty() && g_.microfono) g_.microfono(mic, false);
    }
    const bool remoto = g_.esRemoto && g_.esRemoto(id);
    // 1. PTZ al preset de su banca, salvo que esa cámara esté al aire. Al que habla
    // desde el navegador no se le mueve ninguna cámara.
    const json ptz = remoto ? json::object() : i.value("preset_ptz", json::object());
    const std::string camara = ptz.value("camara", std::string()), posicion = ptz.value("posicion", std::string());
    if (!camara.empty() && !posicion.empty() && g_.irAPosicion) {
        if (g_.camaraAlAire && g_.camaraAlAire(camara))
            aviso_ = "la cámara «" + camara + "» está al aire: no se movió (armá otra o mové después del corte)";
        else if (const std::string e = g_.irAPosicion(camara, posicion); !e.empty())
            aviso_ = "PTZ: " + e;
    } else if (camara.empty() && !remoto) {
        aviso_ = "la banca no tiene preset de cámara (Cuerpo)";
    }
    // 2. La escena de esa cámara, armada o al aire.
    if (!camara.empty() && g_.escenaDe) {
        const std::string escena = g_.escenaDe(camara);
        if (escena.empty())
            aviso_ += (aviso_.empty() ? "" : "; ") + std::string("ninguna escena muestra «") + camara + "»";
        else if (modo == "directo" && g_.cortar)
            g_.cortar(escena);
        else if (g_.armar)
            g_.armar(escena);
    }
    // 4. El reloj y 5. el micrófono.
    alAire_ = id;
    desdeMs_ = QDateTime::currentMSecsSinceEpoch();
    asignadoTurnoS_ = segundos > 0 ? std::clamp(segundos, 10, 4 * 3600) : asignadoS_;
    cola_.erase(std::remove(cola_.begin(), cola_.end(), id), cola_.end());
    if (remoto) {
        g_.palabraRemota(id, true, asignadoTurnoS_);
    } else {
        const std::string mic = i.value("microfono", json::object()).value("fuente", std::string());
        if (!mic.empty() && g_.microfono) g_.microfono(mic, true);
        if (g_.microfonoBanca) g_.microfonoBanca(i.value("banca", std::string()), true, false);
    }
    avisarFila();
    emit cambio();
    return {};
}

QString Bancas::apuntar(const QString& clave) {
    const json i = cuerpo_.buscar(clave.toStdString());
    if (i.empty()) return "no hay nadie con esa banca, id o nombre en el cuerpo";
    const json ptz = i.value("preset_ptz", json::object());
    const std::string camara = ptz.value("camara", std::string()), posicion = ptz.value("posicion", std::string());
    if (camara.empty() || posicion.empty()) return "la banca no tiene preset de cámara (Cuerpo)";
    if (g_.camaraAlAire && g_.camaraAlAire(camara))
        return QString::fromStdString("la cámara «" + camara + "» está al aire: no se mueve");
    if (!g_.irAPosicion) return "el PTZ no está disponible";
    return QString::fromStdString(g_.irAPosicion(camara, posicion));
}

void Bancas::terminar() {
    if (alAire_.empty()) return;
    const json i = cuerpo_.buscar(alAire_);
    if (g_.esRemoto && g_.palabraRemota && g_.esRemoto(alAire_)) g_.palabraRemota(alAire_, false, 0);
    const std::string mic = i.value("microfono", json::object()).value("fuente", std::string());
    if (!mic.empty() && g_.microfono) g_.microfono(mic, false);
    if (g_.microfonoBanca) g_.microfonoBanca(i.value("banca", std::string()), false, true);
    alAire_.clear();
    desdeMs_ = 0;
    avisarFila();
    emit cambio();
}

QString Bancas::siguiente(const QString& modo) {
    if (cola_.empty()) return "la fila de bancas está vacía";
    return alAire(QString::fromStdString(cola_.front()), modo);
}

json Bancas::oradorParaZocalo() const {
    if (alAire_.empty()) return json::object();
    const json i = cuerpo_.buscar(alAire_);
    if (i.empty()) return json::object();
    return {{"nombre", i.value("nombre", std::string())},
            {"cargo", i.value("cargo_oficial", std::string())},
            {"bloque", i.value("bloque", std::string())},
            {"banca", i.value("banca", std::string())},
            {"validado", i.contains("validado_por")}};
}

} // namespace ba::app::asamblea
