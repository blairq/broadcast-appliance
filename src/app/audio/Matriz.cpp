// ============================================================================
// src/app/audio/Matriz.cpp — ver Matriz.hpp
// ============================================================================
#include "audio/Matriz.hpp"
#include "audio/AnilloAudio.hpp"
#include "audio/EquipoAudio.hpp"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace ba::app::audio {

namespace {

constexpr const char* kTipoEntrada = "ba_entrada_virtual";
constexpr std::uint32_t kPrimerCanal = 40; // 0 programa, 1-16 invitados, 17-24 micrófonos
using anillo::AlDispositivo;
using anillo::kBloque;
using anillo::kColchon;
using anillo::Ring;

float dbALineal(double db) { return db <= -96.0 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0)); }
double linealADb(float x) { return x <= 1e-6f ? -100.0 : 20.0 * std::log10(static_cast<double>(x)); }

// --- La fuente de las entradas virtuales: el audio se lo empuja la mezcla ----
const char* nombreEntrada(void*) { return "Entrada virtual (matriz)"; }
void* crearEntrada(obs_data_t*, obs_source_t* s) { return s; }
void destruirEntrada(void*) {}
obs_source_info g_entrada = {};

} // namespace

void registrarEntradaVirtual() {
    g_entrada.id = kTipoEntrada;
    g_entrada.type = OBS_SOURCE_TYPE_INPUT;
    g_entrada.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CAP_DISABLED;
    g_entrada.get_name = nombreEntrada;
    g_entrada.create = crearEntrada;
    g_entrada.destroy = destruirEntrada;
    g_entrada.icon_type = OBS_ICON_TYPE_AUDIO_INPUT;
    obs_register_source(&g_entrada);
}

// --- Estructuras -------------------------------------------------------------------

struct Matriz::Bus {
    std::string id, nombre, tipo;   // tipo: "salida" | "entrada"
    std::string dispositivo;        // salida: id del QAudioDevice
    obs_source_t* fuente = nullptr; // entrada: la fuente que la representa
    std::uint32_t canal = 0;
    std::shared_ptr<Ring> aDispositivo;
    std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<AlDispositivo> io;
    std::shared_ptr<std::atomic<float>> pico = std::make_shared<std::atomic<float>>(0.0f);
};

struct Matriz::Toma {
    Ring ring;
    obs_source_t* fuente = nullptr;
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
        obs_source_t* entrada = nullptr;
        std::shared_ptr<std::atomic<float>> pico;
    };
    std::vector<BusPlan> buses;
};

// --- Ciclo de vida -------------------------------------------------------------------

