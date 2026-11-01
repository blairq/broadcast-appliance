// ============================================================================
// src/app/emision/BroadcastEngine.cpp — Motor de emision: la fachada
//
// Ver BroadcastEngine.hpp. Acá: el ciclo de vida (arrancar, apagar, rearmar con otro lienzo),
// las salidas (crearlas y borrarlas, partidas en SalidaAire y SalidaEmision:
// emision/SalidaMotor.hpp), el reloj del estudio, el estado y los avisos, las capacidades
// (codificadores, tipos de fuente) y la API de siempre, que toma el turno y delega en las
// piezas:
//   EscenasYFuentes (+ CapasPrograma.cpp), MezclaApp, MediosApp, VistasYMedicion,
//   RetornoGateway, EmisionSalidas (el hilo de salidas), VigiaDisco y EmisorAparte.
// ============================================================================

#include "emision/BroadcastEngine.hpp"

#include "Motor.hpp"
#include "Salidas.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "audio/GstRack.hpp"
#include "audio/GstVoz.hpp"
#include "diag/Registro.hpp"
#include "emision/EmisionSalidas.hpp"
#include "emision/EmisorAparte.hpp"
#include "emision/EscenasYFuentes.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/MediosApp.hpp"
#include "emision/MezclaApp.hpp"
#include "emision/NdiSource.hpp"
#include "emision/RetornoGateway.hpp"
#include "emision/SalidaMotor.hpp"
#include "emision/SalidasConfig.hpp"
#include "emision/VigiaDisco.hpp"
#include "emision/VistasYMedicion.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ba::app {

using json = nlohmann::json;

// La por omisión se llama igual en el motor, acá y en lo que se guarda (salidas.json).
static_assert(std::string_view(BroadcastEngine::kSalidaPorOmision) == motor::Motor::kSalidaPorOmision &&
              std::string_view(salidas::kPorOmision) == motor::Motor::kSalidaPorOmision);

bool fpsDeTexto(const std::string& t, int& num, int& den) {
    if (t.empty()) return false;
    const auto barra = t.find('/');
    if (barra != std::string::npos) {
        const int n = std::atoi(t.substr(0, barra).c_str());
        const int d = std::atoi(t.substr(barra + 1).c_str());
        if (n <= 0 || d <= 0) return false;
        num = n;
        den = d;
        return true;
    }
    std::string c = t;
    std::replace(c.begin(), c.end(), ',', '.');
    const double f = std::atof(c.c_str());
    if (f <= 0 || f > 1000) return false;
    // Las cadencias NTSC (23,976 / 29,97 / 59,94 / 119,88): N*1000/1001.
    const double ntsc = std::round(f * 1.001);
    if (std::abs(f - std::floor(f)) > 1e-6 && std::abs(ntsc * 1000.0 / 1001.0 - f) < 0.01) {
        num = static_cast<int>(ntsc) * 1000;
        den = 1001;
        return true;
    }
    num = static_cast<int>(std::lround(f * 1000));
    den = 1000;
    if (num % 1000 == 0) {
        num /= 1000;
        den = 1;
    }
    return true;
}

std::string textoDeFps(int num, int den) {
    if (den <= 1) return std::to_string(num);
    return std::to_string(num) + "/" + std::to_string(den);
}

