// ============================================================================
// src/app/tiempos/TiemposModel.cpp — ver TiemposModel.hpp
// ============================================================================
#include "tiempos/TiemposModel.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18n.hpp"
#include "platform/Hilo.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace ba::app::tiempos {

using json = nlohmann::json;

namespace {
QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"x", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("x")
        .toVariant();
}
} // namespace

TiemposModel::TiemposModel(std::function<Fotos()> fotos, std::filesystem::path archivo, QObject* parent)
    : QObject(parent), fotos_(std::move(fotos)), archivo_(std::move(archivo)) {
    timer_.setInterval(kPeriodoMs);
    connect(&timer_, &QTimer::timeout, this, &TiemposModel::tic);
    muestreo_.setInterval(100);
    connect(&muestreo_, &QTimer::timeout, this, &TiemposModel::muestrear);
    // La historia larga y los eventos se juntan siempre (una foto por segundo: barato).
    reloj1s_.setInterval(1000);
    connect(&reloj1s_, &QTimer::timeout, this, &TiemposModel::cadaSegundo);
    reloj1s_.start();
    // Los análisis de antes de este evento.
    const json j = config::leerObjeto(archivo_).value("analisis", json::array());
    if (j.is_array()) analisis_ = j;
    for (auto& a : analisis_)
        if (a.value("estado", std::string()) == "midiendo") a["estado"] = "fallo", a["error"] = ba::i18n::traducir("tiempos", "se cerró la app midiendo");
}

TiemposModel::~TiemposModel() {
    if (hilo_.joinable()) hilo_.join();
}

void TiemposModel::enHilo(std::function<void()> f) {
    if (hilo_.joinable()) hilo_.join(); // uno por vez (empezar y terminar no se pisan)
    hilo_ = std::thread([f = std::move(f)] {
        ba::platform::nombrarHilo("tiempos");
        f();
    });
}

bool TiemposModel::existeEntrada(const std::string& entrada) const {
    if (entrada == "programa" || entrada == "programa:audio") return true; // la composición y la mezcla
    const Fotos f = fotos_();
    return f.motor.contains("entradas") && f.motor["entradas"].is_object() && f.motor["entradas"].contains(entrada);
}

json TiemposModel::recorrido(const std::string& entrada) {
    if (pipelines_.find(entrada) == pipelines_.end()) return nullptr;
    if (!activo()) rehacer(false);
    if (!mapa_.contains("nodos") || !mapa_["nodos"].is_array()) return nullptr;
    const json* ne = nullptr; // apunta adentro de mapa_ (no de una copia: se usa después del for)
    for (const auto& n : mapa_["nodos"])
        if (n["id"] == "entrada:" + entrada || (ne == nullptr && n["id"] == "entrada:" + entrada + ":audio")) ne = &n;
    if (ne == nullptr || !(*ne)["detalle"].contains("pipeline")) {
        // La composición del programa o la mezcla (no son nodos de entrada): lo medido tal cual.
        const json& pl = pipelines_.at(entrada);
        const json dg = diagnosticar(pl, {});
        const qint64 t = pl.value("t_ms", qint64{0});
        return {{"entrada", entrada}, {"t_ms", t}, {"hace_s", t > 0 ? (QDateTime::currentMSecsSinceEpoch() - t) / 1000 : -1},
                {"segundos", pl.value("segundos", 0.0)}, {"resumen", dg.value("resumen", json::object())},
                {"hallazgos", dg.value("hallazgos", json::array())}, {"tarjetas", json::array()},
                {"cruces", pl.value("cruces", json::array())}, {"elementos", pl.value("elementos", json::array())}};
    }
    const json& pl = (*ne)["detalle"]["pipeline"];
    const json& dg = (*ne)["detalle"].value("diagnostico", json::object());
    // Las tarjetas medidas y de quién es cada elemento.
    json tarjetas = json::array();
    std::map<std::string, std::string> duenio;
    const std::string pre = "tarjeta:" + entrada + ":";
    for (const auto& n : mapa_["nodos"]) {
        const std::string id = n.value("id", std::string());
        const json& d = n.value("detalle", json::object());
        if (id.rfind(pre, 0) != 0 || !d.value("medido_sondas", false)) continue;
        json nombres = json::array();
        for (const auto& el : d.value("elementos", json::array())) {
            nombres.push_back(el.value("nombre", std::string()));
            duenio[el.value("nombre", std::string())] = id;
        }
        tarjetas.push_back({{"id", id}, {"titulo", n.value("titulo", std::string())}, {"ms", n["ms"]},
                            {"ms_por_cuadro", d.value("ms_por_cuadro", json())}, {"elemento", d.value("elemento", std::string())},
                            {"memoria_entrada", d.value("memoria_entrada", std::string())},
                            {"memoria_salida", d.value("memoria_salida", std::string())},
                            {"cruces", d.value("cruces", json::array())}, {"elementos", nombres}});
    }
    json elementos = json::array();
    for (json el : pl.value("elementos", json::array())) {
        const auto d = duenio.find(el.value("nombre", std::string()));
        if (d != duenio.end()) el["tarjeta"] = d->second;
        elementos.push_back(std::move(el));
    }
    const qint64 t = pl.value("t_ms", qint64{0});
    return {{"entrada", entrada},
            {"t_ms", t},
            {"hace_s", t > 0 ? (QDateTime::currentMSecsSinceEpoch() - t) / 1000 : -1},
            {"segundos", pl.value("segundos", 0.0)},
            {"rearmada", pl.value("rearmada", false)},
            {"resumen", dg.value("resumen", json::object())},
            {"hallazgos", dg.value("hallazgos", json::array())},
            {"tarjetas", tarjetas},
            {"cruces", pl.value("cruces", json::array())},
            {"elementos", elementos}};
}

