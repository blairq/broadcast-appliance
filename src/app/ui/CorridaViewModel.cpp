// ============================================================================
// src/app/ui/CorridaViewModel.cpp — ver CorridaViewModel.hpp
// ============================================================================
#include "ui/CorridaViewModel.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

#include "ajustes/Ajustes.hpp"
#include "comun/Restante.hpp"
#include "arranque/AppRuntime.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "asamblea/Votaciones.hpp"
#include "control/BusEventos.hpp"
#include "control/Dsk.hpp"
#include "diag/Registro.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "evento/Condicion.hpp"
#include "evento/LineaPlantillas.hpp"
#include "evento/LineaProductor.hpp"
#include "evento/LineaVista.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "i18n/I18n.hpp"
#include "medios/Playlist.hpp"
#include "persist/ColaEscritura.hpp"
#include "ptz/ControlCamaras.hpp"
#include "sala/OradoresSala.hpp"
#include "sala/Teleprompter.hpp"
#include "ui/CarruselViewModel.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EjecutorLinea.hpp"
#include "ui/EventoAbierto.hpp"
#include <QCoreApplication>
#include "ui/GuionViewModel.hpp"
#include "ui/CapturaError.hpp"

using ::ba::app::errorDe;

namespace ba::app {
namespace {

using json = nlohmann::json;

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

/// El nombre fijo de la música del evento: un tema nuevo reemplaza al anterior en vez
/// de encimarse, que es lo peor que puede pasar en vivo.
constexpr const char* kMusica = "Música del evento";
/// La escena y la fuente que usa el cue de video. Nombres fijos: así un video nuevo
/// reemplaza al anterior en vez de encimarse, y el operador puede componer esa escena una
/// vez y que le sirva para todos. OJO: una escena TAMBIÉN es una fuente, y los nombres son
/// únicos entre todas (si la fuente se llamara igual que su escena, se le agrega un « 2»
/// y después nada la encuentra por nombre).
constexpr const char* kEscenaVideo = "Video del evento";
constexpr const char* kVideo = "Video";

using evento::linea::Accion;

} // namespace

CorridaViewModel::CorridaViewModel(EventoAbierto& abierto, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                                   ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent)
    : QObject(parent), ab_(abierto), guion_(guion), rt_(rt), ventanilla_(ventanilla), sala_(sala), graficos_(graficos) {
    // El estado vivo (la sección `linea` del documento de aire): «cada segundo y en cada
    // cambio». Lo que entra solo lo programa cada EjecutorLinea a la hora justa. El reloj de
    // la vista no es este: la vista cuenta con el Reloj de las vistas y las marcas, que
    // cambian con cada cambio de estado.
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, &CorridaViewModel::escribirVivo);
    connect(this, &CorridaViewModel::correChanged, this, &CorridaViewModel::relojChanged);
    connect(&ab_, &EventoAbierto::lineaCambio, this, &CorridaViewModel::lineaCambio);
    connect(&ab_, &EventoAbierto::abiertoCambio, this, [this]() { revision_ = -1; });
    // Los medios que suenan llegan en la foto (parte Medidas): ahí se ve si terminó el de
    // una pieza que termina con su medio.
    connect(&ventanilla_, &VentanillaMotor::cambio, this, [this](unsigned p) {
        if ((p & FotoMotor::Medidas) != 0 && !mediosVigilados_.empty()) mirarMedios();
    });
}

CorridaViewModel::~CorridaViewModel() = default;

const evento::Evento& CorridaViewModel::ev() const { return ab_.evento(); }
const evento::linea::Linea& CorridaViewModel::linea() const { return ab_.linea(); }
const evento::linea::Proyeccion& CorridaViewModel::proy() const { return ab_.proyeccion(); }
const evento::Cue* CorridaViewModel::cue(int bloque, int c) const { return ab_.cue(bloque, c); }
std::string CorridaViewModel::salidaPorOmision() const { return linea().salidasEfectivas().front(); }

void CorridaViewModel::limpiarBitacora() {
    bitacora_.clear();
    emit bitacoraChanged();
}

void CorridaViewModel::olvidar() {
    parar();
    ejecutores_.clear();
    corrida_.reset();
    cursor_ = {};
    emit correChanged();
}

void CorridaViewModel::lineaCambio(bool desdeLinea) {
    if (corrida_) corrida_->setLinea(lineaDeCorrida());
    vigilarTemas();
    if (desdeLinea && corrida_) despachar(corrida_->alDia(ahoraMs()));
}

void CorridaViewModel::ensayoAcelerado() {
    if (ev().cantidadDeCues() == 0) {
        emit notify(QCoreApplication::translate("EventoViewModel", "El guion no tiene ningún cue"), "error");
        return;
    }
    // Se camina el recorrido de verdad, no la lista: asi se ve el orden real
    // y quedan a la vista los bloques que nunca se visitan.
    const std::vector<std::string> escenas = ventanilla_.foto().nombresDeEscenas();
    int visitados = 0;
    int problemas = 0;
    int sinEjecutar = 0;
    bitacora_.clear();

    // En el orden en que pasa: bloque por bloque y cue por cue.
    for (std::size_t bi = 0; bi < ev().bloques.size(); ++bi)
        for (std::size_t ci = 0; ci < ev().bloques[bi].cues.size(); ++ci) {
        const evento::Cue& c = ev().bloques[bi].cues[ci];
        ++visitados;
        const QString donde = QString("%1.%2").arg(bi + 1).arg(ci + 1);
        const QString que = c.texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c.tipo))
                                            : QString::fromStdString(c.texto);
        const std::string falta = c.queLeFalta();
        if (!falta.empty()) {
            ++problemas;
            anotar(QCoreApplication::translate("EventoViewModel", "%1 %2 — %3").arg(donde, que, QString::fromStdString(falta)), "error");
            continue;
        }
        if (c.tipo == evento::Tipo::Escena) {
            const std::string escena = c.datos.value("escena", std::string());
            if (std::find(escenas.begin(), escenas.end(), escena) == escenas.end()) {
                ++problemas;
                anotar(QCoreApplication::translate("EventoViewModel", "%1 %2 — no existe la escena \"%3\"").arg(donde, que, QString::fromStdString(escena)), "error");
                continue;
            }
        }
        if (!evento::ejecutable(c.tipo)) {
            ++sinEjecutar;
            anotar(QCoreApplication::translate("EventoViewModel", "%1 %2 — el guion lo tiene, pero todavía no sé ejecutarlo").arg(donde, que), "info");
            continue;
        }
        anotar(QCoreApplication::translate("EventoViewModel", "%1 %2 — listo").arg(donde, que), "ok");
        }

    // Bloques que el recorrido nunca pisa: estan escritos pero no pasan.
    int colgados = 0;
    for (const evento::Bloque& b : ev().bloques)
        if (b.cues.empty()) ++colgados;

    QString resumen =
        QCoreApplication::translate("EventoViewModel", "Ensayo acelerado: %1 de %2 cues recorridos").arg(visitados).arg(ev().cantidadDeCues());
    if (colgados > 0) resumen += QCoreApplication::translate("EventoViewModel", ", %n bloque vacío que no pasa", "", colgados);
    if (sinEjecutar > 0) resumen += QCoreApplication::translate("EventoViewModel", ", %1 sin ejecución todavía").arg(sinEjecutar);
    if (problemas > 0) resumen += QCoreApplication::translate("EventoViewModel", ", %1 con problemas").arg(problemas);
    anotar(resumen, problemas > 0 ? "error" : "ok");
    emit notify(resumen, problemas > 0 ? "error" : "ok");
}

// --- Correr -----------------------------------------------------------------

bool CorridaViewModel::corriendo() const {
    if (!corrida_) return false;
    const auto e = corrida_->estado();
    return e == evento::linea::EstadoCorrida::Corriendo || e == evento::linea::EstadoCorrida::Pausada;
}

QString CorridaViewModel::estado() const {
    if (!corrida_) return "parado";
    switch (corrida_->estado()) {
        case evento::linea::EstadoCorrida::Corriendo: return "corriendo";
        case evento::linea::EstadoCorrida::Pausada: return "pausado";
        case evento::linea::EstadoCorrida::Terminada: return "terminado";
        default: return "parado";
    }
}

bool CorridaViewModel::guionCorriendo() const {
    // Pausado tambien cuenta: el evento esta pasando, el operador solo freno
    // el guion un momento.
    return !ensayo_ && corriendo();
}

QVariantMap CorridaViewModel::actual() const {
    if (!corriendo()) return {};
    if (const evento::Cue* c = cue(cursor_.bloque, cursor_.cue)) {
        QVariantMap m = cueAMap(*c, cursor_.bloque, cursor_.cue);
        m["bloqueNombre"] = QString::fromStdString(ev().bloques[static_cast<std::size_t>(cursor_.bloque)].nombre);
        if (const auto* r = proy().ref(cursor_.bloque, cursor_.cue)) m["paso"] = QString::fromStdString(r->paso);
        return m;
    }
    // Un paso que no está en el guion de cues (uno insertado del estante).
    const evento::linea::Instancia* a = corrida_->alAire(salidaPorOmision());
    const evento::linea::Paso* p = a ? corrida_->linea().paso(a->paso) : nullptr;
    if (p == nullptr) return {};
    return QVariantMap{{"paso", QString::fromStdString(p->id)},     {"texto", QString::fromStdString(p->nombre)},
                       {"tipo", "paso"},                             {"bloque", -1},
                       {"cue", -1},                                  {"duracionSeg", static_cast<int>(p->duracionPlanMs() / 1000)}};
}

QVariantMap CorridaViewModel::siguiente() const {
    if (!corriendo()) return {};
    const std::string s = salidaPorOmision();
    const auto mapa = [this](int b, int c) {
        QVariantMap m = cueAMap(*cue(b, c), b, c);
        m["bloqueNombre"] = QString::fromStdString(ev().bloques[static_cast<std::size_t>(b)].nombre);
        if (const auto* r = proy().ref(b, c)) m["paso"] = QString::fromStdString(r->paso);
        return m;
    };
    // Lo que queda del paso al aire (lo que en el v1 era el cue siguiente)…
    const evento::linea::Instancia* a = corrida_->alAire(s);
    const auto [nb, nc] = cueSiguienteDe(cursor_.bloque, cursor_.cue);
    if (const auto* r = proy().ref(nb, nc); a && r && r->paso == a->paso && !r->pieza.empty() && !corrida_->disparada(s, r->pieza))
        return mapa(nb, nc);
    // …y si no, el próximo paso (el que diga la corrida: saltear y mover el próximo cuentan).
    const evento::linea::Resolucion res = corrida_->resolver(ahoraMs());
    const evento::linea::SalidaResuelta* sr = res.salida(s);
    if (sr == nullptr || sr->proximo.empty()) return {};
    const auto [b, c] = proy().cueDe(sr->proximo);
    if (cue(b, c) != nullptr) return mapa(b, c);
    const evento::linea::Paso* p = corrida_->linea().paso(sr->proximo);
    return p ? QVariantMap{{"paso", QString::fromStdString(p->id)}, {"texto", QString::fromStdString(p->nombre)}, {"tipo", "paso"}}
             : QVariantMap{};
}

void CorridaViewModel::moverCursor(int bloque, int cue) {
    const int64_t t = ahoraMs();
    cursor_ = {bloque, cue, t, corrida_ ? corrida_->pausadoMs(t) : 0};
}

std::pair<int, int> CorridaViewModel::cueSiguienteDe(int bloque, int cue) const {
    const int n = static_cast<int>(ev().bloques.size());
    if (bloque < 0) bloque = 0, cue = -1;
    for (int b = bloque; b < n; ++b) {
        const int desde = b == bloque ? cue + 1 : 0;
        if (desde < static_cast<int>(ev().bloques[static_cast<std::size_t>(b)].cues.size())) return {b, desde};
    }
    return {-1, -1};
}

std::pair<int, int> CorridaViewModel::cueAnteriorDe(int bloque, int cue) const {
    if (bloque < 0 || bloque >= static_cast<int>(ev().bloques.size())) return {-1, -1};
    if (cue > 0) return {bloque, cue - 1};
    for (int b = bloque - 1; b >= 0; --b) {
        const auto& cues = ev().bloques[static_cast<std::size_t>(b)].cues;
        if (!cues.empty()) return {b, static_cast<int>(cues.size()) - 1};
    }
    return {-1, -1};
}

int CorridaViewModel::enCueSeg(int64_t ahora) const {
    if (!corrida_ || cursor_.desdeMs == 0) return 0;
    // Lo pausado mientras estaba en este cue no cuenta: el operador paró el evento.
    const int64_t pausa = corrida_->pausadoMs(ahora) - cursor_.pausadoAlEntrarMs;
    return static_cast<int>(std::max<int64_t>(0, ahora - cursor_.desdeMs - pausa) / 1000);
}

QVariantMap CorridaViewModel::marcasReloj() const {
    if (!corrida_) return {};
    const int64_t t = ahoraMs();
    const Cursor& p = cursor_;
    // En qué número de cue vamos, para "12 de 34".
    int nro = 0;
    for (int b = 0; b < static_cast<int>(ev().bloques.size()) && b <= p.bloque; ++b)
        nro += b == p.bloque ? p.cue + 1 : static_cast<int>(ev().bloques[static_cast<std::size_t>(b)].cues.size());
    // Back-timing (T46): lo previsto de lo que sigue al cue (el del cue se descuenta en
    // relojEn con lo que lleva), del guion y del bloque.
    int despues = 0, despuesBloque = 0, total = 0;
    for (int b = 0; b < static_cast<int>(ev().bloques.size()); ++b) {
        const auto& cues = ev().bloques[static_cast<std::size_t>(b)].cues;
        for (int c = 0; c < static_cast<int>(cues.size()); ++c) {
            const int d = cues[static_cast<std::size_t>(c)].duracionSeg;
            total += d;
            if (!p.valido() || !(b > p.bloque || (b == p.bloque && c > p.cue))) continue;
            despues += d;
            if (b == p.bloque) despuesBloque += d;
        }
    }
    const evento::Cue* c = cue(p.bloque, p.cue);
    // Lo que lleva el evento y el cue sin las pausas, en `t`; corriendo (no en pausa),
    // siguen con la hora.
    const int64_t transcurrido = corrida_->arranqueMs() > 0 ? t - corrida_->arranqueMs() - corrida_->pausadoMs(t) : 0;
    int64_t enCue = 0;
    if (cursor_.desdeMs != 0) enCue = std::max<int64_t>(0, t - cursor_.desdeMs - (corrida_->pausadoMs(t) - cursor_.pausadoAlEntrarMs));
    QVariantMap m{{"marcaMs", static_cast<double>(t)},
                  {"sigue", corrida_->estado() == evento::linea::EstadoCorrida::Corriendo},
                  {"corriendo", corriendo()},
                  {"cursor", p.valido()},
                  {"transcurridoMs", static_cast<double>(transcurrido)},
                  {"enCueMs", static_cast<double>(enCue)},
                  {"duracionCueSeg", c != nullptr ? c->duracionSeg : 0},
                  {"previstoSeg", p.valido() ? ev().offsetDeCueSeg(static_cast<std::size_t>(p.bloque), static_cast<std::size_t>(p.cue)) : -1},
                  {"despuesSeg", despues},
                  {"despuesBloqueSeg", despuesBloque},
                  {"totalPrevistoSeg", total},
                  {"cueNro", p.valido() ? nro : 0},
                  {"cuesTotal", static_cast<int>(ev().cantidadDeCues())}};
    if (!ev().inicio.empty()) {
        const QTime ini = QTime::fromString(QString::fromStdString(ev().inicio), "HH:mm");
        if (ini.isValid()) m["finPlaneadoMs"] = static_cast<double>(QDateTime(QDate::currentDate(), ini.addSecs(total)).toMSecsSinceEpoch());
    }
    return m;
}

