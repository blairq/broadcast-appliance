// src/app/arranque/PublicadorFoto.cpp — la foto que lee la interfaz y los avisos (F3-03)
//
// Ver PublicadorFoto.hpp y emision/FotoMotor.hpp. Armar corre en el dueño (el plano de
// control) con el turno del motor: lee cfg_, las piezas del runtime y el motor sin pisarse
// con nadie y deja un valor que la interfaz lee sin candados.

#include "arranque/PublicadorFoto.hpp"

#include "arranque/ServicioEmision.hpp"

#include "diag/Registro.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/SalidasConfig.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <tuple>

namespace ba::app {

namespace {

/// Desde cuándo sale algo (ms de pared): lo de la foto anterior mientras siga (o mientras
/// esté caído y el motor lo rearme: `sigue`), ahora si recién empezó, 0 si no sale.
std::int64_t desdeMs(bool sale, bool sigue, std::int64_t antes) {
    if (!sale && !sigue) return 0;
    if (antes > 0) return antes;
    return sale ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
                : 0;
}

} // namespace

void PublicadorFoto::setFoto(std::function<void(std::shared_ptr<const FotoMotor>, unsigned)> publicar) {
    publicarFoto_ = std::move(publicar);
    foto_.reset();
    trabajo_ = FotoMotor{};
    fotoSucia_ = FotoMotor::Todas | kFotoRelerArchivos;
}

int PublicadorFoto::aportarALaFoto(std::function<void(FotoMotor&, unsigned)> aporte) {
    int id = 0;
    {
        const std::lock_guard<std::mutex> l(aportesMu_);
        id = ++proximoAporte_;
        aportes_.emplace_back(id, std::move(aporte));
    }
    fotoSucia(FotoMotor::Todas);
    return id;
}

void PublicadorFoto::dejarDeAportar(int id) {
    const std::lock_guard<std::mutex> l(aportesMu_);
    aportes_.erase(std::remove_if(aportes_.begin(), aportes_.end(), [id](const auto& a) { return a.first == id; }),
                   aportes_.end());
}

void PublicadorFoto::fotoSucia(unsigned partes) const {
    fotoSucia_.fetch_or(partes);
    // Una sola publicación pedida a la vez: las que se juntan mientras tanto salen juntas.
    if (!publicarFoto_ || fotoPedida_.exchange(true)) return;
    rt_.despuesEnDueno([this]() { const_cast<PublicadorFoto*>(this)->publicarFoto(); });
}

void PublicadorFoto::publicarFoto() {
    fotoPedida_ = false;
    if (!publicarFoto_ || !rt_.started_) return;
    const unsigned sucias = fotoSucia_.exchange(0);
    if ((sucias & FotoMotor::Todas) == 0) return;
    const TurnoMotor::Guarda turno(rt_.engine_.turno());
    // Se rearma lo sucio sobre la de trabajo (lo demás sigue como estaba) y se compara
    // SOLO lo sucio: la foto nueva (una copia) se hace solo si algo cambió de verdad.
    armarFoto(trabajo_, sucias);
    {
        const std::lock_guard<std::mutex> l(aportesMu_);
        for (const auto& [id, aporte] : aportes_)
            if (aporte) aporte(trabajo_, sucias);
    }
    // Qué cambió de verdad: la interfaz se entera solo de eso. Con lo sucio se compara lo
    // que arrastra (FotoMotor::kArrastres: campos de una parte que se rehacen con otra).
    const unsigned cambiaron = foto_ ? FotoMotor::queCambio(*foto_, trabajo_, sucias) : unsigned(FotoMotor::Todas);
    // En pruebas, TODA la foto: algo que cambió sin compararse es una vista que no se
    // entera nunca (el reloj del motor publica enseguida y la próxima comparación da igual).
    if (foto_ && rt_.cfg_.modoPrueba)
        if (const unsigned sin = FotoMotor::cambioSinMarcar(*foto_, trabajo_, sucias); sin != 0)
            ba::diag::registro().linea(
                ba::diag::Nivel::Error, "foto", 0,
                ba::i18n::llenar(ba::i18n::traducir("foto", "la foto cambió partes sin compararlas (%1) al rearmar %2: falta su "
                                                            "fila en FotoMotor::kArrastres"),
                                 {std::to_string(sin), std::to_string(sucias)}));
    if (cambiaron == 0) return;
    foto_ = std::make_shared<const FotoMotor>(trabajo_);
    {
        const std::lock_guard<std::mutex> l(ultimaFotoMu_);
        ultimaFoto_ = foto_;
    }
    publicarFoto_(foto_, cambiaron);
}

std::shared_ptr<const FotoMotor> PublicadorFoto::ultimaFoto() const {
    const std::lock_guard<std::mutex> l(ultimaFotoMu_);
    return ultimaFoto_;
}

void PublicadorFoto::armarFoto(FotoMotor& f, unsigned p) {
    // Lo del dueño que se lee acá: la configuración y el motor (con el turno).
    AppConfig& cfg = rt_.cfg_;
    BroadcastEngine& eng = rt_.engine_;
    // --- Escenas: el orden del operador, la carpeta, si está protegida y las fuentes ------
    if ((p & FotoMotor::Escenas) != 0) {
        f.escenas.clear();
        for (const std::string& n : rt_.orderedSceneNames()) {
            FotoMotor::Escena e;
            e.nombre = n;
            e.id = eng.sceneId(n);
            const SceneConfig* sc = findScene(cfg, e.id);
            if (sc != nullptr) {
                e.carpeta = sc->folder;
                e.protegida = sc->protegida;
            }
            for (const std::string& fu : eng.sceneSources(n)) {
                FotoMotor::Fuente x;
                x.nombre = fu;
                x.tipo = eng.sourceTypeId(fu);
                x.visible = eng.sourceVisible(n, fu);
                // El lugar es el guardado (en fracciones del lienzo), como lo mueve el editor.
                if (sc != nullptr)
                    for (const SourceSpec& s : sc->sources)
                        if (s.name == fu) {
                            x.lugar = {s.rect.x, s.rect.y, s.rect.w, s.rect.h};
                            // La cámara de un remoto (no su pantalla ni su contenido).
                            const auto [lugar, canal] = inputs::lugarDeInvitado(s);
                            if (lugar > 0 && (canal == "guest" || canal == "remoto"))
                                x.remota = canal + ":" + std::to_string(lugar);
                        }
                x.efectos = eng.sourceEfectos(n, fu).aJson();
                e.fuentes.push_back(std::move(x));
            }
            f.escenas.push_back(std::move(e));
        }
        f.fuentes.clear();
        for (const std::string& n : eng.sourceNames()) f.fuentes.emplace_back(n, eng.sourceTypeId(n));
        f.tipos.clear();
        for (const SourceType& t : eng.inputTypes()) f.tipos.push_back({t.id, t.displayName, t.video, t.audio});
    }

    // --- Lo que sale de la por omisión, y de cada salida ---------------------------------
    const bool salidasYa = (p & (FotoMotor::Salidas | FotoMotor::Estado | FotoMotor::Emision)) != 0;
    EngineStatus st;
    nlohmann::json destinos = nlohmann::json::array();
    if (salidasYa) {
        st = eng.status();
        // Los archivos (la lista de salidas, los destinos guardados y si tienen clave) solo
        // cuando cambiaron; lo que dice el motor de cada destino, siempre.
        if ((p & kFotoRelerArchivos) != 0 || fotoListaSalidas_.is_null()) {
            fotoListaSalidas_ = rt_.salidas();
            fotoDestinos_ = rt_.emision_->destinosGuardados();
        }
        destinos = rt_.emision_->destinosConEstado(fotoDestinos_);
    }
    if ((p & FotoMotor::Salidas) != 0) {
        // La armada, el AFV y la T-bar de cada una las suman los conmutadores (aportes_).
        std::vector<FotoMotor::Salida> antes = std::move(f.salidas);
        f.salidas.clear();
        f.tope = fotoListaSalidas_.value("tope", 1);
        for (const auto& s : fotoListaSalidas_.value("salidas", nlohmann::json::array())) {
            const std::string id = s.value("id", std::string());
            if (!eng.existeSalida(id)) continue;
            FotoMotor::Salida x;
            x.id = id;
            x.nombre = s.value("nombre", id);
            x.color = s.value("color", std::string());
            x.codificador = s.value("codificador", std::string());
            x.porOmision = s.value("por_omision", false);
            x.alAire = eng.currentScene(id);
            x.archivo = eng.archivoGrabacion(id);
            bool caida = false;
            for (const auto& e : st.salidas)
                if (e.id == id) {
                    x.transmitiendo = e.streaming || e.destinos > 0;
                    x.grabando = e.recording;
                    x.reconectando = e.reconnecting;
                    caida = e.recordingDown;
                }
            x.sale = eng.haySalidaEn(id);
            const salidas::Pedidos::DeSalida pedido = rt_.emision_->pedidos().de(id);
            x.grabacionPendiente = pedido.graba == salidas::Pedidos::Estado::Pendiente;
            x.transmisionPendiente = pedido.transmite == salidas::Pedidos::Estado::Pendiente;
            for (const auto& d : destinos)
                if (salidas::salidaDeDestino(d) == id)
                    x.destinos.push_back({{"id", d.value("id", std::string())},
                                          {"nombre", d.value("nombre", std::string())},
                                          {"activo", d.value("activo", false)},
                                          {"reconectando", d.value("reconectando", false)},
                                          {"respaldo", d.value("respaldo", false)}});
            x.placa = eng.estadoPlaca(id);
            x.dsk = eng.fuentesDsk(id);
            x.manualHacia = eng.manualHacia(id);
            // Lo de los conmutadores, como estaba (si esta vuelta no lo rearman, no se pierde),
            // y desde cuándo sale.
            const FotoMotor::Salida* previa = nullptr;
            for (const auto& a : antes)
                if (a.id == id) {
                    x.armada = a.armada;
                    x.afv = a.afv;
                    x.tbar = a.tbar;
                    previa = &a;
                }
            x.grabaDesdeMs = desdeMs(x.grabando, caida, previa != nullptr ? previa->grabaDesdeMs : 0);
            x.transmiteDesdeMs = desdeMs(x.transmitiendo, x.reconectando, previa != nullptr ? previa->transmiteDesdeMs : 0);
            f.salidas.push_back(std::move(x));
        }
    }
    if ((p & FotoMotor::Estado) != 0) {
        FotoMotor::EstadoAire& e = f.estado;
        e.transmite = st.streaming;
        e.graba = st.recording;
        e.grabacionCaida = st.recordingDown;
        e.reconectando = st.reconnecting;
        e.problema = st.outputProblem;
        e.codificador = st.videoEncoder;
        e.fps = st.fps;
        e.descartados = st.droppedFrames;
        e.cuadros = st.totalFrames;
        e.bytes = st.bytesSent;
        e.haySalida = eng.haySalida();
        e.hayTransmision = eng.haySalida(false);
        e.corteGrabacionPedido = rt_.recordStopWanted();
        e.corteTransmisionPedido = rt_.streamStopWanted();
        e.grabaDesdeMs = desdeMs(e.graba, e.grabacionCaida, e.grabaDesdeMs);
        e.transmiteDesdeMs = desdeMs(e.transmite, e.reconectando, e.transmiteDesdeMs);
    }

    // --- La emisión ----------------------------------------------------------------------
    if ((p & FotoMotor::Emision) != 0) {
        f.destinos = destinos;
        f.grabacion = eng.grabacionEstado();
        f.grabacion["replay"] = eng.replayEstado();
        f.limpia = eng.cleanFeedEstado();
        f.isos = nlohmann::json::array();
        // El tamaño, de lo que contó el motor: preguntarle al disco trababa con el disco trabado.
        for (const auto& g : eng.sourceRecordings())
            f.isos.push_back({{"fuente", g.source},
                              {"archivo", g.path},
                              {"mb", static_cast<double>(g.bytes) / 1e6},
                              {"audioPropio", g.audioPropio},
                              {"activa", g.activa},
                              {"pausada", g.pausada}});
        f.apurosDisco = eng.discoEstado();
        f.remux = eng.remuxEstado();
        f.retornos = nlohmann::json::object();
        for (int s = 1; s <= cfg.guests; ++s) f.retornos[std::to_string(s)] = eng.retornoInvitado(s);
    }

    // --- Los ajustes que muestra la interfaz ----------------------------------------------
    if ((p & FotoMotor::Ajustes) != 0) {
        FotoMotor::AjustesVideo& a = f.ajustes;
        a.baseAncho = cfg.video.baseWidth;
        a.baseAlto = cfg.video.baseHeight;
        a.salidaAncho = cfg.video.outputWidth;
        a.salidaAlto = cfg.video.outputHeight;
        a.fpsNum = cfg.video.fpsNum;
        a.fpsDen = cfg.video.fpsDen;
        a.transicion = cfg.transition.type;
        a.transicionMs = cfg.transition.durationMs;
        a.tema = cfg.theme;
        a.invitados = cfg.guests;
        a.servidor = cfg.streamCfg.server;
        const char* k = std::getenv(cfg.streamKeyEnv.c_str());
        a.claveEnEntorno = k != nullptr && *k != '\0';
        a.claveGuardada = rt_.streamKeySaved();
        a.sala = rt_.gateway_.room;
    }

    // --- Las superficies: el tema web (las capas DSK las suma CapasDsk, un aporte) ---------
    if (FotoMotor::rearmar(FotoMotor::kTemaWeb, p)) f.temaWeb = cfg.temaWeb;

    // --- El audio de cada fuente (FotoMezcla.cpp) -----------------------------------------
    if ((p & FotoMotor::Mezcla) != 0) armarMezcla(f.mezcla);

    // --- Lo que se mueve siempre ---------------------------------------------------------
    if ((p & FotoMotor::Medidas) != 0) {
        f.medios = nlohmann::json::array();
        for (const auto& m : eng.mediaSources())
            f.medios.push_back({{"fuente", m.name}, {"estado", m.state}, {"duracion_ms", m.durationMs}, {"tiempo_ms", m.timeMs}});
        f.salud = eng.saludSalidas();
        f.loudness = nlohmann::json::object();
        for (const std::string& id : eng.salidaIds())
            if (const audio::LoudnessPrograma* l = eng.loudnessDe(id)) f.loudness[id] = l->json();
    }

    // --- La configuración guardada: copias nuevas (las viejas siguen en las fotos que se leen)
    if ((p & FotoMotor::Configuracion) != 0) {
        f.configuracion.app = std::make_shared<const AppConfig>(cfg);
        f.configuracion.gateway = std::make_shared<const GatewaySettings>(rt_.gateway_);
        f.configuracion.launcher = std::make_shared<const LauncherSettings>(rt_.launcher_);
    }

    // --- Lo del equipo (los chequeos de la preparación) ------------------------------------
    if ((p & FotoMotor::Equipo) != 0) {
        std::tie(f.equipo.relojTipo, f.equipo.relojError) = eng.relojEstado();
        f.equipo.salidas = eng.fotoSalidas();
        const EngineStatus est = salidasYa ? st : eng.status();
        nlohmann::json porSalida = nlohmann::json::array();
        for (const auto& s : est.salidas)
            porSalida.push_back({{"id", s.id},
                                 {"nombre", s.nombre},
                                 {"por_omision", s.porOmision},
                                 {"escena", s.escena},
                                 {"streaming", s.streaming},
                                 {"reconectando", s.reconnecting},
                                 {"recording", s.recording},
                                 {"destinos", s.destinos},
                                 {"limpia", s.limpia},
                                 {"repeticion", s.repeticion}});
        f.equipo.motor = {{"ultimo_error", est.lastError},
                          {"respaldo_codificador", eng.encoderRespaldoActivo()},
                          {"salidas", std::move(porSalida)}};
        f.equipo.audio = nlohmann::json::array();
        for (const std::string& n : eng.audioSources())
            f.equipo.audio.push_back({{"fuente", n}, {"pico_db", eng.sourcePeakDb(n)}, {"mudo", eng.sourceMuted(n)}});
    }

    // --- Lo que leen la asamblea, la sala y la línea de tiempo ------------------------------
    if ((p & FotoMotor::Sala) != 0) {
        FotoMotor::SalaYEvento& s = f.sala;
        const GatewaySettings& g = rt_.gateway_;
        s.sala = g.room;
        s.puerto = g.port;
        s.atencionAutomatica = g.autoIntake;
        s.proxy = g.proxy;
        s.tunel = g.tunnel;
        s.tunelHost = g.tunnelHostname;
        s.pinggyActivo = g.pinggyActivo;
        s.pinggyTurn = g.pinggyTurn;
        s.pinggyTurnUdp = g.pinggyTurnUdp;
        s.pinggyRegion = g.pinggyRegion;
        s.pinggyDominioTipo = g.pinggyDominioTipo;
        s.pinggyHost = g.pinggyHost;
        s.pinggyCnameDestino = g.pinggyCnameDestino;
        s.pinggyListaBlanca = g.pinggyListaBlanca;
        s.pinggyPuertoTunel = g.pinggyPuertoTunel;
        s.operadorVideo = cfg.operatorVideo;
        s.operadorAudio = cfg.operatorAudio;
        s.carpetaGrabacion = rt_.carpetaGrabacion();
        s.audio = eng.audioSources();
        s.hayFuenteBanca = eng.hayFuenteBanca();
        s.escenaDeRanura.clear();
        for (int r = 1; r <= BroadcastEngine::kRanurasBanca; ++r) s.escenaDeRanura.push_back(eng.escenaDeRanura(r));
    }
}

// --- Los avisos -------------------------------------------------------------------------

int PublicadorFoto::observar(std::function<void(AppRuntime::Cambio)> fn) {
    const std::lock_guard<std::mutex> l(observadoresMu_);
    observadores_.emplace_back(++proximoObservador_, std::move(fn));
    return proximoObservador_;
}

void PublicadorFoto::dejarDeObservar(int id) {
    const std::lock_guard<std::mutex> l(observadoresMu_);
    observadores_.erase(std::remove_if(observadores_.begin(), observadores_.end(),
                                       [id](const auto& o) { return o.first == id; }),
                        observadores_.end());
}

unsigned PublicadorFoto::partesDe(AppRuntime::Cambio c) {
    using Cambio = AppRuntime::Cambio;
    // Qué partes de la foto toca cada aviso (FotoMotor::Parte); las salidas y los ajustes,
    // además, lo que la foto lee de archivos (salidas.json, destinos.json).
    switch (c) {
        case Cambio::Salidas:
            return FotoMotor::Salidas | FotoMotor::Estado | FotoMotor::Emision | FotoMotor::Mezcla | kFotoRelerArchivos;
        // (La sala: las fuentes con audio y las de la banca siguen a las escenas y sus fuentes.)
        case Cambio::Escenas: return FotoMotor::Escenas | FotoMotor::Salidas | FotoMotor::Sala | FotoMotor::Mezcla;
        case Cambio::Aire: return FotoMotor::Salidas | FotoMotor::Estado;
        case Cambio::Fuentes: return FotoMotor::Escenas | FotoMotor::Sala | FotoMotor::Mezcla;
        case Cambio::Capa: return FotoMotor::Escenas | FotoMotor::Sala;
        case Cambio::Audio: return FotoMotor::Medidas | FotoMotor::Mezcla;
        case Cambio::Ajustes:
            return FotoMotor::Ajustes | FotoMotor::Escenas | FotoMotor::Emision | FotoMotor::Salidas | FotoMotor::Sala | FotoMotor::Mezcla |
                   FotoMotor::Configuracion | kFotoRelerArchivos;
    }
    return FotoMotor::Todas | kFotoRelerArchivos;
}

void PublicadorFoto::avisar(AppRuntime::Cambio c) const {
    // La foto que lee la interfaz se rearma con esto (la publica el dueño enseguida).
    fotoSucia(partesDe(c));
    // Los que escuchan, en el dueño (los conmutadores son suyos; los view models se lo
    // encargan a su bucle): lo que cambió otro hilo les llega encolado (F3-03).
    if (!rt_.esDueno() && rt_.encolarEnDueno_) {
        rt_.enDueno([this, c]() { avisarObservadores(c); });
        return;
    }
    avisarObservadores(c);
}

void PublicadorFoto::avisarObservadores(AppRuntime::Cambio c) const {
    // Sobre una copia: quien escucha puede darse de baja mientras se le avisa.
    std::vector<std::pair<int, std::function<void(AppRuntime::Cambio)>>> lista;
    {
        const std::lock_guard<std::mutex> l(observadoresMu_);
        lista = observadores_;
    }
    for (const auto& [id, fn] : lista)
        if (fn) fn(c);
}

} // namespace ba::app
