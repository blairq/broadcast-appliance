// ============================================================================
// src/app/ui/CorridaViewModel.cpp — ver CorridaViewModel.hpp
// ============================================================================
#include "ui/CorridaViewModel.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QThread>

#include <algorithm>

#include "comun/Restante.hpp"
#include "evento/LineaEdicion.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;
namespace ln = evento::linea;

int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

QVariant deJson(const json& j) {
    const QJsonDocument d =
        QJsonDocument::fromJson(QByteArray::fromStdString("[" + j.dump(-1, ' ', false, json::error_handler_t::replace) + "]"));
    return d.array().at(0).toVariant();
}

QString qs(const std::string& s) { return QString::fromStdString(s); }

QVariantMap cueAMap(const evento::Cue& c, int bloque, int cue) {
    return QVariantMap{{"id", qs(c.id)},
                       {"tipo", qs(evento::nombreDeTipo(c.tipo))},
                       {"texto", qs(c.texto)},
                       {"datos", deJson(c.datos)},
                       {"duracionSeg", c.duracionSeg},
                       {"automatico", c.automatico},
                       {"falta", qs(c.queLeFalta())},
                       {"bloque", bloque},
                       {"cue", cue}};
}

} // namespace

CorridaViewModel::CorridaViewModel(EventoAbierto& abierto, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                                   ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent)
    : QObject(parent), n_(abierto, guion, rt, ventanilla, sala, graficos) {
    connect(&n_, &ConduccionLinea::correChanged, this, &CorridaViewModel::correChanged);
    connect(&n_, &ConduccionLinea::bitacoraChanged, this, &CorridaViewModel::bitacoraChanged);
    connect(&n_, &ConduccionLinea::notify, this, &CorridaViewModel::notify);
    // Las marcas del reloj cambian con cada cambio de estado (no con la hora).
    connect(this, &CorridaViewModel::correChanged, this, &CorridaViewModel::relojChanged);
    // El reloj de ahora: con las marcas y una vez por segundo (mientras alguien lo lee).
    connect(this, &CorridaViewModel::relojChanged, this, &CorridaViewModel::relojVivoCambio);
    segundero_.setInterval(1000);
    connect(&segundero_, &QTimer::timeout, this, &CorridaViewModel::relojVivoCambio);
}

QVariantMap CorridaViewModel::relojVivo() const {
    // Al segundo (como Reloj.ahora): entre avisos, quien lo relee ve lo mismo.
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return relojEn(marcasReloj(), static_cast<double>(ms - ms % 1000));
}

void CorridaViewModel::connectNotify(const QMetaMethod& signal) {
    if (signal == QMetaMethod::fromSignal(&CorridaViewModel::relojVivoCambio)) {
        ++lectoresReloj_;
        revisarSegundero();
    }
}

void CorridaViewModel::disconnectNotify(const QMetaMethod& signal) {
    // (Con un método inválido se desconectó todo: se recuenta con lo que queda conectado.)
    if (!signal.isValid())
        lectoresReloj_ = isSignalConnected(QMetaMethod::fromSignal(&CorridaViewModel::relojVivoCambio)) ? std::max(lectoresReloj_, 1) : 0;
    else if (signal == QMetaMethod::fromSignal(&CorridaViewModel::relojVivoCambio))
        lectoresReloj_ = std::max(0, lectoresReloj_ - 1);
    else
        return;
    revisarSegundero();
}

void CorridaViewModel::revisarSegundero() {
    // El temporizador es de este hilo; connectNotify puede llegar de otro (raro): allá se encola.
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, &CorridaViewModel::revisarSegundero, Qt::QueuedConnection);
        return;
    }
    if (lectoresReloj_ > 0 && !segundero_.isActive() && !alineando_) {
        // Alineado al segundo, para que cambie junto con los demás relojes de la pantalla.
        alineando_ = true;
        QTimer::singleShot(1000 - static_cast<int>(QDateTime::currentMSecsSinceEpoch() % 1000) + 2, this, [this]() {
            alineando_ = false;
            if (lectoresReloj_ == 0) return;
            emit relojVivoCambio();
            segundero_.start();
        });
    } else if (lectoresReloj_ == 0 && segundero_.isActive()) {
        segundero_.stop();
    }
}

CorridaViewModel::~CorridaViewModel() = default;