std::string fpsLegible(int num, int den) {
    if (den <= 1) return std::to_string(num);
    char b[32];
    std::snprintf(b, sizeof b, "%.3g", static_cast<double>(num) / den);
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

namespace {
std::atomic<int> g_lienzoAncho{0}, g_lienzoAlto{0};
std::atomic<double> g_fps{30.0};

motor::Lienzo lienzoDe(const VideoConfig& v) {
    motor::Lienzo l;
    l.ancho = v.baseWidth;
    l.alto = v.baseHeight;
    l.fpsN = v.fpsNum;
    l.fpsD = std::max(1, v.fpsDen);
    l.color = v.color == "601" ? "bt601" : v.color == "2100pq" ? "bt2100-pq" : v.color == "2100hlg" ? "bt2100-hlg" : "bt709";
    l.rangoCompleto = v.rango == "completo";
    l.diezBits = v.formato == "p010" || v.formato == "i010";
    return l;
}

// Lo del último pedido, DE CADA HILO (como errno): el control y la interfaz se turnan el
// motor, y lo que falló en uno no pisa lo que el otro está por leer.
thread_local std::string t_error;
} // namespace

double fpsActual() { return g_fps.load(); }

bool lienzoActual(int& ancho, int& alto) {
    ancho = g_lienzoAncho.load();
    alto = g_lienzoAlto.load();
    return ancho > 0 && alto > 0;
}

// ============================================================================
// Ciclo de vida
// ============================================================================

BroadcastEngine::BroadcastEngine()
    : escenas_(std::make_unique<EscenasYFuentes>(*this)),
      mezcla_(std::make_unique<MezclaApp>(*this)),
      medios_(std::make_unique<MediosApp>(*this)),
      vistas_(std::make_unique<VistasYMedicion>(*this)),
      retorno_(std::make_unique<RetornoGateway>(*this)),
      emisor_(std::make_unique<EmisorAparte>(*this)),
      disco_(std::make_unique<VigiaDisco>(*this)),
      emision_(std::make_unique<EmisionSalidas>(*this)) {
    // La por omisión existe siempre: lo que no dice salida va a ella.
    auto p = std::make_shared<SalidaAire>();
    p->id = p->nombre = kSalidaPorOmision;
    p->emisor = EmisorAparte::salidasConEmisor().count(p->id) > 0; // la codifica un ba-emisor (F3-01)
    p->retorno = std::make_shared<RetornoDeSalida>();
    p->retorno->id = p->id;
    p->retorno->porOmision = true;
    p->retorno->enUso = true; // BA.Program, como siempre: hasta que se sepa que nadie lo mira
    p->emision = std::make_shared<SalidaEmision>();
    p->emision->id = p->id;
    p->emision->emisor = p->emisor;
    salidas_[p->id] = p;
    ordenSalidas_.push_back(p->id);
    medidasPrograma_ = p->medidas;
    emision_->alta(p->emision);
}

BroadcastEngine::~BroadcastEngine() { shutdown(); }

void BroadcastEngine::setError(std::string msg) {
    {
        std::lock_guard<std::mutex> l(errorM_);
        ultimoError_ = msg;
    }
    t_error = std::move(msg);
}

const std::string& BroadcastEngine::lastError() const { return t_error; }

bool BroadcastEngine::startup(const EnginePaths& paths, const VideoConfig& video, const AudioConfig& audio) {
    const TurnoMotor::Guarda turno(turno_);
    if (started_) return true;
    paths_ = paths;
    video_ = video;
    audio_ = audio;
    std::string err;
    if (!motor::iniciarGStreamer(&err)) {
        sinGStreamer_ = true;
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "GStreamer no arrancó: %1"), {err}));
        return false;
    }
    // Los elementos propios de la app, antes de armar cualquier entrada que los use
    // (racks, voces, NDI): si no, avisaban «no element» y reintentaban.
    audio::gstrack::registrarElemento();
    audio::gstvoz::registrarElemento();
    ndi::registrarElemento();
    // Los avisos del motor, al registro de la app.
    motor::ponerRegistro([](motor::Nivel n, const std::string& t) {
        ba::diag::registro().linea(n == motor::Nivel::Error  ? ba::diag::Nivel::Error
                                   : n == motor::Nivel::Aviso ? ba::diag::Nivel::Aviso
                                                              : ba::diag::Nivel::Info,
                                   "motor", 0, t);
    });
    motor::Motor::Config c;
    c.lienzo = lienzoDe(video);
    c.reloj = relojTipo_;
    c.relojServidor = relojServidor_;
    c.relojPuerto = relojPuerto_;
    c.relojDominio = relojDominio_;
    c.emisores = EmisorAparte::salidasConEmisor(); // su audio también al anillo del emisor (F3-01)
    {
        const std::lock_guard<std::mutex> l(vistas_->candado()); // las vistas se leen desde la interfaz
        motor_ = std::make_unique<motor::Motor>(c);
    }
    // Un lienzo que parpadea con la red eléctrica del país (F2-17): se avisa, no se cambia.
    for (const std::string& aviso : motor::avisosDeLaRed(c.lienzo, motor::redElectricaDelSistema()))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "motor", 0, aviso);
    escenas_->olvidarInternas();
    // Lo que se le encargó y falló después (las órdenes van en cola: ver
    // Motor::orden). Llega desde el hilo del motor.
    motor_->alFallar([this](const std::string& que, const std::string& error) {
        avisarFallo(ba::i18n::llenar(ba::i18n::traducir("emision", "el motor no pudo hacer «%1»: %2"), {que, error}));
    });
    // Los vúmetros: cada foto de niveles, al canal que lee la interfaz (NivelesMotor).
    motor_->alPublicarNiveles([c = &nivelesCanal_](std::shared_ptr<const NivelesMotor::Niveles> n) {
        c->ponerNiveles(std::move(n));
    });
    const std::string e = motor_->iniciar();
    if (!e.empty()) {
        setError(e);
        const std::lock_guard<std::mutex> l(vistas_->candado());
        motor_.reset();
        nivelesCanal_.ponerNiveles(nullptr);
        return false;
    }
    started_ = true;
    disco_->cargarModo();
    emision_->arrancar();
    // Lo que lee el emisor aparte: su canal de video (NV12, del tamaño de la salida)
    // publicado y su vista pedida desde ya. Lo demás de su pedido (la señal limpia, las
    // pistas) lo engancha el hilo de salidas al leerlo (EmisorAparte::leer).
    for (const auto& s : lasSalidas())
        if (s->emisor) emisor_->publicarCanal(s->canalEmisor, s->id, s->porOmision(), false);
    if (relojPublicar_ > 0) motor_->publicarReloj(relojPublicar_);
    fuentes::ponerMotorApp(this);
    // Lo que pinta la app (chat, gráficos, subtítulos) va a su entrada «app».
    fuentes::ponerSalida([this](const std::string& n, const std::uint8_t* d, int w, int h, int st) {
        if (motor_) motor_->empujarCuadro(n, d, w, h, st);
    });
    fuentes::ponerCambiar([this](const std::string& n, const json& cambios) {
        return updateSourceSettings(n, cambios.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
    });
    fuentes::ponerConsultas({[this]() {
                                 std::vector<std::pair<std::string, std::string>> v;
                                 for (const auto& n : escenas_->sourceNames()) v.emplace_back(n, sourceTypeId(n));
                                 return v;
                             },
                             [this](const std::string& n) { return json::parse(sourceSettingsJson(n), nullptr, false); },
                             [this](const std::string& e, const std::string& el, const std::string& p, const std::string& v) {
                                 return motor_ ? motor_->propiedadEntrada(e, el, p, v) : ba::i18n::traducir("emision", "no hay motor");
                             },
                             [this](const std::string& e, const std::string& el) {
                                 return motor_ ? motor_->propiedadesEntrada(e, el) : json::object();
                             },
                             [this](const std::string& n, const std::string& tipo, const json& aj, bool monitor,
                                    const std::vector<int>& pistas) {
                                 if (escenas_->sourceExists(n)) return true;
                                 SourceSpec spec;
                                 spec.id = tipo;
                                 spec.name = n;
                                 spec.settingsJson = aj.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                                 if (!addGlobalAudio(spec)) return false;
                                 setSourcePistas(n, pistas);
                                 setSourceMonitoring(n, monitor);
                                 return true;
                             },
                             [this](const std::string& n) { removeGlobalAudio(n); }});
    g_lienzoAncho = video.baseWidth;
    g_lienzoAlto = video.baseHeight;
    g_fps = video.fpsDen > 0 ? static_cast<double>(video.fpsNum) / video.fpsDen : 30.0;
    // Lo que se mide del programa de cada salida (las demás, al crearlas): loudness y
    // picos (audio), brillo y quietud (video).
    vistas_->medirSalida(porOmision());
    if (vistas_->vistasSinMirar()) motor_->vistasSinMirar(true); // un motor rearmado sigue con el calor
    return true;
}

void BroadcastEngine::shutdown() {
    const TurnoMotor::Guarda turno(turno_);
    vistas_->soltarCapturas();
    if (!started_) return;
    vistas_->esperarCapturas();
    started_ = false;
    emision_->stopAllSourceRecordings();
    // Lo que sale por un emisor aparte no se para: apagar o rearmar el motor no corta el
    // aire (lo para el operador, o el cierre ordenado del launcher; F3-01).
    for (const auto& s : lasSalidas()) {
        if (s->emisor) continue;
        std::vector<std::string> extras;
        for (const auto& [id, orden] : s->extras) extras.push_back(id);
        for (const auto& x : extras) emision_->stopExtra(x);
        emision_->stopCleanFeed(s->id);
        emision_->stopStreaming(s->id);
        emision_->stopRecording(s->id);
    }
    emision_->detener(); // corre lo encolado (las paradas) y se apaga
    emisor_->desenganchar();
    retorno_->stopReturnFeeds();
    vistas_->disablePreview();
    vistas_->soltarVistasDeEscena();
    for (const auto& s : lasSalidas()) vistas_->dejarDeMedir(*s);
    fuentes::ponerMotorApp(nullptr);
    fuentes::ponerSalida(nullptr);
    fuentes::ponerCambiar(nullptr);
    fuentes::ponerConsultas({});
    fuentes::bajaTodas();
    {
        // Las vistas se leen desde la interfaz (latestPreview…): el motor se va con el candado.
        const std::lock_guard<std::mutex> l(vistas_->candado());
        motor_->detener();
        motor_.reset();
        nivelesCanal_.ponerNiveles(nullptr);
    }
    escenas_->vaciar();
    // Las salidas que no son la por omisión se van con el motor (resetVideo las vuelve
    // a crear); la por omisión queda, sin nada al aire (su loudness sigue: hay quien
    // tiene su referencia).
    {
        std::lock_guard<std::mutex> l(salidasM_);
        for (auto it = salidas_.begin(); it != salidas_.end();)
            it = it->first == kSalidaPorOmision ? std::next(it) : salidas_.erase(it);
        ordenSalidas_ = {kSalidaPorOmision};
    }
    emision_->quedarseConLaPorOmision();
    SalidaAire& p = porOmision();
    p.alAire.clear();
    p.armada.clear();
    for (auto& d : p.dsk) d = DskSalida{};
    p.ftb = p.placa = p.ftbAudio = false;
}

