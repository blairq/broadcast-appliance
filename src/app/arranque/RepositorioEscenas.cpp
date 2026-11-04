// ============================================================================
// src/app/arranque/RepositorioEscenas.cpp — ver RepositorioEscenas.hpp
// ============================================================================

#include "arranque/RepositorioEscenas.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "arranque/AireDurable.hpp"
#include "arranque/RuntimeMotor.hpp"
#include "arranque/ArranqueAviso.hpp"
#include "arranque/RutasPortables.hpp"
#include "arranque/ServicioSalidas.hpp"
#include "diag/Registro.hpp"
#include "emision/InputSpecs.hpp"
#include "fault/ModoSeguro.hpp"
#include "i18n/I18nTexto.hpp"
#include "video/FuentesCaptura.hpp"

namespace ba::app {

using Cambio = AppRuntime::Cambio;

namespace {

/// Las fuentes que apaga el modo seguro (F1-10): las entradas NDI y las capturas de ventana.
bool apagadaPorModoSeguro(const SourceSpec& src) {
    const ba::fault::ModoSeguro& m = ba::fault::delProceso();
    if (!m.activo()) return false;
    if (m.apagada(ba::fault::Pieza::CapturaVentana) && src.id == video::kTipoVentana) return true;
    if (m.apagada(ba::fault::Pieza::Ndi)) {
        const nlohmann::json s = nlohmann::json::parse(src.settingsJson.empty() ? "{}" : src.settingsJson, nullptr, false);
        if (inputs::tipoLogico(src.id, s.is_object() ? s : nlohmann::json::object()) == "ba_ndi_input" || src.id == "ba_ndi_input")
            return true;
    }
    return false;
}

/// Los ajustes de una fuente con las rutas relativas a los datos resueltas (RutasPortables).
std::string conRutasDeAhora(const std::string& ajustes) {
    const nlohmann::json j = nlohmann::json::parse(ajustes.empty() ? "{}" : ajustes, nullptr, false);
    return j.is_object() ? rutasAbsolutas(j).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) : ajustes;
}

} // namespace

void RepositorioEscenas::armarEnElMotor() {
    AppConfig& cfg = rt_.cfg_;
    BroadcastEngine& engine = *rt_.engine_;
    // Los volúmenes generales de los grupos, antes que las fuentes: cada una ya entra con el suyo.
    for (const auto& [grupo, g] : cfg.gruposAudio) engine.setGananciaGrupo(grupo, g);
    engine.setTransition(cfg.transition.type, cfg.transition.durationMs);
    for (const auto& sc : cfg.scenes) {
        arranque::avisar(ba::i18n::llenar(ba::i18n::traducir("arranque", "Armando la escena «%1»…"), {sc.name}));
        engine.createScene(sc.name, sc.id);
        // En modo seguro las escenas se crean VACIAS: si una fuente es la que
        // venia tumbando el arranque, asi no se la toca. La configuracion no
        // se modifica, o sea que al arrancar normal vuelven todas solas.
        if (cfg.safeMode) continue;
        for (const auto& src : sc.sources) {
            // Modo seguro nivel 2 (o la caída atribuida): NDI y capturas de ventana
            // no se crean. Quedan en la configuración: vuelven con el arranque normal.
            if (apagadaPorModoSeguro(src)) {
                ba::diag::registro().linea(ba::diag::Nivel::Aviso, "arranque", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("arranque", "modo seguro: «%1» (%2) no se crea"),
                                                            {src.name, sc.name}));
                continue;
            }
            // Una fuente rota (camara desenchufada, id mal escrito) no tumba
            // el proceso: se avisa y la emision sigue con el resto.
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = engine.addSource(sc.id, src);
            const long long ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 50)
                ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: «%1» (%2, %3) tardó %4 ms"),
                                                           {src.name, sc.name, src.id, std::to_string(ms)}));
            if (!ok) {
                std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "fuente '%1' omitida: %2"),
                                                             {src.name, engine.lastError()})
                                                .c_str());
            }
        }
    }
}

void RepositorioEscenas::agregarEntradasDeAudio() {
    // Microfonos y lineas del equipo: canal global, como el audio de los
    // invitados (suenan en todas las escenas, no son items de escena).
    for (const auto& spec : rt_.cfg_.audioInputs) {
        if (!rt_.engine_->addGlobalAudio(spec))
            std::printf("ba-app: %s\n", ba::i18n::llenar(ba::i18n::traducir("arranque", "entrada de audio '%1' omitida: %2"),
                                                         {spec.name, rt_.engine_->lastError()})
                                            .c_str());
    }
}