QString CorridaViewModel::estado() const {
    const ln::Corrida* c = n_.corrida();
    if (c == nullptr) return "parado";
    switch (c->estado()) {
        case ln::EstadoCorrida::Corriendo: return "corriendo";
        case ln::EstadoCorrida::Pausada: return "pausado";
        case ln::EstadoCorrida::Terminada: return "terminado";
        default: return "parado";
    }
}

bool CorridaViewModel::guionCorriendo() const {
    // Pausado tambien cuenta: el evento esta pasando, el operador solo freno el guion un momento.
    return !n_.ensayo() && n_.corriendo();
}

QVariantMap CorridaViewModel::cueEnVista(int b, int c) const {
    QVariantMap m = cueAMap(*n_.cue(b, c), b, c);
    m["bloqueNombre"] = qs(n_.ev().bloques[static_cast<std::size_t>(b)].nombre);
    if (const auto* r = n_.proy().ref(b, c)) m["paso"] = qs(r->paso);
    return m;
}

QVariantMap CorridaViewModel::actual() const {
    if (!n_.corriendo()) return {};
    const ln::CursorGuion& k = n_.cursor();
    if (n_.cue(k.bloque, k.cue) != nullptr) return cueEnVista(k.bloque, k.cue);
    // Un paso que no está en el guion de cues (uno insertado del estante).
    const ln::Instancia* a = n_.corrida()->alAire(n_.salidaPorOmision());
    const ln::Paso* p = a ? n_.corrida()->linea().paso(a->paso) : nullptr;
    if (p == nullptr) return {};
    return QVariantMap{{"paso", qs(p->id)}, {"texto", qs(p->nombre)}, {"tipo", "paso"},
                       {"bloque", -1},      {"cue", -1},             {"duracionSeg", static_cast<int>(p->duracionPlanMs() / 1000)}};
}

QVariantMap CorridaViewModel::siguiente() const {
    if (!n_.corriendo()) return {};
    const ln::Corrida& corrida = *n_.corrida();
    const std::string s = n_.salidaPorOmision();
    const ln::CursorGuion& k = n_.cursor();
    // Lo que queda del paso al aire (lo que en el v1 era el cue siguiente)…
    const ln::Instancia* a = corrida.alAire(s);
    const auto [nb, nc] = ln::cueSiguiente(n_.ev(), k.bloque, k.cue);
    if (const auto* r = n_.proy().ref(nb, nc); a && r && r->paso == a->paso && !r->pieza.empty() && !corrida.disparada(s, r->pieza))
        return cueEnVista(nb, nc);
    // …y si no, el próximo paso (el que diga la corrida: saltear y mover el próximo cuentan).
    const ln::Resolucion res = corrida.resolver(ahoraMs());
    const ln::SalidaResuelta* sr = res.salida(s);
    if (sr == nullptr || sr->proximo.empty()) return {};
    const auto [b, c] = n_.proy().cueDe(sr->proximo);
    if (n_.cue(b, c) != nullptr) return cueEnVista(b, c);
    const ln::Paso* p = corrida.linea().paso(sr->proximo);
    return p ? QVariantMap{{"paso", qs(p->id)}, {"texto", qs(p->nombre)}, {"tipo", "paso"}} : QVariantMap{};
}

QVariantMap CorridaViewModel::marcasReloj() const {
    const ln::Corrida* c = n_.corrida();
    if (c == nullptr) return {};
    const ln::MarcasReloj m = ln::marcasReloj(n_.ev(), *c, n_.cursor(), n_.corriendo(), ahoraMs());
    QVariantMap out{{"marcaMs", static_cast<double>(m.marcaMs)},
                    {"sigue", m.sigue},
                    {"corriendo", m.corriendo},
                    {"cursor", m.cursor},
                    {"transcurridoMs", static_cast<double>(m.transcurridoMs)},
                    {"enCueMs", static_cast<double>(m.enCueMs)},
                    {"duracionCueSeg", m.duracionCueSeg},
                    {"previstoSeg", m.previstoSeg},
                    {"despuesSeg", m.despuesSeg},
                    {"despuesBloqueSeg", m.despuesBloqueSeg},
                    {"totalPrevistoSeg", m.totalPrevistoSeg},
                    {"cueNro", m.cueNro},
                    {"cuesTotal", m.cuesTotal}};
    if (const int64_t plan = n_.finPlaneadoMs(m.totalPrevistoSeg); plan >= 0) out["finPlaneadoMs"] = static_cast<double>(plan);
    return out;
}