bool BroadcastEngine::resetVideo(const VideoConfig& video, const std::function<VideoConfig()>& conElMotorApagado) {
    const TurnoMotor::Guarda turno(turno_);
    if (!started_) return false;
    if (outputsActive() && !conElMotorApagado) {
        setError(ba::i18n::traducir("emision", "no se puede cambiar la resolucion con salidas activas"));
        return false;
    }
    // El lienzo es de todo el motor: se arma uno nuevo con todo lo que había.
    const EscenasYFuentes::Copia copia = escenas_->copia();
    // Las salidas: lo que tenía al aire y armado cada una, sus capas (DSK, placa y negro:
    // el rearme por la GPU o un lienzo nuevo no le sacan al aire lo que tenía encima), y
    // la mezcla, los VCA y el objetivo de loudness de las que no son la por omisión (la
    // por omisión no se va: los suyos quedan).
    struct Antes {
        std::string id, nombre, alAire, armada;
        std::map<std::string, float> volumen;
        std::map<std::string, bool> mute;
        std::map<std::string, float> gananciaGrupo;
        audio::LoudnessPrograma::Objetivo objetivo;
        DskSalida dsk[4];
        bool ftb = false, ftbAudio = false, placa = false;
        std::string placaTexto, placaImagen;
    };
    std::vector<Antes> salidas;
    for (const auto& s : lasSalidas()) {
        Antes a{s->id, s->nombre, s->alAire, s->armada, s->volumen, s->mute, s->gananciaGrupo, s->medidas->loudness.objetivo()};
        std::copy(std::begin(s->dsk), std::end(s->dsk), std::begin(a.dsk));
        a.ftb = s->ftb;
        a.ftbAudio = s->ftbAudio;
        a.placa = s->placa;
        a.placaTexto = s->placaTexto;
        a.placaImagen = s->placaImagen;
        salidas.push_back(std::move(a));
    }
    const bool feeds = retorno_->activo();
    const bool preview = vistas_->previewActiva();
    retorno_->stopReturnFeeds();
    vistas_->disablePreview();
    shutdown();
    if (!startup(paths_, conElMotorApagado ? conElMotorApagado() : video, audio_)) return false;
    for (const auto& n : copia.orden) {
        const auto& f = copia.fuentes.at(n);
        escenas_->crearFuente(f.spec, f.global);
        if (f.syncMs != 0) mezcla_->setSourceSyncOffset(n, f.syncMs);
        if (f.pistas != std::vector<int>{1}) mezcla_->setSourcePistas(n, f.pistas);
        if (f.monitor) mezcla_->setSourceMonitoring(n, true);
    }
    // La escucha como estaba (el solo, o la fija con su salida) en el motor nuevo.
    mezcla_->reponerEscucha();
    escenas_->reponer(copia);
    for (const Antes& a : salidas) {
        if (a.id != kSalidaPorOmision && !crearSalida(a.id, a.nombre)) continue;
        if (a.id != kSalidaPorOmision) {
            for (const auto& [n, v] : a.volumen) mezcla_->setSourceVolume(n, v, a.id);
            for (const auto& [n, m] : a.mute) mezcla_->setSourceMuted(n, m, a.id);
            for (const auto& [g, v] : a.gananciaGrupo) mezcla_->setGananciaGrupo(g, v, a.id);
            laSalida(a.id)->medidas->loudness.ponerObjetivo(
                {{"perfil", a.objetivo.perfil}, {"lufs", a.objetivo.lufs}, {"tp", a.objetivo.tp}, {"agc", a.objetivo.agc}});
        }
        if (!a.alAire.empty()) escenas_->setCurrentSceneCon(a.alAire, "cut", 0, a.id);
        if (!a.armada.empty()) escenas_->armar(a.armada, a.id);
        // Las capas vuelven como estaban, sin fundido (por la misma API: las internas de
        // la placa y el negro se piden de nuevo al motor nuevo, y el negro baja el audio).
        for (int i = 0; i < 4; ++i) {
            const DskSalida& d = a.dsk[i];
            if (!d.contenido.empty()) escenas_->dskArmar(i + 1, d.contenido, d.recuadro, a.id);
            if (d.visible && !d.contenido.empty()) escenas_->dskMostrar(i + 1, true, 0, a.id);
        }
        if (a.placa) escenas_->placa(true, a.placaTexto, a.placaImagen, 0, a.id);
        if (a.ftb) escenas_->ftb(true, 0, a.ftbAudio, a.id);
        escenas_->publicarCapas(*laSalida(a.id), 0);
    }
    mezcla_->aplicarLoudness(); // el objetivo de cada una, al motor nuevo
    const std::map<int, std::string> retornos = mezcla_->retornos(); // (sobre una copia: se reponen)
    for (const auto& [slot, modo] : retornos) mezcla_->setRetornoInvitado(slot, modo);
    if (feeds) retorno_->publishReturnFeeds();
    if (preview) vistas_->enablePreview(video_.outputWidth / 2, video_.outputHeight / 2);
    return true;
}

// ============================================================================
// Las salidas (docs/planes/PLAN-SALIDAS.md, etapa 2)
// ============================================================================

SalidaAire* BroadcastEngine::laSalida(const std::string& id) {
    const auto it = salidas_.find(id.empty() ? std::string(kSalidaPorOmision) : id);
    if (it != salidas_.end()) return it->second.get();
    setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no hay una salida «%1»"), {id}));
    return nullptr;
}

const SalidaAire* BroadcastEngine::laSalida(const std::string& id) const {
    const auto it = salidas_.find(id.empty() ? std::string(kSalidaPorOmision) : id);
    return it == salidas_.end() ? nullptr : it->second.get();
}

std::shared_ptr<SalidaAire> BroadcastEngine::laSalidaCompartida(const std::string& id) {
    const auto it = salidas_.find(id.empty() ? std::string(kSalidaPorOmision) : id);
    if (it != salidas_.end()) return it->second;
    setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no hay una salida «%1»"), {id}));
    return nullptr;
}

SalidaAire& BroadcastEngine::porOmision() { return *salidas_.at(kSalidaPorOmision); }

const SalidaAire& BroadcastEngine::porOmision() const { return *salidas_.at(kSalidaPorOmision); }

std::vector<std::shared_ptr<SalidaAire>> BroadcastEngine::lasSalidas() const {
    std::lock_guard<std::mutex> l(salidasM_);
    std::vector<std::shared_ptr<SalidaAire>> out;
    for (const auto& id : ordenSalidas_) out.push_back(salidas_.at(id));
    return out;
}

std::vector<std::string> BroadcastEngine::salidaIds() const {
    const TurnoMotor::Guarda turno(turno_);
    return ordenSalidas_;
}

bool BroadcastEngine::existeSalida(const std::string& id) const {
    const TurnoMotor::Guarda turno(turno_);
    return laSalida(id) != nullptr;
}

std::string BroadcastEngine::idDeSalida(const std::string& salida) const {
    const TurnoMotor::Guarda turno(turno_);
    const SalidaAire* s = laSalida(salida);
    return s != nullptr ? s->id : std::string();
}

