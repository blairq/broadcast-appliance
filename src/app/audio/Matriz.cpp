// ============================================================================
// src/app/audio/Matriz.cpp — ver Matriz.hpp
// ============================================================================
#include "audio/Matriz.hpp"
#include <QCoreApplication>
#include "audio/AnilloAudio.hpp"
#include "audio/EquipoAudio.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QSaveFile>

#include "emision/BroadcastEngine.hpp"
#include "emision/FuentesApp.hpp"
#include "comun/HoraMonotonica.hpp"
#include "Motor.hpp"
#include "Prioridad.hpp"
#include "audio/GstVoz.hpp"
#include "platform/Ritmo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <set>
#include "ajustes/Ajustes.hpp"
#include "platform/Hilo.hpp"

namespace fs = std::filesystem;

namespace ba::app::audio {

namespace {

using anillo::AlDispositivo;
using anillo::kBloque;
using anillo::kColchon;
/// Lo que agrega un cruce a un bus virtual: el colchón del hilo de mezcla (a 48 kHz).
constexpr int kRetardoBusMs = static_cast<int>(kColchon * 1000 / 48000);
using anillo::Ring;

float dbALineal(double db) { return db <= -96.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
double linealADb(float x) { return x <= 1e-6f ? -100.0 : 20.0 * std::log10(static_cast<double>(x)); }

BroadcastEngine* motor() { return fuentes::motorApp(); }

/// Las pistas de una fuente como máscara (bit 0 = pista 1, el programa).
std::uint32_t mascara(const std::string& f) {
    std::uint32_t m = 0;
    if (BroadcastEngine* e = motor())
        for (int p : e->sourcePistas(f))
            if (p >= 1 && p <= 6) m |= 1u << (p - 1);
    return m;
}
void ponerMascara(const std::string& f, std::uint32_t m) {
    BroadcastEngine* e = motor();
    if (e == nullptr) return;
    std::vector<int> v;
    for (int i = 0; i < 6; ++i)
        if ((m & (1u << i)) != 0) v.push_back(i + 1);
    e->setSourcePistas(f, v);
}
/// La escucha FIJA (el cruce de la matriz, lo que se guarda); el PFL va aparte.
bool escucha(const std::string& f) {
    BroadcastEngine* e = motor();
    return e != nullptr && e->sourceEscuchaFija(f);
}
/// Lo que suena en los auriculares: la escucha fija o el PFL.
bool suenaEnEscucha(const std::string& f) {
    BroadcastEngine* e = motor();
    return e != nullptr && e->sourceMonitoring(f);
}
std::string enPfl() {
    BroadcastEngine* e = motor();
    return e != nullptr ? e->pfl() : std::string();
}
void setEscucha(const std::string& f, bool on) {
    if (BroadcastEngine* e = motor()) e->setSourceMonitoring(f, on);
}
} // namespace


// --- Estructuras -------------------------------------------------------------------

struct Matriz::Bus {
    std::string id, nombre, tipo;   // tipo: "salida" | "entrada"
    std::string dispositivo;        // salida: id del QAudioDevice
    bool entradaCreada = false;     // entrada: su entrada en el motor (lee la cola de `bavoz`)
    std::shared_ptr<Ring> aDispositivo;
    std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<AlDispositivo> io;
    std::shared_ptr<std::atomic<float>> pico = std::make_shared<std::atomic<float>>(0.0f);
};

struct Matriz::Toma {
    Ring ring;
    int toma = 0; ///< la del motor (escucharEntrada)
    std::string nombre;
};

struct Matriz::Plan {
    struct Aporte {
        std::shared_ptr<Toma> toma;
        float gl, gr; // ganancia por canal (nivel y pan)
    };
    struct BusPlan {
        std::string id;
        std::vector<Aporte> aportes;
        std::shared_ptr<Ring> aDispositivo;
        std::string entrada; ///< la entrada virtual (la cola de su `bavoz`), o vacío
        std::shared_ptr<std::atomic<float>> pico;
    };
    std::vector<BusPlan> buses;
};

// --- Ciclo de vida -------------------------------------------------------------------

Matriz::Matriz(fs::path dataRoot, QObject* parent) : QObject(parent), root_(std::move(dataRoot)) {
    for (int i = 0; i < kPistas; ++i)
        pistas_[i] = (i == 0 ? tr("Programa") : tr("Pista %1").arg(i + 1)).toStdString();
    equipo_ = std::make_unique<EquipoAudio>();
    // Se enchufó o desenchufó algo (o se ocultó): columnas, filas y plan de nuevo.
    connect(equipo_.get(), &EquipoAudio::cambio, this, [this]() {
        refrescar();
        armarPlan();
        usarEquipo();
        emit cambio();
    });
    guardarLuego_.setSingleShot(true);
    guardarLuego_.setInterval(600);
    connect(&guardarLuego_, &QTimer::timeout, this, [this]() { guardar(); });
    cargando_ = true;
    cargar();
    cargando_ = false;
    refrescar();
    connect(&timer_, &QTimer::timeout, this, [this]() { leerMedidores(); });
    // Un invitado que entra después: si tiene envíos, se le engancha la toma.
    connect(&lento_, &QTimer::timeout, this, [this]() {
        // Entradas que aparecieron o se fueron (al arrancar, el motor las crea después):
        // filas, tomas y lo que se abre del equipo, sin esperar a que alguien mire la matriz.
        if (motor() != nullptr && fuentesConAudio() != filas_) refrescar();
        bool falta = false;
        {
            std::lock_guard<std::mutex> l(tomasLock_);
            BroadcastEngine* eng = motor();
            for (const auto& [k, e] : envios_)
                if (!tomas_.contains(k.first)) falta = true;
            for (const auto& [n, t] : tomas_)
                if (eng == nullptr || eng->motor() == nullptr || !eng->motor()->tomaViva(t->toma)) falta = true;
        }
        if (falta) {
            sincronizarTomas();
            armarPlan();
        }
        // Las tarjetas «Entrada» de los racks pueden haber empezado (o dejado)
        // de usar una entrada de placa. Solo si cambió: abrir una captura toca
        // CoreAudio en este hilo, y con el audio del sistema trabado cada
        // reintento colgaba la app.
        if (usadasPorOtros_) {
            std::set<std::string> otros = usadasPorOtros_();
            if (otros != ultimosOtros_) {
                ultimosOtros_ = std::move(otros);
                usarEquipo();
            }
        }
    });
    lento_.start(std::max(500, ajustes::entero("audio.rescanEquiposMs", 3000)));
    hilo_ = std::thread([this]() { ba::platform::nombrarHilo("matriz"); bucleMezcla(); });
}

Matriz::~Matriz() {
    {
        std::lock_guard<std::mutex> g(planLock_);
        salir_ = true;
    }
    planCv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
    setActiva(false);
    BroadcastEngine* e = motor();
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        for (auto& [n, t] : tomas_)
            if (t->toma != 0 && e != nullptr && e->motor() != nullptr) e->motor()->dejarDeEscucharEntrada(t->toma);
        tomas_.clear();
    }
    for (auto& b : buses_)
        if (b->sink) b->sink->stop();
}

// --- Fuentes y buses -------------------------------------------------------------------

std::vector<std::string> Matriz::fuentesConAudio() const {
    std::vector<std::string> out;
    if (BroadcastEngine* e = motor()) out = e->audioSources();
    std::sort(out.begin(), out.end());
    return out;
}

Matriz::Bus* Matriz::buscar(const std::string& id) {
    for (auto& b : buses_)
        if (b->id == id) return b.get();
    return nullptr;
}
const Matriz::Bus* Matriz::buscar(const std::string& id) const {
    for (const auto& b : buses_)
        if (b->id == id) return b.get();
    return nullptr;
}

void Matriz::refrescar() {
    if (motor() == nullptr) return;
    const std::vector<std::string> antes = filas_;
    filas_ = fuentesConAudio();
    // Las que aparecieron reciben lo guardado; las que se fueron, al volver, otra vez.
    for (auto it = aplicadas_.begin(); it != aplicadas_.end();)
        it = std::find(filas_.begin(), filas_.end(), *it) == filas_.end() ? aplicadas_.erase(it) : std::next(it);
    for (const auto& f : filas_)
        if (!aplicadas_.contains(f)) aplicarGuardado(f);
    sincronizarTomas();
    armarPlan();
    // Las entradas de placa se abren según lo que las usa; al arrancar la matriz decide
    // antes de que existan en el motor (y quedaban cerradas con sus envíos guardados).
    if (filas_ != antes) usarEquipo();
    emit cambio();
}

QVariantList Matriz::filas() const {
    QVariantList out;
    std::set<std::string> virtuales;
    for (const auto& b : buses_)
        if (b->tipo == "entrada") virtuales.insert(b->nombre);
    for (const auto& f : filas_)
        out.push_back(QVariantMap{{"nombre", QString::fromStdString(f)}, {"virtual", virtuales.contains(f)}});
    return out;
}

namespace {
/// La salida de escucha (los auriculares del operador): la del motor.
QString nombreEscucha() {
    BroadcastEngine* e = motor();
    // La foto del motor: no espera su candado (ver Motor::instantanea).
    const auto foto = e != nullptr && e->motor() != nullptr ? e->motor()->instantanea() : nullptr;
    const nlohmann::json st = foto ? *foto : nlohmann::json();
    const nlohmann::json es = st.is_object() ? st.value("escucha", nlohmann::json::object()) : nlohmann::json::object();
    const std::string d = es.is_object() ? es.value("dispositivo", std::string()) : std::string();
    return d.empty() || d == "default" ? QCoreApplication::translate("matriz", "la salida del sistema") : QString::fromStdString(d);
}
} // namespace

QVariantList Matriz::columnas() const {
    QVariantList out;
    for (int i = 0; i < kPistas; ++i)
        out.push_back(QVariantMap{
            {"id", QString("pista%1").arg(i + 1)},
            {"nombre", QString::fromStdString(pistas_[i])},
            {"tipo", "pista"},
            {"indice", i},
            {"uso", i == 0 ? tr("emisión y grabación") : QString::fromStdString(usoPista_[i])}});
    // Los auriculares del operador (Configuración → Voz del sistema → salida de auriculares).
    out.push_back(QVariantMap{{"id", "escucha"},
                              {"nombre", tr("Escucha")},
                              {"tipo", "escucha"},
                              {"dispositivoNombre", nombreEscucha()}});
    // Las salidas físicas del equipo: una columna por par de canales.
    for (const auto& c : equipo_->columnas())
        out.push_back(QVariantMap{{"id", QString::fromStdString(c.id)},
                                  {"nombre", QString::fromStdString(c.nombre)},
                                  {"tipo", "fisica"},
                                  {"dispositivoNombre", QString::fromStdString(c.dispositivo)}});
    for (const auto& b : buses_) {
        QString disp;
        if (b->tipo == "salida")
            for (const QAudioDevice& d : QMediaDevices::audioOutputs())
                if (QString::fromLatin1(d.id()) == QString::fromStdString(b->dispositivo))
                    disp = d.description();
        out.push_back(QVariantMap{{"id", QString::fromStdString(b->id)},
                                  {"nombre", QString::fromStdString(b->nombre)},
                                  {"tipo", QString::fromStdString(b->tipo)},
                                  {"dispositivo", QString::fromStdString(b->dispositivo)},
                                  {"dispositivoNombre", disp}});
    }
    return out;
}

QVariantMap Matriz::cruces() const {
    QVariantMap out;
    // Pistas nativas: a qué pistas del motor va cada fuente.
    for (const auto& f : filas_) {
        const std::uint32_t mx = mascara(f);
        const bool oye = escucha(f);
        const bool pfl = enPfl() == f;
        if (oye || pfl) out[QString::fromStdString(f) + "|escucha"] = QVariantMap{{"activo", oye}, {"pfl", pfl}};
        for (int i = 0; i < kPistas; ++i)
            if ((mx & (1u << i)) != 0)
                out[QString::fromStdString(f) + QString("|pista%1").arg(i + 1)] =
                    QVariantMap{{"activo", true}};
    }
    for (const auto& [k, e] : envios_)
        out[QString::fromStdString(k.first + "|" + k.second)] = QVariantMap{{"activo", true},
                                                                            {"ajustes", e.ajustes},
                                                                            {"nivelDb", e.nivelDb},
                                                                            {"pan", e.pan},
                                                                            {"mute", e.mute},
                                                                            {"retardo_ms", kRetardoBusMs}};
    return out;
}

double Matriz::picoDbDe(const std::string& columna) const {
    for (const auto& b : buses_)
        if (b->id == columna) return linealADb(b->pico->load());
    const auto it = picosEquipo_.find(columna);
    return it != picosEquipo_.end() ? linealADb(it->second->load()) : -100.0;
}

QStringList Matriz::enviosDe(const QString& fuente) const {
    QStringList out;
    const std::string f = fuente.toStdString();
    const std::uint32_t mx = mascara(f);
    for (int i = 0; i < kPistas; ++i)
        if ((mx & (1u << i)) != 0) out << QString("pista%1").arg(i + 1);
    for (const auto& [k, e] : envios_)
        if (k.first == f) out << QString::fromStdString(k.second);
    return out;
}

QVariantList Matriz::dispositivos() const {
    QVariantList out;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        out.push_back(QVariantMap{{"id", QString::fromLatin1(d.id())}, {"nombre", d.description()}});
    return out;
}

void Matriz::setUsoPista(int indice, const std::string& uso) {
    if (indice < 0 || indice >= kPistas || usoPista_[indice] == uso) return;
    usoPista_[indice] = uso;
    emit cambio();
}

// --- Cruces ---------------------------------------------------------------------------

bool Matriz::haceLazo(const std::string& fuente, const std::string& bus) const {
    // Grafo de buses: A -> B si la entrada virtual de A manda a B.
    auto busDeEntrada = [this](const std::string& nombreFuente) -> std::string {
        for (const auto& b : buses_)
            if (b->tipo == "entrada" && b->nombre == nombreFuente) return b->id;
        return {};
    };
    const std::string origen = busDeEntrada(fuente);
    if (origen.empty()) return false; // una fuente común no arma lazos
    if (origen == bus) return true;
    // ¿Desde `bus` se llega a `origen`?
    std::set<std::string> vistos;
    std::vector<std::string> pila{bus};
    while (!pila.empty()) {
        const std::string b = pila.back();
        pila.pop_back();
        if (b == origen) return true;
        if (!vistos.insert(b).second) continue;
        const Bus* bb = buscar(b);
        if (bb == nullptr || bb->tipo != "entrada") continue;
        for (const auto& [k, e] : envios_)
            if (k.first == bb->nombre) pila.push_back(k.second);
    }
    return false;
}

QString Matriz::ponerPfl(const QString& fuenteQ) {
    BroadcastEngine* e = motor();
    if (e == nullptr) return tr("el motor no está listo");
    const std::string pedida = fuenteQ.toStdString();
    if (!pedida.empty() && !e->sourceExists(pedida)) return tr("No existe la fuente de audio «%1».").arg(fuenteQ);
    if (!e->setPfl(pedida)) return QString::fromStdString(e->lastError());
    // Una entrada de placa en PFL tiene que estar abierta (y la que salió, quizás no).
    // El cambio() lo trae el aviso del motor (avisarFuente). No se guarda: el PFL es de un rato.
    usarEquipo();
    return {};
}

void Matriz::avisarFuente(const std::string& fuente, const char* que) {
    bool primero = false;
    {
        std::lock_guard<std::mutex> l(avisosM_);
        primero = avisos_.empty();
        avisos_[fuente].insert(que);
    }
    if (primero) QMetaObject::invokeMethod(this, [this]() { atenderAvisos(); }, Qt::QueuedConnection);
}

void Matriz::atenderAvisos() {
    std::map<std::string, std::set<std::string>> a;
    {
        std::lock_guard<std::mutex> l(avisosM_);
        a.swap(avisos_);
    }
    bool ruteo = false, anotada = false;
    for (const auto& [f, ques] : a) {
        if (ques.contains("alta")) {
            aplicadas_.erase(f); // nació o se rehízo: con lo de fábrica, hasta que se le ponga lo guardado
            // Solo las de audio (las de video no tienen canal); si todavía no se sabe que
            // tiene audio, la recibe refrescar cuando aparece en las filas.
            const auto con = fuentesConAudio();
            if (std::find(con.begin(), con.end(), f) != con.end()) aplicarGuardado(f);
            ruteo = true;
            continue;
        }
        // Lo de una fuente que todavía no recibió lo guardado son los valores con que
        // nace: no se anotan (pisarían lo guardado).
        if (aplicadas_.contains(f)) {
            anotar(f);
            anotada = true;
        }
        if (ques.contains("escucha") || ques.contains("pistas")) ruteo = true;
    }
    if (anotada) guardarLuego_.start();
    if (ruteo) emit cambio();
}

void Matriz::anotar(const std::string& f) const {
    BroadcastEngine* e = motor();
    if (e == nullptr || !e->sourceExists(f)) return;
    Canal& c = canales_[f];
    c.pistas = mascara(f);
    c.escucha = escucha(f);
    c.volumen = e->sourceVolume(f);
    c.mute = e->sourceMuted(f);
    c.retardoMs = e->sourceSyncOffset(f);
}

void Matriz::aplicarGuardado(const std::string& f) {
    BroadcastEngine* e = motor();
    if (e == nullptr || !e->sourceExists(f)) return;
    if (const auto it = canales_.find(f); it != canales_.end()) {
        const Canal c = it->second; // copia: los avisos de lo que se pone no la tocan (todavía no está aplicada)
        if (c.pistas) ponerMascara(f, *c.pistas & 0x3F);
        if (c.escucha) setEscucha(f, *c.escucha);
        if (c.volumen) e->setSourceVolume(f, *c.volumen);
        if (c.mute) e->setSourceMuted(f, *c.mute);
        if (c.retardoMs) e->setSourceSyncOffset(f, *c.retardoMs);
    }
    aplicadas_.insert(f);
    anotar(f); // lo que no estaba guardado (una fuente nueva) queda con lo suyo
}

QString Matriz::setCruce(const QString& fuenteQ, const QString& busQ, bool activo) {
    const std::string fuente = fuenteQ.toStdString(), bus = busQ.toStdString();
    if (bus.rfind("pista", 0) == 0) {
        const int i = std::atoi(bus.c_str() + 5) - 1;
        if (i < 0 || i >= kPistas) return tr("No existe esa pista.");
        if (motor() == nullptr || !motor()->sourceExists(fuente)) return tr("No existe la fuente «%1».").arg(fuenteQ);
        std::uint32_t mx = mascara(fuente);
        mx = activo ? (mx | (1u << i)) : (mx & ~(1u << i));
        ponerMascara(fuente, mx);
        usarEquipo();
        guardar();
        emit cambio();
        return {};
    }
    if (bus == "escucha") {
        if (motor() == nullptr || !motor()->sourceExists(fuente)) return tr("No existe la fuente «%1».").arg(fuenteQ);
        setEscucha(fuente, activo);
        usarEquipo();
        guardar();
        emit cambio();
        return {};
    }
    if (buscar(bus) == nullptr && !equipo_->anilloDe(bus)) return tr("No existe el bus.");
    const auto k = std::make_pair(fuente, bus);
    if (activo) {
        if (haceLazo(fuente, bus)) return tr("Eso arma un lazo: la entrada virtual volvería a su propio bus.");
        envios_.try_emplace(k);
    } else {
        envios_.erase(k);
    }
    sincronizarTomas();
    armarPlan();
    usarEquipo();
    guardar();
    emit cambio();
    return {};
}

QString Matriz::alternar(const QString& fuente, const QString& bus) {
    const QVariantMap c = cruces().value(fuente + "|" + bus).toMap();
    return setCruce(fuente, bus, !c.value("activo").toBool());
}

void Matriz::setAjustes(const QString& fuente, const QString& bus, bool on) {
    const auto it = envios_.find({fuente.toStdString(), bus.toStdString()});
    if (it == envios_.end()) return;
    it->second.ajustes = on;
    if (!on) { // apagar los ajustes = volver a 0 dB, centro, sin mute
        it->second = Envio{};
        armarPlan();
    }
    guardar();
    emit cambio();
}

void Matriz::ajustar(const QString& fuente, const QString& bus, double nivelDb, double pan, bool mute) {
    const auto it = envios_.find({fuente.toStdString(), bus.toStdString()});
    if (it == envios_.end()) return;
    Envio& e = it->second;
    e.ajustes = true;
    e.nivelDb = std::clamp(nivelDb, -96.0, 12.0);
    e.ganancia = dbALineal(e.nivelDb);
    e.pan = static_cast<float>(std::clamp(pan, -1.0, 1.0));
    e.mute = mute;
    armarPlan();
    guardar();
    emit cambio();
}

// --- Buses virtuales --------------------------------------------------------------------

QString Matriz::agregarSalidaVirtual(const QString& nombre, const QString& dispositivo) {
    if (buses_.size() >= kMaxVirtuales) return tr("No entran más buses virtuales.");
    auto b = std::make_unique<Bus>();
    int n = 1;
    while (buscar("salida" + std::to_string(n)) != nullptr)
        ++n;
    b->id = "salida" + std::to_string(n);
    b->nombre =
        nombre.trimmed().isEmpty() ? tr("Salida virtual %1").arg(n).toStdString() : nombre.trimmed().toStdString();
    b->tipo = "salida";
    buses_.push_back(std::move(b));
    // Con un dispositivo que no anda, no queda un bus mudo guardado como si estuviera bien.
    if (const QString e = setDispositivo(QString::fromStdString(buses_.back()->id), dispositivo); !e.isEmpty()) {
        buses_.pop_back();
        armarPlan(); // setDispositivo ya lo había armado con el bus adentro
        emit cambio();
        return e;
    }
    armarPlan();
    guardar();
    emit cambio();
    return {};
}

QString Matriz::agregarEntradaVirtual(const QString& nombre) {
    if (buses_.size() >= kMaxVirtuales) return tr("No entran más buses virtuales.");
    if (motor() == nullptr) return tr("El motor de audio no está arriba.");
    int n = 1;
    while (buscar("entrada" + std::to_string(n)) != nullptr)
        ++n;
    std::string nom =
        nombre.trimmed().isEmpty() ? tr("Entrada virtual %1").arg(n).toStdString() : nombre.trimmed().toStdString();
    if (motor()->sourceExists(nom)) return tr("Ya hay una fuente con ese nombre.");
    auto b = std::make_unique<Bus>();
    b->id = "entrada" + std::to_string(n);
    b->nombre = nom;
    b->tipo = "entrada";
    if (!crearEntradaVirtual(nom)) return tr("No se pudo crear la fuente.");
    b->entradaCreada = true;
    buses_.push_back(std::move(b));
    refrescar();
    guardar();
    return {};
}

bool Matriz::crearEntradaVirtual(const std::string& nom) {
    std::string esc;
    for (char c : nom) {
        if (c == '\\' || c == '"') esc += '\\';
        esc += c;
    }
    // Una entrada global del motor que lee la mezcla del bus (la cola de su
    // `bavoz`). Arranca sin pistas: se decide en la matriz a dónde va (para
    // que no aparezca sola en el programa).
    const nlohmann::json aj{{"pipeline", ""},
                            {"pipeline_audio", "bavoz voz=\"" + esc + "\""},
                            {"audio", true},
                            {"normalizar", false},
                            {"estudio", false},
                            {"virtual", true},
                            {"voz", nom}};
    return fuentes::crearGlobal(nom, "ba_gst_fuente", aj, false, {});
}

QString Matriz::quitarBus(const QString& idQ) {
    const std::string id = idQ.toStdString();
    for (auto it = buses_.begin(); it != buses_.end(); ++it) {
        if ((*it)->id != id) continue;
        Bus& b = **it;
        for (auto e = envios_.begin(); e != envios_.end();)
            e = (e->first.second == id || (b.tipo == "entrada" && e->first.first == b.nombre))
                    ? envios_.erase(e)
                    : std::next(e);
        if (b.sink) b.sink->stop();
        std::unique_ptr<Bus> muerto = std::move(*it);
        buses_.erase(it);
        armarPlan(); // primero sale de la mezcla
        if (muerto->entradaCreada) {
            gstvoz::vaciar(muerto->nombre);
            fuentes::quitarGlobal(muerto->nombre);
            muerto->entradaCreada = false;
        }
        refrescar();
        guardar();
        return {};
    }
    return tr("No existe el bus.");
}

QString Matriz::renombrar(const QString& idQ, const QString& nombreQ) {
    const std::string id = idQ.toStdString(), nombre = nombreQ.trimmed().toStdString();
    if (nombre.empty()) return tr("Falta el nombre.");
    if (id.rfind("pista", 0) == 0) {
        const int i = std::atoi(id.c_str() + 5) - 1;
        if (i < 0 || i >= kPistas) return tr("No existe esa pista.");
        pistas_[i] = nombre;
    } else {
        Bus* b = buscar(id);
        if (b == nullptr) return tr("No existe el bus.");
        if (b->tipo == "entrada") {
            if (motor() != nullptr && motor()->sourceExists(nombre)) return tr("Ya hay una fuente con ese nombre.");
            // Los envíos que salen de esta entrada cambian de nombre con ella.
            std::map<std::pair<std::string, std::string>, Envio> nuevos;
            for (auto& [k, e] : envios_)
                nuevos[{k.first == b->nombre ? nombre : k.first, k.second}] = e;
            envios_ = std::move(nuevos);
            // En el motor la entrada ES su nombre: se arma una nueva que lee la cola nueva.
            gstvoz::vaciar(b->nombre);
            fuentes::quitarGlobal(b->nombre);
            b->entradaCreada = crearEntradaVirtual(nombre);
        }
        b->nombre = nombre;
    }
    refrescar();
    guardar();
    return {};
}

QString Matriz::setDispositivo(const QString& idQ, const QString& dispositivo) {
    Bus* b = buscar(idQ.toStdString());
    if (b == nullptr || b->tipo != "salida") return tr("No es una salida virtual.");
    if (b->sink) b->sink->stop();
    b->sink.reset();
    b->io.reset();
    b->dispositivo = dispositivo.toStdString();
    QAudioDevice dev;
    for (const QAudioDevice& d : QMediaDevices::audioOutputs())
        if (QString::fromLatin1(d.id()) == dispositivo) dev = d;
    if (dev.isNull()) {
        b->aDispositivo.reset();
        armarPlan();
        emit cambio();
        return dispositivo.isEmpty() ? QString() : tr("Ese dispositivo ya no está.");
    }
    QAudioFormat f;
    f.setSampleRate(48000);
    f.setChannelCount(2);
    f.setSampleFormat(QAudioFormat::Float);
    if (!dev.isFormatSupported(f)) return tr("El dispositivo no acepta audio estéreo a 48 kHz.");
    b->aDispositivo = std::make_shared<Ring>();
    b->io = std::make_unique<AlDispositivo>(b->aDispositivo);
    b->sink = std::make_unique<QAudioSink>(dev, f);
    b->sink->setBufferSize(static_cast<qsizetype>(kColchon * 2 * sizeof(float) * 2));
    b->sink->start(b->io.get());
    armarPlan();
    guardar();
    emit cambio();
    return {};
}

// --- El audio ------------------------------------------------------------------------



void Matriz::sincronizarTomas() {
    // Una toma por fuente que manda a algún bus virtual; las demás no cuestan nada.
    std::set<std::string> hacen;
    for (const auto& [k, e] : envios_)
        hacen.insert(k.first);
    BroadcastEngine* eng = motor();
    motor::Motor* m = eng != nullptr ? eng->motor() : nullptr;
    std::vector<int> soltar;
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        for (auto it = tomas_.begin(); it != tomas_.end();) {
            // La fuente se fue (o el motor se rearmó y la toma ya no anda): se suelta.
            if (hacen.contains(it->first) && eng != nullptr && eng->sourceExists(it->first) && m != nullptr &&
                m->tomaViva(it->second->toma)) {
                ++it;
                continue;
            }
            if (it->second->toma != 0) soltar.push_back(it->second->toma);
            it = tomas_.erase(it);
        }
    }
    // Fuera del lock: soltar espera a que termine el callback (que no lo toma, pero igual).
    for (int t : soltar)
        if (m != nullptr) m->dejarDeEscucharEntrada(t);
    if (m == nullptr) return;
    for (const auto& n : hacen) {
        {
            std::lock_guard<std::mutex> l(tomasLock_);
            if (tomas_.contains(n)) continue;
        }
        if (!eng->sourceExists(n)) continue; // todavía no existe (un invitado que no llegó)
        auto t = std::make_shared<Toma>();
        t->nombre = n;
        // El audio de la entrada después de su cadena, estéreo intercalado a 48 kHz.
        std::weak_ptr<Toma> w = t;
        t->toma = m->escucharEntrada(n, [w](const float* d, std::size_t cuadros) {
            auto x = w.lock();
            if (!x || cuadros == 0) return;
            thread_local std::vector<float> l, r;
            l.resize(cuadros);
            r.resize(cuadros);
            for (std::size_t i = 0; i < cuadros; ++i) {
                l[i] = d[2 * i];
                r[i] = d[2 * i + 1];
            }
            x->ring.push(l.data(), r.data(), static_cast<std::uint32_t>(cuadros));
        });
        if (t->toma == 0) continue;
        std::lock_guard<std::mutex> l(tomasLock_);
        tomas_[n] = std::move(t);
    }
}