QVariantMap CorridaViewModel::relojEn(const QVariantMap& m, double ahora) {
    if (m.isEmpty()) return QVariantMap{{"desvioSeg", 0}, {"enCueSeg", 0}, {"restanteSeg", 0}};
    const double dt = m.value("sigue").toBool() ? std::max(0.0, ahora - m.value("marcaMs").toDouble()) : 0.0;
    const int transcurrido = static_cast<int>((m.value("transcurridoMs").toDouble() + dt) / 1000);
    const int enCue = static_cast<int>((m.value("enCueMs").toDouble() + dt) / 1000);
    const int dur = m.value("duracionCueSeg").toInt();
    const int falta = m.value("cursor").toBool() ? std::max(0, dur - enCue) : 0;
    const int restanteGuion = m.value("despuesSeg").toInt() + falta;
    const int previsto = m.value("previstoSeg").toInt();
    const qint64 fin = static_cast<qint64>(ahora) + restanteGuion * 1000LL;
    // El desvío contra lo previsto para el cue donde está: positivo = vamos tarde.
    QVariantMap r{{"desvioSeg", previsto >= 0 && m.value("corriendo").toBool() ? transcurrido - previsto : 0},
                  {"enCueSeg", enCue},
                  {"restanteSeg", dur > 0 ? dur - enCue : 0},
                  {"estadoRestante", QString::fromLatin1(estadoRestante(dur > 0 ? dur - enCue : 0))},
                  {"cueNro", m.value("cueNro")},
                  {"cuesTotal", m.value("cuesTotal")},
                  {"restanteBloqueSeg", m.value("despuesBloqueSeg").toInt() + falta},
                  {"restanteGuionSeg", restanteGuion},
                  {"horaFinPrevista", QDateTime::fromMSecsSinceEpoch(fin).toString("HH:mm")},
                  {"transcurridoSeg", transcurrido},
                  {"totalPrevistoSeg", m.value("totalPrevistoSeg")}};
    if (m.contains("finPlaneadoMs")) {
        const qint64 plan = static_cast<qint64>(m.value("finPlaneadoMs").toDouble());
        r["horaFinPlaneada"] = QDateTime::fromMSecsSinceEpoch(plan).toString("HH:mm");
        // Positivo: sobra tiempo; negativo: hay que recortar.
        r["backTimingSeg"] = static_cast<int>((plan - fin) / 1000);
    }
    return r;
}

QVariantMap CorridaViewModel::reloj() const {
    const int64_t t = ahoraMs();
    QVariantMap m = relojEn(marcasReloj(), static_cast<double>(t));
    if (!corrida_) return m;
    // Lo del avance automático, para el MCP y el bus (la página usa las propiedades).
    const std::string s = salidaPorOmision();
    m["entraSoloEnSeg"] = corrida_->entraSoloEnSeg(s, t);
    m["hold"] = corrida_->hold(s);
    m["retoma"] = corrida_->retomando();
    return m;
}

int CorridaViewModel::entraSoloEnSeg() const { return corriendo() ? corrida_->entraSoloEnSeg(salidaPorOmision(), ahoraMs()) : 0; }

bool CorridaViewModel::hold() const { return corriendo() && corrida_->hold(salidaPorOmision()); }

QVariantMap CorridaViewModel::retoma() const {
    if (!corrida_ || !corrida_->retomando()) return QVariantMap{{"activa", false}};
    return QVariantMap{{"activa", true},
                       {"pideDescripcion", corrida_->pideDescripcion()},
                       {"descripcion", QString::fromStdString(corrida_->descripcion())},
                       {"resumen", deJson(corrida_->resumenRetoma())}};
}

void CorridaViewModel::setEnsayo(bool on) {
    if (ensayo_ == on) return;
    ensayo_ = on;
    emit correChanged();
    anotar(on ? QCoreApplication::translate("EventoViewModel", "Ensayo: el guion corre sin salir al aire") : QCoreApplication::translate("EventoViewModel", "Ensayo apagado: el guion sale al aire"), "info");
}

void CorridaViewModel::anotar(const QString& texto, const QString& tipo, bool alRegistro) {
    QVariantMap m{
        {"hora", QDateTime::currentDateTime().toString("HH:mm:ss")}, {"texto", texto}, {"tipo", tipo}};
    bitacora_.prepend(m);
    // La bitacora de la página es para mirar de reojo; la del histórico
    // (bitacora.jsonl del evento en curso) la toma del registro general.
    if (alRegistro)
        ba::diag::registro().linea(tipo == "error" ? ba::diag::Nivel::Aviso : ba::diag::Nivel::Info, "guion", 0,
                                   texto.toStdString());
    while (bitacora_.size() > 60)
        bitacora_.removeLast();
    emit bitacoraChanged();
}

int CorridaViewModel::arrancarRevisando(bool forzar) {
    guion_.revisar();
    const int graves = guion_.graves();
    if (graves > 0 && !forzar) return graves;
    arrancar();
    return 0;
}

void CorridaViewModel::nuevaCorrida() {
    for (auto& [s, e] : ejecutores_)
        e->parar();
    ejecutores_.clear();
    corrida_ = std::make_unique<evento::linea::Corrida>(lineaDeCorrida(), entradaBase(true));
    cursor_ = {};
    palabras_.clear();
    grabado_ = nullptr;
    graficosDePieza_.clear();
    musicaDePieza_.clear();
    dskDePieza_.clear();
    ptzPendientes_.clear();
    mediosVigilados_.clear();
    playlistDePieza_.clear();
    // Un ejecutor por salida: su timer va a la hora justa de lo que le toca. El tick
    // es uno para todas (la corrida las resuelve juntas).
    for (const std::string& s : linea().salidasEfectivas()) {
        auto e = std::make_unique<EjecutorLinea>(*corrida_, s, [this](const Accion& a) { hacerAccion(a); }, [] { return ahoraMs(); },
                                                 this);
        connect(e.get(), &EjecutorLinea::toca, this, [this]() {
            if (corrida_) despachar(corrida_->tick(ahoraMs()));
        });
        ejecutores_[s] = std::move(e);
    }
    // Lo que la línea mira afuera (el bus, el aire de cada salida, el orden del día).
    enchufar();
    vigilarTemas();
}

void CorridaViewModel::despachar(const std::vector<Accion>& acc) {
    seguirPalabra(acc);
    for (const Accion& a : acc) {
        auto it = ejecutores_.find(a.salida);
        if (it == ejecutores_.end()) it = ejecutores_.find(salidaPorOmision());
        if (it != ejecutores_.end()) it->second->hacer(a);
    }
    for (auto& [s, e] : ejecutores_)
        e->programar();
    volcarRecorrido();
    // Terminó el ensayo que graba: los tiempos quedan para que una persona los confirme.
    if (!corriendo() && grabar_ && corrida_ && grabado_.is_null()) {
        grabado_ = evento::linea::tiemposGrabados(linea(), corrida_->instancias());
        if (!grabado_["ops"].empty())
            emit notify(QCoreApplication::translate("EventoViewModel", "El ensayo grabó los tiempos de %n paso(s): confirmalos para que queden como duraciones previstas", "",
                           static_cast<int>(grabado_["ops"].size())),
                        "info");
    }
    avisarPresentador();
    if (corriendo()) {
        escribirVivo();
        reloj_.start();
    } else {
        reloj_.stop();
        borrarVivo();
    }
    emit correChanged();
}

void CorridaViewModel::arrancar() {
    std::size_t pasos = 0;
    for (const auto& b : linea().bloques)
        pasos += b.pasos.size();
    if (pasos == 0) {
        emit notify(QCoreApplication::translate("EventoViewModel", "El guion no tiene ningún cue"), "error");
        return;
    }
    // Correrlo de verdad lo pone en curso: lo que pase desde ahora es de este evento.
    if (!ensayo_ && ab_.hay() && ponerEnCurso_) ponerEnCurso_();
    nuevaCorrida();
    anotar(ensayo_ ? QCoreApplication::translate("EventoViewModel", "Arranca el ensayo") : QCoreApplication::translate("EventoViewModel", "Arranca el evento"), "ok");
    despachar(corrida_->arrancar(ahoraMs()));
}

void CorridaViewModel::retroceder() {
    if (!corriendo() || !cursor_.valido()) return;
    const auto [pb, pc] = cueAnteriorDe(cursor_.bloque, cursor_.cue);
    const auto* r = proy().ref(pb, pc);
    if (r == nullptr) return;
    anotar(QCoreApplication::translate("EventoViewModel", "Vuelve atrás"), "info");
    // Dentro del mismo paso: se vuelve a disparar esa pieza. Si no, se va a su paso.
    const evento::linea::Instancia* a = corrida_->alAire(salidaPorOmision());
    if (a && r->paso == a->paso && !r->pieza.empty()) {
        despachar(corrida_->adlib(salidaPorOmision(), r->pieza, ahoraMs()));
        moverCursor(pb, pc);
        emit correChanged();
        return;
    }
    irA(pb, pc);
}

void CorridaViewModel::irA(int bloque, int cue) {
    const auto* r = proy().ref(bloque, cue);
    if (r == nullptr) return;
    const std::string s = salidaPorOmision();
    if (!corriendo()) {
        if (!ensayo_ && ab_.hay() && ponerEnCurso_) ponerEnCurso_();
        nuevaCorrida();
    }
    std::string err;
    std::vector<Accion> acc = corrida_->irA(s, r->paso, ahoraMs(), &err);
    if (!err.empty()) {
        anotar(QString::fromStdString(err), "error");
        despachar(acc);
        return;
    }
    // El cue elegido, si es una pieza del medio del paso, se hace ya (como en el v1).
    if (!r->pieza.empty() && !corrida_->disparada(s, r->pieza)) {
        const std::vector<Accion> mas = corrida_->adlib(s, r->pieza, ahoraMs());
        acc.insert(acc.end(), mas.begin(), mas.end());
    }
    anotar(QCoreApplication::translate("EventoViewModel", "Salta al cue elegido"), "info");
    despachar(acc);
    moverCursor(bloque, cue);
    emit correChanged();
}

void CorridaViewModel::pausar() {
    if (!corriendo()) return;
    corrida_->pausar(ahoraMs());
    anotar(QCoreApplication::translate("EventoViewModel", "Guion en pausa"), "info");
    despachar({});
}

void CorridaViewModel::reanudar() {
    if (!corriendo()) return;
    anotar(QCoreApplication::translate("EventoViewModel", "Sigue el guion"), "info");
    despachar(corrida_->reanudar(ahoraMs()));
}

void CorridaViewModel::parar() {
    if (!corriendo()) return;
    sacarAlAvanzar();
    despachar(corrida_->terminar(ahoraMs()));
}

// --- La línea corriendo -----------------------------------------------------------

evento::linea::Entrada CorridaViewModel::entradaBase(bool hoy) const {
    evento::linea::Entrada e;
    const QTimeZone tz = ev().huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(ev().huso));
    const QTimeZone z = tz.isValid() ? tz : QTimeZone::systemTimeZone();
    QDate dia = QDateTime::currentDateTime(z).date();
    if (!hoy && evento::fechaValida(ev().fecha)) dia = QDate::fromString(QString::fromStdString(ev().fecha), "yyyy-MM-dd");
    e.medianocheMs = QDateTime(dia, QTime(0, 0), z).toMSecsSinceEpoch();
    if (evento::horaValida(ev().inicio))
        e.inicioPlaneadoMs = QDateTime(dia, QTime::fromString(QString::fromStdString(ev().inicio), "HH:mm"), z).toMSecsSinceEpoch();
    // Las cámaras de cada escena, para los conflictos de PTZ entre salidas: las fuentes
    // de la escena (una cámara es una entrada, y una PTZ nombra la suya).
    e.camarasDeEscena = [this](const std::string& escena) { return ventanilla_.foto().fuentesDe(escena); };
    return e;
}

std::string CorridaViewModel::horaDe(int64_t ms) const {
    const QTimeZone tz = ev().huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(ev().huso));
    return QDateTime::fromMSecsSinceEpoch(ms, tz.isValid() ? tz : QTimeZone::systemTimeZone()).toString("HH:mm:ss").toStdString();
}

int64_t CorridaViewModel::horaHoyMs(const std::string& hhmm) const {
    if (!evento::horaValida(hhmm)) return -1;
    const QTimeZone tz = ev().huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(ev().huso));
    const QTimeZone z = tz.isValid() ? tz : QTimeZone::systemTimeZone();
    return QDateTime(QDateTime::currentDateTime(z).date(), QTime::fromString(QString::fromStdString(hhmm), "HH:mm"), z)
        .toMSecsSinceEpoch();
}



