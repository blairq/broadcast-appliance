// ============================================================================
// src/app/audio/Matriz.cpp — ver Matriz.hpp
// ============================================================================
#include "audio/Matriz.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include <QCoreApplication>
#include "audio/RingEstereo.hpp"
#include "audio/EquipoAudio.hpp"

#include <QAudioDevice>
#include <QIODevice>
#include <QMediaDevices>
#include <QSaveFile>

#include "emision/BroadcastEngine.hpp"
#include "emision/FuentesApp.hpp"
#include "Motor.hpp"
#include "Piezas.hpp" // Vigia: las placas de salida
#include "Prioridad.hpp"
#include "platform/Ritmo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include "ajustes/Ajustes.hpp"
#include "platform/Hilo.hpp"

namespace fs = std::filesystem;

namespace ba::app::audio {

namespace {

using anillo::kBloque;
/// Lo que agrega un cruce a un bus virtual: el bus de oído del motor (latenciaOidoNs).
int retardoBusMs() { return static_cast<int>(motor::latenciaOidoNs() / 1'000'000); }
using anillo::Ring;

float dbALineal(double db) { return db <= -96.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
double linealADb(float x) { return x <= 1e-6f ? -100.0 : 20.0 * std::log10(static_cast<double>(x)); }

BroadcastEngine* motor() { return fuentes::motorApp(); }
/// El motor de GStreamer de la app (nulo si no está andando).
motor::Motor* motorGst() {
    BroadcastEngine* e = motor();
    return e != nullptr ? e->motor() : nullptr;
}

/// Pan de potencia constante: la ganancia de cada canal de un envío.
std::pair<float, float> canalesDelPan(float ganancia, float pan) {
    const float a = (pan + 1.0f) * 0.25f * 3.14159265f;
    const float gl = pan == 0.0f ? 1.0f : std::cos(a) * 1.41421356f;
    const float gr = pan == 0.0f ? 1.0f : std::sin(a) * 1.41421356f;
    return {ganancia * gl, ganancia * gr};
}

/// Las placas de salida del equipo (las del motor: su id estable y su nombre), sin enumerar:
/// lo último que vio el Vigía.
std::vector<std::pair<std::string, std::string>> placasDeSalida() { return motor::Vigia::el().salidasDeAudio(); }
std::string nombreDePlaca(const std::string& id) {
    for (const auto& [i, n] : placasDeSalida())
        if (i == id) return n;
    return {};
}

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
/// Lo que suena en los auriculares: la escucha fija o el solo (PFL/AFL).
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

// Un bus virtual: en el motor (Motor::matrizBus, F3-08), que lo mezcla y lo saca.
struct Matriz::Bus {
    std::string id, nombre, tipo;   // tipo: "salida" | "entrada"
    std::string dispositivo;        // salida: el id estable de la placa (el del motor)
    std::string salida;             // la salida del programa cuyo mute sigue ("" = la por omisión)
    bool entradaCreada = false;     // entrada: su entrada en el motor (lee el bus: intersrc)
};

struct Matriz::Toma {
    Ring ring;
    int toma = 0; ///< la del motor (escucharEntrada)
    std::string nombre;
};

// Lo que mezcla el hilo de acá: solo las salidas FÍSICAS del equipo (EquipoAudio; los
// buses virtuales los mezcla el motor).
struct Matriz::Plan {
    struct Aporte {
        std::shared_ptr<Toma> toma;
        float gl, gr; // ganancia por canal (nivel y pan)
    };
    struct BusPlan {
        std::string id;
        std::vector<Aporte> aportes;
        std::shared_ptr<Ring> aDispositivo;
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
        // Un motor nuevo (arrancó, o se rearmó) no tiene los buses del anterior (se mira en su
        // foto, sin su candado); y una placa guardada con el id de antes (el de Qt) se pasa al
        // del motor cuando el Vigía la ve.
        bool faltanBuses = false;
        if (motor::Motor* m = motorGst())
            if (const auto foto = m->instantanea()) {
                const json mz = foto->value("matriz", json::object());
                for (const auto& b : buses_) faltanBuses = faltanBuses || !mz.contains(b->id);
            }
        if (migrarPlacas() || faltanBuses) armarPlan();
        // Se enchufó o se fue una placa (lo ve el Vigía del motor): la lista de dispositivos.
        if (const auto g = motor::Vigia::el().generacion(); g != genPlacas_) {
            genPlacas_ = g;
            emit cambio();
        }
        bool falta = false;
        {
            std::lock_guard<std::mutex> l(tomasLock_);
            BroadcastEngine* eng = motor();
            for (const auto& [k, e] : envios_) // (las tomas son de lo que va a una salida física)
                if (k.second.rfind("fis:", 0) == 0 && !tomas_.contains(k.first)) falta = true;
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
    // Los buses virtuales del motor se van con él (o con la app): no hace falta pedirlo.
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
        const QString disp = b->tipo == "salida" ? QString::fromStdString(nombreDePlaca(b->dispositivo)) : QString();
        out.push_back(QVariantMap{{"id", QString::fromStdString(b->id)},
                                  {"nombre", QString::fromStdString(b->nombre)},
                                  {"tipo", QString::fromStdString(b->tipo)},
                                  {"dispositivo", QString::fromStdString(b->dispositivo)},
                                  {"dispositivoNombre", disp},
                                  {"salida", QString::fromStdString(b->salida)}});
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
                                                                            {"retardo_ms", retardoBusMs()}};
    return out;
}

double Matriz::picoDbDe(const std::string& columna) const {
    // Un bus virtual: lo que el motor publica para los vúmetros (el mayor de los canales).
    if (buscar(columna) != nullptr) {
        motor::Motor* m = motorGst();
        const auto niveles = m != nullptr ? m->niveles() : nullptr;
        if (niveles)
            for (const auto& n : *niveles)
                if (n.id == "matriz:" + columna) return std::max(n.pico[0], n.pico[1]);
        return -100.0;
    }
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
    for (const auto& [id, nombre] : placasDeSalida())
        out.push_back(QVariantMap{{"id", QString::fromStdString(id)}, {"nombre", QString::fromStdString(nombre)}});
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

QString Matriz::ponerPfl(const QString& fuenteQ, const QString& modo, const QString& salida) {
    BroadcastEngine* e = motor();
    if (e == nullptr) return tr("el motor no está listo");
    const std::string pedida = fuenteQ.toStdString();
    if (!pedida.empty() && !e->sourceExists(pedida)) return tr("No existe la fuente de audio «%1».").arg(fuenteQ);
    if (!e->setPfl(pedida, modo.toStdString(), salida.toStdString())) return QString::fromStdString(e->lastError());
    // Una entrada de placa en PFL tiene que estar abierta (y la que salió, quizás no).
    // El cambio() lo trae el aviso del motor (avisarFuente). No se guarda: el PFL es de un rato.
    usarEquipo();
    return {};
}

QString Matriz::ponerSalidaEscucha(const QString& salida) {
    BroadcastEngine* e = motor();
    if (e == nullptr) return tr("el motor no está listo");
    const std::string antes = e->salidaEscucha();
    if (!e->setSalidaEscucha(salida.toStdString())) return QString::fromStdString(e->lastError());
    if (e->salidaEscucha() != antes) emit cambio();
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
    if (!crearEntradaVirtual(nom, b->id)) return tr("No se pudo crear la fuente.");
    b->entradaCreada = true;
    buses_.push_back(std::move(b));
    armarPlan(); // el bus en el motor (si no, la entrada lee un productor que no existe)
    refrescar();
    guardar();
    return {};
}

bool Matriz::crearEntradaVirtual(const std::string& nom, const std::string& busId) {
    // Una entrada global del motor que lee la mezcla del bus (su productor en el motor, con
    // el reloj del estudio: F3-08). Arranca sin pistas: se decide en la matriz a dónde va
    // (para que no aparezca sola en el programa).
    const std::string lee = "intersrc producer-name=\"" + motor::Motor::productorMatriz(busId) + "\"";
    const nlohmann::json aj{{"pipeline", ""},
                            {"pipeline_audio", lee},
                            {"audio", true},
                            {"normalizar", false},
                            {"estudio", false},
                            {"virtual", true}};
    if (!fuentes::crearGlobal(nom, "ba_gst_fuente", aj, false, {})) return false;
    // Una que ya estaba (guardada antes de F3-08) leía la cola de su `bavoz`, que ya nadie
    // llena: pasa a leer el bus.
    if (fuentes::ajustesDe(nom).value("pipeline_audio", std::string()) != lee)
        if (BroadcastEngine* e = motor()) e->updateSourceSettings(nom, nlohmann::json{{"pipeline_audio", lee}}.dump());
    return true;
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
        std::unique_ptr<Bus> muerto = std::move(*it);
        buses_.erase(it);
        armarPlan(); // primero salen sus envíos (y de los otros buses, lo que mandaba ella)
        if (muerto->entradaCreada) {
            fuentes::quitarGlobal(muerto->nombre);
            muerto->entradaCreada = false;
        }
        if (motor::Motor* m = motorGst()) m->quitarMatrizBus(muerto->id);
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
            // En el motor la entrada ES su nombre: se arma una nueva que lee el mismo bus.
            fuentes::quitarGlobal(b->nombre);
            b->entradaCreada = crearEntradaVirtual(nombre, b->id);
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
    b->dispositivo = dispositivo.toStdString();
    // La placa la abre el motor (y si se va, queda muda hasta que vuelva). Elegir una que no
    // está es un error (si el Vigía ya sabe cuáles hay).
    const auto placas = placasDeSalida();
    const bool esta = b->dispositivo.empty() || placas.empty() ||
                      std::any_of(placas.begin(), placas.end(), [&](const auto& p) { return p.first == b->dispositivo; });
    armarPlan();
    if (esta) guardar();
    emit cambio();
    return esta ? QString() : tr("Ese dispositivo ya no está.");
}

QString Matriz::setSalida(const QString& idQ, const QString& salida) {
    Bus* b = buscar(idQ.toStdString());
    if (b == nullptr || b->tipo != "salida") return tr("No es una salida virtual.");
    // El mute de cada fuente en este bus pasa a ser el de esa salida (lo aplica el motor; una
    // salida que después se borra vuelve sola a la por omisión).
    b->salida = salida.toStdString();
    armarPlan();
    guardar();
    emit cambio();
    return {};
}

bool Matriz::migrarPlacas() {
    // Antes de F3-08 la placa de una salida virtual se guardaba con el id de Qt Multimedia;
    // el motor usa el suyo (el `unique-id` de macOS, el `device` del resto). Se pasa por el
    // nombre, cuando el Vigía ya ve las placas.
    const auto placas = placasDeSalida();
    if (placas.empty()) return false;
    bool cambio = false;
    for (auto& b : buses_) {
        if (b->tipo != "salida" || b->dispositivo.empty()) continue;
        if (std::any_of(placas.begin(), placas.end(), [&](const auto& p) { return p.first == b->dispositivo; })) continue;
        for (const QAudioDevice& d : QMediaDevices::audioOutputs()) {
            if (QString::fromLatin1(d.id()).toStdString() != b->dispositivo) continue;
            const std::string nombre = d.description().toStdString();
            for (const auto& [id, n] : placas)
                if (n == nombre) {
                    b->dispositivo = id;
                    cambio = true;
                }
        }
    }
    if (cambio) guardar();
    return cambio;
}

// --- El audio ------------------------------------------------------------------------



void Matriz::sincronizarTomas() {
    // Una toma por fuente que manda a alguna salida FÍSICA (las del equipo: los buses
    // virtuales los mezcla el motor, sin tomas); las demás no cuestan nada.
    std::set<std::string> hacen;
    for (const auto& [k, e] : envios_)
        if (k.second.rfind("fis:", 0) == 0) hacen.insert(k.first);
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
    // Los buses virtuales: en el motor (F3-08), con lo que les manda cada entrada (lo que
    // no cambió no se toca allá). Uno de salida sale por su placa; uno de entrada solo
    // entrega su mezcla (la lee su entrada).
    if (motor::Motor* m = motorGst()) {
        for (const auto& b : buses_) {
            std::vector<motor::Motor::EnvioMatriz> envios;
            for (const auto& [k, e] : envios_) {
                if (k.second != b->id || e.mute) continue;
                const auto [gl, gr] = canalesDelPan(e.ganancia, e.pan);
                envios.push_back({k.first, gl, gr});
            }
            m->matrizBus(b->id, envios, b->tipo == "salida" ? b->dispositivo : std::string(), b->salida);
        }
    }
    // Las salidas físicas: el hilo de mezcla de acá, con las tomas de cada entrada (todavía
    // con Qt Multimedia: ver Matriz.hpp).
    auto p = std::make_shared<Plan>();
    std::lock_guard<std::mutex> l(tomasLock_);
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
            const auto [gl, gr] = canalesDelPan(e.ganancia, e.pan);
            bp.aportes.push_back({t->second, gl, gr});
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
        m["bus:" + QString::fromStdString(b->id)] = picoDbDe(b->id);
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
            {{"id", b->id}, {"nombre", b->nombre}, {"tipo", b->tipo}, {"dispositivo", b->dispositivo}, {"salida", b->salida}});
    for (const auto& [k, e] : envios_)
        envios.push_back({{"fuente", k.first},
                          {"bus", k.second},
                          {"nivelDb", e.nivelDb},
                          {"pan", e.pan},
                          {"mute", e.mute},
                          {"ajustes", e.ajustes},
                          // Lo que suma pasar por la mezcla propia (las pistas nativas: 0).
                          {"retardo_ms", retardoBusMs()}});
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
    for (const auto& b : buses_) buses[b->id] = std::round(picoDbDe(b->id) * 10) / 10;
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
    config::guardar(root_ / "Config" / "audio-matriz.json", j); // el escritor único: un corte no lo deja trunco
}

void Matriz::cargar() {
    const json j = config::leerObjeto(root_ / "Config" / "audio-matriz.json");
    if (j.empty()) return;
    for (const auto& p : j.value("pistas", json::array())) {
        const std::string id = p.value("id", std::string());
        const int i = id.rfind("pista", 0) == 0 ? std::atoi(id.c_str() + 5) - 1 : -1;
        if (i >= 0 && i < kPistas) pistas_[i] = p.value("nombre", pistas_[i]);
    }
    if (motor() == nullptr) return;
    for (const auto& b : j.value("buses", json::array())) {
        const std::string tipo = b.value("tipo", std::string());
        if (tipo == "salida") {
            // Tal como estaba, aunque su placa no esté (queda muda hasta que vuelva): validarla
            // es de cuando se elige. Antes se validaba acá y una placa desenchufada al arrancar
            // se llevaba el bus (y el id del guardado caía en otro).
            if (buses_.size() >= kMaxVirtuales) continue;
            auto nb = std::make_unique<Bus>();
            nb->id = b.value("id", std::string("salida") + std::to_string(buses_.size() + 1));
            nb->nombre = b.value("nombre", nb->id);
            nb->tipo = "salida";
            nb->dispositivo = b.value("dispositivo", std::string());
            nb->salida = b.value("salida", std::string());
            buses_.push_back(std::move(nb));
        } else if (tipo == "entrada") {
            // Con su id de siempre: su entrada lee el bus por él (y si la fuente ya estaba, se
            // la deja, leyendo el bus).
            const std::string nombre = b.value("nombre", std::string());
            if (nombre.empty() || buses_.size() >= kMaxVirtuales) continue;
            auto nb = std::make_unique<Bus>();
            nb->id = b.value("id", std::string("entrada") + std::to_string(buses_.size() + 1));
            nb->nombre = nombre;
            nb->tipo = "entrada";
            nb->entradaCreada = crearEntradaVirtual(nombre, nb->id);
            buses_.push_back(std::move(nb));
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