void Matriz::usarEquipo() {
    // Columnas físicas que reciben algo: su dispositivo se abre.
    std::set<std::string> columnas, entradas;
    for (const auto& [k, e] : envios_)
        if (k.second.rfind("fis:", 0) == 0 && !e.mute) columnas.insert(k.second);
    equipo_->usarColumnas(columnas);
    // Entradas físicas que se usan (una pista, la escucha, un envío o la
    // tarjeta «Entrada» de un rack): su captura se abre.
    const std::set<std::string> otros = usadasPorOtros_ ? usadasPorOtros_() : std::set<std::string>{};
    for (const auto& f : filas_) {
        if (!equipo_->esEntradaFisica(f)) continue;
        bool usa = mascara(f) != 0 || suenaEnEscucha(f);
        for (const auto& [k, e] : envios_)
            if (k.first == f) usa = true;
        if (otros.contains(f)) usa = true;
        if (usa) entradas.insert(f);
    }
    equipo_->usarEntradas(entradas);
}

bool Matriz::mostrarEquipo() const { return equipo_->mostrar(); }
void Matriz::setMostrarEquipo(bool on) {
    equipo_->setMostrar(on);
    guardar();
}
QVariantList Matriz::equipo() const { return equipo_->dispositivos(); }
void Matriz::setPistasPlegadas(bool on) {
    if (pistasPlegadas_ == on) return;
    pistasPlegadas_ = on;
    guardar();
    emit cambio();
}
void Matriz::setVisibleDispositivo(const QString& id, bool visible) {
    equipo_->setVisible(id.toStdString(), visible);
    guardar();
}