void CorridaViewModel::escribirVivo() {
    // Lo que sale de verdad: el ensayo no se retoma.
    if (!corriendo() || ensayo_) return;
    json j = corrida_->aJsonVivo(ahoraMs());
    j["evento"] = ab_.carpeta().toStdString();
    // La sección de la línea del documento único de aire (F3-04; antes, Runtime/linea-aire.json
    // aparte): el paso de la corrida y los HOLD van en la MISMA versión que la escena, la
    // placa y las salidas de ese momento. Lo escribe el dueño del documento, no este hilo.
    rt_.documentoAire().ponerLinea(std::move(j));
}

void CorridaViewModel::borrarVivo() { rt_.documentoAire().ponerLinea(json()); }

void CorridaViewModel::retomarLinea() {
    // Lo leyó el runtime al arrancar, con el resto del documento (una sola lectura, de un
    // solo momento): lo que había al aire y en qué iba la línea.
    const json& j = rt_.aireAnterior().linea;
    if (rt_.lineaDesconocida()) {
        // El documento (o la línea de la versión anterior) volvió dañado: no se sabe en qué
        // iba la línea. Nunca seguir callados: la del evento abierto vuelve en HOLD en
        // todas las salidas, con la pregunta de siempre, sin nada repuesto (lo que estaba
        // al aire lo dice quien opera). La alarma ya la levantó el runtime.
        if (!ab_.hay()) return;
        const int64_t ahora = ahoraMs();
        retomarDesde(json{{"estado", "corriendo"}, {"evento", ab_.carpeta().toStdString()}, {"escritoMs", ahora}, {"arranqueMs", ahora}});
        return;
    }
    if (!j.is_object()) return;
    const std::string est = j.value("estado", std::string());
    if (est != "corriendo" && est != "pausada") {
        borrarVivo();
        return;
    }
    const QString ev = QString::fromStdString(j.value("evento", std::string()));
    if (ev.isEmpty() || (ev != ab_.carpeta() && (!abrir_ || !abrir_(ev)))) return;
    retomarDesde(j);
}

void CorridaViewModel::retomarDesde(const json& vivo) {
    ensayo_ = false;
    nuevaCorrida();
    const std::vector<Accion> acc = corrida_->retomar(vivo, ahoraMs());
    // El cursor, en el paso que estaba al aire (desde cuando estaba).
    if (const evento::linea::Instancia* a = corrida_->alAire(salidaPorOmision())) {
        const auto [b, c] = proy().cueDe(a->paso);
        moverCursor(b, c);
        cursor_.desdeMs = a->desdeMs;
    }
    anotar(QCoreApplication::translate("EventoViewModel", "Volvió la luz: la línea quedó en HOLD. Contá qué está pasando para seguir."), "error");
    emit notify(QCoreApplication::translate("EventoViewModel", "Volvió la luz en medio del evento: la línea quedó en HOLD. ¿Qué está pasando?"), "error");
    despachar(acc);
}

QString CorridaViewModel::pedidoDeRetoma(const QString& descripcion) const {
    const QString nombre = ab_.nombre();
    const QString resumen = corrida_ ? QString::fromStdString(corrida_->resumenRetoma().dump()) : QString("{}");
    return QCoreApplication::translate("EventoViewModel", "Volvió la luz en medio del evento «%1» y la línea de tiempo quedó en HOLD en todas las salidas (el aire "
              "volvió como estaba). La persona que opera cuenta qué está pasando: «%2». Mirá linea_estado (y linea_recorrido "
              "si hace falta) y PROPONÉ cómo adecuar la línea: dónde reenganchar, qué saltear, qué reponer y qué recortar, "
              "probándolo con linea_editar o linea_ajustar con probar:true. Mostrale la propuesta y esperá que la confirme; "
              "recién ahí aplicala y soltá el HOLD con linea_soltar (en `decision`, lo que se decidió). Si dice que siga como "
              "estaba, linea_soltar sin cambiar nada. El resumen de la retoma: %3")
        .arg(nombre, descripcion, resumen);
}

QString CorridaViewModel::describirRetoma(const QString& texto) {
    if (!corrida_ || !corrida_->retomando() || texto.trimmed().isEmpty()) return {};
    corrida_->describir(texto.trimmed().toStdString(), ahoraMs());
    anotar(QCoreApplication::translate("EventoViewModel", "Lo que está pasando: %1").arg(texto.trimmed()), "info");
    despachar({});
    return pedidoDeRetoma(texto.trimmed());
}

void CorridaViewModel::seguirComoEstaba() {
    if (!corrida_ || !corrida_->retomando()) return;
    anotar(QCoreApplication::translate("EventoViewModel", "Sigue como estaba: se suelta el HOLD"), "ok");
    despachar(corrida_->soltar(QCoreApplication::translate("EventoViewModel", "seguir como estaba").toStdString(), ahoraMs()));
}

std::filesystem::path CorridaViewModel::archivoRecorrido() const {
    if (!ab_.hay()) return {};
    return ab_.dir() / "recorrido.jsonl";
}

void CorridaViewModel::volcarRecorrido() {
    if (!corrida_) return;
    const std::vector<json> v = corrida_->sacarRecorrido();
    // Lo emitido de verdad: lo del ensayo no se anota.
    if (v.empty() || ensayo_ || !ab_.hay()) return;
    // Un diario: se suma al final por la cola del escritor único (fuera de la interfaz).
    std::string lineas;
    for (const json& x : v) lineas += x.dump(-1, ' ', false, json::error_handler_t::replace) + '\n';
    ba::persist::cola().agregar(archivoRecorrido(), std::move(lineas));
}

// --- La línea, para el MCP ---------------------------------------------------------

namespace {

json reparosJson(const std::vector<evento::linea::Reparo>& rs) {
    json out = json::array();
    for (const auto& r : rs)
        out.push_back({{"bloque", r.bloque}, {"paso", r.paso}, {"pieza", r.pieza}, {"grave", r.grave}, {"texto", r.texto}});
    return out;
}

/// El número de la última revisión en Respaldos/ (linea-<n>.json); 0 si no hay.
int revisionEnDisco(const std::filesystem::path& respaldos) {
    int n = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(respaldos, ec)) {
        const std::string f = e.path().filename().string();
        if (f.rfind("linea-", 0) != 0 || e.path().extension() != ".json") continue;
        try {
            n = std::max(n, std::stoi(f.substr(6)));
        } catch (...) {
        }
    }
    return n;
}

} // namespace

int CorridaViewModel::ultimaRevision() {
    // Una vez por evento se mira la carpeta; después se lleva la cuenta (lo encolado
    // todavía no está en el disco).
    if (revision_ < 0) revision_ = revisionEnDisco(ab_.dir() / "Respaldos");
    return revision_;
}

json CorridaViewModel::lineaEstado() const {
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    json j;
    if (corrida_) {
        j = corrida_->estadoJson(t, hora);
    } else {
        // Sin correr: lo que dice el plan desde ahora (o desde la hora prevista).
        const evento::linea::Corrida plan(linea(), entradaBase(false));
        j = plan.estadoJson(t, hora);
    }
    j["evento"] = ab_.carpeta().toStdString();
    j["ensayo"] = ensayo_;
    j["salidaPorOmision"] = salidaPorOmision();
    // Las PTZ que esperan que su cámara salga del aire: «mover igual» o «dejar para
    // cuando se libere» (lineaPtz).
    json ptz = json::array();
    for (const auto& [clave, x] : ptzPendientes_)
        ptz.push_back({{"pieza", x.pieza}, {"paso", x.paso}, {"salida", x.salida}, {"camara", x.camara}, {"posicion", x.posicion},
                       {"desde", horaDe(x.desdeMs)}});
    j["ptzPendientes"] = ptz;
    // Lo que propone el productor para llegar a horario (ui/LineaProductor lo deja en el
    // bus, linea/<salida>/propuesta): para la escaleta web y el asistente. Solo se muestra;
    // se aplica desde la conducción.
    if (control::BusEventos* bus = sistema().bus; bus != nullptr && j.contains("salidas"))
        for (json& x : j["salidas"]) {
            const json u = bus->ultimo("linea/" + x.value("salida", std::string()) + "/propuesta");
            if (u.is_object() && u.value("valor", json()).is_object()) x["propuesta"] = u["valor"];
        }
    return j;
}

json CorridaViewModel::lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const {
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    const std::string n = nivel.empty() ? "pasos" : nivel;
    if (corrida_) return evento::linea::vista(corrida_->linea(), corrida_->resolver(t), s, desdeMs, hastaMs, n, t, hora);
    evento::linea::Entrada e = entradaBase(false);
    e.ahoraMs = t;
    return evento::linea::vista(linea(), evento::linea::resolver(linea(), e), s, desdeMs, hastaMs, n, t, hora);
}

json CorridaViewModel::lineaEditar(const json& ops, bool probar) {
    if (!ab_.hay()) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString()}};
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    // En vivo, lo emitido (y lo que está al aire) no se toca.
    std::function<bool(const std::string&)> intocable;
    if (corriendo())
        intocable = [this](const std::string& paso) {
            const auto& is = corrida_->instancias();
            return std::any_of(is.begin(), is.end(), [&](const evento::linea::Instancia& x) { return x.paso == paso; });
        };
    evento::linea::Edicion ed = evento::linea::editar(linea(), ops, intocable);
    if (!ed.ok) return {{"ok", false}, {"error", ed.error}};
    // El pronóstico como quedaría: con lo emitido y lo insertado, si corre.
    const std::string s = salidaPorOmision();
    json pron;
    if (corrida_) {
        evento::linea::Corrida prueba = *corrida_;
        prueba.setLinea(ed.linea);
        pron = evento::linea::vista(prueba.linea(), prueba.resolver(t), s, 0, 0, "pasos", t, hora);
    } else {
        evento::linea::Entrada e = entradaBase(false);
        e.ahoraMs = t;
        pron = evento::linea::vista(ed.linea, evento::linea::resolver(ed.linea, e), s, 0, 0, "pasos", t, hora);
    }
    evento::linea::OpcionesRevision o;
    o.contexto = entradaBase(false);
    json out{{"ok", true},
             {"probar", probar},
             {"diff", ed.diff},
             {"pronostico", pron},
             {"reparos", reparosJson(evento::linea::revisar(ed.linea, o))}};
    if (probar) return out;
    // La revisión: la línea de antes, para poder deshacer.
    // (La escribe el hilo escritor, con la carpeta; se cuenta acá: la encolada vale.)
    const int n = ultimaRevision() + 1;
    ba::persist::cola().escribir(ab_.dir() / "Respaldos" / ("linea-" + std::to_string(n) + ".json"),
                                 linea().aJson().dump(2, ' ', false, json::error_handler_t::replace));
    revision_ = n;
    ab_.ponerLinea(std::move(ed.linea));
    guion_.guardar();
    anotar(QCoreApplication::translate("EventoViewModel", "La línea cambió (la de antes quedó como revisión %1)").arg(n), "info");
    out["revision"] = n;
    return out;
}

json CorridaViewModel::lineaDeshacer() {
    if (!ab_.hay()) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString()}};
    const int n = ultimaRevision();
    if (n == 0) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "No hay revisiones de la línea para deshacer.").toStdString()}};
    const auto archivo = ab_.dir() / "Respaldos" / ("linea-" + std::to_string(n) + ".json");
    std::string texto;
    const json j = ba::persist::cola().leer(archivo, texto) ? json::parse(texto, nullptr, false) : json();
    if (!j.is_object()) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "La revisión %1 no se puede leer.").arg(n).toStdString()}};
    ba::persist::cola().borrar(archivo);
    revision_ = n - 1;
    ab_.ponerLinea(evento::linea::Linea::deJson(j));
    guion_.guardar();
    anotar(QCoreApplication::translate("EventoViewModel", "La línea volvió a la revisión %1").arg(n), "info");
    return {{"ok", true}, {"revision", n}};
}

json CorridaViewModel::lineaRevisar() const {
    evento::linea::OpcionesRevision o;
    o.contexto = entradaBase(false);
    const std::vector<std::string> escenas = ventanilla_.foto().nombresDeEscenas();
    o.existeEscena = [&escenas](const std::string& e) { return std::find(escenas.begin(), escenas.end(), e) != escenas.end(); };
    const auto rs = evento::linea::revisar(linea(), o);
    const int graves = static_cast<int>(std::count_if(rs.begin(), rs.end(), [](const auto& r) { return r.grave; }));
    return {{"reparos", reparosJson(rs)}, {"graves", graves}, {"sePuedeCorrer", graves == 0}};
}

json CorridaViewModel::lineaAjustar(const std::string& salida, int64_t objetivoMs) const {
    const int64_t t = ahoraMs();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    json a;
    if (corrida_) {
        a = evento::linea::proponerAjuste(corrida_->linea(), corrida_->resolver(t), s, objetivoMs);
    } else {
        evento::linea::Entrada e = entradaBase(false);
        e.ahoraMs = t;
        a = evento::linea::proponerAjuste(linea(), evento::linea::resolver(linea(), e), s, objetivoMs);
    }
    a["objetivo"] = horaDe(objetivoMs);
    return a;
}

namespace {
/// "" si la salida existe en la línea; si no, por qué no.
std::string errorDeSalida(const evento::linea::Linea& l, const std::string& s) {
    const auto ss = l.salidasEfectivas();
    if (std::find(ss.begin(), ss.end(), s) != ss.end()) return {};
    std::string hay;
    for (const auto& x : ss)
        hay += (hay.empty() ? "" : ", ") + x;
    return ba::i18n::llenar(ba::i18n::traducir("evento", "No hay una salida «%1» en la línea (hay: %2)."), {s, hay});
}
} // namespace

std::string CorridaViewModel::lineaGo(const std::string& salida) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    if (!corriendo()) {
        // Arrancar es con la revisión previa, venga de donde venga (la conducción, la vista
        // de la línea o el MCP): con problemas graves no sale.
        if (const int graves = arrancarRevisando(false); graves > 0)
            return QCoreApplication::translate("EventoViewModel", "La revisión encontró %n problema(s) grave(s): mirá evento_revisar y corregilo, o "
                                       "arrancá igual con evento_correr {accion: arrancar, forzar: true}.",
                                       "", graves)
                .toStdString();
        return corriendo() ? std::string() : QCoreApplication::translate("EventoViewModel", "No se pudo arrancar: la línea no tiene pasos.").toStdString();
    }
    // La pieza que sigue en el guion de cues (el cursor es el de la salida por omisión): si
    // es del paso al aire y no salió, el GO la dispara (evento::linea::Corrida::goDelGuion).
    std::string pasoSig, piezaSig;
    if (s == salidaPorOmision()) {
        const auto [nb, nc] = cueSiguienteDe(cursor_.bloque, cursor_.cue);
        if (const auto* r = proy().ref(nb, nc)) pasoSig = r->paso, piezaSig = r->pieza;
    }
    despachar(corrida_->goDelGuion(s, pasoSig, piezaSig, ahoraMs()));
    return {};
}

