// src/app/arranque/FotoDelRuntime.cpp — la foto que lee la interfaz (AppRuntime, F3-03)
//
// Ver AppRuntime.hpp («La foto que lee la interfaz») y emision/FotoMotor.hpp. Todo acá corre
// en el dueño (el plano de control) con el turno del motor: lee cfg_ y el motor sin pisarse
// con nadie y deja un valor que la interfaz lee sin candados.

#include "arranque/AppRuntime.hpp"

#include "emision/SalidasConfig.hpp"

#include <cstdlib>

namespace ba::app {

void AppRuntime::setFoto(std::function<void(std::shared_ptr<const FotoMotor>, unsigned)> publicar) {
    publicarFoto_ = std::move(publicar);
    foto_.reset();
    fotoSucia_ = FotoMotor::Todas | kFotoRelerArchivos;
}

int AppRuntime::aportarALaFoto(std::function<void(FotoMotor&, unsigned)> aporte) {
    int id = 0;
    {
        const std::lock_guard<std::mutex> l(aportesMu_);
        id = ++proximoAporte_;
        aportes_.emplace_back(id, std::move(aporte));
    }
    fotoSucia(FotoMotor::Todas);
    return id;
}

void AppRuntime::dejarDeAportar(int id) {
    const std::lock_guard<std::mutex> l(aportesMu_);
    aportes_.erase(std::remove_if(aportes_.begin(), aportes_.end(), [id](const auto& a) { return a.first == id; }),
                   aportes_.end());
}

void AppRuntime::fotoSucia(unsigned partes) const {
    fotoSucia_.fetch_or(partes);
    // Una sola publicación pedida a la vez: las que se juntan mientras tanto salen juntas.
    if (!publicarFoto_ || fotoPedida_.exchange(true)) return;
    despuesEnDueno([this]() { const_cast<AppRuntime*>(this)->publicarFoto(); });
}

void AppRuntime::publicarFoto() {
    fotoPedida_ = false;
    if (!publicarFoto_ || !started_) return;
    const unsigned sucias = fotoSucia_.exchange(0);
    if ((sucias & FotoMotor::Todas) == 0) return;
    const TurnoMotor::Guarda turno(engine_.turno());
    auto f = std::make_shared<FotoMotor>(foto_ ? *foto_ : FotoMotor{});
    armarFoto(*f, sucias);
    {
        const std::lock_guard<std::mutex> l(aportesMu_);
        for (const auto& [id, aporte] : aportes_)
            if (aporte) aporte(*f, sucias);
    }
    // Qué cambió de verdad: la interfaz se entera solo de eso.
    unsigned cambiaron = 0;
    if (!foto_) {
        cambiaron = FotoMotor::Todas;
    } else {
        const FotoMotor& a = *foto_;
        const FotoMotor& n = *f;
        if (n.escenas != a.escenas || n.fuentes != a.fuentes || n.tipos != a.tipos ||
            n.transicionesEntrada != a.transicionesEntrada)
            cambiaron |= FotoMotor::Escenas;
        if (n.salidas != a.salidas || n.tope != a.tope) cambiaron |= FotoMotor::Salidas;
        if (n.estado != a.estado) cambiaron |= FotoMotor::Estado;
        if (n.destinos != a.destinos || n.grabacion != a.grabacion || n.limpia != a.limpia || n.isos != a.isos ||
            n.apurosDisco != a.apurosDisco || n.remux != a.remux || n.retornos != a.retornos)
            cambiaron |= FotoMotor::Emision;
        if (n.ajustes != a.ajustes) cambiaron |= FotoMotor::Ajustes;
        if (n.medios != a.medios || n.salud != a.salud || n.loudness != a.loudness) cambiaron |= FotoMotor::Medidas;
    }
    if (cambiaron == 0) return;
    foto_ = f;
    publicarFoto_(foto_, cambiaron);
}

void AppRuntime::armarFoto(FotoMotor& f, unsigned p) {
    // --- Escenas: el orden del operador, la carpeta, si está protegida y las fuentes ------
    if ((p & FotoMotor::Escenas) != 0) {
        f.escenas.clear();
        for (const std::string& n : orderedSceneNames()) {
            FotoMotor::Escena e;
            e.nombre = n;
            const SceneConfig* sc = findScene(cfg_, n);
            if (sc != nullptr) {
                e.carpeta = sc->folder;
                e.protegida = sc->protegida;
            }
            for (const std::string& fu : engine_.sceneSources(n)) {
                FotoMotor::Fuente x;
                x.nombre = fu;
                x.tipo = engine_.sourceTypeId(fu);
                x.visible = engine_.sourceVisible(n, fu);
                // El lugar es el guardado (en fracciones del lienzo), como lo mueve el editor.
                if (sc != nullptr)
                    for (const SourceSpec& s : sc->sources)
                        if (s.name == fu) x.lugar = {s.rect.x, s.rect.y, s.rect.w, s.rect.h};
                x.efectos = engine_.sourceEfectos(n, fu).aJson();
                e.fuentes.push_back(std::move(x));
            }
            f.escenas.push_back(std::move(e));
        }
        f.fuentes.clear();
        for (const std::string& n : engine_.sourceNames()) f.fuentes.emplace_back(n, engine_.sourceTypeId(n));
        f.tipos.clear();
        for (const SourceType& t : engine_.inputTypes()) f.tipos.push_back({t.id, t.displayName, t.video, t.audio});
    }

    // --- Lo que sale de la por omisión, y de cada salida ---------------------------------
    const bool salidasYa = (p & (FotoMotor::Salidas | FotoMotor::Estado | FotoMotor::Emision)) != 0;
    EngineStatus st;
    nlohmann::json destinos = nlohmann::json::array();
    if (salidasYa) {
        st = engine_.status();
        // Los archivos (la lista de salidas, los destinos guardados y si tienen clave) solo
        // cuando cambiaron; lo que dice el motor de cada destino, siempre.
        if ((p & kFotoRelerArchivos) != 0 || fotoListaSalidas_.is_null()) {
            fotoListaSalidas_ = salidas();
            fotoDestinos_ = destinosGuardados();
        }
        destinos = destinosConEstado(fotoDestinos_);
    }
    if ((p & FotoMotor::Salidas) != 0) {
        // La armada, el AFV y la T-bar de cada una las suman los conmutadores (aportes_).
        std::vector<FotoMotor::Salida> antes = std::move(f.salidas);
        f.salidas.clear();
        f.tope = fotoListaSalidas_.value("tope", 1);
        for (const auto& s : fotoListaSalidas_.value("salidas", nlohmann::json::array())) {
            const std::string id = s.value("id", std::string());
            if (!engine_.existeSalida(id)) continue;
            FotoMotor::Salida x;
            x.id = id;
            x.nombre = s.value("nombre", id);
            x.color = s.value("color", std::string());
            x.codificador = s.value("codificador", std::string());
            x.porOmision = s.value("por_omision", false);
            x.alAire = engine_.currentScene(id);
            x.archivo = engine_.archivoGrabacion(id);
            for (const auto& e : st.salidas)
                if (e.id == id) {
                    x.transmitiendo = e.streaming || e.destinos > 0;
                    x.grabando = e.recording;
                    x.reconectando = e.reconnecting;
                }
            x.sale = engine_.haySalidaEn(id);
            for (const auto& d : destinos)
                if (salidas::salidaDeDestino(d) == id)
                    x.destinos.push_back({{"id", d.value("id", std::string())},
                                          {"nombre", d.value("nombre", std::string())},
                                          {"activo", d.value("activo", false)},
                                          {"reconectando", d.value("reconectando", false)},
                                          {"respaldo", d.value("respaldo", false)}});
            x.placa = engine_.estadoPlaca(id);
            x.dsk = engine_.fuentesDsk(id);
            x.manualHacia = engine_.manualHacia(id);
            // Lo de los conmutadores, como estaba (si esta vuelta no lo rearman, no se pierde).
            for (const auto& a : antes)
                if (a.id == id) {
                    x.armada = a.armada;
                    x.afv = a.afv;
                    x.tbar = a.tbar;
                }
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
        e.haySalida = engine_.haySalida();
        e.hayTransmision = engine_.haySalida(false);
        e.corteGrabacionPedido = recordStopWanted_;
        e.corteTransmisionPedido = streamStopWanted_;
    }

    // --- La emisión ----------------------------------------------------------------------
    if ((p & FotoMotor::Emision) != 0) {
        f.destinos = destinos;
        f.grabacion = engine_.grabacionEstado();
        f.grabacion["replay"] = engine_.replayEstado();
        f.limpia = engine_.cleanFeedEstado();
        f.isos = nlohmann::json::array();
        // El tamaño, de lo que contó el motor: preguntarle al disco trababa con el disco trabado.
        for (const auto& g : engine_.sourceRecordings())
            f.isos.push_back({{"fuente", g.source},
                              {"archivo", g.path},
                              {"mb", static_cast<double>(g.bytes) / 1e6},
                              {"audioPropio", g.audioPropio},
                              {"activa", g.activa},
                              {"pausada", g.pausada}});
        f.apurosDisco = engine_.discoEstado();
        f.remux = engine_.remuxEstado();
        f.retornos = nlohmann::json::object();
        for (int s = 1; s <= cfg_.guests; ++s) f.retornos[std::to_string(s)] = engine_.retornoInvitado(s);
    }

    // --- Los ajustes que muestra la interfaz ----------------------------------------------
    if ((p & FotoMotor::Ajustes) != 0) {
        FotoMotor::AjustesVideo& a = f.ajustes;
        a.baseAncho = cfg_.video.baseWidth;
        a.baseAlto = cfg_.video.baseHeight;
        a.salidaAncho = cfg_.video.outputWidth;
        a.salidaAlto = cfg_.video.outputHeight;
        a.fpsNum = cfg_.video.fpsNum;
        a.fpsDen = cfg_.video.fpsDen;
        a.transicion = cfg_.transition.type;
        a.transicionMs = cfg_.transition.durationMs;
        a.tema = cfg_.theme;
        a.invitados = cfg_.guests;
        a.servidor = cfg_.streamCfg.server;
        const char* k = std::getenv(cfg_.streamKeyEnv.c_str());
        a.claveEnEntorno = k != nullptr && *k != '\0';
        a.claveGuardada = streamKeySaved();
        a.sala = gateway_.room;
    }

    // --- Lo que se mueve siempre ---------------------------------------------------------
    if ((p & FotoMotor::Medidas) != 0) {
        f.medios = nlohmann::json::array();
        for (const auto& m : engine_.mediaSources())
            f.medios.push_back({{"fuente", m.name}, {"estado", m.state}, {"duracion_ms", m.durationMs}, {"tiempo_ms", m.timeMs}});
        f.salud = engine_.saludSalidas();
        f.loudness = nlohmann::json::object();
        for (const std::string& id : engine_.salidaIds())
            if (const audio::LoudnessPrograma* l = engine_.loudnessDe(id)) f.loudness[id] = l->json();
    }
}

} // namespace ba::app