void Matriz::armarPlan() {
    auto p = std::make_shared<Plan>();
    std::lock_guard<std::mutex> l(tomasLock_);
    for (const auto& b : buses_) {
        Plan::BusPlan bp;
        bp.id = b->id;
        bp.aDispositivo = b->aDispositivo;
        if (b->tipo == "entrada" && b->entradaCreada) bp.entrada = b->nombre;
        bp.pico = b->pico;
        for (const auto& [k, e] : envios_) {
            if (k.second != b->id || e.mute) continue;
            const auto t = tomas_.find(k.first);
            if (t == tomas_.end()) continue;
            // Pan de potencia constante.
            const float a = (e.pan + 1.0f) * 0.25f * 3.14159265f;
            const float gl = e.pan == 0.0f ? 1.0f : std::cos(a) * 1.41421356f;
            const float gr = e.pan == 0.0f ? 1.0f : std::sin(a) * 1.41421356f;
            bp.aportes.push_back({t->second, e.ganancia * gl, e.ganancia * gr});
        }
        p->buses.push_back(std::move(bp));
    }
    // Las salidas físicas, igual que una salida virtual: al anillo de su par de canales.
    for (const auto& c : equipo_->columnas()) {
        Plan::BusPlan bp;
        bp.id = c.id;
        bp.aDispositivo = equipo_->anilloDe(c.id);
        auto& pk = picosEquipo_[c.id];
        if (!pk) pk = std::make_shared<std::atomic<float>>(0.0f);
        bp.pico = pk;
        for (const auto& [k, e] : envios_) {
            if (k.second != c.id || e.mute) continue;
            const auto t = tomas_.find(k.first);
            if (t == tomas_.end()) continue;
            const float a = (e.pan + 1.0f) * 0.25f * 3.14159265f;
            const float gl = e.pan == 0.0f ? 1.0f : std::cos(a) * 1.41421356f;
            const float gr = e.pan == 0.0f ? 1.0f : std::sin(a) * 1.41421356f;
            bp.aportes.push_back({t->second, e.ganancia * gl, e.ganancia * gr});
        }
        if (!bp.aportes.empty()) p->buses.push_back(std::move(bp));
    }
    {
        std::lock_guard<std::mutex> g(planLock_);
        plan_ = std::move(p);
    }
    planCv_.notify_all();
}

