// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/asamblea/Debate.cpp — ver Debate.hpp

#include "asamblea/Debate.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18n.hpp"

#include "asamblea/Cuerpo.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <fstream>

namespace ba::app::asamblea {

using json = nlohmann::json;

namespace {
long long ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

std::string audioDeCanal(int canal) {
    return canal == 2 ? "Remoto con la palabra 2 (audio)" : "Remoto con la palabra (audio)";
}

QVariantMap aQvm(const nlohmann::json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object().toVariantMap();
}
} // namespace

Debate::Debate(Cuerpo& cuerpo, std::filesystem::path datos, Ganchos g, QObject* parent)
    : QObject(parent), cuerpo_(cuerpo), archivo_(datos / "Config" / "debate.json"), g_(std::move(g)) {
    config_ = {{"escenas", {{"sala_sala", ""}, {"sala_remoto", ""}, {"remoto_remoto", ""}}},
               {"turno_s", 120},
               {"directo", false}};
    cargar();
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, [this]() { tic(); });
}

void Debate::cargar() {
    const json j = config::leerObjeto(archivo_);
    if (j.empty()) return;
    configurar(j);
}

void Debate::guardar() const {
    config::guardar(archivo_, config_);
}

std::string Debate::configurar(const json& c) {
    if (!c.is_object()) return ba::i18n::traducir("asamblea", "la configuración va como objeto");
    if (c.contains("escenas") && c["escenas"].is_object())
        for (const char* k : {"sala_sala", "sala_remoto", "remoto_remoto"})
            if (c["escenas"].contains(k) && c["escenas"][k].is_string()) config_["escenas"][k] = c["escenas"][k];
    if (c.contains("turno_s") && c["turno_s"].is_number_integer())
        config_["turno_s"] = std::clamp(c["turno_s"].get<int>(), 0, 3600);
    if (c.contains("directo") && c["directo"].is_boolean()) config_["directo"] = c["directo"];
    guardar();
    emit cambio();
    return {};
}

QString Debate::configurarUi(const QVariantMap& cambios) {
    return QString::fromStdString(
        configurar(json::parse(QJsonDocument::fromVariant(cambios).toJson(QJsonDocument::Compact).toStdString(), nullptr, false)));
}

json Debate::lado(const std::string& id) const {
    if (id.empty()) return nullptr;
    const json i = cuerpo_.buscar(id);
    const bool remoto = id == a_ ? aRemoto_ : bRemoto_;
    json l{{"id", id}, {"nombre", i.value("nombre", id)}, {"banca", i.value("banca", std::string())}, {"remoto", remoto}};
    if (remoto && g_.canalRemoto) l["canal"] = g_.canalRemoto(id);
    return l;
}

std::string Debate::tipo() const {
    if (aRemoto_ && bRemoto_) return "remoto_remoto";
    if (aRemoto_ || bRemoto_) return "sala_remoto";
    return "sala_sala";
}

json Debate::estadoJson() const {
    json e{{"activo", activo_}, {"config", config_}};
    if (!activo_) return e;
    const long long pasados = turnoDesdeMs_ > 0 ? (ahoraMs() - turnoDesdeMs_) / 1000 : 0;
    e["tipo"] = tipo();
    e["a"] = lado(a_);
    e["b"] = lado(b_);
    e["turno"] = turno_;
    e["turno_s"] = turnoS_;
    e["restante_s"] = turnoS_ > 0 && (turno_ == "a" || turno_ == "b") ? turnoS_ - pasados : 0;
    e["escena"] = escena_;
    e["aviso"] = aviso_;
    return e;
}

QVariantMap Debate::estado() const { return aQvm(estadoJson()); }

std::string Debate::iniciar(const std::string& a, const std::string& b, const std::string& t) {
    const json ia = cuerpo_.buscar(a), ib = cuerpo_.buscar(b);
    if (ia.empty()) return ba::i18n::llenar(ba::i18n::traducir("asamblea", "no hay nadie con la banca, id o nombre «%1» en el Cuerpo"), {a});
    if (ib.empty()) return ba::i18n::llenar(ba::i18n::traducir("asamblea", "no hay nadie con la banca, id o nombre «%1» en el Cuerpo"), {b});
    const std::string idA = ia.value("id", std::string()), idB = ib.value("id", std::string());
    if (idA == idB) return ba::i18n::traducir("asamblea", "un debate es entre dos personas distintas");
    if (activo_) terminar();
    aviso_.clear();
    if (g_.terminarOrador) g_.terminarOrador();
    a_ = idA;
    b_ = idB;
    aRemoto_ = g_.esRemoto && g_.esRemoto(a_);
    bRemoto_ = g_.esRemoto && g_.esRemoto(b_);
    activo_ = true;
    // La central, si habla alguno de la sala.
    if ((!aRemoto_ || !bRemoto_) && g_.central) g_.central(true);
    // Los remotos: la palabra todo el debate (A primero: canal 1), el micrófono según el turno.
    const int seg = config_.value("turno_s", 120);
    for (const auto& [id, remoto] : {std::pair{a_, aRemoto_}, std::pair{b_, bRemoto_}})
        if (remoto && g_.palabraRemota) g_.palabraRemota(id, true, 0);
    // Las cámaras de los de la sala.
    for (const auto& [id, remoto] : {std::pair{a_, aRemoto_}, std::pair{b_, bRemoto_}})
        if (!remoto && g_.apuntar)
            if (const std::string e = g_.apuntar(id); !e.empty()) aviso_ += (aviso_.empty() ? "" : "; ") + e;
    // La escena de este tipo de debate.
    escena_ = config_["escenas"].value(tipo(), std::string());
    if (escena_.empty()) {
        aviso_ += (aviso_.empty() ? "" : "; ") +
                  (tipo() == "sala_sala"     ? ba::i18n::traducir("asamblea", "no hay escena configurada para un debate entre dos de la sala")
                   : tipo() == "sala_remoto" ? ba::i18n::traducir("asamblea", "no hay escena configurada para un debate entre uno de "
                                                                              "la sala y un remoto")
                                             : ba::i18n::traducir("asamblea", "no hay escena configurada para un debate entre dos remotos"));
    } else if (config_.value("directo", false) && g_.cortar) {
        g_.cortar(escena_);
    } else if (g_.armar) {
        g_.armar(escena_);
    }
    turnoS_ = seg;
    turno_.clear();
    turno(t.empty() ? "a" : t);
    reloj_.start();
    if (!aviso_.empty()) emit aviso(QString::fromStdString("Debate: " + aviso_), "warning");
    return {};
}