void TiemposModel::setActivo(bool a) {
    if (a == activo()) return;
    qInfo("[tiempos] %s", a ? ba::i18n::traducir("tiempos", "midiendo (la página se ve)").c_str()
                            : ba::i18n::traducir("tiempos", "en reposo").c_str());
    if (a) {
        tic(); // que la página no arranque vacía
        timer_.start();
    } else {
        timer_.stop();
        historia_.clear(); // la corta; la larga sigue
    }
    emit activoCambio();
}

bool TiemposModel::rehacer(bool avisar) {
    // Una foto con algo inesperado no tumba la página (ni el MCP): queda el último grafo bueno.
    try {
        Fotos f = fotos_();
        f.pipelines = pipelines_;
        mapa_ = armar(f);
        if (avisar) {
            mapaUi_ = aVariant(mapa_).toMap();
            emit cambio();
        }
        return true;
    } catch (const std::exception& e) {
        qWarning("[tiempos] %s", ba::i18n::llenar(ba::i18n::traducir("tiempos", "no se pudo armar el grafo: %1"), {e.what()}).c_str());
        return false;
    }
}

json TiemposModel::ahora() {
    if (!activo()) rehacer();
    return mapa_;
}

void TiemposModel::tic() {
    if (!rehacer()) return;
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    std::set<std::string> vivos;
    for (const auto& n : mapa_["nodos"]) {
        const std::string id = n.value("id", std::string());
        vivos.insert(id);
        if (!n["ms"].is_number()) continue;
        auto& h = historia_[id];
        h.push_back({t, n["ms"].get<double>(), n["acumulado_ms"].is_number() ? n["acumulado_ms"].get<double>() : 0.0});
        while (!h.empty() && t - h.front().t > kHistoriaS * 1000) h.pop_front();
    }
    for (auto it = historia_.begin(); it != historia_.end();)
        it = vivos.count(it->first) ? std::next(it) : historia_.erase(it);
}

json TiemposModel::nodo(const std::string& id, int segundos) const {
    for (const auto& n : mapa_.value("nodos", json::array()))
        if (n.value("id", std::string()) == id) {
            json out = n;
            json h = json::array();
            for (const QVariant& v : historia(QString::fromStdString(id), segundos)) {
                const QVariantMap m = v.toMap();
                h.push_back({{"t_ms", m["t"].toLongLong()}, {"ms", m["ms"].toDouble()}, {"acumulado_ms", m["acumulado"].toDouble()}});
            }
            out["historia"] = h;
            return out;
        }
    return nullptr;
}

QVariantList TiemposModel::historia(const QString& id, int segundos) const {
    QVariantList out;
    const qint64 desde = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(segundos) * 1000;
    const auto corta = historia_.find(id.toStdString());
    const auto& fuente = segundos <= kHistoriaS && corta != historia_.end() && corta->second.size() > 1 ? historia_ : larga_;
    if (const auto it = fuente.find(id.toStdString()); it != fuente.end())
        for (const auto& m : it->second)
            if (m.t >= desde) out << QVariantMap{{"t", m.t}, {"ms", m.ms}, {"acumulado", m.acumulado}};
    return out;
}