void Matriz::bucleMezcla() {
    motor::prioridadDelHilo(motor::ClaseHilo::Audio); // mezcla con plazo de 10 ms: tiempo real
    std::vector<float> al(kBloque), ar(kBloque), ml(kBloque), mr(kBloque);
    // Un bloque cada 10 ms con plazo absoluto (en Windows, con el temporizador fino:
    // platform/Ritmo); un atraso de hasta 100 ms se recupera, más ya no.
    ba::platform::Ritmo ritmo{std::chrono::milliseconds(10), std::chrono::milliseconds(100)};
    while (!salir_) {
        // Sin ningún bus que mezclar (lo común: nadie usa la matriz), espera a que haya
        // uno en vez de despertarse cada 10 ms para nada.
        {
            std::unique_lock<std::mutex> g(planLock_);
            if (!plan_ || plan_->buses.empty()) {
                planCv_.wait(g, [this]() { return salir_.load() || (plan_ && !plan_->buses.empty()); });
                ritmo.reiniciar();
                if (salir_) break;
            }
        }
        ritmo.esperar();
        std::shared_ptr<const Plan> p;
        {
            std::lock_guard<std::mutex> g(planLock_);
            p = plan_;
        }
        if (!p) continue;
        // Cada toma se lee UNA vez por bloque aunque vaya a varios buses.
        std::map<Toma*, std::pair<std::vector<float>, std::vector<float>>> leido;
        for (const auto& b : p->buses)
            for (const auto& a : b.aportes)
                if (!leido.contains(a.toma.get())) {
                    auto& par = leido[a.toma.get()];
                    par.first.assign(kBloque, 0.0f);
                    par.second.assign(kBloque, 0.0f);
                    a.toma->ring.pop(par.first.data(), par.second.data(), kBloque);
                }
        for (const auto& b : p->buses) {
            std::fill(ml.begin(), ml.end(), 0.0f);
            std::fill(mr.begin(), mr.end(), 0.0f);
            for (const auto& a : b.aportes) {
                const auto& par = leido[a.toma.get()];
                for (std::uint32_t i = 0; i < kBloque; ++i) {
                    ml[i] += par.first[i] * a.gl;
                    mr[i] += par.second[i] * a.gr;
                }
            }
            float pk = 0.0f;
            for (std::uint32_t i = 0; i < kBloque; ++i)
                pk = std::max({pk, std::fabs(ml[i]), std::fabs(mr[i])});
            b.pico->store(std::max(pk, b.pico->load() * 0.85f));
            if (b.aDispositivo) b.aDispositivo->push(ml.data(), mr.data(), kBloque);
            if (!b.entrada.empty()) {
                // A la cola de la entrada virtual (estéreo intercalado), un poco adelante de ahora.
                thread_local std::vector<float> st;
                st.resize(static_cast<std::size_t>(kBloque) * 2);
                for (std::uint32_t i = 0; i < kBloque; ++i) {
                    st[2 * i] = ml[i];
                    st[2 * i + 1] = mr[i];
                }
                gstvoz::encolar(b.entrada, st.data(), kBloque, 48000, ba::app::ahoraNs() + 20'000'000ULL, 2);
            }
        }
    }
}