bool RepositorioEscenas::addAudioInput(const SourceSpec& pedida) {
    SourceSpec spec = pedida;
    inputs::alMotor(spec); // se guarda con el tipo del motor (esquema 2 de app.json)
    if (!rt_.engine_->addGlobalAudio(spec)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    const auto it = std::find_if(rt_.cfg_.audioInputs.begin(), rt_.cfg_.audioInputs.end(),
                                 [&](const SourceSpec& s) { return s.name == spec.name; });
    if (it == rt_.cfg_.audioInputs.end())
        rt_.cfg_.audioInputs.push_back(spec);
    else
        *it = spec;
    rt_.avisar(Cambio::Audio);
    return true;
}

bool RepositorioEscenas::removeAudioInput(const std::string& name) {
    const bool ok = rt_.engine_->removeGlobalAudio(name);
    rt_.cfg_.audioInputs.erase(std::remove_if(rt_.cfg_.audioInputs.begin(), rt_.cfg_.audioInputs.end(),
                                          [&](const SourceSpec& s) { return s.name == name; }),
                           rt_.cfg_.audioInputs.end());
    rt_.avisar(Cambio::Audio);
    return ok;
}

std::optional<SourceSpec> RepositorioEscenas::specDe(const std::string& scene, const std::string& name) const {
    if (const SourceSpec* s = const_cast<RepositorioEscenas*>(this)->findSpec(scene, name)) return *s;
    return std::nullopt;
}

SourceSpec* RepositorioEscenas::findSpec(const std::string& scene, const std::string& name) {
    SceneConfig* sc = findScene(rt_.cfg_, scene);
    if (sc == nullptr) {
        return nullptr;
    }
    for (auto& s : sc->sources) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

bool RepositorioEscenas::addScene(const std::string& name) {
    if (!rt_.engine_->createScene(name)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    SceneConfig nueva;
    nueva.name = name;
    nueva.id = rt_.engine_->sceneId(name); // el que le dio el motor
    rt_.cfg_.scenes.push_back(nueva);
    rt_.avisar(Cambio::Escenas);
    return true;
}

bool RepositorioEscenas::copyScene(const std::string& from, const std::string& to) {
    // `errorDelHilo()` es pegajoso: guarda lo ultimo que fallo, de cuando sea. Como
    // aca se lo usa DESPUES de haber salido bien —para contar que una fuente no
    // se pudo recrear— hay que limpiarlo al entrar, o la copia informa como
    // propio un error de otra operacion anterior.
    AppRuntime::errorDelHilo().clear();
    const SceneConfig* origen = findScene(rt_.cfg_, from);
    if (origen == nullptr) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena \"%1\""), {from});
        return false;
    }
    for (const SceneConfig& s : rt_.cfg_.scenes)
        if (s.name == to) {
            AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "ya hay una escena llamada \"%1\""), {to});
            return false;
        }
    if (!rt_.engine_->createScene(to)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    // Se copia la CONFIGURACION, no las fuentes del motor: compartirlas haria que
    // mover algo en la copia moviera el original. Recrearlas desde la
    // especificacion da dos escenas de verdad independientes, que es lo que uno
    // espera al copiar.
    SceneConfig copia = *origen;
    copia.name = to;
    copia.id = rt_.engine_->sceneId(to); // otra escena: otro id
    copia.protegida = false; // la copia se puede tocar: para eso se copio
    rt_.cfg_.scenes.push_back(copia);
    for (const SourceSpec& spec : copia.sources) {
        SourceSpec s = spec;
        if (!addSource(to, s)) {
            // Lo que no se pudo recrear se dice, pero no se deshace todo: una
            // escena copiada a medias se arregla a mano; una que desaparecio
            // sin explicacion, no.
            AppRuntime::errorDelHilo() = ba::i18n::llenar(
                ba::i18n::traducir("escenas", "la escena se copio, pero la fuente \"%1\" no se pudo recrear: %2"),
                {spec.name, AppRuntime::errorDelHilo()});
        }
    }
    rt_.avisar(Cambio::Escenas);
    return true;
}

bool RepositorioEscenas::setSceneProtected(const std::string& name, bool protegida) {
    if (SceneConfig* s = findScene(rt_.cfg_, name)) {
        s->protegida = protegida;
        rt_.avisar(Cambio::Escenas);
        return true;
    }
    AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena \"%1\""), {name});
    return false;
}

