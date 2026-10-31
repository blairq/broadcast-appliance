// ============================================================================
// src/app/ui/LineaProductor.cpp — ver LineaProductor.hpp
// ============================================================================
#include "ui/LineaProductor.hpp"

#include "control/BusEventos.hpp"
#include "ui/EventoViewModel.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdlib>

namespace ba::app {

using nlohmann::json;

namespace {

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("v")
        .toVariant();
}

/// «3:20» (con «+» o «−» delante si se pide el signo).
QString minutos(int seg, bool signo) {
    const int a = std::abs(seg);
    const QString t = QStringLiteral("%1:%2").arg(a / 60).arg(a % 60, 2, 10, QLatin1Char('0'));
    return signo ? (seg < 0 ? QStringLiteral("−") : QStringLiteral("+")) + t : t;
}

/// Lo resuelto de cada opción, sin las ops (para la vista y el bus).
json sinOps(const json& p) {
    json v = p;
    for (json& o : v["opciones"]) o.erase("ops");
    return v;
}

} // namespace

LineaProductor::LineaProductor(EventoViewModel& ev, control::BusEventos& bus, QObject* parent)
    : QObject(parent), ev_(ev), bus_(bus) {
    // Los temas con id de salida (los sin id son copia de la por omisión).
    oyente_ = bus_.suscribir("linea/+/overunder", [this](const std::string& tema, const json& valor) {
        const auto a = tema.find('/'), b = tema.rfind('/');
        if (a == std::string::npos || b <= a) return;
        overUnder(tema.substr(a + 1, b - a - 1), valor, QDateTime::currentMSecsSinceEpoch());
    });
}

LineaProductor::~LineaProductor() { bus_.desuscribir(oyente_); }

json LineaProductor::ajusteProbado(EventoViewModel& ev, const std::string& salida, int64_t hastaMs) {
    json r = ev.lineaAjustar(salida, hastaMs);
    // Siempre probado: cada opción con cómo quedaría (se propone; la persona elige).
    const auto resultado = [&ev](const json& ops) -> json {
        const json p = ev.lineaEditar(ops, true);
        if (!p.value("ok", false)) return {{"error", p.value("error", std::string())}};
        const json v = p.value("pronostico", json::object());
        return {{"fin", v.value("fin", json())}, {"overUnderSeg", v.value("overUnderSeg", json())},
                {"reparos", p.value("reparos", json::array()).size()}};
    };
    if (r.contains("ops") && !r["ops"].empty()) r["pronostico"] = ev.lineaEditar(r["ops"], true).value("pronostico", json());
    for (json& o : r["opciones"]) o["resultado"] = resultado(o["ops"]);
    for (json& h : r["hardStarts"])
        for (json& o : h["opciones"]) o["resultado"] = resultado(o["ops"]);
    if (hastaMs <= 0) r.erase("objetivo"); // sin hora pedida: el fin planeado (objetivoMs lo dice)
    return r;
}

void LineaProductor::overUnder(const std::string& salida, const json& valor, int64_t ahoraMs) {
    if (!valor.is_object()) return;
    const int over = valor.value("seg", 0);
    const bool corre = valor.value("corrida", json()) == "corriendo";
    if (!activo_ || !corre) {
        sacar(salida);
        return;
    }
    if (over * 2 < vigia_.umbralSeg) sacar(salida); // se recuperó
    if (!vigia_.debeProponer(salida, over, ahoraMs)) return;
    const json a = ajusteProbado(ev_, salida, 0);
    // La mejor: la primera que alcanza; si ninguna, la que más gana.
    const json* mejor = nullptr;
    for (const json& o : a.value("opciones", json::array())) {
        if (o.value("ganaSeg", 0) >= over) {
            mejor = &o;
            break;
        }
        if (!mejor || o.value("ganaSeg", 0) > mejor->value("ganaSeg", 0)) mejor = &o;
    }
    const QString vamos = tr("Vamos %1").arg(minutos(over, true));
    QString texto;
    if (!mejor) texto = tr("%1 y no hay nada recortable: hay que decidir qué acortar").arg(vamos);
    else if (mejor->value("ganaSeg", 0) >= over)
        texto = tr("%1; si se hace esto llegamos: %2").arg(vamos, QString::fromStdString(mejor->value("texto", std::string())));
    else
        texto = tr("%1; con esto se gana %2: %3")
                    .arg(vamos, minutos(mejor->value("ganaSeg", 0), false), QString::fromStdString(mejor->value("texto", std::string())));
    json p{{"salida", salida}, {"overSeg", over}, {"texto", texto.toStdString()}, {"opciones", a.value("opciones", json::array())},
           {"hardStarts", a.value("hardStarts", json::array())}, {"ms", ahoraMs}};
    propuestas_[salida] = p;
    bus_.publicar("linea/" + salida + "/propuesta", sinOps(p), true);
    emit propuestasChanged();
    emit aviso(texto);
}

void LineaProductor::sacar(const std::string& salida) {
    if (propuestas_.erase(salida) == 0) return;
    bus_.publicar("linea/" + salida + "/propuesta", nullptr, true);
    emit propuestasChanged();
}

QVariantList LineaProductor::propuestasUi() const {
    QVariantList l;
    for (const auto& [s, p] : propuestas_) l << aQv(sinOps(p));
    return l;
}

void LineaProductor::setActivo(bool a) {
    if (a == activo_) return;
    activo_ = a;
    if (!a)
        while (!propuestas_.empty()) sacar(propuestas_.begin()->first);
    emit activoChanged();
}

void LineaProductor::setUmbralSeg(int s) {
    s = std::clamp(s, 10, 3600);
    if (s == vigia_.umbralSeg) return;
    vigia_.umbralSeg = s;
    vigia_.escalonSeg = s;
    emit activoChanged();
}

QString LineaProductor::aplicar(const QString& salida, const QString& opcion) {
    const auto it = propuestas_.find(salida.toStdString());
    if (it == propuestas_.end()) return tr("Ya no hay una propuesta para esa salida.");
    const std::string id = opcion.toStdString();
    json ops;
    for (const json& o : it->second.value("opciones", json::array()))
        if (o.value("id", std::string()) == id) ops = o.value("ops", json::array());
    for (const json& h : it->second.value("hardStarts", json::array()))
        for (const json& o : h.value("opciones", json::array()))
            if (o.value("id", std::string()) == id) ops = o.value("ops", json::array());
    if (!ops.is_array() || ops.empty()) return tr("No hay una opción «%1».").arg(opcion);
    const json r = ev_.lineaEditar(ops, false);
    if (!r.value("ok", false)) return QString::fromStdString(r.value("error", std::string()));
    const std::string s = it->first; // sacar() lo borra: no pasarle la clave del mismo elemento
    sacar(s);
    return {};
}

void LineaProductor::descartar(const QString& salida) { sacar(salida.toStdString()); }

} // namespace ba::app