bool BroadcastEngine::crearSalida(const std::string& id, const std::string& nombre) {
    const TurnoMotor::Guarda turno(turno_);
    if (!started_ || !motor_) {
        setError(ba::i18n::traducir("emision", "el motor no está andando"));
        return false;
    }
    if (std::string e = salidas::validarId(id); !e.empty()) {
        setError(e);
        return false;
    }
    if (salidas_.count(id) > 0) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "ya hay una salida «%1»"), {id}));
        return false;
    }
    // En el motor: su programa, su preview y su mezcla (va en su cola: un error
    // que llegue después vuelve por alFallarMotor).
    if (std::string e = motor_->crearSalida(id, nombre); !e.empty()) {
        setError(e);
        return false;
    }
    auto s = std::make_shared<SalidaAire>();
    s->id = id;
    s->nombre = nombre.empty() ? id : nombre;
    s->emisor = EmisorAparte::salidasConEmisor().count(id) > 0;
    s->retorno = std::make_shared<RetornoDeSalida>();
    s->retorno->id = id;
    s->emision = std::make_shared<SalidaEmision>();
    s->emision->id = id;
    s->emision->emisor = s->emisor;
    // Su mezcla arranca como la de la por omisión (lo mismo hace el motor con sus mandos).
    for (const auto& [n, f] : escenas_->todasLasFuentes()) {
        s->volumen[n] = f.spec.volume;
        s->mute[n] = f.spec.muted;
    }
    // Y el objetivo de loudness (el suyo, si lo guardó, lo pone después ObjetivoLoudness).
    {
        const auto ob = porOmision().medidas->loudness.objetivo();
        s->medidas->loudness.ponerObjetivo({{"perfil", ob.perfil}, {"lufs", ob.lufs}, {"tp", ob.tp}, {"agc", ob.agc}});
    }
    {
        std::lock_guard<std::mutex> l(salidasM_);
        salidas_[id] = s;
        ordenSalidas_.push_back(id);
    }
    emision_->alta(s->emision);
    vistas_->medirSalida(*s);
    if (retorno_->activo()) retorno_->publicarRetorno(s->retorno);
    if (s->emisor) emisor_->publicarCanal(s->canalEmisor, s->id, false, false); // lo que lee su emisor, siempre (F3-01)
    mezcla_->aplicarLoudness();
    return true;
}

bool BroadcastEngine::borrarSalida(const std::string& id) {
    const TurnoMotor::Guarda turno(turno_);
    if (id.empty() || id == kSalidaPorOmision) {
        setError(ba::i18n::traducir("emision", "la salida por omisión no se puede borrar"));
        return false;
    }
    std::shared_ptr<SalidaAire> s = laSalidaCompartida(id);
    if (!s) return false;
    // Lo que sale de ella se para en el hilo de salidas, después de lo que ya estaba encolado.
    emision_->baja(s);
    retorno_->soltarRetorno(*s->retorno);
    emisor_->soltarCanal(s->canalEmisor, true);
    vistas_->dejarDeMedir(*s);
    {
        std::lock_guard<std::mutex> l(salidasM_);
        salidas_.erase(id);
        ordenSalidas_.erase(std::remove(ordenSalidas_.begin(), ordenSalidas_.end(), id), ordenSalidas_.end());
    }
    // El motor la desarma aparte (y una nueva con el mismo id espera ese desarme).
    if (motor_)
        if (std::string e = motor_->borrarSalida(id); !e.empty()) {
            setError(e);
            return false;
        }
    mezcla_->alBorrarSalida(id);
    return true;
}

// ============================================================================
// El estado, los avisos y el reloj
// ============================================================================

const json& BroadcastEngine::estadoMotor() const {
    // La foto que publica el bucle del motor: leerla no espera el candado (ver
    // Motor::instantanea). Se guarda la copia para devolver una referencia que
    // no cambie mientras el que llama la recorre.
    // La foto no cambia nunca (el motor publica una nueva): alcanza con quedarse con
    // ella, sin copiarla (se copiaba entera con cada foto nueva, varias por segundo).
    // Sin foto todavía, un objeto vacío: arrancando, `value()` sobre un null tiraba
    // excepción (caída del 2026-10-11).
    static const json vacio = json::object();
    if (motor_)
        if (auto f = motor_->instantanea(); f && f.get() != estadoFoto_.get() && f->is_object()) estadoFoto_ = f;
    return estadoFoto_ ? *estadoFoto_ : vacio;
}

void BroadcastEngine::avisar(const AvisoMotor& a) {
    ba::diag::registro().linea(a.nivel == AvisoMotor::Nivel::Bien ? ba::diag::Nivel::Info : ba::diag::Nivel::Aviso, "salidas", 0,
                               a.texto);
    std::function<void(const AvisoMotor&)> fn;
    {
        std::lock_guard<std::mutex> l(falloMotorM_);
        fn = falloMotor_;
    }
    if (fn) fn(a);
}

void BroadcastEngine::alFallarMotor(std::function<void(const AvisoMotor&)> fn) {
    std::lock_guard<std::mutex> l(falloMotorM_);
    falloMotor_ = std::move(fn);
}

void BroadcastEngine::alCambiarAudioFuente(std::function<void(const std::string&, const char*)> fn) {
    const TurnoMotor::Guarda turno(turno_);
    cambioAudio_ = std::move(fn);
}

void BroadcastEngine::atenderSalidas() {
    const TurnoMotor::Guarda turno(turno_);
    emision_->atenderBuzon();
}

bool BroadcastEngine::ponerReloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio) {
    const TurnoMotor::Guarda turno(turno_);
    if (tipo == relojTipo_ && servidor == relojServidor_ && puerto == relojPuerto_ && dominio == relojDominio_) return true;
    relojTipo_ = tipo;
    relojServidor_ = servidor;
    relojPuerto_ = puerto;
    relojDominio_ = dominio;
    // Todo el estudio corre con ese reloj: se arma de nuevo con lo que había.
    return !started_ || resetVideo(video_);
}

std::pair<std::string, std::string> BroadcastEngine::relojEstado() const {
    const TurnoMotor::Guarda turno(turno_);
    return {relojTipo_.empty() ? std::string("sistema") : relojTipo_, motor_ ? motor_->reloj().error() : std::string()};
}

std::string BroadcastEngine::publicarReloj(int puerto) {
    const TurnoMotor::Guarda turno(turno_);
    relojPublicar_ = std::max(0, puerto);
    return motor_ ? motor_->publicarReloj(relojPublicar_) : std::string();
}

EngineStatus BroadcastEngine::status() const {
    const TurnoMotor::Guarda turno(turno_);
    EngineStatus st;
    {
        std::lock_guard<std::mutex> l(errorM_);
        st.lastError = ultimoError_;
    }
    if (!started_) return st;
    st.videoActive = true;
    st.audioActive = true;
    st.fps = static_cast<double>(video_.fpsNum) / std::max(1, video_.fpsDen);
    const json f = emision_->fotoSalidas();
    const json t = f.value("principal", json::object());
    if (!t.empty()) {
        st.streaming = t.value("andando", false);
        st.droppedFrames = t.value("perdidos", 0);
        st.totalFrames = t.value("cuadros", 0);
        st.bytesSent = t.value("bytes", std::uint64_t{0});
        st.videoEncoder = t.value("codificador", std::string());
        st.reconnecting = t.value("reconectando", false);
        if (st.reconnecting) {
            const std::string e = t.value("error", std::string());
            st.outputProblem = e.find("refused") != std::string::npos   ? ba::i18n::traducir("emision", "No se pudo conectar con el servidor")
                               : e.find("auth") != std::string::npos    ? ba::i18n::traducir("emision", "El servidor rechazó la transmisión (¿la clave es correcta?)")
                                                                        : ba::i18n::traducir("emision", "Se cortó la conexión con el servidor");
        }
    }
    const json g = f.value("grabacion", json::object());
    st.recording = g.value("activa", false);
    st.recordingDown = g.value("caida", false);
    if (st.videoEncoder.empty()) st.videoEncoder = g.value("estado", json::object()).value("codificador", std::string());
    // El resumen de cada salida (la por omisión, igual que los campos de arriba).
    const json todas = f.value("salidas", json::object());
    for (const auto& id : ordenSalidas_) {
        const SalidaAire& s = *salidas_.at(id);
        const json fs = todas.value(id, json::object());
        EngineStatus::Salida r;
        r.id = id;
        r.nombre = s.nombre;
        r.porOmision = s.porOmision();
        r.escena = escenas_->nombreDe(s.alAire);
        const json p = fs.value("principal", json::object());
        r.streaming = p.value("andando", false);
        r.reconnecting = p.value("reconectando", false);
        r.recording = fs.value("grabacion", json::object()).value("activa", false);
        r.recordingDown = fs.value("grabacion", json::object()).value("caida", false);
        const json ex = fs.value("extras", json::object());
        for (const auto& [eid, x] : ex.items()) r.destinos += x.value("andando", false) ? 1 : 0;
        r.limpia = fs.value("limpio", json::object()).value("activa", false);
        r.repeticion = fs.value("replay", json::object()).value("activa", false);
        st.salidas.push_back(std::move(r));
    }
    return st;
}