bool RepositorioEscenas::sceneProtected(const std::string& name) const {
    const SceneConfig* s = findScene(rt_.cfg_, name);
    return s != nullptr && s->protegida;
}

bool RepositorioEscenas::removeScene(const std::string& name) {
    if (sceneProtected(name)) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "la escena \"%1\" esta protegida: sacale la proteccion primero"), {name});
        return false;
    }
    const std::string id = rt_.engine_->sceneId(name);
    if (!rt_.engine_->removeScene(name)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    rt_.cfg_.scenes.erase(std::remove_if(rt_.cfg_.scenes.begin(), rt_.cfg_.scenes.end(),
                                     [&](const SceneConfig& s) { return s.id == id; }),
                      rt_.cfg_.scenes.end());
    rt_.cfg_.currentScene = rt_.engine_->currentScene();
    rt_.avisar(Cambio::Escenas);
    return true;
}

bool RepositorioEscenas::renameScene(const std::string& from, const std::string& to) {
    if (sceneProtected(from)) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "la escena \"%1\" esta protegida: sacale la proteccion primero"), {from});
        return false;
    }
    const std::string id = rt_.engine_->sceneId(from);
    // Solo el nombre: lo que apunta a la escena guarda su id y el motor la conoce por él
    // (no hay corte aunque esté al aire, y nadie tiene que seguir el renombre).
    if (!rt_.engine_->renameScene(from, to)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    if (SceneConfig* sc = findScene(rt_.cfg_, id)) {
        sc->name = to;
    }
    rt_.cfg_.currentScene = rt_.engine_->currentScene();
    rt_.avisar(Cambio::Escenas);
    return true;
}

std::string RepositorioEscenas::sceneFolder(const std::string& name) const {
    const SceneConfig* sc = findScene(rt_.cfg_, name);
    return sc != nullptr ? sc->folder : std::string();
}

bool RepositorioEscenas::setSceneFolder(const std::string& name, const std::string& folder) {
    SceneConfig* sc = findScene(rt_.cfg_, name);
    if (sc == nullptr) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena '%1'"), {name});
        return false;
    }
    sc->folder = folder;
    rt_.avisar(Cambio::Escenas);
    return true;
}

std::vector<std::string> RepositorioEscenas::sceneFolders() const {
    std::vector<std::string> out;
    for (const auto& sc : rt_.cfg_.scenes)
        if (!sc.folder.empty() && std::find(out.begin(), out.end(), sc.folder) == out.end())
            out.push_back(sc.folder);
    return out;
}

std::vector<std::string> RepositorioEscenas::orderedSceneNames() const {
    const std::vector<std::string> live = rt_.engine_->sceneNames();
    std::vector<std::string> out;
    for (const auto& sc : rt_.cfg_.scenes)
        if (std::find(live.begin(), live.end(), sc.name) != live.end()) out.push_back(sc.name);
    for (const auto& n : live)
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    return out;
}

bool RepositorioEscenas::moveScene(const std::string& name, const std::string& before) {
    if (name == before) return true;
    const SceneConfig* pedida = findScene(rt_.cfg_, name);
    const auto it = std::find_if(rt_.cfg_.scenes.begin(), rt_.cfg_.scenes.end(),
                                 [&](const SceneConfig& s) { return &s == pedida; });
    if (it == rt_.cfg_.scenes.end()) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la escena '%1'"), {name});
        return false;
    }
    SceneConfig moved = *it;
    rt_.cfg_.scenes.erase(it);
    auto dst = rt_.cfg_.scenes.end();
    if (!before.empty()) {
        const SceneConfig* antes = findScene(rt_.cfg_, before);
        dst = std::find_if(rt_.cfg_.scenes.begin(), rt_.cfg_.scenes.end(),
                           [&](const SceneConfig& s) { return &s == antes; });
        if (dst != rt_.cfg_.scenes.end()) moved.folder = dst->folder; // cae en su misma carpeta
    }
    rt_.cfg_.scenes.insert(dst, std::move(moved));
    rt_.avisar(Cambio::Escenas);
    return true;
}

bool RepositorioEscenas::setScene(const std::string& name, const std::string& salida) {
    return anotarAire(rt_.engine_->setCurrentScene(name, salida), name, salida);
}

bool RepositorioEscenas::setSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    return anotarAire(rt_.engine_->setCurrentSceneCon(name, tipo, ms, salida), name, salida);
}