// --- Historia larga y eventos --------------------------------------------------------------

void TiemposModel::cadaSegundo() {
    if (!activo() && !rehacer(false)) return;
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    std::set<std::string> vivos;
    for (const auto& n : mapa_["nodos"]) {
        const std::string id = n.value("id", std::string());
        vivos.insert(id);
        if (!n["ms"].is_number()) continue;
        auto& h = larga_[id];
        h.push_back({t, n["ms"].get<double>(), n["acumulado_ms"].is_number() ? n["acumulado_ms"].get<double>() : 0.0});
        while (!h.empty() && t - h.front().t > kHistoriaLargaS * 1000) h.pop_front();
    }
    // Lo que ya no está y no tiene nada en la ventana, afuera.
    for (auto it = larga_.begin(); it != larga_.end();)
        it = vivos.count(it->first) || (!it->second.empty() && t - it->second.back().t < kHistoriaLargaS * 1000) ? std::next(it)
                                                                                                                : larga_.erase(it);
    detectar(t);
}

void TiemposModel::evento(qint64 t, const std::string& tipo, const std::string& texto, const std::string& entrada) {
    // En orden de tiempo (uno «firme» se anota en el momento en que empezó, unos segundos atrás).
    auto it = eventos_.end();
    while (it != eventos_.begin() && std::prev(it)->value("t", qint64{0}) > t) --it;
    eventos_.insert(it, json{{"t", t}, {"tipo", tipo}, {"texto", texto}, {"entrada", entrada}});
    while (!eventos_.empty() && (eventos_.size() > 500 || t - eventos_.front().value("t", qint64{0}) > kHistoriaLargaS * 1000))
        eventos_.pop_front();
}