// ============================================================================
// Capacidades (no miran el estado del motor: desde cualquier hilo)
// ============================================================================

std::string BroadcastEngine::graphicsDeviceName() {
#if defined(__APPLE__)
    return "Apple (VideoToolbox, GStreamer)";
#else
    return "GStreamer";
#endif
}

std::vector<BroadcastEngine::EncoderInfo> BroadcastEngine::videoEncoders() {
    std::vector<EncoderInfo> out;
    for (const char* f : {"vtenc_h264_hw", "vtenc_h264", "nvh264enc", "qsvh264enc", "d3d12h264enc", "amfh264enc", "vah264enc",
                          "mfh264enc", "x264enc"}) {
        if (!motor::hayFabrica(f)) continue;
        const std::string id = f;
        out.push_back({id, id, "h264", id != "x264enc"});
    }
    return out;
}

std::vector<std::string> BroadcastEngine::videoEncoderCandidates(const EncoderConfig& enc) {
    std::vector<std::string> pedidos;
    const std::string& v = enc.videoEncoder;
    if (v == "videotoolbox") pedidos = {"vtenc_h264_hw", "vtenc_h264"};
    else if (v == "nvenc") pedidos = {"nvh264enc"};
    else if (v == "qsv") pedidos = {"qsvh264enc"};
    else if (v == "amf") pedidos = {"amfh264enc"};
    else if (v == "vaapi") pedidos = {"vah264enc", "vah264lpenc"};
    else if (v == "x264") pedidos = {"x264enc"};
    else if (!v.empty() && v != "auto") pedidos = {v};
    else if (enc.preferHardware) pedidos = {"vtenc_h264_hw", "nvh264enc", "qsvh264enc", "d3d12h264enc", "amfh264enc", "vah264enc", "vtenc_h264"};
    std::vector<std::string> out;
    for (const auto& p : pedidos)
        if (motor::hayFabrica(p)) out.push_back(p);
    if (std::find(out.begin(), out.end(), "x264enc") == out.end() && motor::hayFabrica("x264enc")) out.push_back("x264enc");
    return out;
}

std::vector<SourceType> BroadcastEngine::tiposDeFuente() { return EscenasYFuentes::tiposDeFuente(); }

std::string BroadcastEngine::grupoDeAudio(const std::string& nombre) { return MezclaApp::grupoDeAudio(nombre); }

int BroadcastEngine::kbpsGrabacion(const RecordConfig& rec, const EncoderConfig& enc, bool conPistas) {
    return EmisionSalidas::kbpsGrabacion(rec, enc, conPistas);
}

bool BroadcastEngine::outputsActive() const {
    const TurnoMotor::Guarda turno(turno_);
    return emision_->outputsActive();
}

bool BroadcastEngine::resetEncoders() {
    const TurnoMotor::Guarda turno(turno_);
    if (outputsActive()) {
        setError(ba::i18n::traducir("emision", "no se puede cambiar la codificacion con salidas activas"));
        return false;
    }
    return true;
}

int BroadcastEngine::enviarCaption(const std::string&, double) { return 0; }

void BroadcastEngine::holdShowing(const std::string&, bool) {}

int BroadcastEngine::adoptPendingAudio() { return 0; }

// ============================================================================
// Lo que delega en las piezas (con el turno donde lo tomaba siempre)
// ============================================================================

#define BA_TURNO const TurnoMotor::Guarda turno(turno_)