bool RepositorioEscenas::anotarAire(bool ok, const std::string& name, const std::string& salida) {
    if (!ok) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    anotarEscenaAlAire(name, salida);
    return true;
}

void RepositorioEscenas::anotarEscenaAlAire(const std::string& name, const std::string& salida) {
    // La de la por omisión va también en app.json (la de las demás, solo en al-aire.json);
    // la configuración es del dueño: se la anota él.
    // (Por su nombre: puede llegar el id.)
    if (rt_.engine_->idDeSalida(salida) == salidas::kPorOmision)
        rt_.enDueno([this, nombre = rt_.engine_->sceneName(name)]() { rt_.cfg_.currentScene = nombre; });
    // El documento de aire, ya: con la interfaz trabada, la vuelta del motor (que lo
    // pone en cada una) no llega, y lo que sale tiene que volver igual tras un corte.
    rt_.aire_->writeOnAirState();
    rt_.avisar(Cambio::Aire);
}

bool RepositorioEscenas::setTransition(const std::string& type, int durationMs) {
    if (!rt_.engine_->setTransition(type, durationMs)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    rt_.cfg_.transition = TransitionConfig{type, durationMs};
    rt_.avisar(Cambio::Aire);
    return true;
}

bool RepositorioEscenas::addSource(const std::string& scene, const SourceSpec& pedida) {
    SourceSpec spec = pedida; // se guarda como la creó el motor (un archivo: ba_gst_medio)
    // Una ruta relativa a los datos («Media/x.png», también en el location= de un
    // pipeline) es de la configuración en uso: el motor la necesita entera.
    spec.settingsJson = conRutasDeAhora(spec.settingsJson);
    inputs::alMotor(spec); // se guarda con el tipo del motor (esquema 2 de app.json)
    if (!rt_.engine_->addSource(scene, spec)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    if (SceneConfig* sc = findScene(rt_.cfg_, scene)) {
        SourceSpec stored = spec;
        stored.settingsJson = rt_.engine_->sourceSettingsJson(spec.name);
        sc->sources.push_back(stored);
    }
    rt_.avisar(Cambio::Fuentes);
    return true;
}

bool RepositorioEscenas::removeSource(const std::string& scene, const std::string& name) {
    if (!rt_.engine_->removeSource(scene, name)) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la fuente '%1' en '%2'"), {name, scene});
        return false;
    }
    if (SceneConfig* sc = findScene(rt_.cfg_, scene)) {
        sc->sources.erase(std::remove_if(sc->sources.begin(), sc->sources.end(),
                                         [&](const SourceSpec& s) { return s.name == name; }),
                          sc->sources.end());
    }
    rt_.avisar(Cambio::Fuentes);
    return true;
}

bool RepositorioEscenas::setSourceCrop(const std::string& scene, const std::string& name, float izquierda, float arriba,
                                       float derecha, float abajo) {
    if (!rt_.engine_->setSourceCrop(scene, name, izquierda, arriba, derecha, abajo)) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(
            ba::i18n::traducir("escenas", "no se pudo recortar '%1' en '%2' (¿recorte más grande que la imagen?)"), {name, scene});
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->recorte[0] = izquierda;
        s->recorte[1] = arriba;
        s->recorte[2] = derecha;
        s->recorte[3] = abajo;
    }
    rt_.avisar(Cambio::Capa);
    return true;
}

bool RepositorioEscenas::setSourceEfectos(const std::string& scene, const std::string& name, const EfectosCapa& efectos) {
    if (!rt_.engine_->setSourceEfectos(scene, name, efectos)) {
        AppRuntime::errorDelHilo() = ba::i18n::llenar(ba::i18n::traducir("escenas", "no existe la fuente '%1' en '%2'"), {name, scene});
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) s->efectos = efectos;
    rt_.avisar(Cambio::Capa);
    return true;
}

bool RepositorioEscenas::moveSource(const std::string& scene, const std::string& name, int delta) {
    if (!rt_.engine_->moveSource(scene, name, delta)) {
        return false;
    }
    if (SceneConfig* sc = findScene(rt_.cfg_, scene)) {
        auto& v = sc->sources;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i].name == name) {
                const std::size_t j = delta > 0 ? i + 1 : (i == 0 ? 0 : i - 1);
                if (j < v.size() && j != i) {
                    std::swap(v[i], v[j]);
                }
                break;
            }
        }
    }
    rt_.avisar(Cambio::Fuentes);
    return true;
}