void TiemposModel::detectar(qint64 t) {
    // Lo de esta foto que importa para los eventos.
    json ahora{{"activas", json::object()}, {"tarde", json::object()}, {"rearme", json::object()}, {"salidas", json::object()},
               {"escena", ""}, {"latencia", mapa_.value("estudio", json::object()).value("latencia_ms", 0.0)}};
    for (const auto& n : mapa_.value("nodos", json::array())) {
        const std::string tipo = n.value("tipo", std::string());
        if (tipo == "entrada") {
            const json d = n.value("detalle", json::object());
            const std::string e = d.value("entrada", std::string());
            ahora["activas"][e] = ahora["activas"].value(e, false) || n.value("activo", false);
            ahora["tarde"][e] = ahora["tarde"].value(e, false) || d.value("llega_tarde", false);
            const double r = d.value("rearmada_hace_s", -1.0);
            ahora["rearme"][e] = r >= 0 && r < 2.5;
        } else if (tipo == "escena") {
            ahora["escena"] = n.value("titulo", std::string());
        }
    }
    for (const auto& sa : mapa_.value("salidas", json::array())) ahora["salidas"][sa.value("id", std::string())] = sa.value("titulo", std::string());
    if (previo_.is_null()) { // la primera foto: el punto de partida, sin eventos
        previo_ = std::move(ahora);
        return;
    }
    const size_t antes = eventos_.size();
    // Los primeros segundos todo se acomoda (entradas que dan un cuadro y paran, la latencia
    // del estudio que sube): es el punto de partida, no son eventos.
    if (arranque_ == 0) arranque_ = t;
    const bool arrancando = t - arranque_ < kArranqueMs;
    // Conexión y «llega tarde»: solo cuando el cambio se sostiene kFirmeMs (una entrada sin
    // nada que se rearma cada 10 s da datos un ratito: eso no es una conexión). El evento
    // va en el momento en que empezó.
    const auto firme = [&](const char* clave, const std::string& e, bool ahoraSi, const std::string& si, const std::string& no,
                           const char* tipoSi, const char* tipoNo) {
        json& f = firmes_[clave][e];
        if (!f.is_object()) f = {{"estado", ahoraSi}, {"cand", ahoraSi}, {"desde", t}};
        if (ahoraSi != f.value("cand", ahoraSi)) {
            f["cand"] = ahoraSi;
            f["desde"] = t;
        }
        if (f.value("cand", ahoraSi) != f.value("estado", ahoraSi) && t - f.value("desde", t) >= kFirmeMs) {
            f["estado"] = f["cand"];
            if (!arrancando) evento(f.value("desde", t), ahoraSi ? tipoSi : tipoNo, ba::i18n::llenar(ahoraSi ? si : no, {e}), e);
        }
    };
    for (const auto& [e, activa] : ahora["activas"].items())
        firme("activas", e, activa.get<bool>(), ba::i18n::traducir("tiempos", "%1: llegan datos"),
              ba::i18n::traducir("tiempos", "%1: dejaron de llegar datos"), "conexion", "desconexion");
    for (const auto& [e, era] : previo_["activas"].items())
        if (!ahora["activas"].contains(e)) {
            if (firmes_["activas"].value(e, json::object()).value("estado", false))
                evento(t, "desconexion", ba::i18n::llenar(ba::i18n::traducir("tiempos", "%1: ya no está"), {e}), e);
            firmes_["activas"].erase(e);
        }
    for (const auto& [e, tarde] : ahora["tarde"].items())
        firme("tarde", e, tarde.get<bool>(), ba::i18n::traducir("tiempos", "%1: llega tarde a la mezcla"),
              ba::i18n::traducir("tiempos", "%1: volvió a llegar a tiempo"), "tarde", "tarde");
    // Los rearmes, solo de las que estaban andando (las que esperan a alguien se rearman solas cada tanto).
    for (const auto& [e, r] : ahora["rearme"].items())
        if (r.get<bool>() && !previo_["rearme"].value(e, false) && firmes_["activas"].value(e, json::object()).value("estado", false))
            evento(t, "rearme", ba::i18n::llenar(ba::i18n::traducir("tiempos", "%1: se rearmó"), {e}), e);
    const std::string esc = ahora.value("escena", std::string()), escAntes = previo_.value("escena", std::string());
    if (esc != escAntes && !esc.empty() && !arrancando)
        evento(t, "escena", ba::i18n::llenar(ba::i18n::traducir("tiempos", "Al aire: %1"), {esc}));
    for (const auto& [id, titulo] : ahora["salidas"].items())
        if (!previo_["salidas"].contains(id))
            evento(t, "salida", ba::i18n::llenar(ba::i18n::traducir("tiempos", "%1: arrancó"), {titulo.get<std::string>()}));
    for (const auto& [id, titulo] : previo_["salidas"].items())
        if (!ahora["salidas"].contains(id))
            evento(t, "salida", ba::i18n::llenar(ba::i18n::traducir("tiempos", "%1: paró"), {titulo.get<std::string>()}));
    const double lat = ahora.value("latencia", 0.0), latAntes = previo_.value("latencia", 0.0);
    if (!arrancando && std::fabs(lat - latAntes) >= 10.0)
        evento(t, "latencia", ba::i18n::llenar(ba::i18n::traducir("tiempos", "Latencia del estudio: %1 → %2 ms"),
                                               {std::to_string(static_cast<int>(std::lround(latAntes))),
                                                std::to_string(static_cast<int>(std::lround(lat)))}));
    previo_ = std::move(ahora);
    if (eventos_.size() != antes) emit eventosCambio();
}

json TiemposModel::eventosJson(int segundos) const {
    json out = json::array();
    const qint64 desde = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(segundos) * 1000;
    for (const auto& e : eventos_)
        if (e.value("t", qint64{0}) >= desde) out.push_back(e);
    return out;
}

QVariantList TiemposModel::eventos() const { return aVariant(eventosJson(kHistoriaLargaS)).toList(); }

json TiemposModel::agregarEvento(qint64 t, const std::string& texto, const std::string& quien) {
    const qint64 ahoraMs = QDateTime::currentMSecsSinceEpoch();
    if (t <= 0 || t > ahoraMs) t = ahoraMs;
    // En su lugar (los eventos van en orden de tiempo).
    json e{{"t", t}, {"tipo", "nota"}, {"texto", texto.empty() ? std::string("nota") : texto}, {"entrada", ""}, {"quien", quien}};
    auto it = eventos_.begin();
    while (it != eventos_.end() && it->value("t", qint64{0}) <= t) ++it;
    eventos_.insert(it, e);
    emit eventosCambio();
    return e;
}

