// src/app/escenas/RespaldoEntradas.cpp — ver el .hpp

#include "escenas/RespaldoEntradas.hpp"
#include "arranque/ArchivoConfig.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/TurnoMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "escenas/RefEscena.hpp"
#include "video/VideoGrafos.hpp"

#include <QDateTime>

#include <algorithm>
#include <iterator>

namespace ba::app::escenas {

RespaldoEntradas::RespaldoEntradas(AppRuntime& rt, video::VideoGrafos& video, Conmutadores& conmutadores, QObject* parent)
    : QObject(parent), rt_(rt), video_(video), conmutadores_(conmutadores) {
    // Lo configurado, una vez (por entrada, bajo «entradas»: el nombre de una entrada no
    // choca con `esquema`); después lo cambia setRespaldo.
    const nlohmann::json j = config::leerObjeto(archivo()).value("entradas", nlohmann::json::object());
    if (j.is_object()) respaldos_ = j;
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
    // La escena de cada respaldo va por su id: un renombre pone al día el nombre (el aviso
    // llega en el dueño, con el turno).
    observador_ = rt_.observar([this](AppRuntime::Cambio c) {
        if (c == AppRuntime::Cambio::Escenas) seguirEscenas();
    });
}

RespaldoEntradas::~RespaldoEntradas() {
    if (observador_ >= 0) rt_.dejarDeObservar(observador_);
}

void RespaldoEntradas::seguirEscenas() {
    const TablaEscenas t = rt_.engine().tablaEscenas();
    if (t.vacia()) return;
    nlohmann::json todos;
    {
        const std::lock_guard<std::mutex> l(mu_);
        bool hubo = false;
        for (auto& [entrada, r] : respaldos_.items()) hubo = seguirRef(r, t) || hubo;
        if (!hubo) return;
        todos = respaldos_;
    }
    config::guardar(archivo(), {{"entradas", todos}});
    emit respaldosCambio();
}

std::filesystem::path RespaldoEntradas::archivo() const { return rt_.dataRoot() / "Config" / "respaldo-entradas.json"; }

nlohmann::json RespaldoEntradas::todos() const {
    const std::lock_guard<std::mutex> l(mu_);
    return respaldos_;
}

nlohmann::json RespaldoEntradas::respaldo(const std::string& entrada) const {
    const std::lock_guard<std::mutex> l(mu_);
    return respaldos_.value(entrada, nlohmann::json::object());
}

std::string RespaldoEntradas::setRespaldo(const std::string& entrada, const std::string& escena,
                                          const std::string& alPerderAire, int ms) {
    if (alPerderAire != "avisar" && alPerderAire != "armar" && alPerderAire != "cortar")
        return tr("al_perder_aire: avisar, armar o cortar").toStdString();
    nlohmann::json r{{"escena", escena}, {"al_perder_aire", alPerderAire}, {"ms", std::clamp(ms, 200, 60000)}};
    // Con su id (de la última foto: esto se pide desde cualquier hilo), así un renombre no la pierde.
    if (const auto f = rt_.ultimaFoto()) seguirRef(r, TablaEscenas(f->idsDeEscenas()));
    nlohmann::json todos;
    {
        const std::lock_guard<std::mutex> l(mu_);
        respaldos_[entrada] = r;
        todos = respaldos_;
    }
    config::guardar(archivo(), {{"entradas", todos}});
    emit respaldosCambio();
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
            // Volvió: se rearma en cada salida.
            const QString fin = QChar('\n') + it.key();
            for (auto h = hecho_.begin(); h != hecho_.end();)
                h = h->endsWith(fin) ? hecho_.erase(h) : std::next(h);
            it = sinSenalDesde_.erase(it);
        } else {
            ++it;
        }
    if (sin != sinSenal_) {
        sinSenal_ = sin;
        emit sinSenalCambio(sinSenal_);
    }
    // Una entrada AL AIRE sin señal: lo que se configuró (avisar, armar o cortar al respaldo).
    // Al aire = se ve en el programa de una salida (escena o capa DSK, el recuadro de LSA
    // también) Y esa salida sale; si no se transmite ni graba, nada (si después sale, avisa
    // entonces). Cada salida con su conmutador.
    if (sin.isEmpty()) return;
    const nlohmann::json resp = todos();
    const TablaEscenas tabla = rt_.engine().tablaEscenas();
    QStringList perdidas; // las de solo avisar, juntas en un aviso (una vez aunque esté en varias)
    for (Conmutador* c : conmutadores_.todos()) {
        const std::string& salida = c->salida();
        if (!rt_.engine().haySalidaEn(salida)) continue;
        const auto alAire = rt_.engine().fuentesEnPrograma(salida);
        for (const QString& e : sin) {
            const QString clave = QString::fromStdString(salida) + QChar('\n') + e;
            if (hecho_.contains(clave)) continue;
            if (std::find(alAire.begin(), alAire.end(), e.toStdString()) == alAire.end()) continue;
            const nlohmann::json r = resp.value(e.toStdString(), nlohmann::json::object());
            if (ahora - sinSenalDesde_.value(e) < r.value("ms", 1000)) continue;
            hecho_.insert(clave);
            // Por el id si lo tiene; en los avisos, el nombre de ahora.
            const std::string escena = apunta(r, tabla);
            const std::string modo = r.value("al_perder_aire", std::string("avisar"));
            const QString qe = QString::fromStdString(tabla.hay(escena) ? tabla.nombre(escena) : escena);
            if (modo == "cortar" && !escena.empty()) {
                const std::string err = c->poner(escena);
                emit aviso(err.empty() ? tr("«%1» se quedó sin señal al aire: se cortó a «%2»").arg(e, qe)
                                       : tr("«%1» se quedó sin señal al aire y no se pudo cortar a «%2»: %3")
                                             .arg(e, qe, QString::fromStdString(err)));
            } else if (modo == "armar" && !escena.empty()) {
                const std::string err = c->armar(escena);
                emit aviso(err.empty() ? tr("«%1» se quedó sin señal al aire: «%2» quedó armada (TAKE)").arg(e, qe)
                                       : tr("«%1» se quedó sin señal al aire: %2").arg(e, QString::fromStdString(err)));
            } else if (tuvoSenal_.contains(e) && !perdidas.contains("«" + e + "»")) {
                // Solo la que tenía señal y la perdió: una entrada de invitado o de pantalla
                // compartida vacía (nadie conectado, nadie compartiendo) es lo normal, no una
                // alarma; la lista de las que no tienen señal está en el tablero (sinSenal).
                perdidas << "«" + e + "»";
            }
        }
    }
    if (perdidas.size() == 1)
        emit aviso(tr("%1 se quedó sin señal AL AIRE").arg(perdidas.first()));
    else if (perdidas.size() > 1)
        emit aviso(tr("%1 y %2 se quedaron sin señal AL AIRE")
                       .arg(perdidas.mid(0, perdidas.size() - 1).join(", "), perdidas.last()));
}

} // namespace ba::app::escenas