bool RepositorioEscenas::renameSource(const std::string& from, const std::string& to) {
    if (!rt_.engine_->renameSource(from, to)) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    // La misma fuente puede estar en varias escenas, y entre las entradas de audio.
    for (auto& sc : rt_.cfg_.scenes)
        for (auto& s : sc.sources)
            if (s.name == from) s.name = to;
    for (auto& s : rt_.cfg_.audioInputs)
        if (s.name == from) s.name = to;
    rt_.avisar(Cambio::Fuentes);
    return true;
}

bool RepositorioEscenas::setSourceVisible(const std::string& scene, const std::string& name, bool visible) {
    if (!rt_.engine_->setSourceVisible(scene, name, visible)) {
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->visible = visible;
    }
    rt_.avisar(Cambio::Capa);
    return true;
}

bool RepositorioEscenas::setSourceRect(const std::string& scene, const std::string& name, const CanvasRect& rect) {
    if (!rt_.engine_->setSourceRect(scene, name, rect)) {
        return false;
    }
    if (SourceSpec* s = findSpec(scene, name)) {
        s->rect = rect;
    }
    rt_.avisar(Cambio::Capa);
    return true;
}

bool RepositorioEscenas::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    if (!rt_.engine_->updateSourceSettings(name, conRutasDeAhora(settingsJson))) {
        AppRuntime::errorDelHilo() = rt_.engine_->lastError();
        return false;
    }
    // La misma fuente puede estar en varias escenas: se sincronizan todas.
    const std::string current = rt_.engine_->sourceSettingsJson(name);
    for (auto& sc : rt_.cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.settingsJson = current;
            }
        }
    }
    // Y las entradas globales (micrófonos: p. ej. sus filtros de audio).
    for (auto& s : rt_.cfg_.audioInputs)
        if (s.name == name) s.settingsJson = current;
    rt_.avisar(Cambio::Capa);
    rt_.fotoSucia(FotoMotor::Mezcla); // los ajustes de una entrada de audio también están ahí
    return true;
}

bool RepositorioEscenas::setVolume(const std::string& name, float mul, const std::string& salida) {
    if (!rt_.engine_->setSourceVolume(name, mul, salida)) {
        return false;
    }
    if (const std::string id = rt_.engine_->idDeSalida(salida); id != BroadcastEngine::kSalidaPorOmision) {
        rt_.servicioSalidas_->guardarMando(id, name); // la mezcla de otra salida: es de su salida, no de la escena
        rt_.avisar(Cambio::Audio);
        return true;
    }
    for (auto& sc : rt_.cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.volume = mul;
            }
        }
    }
    for (auto& s : rt_.cfg_.audioInputs)
        if (s.name == name) s.volume = mul;
    rt_.avisar(Cambio::Audio);
    return true;
}

bool RepositorioEscenas::setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida) {
    if (grupo.empty()) return false;
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty()) return false;
    const float g = std::max(0.0f, mul);
    rt_.engine_->setGananciaGrupo(grupo, g, id);
    if (id != BroadcastEngine::kSalidaPorOmision) {
        rt_.servicioSalidas_->guardarGrupo(id, grupo, g);
    } else if (std::abs(g - 1.0f) < 1e-6f) {
        rt_.cfg_.gruposAudio.erase(grupo);
    } else {
        rt_.cfg_.gruposAudio[grupo] = g;
    }
    rt_.avisar(Cambio::Audio);
    return true;
}

bool RepositorioEscenas::setMuted(const std::string& name, bool muted, const std::string& salida) {
    if (!rt_.engine_->setSourceMuted(name, muted, salida)) {
        return false;
    }
    if (const std::string id = rt_.engine_->idDeSalida(salida); id != BroadcastEngine::kSalidaPorOmision) {
        rt_.servicioSalidas_->guardarMando(id, name);
        rt_.avisar(Cambio::Audio);
        return true;
    }
    for (auto& sc : rt_.cfg_.scenes) {
        for (auto& s : sc.sources) {
            if (s.name == name) {
                s.muted = muted;
            }
        }
    }
    for (auto& s : rt_.cfg_.audioInputs)
        if (s.name == name) s.muted = muted;
    rt_.avisar(Cambio::Audio);
    return true;
}

void RepositorioEscenas::reubicarFuentes() {
    // Las ubicaciones son fracciones del lienzo: se recalculan en pixeles.
    for (const auto& sc : rt_.cfg_.scenes) {
        for (const auto& s : sc.sources) {
            rt_.engine_->setSourceRect(sc.name, s.name, s.rect);
        }
    }
}

} // namespace ba::app