// --- Medidores -----------------------------------------------------------------------


void Matriz::setActiva(bool on) {
    if (on == activa_) return;
    activa_ = on;
    if (on) {
        refrescar();
        timer_.start(50);
        return;
    }
    timer_.stop();
}

void Matriz::usar(QObject* vista, bool si) {
    if (vista == nullptr) return;
    if (si && !vistas_.contains(vista)) {
        vistas_.insert(vista);
        connect(vista, &QObject::destroyed, this, [this](QObject* o) {
            vistas_.remove(o);
            setActiva(!vistas_.isEmpty());
        });
    } else if (!si) {
        vistas_.remove(vista);
    }
    setActiva(!vistas_.isEmpty());
}

void Matriz::leerMedidores() {
    BroadcastEngine* eng = motor();
    if (eng == nullptr) return;
    // El pico de cada fila: lo que ENTREGA la entrada (después de sus filtros y su rack),
    // vaya o no a alguna salida. Antes era su nivel en el programa: sin la pista 1, una
    // entrada con señal quedaba en −100 y parecía muda. Lo mide el motor.
    std::map<std::string, double> picos;
    for (const auto& f : filas_)
        picos[f] = eng->sourceOutputPeakDb(f);
    QVariantMap m;
    for (const auto& [n, p] : picos)
        m["fila:" + QString::fromStdString(n)] = p;
    for (const auto& b : buses_)
        m["bus:" + QString::fromStdString(b->id)] = linealADb(b->pico->load());
    for (const auto& [id, pk] : picosEquipo_)
        m["bus:" + QString::fromStdString(id)] = linealADb(pk->load());
    // Pistas nativas: el máximo de lo que les llega (sin tocar el audio).
    for (int i = 0; i < kPistas; ++i) {
        double mx = -100.0;
        for (const auto& f : filas_) {
            const bool va = (mascara(f) & (1u << i)) != 0 && !eng->sourceMuted(f);
            if (va) mx = std::max(mx, picos[f]);
        }
        m[QString("bus:pista%1").arg(i + 1)] = mx;
    }
    medidores_ = m;
    emit medidoresCambio();
}