// --- Escenas y fuentes (EscenasYFuentes) ---------------------------------------------------
std::string BroadcastEngine::errorDeCompositor(const std::string& nombre) const { BA_TURNO; return escenas_->errorDeCompositor(nombre); }
bool BroadcastEngine::createScene(const std::string& name, const std::string& id) { BA_TURNO; return escenas_->createScene(name, id); }
bool BroadcastEngine::removeScene(const std::string& name) { BA_TURNO; return escenas_->removeScene(name); }
bool BroadcastEngine::renameScene(const std::string& from, const std::string& to) { BA_TURNO; return escenas_->renameScene(from, to); }
std::vector<std::string> BroadcastEngine::sceneNames() const { BA_TURNO; return escenas_->sceneNames(); }
bool BroadcastEngine::setCurrentScene(const std::string& name, const std::string& salida) { BA_TURNO; return escenas_->setCurrentScene(name, salida); }
std::string BroadcastEngine::currentScene(const std::string& salida) const { BA_TURNO; return escenas_->currentScene(salida); }
std::string BroadcastEngine::currentSceneId(const std::string& salida) const { BA_TURNO; return escenas_->currentSceneId(salida); }
std::string BroadcastEngine::armadaId(const std::string& salida) const { BA_TURNO; return escenas_->armadaId(salida); }
std::string BroadcastEngine::sceneId(const std::string& escena) const { BA_TURNO; return escenas_->idDe(escena); }
std::string BroadcastEngine::sceneName(const std::string& escena) const { BA_TURNO; return escenas_->nombreDe(escena); }
escenas::TablaEscenas BroadcastEngine::tablaEscenas() const { BA_TURNO; return escenas_->tabla(); }
bool BroadcastEngine::setTransition(const std::string& type, int durationMs) { BA_TURNO; return escenas_->setTransition(type, durationMs); }
bool BroadcastEngine::setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    BA_TURNO;
    return escenas_->setCurrentSceneCon(name, tipo, ms, salida);
}
bool BroadcastEngine::transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida) {
    BA_TURNO;
    return escenas_->transicionManual(name, t, soltar, alAire, salida);
}
std::string BroadcastEngine::manualHacia(const std::string& salida) const { BA_TURNO; return escenas_->manualHacia(salida); }
bool BroadcastEngine::armar(const std::string& name, const std::string& salida) { BA_TURNO; return escenas_->armar(name, salida); }
bool BroadcastEngine::buildScene() { BA_TURNO; return escenas_->buildScene(); }
bool BroadcastEngine::ftb(bool activar, int ms, bool bajarAudio, const std::string& salida) { BA_TURNO; return escenas_->ftb(activar, ms, bajarAudio, salida); }
bool BroadcastEngine::placa(bool activar, const std::string& texto, const std::string& imagen, int ms, const std::string& salida) {
    BA_TURNO;
    return escenas_->placa(activar, texto, imagen, ms, salida);
}
nlohmann::json BroadcastEngine::estadoPlaca(const std::string& salida) const { BA_TURNO; return escenas_->estadoPlaca(salida); }
bool BroadcastEngine::dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro,
                               const std::string& salida) {
    BA_TURNO;
    return escenas_->dskArmar(capa, contenido, recuadro, salida);
}
bool BroadcastEngine::dskMostrar(int capa, bool visible, int ms, const std::string& salida) { BA_TURNO; return escenas_->dskMostrar(capa, visible, ms, salida); }
nlohmann::json BroadcastEngine::dskEstado(const std::string& salida) const { BA_TURNO; return escenas_->dskEstado(salida); }
std::vector<std::string> BroadcastEngine::fuentesDsk(const std::string& salida) const { BA_TURNO; return escenas_->fuentesDsk(salida); }
bool BroadcastEngine::textoGlobal(const std::string& nombre, const std::string& texto) { BA_TURNO; return escenas_->textoGlobal(nombre, texto); }
void BroadcastEngine::setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar) { BA_TURNO; escenas_->setRanurasBanca(std::move(ranuras), forzar); }
bool BroadcastEngine::hayFuenteBanca() const { BA_TURNO; return escenas_->hayFuenteBanca(); }
std::string BroadcastEngine::escenaDeRanura(int ranura) const { BA_TURNO; return escenas_->escenaDeRanura(ranura); }
std::vector<std::string> BroadcastEngine::fuentesEnPrograma(const std::string& salida) const { BA_TURNO; return escenas_->fuentesEnPrograma(salida); }
std::vector<std::string> BroadcastEngine::fuentesEnPrograma() const { BA_TURNO; return escenas_->fuentesEnPrograma(); }
bool BroadcastEngine::fuenteAlAire(const std::string& fuente) const { BA_TURNO; return escenas_->fuenteAlAire(fuente); }
std::string BroadcastEngine::ponerVideoDeGrafico(const std::string& grafico, const nlohmann::json& capas) { BA_TURNO; return escenas_->ponerVideoDeGrafico(grafico, capas); }
void BroadcastEngine::empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs) { BA_TURNO; escenas_->empezarVideoDeGrafico(grafico, desdeNs); }
void BroadcastEngine::quitarVideoDeGrafico(const std::string& grafico) { BA_TURNO; escenas_->quitarVideoDeGrafico(grafico); }
bool BroadcastEngine::addSource(const std::string& scene, const SourceSpec& spec) { BA_TURNO; return escenas_->addSource(scene, spec); }
bool BroadcastEngine::addSource(const SourceSpec& spec) { BA_TURNO; return escenas_->addSource(spec); }
bool BroadcastEngine::removeSource(const std::string& scene, const std::string& name) { BA_TURNO; return escenas_->removeSource(scene, name); }
bool BroadcastEngine::moveSource(const std::string& scene, const std::string& name, int delta) { BA_TURNO; return escenas_->moveSource(scene, name, delta); }
std::vector<std::string> BroadcastEngine::sceneSources(const std::string& scene) const { BA_TURNO; return escenas_->sceneSources(scene); }
bool BroadcastEngine::setSourceVisible(const std::string& scene, const std::string& name, bool visible) {
    BA_TURNO;
    return escenas_->setSourceVisible(scene, name, visible);
}
bool BroadcastEngine::sourceVisible(const std::string& scene, const std::string& name) const { BA_TURNO; return escenas_->sourceVisible(scene, name); }
bool BroadcastEngine::setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) {
    BA_TURNO;
    return escenas_->setSourceRect(scene, name, rect);
}
bool BroadcastEngine::setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba, float derecha,
                                    float abajo) {
    BA_TURNO;
    return escenas_->setSourceCrop(scene, name, izquierda, arriba, derecha, abajo);
}
bool BroadcastEngine::setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos) {
    BA_TURNO;
    return escenas_->setSourceEfectos(scene, name, efectos);
}
EfectosCapa BroadcastEngine::sourceEfectos(const std::string& scene, const std::string& name) const { BA_TURNO; return escenas_->sourceEfectos(scene, name); }
void BroadcastEngine::aplicarRecortesPendientes() { BA_TURNO; escenas_->aplicarRecortesPendientes(); }
bool BroadcastEngine::renameSource(const std::string& from, const std::string& to) { BA_TURNO; return escenas_->renameSource(from, to); }
std::string BroadcastEngine::sourceTypeId(const std::string& name) const { BA_TURNO; return escenas_->sourceTypeId(name); }
void BroadcastEngine::setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor) {
    BA_TURNO;
    escenas_->setTraer(entrada, compositor, productor);
}
bool BroadcastEngine::sourceExists(const std::string& name) const { BA_TURNO; return escenas_->sourceExists(name); }
nlohmann::json BroadcastEngine::estadoEntrada(const std::string& name) const { BA_TURNO; return escenas_->estadoEntrada(name); }
std::vector<std::pair<std::string, CanvasRect>> BroadcastEngine::itemsVisibles(const std::string& scene) const { BA_TURNO; return escenas_->itemsVisibles(scene); }
std::string BroadcastEngine::controlEntrada(const std::string& name, const nlohmann::json& campos) { BA_TURNO; return escenas_->controlEntrada(name, campos); }
std::string BroadcastEngine::sourceSettingsJson(const std::string& name) const { BA_TURNO; return escenas_->sourceSettingsJson(name); }
bool BroadcastEngine::sourceSize(const std::string& name, int& width, int& height) const { BA_TURNO; return escenas_->sourceSize(name, width, height); }
bool BroadcastEngine::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    BA_TURNO;
    return escenas_->updateSourceSettings(name, settingsJson);
}
bool BroadcastEngine::sourceTypeExists(const std::string& typeId) const { BA_TURNO; return escenas_->sourceTypeExists(typeId); }
bool BroadcastEngine::addGuestAudio(int slot) { BA_TURNO; return escenas_->addGuestAudio(slot); }
bool BroadcastEngine::addGlobalAudio(const SourceSpec& spec) { BA_TURNO; return escenas_->addGlobalAudio(spec); }
bool BroadcastEngine::removeGlobalAudio(const std::string& name) { BA_TURNO; return escenas_->removeGlobalAudio(name); }
std::vector<std::string> BroadcastEngine::globalAudioInputs() const { BA_TURNO; return escenas_->globalAudioInputs(); }
bool BroadcastEngine::addGuestVideo(int slot) { return addGuestVideo(slot, CanvasRect{0.5f, 0.5f, 0.5f, 0.5f}); }
bool BroadcastEngine::addGuestVideo(int slot, const CanvasRect& rect) { BA_TURNO; return escenas_->addGuestVideo(slot, rect); }
std::vector<std::string> BroadcastEngine::sourceNames() const { BA_TURNO; return escenas_->sourceNames(); }