std::string Debate::turno(const std::string& t) {
    if (!activo_) return ba::i18n::traducir("asamblea", "no hay un debate en curso");
    if (t != "a" && t != "b" && t != "ambos" && t != "nadie") return ba::i18n::traducir("asamblea", "el turno es a, b, ambos o nadie");
    turno_ = t;
    turnoDesdeMs_ = ahoraMs();
    turnoS_ = config_.value("turno_s", 120);
    avisoMinuto_ = avisoFin_ = false;
    aplicarTurno();
    emit cambio();
    return {};
}

void Debate::aplicarTurno() {
    for (const auto& [id, remoto, lado] : {std::tuple{a_, aRemoto_, std::string("a")}, std::tuple{b_, bRemoto_, std::string("b")}}) {
        const bool habla = turno_ == "ambos" || turno_ == lado;
        if (remoto) {
            // Su imagen sigue entrando; lo que se abre y se cierra es su audio.
            if (g_.canalRemoto && g_.microfono)
                if (const int canal = g_.canalRemoto(id); canal > 0) g_.microfono(audioDeCanal(canal), habla);
            if (g_.avisarRemoto) {
                const long long pasados = (ahoraMs() - turnoDesdeMs_) / 1000;
                g_.avisarRemoto(id, json{{"type", "debate"},
                                         {"activo", true},
                                         {"habla", habla},
                                         {"segundos", habla && turno_ != "ambos" && turnoS_ > 0 ? turnoS_ - pasados : 0}});
            }
        } else if (g_.microfonoBanca) {
            g_.microfonoBanca(cuerpo_.buscar(id).value("banca", std::string()), habla, false);
        }
    }
}

void Debate::sumar(int segundos) {
    if (!activo_ || (turno_ != "a" && turno_ != "b")) return;
    turnoS_ = std::max(10, turnoS_ + segundos);
    avisoMinuto_ = avisoFin_ = false;
    aplicarTurno(); // el celular del que habla toma el reloj nuevo
    emit cambio();
}

void Debate::tic() {
    if (!activo_) {
        reloj_.stop();
        return;
    }
    if ((turno_ == "a" || turno_ == "b") && turnoS_ > 0) {
        const long long queda = turnoS_ - (ahoraMs() - turnoDesdeMs_) / 1000;
        const std::string id = turno_ == "a" ? a_ : b_;
        const bool remoto = turno_ == "a" ? aRemoto_ : bRemoto_;
        const auto avisar = [&](const std::string& que) {
            if (remoto && g_.avisoRemoto) g_.avisoRemoto(id, que);
            if (!remoto && g_.avisoCentral) g_.avisoCentral(cuerpo_.buscar(id).value("banca", std::string()), que);
            emit aviso((que == "terminado" ? tr("%1: se terminó su tiempo de debate") : tr("%1: queda un minuto"))
                           .arg(QString::fromStdString(cuerpo_.buscar(id).value("nombre", id))),
                       que == "terminado" ? "warning" : "info");
        };
        if (queda <= 60 && !avisoMinuto_ && turnoS_ > 60) {
            avisoMinuto_ = true;
            avisar("ultimo_minuto");
        }
        if (queda <= 0 && !avisoFin_) {
            avisoFin_ = true;
            avisar("terminado");
        }
    }
    emit cambio();
}

void Debate::terminar() {
    if (!activo_) return;
    reloj_.stop();
    for (const auto& [id, remoto] : {std::pair{a_, aRemoto_}, std::pair{b_, bRemoto_}}) {
        if (remoto) {
            // El audio de su canal vuelve a quedar abierto para el uso normal (la fila).
            if (g_.canalRemoto && g_.microfono)
                if (const int canal = g_.canalRemoto(id); canal > 0) g_.microfono(audioDeCanal(canal), true);
            if (g_.avisarRemoto) g_.avisarRemoto(id, json{{"type", "debate"}, {"activo", false}});
            if (g_.palabraRemota) g_.palabraRemota(id, false, 0);
        } else if (g_.microfonoBanca) {
            g_.microfonoBanca(cuerpo_.buscar(id).value("banca", std::string()), false, true);
        }
    }
    if ((!aRemoto_ || !bRemoto_) && g_.central) g_.central(false);
    activo_ = false;
    a_.clear();
    b_.clear();
    aRemoto_ = bRemoto_ = false;
    turno_ = "nadie";
    escena_.clear();
    aviso_.clear();
    emit cambio();
}

QString Debate::iniciarUi(const QString& a, const QString& b, const QString& t) {
    return QString::fromStdString(iniciar(a.toStdString(), b.toStdString(), t.toStdString()));
}

} // namespace ba::app::asamblea