// --- Guardar / cargar ----------------------------------------------------------------

Matriz::json Matriz::estado() const {
    json pistas = json::array(), buses = json::array(), envios = json::array(), filas = json::array();
    for (int i = 0; i < kPistas; ++i)
        pistas.push_back(
            {{"id", "pista" + std::to_string(i + 1)}, {"nombre", pistas_[i]}, {"uso", usoPista_[i]}});
    for (const auto& b : buses_)
        buses.push_back(
            {{"id", b->id}, {"nombre", b->nombre}, {"tipo", b->tipo}, {"dispositivo", b->dispositivo}});
    for (const auto& [k, e] : envios_)
        envios.push_back({{"fuente", k.first},
                          {"bus", k.second},
                          {"nivelDb", e.nivelDb},
                          {"pan", e.pan},
                          {"mute", e.mute},
                          {"ajustes", e.ajustes},
                          // Lo que suma pasar por la mezcla propia (las pistas nativas: 0).
                          {"retardo_ms", kRetardoBusMs}});
    for (const auto& f : filas_) {
        json pis = json::array();
        const bool oye = escucha(f);
        const std::uint32_t mx = mascara(f);
        for (int i = 0; i < kPistas; ++i)
            if ((mx & (1u << i)) != 0) pis.push_back("pista" + std::to_string(i + 1));
        filas.push_back({{"fuente", f}, {"pistas", pis}, {"escucha", oye}, {"pfl", enPfl() == f}});
    }
    return {{"pistas", pistas},
            {"escucha", {{"id", "escucha"}, {"dispositivo", nombreEscucha().toStdString()}}},
            {"equipo",
             [this] {
                 json eq = json::array();
                 for (const auto& c : equipo_->columnas())
                     eq.push_back({{"id", c.id}, {"nombre", c.nombre}, {"dispositivo", c.dispositivo}});
                 return json{{"mostrar", equipo_->mostrar()}, {"salidas", eq}};
             }()},
            {"buses", buses},
            {"envios", envios},
            {"fuentes", filas}};
}