// --- La mezcla (MezclaApp) -----------------------------------------------------------------
std::vector<std::string> BroadcastEngine::audioSources() const { BA_TURNO; return mezcla_->audioSources(); }
bool BroadcastEngine::setSourceVolume(const std::string& name, float mul, const std::string& salida) { BA_TURNO; return mezcla_->setSourceVolume(name, mul, salida); }
float BroadcastEngine::sourceVolume(const std::string& name, const std::string& salida) const { BA_TURNO; return mezcla_->sourceVolume(name, salida); }
void BroadcastEngine::setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida) { BA_TURNO; mezcla_->setGananciaGrupo(grupo, mul, salida); }
float BroadcastEngine::gananciaGrupo(const std::string& grupo, const std::string& salida) const { BA_TURNO; return mezcla_->gananciaGrupo(grupo, salida); }
bool BroadcastEngine::setSourceMuted(const std::string& name, bool muted, const std::string& salida) { BA_TURNO; return mezcla_->setSourceMuted(name, muted, salida); }
bool BroadcastEngine::sourceMuted(const std::string& name, const std::string& salida) const { BA_TURNO; return mezcla_->sourceMuted(name, salida); }
float BroadcastEngine::sourcePeakDb(const std::string& name) const { BA_TURNO; return mezcla_->sourcePeakDb(name); }
bool BroadcastEngine::setSourceMonitoring(const std::string& name, bool on) { BA_TURNO; return mezcla_->setSourceMonitoring(name, on); }
bool BroadcastEngine::sourceMonitoring(const std::string& name) const { BA_TURNO; return mezcla_->sourceMonitoring(name); }
bool BroadcastEngine::sourceEscuchaFija(const std::string& name) const { BA_TURNO; return mezcla_->sourceEscuchaFija(name); }
bool BroadcastEngine::setPfl(const std::string& name, const std::string& modo, const std::string& salida) { BA_TURNO; return mezcla_->setPfl(name, modo, salida); }
std::string BroadcastEngine::pfl() const { BA_TURNO; return mezcla_->pfl(); }
std::string BroadcastEngine::pflModo() const { BA_TURNO; return mezcla_->pflModo(); }
bool BroadcastEngine::setSalidaEscucha(const std::string& salida) { BA_TURNO; return mezcla_->setSalidaEscucha(salida); }
std::string BroadcastEngine::salidaEscucha() const { BA_TURNO; return mezcla_->salidaEscucha(); }
bool BroadcastEngine::setSourceSyncOffset(const std::string& name, int ms) { BA_TURNO; return mezcla_->setSourceSyncOffset(name, ms); }
int BroadcastEngine::sourceSyncOffset(const std::string& name) const { BA_TURNO; return mezcla_->sourceSyncOffset(name); }
bool BroadcastEngine::setSourcePistas(const std::string& name, const std::vector<int>& pistas) { BA_TURNO; return mezcla_->setSourcePistas(name, pistas); }
std::vector<int> BroadcastEngine::sourcePistas(const std::string& name) const { BA_TURNO; return mezcla_->sourcePistas(name); }
void BroadcastEngine::setRetornoInvitado(int slot, const std::string& modo) { BA_TURNO; mezcla_->setRetornoInvitado(slot, modo); }
std::string BroadcastEngine::retornoInvitado(int slot) const { BA_TURNO; return mezcla_->retornoInvitado(slot); }
audio::LoudnessPrograma& BroadcastEngine::loudness(const std::string& salida) { BA_TURNO; return mezcla_->loudness(salida); }
audio::LoudnessPrograma* BroadcastEngine::loudnessDe(const std::string& salida) { BA_TURNO; return mezcla_->loudnessDe(salida); }
void BroadcastEngine::aplicarLoudness() { BA_TURNO; mezcla_->aplicarLoudness(); }
int BroadcastEngine::pistasGrabacionMask() const { BA_TURNO; return mezcla_->pistasGrabacionMask(); }
const audio::LoudnessPrograma& BroadcastEngine::loudnessPrograma() const { return medidasPrograma_->loudness; }

// --- Los medios (MediosApp) ----------------------------------------------------------------
std::vector<BroadcastEngine::MediaInfo> BroadcastEngine::mediaSources() const { BA_TURNO; return medios_->mediaSources(); }
bool BroadcastEngine::playMedia(const std::string& name, const std::string& file, bool loop, float volume) { BA_TURNO; return medios_->playMedia(name, file, loop, volume); }
bool BroadcastEngine::stopMedia(const std::string& name) { BA_TURNO; return medios_->stopMedia(name); }
bool BroadcastEngine::restartMedia(const std::string& name) { BA_TURNO; return medios_->restartMedia(name); }
bool BroadcastEngine::seekMedia(const std::string& name, std::int64_t ms) { BA_TURNO; return medios_->seekMedia(name, ms); }
bool BroadcastEngine::pararMedia(const std::string& name) { BA_TURNO; return medios_->pararMedia(name); }

// --- Vistas y medición (VistasYMedicion) ---------------------------------------------------
bool BroadcastEngine::enablePreview(int width, int height) { BA_TURNO; return vistas_->enablePreview(width, height); }
void BroadcastEngine::disablePreview() { BA_TURNO; vistas_->disablePreview(); }
bool BroadcastEngine::latestPreview(motor::Cuadro& out, std::uint64_t& serial) const { return vistas_->latestPreview(out, serial); }
bool BroadcastEngine::enableScenePreview(const std::string& canal, const std::string& name, int maxWidth) {
    BA_TURNO;
    return vistas_->enableScenePreview(canal, name, maxWidth);
}
bool BroadcastEngine::enableSalidaPreview(const std::string& canal, const std::string& bus, const std::string& salida, int maxWidth) {
    BA_TURNO;
    return vistas_->enableSalidaPreview(canal, bus, salida, maxWidth);
}
void BroadcastEngine::disableScenePreview(const std::string& canal) { BA_TURNO; vistas_->disableScenePreview(canal); }
void BroadcastEngine::medirVista(const std::string& clave) { BA_TURNO; vistas_->medirVista(clave); }
std::string BroadcastEngine::scenePreviewName(const std::string& canal) const { return vistas_->scenePreviewName(canal); }
bool BroadcastEngine::renderScenePreview(const std::string& canal, motor::Cuadro& out, std::uint64_t& serial) {
    return vistas_->renderScenePreview(canal, out, serial);
}
void BroadcastEngine::capturarAparte(const std::string& name, int maxWidth, Captura listo) { vistas_->capturarAparte(name, maxWidth, std::move(listo)); }
std::string BroadcastEngine::prepararCaptura(const std::string& name, int maxWidth, std::string& clave, int& w, int& h,
                                             bool& esEscena) const {
    BA_TURNO;
    return vistas_->prepararCaptura(name, maxWidth, clave, w, h, esEscena);
}
std::string BroadcastEngine::capturarVista(const std::string& clave, int w, int h, bool esEscena, std::vector<std::uint8_t>& rgba,
                                           int& width, int& height) {
    return vistas_->capturarVista(clave, w, h, esEscena, rgba, width, height);
}
void BroadcastEngine::programPeaksDb(float& left, float& right, const std::string& salida) { BA_TURNO; vistas_->programPeaksDb(left, right, salida); }
float BroadcastEngine::picoProgramaVigiaDb() { return vistas_->picoProgramaVigiaDb(); }
BroadcastEngine::ProgramaVideo BroadcastEngine::programaVideo(const std::string& salida) const { return vistas_->programaVideo(salida); }
std::uint64_t BroadcastEngine::cuadrosPrograma() const { return medidasPrograma_->cuadros.load(); }
bool BroadcastEngine::empezarMedicionSincro(const std::string& salida) { BA_TURNO; return vistas_->empezarMedicionSincro(salida); }
nlohmann::json BroadcastEngine::terminarMedicionSincro() { BA_TURNO; return vistas_->terminarMedicionSincro(); }
bool BroadcastEngine::midiendoSincro() const { BA_TURNO; return vistas_->midiendoSincro(); }
nlohmann::json BroadcastEngine::latenciaEstudio() const { BA_TURNO; return vistas_->latenciaEstudio(); }
bool BroadcastEngine::bajarLatenciaEstudio() { return vistas_->bajarLatenciaEstudio(); }
void BroadcastEngine::ponerTopeEstudio(long long ms) { BA_TURNO; vistas_->ponerTopeEstudio(ms); }
void BroadcastEngine::soltarVistasSinMirar(bool on) { BA_TURNO; vistas_->soltarVistasSinMirar(on); }

