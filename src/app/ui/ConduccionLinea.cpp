// ============================================================================
// src/app/ui/ConduccionLinea.cpp — ver ConduccionLinea.hpp
// ============================================================================
#include "ui/ConduccionLinea.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QTimeZone>

#include <algorithm>
#include <sstream>

#include "arranque/AppRuntime.hpp"
#include "asamblea/OrdenDelDia.hpp"
#include "control/BusEventos.hpp"
#include "diag/Registro.hpp"
#include "emision/VentanillaMotor.hpp"
#include "escenas/Conmutador.hpp"
#include "evento/Condicion.hpp"
#include "evento/LineaPlantillas.hpp"
#include "evento/LineaProductor.hpp"
#include "evento/LineaVista.hpp"
#include "i18n/I18n.hpp"
#include "medios/Playlist.hpp"
#include "persist/ColaEscritura.hpp"
#include "sala/Teleprompter.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EjecucionLinea.hpp"
#include "ui/EjecutorLinea.hpp"
#include "ui/EventoAbierto.hpp"
#include "ui/GuionViewModel.hpp"

namespace ba::app {
namespace {

using json = nlohmann::json;
using evento::linea::Accion;

int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

ConduccionLinea::ConduccionLinea(EventoAbierto& abierto, GuionViewModel& guion, AppRuntime& rt, VentanillaMotor& ventanilla,
                                 ConferenceViewModel* sala, graficos::Emisor* graficos, QObject* parent)
    : QObject(parent), ab_(abierto), guion_(guion), rt_(rt), ventanilla_(ventanilla), sala_(sala),
      ejec_(std::make_unique<EjecucionLinea>(*this, guion, rt, ventanilla, sala, graficos)) {
    // El estado vivo (la sección `linea` del documento de aire): «cada segundo y en cada
    // cambio». Lo que entra solo lo programa cada EjecutorLinea a la hora justa. El reloj de
    // la vista no es este: la vista cuenta con el Reloj de las vistas y las marcas, que
    // cambian con cada cambio de estado.
    reloj_.setInterval(1000);
    connect(&reloj_, &QTimer::timeout, this, &ConduccionLinea::escribirVivo);
    connect(&ab_, &EventoAbierto::lineaCambio, this, &ConduccionLinea::lineaCambio);
    connect(&ab_, &EventoAbierto::abiertoCambio, this, [this]() { revision_ = -1; });
}

ConduccionLinea::~ConduccionLinea() = default;

void ConduccionLinea::setHerramienta(Herramienta fn) { ejec_->setHerramienta(std::move(fn)); }
void ConduccionLinea::setVinculos(graficos::Vinculos* v) { ejec_->setVinculos(v); }
void ConduccionLinea::setCarruseles(CarruselViewModel* c) { ejec_->setCarruseles(c); }


const evento::Evento& ConduccionLinea::ev() const { return ab_.evento(); }
const evento::linea::Linea& ConduccionLinea::linea() const { return ab_.linea(); }
const evento::linea::Proyeccion& ConduccionLinea::proy() const { return ab_.proyeccion(); }
const evento::Cue* ConduccionLinea::cue(int bloque, int c) const { return ab_.cue(bloque, c); }
std::string ConduccionLinea::salidaPorOmision() const { return linea().salidasEfectivas().front(); }

void ConduccionLinea::limpiarBitacora() {
    bitacora_.clear();
    emit bitacoraChanged();
}

void ConduccionLinea::olvidar() {
    parar();
    ejecutores_.clear();
    corrida_.reset();
    cursor_ = {};
    emit correChanged();
}

void ConduccionLinea::lineaCambio(bool desdeLinea) {
    if (corrida_) corrida_->setLinea(lineaDeCorrida());
    vigilarTemas();
    if (desdeLinea && corrida_) despachar(corrida_->alDia(ahoraMs()));
}

void ConduccionLinea::ensayoAcelerado() {
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

bool ConduccionLinea::corriendo() const {
    if (!corrida_) return false;
    const auto e = corrida_->estado();
    return e == evento::linea::EstadoCorrida::Corriendo || e == evento::linea::EstadoCorrida::Pausada;
}

void ConduccionLinea::moverCursor(int bloque, int cue) {
    const int64_t t = ahoraMs();
    cursor_ = {bloque, cue, t, corrida_ ? corrida_->pausadoMs(t) : 0};
}

void ConduccionLinea::setEnsayo(bool on) {
    if (ensayo_ == on) return;
    ensayo_ = on;
    emit correChanged();
    anotar(on ? QCoreApplication::translate("EventoViewModel", "Ensayo: el guion corre sin salir al aire") : QCoreApplication::translate("EventoViewModel", "Ensayo apagado: el guion sale al aire"), "info");
}

void ConduccionLinea::anotar(const QString& texto, const QString& tipo, bool alRegistro) {
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

int ConduccionLinea::arrancarRevisando(bool forzar) {
    guion_.revisar();
    const int graves = guion_.graves();
    if (graves > 0 && !forzar) return graves;
    arrancar();
    return 0;
}

void ConduccionLinea::nuevaCorrida() {
    for (auto& [s, e] : ejecutores_)
        e->parar();
    ejecutores_.clear();
    corrida_ = std::make_unique<evento::linea::Corrida>(lineaDeCorrida(), entradaBase(true));
    cursor_ = {};
    palabras_.clear();
    grabado_ = nullptr;
    ejec_->reiniciar();
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

void ConduccionLinea::despachar(const std::vector<Accion>& acc) {
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

void ConduccionLinea::arrancar() {
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

void ConduccionLinea::retroceder() {
    if (!corriendo() || !cursor_.valido()) return;
    const auto [pb, pc] = evento::linea::cueAnterior(ev(), cursor_.bloque, cursor_.cue);
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

void ConduccionLinea::irA(int bloque, int cue) {
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

void ConduccionLinea::pausar() {
    if (!corriendo()) return;
    corrida_->pausar(ahoraMs());
    anotar(QCoreApplication::translate("EventoViewModel", "Guion en pausa"), "info");
    despachar({});
}

void ConduccionLinea::reanudar() {
    if (!corriendo()) return;
    anotar(QCoreApplication::translate("EventoViewModel", "Sigue el guion"), "info");
    despachar(corrida_->reanudar(ahoraMs()));
}

void ConduccionLinea::parar() {
    if (!corriendo()) return;
    ejec_->sacarAlAvanzar();
    despachar(corrida_->terminar(ahoraMs()));
}

evento::linea::Entrada ConduccionLinea::entradaBase(bool hoy) const {
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

std::string ConduccionLinea::horaDe(int64_t ms) const {
    const QTimeZone tz = ev().huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(ev().huso));
    return QDateTime::fromMSecsSinceEpoch(ms, tz.isValid() ? tz : QTimeZone::systemTimeZone()).toString("HH:mm:ss").toStdString();
}

int64_t ConduccionLinea::horaHoyMs(const std::string& hhmm) const {
    if (!evento::horaValida(hhmm)) return -1;
    const QTimeZone tz = ev().huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(ev().huso));
    const QTimeZone z = tz.isValid() ? tz : QTimeZone::systemTimeZone();
    return QDateTime(QDateTime::currentDateTime(z).date(), QTime::fromString(QString::fromStdString(hhmm), "HH:mm"), z)
        .toMSecsSinceEpoch();
}

int64_t ConduccionLinea::finPlaneadoMs(int totalPrevistoSeg) const {
    if (ev().inicio.empty()) return -1;
    const QTime ini = QTime::fromString(QString::fromStdString(ev().inicio), "HH:mm");
    if (!ini.isValid()) return -1;
    return QDateTime(QDate::currentDate(), ini.addSecs(totalPrevistoSeg)).toMSecsSinceEpoch();
}

void ConduccionLinea::escribirVivo() {
    // Lo que sale de verdad: el ensayo no se retoma.
    if (!corriendo() || ensayo_) return;
    json j = corrida_->aJsonVivo(ahoraMs());
    j["evento"] = ab_.carpeta().toStdString();
    // La sección de la línea del documento único de aire (F3-04; antes, Runtime/linea-aire.json
    // aparte): el paso de la corrida y los HOLD van en la MISMA versión que la escena, la
    // placa y las salidas de ese momento. Lo escribe el dueño del documento, no este hilo.
    rt_.documentoAire().ponerLinea(std::move(j));
}

void ConduccionLinea::borrarVivo() { rt_.documentoAire().ponerLinea(json()); }

void ConduccionLinea::retomarLinea() {
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

void ConduccionLinea::retomarDesde(const json& vivo) {
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

QString ConduccionLinea::pedidoDeRetoma(const QString& descripcion) const {
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

QString ConduccionLinea::describirRetoma(const QString& texto) {
    if (!corrida_ || !corrida_->retomando() || texto.trimmed().isEmpty()) return {};
    corrida_->describir(texto.trimmed().toStdString(), ahoraMs());
    anotar(QCoreApplication::translate("EventoViewModel", "Lo que está pasando: %1").arg(texto.trimmed()), "info");
    despachar({});
    return pedidoDeRetoma(texto.trimmed());
}

void ConduccionLinea::seguirComoEstaba() {
    if (!corrida_ || !corrida_->retomando()) return;
    anotar(QCoreApplication::translate("EventoViewModel", "Sigue como estaba: se suelta el HOLD"), "ok");
    despachar(corrida_->soltar(QCoreApplication::translate("EventoViewModel", "seguir como estaba").toStdString(), ahoraMs()));
}

std::filesystem::path ConduccionLinea::archivoRecorrido() const {
    if (!ab_.hay()) return {};
    return ab_.dir() / "recorrido.jsonl";
}

void ConduccionLinea::volcarRecorrido() {
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

int ConduccionLinea::ultimaRevision() {
    // Una vez por evento se mira la carpeta; después se lleva la cuenta (lo encolado
    // todavía no está en el disco).
    if (revision_ < 0) revision_ = revisionEnDisco(ab_.dir() / "Respaldos");
    return revision_;
}

json ConduccionLinea::lineaEstado() const {
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
    j["ptzPendientes"] = ejec_->ptzPendientes(hora);
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

json ConduccionLinea::lineaVer(const std::string& salida, int64_t desdeMs, int64_t hastaMs, const std::string& nivel) const {
    const auto hora = [this](int64_t ms) { return horaDe(ms); };
    const int64_t t = ahoraMs();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    const std::string n = nivel.empty() ? "pasos" : nivel;
    if (corrida_) return evento::linea::vista(corrida_->linea(), corrida_->resolver(t), s, desdeMs, hastaMs, n, t, hora);
    evento::linea::Entrada e = entradaBase(false);
    e.ahoraMs = t;
    return evento::linea::vista(linea(), evento::linea::resolver(linea(), e), s, desdeMs, hastaMs, n, t, hora);
}

json ConduccionLinea::lineaEditar(const json& ops, bool probar) {
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

json ConduccionLinea::lineaDeshacer() {
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

json ConduccionLinea::lineaRevisar() const {
    evento::linea::OpcionesRevision o;
    o.contexto = entradaBase(false);
    const std::vector<std::string> escenas = ventanilla_.foto().nombresDeEscenas();
    o.existeEscena = [&escenas](const std::string& e) { return std::find(escenas.begin(), escenas.end(), e) != escenas.end(); };
    const auto rs = evento::linea::revisar(linea(), o);
    const int graves = static_cast<int>(std::count_if(rs.begin(), rs.end(), [](const auto& r) { return r.grave; }));
    return {{"reparos", reparosJson(rs)}, {"graves", graves}, {"sePuedeCorrer", graves == 0}};
}

json ConduccionLinea::lineaAjustar(const std::string& salida, int64_t objetivoMs) const {
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

std::string ConduccionLinea::lineaGo(const std::string& salida) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
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
        const auto [nb, nc] = evento::linea::cueSiguiente(ev(), cursor_.bloque, cursor_.cue);
        if (const auto* r = proy().ref(nb, nc)) pasoSig = r->paso, piezaSig = r->pieza;
    }
    despachar(corrida_->goDelGuion(s, pasoSig, piezaSig, ahoraMs()));
    return {};
}

std::string ConduccionLinea::lineaHold(const std::string& salida, std::optional<bool> on) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo: no hay paso que frenar.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
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

std::string ConduccionLinea::lineaMoverProximo(const std::string& salida, int delta) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
    if (!corrida_->moverProximo(s, delta, ahoraMs())) return QCoreApplication::translate("EventoViewModel", "No hay a dónde mover el próximo.").toStdString();
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string ConduccionLinea::lineaSaltar(const std::string& salida) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
    if (!corrida_->saltar(s, ahoraMs())) return QCoreApplication::translate("EventoViewModel", "No hay próximo para saltear.").toStdString();
    anotar(QCoreApplication::translate("EventoViewModel", "Saltea el próximo paso"), "info");
    despachar(corrida_->alDia(ahoraMs()));
    return {};
}

std::string ConduccionLinea::lineaIr(const std::string& salida, const std::string& paso) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay un evento abierto (guion_abrir).").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
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

std::string ConduccionLinea::lineaRepetir(const std::string& salida) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
    if (corrida_->alAire(s) == nullptr) return QCoreApplication::translate("EventoViewModel", "No hay ningún paso al aire en esa salida.").toStdString();
    anotar(QCoreApplication::translate("EventoViewModel", "Repite el paso al aire"), "info");
    despachar(corrida_->repetir(s, ahoraMs()));
    return {};
}

std::string ConduccionLinea::lineaAdlib(const std::string& salida, const std::string& pieza) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
    // Un paso del estante no se «dispara»: va de próximo.
    for (const evento::linea::ItemEstante& it : corrida_->linea().estante)
        if (it.id == pieza && it.paso) return lineaInsertar(s, pieza, false);
    std::string err;
    const std::vector<Accion> acc = corrida_->adlib(s, pieza, ahoraMs(), &err);
    despachar(acc);
    return err;
}

std::string ConduccionLinea::lineaInsertar(const std::string& salida, const std::string& item, bool alAire) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->insertar(s, item, alAire, ahoraMs(), &err);
    if (err.empty()) anotar(QCoreApplication::translate("EventoViewModel", "Inserta «%1» del estante").arg(QString::fromStdString(item)), "info");
    despachar(acc);
    return err;
}

json ConduccionLinea::lineaRecorrido(int ultimos) const {
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

std::string ConduccionLinea::lineaRetomar(const std::string& descripcion, bool simular) {
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

std::string ConduccionLinea::lineaSoltar(const std::string& decision) {
    if (!corrida_ || !corrida_->retomando()) return QCoreApplication::translate("EventoViewModel", "La línea no está en HOLD por un corte.").toStdString();
    const std::string d = decision.empty() ? QCoreApplication::translate("EventoViewModel", "seguir como estaba").toStdString() : decision;
    anotar(QCoreApplication::translate("EventoViewModel", "Se suelta el HOLD del corte: %1").arg(QString::fromStdString(d)), "ok");
    despachar(corrida_->soltar(d, ahoraMs()));
    return {};
}

evento::Cue ConduccionLinea::cueDePieza(const evento::linea::Pieza& z, const std::string& paso) const {
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

void ConduccionLinea::hacerAccion(const Accion& a) {
    if (!corrida_) return;
    // Cada salida sale de verdad (L6): su conmutador, su mezcla, sus capas. El cursor
    // del guion de cues sigue a la por omisión (es su lectura).
    const std::string salida = a.salida.empty() ? salidaPorOmision() : a.salida;
    const bool principal = salida == salidaPorOmision();
    switch (a.que) {
        case Accion::Que::EntraPaso: {
            if (!ensayo_) ejec_->sacarAlAvanzar(salida);
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
            ejec_->pieza(*z, a, titulo);
            return;
        }
        case Accion::Que::Saca:
            if (!ensayo_) ejec_->sacar(a);
            return;
        case Accion::Que::Armar:
            ejec_->armar(a.escena, salida);
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

// --- La línea con varias salidas y lo de afuera (L6/L7) ------------------------------------
//
// docs/planes/PLAN-LINEA-DE-TIEMPO.md 3.4, 3.6 y 3.9. La corrida (pura) decide; acá se
// engancha a lo que hay en la app: el conmutador de cada salida, el bus (para las
// condiciones), las capas DSK, las cámaras PTZ, el orden del día, las votaciones y la
// playlist. Nada de esto publica temas linea/* (eso es de ui/LineaBus).

QString ConduccionLinea::enSalida(const std::string& salida, const QString& texto) const {
    if (linea().salidasEfectivas().size() < 2) return texto;
    return QCoreApplication::translate("EventoViewModel", "«%1» · %2").arg(QString::fromStdString(salida), texto);
}

void ConduccionLinea::enchufar() {
    const SistemaLinea s = sistema();
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
        connect(s.ordenDelDia, &asamblea::OrdenDelDia::cambio, this, &ConduccionLinea::expandirOrdenDelDia);
    // La playlist: cuando se para sola, terminó «su medio» (para un paso que dura lo que ella).
    if (primeraVez(s.playlist))
        connect(s.playlist, &medios::Playlist::estadoCambio, this, [this]() {
            if (const std::string pieza = ejec_->alPlaylist(); !pieza.empty()) medioTermino(pieza);
        });
    // Los pulsos (tecla:, botonera:, web:): los publican las superficies y los atajos
    // en «linea/pulso» (un plugin también puede). Se atienden después, en limpio, como
    // los temas de las condiciones.
    if (primeraVez(s.bus)) {
        QPointer<ConduccionLinea> yo(this);
        s.bus->suscribir("linea/pulso", [yo](const std::string&, const json& valor) {
            if (!yo || !valor.is_string()) return;
            const std::string clave = valor.get<std::string>();
            QMetaObject::invokeMethod(
                yo, [yo, clave]() { if (yo && yo->corriendo()) yo->lineaPulso(clave); }, Qt::QueuedConnection);
        });
    }
    expandirOrdenDelDia();
}

void ConduccionLinea::vigilarTemas() {
    control::BusEventos* bus = sistema().bus;
    if (bus == nullptr || !corrida_) return;
    for (const std::string& tema : corrida_->temasQueMira()) {
        if (!suscripciones_.count(tema)) {
            // Lo de afuera llega en medio de cualquier cosa (hasta de un TAKE de la
            // línea): se atiende después, en limpio. Los temas que dejan de usarse
            // quedan suscriptos: la corrida ignora lo que no mira.
            QPointer<ConduccionLinea> yo(this);
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

void ConduccionLinea::alAireCambio(const std::string& salida, const std::string& escena) {
    // ¿Fue a mano? Lo que cambia la línea misma (tomarEscena, el video) no cuenta: su aviso
    // llega encolado desde el dueño del motor, después de la orden.
    const bool deLaLinea = ejec_->esDeLaLinea(salida, escena);
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
    ejec_->alAireCambio();
}

void ConduccionLinea::expandirOrdenDelDia() {
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

std::string ConduccionLinea::lineaPulso(const std::string& clave) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    if (const std::string e = evento::linea::errorDeCondicion(clave); !e.empty()) return e;
    despachar(corrida_->pulso(clave, ahoraMs()));
    return {};
}

std::string ConduccionLinea::lineaReenganchar(const std::string& salida) {
    if (!corriendo()) return QCoreApplication::translate("EventoViewModel", "La línea no está corriendo.").toStdString();
    const std::string s = salida.empty() ? salidaPorOmision() : salida;
    if (std::string e = evento::linea::errorDeSalida(linea(), s); !e.empty()) return e;
    std::string err;
    const std::vector<Accion> acc = corrida_->reenganchar(s, ahoraMs(), &err);
    if (err.empty()) anotar(enSalida(s, QCoreApplication::translate("EventoViewModel", "Reengancha: vuelve a la escena del paso")), "info");
    despachar(acc);
    return err;
}

std::string ConduccionLinea::lineaPtz(const std::string& pieza, const std::string& decision) {
    return ejec_->decidirPtz(pieza, decision);
}

void ConduccionLinea::medioTermino(const std::string& pieza) {
    if (corriendo()) despachar(corrida_->medioTermino(pieza, ahoraMs()));
}

// --- Lo que dijo cada orador, en el recorrido -----------------------------------------------
//
// El pronóstico usa lo previsto de la pieza `orador`; lo que habló de verdad lo sabe el
// orden del día (OrdenDelDia::tiemposPalabra, por persona y acumulado). Mientras un paso
// con orador está al aire se guarda cuánto llevaba cada uno al entrar; al salir, la
// diferencia va al recorrido («palabra»): quién habló en ese paso y cuánto.

std::map<std::string, int64_t> ConduccionLinea::segundosDePalabra() const {
    std::map<std::string, int64_t> m;
    asamblea::OrdenDelDia* od = sistema().ordenDelDia;
    if (od == nullptr) return m;
    for (const json& o : od->tiemposPalabra(json::array()))
        m[o.value("nombre", std::string())] = o.value("segundos", 0LL);
    return m;
}

void ConduccionLinea::seguirPalabra(const std::vector<Accion>& acc) {
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

void ConduccionLinea::avisarPresentador() {
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

evento::linea::Linea ConduccionLinea::lineaDeCorrida() const {
    return grabar_ ? evento::linea::lineaParaGrabar(linea()) : linea();
}

void ConduccionLinea::setGrabarTiempos(bool on) {
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

json ConduccionLinea::lineaTiemposGrabados() const {
    if (grabado_.is_object()) return grabado_;
    return {{"grabando", grabar_}, {"corriendo", corriendo()}, {"pasos", json::array()}};
}

std::string ConduccionLinea::aplicarTiemposGrabados() {
    if (!grabado_.is_object() || grabado_["ops"].empty())
        return QCoreApplication::translate("EventoViewModel", "No hay tiempos grabados para guardar.").toStdString();
    const json r = lineaEditar(grabado_["ops"], false);
    if (!r.value("ok", false)) return r.value("error", std::string());
    anotar(QCoreApplication::translate("EventoViewModel", "Los tiempos del ensayo quedaron como duraciones previstas"), "ok");
    grabado_ = nullptr;
    grabar_ = false;
    emit correChanged();
    return {};
}

void ConduccionLinea::descartarTiemposGrabados() {
    if (grabado_.is_null()) return;
    grabado_ = nullptr;
    emit correChanged();
}

// --- Armar desde plantillas y mirar las cámaras (linea_desde_plantilla, linea_camaras) ------------

json ConduccionLinea::lineaDesdePlantilla(const json& a, const std::string& salida, const json& guia, const json& ordenDelDia) {
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

json ConduccionLinea::lineaCamaras(const std::string& camara,
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

} // namespace ba::app