QVariantMap CorridaViewModel::relojEn(const QVariantMap& m, double ahora) {
    if (m.isEmpty()) return QVariantMap{{"desvioSeg", 0}, {"enCueSeg", 0}, {"restanteSeg", 0}};
    ln::MarcasReloj x;
    x.marcaMs = static_cast<int64_t>(m.value("marcaMs").toDouble());
    x.sigue = m.value("sigue").toBool();
    x.corriendo = m.value("corriendo").toBool();
    x.cursor = m.value("cursor").toBool();
    x.transcurridoMs = static_cast<int64_t>(m.value("transcurridoMs").toDouble());
    x.enCueMs = static_cast<int64_t>(m.value("enCueMs").toDouble());
    x.duracionCueSeg = m.value("duracionCueSeg").toInt();
    x.previstoSeg = m.value("previstoSeg").toInt();
    x.despuesSeg = m.value("despuesSeg").toInt();
    x.despuesBloqueSeg = m.value("despuesBloqueSeg").toInt();
    const ln::Reloj r = ln::relojEn(x, static_cast<int64_t>(ahora));
    QVariantMap out{{"desvioSeg", r.desvioSeg},
                    {"enCueSeg", r.enCueSeg},
                    {"restanteSeg", r.restanteSeg},
                    {"estadoRestante", QString::fromLatin1(estadoRestante(r.restanteSeg))},
                    {"cueNro", m.value("cueNro")},
                    {"cuesTotal", m.value("cuesTotal")},
                    {"restanteBloqueSeg", r.restanteBloqueSeg},
                    {"restanteGuionSeg", r.restanteGuionSeg},
                    {"horaFinPrevista", QDateTime::fromMSecsSinceEpoch(r.finPrevistoMs).toString("HH:mm")},
                    {"transcurridoSeg", r.transcurridoSeg},
                    {"totalPrevistoSeg", m.value("totalPrevistoSeg")}};
    if (m.contains("finPlaneadoMs")) {
        const qint64 plan = static_cast<qint64>(m.value("finPlaneadoMs").toDouble());
        out["horaFinPlaneada"] = QDateTime::fromMSecsSinceEpoch(plan).toString("HH:mm");
        // Positivo: sobra tiempo; negativo: hay que recortar.
        out["backTimingSeg"] = static_cast<int>((plan - r.finPrevistoMs) / 1000);
    }
    return out;
}

QVariantMap CorridaViewModel::reloj() const {
    const int64_t t = ahoraMs();
    QVariantMap m = relojEn(marcasReloj(), static_cast<double>(t));
    const ln::Corrida* c = n_.corrida();
    if (c == nullptr) return m;
    // Lo del avance automático, para el MCP y el bus (la página usa las propiedades).
    const std::string s = n_.salidaPorOmision();
    m["entraSoloEnSeg"] = c->entraSoloEnSeg(s, t);
    m["hold"] = c->hold(s);
    m["retoma"] = c->retomando();
    return m;
}

int CorridaViewModel::entraSoloEnSeg() const {
    return n_.corriendo() ? n_.corrida()->entraSoloEnSeg(n_.salidaPorOmision(), ahoraMs()) : 0;
}

bool CorridaViewModel::hold() const { return n_.corriendo() && n_.corrida()->hold(n_.salidaPorOmision()); }

QVariantMap CorridaViewModel::retoma() const {
    const ln::Corrida* c = n_.corrida();
    if (c == nullptr || !c->retomando()) return QVariantMap{{"activa", false}};
    return QVariantMap{{"activa", true},
                       {"pideDescripcion", c->pideDescripcion()},
                       {"descripcion", qs(c->descripcion())},
                       {"resumen", deJson(c->resumenRetoma())}};
}

QVariantMap CorridaViewModel::tiemposGrabadosUi() const {
    const json& g = n_.grabado();
    if (!g.is_object()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(g.dump(-1, ' ', false, json::error_handler_t::replace))).object().toVariantMap();
}

CorridaViewModel::Publico CorridaViewModel::publico() const {
    return ln::publico(n_.ev(), n_.corrida(), n_.cursor(), n_.ensayo(), ahoraMs());
}

} // namespace ba::app