void TiemposModel::borrarEventoUi(double t, const QString& texto) {
    for (auto it = eventos_.begin(); it != eventos_.end(); ++it)
        if (it->value("tipo", std::string()) == "nota" && it->value("t", qint64{0}) == static_cast<qint64>(t) &&
            it->value("texto", std::string()) == texto.toStdString()) {
            eventos_.erase(it);
            emit eventosCambio();
            return;
        }
}

json TiemposModel::impacto(qint64 t, int segundos) const {
    const qint64 v = static_cast<qint64>(std::clamp(segundos, 2, 300)) * 1000;
    const auto mediana = [](std::vector<double> x) {
        if (x.empty()) return -1.0;
        std::sort(x.begin(), x.end());
        return x[x.size() / 2];
    };
    std::map<std::string, std::string> titulos;
    for (const auto& n : mapa_.value("nodos", json::array())) titulos[n.value("id", std::string())] = n.value("titulo", std::string());
    json out = json::array();
    for (const auto& [id, h] : larga_) {
        std::vector<double> a, d, aa, da;
        for (const auto& m : h) {
            if (m.t >= t - v && m.t < t) a.push_back(m.ms), aa.push_back(m.acumulado);
            else if (m.t >= t && m.t < t + v) d.push_back(m.ms), da.push_back(m.acumulado);
        }
        if (a.empty() || d.empty()) continue;
        const double ma = mediana(a), md = mediana(d), maa = mediana(aa), mda = mediana(da);
        out.push_back({{"id", id}, {"titulo", titulos.count(id) ? titulos[id] : id}, {"antes_ms", std::round(ma * 10) / 10},
                       {"despues_ms", std::round(md * 10) / 10}, {"cambio_ms", std::round((md - ma) * 10) / 10},
                       {"acumulado_antes_ms", std::round(maa * 10) / 10}, {"acumulado_despues_ms", std::round(mda * 10) / 10}});
    }
    std::sort(out.begin(), out.end(), [](const json& x, const json& y) {
        return std::fabs(x["cambio_ms"].get<double>()) > std::fabs(y["cambio_ms"].get<double>());
    });
    return out;
}

// --- Análisis -----------------------------------------------------------------------------

namespace {
double percentil(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())))];
}
double redondo(double ms) { return std::round(ms * 10.0) / 10.0; }
} // namespace

QVariantList TiemposModel::analisis() const { return aVariant(analisis_).toList(); }

QString TiemposModel::analizar(int segundos, std::function<void(const json&)> listo, std::vector<std::string> entradas) {
    if (muestreo_.isActive() || !conSondas_.empty()) return tr("ya hay un análisis midiendo");
    // Las sondas: en las entradas pedidas, o en todas las del motor.
    if (sondas_.empezar) {
        if (entradas.empty()) {
            const Fotos f = fotos_();
            if (f.motor.contains("entradas") && f.motor["entradas"].is_object())
                for (const auto& [id, e] : f.motor["entradas"].items()) entradas.push_back(id);
        } else {
            for (const auto& id : entradas)
                if (!existeEntrada(id)) return tr("no existe la entrada «%1»").arg(QString::fromStdString(id));
        }
        conSondas_ = entradas;
        enHilo([s = sondas_.empezar, entradas]() {
            for (const auto& id : entradas) {
                const std::string e = s(id);
                if (!e.empty())
                    qWarning("[tiempos] %s", ba::i18n::llenar(ba::i18n::traducir("tiempos", "sondas en %1: %2"), {id, e}).c_str());
            }
        });
    }
    segundos_ = std::clamp(segundos, 2, 120);
    desde_ = QDateTime::currentMSecsSinceEpoch();
    muestrasNodo_.clear();
    muestrasSalida_.clear();
    titulosSalida_.clear();
    listo_ = std::move(listo);
    int id = 1;
    for (const auto& a : analisis_) id = std::max(id, a.value("id", 0) + 1);
    analisis_.insert(analisis_.begin(), json{{"id", id}, {"estado", "midiendo"}, {"progreso", 0.0}, {"desde_ms", desde_},
                                             {"segundos", segundos_}, {"tipo", "tiempos"}});
    if (!conSondas_.empty()) analisis_[0]["entradas"] = conSondas_;
    while (analisis_.size() > 50) analisis_.erase(analisis_.end() - 1);
    muestreo_.start();
    emit analisisCambio();
    return {};
}