std::string CorridaViewModel::lineaHold(const std::string& salida, std::optional<bool> on) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo: no hay paso que frenar.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    const bool v = on ? *on : !corrida_->hold(s);
    // Tras un corte, soltar el HOLD es seguir como estaba.
    if (!v && corrida_->retomando()) {
        seguirComoEstaba();
        return {};
    }
    if (corrida_->hold(s) == v) return {};
    if (!corrida_->setHold(s, v, ahoraMs())) return QCoreApplication::translate("EventoViewModel", "No hay ningún paso al aire en esa salida.").toStdString();
    anotar(v ? QCoreApplication::translate("EventoViewModel", "HOLD: este paso no se va solo") : QCoreApplication::translate("EventoViewModel", "HOLD suelto: el paso vuelve a irse solo"), "info");
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string CorridaViewModel::lineaMoverProximo(const std::string& salida, int delta) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    if (!corrida_->moverProximo(s, delta, ahoraMs())) return QCoreApplication::translate("EventoViewModel", "No hay a dónde mover el próximo.").toStdString();
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string CorridaViewModel::lineaSaltar(const std::string& salida) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    if (!corrida_->saltar(s, ahoraMs())) return QCoreApplication::translate("EventoViewModel", "No hay próximo para saltear.").toStdString();
    anotar(QCoreApplication::translate("EventoViewModel", "Saltea el próximo paso"), "info");
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string CorridaViewModel::lineaIr(const std::string& salida, const std::string& paso) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    if (!corriendo()) {
        if (!ensayo_ && ponerEnCurso_) ponerEnCurso_();
        nuevaCorrida();
    }
    std::string err;
    const std::vector<Accion> acc = corrida_->irA(s, paso, ahoraMs(), &err);
    if (err.empty()) anotar(QCoreApplication::translate("EventoViewModel", "Va al paso «%1»").arg(QString::fromStdString(paso)), "info");
    despachar(acc);
    return err;
}

std::string CorridaViewModel::lineaRepetir(const std::string& salida) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    if (corrida_->alAire(s) == nullptr) return QCoreApplication::translate("EventoViewModel", "No hay ningún paso al aire en esa salida.").toStdString();
    anotar(QCoreApplication::translate("EventoViewModel", "Repite el paso al aire"), "info");
    despachar(corrida_->repetir(s, ahoraMs()));
    return {};
}

std::string CorridaViewModel::lineaAdlib(const std::string& salida, const std::string& pieza) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    // Un paso del estante no se «dispara»: va de próximo.
    for (const evento::linea::ItemEstante& it : corrida_->linea().estante)
        if (it.id == pieza && it.paso) return lineaInsertar(s, pieza, false);
    std::string err;
    const std::vector<Accion> acc = corrida_->adlib(s, pieza, ahoraMs(), &err);
    despachar(acc);
    return err;
}

std::string CorridaViewModel::lineaInsertar(const std::string& salida, const std::string& item, bool alAire) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->insertar(s, item, alAire, ahoraMs(), &err);
    if (err.empty()) anotar(QCoreApplication::translate("EventoViewModel", "Inserta «%1» del estante").arg(QString::fromStdString(item)), "info");
    despachar(acc);
    return err;
}

json CorridaViewModel::lineaRecorrido(int ultimos) const {
    json v = json::array();
    std::string texto; // con lo encolado para sumar (volcarRecorrido)
    ba::persist::cola().leer(archivoRecorrido(), texto);
    std::istringstream f(texto);
    for (std::string l; std::getline(f, l);) {
        json x = json::parse(l, nullptr, false);
        if (!x.is_object()) continue;
        if (x.contains("ms") && x["ms"].is_number_integer()) x["hora"] = horaDe(x["ms"].get<int64_t>());
        v.push_back(std::move(x));
    }
    if (ultimos > 0 && static_cast<int>(v.size()) > ultimos) v.erase(v.begin(), v.end() - ultimos);
    return {{"renglones", v}, {"ensayo", ensayo_}};
}

std::string CorridaViewModel::lineaRetomar(const std::string& descripcion, bool simular) {
    if (simular) {
        if (!corriendo() || ensayo_)
            return QCoreApplication::translate("EventoViewModel", "Para simular un corte la línea tiene que estar corriendo, sin ensayo.").toStdString();
        // Como si se hubiera cortado la luz recién: el estado vivo de ahora, retomado.
        retomarDesde(corrida_->aJsonVivo(ahoraMs()));
    }
    if (!corrida_ || !corrida_->retomando())
        return QCoreApplication::translate("EventoViewModel", "La línea no está volviendo de un corte: no hay nada que describir.").toStdString();
    if (!descripcion.empty()) describirRetoma(QString::fromStdString(descripcion));
    return {};
}

std::string CorridaViewModel::lineaSoltar(const std::string& decision) {
    if (!corrida_ || !corrida_->retomando()) return QCoreApplication::translate("EventoViewModel", "La línea no está en HOLD por un corte.").toStdString();
    const std::string d = decision.empty() ? QCoreApplication::translate("EventoViewModel", "seguir como estaba").toStdString() : decision;
    anotar(QCoreApplication::translate("EventoViewModel", "Se suelta el HOLD del corte: %1").arg(QString::fromStdString(d)), "ok");
    despachar(corrida_->soltar(d, ahoraMs()));
    return {};
}

std::string CorridaViewModel::prepararEscenaDeVideo(AppRuntime& rt, const std::string& escenaV, const std::string& fuenteV,
                                                  bool* creada) {
    // En el dueño del motor (lo pide el cue de video). Una por salida (la por omisión, la
    // de siempre): cada salida pasa su video.
    const auto escenas = rt.engine().sceneNames();
    const bool existe = std::find(escenas.begin(), escenas.end(), escenaV) != escenas.end();
    if (!existe) {
        if (!rt.addScene(escenaV))
            return QCoreApplication::translate("EventoViewModel", "No se pudo crear la escena del video: %1").arg(QString::fromStdString(rt.lastError())).toStdString();
        // Protegida: si alguien la borra, todos los cues de video del guion
        // dejan de funcionar y no es obvio por qué.
        rt.setSceneProtected(escenaV, true);
        if (creada != nullptr) *creada = true;
    }
    // La fuente de video: se crea una vez y después solo se le cambia el
    // archivo. Ocupa el cuadro entero, que es lo que uno espera de un video.
    const auto fuentes = rt.engine().sceneSources(escenaV);
    if (std::find(fuentes.begin(), fuentes.end(), fuenteV) == fuentes.end()) {
        SourceSpec spec;
        spec.id = inputs::kMedioType;
        spec.name = fuenteV;
        spec.rect = CanvasRect{0.0, 0.0, 1.0, 1.0};
        spec.settingsJson = R"({"clear_on_media_end":true})";
        if (!rt.addSource(escenaV, spec))
            return QCoreApplication::translate("EventoViewModel", "No se pudo crear la fuente de video: %1").arg(QString::fromStdString(rt.lastError())).toStdString();
    }
    return {};
}

void CorridaViewModel::sacarAlAvanzar(const std::string& salida) {
    if (graficos_ == nullptr) return;
    for (auto it = alAvanzar_.begin(); it != alAvanzar_.end();) {
        if (!salida.empty() && it->first != salida) {
            ++it;
            continue;
        }
        for (const auto& [fuente, gen] : it->second) graficos_->sacar(fuente, gen);
        it = alAvanzar_.erase(it);
    }
}

std::optional<std::string> CorridaViewModel::salidaDelMotor(const std::string& salida) const {
    // Los ids de la línea son los de las salidas del motor; la por omisión de la línea,
    // si el motor no tiene una con ese id, es la por omisión del motor ("").
    if (ventanilla_.foto().salida(salida) != nullptr) return salida;
    if (salida == salidaPorOmision()) return std::string();
    return std::nullopt;
}

const FotoMotor::Salida* CorridaViewModel::aireDe(const std::string& salida) const {
    const std::optional<std::string> id = salidaDelMotor(salida);
    return id ? ventanilla_.foto().salida(*id) : nullptr;
}

void CorridaViewModel::pedirAlConmutador(const char* que, const std::string& salida,
                                        std::function<std::string(escenas::Conmutador&)> orden,
                                        std::function<void(const std::string& error)> listo) {
    const std::optional<std::string> id = salidaDelMotor(salida);
    escenas::Conmutadores* cs = sistema().conmutadores;
    if (!id || cs == nullptr) {
        if (listo) listo(QCoreApplication::translate("EventoViewModel", "la salida «%1» no tiene conmutador de escenas").arg(QString::fromStdString(salida)).toStdString());
        return;
    }
    // El conmutador es del dueño del motor: la orden corre allá (en orden, sin esperar).
    ventanilla_.pedir(
        this, que,
        [cs, id = *id, salida, orden = std::move(orden)]() -> std::string {
            escenas::Conmutador* c = id.empty() ? &cs->porOmision() : cs->de(id);
            if (c == nullptr)
                return QCoreApplication::translate("EventoViewModel", "la salida «%1» no tiene conmutador de escenas").arg(QString::fromStdString(salida)).toStdString();
            return orden(*c);
        },
        [listo = std::move(listo)](const std::string& e) {
            if (listo) listo(e);
        });
}

void CorridaViewModel::tomarEscena(const std::string& escena, const nlohmann::json& datos, const std::string& salida,
                                  std::function<void(const std::string& error)> listo) {
    if (escena.empty()) return listo(QCoreApplication::translate("EventoViewModel", "el cue no dice qué escena poner").toStdString());
    // Ya al aire (la puso el operador a mano, o la repite el cue): no hay nada que tomar.
    const FotoMotor::Salida* s = aireDe(salida);
    if (s != nullptr && s->alAire == escena) return listo({});
    // Lo cambia la línea: no es «a mano» (el aviso del conmutador llega después).
    escenasDeLaLinea_[salida].insert(escena);
    const std::string tipo = datos.value("transicion", std::string());
    const int ms = datos.value("ms", -1);
    pedirAlConmutador(
        "linea_tomar", salida,
        [escena, tipo, ms](escenas::Conmutador& c) {
            if (c.alAire() == escena) return std::string();
            std::string e;
            if (c.armada() != escena) e = c.armar(escena);
            // La transición del cue si dice; si no, la de entrada de la escena (o la global).
            if (e.empty()) e = c.take(tipo, ms);
            return e;
        },
        [this, salida, escena, listo = std::move(listo)](const std::string& e) {
            if (!e.empty()) olvidarEscenaDeLaLinea(salida, escena);
            listo(e);
        });
}

void CorridaViewModel::olvidarEscenaDeLaLinea(const std::string& salida, const std::string& escena) {
    auto it = escenasDeLaLinea_.find(salida);
    if (it == escenasDeLaLinea_.end()) return;
    if (auto x = it->second.find(escena); x != it->second.end()) it->second.erase(x);
}

void CorridaViewModel::armarEscena(const std::string& escena, const std::string& salida) {
    if (ensayo_ || escena.empty()) return;
    // La que ya está al aire no se arma (el TAKE no tendría sentido) y la ya armada, tampoco.
    if (const FotoMotor::Salida* s = aireDe(salida); s != nullptr && (escena == s->alAire || escena == s->armada)) return;
    pedirAlConmutador(
        "linea_armar", salida,
        [escena](escenas::Conmutador& c) {
            if (escena == c.alAire() || escena == c.armada()) return std::string();
            return c.armar(escena);
        },
        [this, escena, salida](const std::string& e) {
            if (!e.empty()) {
                anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "No se pudo armar «%1» en el PVW: %2").arg(QString::fromStdString(escena), QString::fromStdString(e))),
                       "error");
                return;
            }
            anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "Armada en el PVW: %1").arg(QString::fromStdString(escena))), "info");
        });
}

evento::Cue CorridaViewModel::cueDePieza(const evento::linea::Pieza& z, const std::string& paso) const {
    evento::Cue c;
    c.id = z.id;
    if (!evento::tipoDeNombre(z.tipo, c.tipo)) c.tipo = evento::Tipo::Nota;
    c.texto = z.texto;
    c.datos = z.datos;
    // La duración es la del guion de cues (la de un orador es su tiempo para hablar);
    // si la pieza no está ahí (del estante, insertada), la de su paso.
    const auto [b, k] = proy().cueDe(paso, z.id);
    if (const evento::Cue* v = cue(b, k)) c.duracionSeg = v->duracionSeg;
    else if (const evento::linea::Paso* p = corrida_ ? corrida_->linea().paso(paso) : nullptr)
        c.duracionSeg = static_cast<int>(p->duracionPlanMs() / 1000);
    return c;
}