Matriz::Matriz(fs::path dataRoot, QObject* parent) : QObject(parent), root_(std::move(dataRoot)) {
    for (int i = 0; i < kPistas; ++i)
        pistas_[i] = i == 0 ? "Programa" : "Pista " + std::to_string(i + 1);
    equipo_ = std::make_unique<EquipoAudio>();
    // Se enchufó o desenchufó algo (o se ocultó): columnas, filas y plan de nuevo.
    connect(equipo_.get(), &EquipoAudio::cambio, this, [this]() {
        refrescar();
        armarPlan();
        usarEquipo();
        emit cambio();
    });
    cargando_ = true;
    cargar();
    cargando_ = false;
    refrescar();
    connect(&timer_, &QTimer::timeout, this, [this]() { leerMedidores(); });
    // Un invitado que entra después: si tiene envíos, se le engancha la toma.
    connect(&lento_, &QTimer::timeout, this, [this]() {
        bool falta = false;
        {
            std::lock_guard<std::mutex> l(tomasLock_);
            for (const auto& [k, e] : envios_)
                if (!tomas_.contains(k.first)) falta = true;
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
    lento_.start(3000);
    hilo_ = std::thread([this]() { bucleMezcla(); });
}

Matriz::~Matriz() {
    salir_ = true;
    if (hilo_.joinable()) hilo_.join();
    // Si libobs ya se apagó (cierre temprano), sus fuentes ya no existen.
    if (!obs_initialized()) return;
    setActiva(false);
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        for (auto& [n, t] : tomas_)
            if (t->fuente != nullptr) {
                obs_source_remove_audio_capture_callback(t->fuente, &Matriz::alAudio, t.get());
                obs_source_release(t->fuente);
            }
        tomas_.clear();
    }
    for (auto& b : buses_) {
        if (b->sink) b->sink->stop();
        if (b->fuente != nullptr) {
            obs_set_output_source(b->canal, nullptr);
            obs_source_release(b->fuente);
        }
    }
}

// --- Fuentes y buses -------------------------------------------------------------------

std::vector<std::string> Matriz::fuentesConAudio() const {
    std::vector<std::string> out;
    obs_enum_sources(
        [](void* p, obs_source_t* s) {
            if ((obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) != 0) {
                const char* n = obs_source_get_name(s);
                if (n != nullptr && *n) static_cast<std::vector<std::string>*>(p)->push_back(n);
            }
            return true;
        },
        &out);
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
    if (!obs_initialized()) return;
    filas_ = fuentesConAudio();
    sincronizarTomas();
    armarPlan();
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
/// La salida de escucha (los auriculares del operador): el monitoreo de libobs.
QString nombreEscucha() {
    const char* nombre = nullptr;
    const char* id = nullptr;
    obs_get_audio_monitoring_device(&nombre, &id);
    if (id == nullptr || std::string(id).empty() || std::string(id) == "default")
        return "la salida del sistema";
    return QString::fromUtf8(nombre != nullptr ? nombre : id);
}
/// Escuchar una fuente sin sacarla del aire: OBS_MONITORING_TYPE_MONITOR_ONLY
/// la SACA de la mezcla, así que si tiene alguna pista va «monitor y salida».
void setEscucha(obs_source_t* s, bool on) {
    if (!on) {
        obs_source_set_monitoring_type(s, OBS_MONITORING_TYPE_NONE);
        return;
    }
    obs_source_set_monitoring_type(s, obs_source_get_audio_mixers(s) != 0
                                          ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT
                                          : OBS_MONITORING_TYPE_MONITOR_ONLY);
}
bool escucha(obs_source_t* s) { return obs_source_get_monitoring_type(s) != OBS_MONITORING_TYPE_NONE; }
} // namespace

QVariantList Matriz::columnas() const {
    QVariantList out;
    for (int i = 0; i < kPistas; ++i)
        out.push_back(QVariantMap{
            {"id", QString("pista%1").arg(i + 1)},
            {"nombre", QString::fromStdString(pistas_[i])},
            {"tipo", "pista"},
            {"indice", i},
            {"uso", QString::fromStdString(i == 0 ? std::string("emisión y grabación") : usoPista_[i])}});
    // Los auriculares del operador (Configuración → Audio y voz → salida de auriculares).
    out.push_back(QVariantMap{{"id", "escucha"},
                              {"nombre", "Escucha"},
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
    // Pistas nativas: lo que tiene cada fuente en sus mezcladores de OBS.
    for (const auto& f : filas_) {
        obs_source_t* s = obs_get_source_by_name(f.c_str());
        if (s == nullptr) continue;
        const std::uint32_t mx = obs_source_get_audio_mixers(s);
        const bool oye = escucha(s);
        obs_source_release(s);
        if (oye) out[QString::fromStdString(f) + "|escucha"] = QVariantMap{{"activo", true}};
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
                                                                            {"mute", e.mute}};
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

QString Matriz::setCruce(const QString& fuenteQ, const QString& busQ, bool activo) {
    const std::string fuente = fuenteQ.toStdString(), bus = busQ.toStdString();
    if (bus.rfind("pista", 0) == 0) {
        const int i = std::atoi(bus.c_str() + 5) - 1;
        if (i < 0 || i >= kPistas) return "No existe esa pista.";
        obs_source_t* s = obs_get_source_by_name(fuente.c_str());
        if (s == nullptr) return "No existe la fuente «" + fuenteQ + "».";
        std::uint32_t mx = obs_source_get_audio_mixers(s);
        mx = activo ? (mx | (1u << i)) : (mx & ~(1u << i));
        obs_source_set_audio_mixers(s, mx);
        // Si se escucha «solo monitor», con una pista no saldría al aire.
        if (escucha(s)) setEscucha(s, true);
        obs_source_release(s);
        usarEquipo();
        guardar();
        emit cambio();
        return {};
    }
    if (bus == "escucha") {
        obs_source_t* s = obs_get_source_by_name(fuente.c_str());
        if (s == nullptr) return "No existe la fuente «" + fuenteQ + "».";
        setEscucha(s, activo);
        obs_source_release(s);
        usarEquipo();
        guardar();
        emit cambio();
        return {};
    }
    if (buscar(bus) == nullptr && !equipo_->anilloDe(bus)) return "No existe el bus.";
    const auto k = std::make_pair(fuente, bus);
    if (activo) {
        if (haceLazo(fuente, bus)) return "Eso arma un lazo: la entrada virtual volvería a su propio bus.";
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
    if (buses_.size() >= kMaxVirtuales) return "No entran más buses virtuales.";
    auto b = std::make_unique<Bus>();
    int n = 1;
    while (buscar("salida" + std::to_string(n)) != nullptr)
        ++n;
    b->id = "salida" + std::to_string(n);
    b->nombre =
        nombre.trimmed().isEmpty() ? "Salida virtual " + std::to_string(n) : nombre.trimmed().toStdString();
    b->tipo = "salida";
    buses_.push_back(std::move(b));
    setDispositivo(QString::fromStdString(buses_.back()->id), dispositivo);
    armarPlan();
    guardar();
    emit cambio();
    return {};
}

QString Matriz::agregarEntradaVirtual(const QString& nombre) {
    if (buses_.size() >= kMaxVirtuales) return "No entran más buses virtuales.";
    if (!obs_initialized()) return "El motor de audio no está arriba.";
    int n = 1;
    while (buscar("entrada" + std::to_string(n)) != nullptr)
        ++n;
    std::string nom =
        nombre.trimmed().isEmpty() ? "Entrada virtual " + std::to_string(n) : nombre.trimmed().toStdString();
    if (obs_source_t* existe = obs_get_source_by_name(nom.c_str())) {
        obs_source_release(existe);
        return "Ya hay una fuente con ese nombre.";
    }
    std::set<std::uint32_t> usados;
    for (const auto& b : buses_)
        if (b->tipo == "entrada") usados.insert(b->canal);
    std::uint32_t canal = kPrimerCanal;
    while (usados.contains(canal))
        ++canal;
    auto b = std::make_unique<Bus>();
    b->id = "entrada" + std::to_string(n);
    b->nombre = nom;
    b->tipo = "entrada";
    b->canal = canal;
    b->fuente = obs_source_create(kTipoEntrada, nom.c_str(), nullptr, nullptr);
    if (b->fuente == nullptr) return "No se pudo crear la fuente.";
    // Arranca sin pistas: se decide en la matriz a dónde va (para que no
    // aparezca sola en el programa).
    obs_source_set_audio_mixers(b->fuente, 0);
    obs_set_output_source(canal, b->fuente); // activa: se mezcla aunque no esté en una escena
    buses_.push_back(std::move(b));
    refrescar();
    guardar();
    return {};
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
        if (muerto->fuente != nullptr) {
            obs_set_output_source(muerto->canal, nullptr);
            obs_source_remove(muerto->fuente);
            obs_source_release(muerto->fuente);
            muerto->fuente = nullptr;
        }
        refrescar();
        guardar();
        return {};
    }
    return "No existe el bus.";
}

QString Matriz::renombrar(const QString& idQ, const QString& nombreQ) {
    const std::string id = idQ.toStdString(), nombre = nombreQ.trimmed().toStdString();
    if (nombre.empty()) return "Falta el nombre.";
    if (id.rfind("pista", 0) == 0) {
        const int i = std::atoi(id.c_str() + 5) - 1;
        if (i < 0 || i >= kPistas) return "No existe esa pista.";
        pistas_[i] = nombre;
    } else {
        Bus* b = buscar(id);
        if (b == nullptr) return "No existe el bus.";
        if (b->tipo == "entrada") {
            if (obs_source_t* existe = obs_get_source_by_name(nombre.c_str())) {
                obs_source_release(existe);
                return "Ya hay una fuente con ese nombre.";
            }
            // Los envíos que salen de esta entrada cambian de nombre con ella.
            std::map<std::pair<std::string, std::string>, Envio> nuevos;
            for (auto& [k, e] : envios_)
                nuevos[{k.first == b->nombre ? nombre : k.first, k.second}] = e;
            envios_ = std::move(nuevos);
            obs_source_set_name(b->fuente, nombre.c_str());
        }
        b->nombre = nombre;
    }
    refrescar();
    guardar();
    return {};
}

QString Matriz::setDispositivo(const QString& idQ, const QString& dispositivo) {
    Bus* b = buscar(idQ.toStdString());
    if (b == nullptr || b->tipo != "salida") return "No es una salida virtual.";
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
        return dispositivo.isEmpty() ? QString() : "Ese dispositivo ya no está.";
    }
    QAudioFormat f;
    f.setSampleRate(48000);
    f.setChannelCount(2);
    f.setSampleFormat(QAudioFormat::Float);
    if (!dev.isFormatSupported(f)) return "El dispositivo no acepta audio estéreo a 48 kHz.";
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

void Matriz::alAudio(void* param, obs_source_t*, const ::audio_data* data, bool muted) {
    auto* t = static_cast<Toma*>(param);
    if (data == nullptr || data->frames == 0 || data->data[0] == nullptr) return;
    const auto* l = reinterpret_cast<const float*>(data->data[0]);
    const auto* r = data->data[1] != nullptr ? reinterpret_cast<const float*>(data->data[1]) : l;
    // Con hora: la voz del sistema y la del teleprompter entregan frases enteras
    // agendadas a futuro; sin esto el anillo las recortaba a 80 ms.
    const std::uint32_t sr = audio_output_get_sample_rate(obs_get_audio());
    if (muted) {
        std::vector<float> z(data->frames, 0.0f);
        t->ring.pushEn(z.data(), z.data(), data->frames, data->timestamp, os_gettime_ns(), sr);
    } else {
        t->ring.pushEn(l, r, data->frames, data->timestamp, os_gettime_ns(), sr);
    }
}

void Matriz::sincronizarTomas() {
    // Una toma por fuente que manda a algún bus virtual; las demás no cuestan nada.
    std::set<std::string> hacen;
    for (const auto& [k, e] : envios_)
        hacen.insert(k.first);
    std::lock_guard<std::mutex> l(tomasLock_);
    for (auto it = tomas_.begin(); it != tomas_.end();) {
        if (hacen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second->fuente != nullptr) {
            obs_source_remove_audio_capture_callback(it->second->fuente, &Matriz::alAudio, it->second.get());
            obs_source_release(it->second->fuente);
        }
        it = tomas_.erase(it);
    }
    for (const auto& n : hacen) {
        if (tomas_.contains(n)) continue;
        obs_source_t* s = obs_get_source_by_name(n.c_str());
        if (s == nullptr) continue; // todavía no existe (un invitado que no llegó)
        auto t = std::make_shared<Toma>();
        t->fuente = s;
        t->nombre = n;
        obs_source_add_audio_capture_callback(s, &Matriz::alAudio, t.get());
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
        bool usa = false;
        if (obs_source_t* s = obs_get_source_by_name(f.c_str())) {
            usa = obs_source_get_audio_mixers(s) != 0 || escucha(s);
            obs_source_release(s);
        }
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
        bp.entrada = b->fuente;
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
    std::lock_guard<std::mutex> g(planLock_);
    plan_ = std::move(p);
}

void Matriz::bucleMezcla() {
    using reloj = std::chrono::steady_clock;
    std::vector<float> al(kBloque), ar(kBloque), ml(kBloque), mr(kBloque);
    auto proximo = reloj::now();
    while (!salir_) {
        proximo += std::chrono::milliseconds(10);
        std::this_thread::sleep_until(proximo);
        if (reloj::now() - proximo > std::chrono::milliseconds(100)) proximo = reloj::now(); // se atrasó
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
            if (b.entrada != nullptr) {
                obs_source_audio a{};
                a.data[0] = reinterpret_cast<const std::uint8_t*>(ml.data());
                a.data[1] = reinterpret_cast<const std::uint8_t*>(mr.data());
                a.frames = kBloque;
                a.speakers = SPEAKERS_STEREO;
                a.format = AUDIO_FORMAT_FLOAT_PLANAR;
                a.samples_per_sec = 48000;
                a.timestamp = os_gettime_ns();
                obs_source_output_audio(b.entrada, &a);
            }
        }
    }
}

// --- Medidores -----------------------------------------------------------------------

namespace {
void alVolmeter(void* param, const float*, const float peak[MAX_AUDIO_CHANNELS], const float*) {
    auto* p = static_cast<std::atomic<float>*>(param);
    float db = -100.0f;
    for (int c = 0; c < 2; ++c)
        db = std::max(db, peak[c]);
    p->store(db);
}
} // namespace

void Matriz::setActiva(bool on) {
    if (on == activa_) return;
    activa_ = on;
    if (on) {
        refrescar();
        timer_.start(50);
        return;
    }
    timer_.stop();
    for (auto& [n, vm] : volmeters_)
        obs_volmeter_destroy(vm);
    volmeters_.clear();
    picos_.clear();
}

void Matriz::leerMedidores() {
    if (!obs_initialized()) return;
    // Un vúmetro de OBS por fila (después de los filtros, como lo que suena).
    for (const auto& f : filas_) {
        if (volmeters_.contains(f)) continue;
        obs_source_t* s = obs_get_source_by_name(f.c_str());
        if (s == nullptr) continue;
        auto pico = std::make_shared<std::atomic<float>>(-100.0f);
        obs_volmeter_t* vm = obs_volmeter_create(OBS_FADER_LOG);
        obs_volmeter_attach_source(vm, s);
        obs_volmeter_add_callback(vm, alVolmeter, pico.get());
        obs_source_release(s);
        volmeters_[f] = vm;
        picos_[f] = pico;
    }
    QVariantMap m;
    for (const auto& [n, p] : picos_)
        m["fila:" + QString::fromStdString(n)] = static_cast<double>(p->load());
    for (const auto& b : buses_)
        m["bus:" + QString::fromStdString(b->id)] = linealADb(b->pico->load());
    for (const auto& [id, pk] : picosEquipo_)
        m["bus:" + QString::fromStdString(id)] = linealADb(pk->load());
    // Pistas nativas: el máximo de lo que les llega (sin tocar el audio).
    for (int i = 0; i < kPistas; ++i) {
        double mx = -100.0;
        for (const auto& f : filas_) {
            obs_source_t* s = obs_get_source_by_name(f.c_str());
            if (s == nullptr) continue;
            const bool va = (obs_source_get_audio_mixers(s) & (1u << i)) != 0 && !obs_source_muted(s);
            obs_source_release(s);
            if (va && picos_.contains(f)) mx = std::max(mx, static_cast<double>(picos_[f]->load()));
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
                          {"ajustes", e.ajustes}});
    for (const auto& f : filas_) {
        json pis = json::array();
        bool oye = false;
        if (obs_source_t* s = obs_get_source_by_name(f.c_str())) {
            const std::uint32_t mx = obs_source_get_audio_mixers(s);
            oye = escucha(s);
            obs_source_release(s);
            for (int i = 0; i < kPistas; ++i)
                if ((mx & (1u << i)) != 0) pis.push_back("pista" + std::to_string(i + 1));
        }
        filas.push_back({{"fuente", f}, {"pistas", pis}, {"escucha", oye}});
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

void Matriz::guardar() const {
    if (cargando_) return;
    json j = estado();
    j.erase("fuentes");
    // Las pistas nativas de cada fuente (OBS no las guarda por nosotros).
    json mx = json::object();
    for (const auto& f : filas_)
        if (obs_source_t* s = obs_get_source_by_name(f.c_str())) {
            mx[f] = obs_source_get_audio_mixers(s);
            obs_source_release(s);
        }
    j["mezcladores"] = mx;
    // Qué fuentes suenan en la escucha (los auriculares).
    json oye = json::object();
    for (const auto& f : filas_)
        if (obs_source_t* s = obs_get_source_by_name(f.c_str())) {
            oye[f] = escucha(s);
            obs_source_release(s);
        }
    j["escucha"] = oye;
    j["equipo"] = equipo_->guardado();
    j["pistasPlegadas"] = pistasPlegadas_;
    const fs::path p = root_ / "Config" / "audio-matriz.json";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p) << j.dump(1);
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
    if (!obs_initialized()) return;
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
    for (const auto& [fuente, mx] : j.value("mezcladores", json::object()).items())
        if (obs_source_t* s = obs_get_source_by_name(fuente.c_str())) {
            // Las pistas 2-6 que toma la grabación individual no se reponen
            // de un guardado viejo: la grabación las pone y las saca sola.
            obs_source_set_audio_mixers(s, mx.get<std::uint32_t>() & 0x3F);
            obs_source_release(s);
        }
    // Primero el equipo: crea las fuentes de las entradas físicas, que el resto
    // (pistas, escucha) reconoce por nombre.
    equipo_->cargar(j.value("equipo", json::object()));
    pistasPlegadas_ = j.value("pistasPlegadas", true);
    for (const auto& [fuente, mx2] : j.value("mezcladores", json::object()).items())
        if (equipo_->esEntradaFisica(fuente))
            if (obs_source_t* s = obs_get_source_by_name(fuente.c_str())) {
                obs_source_set_audio_mixers(s, mx2.get<std::uint32_t>() & 0x3F);
                obs_source_release(s);
            }
    for (const auto& [fuente, on] : j.value("escucha", json::object()).items())
        if (obs_source_t* s = obs_get_source_by_name(fuente.c_str())) {
            if (on.is_boolean()) setEscucha(s, on.get<bool>());
            obs_source_release(s);
        }
}

} // namespace ba::app::audio