Matriz::json Matriz::diagnostico() const {
    json filas = json::object(), buses = json::object(), tomas = json::array();
    if (BroadcastEngine* eng = motor())
        for (const auto& f : filas_) filas[f] = std::round(eng->sourceOutputPeakDb(f) * 10) / 10;
    for (const auto& b : buses_) buses[b->id] = std::round(linealADb(b->pico->load()) * 10) / 10;
    for (const auto& [id, pk] : picosEquipo_) buses[id] = std::round(linealADb(pk->load()) * 10) / 10;
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        for (const auto& [n, t] : tomas_) tomas.push_back(n);
    }
    return {{"filas_db", filas}, {"buses_db", buses}, {"tomas", tomas}, {"equipo", equipo_->estadoDispositivos()}};
}

void Matriz::guardar() const {
    if (cargando_) return;
    json j = estado();
    j.erase("fuentes");
    // El audio de cada fuente: las que están se anotan del motor; las que no (una
    // placa desenchufada) quedan como estaban guardadas.
    for (const auto& f : aplicadas_) anotar(f);
    json canales = json::object();
    for (const auto& [f, c] : canales_) {
        json k = json::object();
        if (c.pistas) k["pistas"] = *c.pistas;
        if (c.escucha) k["escucha"] = *c.escucha;
        if (c.volumen) k["volumen"] = *c.volumen;
        if (c.mute) k["mute"] = *c.mute;
        if (c.retardoMs) k["retardo_ms"] = *c.retardoMs;
        canales[f] = k;
    }
    j["canales"] = canales;
    j["equipo"] = equipo_->guardado();
    j["pistasPlegadas"] = pistasPlegadas_;
    const fs::path p = root_ / "Config" / "audio-matriz.json";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    // Escribir aparte y reemplazar: un corte a mitad no deja el archivo trunco (y la
    // próxima carga no lo ignora y lo pisa con la matriz vacía).
    QSaveFile out(QString::fromStdString(p.string()));
    if (!out.open(QIODevice::WriteOnly)) return;
    out.write(QByteArray::fromStdString(j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace)));
    out.commit();
}