void CorridaViewModel::hacerAccion(const Accion& a) {
    if (!corrida_) return;
    // Cada salida sale de verdad (L6): su conmutador, su mezcla, sus capas. El cursor
    // del guion de cues sigue a la por omisión (es su lectura).
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const bool principal = salida == salidaPorOmision();
    switch (a.que) {
        case Accion::Que::EntraPaso: {
            if (!ensayo_) sacarAlAvanzar(salida);
            if (principal) {
                const auto [b, c] = proy().cueDe(a.paso);
                moverCursor(b, c);
            }
            const evento::linea::Paso* p = corrida_->linea().paso(a.paso);
            const QString nombre = QString::fromStdString(p && !p->nombre.empty() ? p->nombre : a.paso);
            anotar(enSalida(salida, ensayo_ ? QCoreApplication::translate("EventoViewModel", "(ensayo) Entra «%1»").arg(nombre) : QCoreApplication::translate("EventoViewModel", "Entra «%1»").arg(nombre)), "info",
                   !ensayo_);
            return;
        }
        case Accion::Que::Pieza: {
            const evento::linea::Pieza* z = corrida_->pieza(a.paso, a.pieza);
            if (z == nullptr) return;
            // El cursor del guion de cues sigue a lo que se dispara en el paso al aire.
            if (principal && !a.reposicion && !a.paso.empty()) {
                const auto [b, c] = proy().cueDe(a.paso, a.pieza);
                const auto* r = proy().ref(cursor_.bloque, cursor_.cue);
                if (b >= 0 && r != nullptr && r->paso == a.paso && (b > cursor_.bloque || (b == cursor_.bloque && c > cursor_.cue)))
                    moverCursor(b, c);
            }
            const QString titulo = QString::fromStdString(z->texto.empty() ? z->tipo : z->texto);
            if (ensayo_) {
                // El ensayo recorre la línea igual pero no toca nada: es para ver
                // tiempos y orden antes del evento.
                anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "(ensayo) %1").arg(titulo)), "info", false);
                return;
            }
            if (a.reposicion) anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "Repone: %1").arg(titulo)), "info");
            // Una acción del asistente programada en la línea: se aprobó al escribirla y
            // no frena el vivo (se hace por el mismo servidor de herramientas).
            if (z->tipo == "asistente") {
                const std::string herramienta = z->datos.value("herramienta", std::string());
                if (!herramienta_ || herramienta.empty()) {
                    anotar(QCoreApplication::translate("EventoViewModel", "No se pudo hacer «%1»: el servidor de herramientas no está").arg(titulo), "error");
                    return;
                }
                QPointer<CorridaViewModel> yo(this);
                herramienta_(herramienta, z->datos.value("args", json::object()), [yo, titulo](bool ok, const std::string& texto) {
                    if (!yo) return;
                    yo->anotar(ok ? QCoreApplication::translate("EventoViewModel", "Hecho: %1").arg(titulo)
                                  : QCoreApplication::translate("EventoViewModel", "No se pudo hacer «%1»: %2").arg(titulo, QString::fromStdString(texto)),
                               ok ? "ok" : "error");
                });
                return;
            }
            if (ejecutarPiezaLinea(*z, a, titulo)) return;
            evento::Tipo t{};
            if (!evento::tipoDeNombre(z->tipo, t)) {
                // Honestidad: el tipo existe en la línea pero todavía no se ejecuta.
                anotar(QCoreApplication::translate("EventoViewModel", "Todavía no sé ejecutar una pieza de tipo «%1»: %2").arg(QString::fromStdString(z->tipo), titulo), "error");
                return;
            }
            const evento::linea::Pista* pi = corrida_->linea().pista(z->pista);
            ejecutarCue(cueDePieza(*z, a.paso), a.paso, salida + "/" + a.paso + "/" + a.pieza, salida, pi == nullptr || pi->salida.empty());
            return;
        }
        case Accion::Que::Saca:
            if (!ensayo_) sacarPieza(a);
            return;
        case Accion::Que::Armar:
            armarEscena(a.escena, salida);
            return;
        case Accion::Que::Avisar:
            anotar(enSalida(salida, QString::fromStdString(a.texto)), "info");
            emit notify(QString::fromStdString(a.texto), "info");
            return;
        case Accion::Que::Termina:
            anotar(enSalida(salida, ensayo_ ? QCoreApplication::translate("EventoViewModel", "Terminó el ensayo") : QCoreApplication::translate("EventoViewModel", "Terminó el evento")), "ok");
            return;
    }
}

void CorridaViewModel::sacarPieza(const Accion& a) {
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const std::string clave = salida + "/" + a.paso + "/" + a.pieza;
    if (const auto it = graficosDePieza_.find(clave); it != graficosDePieza_.end()) {
        // Con su generación: si después se puso otra cosa en esa fuente, no se toca.
        if (graficos_ != nullptr) graficos_->sacar(it->second.first, it->second.second);
        graficosDePieza_.erase(it);
    }
    if (const auto it = musicaDePieza_.find(salida); it != musicaDePieza_.end() && it->second == clave) {
        ventanilla_.pedir("linea_musica", [&rt = rt_, m = musicaDe(salida)]() { rt.engine().stopMedia(m); });
        musicaDePieza_.erase(it);
        anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "Música: para")), "ok");
    }
    // La capa que subió la pieza baja con ella (una fija, como la de LSA, no: pide confirmación).
    if (const auto it = dskDePieza_.find(clave); it != dskDePieza_.end()) {
        if (control::Dsk* d = sistema().dsk) {
            const std::string e = d->mostrar(it->second.first, false, false, it->second.second);
            if (!e.empty()) anotar(enSalida(salida, QString::fromStdString(e)), "info");
        }
        dskDePieza_.erase(it);
    }
    // Una PTZ que esperaba y ya no va: no se mueve más.
    ptzPendientes_.erase(clave);
    mediosVigilados_.erase(a.pieza);
    if (playlistDePieza_ == a.pieza) playlistDePieza_.clear();
}