// --- Al gateway (RetornoGateway) -----------------------------------------------------------
bool BroadcastEngine::publishReturnFeeds() { BA_TURNO; return retorno_->publishReturnFeeds(); }
void BroadcastEngine::stopReturnFeeds() { BA_TURNO; retorno_->stopReturnFeeds(); }
void BroadcastEngine::ponerTopeRetorno(int ancho, int alto) { BA_TURNO; retorno_->ponerTopeRetorno(ancho, alto); }
void BroadcastEngine::retornoEnUso(bool si, const std::string& salida) { BA_TURNO; retorno_->retornoEnUso(si, salida); }
bool BroadcastEngine::escenaACanal(int k, const std::string& escena) { BA_TURNO; return retorno_->escenaACanal(k, escena); }
std::string BroadcastEngine::escenaDeCanal(int k) const { BA_TURNO; return retorno_->escenaDeCanal(k); }
int BroadcastEngine::tomarCanalEscena(const std::string& escena, const std::string& dueno) { BA_TURNO; return retorno_->tomarCanalEscena(escena, dueno); }
void BroadcastEngine::soltarCanalesEscena(const std::string& dueno, const std::set<int>& salvo) { BA_TURNO; retorno_->soltarCanalesEscena(dueno, salvo); }
bool BroadcastEngine::canalEscenaConDuenos(int k) const { BA_TURNO; return retorno_->canalEscenaConDuenos(k); }

// --- Lo que sale (EmisionSalidas y VigiaDisco) ---------------------------------------------
nlohmann::json BroadcastEngine::fotoSalidas() const { return emision_->fotoSalidas(); }
bool BroadcastEngine::haySalida(bool contarGrabacion) const { return emision_->haySalida(contarGrabacion); }
bool BroadcastEngine::haySalidaEn(const std::string& salida, bool contarGrabacion) const { BA_TURNO; return emision_->haySalidaEn(salida, contarGrabacion); }
bool BroadcastEngine::startStreaming(const StreamConfig& stream, const EncoderConfig& enc, bool cambiarEnCaliente,
                                     const std::string& salida, AlResultado alTerminar) {
    BA_TURNO;
    return emision_->startStreaming(stream, enc, cambiarEnCaliente, salida, std::move(alTerminar));
}
BroadcastEngine::Inicio BroadcastEngine::ultimoInicio() const { return emision_->ultimoInicio(); }
std::string BroadcastEngine::destinoActivo(const std::string& salida) const { BA_TURNO; return emision_->destinoActivo(salida); }
bool BroadcastEngine::startExtra(const std::string& id, const std::string& server, const std::string& key, const EncoderConfig& enc,
                                 const std::string& salida, const std::string& serverRespaldo) {
    BA_TURNO;
    return emision_->startExtra(id, server, key, enc, salida, serverRespaldo);
}
void BroadcastEngine::stopExtra(const std::string& id) { BA_TURNO; emision_->stopExtra(id); }
void BroadcastEngine::stopExtras() { BA_TURNO; emision_->stopExtras(); }
nlohmann::json BroadcastEngine::extrasEstado() const { BA_TURNO; return emision_->extrasEstado(); }
nlohmann::json BroadcastEngine::saludSalidas() const { BA_TURNO; return emision_->saludSalidas(); }
void BroadcastEngine::stopStreaming(const std::string& salida) { BA_TURNO; emision_->stopStreaming(salida); }
bool BroadcastEngine::startRecording(const RecordConfig& rec, const EncoderConfig& enc, const std::string& salida,
                                     AlResultado alTerminar) {
    BA_TURNO;
    return emision_->startRecording(rec, enc, salida, std::move(alTerminar));
}
void BroadcastEngine::stopRecording(const std::string& salida) { BA_TURNO; emision_->stopRecording(salida); }
nlohmann::json BroadcastEngine::grabacionEstado(const std::string& salida) const { BA_TURNO; return emision_->grabacionEstado(salida); }
std::string BroadcastEngine::archivoGrabacion(const std::string& salida) const { BA_TURNO; return emision_->archivoGrabacion(salida); }
bool BroadcastEngine::remuxVarios(const std::vector<std::string>& archivos, const std::string& formato, bool borrarOrigen) {
    BA_TURNO;
    return emision_->remuxVarios(archivos, formato, borrarOrigen);
}
bool BroadcastEngine::remux(const std::string& archivo, const std::string& formato, bool borrarOrigen) {
    BA_TURNO;
    return emision_->remuxVarios({archivo}, formato, borrarOrigen);
}
nlohmann::json BroadcastEngine::remuxEstado() const { BA_TURNO; return emision_->remuxEstado(); }
bool BroadcastEngine::setReplay(int segundos, const std::string& dir, const EncoderConfig& enc, const std::string& salida) {
    BA_TURNO;
    return emision_->setReplay(segundos, dir, enc, salida);
}
bool BroadcastEngine::guardarReplay(const std::string& salida) { BA_TURNO; return emision_->guardarReplay(salida); }
std::string BroadcastEngine::ultimoReplay(const std::string& salida) const { BA_TURNO; return emision_->ultimoReplay(salida); }
nlohmann::json BroadcastEngine::replayEstado(const std::string& salida) const { BA_TURNO; return emision_->replayEstado(salida); }
bool BroadcastEngine::encoderRespaldoActivo() const { BA_TURNO; return emision_->encoderRespaldoActivo(); }
bool BroadcastEngine::startSourceRecording(const std::string& source, const std::string& dir, const EncoderConfig& enc) {
    BA_TURNO;
    return emision_->startSourceRecording(source, dir, enc);
}
bool BroadcastEngine::stopSourceRecording(const std::string& source) { BA_TURNO; return emision_->stopSourceRecording(source); }
void BroadcastEngine::stopAllSourceRecordings() { BA_TURNO; emision_->stopAllSourceRecordings(); }
std::vector<BroadcastEngine::GrabacionIndividual> BroadcastEngine::sourceRecordings() const { BA_TURNO; return emision_->sourceRecordings(); }
bool BroadcastEngine::startCleanFeed(const std::string& destino, const std::string& clave, const std::string& dir,
                                     const EncoderConfig& enc, const std::string& salida) {
    BA_TURNO;
    return emision_->startCleanFeed(destino, clave, dir, enc, salida);
}
void BroadcastEngine::stopCleanFeed(const std::string& salida) { BA_TURNO; emision_->stopCleanFeed(salida); }
nlohmann::json BroadcastEngine::cleanFeedEstado(const std::string& salida) const { BA_TURNO; return emision_->cleanFeedEstado(salida); }
void BroadcastEngine::simularDiscoTrabado(int ms) { BA_TURNO; emision_->simularDiscoTrabado(ms); }
std::string BroadcastEngine::modoDisco() const { return disco_->modo(); }
void BroadcastEngine::ponerUmbralesDisco(double apurosS, int recuperadoS) { disco_->ponerUmbrales(apurosS, recuperadoS); }
std::string BroadcastEngine::setModoDisco(const std::string& modo) { BA_TURNO; return disco_->setModo(modo); }
void BroadcastEngine::priorizarPrograma(bool si) { BA_TURNO; disco_->priorizar(si); }
nlohmann::json BroadcastEngine::discoEstado() const { BA_TURNO; return emision_->fotoSalidas().value("disco", json::object()); }

#undef BA_TURNO

} // namespace ba::app
