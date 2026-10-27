// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/Bancas.hpp"
#include "i18n/I18n.hpp"
#include <cctype>
#include <sstream>

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
    return QJsonDocument::fromJson(QByteArray::fromStdString(estadoJson().dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}

QString Bancas::agregar(const QString& clave) {
    const json i = cuerpo_.buscar(clave.toStdString());
    if (i.empty()) return tr("no hay nadie con esa banca, id o nombre en el cuerpo");
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
    if (i.empty()) return tr("no hay nadie con esa banca, id o nombre en el cuerpo");
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
    const bool laEncuadra = !camara.empty() && g_.laEncuadraLaFuente && g_.laEncuadraLaFuente(camara, posicion);
    if (laEncuadra) {
        // La fuente «Banca con la palabra» la recorta de la toma entera: no se mueve nada
        // (el plano general de esa misma cámara sigue igual).
    } else if (!camara.empty() && !posicion.empty() && g_.irAPosicion) {
        if (g_.camaraAlAire && g_.camaraAlAire(camara))
            aviso_ = ba::i18n::llenar(ba::i18n::traducir("asamblea", "la cámara «%1» está al aire: no se movió (armá otra o mové "
                                                                     "después del corte)"),
                                      {camara});
        else if (const std::string e = g_.irAPosicion(camara, posicion); !e.empty())
            aviso_ = "PTZ: " + e;
    } else if (camara.empty() && !remoto) {
        aviso_ = ba::i18n::traducir("asamblea", "la banca no tiene preset de cámara (Cuerpo)");
    }
    // 2. La escena del orador, armada o al aire: la que tiene la fuente «Banca con la
    // palabra» (ranura 1); si no hay, la primera que muestra esa cámara (lo de antes).
    if (!camara.empty() && g_.escenaDe) {
        std::string escena = g_.escenaDeRanura ? g_.escenaDeRanura(1) : std::string();
        if (escena.empty()) escena = g_.escenaDe(camara);
        if (escena.empty())
            aviso_ += (aviso_.empty() ? "" : "; ") + ba::i18n::llenar(ba::i18n::traducir("asamblea", "ninguna escena muestra «%1»"), {camara});
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
    if (i.empty()) return tr("no hay nadie con esa banca, id o nombre en el cuerpo");
    const json ptz = i.value("preset_ptz", json::object());
    const std::string camara = ptz.value("camara", std::string()), posicion = ptz.value("posicion", std::string());
    if (camara.empty() || posicion.empty()) return tr("la banca no tiene preset de cámara (Cuerpo)");
    if (g_.camaraAlAire && g_.camaraAlAire(camara))
        return tr("la cámara «%1» está al aire: no se mueve").arg(QString::fromStdString(camara));
    if (!g_.irAPosicion) return tr("el PTZ no está disponible");
    return QString::fromStdString(g_.irAPosicion(camara, posicion));
}

QString Bancas::prepararToma(const QString& clave) {
    const json i = cuerpo_.buscar(clave.toStdString());
    if (i.empty()) return tr("no hay nadie con esa banca, id o nombre en el cuerpo");
    const json ptz = i.value("preset_ptz", json::object());
    const std::string camara = ptz.value("camara", std::string()), posicion = ptz.value("posicion", std::string());
    if (!camara.empty() && g_.laEncuadraLaFuente && g_.laEncuadraLaFuente(camara, posicion)) return {};
    return apuntar(clave);
}

json Bancas::resolver(const std::string& que) const {
    std::string t = Cuerpo::normalizar(que);
    // Lo que se dice antes: «enfocar a la banca 10», «al consejero López».
    for (const char* p : {"enfocar ", "enfoca ", "a la ", "al ", "a ", "la ", "el ", "los ", "consejero ", "consejera ",
                          "concejal ", "integrante ", "señor ", "senor ", "señora ", "senora "})
        if (t.rfind(p, 0) == 0) t = t.substr(std::string(p).size());
    if (t.empty()) return {{"error", ba::i18n::traducir("asamblea", "¿a quién o a qué banca?")}};
    const auto conToma = [&](json r, const std::string& banca) {
        const json tm = cuerpo_.toma(banca);
        if (tm.empty())
            return json{{"error", ba::i18n::llenar(ba::i18n::traducir("asamblea", "la banca %1 no tiene toma de cámara "
                                                                                  "(configurala en las bancas)"),
                                                   {banca})}};
        r["camara"] = tm.value("camara", std::string());
        r["posicion"] = tm.value("posicion", std::string());
        return r;
    };
    // 1. Una banca: «banca 10», «10».
    {
        std::string b = t;
        if (b.rfind("banca ", 0) == 0) b = b.substr(6);
        const bool dijoBanca = b.size() != t.size() || std::all_of(b.begin(), b.end(), [](unsigned char c) { return std::isdigit(c); });
        if (dijoBanca && !b.empty()) {
            const json i = cuerpo_.buscar(b);
            const bool deEsaBanca = !i.empty() && Cuerpo::normalizar(i.value("banca", std::string())) == b;
            if (deEsaBanca || !cuerpo_.toma(b).empty()) {
                json r{{"tipo", "banca"}, {"banca", deEsaBanca ? i.value("banca", std::string()) : b}};
                if (deEsaBanca) r["integrante"] = {{"id", i.value("id", std::string())}, {"nombre", i.value("nombre", std::string())}};
                return conToma(r, r["banca"].get<std::string>());
            }
            if (b.size() != t.size()) return {{"error", ba::i18n::llenar(ba::i18n::traducir("asamblea", "no hay una banca «%1»"), {b})}};
        }
    }
    // 2. Una persona: el nombre entero, o todas las palabras dichas en su nombre («López»).
    {
        json i = cuerpo_.buscar(t);
        if (i.empty()) {
            std::vector<std::string> palabras;
            std::istringstream ss(t);
            for (std::string w; ss >> w;) palabras.push_back(w);
            json candidatos = json::array();
            for (const auto& x : cuerpo_.integrantes()) {
                const std::string n = " " + Cuerpo::normalizar(x.value("nombre", std::string())) + " ";
                bool todas = !palabras.empty();
                for (const auto& w : palabras) todas = todas && n.find(" " + w) != std::string::npos;
                if (todas) candidatos.push_back(x);
            }
            if (candidatos.size() == 1) i = candidatos[0];
            else if (candidatos.size() > 1) {
                json nombres = json::array();
                for (const auto& c : candidatos)
                    nombres.push_back(ba::i18n::llenar(ba::i18n::traducir("asamblea", "%1 (banca %2)"),
                                                       {c.value("nombre", std::string()), c.value("banca", std::string())}));
                return {{"error", ba::i18n::traducir("asamblea", "hay más de uno: decí cuál")}, {"candidatos", nombres}};
            }
        }
        if (!i.empty()) {
            const std::string banca = i.value("banca", std::string());
            if (banca.empty())
                return {{"error", ba::i18n::llenar(ba::i18n::traducir("asamblea", "%1 no tiene banca"), {i.value("nombre", std::string())})}};
            return conToma({{"tipo", "persona"}, {"banca", banca},
                            {"integrante", {{"id", i.value("id", std::string())}, {"nombre", i.value("nombre", std::string())}}}},
                           banca);
        }
    }
    // 3. El nombre de una posición de cualquier cámara («Presidencia», «Plano general»).
    if (g_.posicionesConNombre) {
        const auto v = g_.posicionesConNombre(t);
        if (v.size() == 1) return {{"tipo", "posicion"}, {"camara", v[0].first}, {"posicion", v[0].second}};
        if (v.size() > 1) {
            json c = json::array();
            for (const auto& [cam, pos] : v) c.push_back(pos + " (" + cam + ")");
            return {{"error", ba::i18n::traducir("asamblea", "esa posición está en varias cámaras: decí cuál")}, {"candidatos", c}};
        }
    }
    return {{"error", ba::i18n::llenar(ba::i18n::traducir("asamblea", "no encontré una banca, una persona ni una posición «%1»"), {que})}};
}

QString Bancas::enfocar(const QString& que, bool forzar) {
    const json r = resolver(que.toStdString());
    if (r.contains("error")) {
        std::string e = r.value("error", std::string());
        if (r.contains("candidatos")) {
            e += ": ";
            for (size_t k = 0; k < r["candidatos"].size(); ++k) e += (k ? ", " : "") + r["candidatos"][k].get<std::string>();
        }
        return QString::fromStdString(e);
    }
    const std::string camara = r.value("camara", std::string()), posicion = r.value("posicion", std::string());
    if (!forzar && g_.camaraAlAire && g_.camaraAlAire(camara))
        return tr("la cámara «%1» está al aire: moverla se ve (pedilo con forzar)").arg(QString::fromStdString(camara));
    if (!g_.irAPosicion) return tr("el PTZ no está disponible");
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
    if (cola_.empty()) return tr("la fila de bancas está vacía");
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