void CorridaViewModel::ejecutarCue(const evento::Cue& cue, const std::string& paso, const std::string& clave, const std::string& salida,
                                  bool global) {
    const evento::Cue* c = &cue;
    const QString titulo = c->texto.empty() ? QString::fromStdString(evento::nombreDeTipo(c->tipo))
                                            : QString::fromStdString(c->texto);

    // Todo por el runtime y los conmutadores, pedido al dueño del motor: el runtime avisa
    // (la tarjeta Dirección, el mezclador, el tally) y lo deja guardado en la configuración.
    switch (c->tipo) {
        case evento::Tipo::Escena: {
            const std::string escena = c->datos.value("escena", std::string());
            // Sin transición propia, la del paso (si dice).
            json datos = c->datos;
            const evento::linea::Paso* p = corrida_ ? corrida_->linea().paso(paso) : nullptr;
            if (!datos.contains("transicion") && p != nullptr && p->transicion.is_object()) {
                datos["transicion"] = p->transicion.value("tipo", std::string());
                datos["ms"] = p->transicion.value("ms", -1);
            }
            tomarEscena(escena, datos, salida, [this, escena, salida](const std::string& err) {
                if (!err.empty()) {
                    anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "No se pudo poner la escena «%1»: %2").arg(QString::fromStdString(escena), QString::fromStdString(err))),
                           "error");
                    emit notify(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: %1").arg(QString::fromStdString(err)), "error");
                    return;
                }
                anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "Al aire: %1").arg(QString::fromStdString(escena))), "ok");
            });
            return;
        }
        case evento::Tipo::Orador: {
            const QString quien = QString::fromStdString(c->datos.value("participante", std::string()));
            if (sala_ == nullptr || quien.isEmpty()) {
                anotar(QCoreApplication::translate("EventoViewModel", "No se pudo dar la palabra: no dice a quién"), "error");
                return;
            }
            // Las mismas operaciones que la página Sala y el MCP (sala::OradoresSala).
            if (c->duracionSeg > 0) sala_->oradores().tiempo(quien.toStdString(), c->duracionSeg);
            sala_->oradores().alAire(quien.toStdString(), true);
            anotar(QCoreApplication::translate("EventoViewModel", "Habla %1").arg(quien), "ok");
            return;
        }
        case evento::Tipo::Musica: {
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            // El nombre es fijo (kMusica): asi un tema nuevo reemplaza al anterior
            // en vez de encimarse, que es lo peor que puede pasar en vivo.
            const std::string medio = musicaDe(salida);
            if (accion == "parar") {
                ventanilla_.pedir("linea_musica", [&rt = rt_, medio]() { rt.engine().stopMedia(medio); });
                musicaDePieza_.erase(salida);
                anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "Música: para")), "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = guion_.medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                anotar(QCoreApplication::translate("EventoViewModel", "No encontré la música \"%1\" en los medios del evento").arg(rel), "error");
                emit notify(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: falta «%1» en los medios del evento").arg(rel), "error");
                return;
            }
            const bool repetir = c->datos.value("repetir", false);
            // Con varias salidas, la música de cada una suena solo en su mezcla (las salidas
            // del motor, de su foto). Todo en el dueño, en una orden.
            std::vector<std::string> mezclas;
            if (corrida_ && corrida_->linea().salidasEfectivas().size() > 1)
                for (const FotoMotor::Salida& x : ventanilla_.foto().salidas) mezclas.push_back(x.id);
            const FotoMotor::Salida* propia = aireDe(salida);
            ventanilla_.pedir(
                this, "linea_musica",
                [&rt = rt_, medio, r = ruta.toStdString(), repetir, mezclas, propia = propia ? propia->id : salida]() -> std::string {
                    if (!rt.engine().playMedia(medio, r, repetir)) return rt.engine().lastError();
                    for (const std::string& m : mezclas) rt.setMuted(medio, m != propia, m);
                    return {};
                },
                [this, rel](const std::string& e) {
                    if (!e.empty()) anotar(QCoreApplication::translate("EventoViewModel", "No se pudo reproducir %1: %2").arg(rel, QString::fromStdString(e)), "error");
                });
            musicaDePieza_[salida] = clave; // la saca su pieza cuando se termina su vida (si la tiene)
            const std::string pieza = clave.substr(clave.rfind('/') + 1);
            if (!repetir) mediosVigilados_[pieza] = medio; // para «medio:<pieza>.termino»
            anotar(enSalida(salida, repetir ? QCoreApplication::translate("EventoViewModel", "Música: %1 (en bucle)").arg(rel) : QCoreApplication::translate("EventoViewModel", "Música: %1").arg(rel)), "ok");
            return;
        }
        case evento::Tipo::Teleprompter: {
            // El texto vive en Discursos/ de la carpeta del evento, no pegado
            // en la tablet: asi viaja con el evento y el asistente puede
            // ayudar a escribirlo o acortarlo.
            const QString rel = QString::fromStdString(c->datos.value("discurso", std::string()));
            if (sala_ == nullptr || rel.isEmpty()) {
                anotar(QCoreApplication::translate("EventoViewModel", "No se pudo cargar el discurso: no dice cuál"), "error");
                return;
            }
            // La misma carga que la página Teleprompter (sala::Teleprompter): el
            // discurso de Discursos/ con su ruta de título. `arrancar` en los
            // datos del cue: por defecto el texto queda LISTO pero parado, que
            // es lo que se quiere casi siempre (el orador todavia esta
            // caminando hacia el atril).
            // En cuál (cada uno con su texto y su página); sin decir, el principal.
            const std::string cual = c->datos.value("prompter", std::string());
            if (!cual.empty() && !guion_.listaPrompters().existe(cual)) {
                anotar(QCoreApplication::translate("EventoViewModel", "No hay un teleprompter «%1» en el evento").arg(QString::fromStdString(cual)), "error");
                return;
            }
            // El texto, de la caché del guion (Discursos/ se lee en el fondo al abrir y cuando
            // cambia): en vivo no se va al disco.
            bool ok = false;
            const QString texto = guion_.textoDeDiscurso(rel, &ok);
            if (ok) {
                auto tp = sala_->teleprompter().para(cual);
                tp.cargarTexto(rel.toStdString(), texto.toStdString());
                if (c->datos.value("arrancar", false)) tp.arrancar(true);
            }
            if (!ok) {
                anotar(QCoreApplication::translate("EventoViewModel", "No se pudo abrir el discurso: %1").arg(rel), "error");
                emit notify(QCoreApplication::translate("EventoViewModel", "No encontré el discurso \"%1\" en la carpeta del evento").arg(rel), "error");
                return;
            }
            anotar(QCoreApplication::translate("EventoViewModel", "Teleprompter: %1").arg(rel), "ok");
            return;
        }
        case evento::Tipo::Grafico: {
            // El gráfico sale en una FUENTE de escena, que el guion nombra. Si
            // esa fuente no está en ninguna escena, el gráfico no lo ve nadie:
            // se dice, en vez de dar el cue por hecho.
            const std::string accion = c->datos.value("accion", std::string("mostrar"));
            // Prender o apagar un rotulo que se mantiene solo: «durante las
            // entrevistas, zocalo automatico; en el cierre, apagado».
            if (accion == "prender" || accion == "apagar") {
                if (vinculos_ == nullptr) {
                    anotar(QCoreApplication::translate("EventoViewModel", "No hay motor de gráficos para los vínculos"), "error");
                    return;
                }
                const QString id = QString::fromStdString(c->datos.value("vinculo", std::string()));
                if (!vinculos_->activar(id, accion == "prender")) {
                    anotar(QCoreApplication::translate("EventoViewModel", "No existe el vínculo \"%1\"").arg(id), "error");
                    emit notify(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: no existe el vínculo \"%1\"").arg(id), "error");
                    return;
                }
                anotar(
                    (accion == "prender" ? QCoreApplication::translate("EventoViewModel", "Prende el rótulo automático %1") : QCoreApplication::translate("EventoViewModel", "Apaga el rótulo automático %1")).arg(id),
                    "ok");
                return;
            }
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (graficos_ == nullptr) {
                anotar(QCoreApplication::translate("EventoViewModel", "No hay renderizador de gráficos en este bundle"), "error");
                return;
            }
            const std::uint64_t id = graficos_->fuentePorNombre(fuente);
            if (id == 0) {
                anotar(QCoreApplication::translate("EventoViewModel", "No hay ninguna fuente de gráfico llamada \"%1\" en las escenas").arg(fuente), "error");
                emit notify(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: falta la fuente de gráfico \"%1\"").arg(fuente),
                            "error");
                return;
            }
            if (accion == "ocultar") {
                const QString err = graficos_->ocultar(id);
                if (!err.isEmpty()) {
                    anotar(QCoreApplication::translate("EventoViewModel", "No se pudo sacar el gráfico: %1").arg(err), "error");
                    return;
                }
                anotar(QCoreApplication::translate("EventoViewModel", "Saca el gráfico de %1").arg(fuente), "ok");
                return;
            }
            const QString grafico = QString::fromStdString(c->datos.value("grafico", std::string()));
            QJsonObject valores;
            if (c->datos.contains("valores"))
                valores =
                    QJsonDocument::fromJson(QByteArray::fromStdString(c->datos["valores"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
            const int dur = c->datos.value("duracionMs", 0);
            const bool repetir = c->datos.value("repetir", false);
            // CUÁNDO SE VA: lo que diga la cue (seVa); si no dice, «sola» si trae un
            // «cuándo se va» (salidaEn), y si no, lo que diga la ficha del gráfico (los
            // títulos y placas: al avanzar), o queda.
            std::string seVa = c->datos.value("seVa", std::string());
            if (seVa.empty())
                seVa = valores.value("salidaEn").toInt(0) > 0
                           ? "sola"
                           : graficos_->catalogo().seVaDe(grafico).toStdString();
            if (seVa != "sola") valores.remove("salidaEn"); // al avanzar o queda: no se va solo
            const QString err = graficos_->mostrar(grafico, valores, id, dur, repetir);
            if (!err.isEmpty()) {
                anotar(QCoreApplication::translate("EventoViewModel", "No se pudo sacar al aire el gráfico \"%1\": %2").arg(grafico, err), "error");
                emit notify(QCoreApplication::translate("EventoViewModel", "El cue de gráfico falló: %1").arg(err), "error");
                return;
            }
            // La fuente es una sola: si otra salida la tenía para «al avanzar», ahora es de esta.
            for (auto& [s, lista] : alAvanzar_)
                lista.erase(std::remove_if(lista.begin(), lista.end(), [id](const auto& a) { return a.first == id; }), lista.end());
            if (seVa == "al-avanzar") alAvanzar_[salida].emplace_back(id, graficos_->generacion(id));
            graficosDePieza_[clave] = {id, graficos_->generacion(id)};
            anotar((seVa == "al-avanzar" ? QCoreApplication::translate("EventoViewModel", "Al aire el gráfico %1 en %2 (se va al avanzar)")
                    : seVa == "sola"       ? QCoreApplication::translate("EventoViewModel", "Al aire el gráfico %1 en %2 (se va solo)")
                                           : QCoreApplication::translate("EventoViewModel", "Al aire el gráfico %1 en %2"))
                       .arg(grafico, fuente),
                   "ok");
            return;
        }
        case evento::Tipo::Carrusel: {
            // Fija carrusel y lámina (no «la siguiente»): si en el medio se movió a mano desde
            // la tarjeta, el guion lo vuelve a donde dice. Las mismas operaciones que la tarjeta.
            const QString nombre = QString::fromStdString(c->datos.value("carrusel", std::string()));
            const int lamina = evento::numeroDeLamina(c->datos);
            if (carruseles_ == nullptr) {
                anotar(QCoreApplication::translate("EventoViewModel", "No hay carruseles en este programa"), "error");
                return;
            }
            const QString err = errorDe([&] { carruseles_->mostrar(nombre, lamina); });
            if (!err.isEmpty()) {
                anotar(QCoreApplication::translate("EventoViewModel", "No se pudo poner el carrusel: %1").arg(err), "error");
                emit notify(QCoreApplication::translate("EventoViewModel", "El cue de carrusel falló: %1").arg(err), "error");
                return;
            }
            anotar(QCoreApplication::translate("EventoViewModel", "Carrusel «%1», lámina %2").arg(nombre).arg(lamina), "ok");
            return;
        }
        case evento::Tipo::Espera: anotar(QCoreApplication::translate("EventoViewModel", "Espera tu confirmación: %1").arg(titulo), "info"); return;
        case evento::Tipo::Nota:
        case evento::Tipo::Pausa: anotar(titulo, "info"); return;
        case evento::Tipo::Audio: {
            const std::string accion = c->datos.value("accion", std::string());
            const QString fuente = QString::fromStdString(c->datos.value("fuente", std::string()));
            if (fuente.isEmpty()) {
                anotar(QCoreApplication::translate("EventoViewModel", "El cue de audio no dice sobre que fuente actuar"), "error");
                return;
            }
            const std::string f = fuente.toStdString();
            // En qué mezcla: la de su salida; desde una pista global, en todas (así una
            // salida no le apaga el micrófono a otra: eso solo lo hace una pista global).
            std::vector<std::string> mezclas;
            if (const FotoMotor::Salida* propia = aireDe(salida)) mezclas.push_back(propia->id);
            else mezclas.push_back(salida);
            if (global) {
                mezclas.clear();
                for (const FotoMotor::Salida& x : ventanilla_.foto().salidas) mezclas.push_back(x.id);
            }
            // En el dueño (la mezcla es del motor): true si se pudo en todas.
            const auto enTodas = [this, mezclas](const char* que, std::function<bool(AppRuntime&, const std::string&)> hacer,
                                                 std::function<void(bool)> listo) {
                ventanilla_.pedir(
                    this, que,
                    [&rt = rt_, mezclas, hacer = std::move(hacer)]() {
                        bool ok = !mezclas.empty();
                        for (const std::string& m : mezclas)
                            ok = hacer(rt, m) && ok;
                        return ok;
                    },
                    std::move(listo));
            };
            if (accion == "silenciar" || accion == "activar") {
                const bool mudo = accion == "silenciar";
                enTodas("linea_audio", [f, mudo](AppRuntime& rt, const std::string& m) { return rt.setMuted(f, mudo, m); },
                        [this, fuente, mudo](bool ok) {
                            if (!ok) {
                                anotar(QCoreApplication::translate("EventoViewModel", "No existe la fuente de audio \"%1\"").arg(fuente), "error");
                                emit notify(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: no existe la fuente \"%1\"").arg(fuente), "error");
                                return;
                            }
                            anotar((mudo ? QCoreApplication::translate("EventoViewModel", "Silencia %1") : QCoreApplication::translate("EventoViewModel", "Activa %1")).arg(fuente), "ok");
                        });
                return;
            }
            if (accion == "volumen") {
                // En dB, que es como piensa el operador. 0 = sin cambio.
                const double db = c->datos.value("db", 0.0);
                const float mul = static_cast<float>(std::pow(10.0, db / 20.0));
                enTodas("linea_audio", [f, mul](AppRuntime& rt, const std::string& m) { return rt.setVolume(f, mul, m); },
                        [this, fuente, db](bool ok) {
                            if (!ok) return anotar(QCoreApplication::translate("EventoViewModel", "No existe la fuente de audio \"%1\"").arg(fuente), "error");
                            anotar(fuente + " a " + QString::number(db, 'f', 1) + " dB", "ok");
                        });
                return;
            }
            anotar(QCoreApplication::translate("EventoViewModel", "No sé qué es la acción de audio \"%1\" (silenciar, activar o volumen)").arg(QString::fromStdString(accion)),
                   "error");
            return;
        }
        case evento::Tipo::Video: {
            // Un video tiene que VERSE, o sea estar en una escena, y las
            // escenas las arma el operador: meterle una fuente a la escena que
            // está al aire le desarmaría lo que compuso.
            //
            // Por eso el video del guion tiene SU escena, una sola, que se crea
            // sola la primera vez y queda PROTEGIDA: es la que el guion llena y
            // pone al aire. El operador puede componerla como quiera —ponerle
            // un fondo, un logo— y lo único que el guion le cambia es el
            // archivo de la fuente de video.
            const std::string accion = c->datos.value("accion", std::string("arrancar"));
            if (accion == "parar") {
                // Sin archivo, la fuente queda en negro. No se la saca de la
                // escena: recrearla en cada video costaria mas y se veria.
                ventanilla_.pedir("linea_video",
                                  [&rt = rt_, v = videoDe(salida)]() { rt.updateSourceSettings(v, R"({"local_file":""})"); });
                anotar(QCoreApplication::translate("EventoViewModel", "Video: para"), "ok");
                return;
            }
            const QString rel = QString::fromStdString(c->datos.value("archivo", std::string()));
            const QString ruta = guion_.medioPath(rel);
            if (ruta.isEmpty() || !QFile::exists(ruta)) {
                anotar(QCoreApplication::translate("EventoViewModel", "No encontré el video \"%1\" en los medios del evento").arg(rel), "error");
                emit notify(QCoreApplication::translate("EventoViewModel", "El cue no se pudo ejecutar: falta «%1» en los medios del evento").arg(rel), "error");
                return;
            }
            const bool repetir = c->datos.value("repetir", false);
            // Se le cambia el ARCHIVO a la fuente que ya esta en la escena, no
            // se crea una nueva: `playMedia` sirve para la musica, que va por
            // un canal global de audio y no se ve.
            const QJsonObject ajustes{{"local_file", ruta}, {"looping", repetir}, {"clear_on_media_end", true}};
            const bool alAire = c->datos.value("alAire", true);
            if (const FotoMotor::Salida* s = aireDe(salida)) escenaPrevia_ = QString::fromStdString(s->alAire);
            const std::string escenaV = escenaVideoDe(salida), fuenteV = videoDe(salida);
            // Lo que no es «a mano»: la escena del video, si va al aire.
            if (alAire) escenasDeLaLinea_[salida].insert(escenaV);
            const std::optional<std::string> id = salidaDelMotor(salida);
            escenas::Conmutadores* cs = sistema().conmutadores;
            struct Hecho {
                std::string error, aviso;
                bool creada = false;
            };
            // Todo en el dueño, en una orden: la escena y la fuente (si faltan), el archivo,
            // rebobinar y, recién DESPUÉS de que el archivo esté cargado, al aire (al revés se
            // ve un cuadro en negro mientras abre).
            ventanilla_.pedir(
                this, "linea_video",
                [&rt = rt_, escenaV, fuenteV, cfg = QJsonDocument(ajustes).toJson(QJsonDocument::Compact).toStdString(), alAire,
                 id, cs]() {
                    Hecho h;
                    h.error = prepararEscenaDeVideo(rt, escenaV, fuenteV, &h.creada);
                    if (!h.error.empty()) return h;
                    if (!rt.updateSourceSettings(fuenteV, cfg)) {
                        h.aviso = rt.lastError();
                        return h;
                    }
                    // Y se rebobina: si el archivo es el mismo que ya estaba, el motor no
                    // tiene motivo para volver a empezar y el cue no mostraria nada.
                    rt.engine().restartMedia(fuenteV);
                    if (alAire) {
                        // Por el conmutador de su salida (corte directo, como antes): el tally, el
                        // AFV y la escena guardada se enteran igual que con cualquier otro cambio.
                        escenas::Conmutador* cm = cs == nullptr || !id ? nullptr : id->empty() ? &cs->porOmision() : cs->de(*id);
                        h.error = cm ? cm->poner(escenaV) : QCoreApplication::translate("EventoViewModel", "no hay conmutador de escenas").toStdString();
                        if (!h.error.empty())
                            h.error = QCoreApplication::translate("EventoViewModel", "No se pudo poner al aire la escena del video: %1").arg(QString::fromStdString(h.error)).toStdString();
                    }
                    return h;
                },
                [this, salida, rel, repetir, escenaV, alAire, clave, fuenteV](const Hecho& h) {
                    if (h.creada) anotar(QCoreApplication::translate("EventoViewModel", "Creé la escena «%1» para los videos").arg(QString::fromStdString(escenaV)), "info");
                    if (!h.aviso.empty() || !h.error.empty()) {
                        if (alAire) olvidarEscenaDeLaLinea(salida, escenaV);
                        if (!h.aviso.empty()) return anotar(QCoreApplication::translate("EventoViewModel", "No se pudo cargar %1: %2").arg(rel, QString::fromStdString(h.aviso)), "error");
                        return anotar(enSalida(salida, QString::fromStdString(h.error)), "error");
                    }
                    if (!repetir) mediosVigilados_[clave.substr(clave.rfind('/') + 1)] = fuenteV; // «medio:<pieza>.termino»
                            anotar(enSalida(salida, repetir ? QCoreApplication::translate("EventoViewModel", "Video: %1 (en bucle)").arg(rel) : QCoreApplication::translate("EventoViewModel", "Video: %1").arg(rel)), "ok");
                });
            return;
        }
        case evento::Tipo::Pantalla: {
            // Lo mismo que pantalla_mostrar: `contenido` es el texto de un
            // cartel, una dirección (url) o el nombre de una vista (programa,
            // reloj, prompter, tally, votacion).
            const std::string pantalla = c->datos.value("pantalla", std::string());
            const std::string contenido = c->datos.value("contenido", std::string());
            std::string tipo = c->datos.value("tipo", std::string());
            if (tipo.empty())
                tipo = contenido.rfind("http", 0) == 0 ? "url"
                       : (contenido == "programa" || contenido == "reloj" || contenido == "prompter" ||
                          contenido == "tally" || contenido == "votacion")
                           ? contenido
                           : "mensaje";
            json args{{"pantalla", pantalla}, {"tipo", tipo}};
            if (tipo == "url") args["url"] = contenido;
            if (tipo == "mensaje" || tipo == "qr") args["texto"] = contenido;
            if (c->datos.contains("pie")) args["pie"] = c->datos["pie"];
            if (!herramienta_) {
                anotar(QCoreApplication::translate("EventoViewModel", "No se puede mandar a la pantalla: el servidor de herramientas no está"), "error");
                return;
            }
            QPointer<CorridaViewModel> yo(this);
            herramienta_("pantalla_mostrar", args, [yo, titulo, pantalla](bool ok, const std::string& texto) {
                if (!yo) return;
                yo->anotar((ok ? QCoreApplication::translate("EventoViewModel", "Pantalla «%1»: %2").arg(QString::fromStdString(pantalla), titulo)
                               : QCoreApplication::translate("EventoViewModel", "No se pudo mandar a «%1»: %2")
                                     .arg(QString::fromStdString(pantalla), QString::fromStdString(texto))),
                           ok ? "ok" : "error");
            });
            return;
        }
        default:
            // Honestidad: el tipo existe en el guion pero todavia no se
            // ejecuta. Se anota y la línea sigue, en vez de hacer como que
            // paso algo.
            anotar(QCoreApplication::translate("EventoViewModel", "Todavía no sé ejecutar un cue de tipo \"%1\": %2")
                       .arg(QString::fromStdString(evento::nombreDeTipo(c->tipo)), titulo),
                   "error");
            return;
    }
}

// --- La línea con varias salidas y lo de afuera (L6/L7) ------------------------------------
//
// docs/planes/PLAN-LINEA-DE-TIEMPO.md 3.4, 3.6 y 3.9. La corrida (pura) decide; acá se
// engancha a lo que hay en la app: el conmutador de cada salida, el bus (para las
// condiciones), las capas DSK, las cámaras PTZ, el orden del día, las votaciones y la
// playlist. Nada de esto publica temas linea/* (eso es de ui/LineaBus).

QString CorridaViewModel::enSalida(const std::string& salida, const QString& texto) const {
    if (linea().salidasEfectivas().size() < 2) return texto;
    return QCoreApplication::translate("EventoViewModel", "«%1» · %2").arg(QString::fromStdString(salida), texto);
}

std::string CorridaViewModel::musicaDe(const std::string& salida) const {
    return salida.empty() || salida == salidaPorOmision() ? std::string(kMusica) : std::string(kMusica) + " · " + salida;
}

std::string CorridaViewModel::escenaVideoDe(const std::string& salida) const {
    return salida.empty() || salida == salidaPorOmision() ? std::string(kEscenaVideo) : std::string(kEscenaVideo) + " · " + salida;
}