void Matriz::cargar() {
    std::ifstream in(root_ / "Config" / "audio-matriz.json");
    if (!in) return;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    for (const auto& p : j.value("pistas", json::array())) {
        const std::string id = p.value("id", std::string());
        const int i = id.rfind("pista", 0) == 0 ? std::atoi(id.c_str() + 5) - 1 : -1;
        if (i >= 0 && i < kPistas) pistas_[i] = p.value("nombre", pistas_[i]);
    }
    if (motor() == nullptr) return;
    for (const auto& b : j.value("buses", json::array())) {
        const std::string tipo = b.value("tipo", std::string());
        if (tipo == "salida") {
            agregarSalidaVirtual(QString::fromStdString(b.value("nombre", std::string())),
                                 QString::fromStdString(b.value("dispositivo", std::string())));
            buses_.back()->id = b.value("id", buses_.back()->id);
        } else if (tipo == "entrada") {
            if (agregarEntradaVirtual(QString::fromStdString(b.value("nombre", std::string()))).isEmpty())
                buses_.back()->id = b.value("id", buses_.back()->id);
        }
    }
    for (const auto& e : j.value("envios", json::array())) {
        Envio v;
        v.nivelDb = e.value("nivelDb", 0.0);
        v.ganancia = dbALineal(v.nivelDb);
        v.pan = e.value("pan", 0.0f);
        v.mute = e.value("mute", false);
        v.ajustes = e.value("ajustes", false);
        envios_[{e.value("fuente", std::string()), e.value("bus", std::string())}] = v;
    }
    // El audio de cada fuente: se le pone cuando aparece (refrescar, o el aviso de alta del motor).
    for (const auto obj_ = j.value("canales", json::object()); const auto& [fuente, k] : obj_.items()) {
        Canal& c = canales_[fuente];
        if (k.contains("pistas") && k["pistas"].is_number()) c.pistas = k["pistas"].get<std::uint32_t>();
        if (k.contains("escucha") && k["escucha"].is_boolean()) c.escucha = k["escucha"].get<bool>();
        if (k.contains("volumen") && k["volumen"].is_number()) c.volumen = k["volumen"].get<float>();
        if (k.contains("mute") && k["mute"].is_boolean()) c.mute = k["mute"].get<bool>();
        if (k.contains("retardo_ms") && k["retardo_ms"].is_number()) c.retardoMs = k["retardo_ms"].get<int>();
    }
    // Los archivos de antes («mezcladores» y «escucha» sueltos).
    for (const auto obj_ = j.value("mezcladores", json::object()); const auto& [fuente, mx] : obj_.items())
        if (mx.is_number() && !canales_[fuente].pistas) canales_[fuente].pistas = mx.get<std::uint32_t>();
    for (const auto obj_ = j.value("escucha", json::object()); const auto& [fuente, on] : obj_.items())
        if (on.is_boolean() && !canales_[fuente].escucha) canales_[fuente].escucha = on.get<bool>();
    // El equipo crea las fuentes de las entradas físicas (reciben lo suyo al aparecer).
    equipo_->cargar(j.value("equipo", json::object()));
    pistasPlegadas_ = j.value("pistasPlegadas", true);
}

} // namespace ba::app::audio