void TiemposModel::muestrear() {
    if (!rehacer()) return;
    for (const auto& n : mapa_["nodos"])
        if (n["ms"].is_number()) muestrasNodo_[n.value("id", std::string())].push_back(n["ms"].get<double>());
    for (const auto& sa : mapa_["salidas"]) {
        const std::string id = sa.value("id", std::string());
        if (sa["total_ms"].is_number()) muestrasSalida_[id].push_back(sa["total_ms"].get<double>());
        titulosSalida_[id] = sa.value("titulo", id);
    }
    const qint64 pasado = QDateTime::currentMSecsSinceEpoch() - desde_;
    analisis_[0]["progreso"] = std::min(1.0, static_cast<double>(pasado) / (segundos_ * 1000.0));
    if (pasado >= segundos_ * 1000) terminar();
    else emit analisisCambio();
}

void TiemposModel::terminar() {
    muestreo_.stop();
    json& a = analisis_[0];
    json nodos = json::object();
    for (const auto& [id, v] : muestrasNodo_)
        nodos[id] = {{"p50_ms", redondo(percentil(v, 0.5))}, {"p95_ms", redondo(percentil(v, 0.95))}, {"n", v.size()}};
    json salidas = json::array();
    for (const auto& [id, v] : muestrasSalida_)
        salidas.push_back({{"id", id}, {"titulo", titulosSalida_[id]}, {"total_ms", redondo(percentil(v, 0.5))},
                           {"p95_ms", redondo(percentil(v, 0.95))}});
    a["nodos"] = nodos;
    a["salidas"] = salidas;
    if (conSondas_.empty()) return cerrar();
    // Sacar las sondas (en el hilo) y volver con lo medido.
    enHilo([this, s = sondas_.terminar, entradas = conSondas_, vivo = QPointer<TiemposModel>(this)]() {
        auto r = std::make_shared<std::map<std::string, json>>();
        for (const auto& id : entradas) (*r)[id] = s(id);
        QMetaObject::invokeMethod(this, [this, vivo, r]() {
            if (!vivo) return;
            const qint64 t = QDateTime::currentMSecsSinceEpoch();
            json pls = json::object();
            for (auto& [id, p] : *r) {
                if (!p.is_object() || p.contains("error")) continue;
                p["t_ms"] = t;
                pipelines_[id] = p;
                pls[id] = p;
            }
            conSondas_.clear();
            analisis_[0]["pipelines"] = pls;
            // Las tarjetas pasan a «medido»: lo de las sondas (mediana y p95 de lo que agrega
            // cada elemento) reemplaza lo que informaba cada una.
            if (rehacer(activo()))
                for (const auto& n : mapa_["nodos"]) {
                    const json& d = n.value("detalle", json::object());
                    if (!d.value("medido_sondas", false) || !n["ms"].is_number()) continue;
                    double p95 = 0;
                    for (const auto& el : d.value("elementos", json::array())) p95 += el.value("agrega_p95_ms", 0.0);
                    analisis_[0]["nodos"][n.value("id", std::string())] = {
                        {"p50_ms", redondo(n["ms"].get<double>())}, {"p95_ms", redondo(p95)}, {"fuente", "sondas"}};
                }
            cerrar();
        }, Qt::QueuedConnection);
    });
}

void TiemposModel::cerrar() {
    json& a = analisis_[0];
    const json& nodos = a["nodos"];
    a["progreso"] = 1.0;
    if (nodos.empty() && a.value("pipelines", json::object()).empty()) {
        a["estado"] = "fallo";
        a["error"] = ba::i18n::traducir("tiempos", "no llegó ninguna medición (¿el motor está andando?)");
    } else {
        a["estado"] = "listo";
    }
    guardar();
    emit analisisCambio();
    if (listo_) {
        auto f = std::move(listo_);
        listo_ = {};
        f(a);
    }
}

void TiemposModel::borrarAnalisis() {
    if (muestreo_.isActive() || !conSondas_.empty()) return;
    analisis_ = json::array();
    guardar();
    emit analisisCambio();
}

void TiemposModel::guardar() const {
    if (archivo_.empty()) return;
    config::guardar(archivo_, {{"analisis", analisis_}});
}

} // namespace ba::app::tiempos