std::string CorridaViewModel::videoDe(const std::string& salida) const {
    return salida.empty() || salida == salidaPorOmision() ? std::string(kVideo) : std::string(kVideo) + " · " + salida;
}

void CorridaViewModel::enchufar() {
    const Sistema s = sistema();
    const auto primeraVez = [this](const QObject* o) { return o != nullptr && enchufados_.insert(o).second; };
    // El aire de cada salida: lo hecho «a mano» y las PTZ que esperan una cámara.
    if (primeraVez(s.conmutadores)) {
        const auto seguir = [this](escenas::Conmutador* c) {
            // La salida por su id (no el puntero): el aviso puede llegar encolado (F3-03).
            connect(c, &escenas::Conmutador::alAireCambio, this,
                    [this, salida = c->salida()](const QString& escena) { alAireCambio(salida, escena.toStdString()); });
        };
        for (escenas::Conmutador* c : s.conmutadores->todos())
            seguir(c);
        // Una salida nueva: el aviso llega encolado (los conmutadores son del dueño del
        // motor) y por su id; si ya se fue, no hay a quién seguir.
        connect(s.conmutadores, &escenas::Conmutadores::creado, this, [this, seguir](const QString& salida) {
            if (escenas::Conmutador* c = sistema().conmutadores->de(salida.toStdString()); c != nullptr && c->salida() == salida.toStdString())
                seguir(c);
        });
    }
    // El orden del día: los bloques que salen de él se re-expanden al cambiar.
    if (primeraVez(s.ordenDelDia))
        connect(s.ordenDelDia, &asamblea::OrdenDelDia::cambio, this, &CorridaViewModel::expandirOrdenDelDia);
    // La playlist: cuando se para sola, terminó «su medio» (para un paso que dura lo que ella).
    if (primeraVez(s.playlist))
        connect(s.playlist, &medios::Playlist::estadoCambio, this, [this]() {
            const Sistema s = sistema();
            if (s.playlist == nullptr || playlistDePieza_.empty()) return;
            const bool suena = s.playlist->estado().value("reproduciendo", false);
            if (suena) playlistAndando_ = true;
            if (suena || !playlistAndando_) return;
            const std::string pieza = playlistDePieza_;
            playlistDePieza_.clear();
            playlistAndando_ = false;
            if (corriendo()) despachar(corrida_->medioTermino(pieza, ahoraMs()));
        });
    // Los pulsos (tecla:, botonera:, web:): los publican las superficies y los atajos
    // en «linea/pulso» (un plugin también puede). Se atienden después, en limpio, como
    // los temas de las condiciones.
    if (primeraVez(s.bus)) {
        QPointer<CorridaViewModel> yo(this);
        s.bus->suscribir("linea/pulso", [yo](const std::string&, const json& valor) {
            if (!yo || !valor.is_string()) return;
            const std::string clave = valor.get<std::string>();
            QMetaObject::invokeMethod(
                yo, [yo, clave]() { if (yo && yo->corriendo()) yo->lineaPulso(clave); }, Qt::QueuedConnection);
        });
    }
    expandirOrdenDelDia();
}

void CorridaViewModel::vigilarTemas() {
    control::BusEventos* bus = sistema().bus;
    if (bus == nullptr || !corrida_) return;
    for (const std::string& tema : corrida_->temasQueMira()) {
        if (!suscripciones_.count(tema)) {
            // Lo de afuera llega en medio de cualquier cosa (hasta de un TAKE de la
            // línea): se atiende después, en limpio. Los temas que dejan de usarse
            // quedan suscriptos: la corrida ignora lo que no mira.
            QPointer<CorridaViewModel> yo(this);
            suscripciones_[tema] = bus->suscribir(tema, [yo](const std::string& t, const json& valor) {
                if (!yo) return;
                QMetaObject::invokeMethod(
                    yo,
                    [yo, t, valor]() {
                        if (!yo || !yo->corrida_) return;
                        const std::vector<Accion> acc = yo->corrida_->hecho(t, valor, ahoraMs());
                        if (yo->corriendo()) yo->despachar(acc);
                    },
                    Qt::QueuedConnection);
            });
        }
        // La corrida es nueva (o el tema): se pone al día con lo último que se publicó.
        const json u = bus->ultimo(tema);
        if (u.is_object() && u.contains("valor")) {
            const std::vector<Accion> acc = corrida_->hecho(tema, u["valor"], ahoraMs());
            if (corriendo() && !acc.empty()) despachar(acc);
        }
    }
}

void CorridaViewModel::alAireCambio(const std::string& salida, const std::string& escena) {
    // ¿Fue a mano? Lo que cambia la línea misma (tomarEscena, el video) no cuenta: su aviso
    // llega encolado desde el dueño del motor, después de la orden.
    bool deLaLinea = false;
    if (auto it = escenasDeLaLinea_.find(salida); it != escenasDeLaLinea_.end())
        if (auto x = it->second.find(escena); x != it->second.end()) {
            it->second.erase(x);
            deLaLinea = true;
        }
    if (!deLaLinea && corriendo() && !ensayo_) {
        const std::string antes = corrida_->aMano(salida);
        corrida_->marcarAMano(salida, escena, ahoraMs());
        const std::string ahora = corrida_->aMano(salida);
        if (ahora != antes) {
            if (!ahora.empty())
                anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "A mano: «%1» en vez de la del paso (la línea sigue; «reenganchar» vuelve)")
                                            .arg(QString::fromStdString(ahora))),
                       "info");
            volcarRecorrido();
            emit correChanged();
        }
    }
    // Una cámara pudo haber salido del aire: las PTZ que la esperaban, ahora.
    ptz::ControlCamaras* cams = sistema().camaras;
    if (cams == nullptr) return;
    std::vector<std::string> listas;
    for (const auto& [clave, x] : ptzPendientes_)
        if (cams->chequearMovimiento(QString::fromStdString(x.camara)).isEmpty()) listas.push_back(clave);
    for (const std::string& clave : listas)
        moverPtz(clave, false);
}

void CorridaViewModel::expandirOrdenDelDia() {
    asamblea::OrdenDelDia* od = sistema().ordenDelDia;
    if (od == nullptr || !ab_.hay()) return;
    std::set<std::string> emitidos;
    if (corrida_)
        for (const evento::linea::Instancia& x : corrida_->instancias())
            emitidos.insert(x.paso);
    evento::linea::Linea l = linea();
    if (!evento::linea::expandirOrdenDelDia(l, od->puntos(), emitidos)) return;
    anotar(QCoreApplication::translate("EventoViewModel", "El orden del día cambió: la línea se volvió a armar (lo que ya salió no se tocó)"), "info");
    ab_.ponerLinea(std::move(l));
}

