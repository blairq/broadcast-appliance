// src/app/escenas/RespaldoEntradas.cpp — ver el .hpp

#include "escenas/RespaldoEntradas.hpp"
#include "arranque/ArchivoConfig.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/TurnoMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "video/VideoGrafos.hpp"

#include <QDateTime>

#include <algorithm>
#include <fstream>

namespace ba::app::escenas {

RespaldoEntradas::RespaldoEntradas(AppRuntime& rt, video::VideoGrafos& video, Conmutador& conmutador, QObject* parent)
    : QObject(parent), rt_(rt), video_(video), conmutador_(conmutador) {
    // Cada segundo, con el turno del motor si está libre (si la interfaz lo tiene, esta
    // vuelta se saltea: la próxima la alcanza).
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, [this]() {
        const TurnoMotor& t = rt_.engine().turno();
        if (!t.tomarHasta(0)) return;
        revisar();
        t.soltar();
    });
    reloj_.start();
}

nlohmann::json RespaldoEntradas::leer() const {
    // Por entrada, bajo «entradas» (el nombre de una entrada no choca con `esquema`).
    const nlohmann::json j = config::leerObjeto(rt_.dataRoot() / "Config" / "respaldo-entradas.json")
                                 .value("entradas", nlohmann::json::object());
    return j.is_object() ? j : nlohmann::json::object();
}

nlohmann::json RespaldoEntradas::respaldo(const std::string& entrada) const {
    return leer().value(entrada, nlohmann::json::object());
}

std::string RespaldoEntradas::setRespaldo(const std::string& entrada, const std::string& escena,
                                          const std::string& alPerderAire, int ms) {
    if (alPerderAire != "avisar" && alPerderAire != "armar" && alPerderAire != "cortar")
        return tr("al_perder_aire: avisar, armar o cortar").toStdString();
    nlohmann::json todos = leer();
    todos[entrada] = {{"escena", escena}, {"al_perder_aire", alPerderAire}, {"ms", std::clamp(ms, 200, 60000)}};
    config::guardar(rt_.dataRoot() / "Config" / "respaldo-entradas.json", {{"entradas", todos}});
    return {};
}

void RespaldoEntradas::revisar() {
    QStringList sin;
    for (const QString& e : video_.entradas()) {
        const nlohmann::json st = video_.estadoEntrada(e.toStdString());
        if (!st.contains("gstreamer") || !st.value("activa", true)) continue;
        if (!video::VideoGrafos::tieneSenal(st)) sin << e;
    }
    const qint64 ahora = QDateTime::currentMSecsSinceEpoch();
    for (const QString& e : video_.entradas())
        if (!sin.contains(e)) tuvoSenal_.insert(e);
    for (const QString& e : sin)
        if (!sinSenalDesde_.contains(e)) sinSenalDesde_[e] = ahora;
    for (auto it = sinSenalDesde_.begin(); it != sinSenalDesde_.end();)
        if (!sin.contains(it.key())) {
            hecho_.remove(it.key());
            it = sinSenalDesde_.erase(it);
        } else {
            ++it;
        }
    if (sin != sinSenal_) {
        sinSenal_ = sin;
        emit sinSenalCambio(sinSenal_);
    }
    // Una entrada AL AIRE sin señal: lo que se configuró (avisar, armar o cortar al respaldo).
    // Al aire = se ve en el programa (escena o capa DSK, el recuadro de LSA también) Y el
    // programa sale; si no se transmite ni graba, nada (si después sale, avisa entonces).
    if (!rt_.engine().haySalida()) return;
    const nlohmann::json resp = leer();
    const auto alAire = rt_.engine().fuentesEnPrograma();
    QStringList perdidas; // las de solo avisar, juntas en un aviso
    for (const QString& e : sin) {
        if (hecho_.contains(e)) continue;
        if (std::find(alAire.begin(), alAire.end(), e.toStdString()) == alAire.end()) continue;
        const nlohmann::json r = resp.value(e.toStdString(), nlohmann::json::object());
        if (ahora - sinSenalDesde_.value(e) < r.value("ms", 1000)) continue;
        hecho_.insert(e);
        const std::string escena = r.value("escena", std::string());
        const std::string modo = r.value("al_perder_aire", std::string("avisar"));
        const QString qe = QString::fromStdString(escena);
        if (modo == "cortar" && !escena.empty()) {
            const std::string err = conmutador_.poner(escena);
            emit aviso(err.empty() ? tr("«%1» se quedó sin señal al aire: se cortó a «%2»").arg(e, qe)
                                   : tr("«%1» se quedó sin señal al aire y no se pudo cortar a «%2»: %3")
                                         .arg(e, qe, QString::fromStdString(err)));
        } else if (modo == "armar" && !escena.empty()) {
            const std::string err = conmutador_.armar(escena);
            emit aviso(err.empty() ? tr("«%1» se quedó sin señal al aire: «%2» quedó armada (TAKE)").arg(e, qe)
                                   : tr("«%1» se quedó sin señal al aire: %2").arg(e, QString::fromStdString(err)));
        } else if (tuvoSenal_.contains(e)) {
            // Solo la que tenía señal y la perdió: una entrada de invitado o de pantalla
            // compartida vacía (nadie conectado, nadie compartiendo) es lo normal, no una
            // alarma; la lista de las que no tienen señal está en el tablero (sinSenal).
            perdidas << "«" + e + "»";
        }
    }
    if (perdidas.size() == 1)
        emit aviso(tr("%1 se quedó sin señal AL AIRE").arg(perdidas.first()));
    else if (perdidas.size() > 1)
        emit aviso(tr("%1 y %2 se quedaron sin señal AL AIRE")
                       .arg(perdidas.mid(0, perdidas.size() - 1).join(", "), perdidas.last()));
}

} // namespace ba::app::escenas