bool CorridaViewModel::ejecutarPiezaLinea(const evento::linea::Pieza& z, const Accion& a, const QString& titulo) {
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const std::string clave = salida + "/" + a.paso + "/" + a.pieza;
    const Sistema s = sistema();
    const auto falla = [&](const QString& porque) {
        anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "No se pudo hacer «%1»: %2").arg(titulo, porque)), "error");
        emit notify(QCoreApplication::translate("EventoViewModel", "La pieza «%1» no se pudo hacer: %2").arg(titulo, porque), "error");
    };
    const auto bien = [&](const QString& que) { anotar(enSalida(salida, que), "ok"); };
    const json& d = z.datos;

    if (z.tipo == "ptz") {
        // {camara, posicion, segundos?, siOcupada: esperar|mover|saltar}
        if (s.camaras == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay control de cámaras")), true;
        PtzPendiente x{salida, a.paso, a.pieza, d.value("camara", std::string()), d.value("posicion", std::string()),
                       d.value("segundos", -1.0), ahoraMs()};
        if (x.camara.empty() || x.posicion.empty()) return falla(QCoreApplication::translate("EventoViewModel", "la PTZ no dice qué cámara ni a qué posición")), true;
        const QString motivo = s.camaras->chequearMovimiento(QString::fromStdString(x.camara));
        const std::string si = d.value("siOcupada", std::string("esperar"));
        ptzPendientes_[clave] = x;
        if (motivo.isEmpty() || si == "mover") {
            moverPtz(clave, !motivo.isEmpty());
        } else if (si == "saltar") {
            ptzPendientes_.erase(clave);
            anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1»: no se mueve (%2)").arg(titulo, motivo)), "info");
        } else {
            // Ocupada: espera a que se libere, y la persona puede decir «mover igual».
            anotar(enSalida(salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1» espera: %2").arg(titulo, motivo)), "info");
            emit notify(QCoreApplication::translate("EventoViewModel", "%1 La PTZ «%2» se hace cuando se libere (o «mover igual»).").arg(motivo, titulo), "info");
            emit correChanged();
        }
        return true;
    }

    if (z.tipo == "dsk") {
        // {capa: 1..4, accion: subir|bajar, confirmada?}: en las capas de ESTA salida.
        if (s.dsk == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay capas DSK")), true;
        const int capa = d.value("capa", 0);
        const bool subir = d.value("accion", std::string("subir")) != "bajar";
        const std::string e = s.dsk->mostrar(capa, subir, d.value("confirmada", false), salida);
        if (!e.empty()) return falla(QString::fromStdString(e)), true;
        if (subir) dskDePieza_[clave] = {capa, salida}; // baja cuando se termina su vida
        else dskDePieza_.erase(clave);
        bien(subir ? QCoreApplication::translate("EventoViewModel", "Sube la capa %1").arg(capa) : QCoreApplication::translate("EventoViewModel", "Baja la capa %1").arg(capa));
        return true;
    }

    if (z.tipo == "asamblea") {
        // {accion: tratar (punto) | siguiente | punto (punto, estado, resultado?) |
        //  votacion (que: abrir|pausar|reanudar|cerrar|a_quorum, tema, menu, segundos,
        //  resultado: al cerrar, también el resultado oficial; de fábrica, sí)}
        const std::string accion = d.value("accion", std::string());
        const auto numero = [&](const char* k) {
            const json& v = d.contains(k) ? d[k] : json();
            return v.is_number() ? v.get<int>() : v.is_string() ? std::atoi(v.get<std::string>().c_str()) : 0;
        };
        if (accion == "tratar" || accion == "siguiente" || accion == "punto") {
            if (s.ordenDelDia == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay orden del día")), true;
            const int n = numero("punto");
            const std::string e = accion == "siguiente" ? s.ordenDelDia->siguiente()
                                  : accion == "tratar"  ? s.ordenDelDia->punto(n, "en_tratamiento", "")
                                                        : s.ordenDelDia->punto(n, d.value("estado", std::string()), d.value("resultado", std::string()));
            if (!e.empty()) return falla(QString::fromStdString(e)), true;
            bien(accion == "siguiente" ? QCoreApplication::translate("EventoViewModel", "Orden del día: el punto que sigue") : QCoreApplication::translate("EventoViewModel", "Orden del día: punto %1").arg(n));
            return true;
        }
        if (accion == "votacion") {
            if (s.votaciones == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay votaciones")), true;
            const std::string que = d.value("que", std::string());
            const bool conResultado = que == "cerrar" && d.value("resultado", true);
            QPointer<CorridaViewModel> yo(this);
            QPointer<asamblea::Votaciones> vs(s.votaciones);
            s.votaciones->accion(que, d, "linea", [yo, vs, titulo, salida, conResultado](const std::string& e) {
                if (!yo) return;
                if (!e.empty()) {
                    yo->anotar(yo->enSalida(salida, QCoreApplication::translate("EventoViewModel", "Votación (%1): %2").arg(titulo, QString::fromStdString(e))), "error");
                    return;
                }
                yo->anotar(yo->enSalida(salida, QCoreApplication::translate("EventoViewModel", "Votación: %1").arg(titulo)), "ok");
                // Cerrada: el resultado oficial (a Quórum y al bus: sesion/votacion).
                if (conResultado && vs)
                    vs->accion("a_quorum", json::object(), "linea", [yo, salida](const std::string& e2) {
                        if (yo && !e2.empty())
                            yo->anotar(yo->enSalida(salida, QCoreApplication::translate("EventoViewModel", "El resultado no se pudo cargar: %1").arg(QString::fromStdString(e2))),
                                       "error");
                    });
            });
            return true;
        }
        return falla(QCoreApplication::translate("EventoViewModel", "«%1» no es una acción de la asamblea (tratar, siguiente, punto, votacion)").arg(QString::fromStdString(accion))),
               true;
    }

    if (z.tipo == "playlist") {
        // {accion: play|next|prev|stop, indice?, autoNext?, loop?}: la línea manda sobre la
        // playlist, que sigue siendo la dueña del in/out y del auto-next.
        if (s.playlist == nullptr) return falla(QCoreApplication::translate("EventoViewModel", "no hay playlist")), true;
        if (d.contains("autoNext")) s.playlist->setAutoNext(d.value("autoNext", true));
        if (d.contains("loop")) s.playlist->setLoop(d.value("loop", false));
        const std::string accion = d.value("accion", std::string("play"));
        const std::string e = s.playlist->control(accion, d.value("indice", -1));
        if (!e.empty()) return falla(QString::fromStdString(e)), true;
        if (accion == "play") {
            playlistDePieza_ = a.pieza; // su fin (medio:<pieza>.termino) es cuando la playlist se para sola
            playlistAndando_ = false;
        } else if (accion == "stop") {
            playlistDePieza_.clear();
        }
        bien(QCoreApplication::translate("EventoViewModel", "Playlist: %1").arg(QString::fromStdString(accion)));
        return true;
    }
    return false;
}

void CorridaViewModel::moverPtz(const std::string& clave, bool forzar) {
    const auto it = ptzPendientes_.find(clave);
    if (it == ptzPendientes_.end()) return;
    const PtzPendiente x = it->second;
    ptzPendientes_.erase(it);
    ptz::ControlCamaras* cams = sistema().camaras;
    if (cams == nullptr) return;
    const QVariantMap r = cams->irAPosicion(QString::fromStdString(x.camara), QString::fromStdString(x.posicion), x.segundos);
    if (!r.value("ok").toBool()) {
        anotar(enSalida(x.salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1» a «%2»: %3")
                                      .arg(QString::fromStdString(x.camara), QString::fromStdString(x.posicion), r.value("error").toString())),
               "error");
    } else {
        anotar(enSalida(x.salida, (forzar ? QCoreApplication::translate("EventoViewModel", "PTZ «%1» a «%2» (movida igual, estaba al aire)") : QCoreApplication::translate("EventoViewModel", "PTZ «%1» a «%2»"))
                                      .arg(QString::fromStdString(x.camara), QString::fromStdString(x.posicion))),
               "ok");
    }
    emit correChanged();
}

void CorridaViewModel::mirarMedios() {
    std::map<std::string, std::string> estado;
    for (const auto& m : ventanilla_.foto().medios) estado[m.value("fuente", std::string())] = m.value("estado", std::string());
    std::vector<std::string> terminaron;
    for (const auto& [pieza, medio] : mediosVigilados_) {
        const auto e = estado.find(medio);
        const std::string st = e == estado.end() ? std::string("none") : e->second;
        // Un medio que todavía no arrancó no «terminó».
        if (st == "playing") mediosAndando_.insert(pieza);
        else if (mediosAndando_.count(pieza) && (st == "ended" || st == "stopped" || st == "none" || st == "error"))
            terminaron.push_back(pieza);
    }
    for (const std::string& pieza : terminaron) {
        mediosVigilados_.erase(pieza);
        mediosAndando_.erase(pieza);
        if (corriendo()) despachar(corrida_->medioTermino(pieza, ahoraMs()));
    }
    if (mediosVigilados_.empty()) mediosAndando_.clear();
}

std::string CorridaViewModel::lineaPulso(const std::string& clave) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    if (const std::string e = evento::linea::errorDeCondicion(clave); !e.empty()) return e;
    despachar(corrida_->pulso(clave, ahoraMs()));
    return {};
}

std::string CorridaViewModel::lineaReenganchar(const std::string& salida) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = errorDeSalida(linea(), s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->reenganchar(s, ahoraMs(), &err);
    if (err.empty()) anotar(enSalida(s, QCoreApplication::translate("EventoViewModel", "Reengancha: vuelve a la escena del paso")), "info");
    despachar(acc);
    return err;
}

std::string CorridaViewModel::lineaPtz(const std::string& pieza, const std::string& decision) {
    for (const auto& [clave, x] : ptzPendientes_) {
        if (x.pieza != pieza) continue;
        const std::string k = clave;
        if (decision == "mover") moverPtz(k, true);
        else if (decision == "saltar") {
            anotar(enSalida(x.salida, QCoreApplication::translate("EventoViewModel", "PTZ «%1»: se deja sin mover").arg(QString::fromStdString(pieza))), "info");
            ptzPendientes_.erase(k);
            emit correChanged();
        } else if (decision != "esperar") {
            return QCoreApplication::translate("EventoViewModel", "La decisión es «mover», «esperar» o «saltar».").toStdString();
        }
        return {};
    }
    return QCoreApplication::translate("EventoViewModel", "No hay ninguna PTZ «%1» esperando.").arg(QString::fromStdString(pieza)).toStdString();
}

// --- Lo que dijo cada orador, en el recorrido -----------------------------------------------
//
// El pronóstico usa lo previsto de la pieza `orador`; lo que habló de verdad lo sabe el
// orden del día (OrdenDelDia::tiemposPalabra, por persona y acumulado). Mientras un paso
// con orador está al aire se guarda cuánto llevaba cada uno al entrar; al salir, la
// diferencia va al recorrido («palabra»): quién habló en ese paso y cuánto.

std::map<std::string, int64_t> CorridaViewModel::segundosDePalabra() const {
    std::map<std::string, int64_t> m;
    asamblea::OrdenDelDia* od = sistema().ordenDelDia;
    if (od == nullptr) return m;
    for (const json& o : od->tiemposPalabra(json::array()))
        m[o.value("nombre", std::string())] = o.value("segundos", 0LL);
    return m;
}

void CorridaViewModel::seguirPalabra(const std::vector<Accion>& acc) {
    if (!corrida_ || ensayo_) return;
    for (const Accion& a : acc) {
        if (a.que != Accion::Que::EntraPaso && a.que != Accion::Que::Termina) continue;
        const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
        // Se fue el que estaba: lo que habló cada uno mientras tanto.
        if (const auto it = palabras_.find(salida); it != palabras_.end()) {
            const Palabra antes = it->second;
            palabras_.erase(it);
            json hablaron = json::array();
            int64_t total = 0;
            for (const auto& [nombre, seg] : segundosDePalabra()) {
                const auto a0 = antes.segundos.find(nombre);
                const int64_t d = seg - (a0 == antes.segundos.end() ? 0 : a0->second);
                if (d <= 0 || nombre.empty()) continue;
                hablaron.push_back({{"nombre", nombre}, {"segundos", d}});
                total += d;
            }
            if (!hablaron.empty())
                corrida_->anotarDeAfuera(ahoraMs(), "palabra", {{"salida", salida}, {"paso", antes.paso}, {"segundos", total},
                                                                 {"previstoSeg", antes.previstoSeg}, {"hablaron", hablaron}});
        }
        if (a.que != Accion::Que::EntraPaso) continue;
        const evento::linea::Paso* p = corrida_->linea().paso(a.paso);
        if (p == nullptr || std::none_of(p->piezas.begin(), p->piezas.end(), [](const auto& z) { return z.tipo == "orador"; })) continue;
        palabras_[salida] = {a.paso, static_cast<int>(p->duracionPlanMs() / 1000), segundosDePalabra()};
    }
}

// --- Lo del presentador en los atriles (/prompter) -------------------------------------------
//
// La vista del orador (la tablet del atril) muestra además cuánto le queda al paso al
// aire y qué viene, como la pantalla de presentador de Sofie. No es un aparato del
// control web: va por el gateway a TODOS los teleprompters («*»), y solo cuando cambia
// (la cuenta la hace la página con la hora de fin y el reloj del servidor).

void CorridaViewModel::avisarPresentador() {
    if (sala_ == nullptr) return;
    json p; // null: no corre ninguna línea (o es un ensayo: el atril no se entera)
    if (corriendo() && !ensayo_) {
        const json est = lineaEstado();
        for (const json& x : est.value("salidas", json::array())) {
            if (x.value("salida", std::string()) != salidaPorOmision()) continue;
            const json a = x.value("alAire", json()), pr = x.value("proximo", json());
            if (!a.is_object()) break;
            const int64_t t = ahoraMs();
            json aire{{"nombre", a.value("nombre", std::string())}, {"desdeMs", t - a.value("llevaSeg", 0LL) * 1000}};
            if (a.value("previstoSeg", 0) > 0) aire["hastaMs"] = t + a.value("restanteSeg", 0LL) * 1000;
            p = {{"alAire", aire}, {"hold", x.value("hold", false)}};
            if (pr.is_object()) p["proximo"] = {{"nombre", pr.value("nombre", std::string())}, {"automatico", pr.value("automatico", false)}};
        }
    }
    // Lo que cambia de verdad (no el segundo en que se calculó): el paso, su fin al
    // segundo, el próximo y el HOLD.
    json firma = p;
    if (firma.is_object()) {
        firma["alAire"].erase("desdeMs");
        if (firma["alAire"].contains("hastaMs")) firma["alAire"]["hastaMs"] = firma["alAire"]["hastaMs"].get<int64_t>() / 2000;
    }
    if (firma == presentadorEnviado_) return;
    presentadorEnviado_ = firma;
    sala_->teleprompter().para("*").presentador(p);
}

// --- Grabar la línea en vivo (L10) ------------------------------------------------------------

evento::linea::Linea CorridaViewModel::lineaDeCorrida() const {
    return grabar_ ? evento::linea::lineaParaGrabar(linea()) : linea();
}

void CorridaViewModel::setGrabarTiempos(bool on) {
    if (on == grabar_) return;
    if (corriendo()) {
        emit notify(QCoreApplication::translate("EventoViewModel", "Grabar los tiempos se elige antes de arrancar el ensayo"), "error");
        return;
    }
    grabar_ = on;
    // Grabar es un ensayo: no sale nada al aire mientras se toca GO a mano.
    if (on) setEnsayo(true);
    grabado_ = nullptr;
    emit correChanged();
}

QVariantMap CorridaViewModel::tiemposGrabadosUi() const {
    if (!grabado_.is_object()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(grabado_.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

json CorridaViewModel::lineaTiemposGrabados() const {
    if (grabado_.is_object()) return grabado_;
    return {{"grabando", grabar_}, {"corriendo", corriendo()}, {"pasos", json::array()}};
}

void CorridaViewModel::aplicarTiemposGrabados() {
    if (!grabado_.is_object() || grabado_["ops"].empty()) return avisarError(QCoreApplication::translate("EventoViewModel", "No hay tiempos grabados para guardar."));
    const json r = lineaEditar(grabado_["ops"], false);
    if (!r.value("ok", false)) return avisarError(QString::fromStdString(r.value("error", std::string())));
    anotar(QCoreApplication::translate("EventoViewModel", "Los tiempos del ensayo quedaron como duraciones previstas"), "ok");
    grabado_ = nullptr;
    grabar_ = false;
    emit correChanged();
}

void CorridaViewModel::descartarTiemposGrabados() {
    if (grabado_.is_null()) return;
    grabado_ = nullptr;
    emit correChanged();
}

// --- Armar desde plantillas y mirar las cámaras (linea_desde_plantilla, linea_camaras) ------------

json CorridaViewModel::lineaDesdePlantilla(const json& a, const std::string& salida, const json& guia, const json& ordenDelDia) {
    namespace L = evento::linea;
    if (!ab_.hay()) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString()}};
    const std::string plantilla = a.value("plantilla", std::string()), escenario = a.value("escenario", std::string());
    const std::string origen = a.value("origen", std::string());
    if (plantilla.empty() && escenario.empty() && origen.empty()) {
        // Sin nada: las que hay (las del evento y las de fábrica).
        json lista = json::array();
        const auto sumar = [&lista](const L::Plantilla& p, const char* de) {
            json pasos = json::array();
            for (const L::Paso& x : p.pasos) pasos.push_back({{"id", x.id}, {"nombre", x.nombre}, {"piezas", x.piezas.size()}});
            lista.push_back({{"id", p.id}, {"nombre", p.nombre}, {"de", de}, {"pasos", pasos}});
        };
        for (const L::Plantilla& p : linea().plantillas) sumar(p, "evento");
        for (const L::Plantilla& p : L::plantillasDeFabrica()) sumar(p, "fabrica");
        return {{"ok", true}, {"plantillas", lista}};
    }
    json armado;
    if (!escenario.empty()) {
        if (!guia.is_object())
            return {{"ok", false},
                    {"error", ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay una plantilla del escenario «%1» (evento_escenarios)"), {escenario})}};
        armado = guia.contains("linea") && guia["linea"].is_object() ? L::lineaDeGuia(linea(), L::Linea::deJson(guia["linea"]))
                                                                      : L::lineaDeGuia(linea(), evento::Evento::deJson(guia));
    } else if (origen == "orden-del-dia") {
        armado = L::bloqueDeOrdenDelDia(linea(), plantilla.empty() ? "punto" : plantilla, ordenDelDia,
                                        a.value("nombreBloque", std::string()), salida);
    } else {
        L::DesdePlantilla d;
        d.plantilla = plantilla;
        d.datos = a.value("datos", json::array());
        d.bloque = a.value("bloque", std::string());
        d.nombreBloque = a.value("nombreBloque", std::string());
        d.alPrincipio = a.contains("despuesDe") && a["despuesDe"] == "";
        d.despuesDe = a.value("despuesDe", std::string());
        d.prefijo = a.value("prefijo", std::string());
        d.salida = salida;
        armado = L::desdePlantilla(linea(), d);
    }
    if (!armado.value("ok", true) || !armado.contains("ops")) return {{"ok", false}, {"error", armado.value("error", std::string())}};
    json r = lineaEditar(armado["ops"], a.value("probar", true));
    if (!r.value("ok", false)) return r;
    r["ops"] = armado["ops"];
    r["avisos"] = armado.value("avisos", json::array());
    if (armado.contains("pasos")) r["pasos"] = armado["pasos"];
    if (armado.contains("bloques")) r["bloques"] = armado["bloques"];
    return r;
}

json CorridaViewModel::lineaCamaras(const std::string& camara,
                                    const std::function<std::vector<std::string>(const std::string&)>& camarasDeEscena) const {
    if (!ab_.hay()) return {{"ok", false}, {"error", QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString()}};
    json vistas = json::array();
    for (const std::string& s : linea().salidasEfectivas()) {
        json v = lineaVer(s, 0, 0, "pasos");
        if (!v.is_null()) vistas.push_back(std::move(v));
    }
    json c = evento::linea::proximosPorCamara(linea(), vistas, camarasDeEscena);
    if (!camara.empty()) c = json{{camara, c.value(camara, json::array())}};
    return {{"ok", true}, {"camaras", c}};
}

// --- Lo público, para la página web del evento ------------------------------------------------

CorridaViewModel::Publico CorridaViewModel::publico() const {
    // El estado público: en qué sección va (nada de cues ni notas). Un ensayo no se publica.
    Publico p;
    if (!corrida_ || ensayo_) return p;
    switch (corrida_->estado()) {
        case evento::linea::EstadoCorrida::Corriendo: p.estado = "en_vivo"; break;
        case evento::linea::EstadoCorrida::Pausada: p.estado = "pausa"; break;
        case evento::linea::EstadoCorrida::Terminada: p.estado = "terminado"; break;
        default: break;
    }
    if (p.estado == "en_vivo" || p.estado == "pausa") p.bloque = cursor_.bloque;
    if (corriendo() && p.bloque >= 0 && p.bloque < static_cast<int>(ev().bloques.size())) {
        // Lo que le falta a la sección actual según lo previsto, y quién habla (si el cue
        // que corre es de orador: el que tiene la palabra se ve al aire, no es privado).
        const auto& cues = ev().bloques[static_cast<std::size_t>(p.bloque)].cues;
        int falta = 0;
        for (int c = cursor_.cue; c < static_cast<int>(cues.size()); ++c) falta += cues[static_cast<std::size_t>(c)].duracionSeg;
        p.restanteActualSeg = std::max(0, falta - enCueSeg(ahoraMs()));
        if (const evento::Cue* c = cue(cursor_.bloque, cursor_.cue); c != nullptr && c->tipo == evento::Tipo::Orador)
            p.habla = c->datos.value("participante", std::string());
    }
    return p;
}

} // namespace ba::app
