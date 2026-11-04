// ============================================================================
// src/app/emision/EscenasYFuentes.cpp — ver EscenasYFuentes.hpp
//
// El modelo (qué escenas hay, qué fuentes tienen, dónde y con qué recorte) vive acá; el
// motor recibe cada escena ya traducida a capas en píxeles del lienzo y cada fuente ya
// traducida a los ajustes de su entrada. Las capas encima del programa, en CapasPrograma.cpp.
//
// LOS TIPOS: lo que llega con un tipo de antes (de la época de OBS) pasa primero
// por inputs::alMotor (InputSpecs.hpp), una sola vez y en un solo lugar. Después
// `ajustesMotor` arma los ajustes de la entrada:
//   ba_gst_fuente / ba_gst_medio        tal cual (el motor lee los mismos ajustes)
//   capturas de pantalla                 tipo=pantalla
//   lo que pinta la app (gráficos, chat, subtítulos)   tipo=app
//   los tipos registrados con `motor` (color, texto, imagen, grilla…: FuentesApp)
// ============================================================================

#include "emision/EscenasYFuentes.hpp"

#include "emision/EmisionSalidas.hpp"
#include "emision/MezclaApp.hpp"
#include "emision/SalidaMotor.hpp"
#include "i18n/I18nTexto.hpp"
#include "platform/Paths.hpp"

#include <filesystem>
#include "emision/FuentesApp.hpp"
#include "emision/InputSpecs.hpp"
#include "video/FuentesCaptura.hpp"
#include "graficos/VideoEnGrafico.hpp"

#include "Motor.hpp"
#include "Salidas.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <thread>

namespace ba::app {

using json = nlohmann::json;

namespace {
/// La sección `k` de un objeto, SIN COPIARLA (un objeto vacío si no está). Con
/// `value("entradas", json::object())` se copiaba el estado de todas las entradas cada
/// vez que se pedía una: varias veces por segundo desde la interfaz (~3 % de un núcleo).
const json& seccion(const json& j, const char* k) {
    static const json vacio = json::object();
    if (!j.is_object()) return vacio;
    const auto it = j.find(k);
    return it != j.end() && it->is_object() ? *it : vacio;
}


bool termina(const std::string& s, const std::string& fin) {
    return s.size() >= fin.size() && s.compare(s.size() - fin.size(), fin.size(), fin) == 0;
}

/// Tipos de captura de pantalla de OBS (cualquier sistema).
bool esPantalla(const std::string& id) {
    return id == "screen_capture" || id == "display_capture" || id == "macos-capture" || id == "foto_fuente" ||
           id == "monitor_capture" || id == "xshm_input" || id == "pipewire-desktop-capture-source" ||
           id == "ba_pantalla";
}

constexpr const char* kTipoCompositor = "ba_video_compositor";
constexpr const char* kTipoSalida = "ba_video_salida";

/// La config de capas de un compositor (o de una salida: su entrada, a todo el lienzo).
json configDe(const std::string& tipo, const json& aj) {
    if (!aj.is_object()) return json::object();
    if (tipo == kTipoSalida)
        // `productor`: si la Salida cuelga del medio de la cadena, lo que publica su basalida.
        return {{"capas", json::array({{{"fuente", aj.value("fuente", std::string())},
                                        {"productor", aj.value("productor", std::string())},
                                        {"x", 0.0}, {"y", 0.0}, {"ancho", 1.0}, {"alto", 1.0}}})}};
    json config = aj.value("config", json());
    return config.is_string() ? json::parse(config.get<std::string>(), nullptr, false) : config;
}
const std::string kPrefijoCompositor = "__compositor:";

/// Lo que pinta la app (QImage → cuadros al motor): los de siempre y todo tipo
/// registrado como «deApp» (fuentes::registrar). Antes era solo esta lista, y un
/// tipo nuevo (la fuente PDF) se rechazaba con «todavía no lo arma el motor».
bool esDeApp(const std::string& id) {
    if (id == "ba_subtitulos" || id == "ba_chat_overlay" || id == "ba_grafico" || id == "ba_app" || id == "ba_qr" ||
        id == "ba_video_compositor" || id == "ba_placeholder")
        return true;
    fuentes::Tipo t;
    return fuentes::tipo(id, t) && t.deApp;
}

} // namespace

// ============================================================================
// Efectos de una capa (transparencia, borde difuso, sombra)
// ============================================================================

bool EfectosCapa::aplicar(const json& j, std::string* error) {
    if (!j.is_object()) {
        if (error) *error = ba::i18n::traducir("emision", "efectos: tiene que ser un objeto");
        return false;
    }
    const auto num = [&](const json& o, const char* k, float& v, float mn, float mx) {
        if (!o.contains(k)) return true;
        if (!o[k].is_number()) {
            if (error) *error = ba::i18n::llenar(ba::i18n::traducir("emision", "efectos: «%1» tiene que ser un número"), {k});
            return false;
        }
        v = std::clamp(o[k].get<float>(), mn, mx);
        return true;
    };
    if (!num(j, "opacidad", opacidad, 0, 1) || !num(j, "borde", borde, 0, 1000) || !num(j, "esquinas", esquinas, 0, 2000)) return false;
    if (j.contains("sombra")) {
        const json& s = j["sombra"];
        if (s.is_boolean()) {
            sombra = s.get<bool>();
            return true;
        }
        if (!s.is_object()) {
            if (error) *error = ba::i18n::traducir("emision", "efectos.sombra: tiene que ser un objeto (o true/false)");
            return false;
        }
        if (s.contains("activa")) sombra = s.value("activa", sombra);
        else sombra = true; // pedir una sombra con sus números la prende
        if (s.contains("color")) {
            const std::string c = s["color"].is_string() ? s["color"].get<std::string>() : std::string();
            const std::string h = !c.empty() && c[0] == '#' ? c.substr(1) : c;
            const bool hex = (h.size() == 6 || h.size() == 8) &&
                             std::all_of(h.begin(), h.end(), [](char x) { return std::isxdigit(static_cast<unsigned char>(x)) != 0; });
            if (!hex) {
                if (error) *error = "efectos.sombra.color: \"#RRGGBB\" o \"#AARRGGBB\"";
                return false;
            }
            color = "#" + h;
        }
        if (!num(s, "alfa", alfa, 0, 1) || !num(s, "difuminado", difuminado, 0, 500) || !num(s, "distancia", distancia, 0, 1000) ||
            !num(s, "angulo", angulo, -360, 360) || !num(s, "tamano", tamano, -500, 500))
            return false;
    }
    return true;
}

json EfectosCapa::aJson() const {
    return {{"opacidad", opacidad},
            {"borde", borde},
            {"esquinas", esquinas},
            {"sombra",
             {{"activa", sombra}, {"color", color}, {"alfa", alfa}, {"difuminado", difuminado}, {"distancia", distancia},
              {"angulo", angulo}, {"tamano", tamano}}}};
}

json EfectosCapa::sombraMotor() const {
    if (!sombra && borde <= 0 && esquinas <= 0) return nullptr;
    return {{"alfa", sombra ? alfa : 0.0f}, {"color", color},          {"difuminado", difuminado}, {"distancia", distancia},
            {"angulo", angulo},             {"tamano", tamano},        {"borde", borde},
            {"radio", esquinas}};
}

EscenasYFuentes::EscenaApp* EscenasYFuentes::escena(const std::string& nombre) {
    return const_cast<EscenaApp*>(std::as_const(*this).escena(nombre));
}

const EscenasYFuentes::EscenaApp* EscenasYFuentes::escena(const std::string& nombre) const {
    // Por el id primero (lo que se guarda), después por el nombre (lo que se pide).
    if (nombre.empty()) return nullptr;
    for (const auto& e : scenes_)
        if (e.id == nombre) return &e;
    for (const auto& e : scenes_)
        if (e.nombre == nombre) return &e;
    return nullptr;
}

std::string EscenasYFuentes::idDe(const std::string& escenaPedida) const {
    const EscenaApp* e = escena(escenaPedida);
    return e != nullptr ? e->id : std::string();
}

std::string EscenasYFuentes::nombreDe(const std::string& escenaPedida) const {
    const EscenaApp* e = escena(escenaPedida);
    return e != nullptr ? e->nombre : std::string();
}

escenas::TablaEscenas EscenasYFuentes::tabla() const {
    escenas::TablaEscenas t;
    for (const auto& e : scenes_) t.agregar(e.id, e.nombre);
    return t;
}

std::string EscenasYFuentes::errorDeCompositor(const std::string& nombre) const {
    const json e = m_.estadoMotor().value("video", json::object()).value("errores_escenas", json::object());
    const auto it = e.find(kPrefijoCompositor + nombre);
    return it != e.end() && it->is_string() ? it->get<std::string>() : std::string();
}

json EscenasYFuentes::escenaCompositor(const std::string& nombre, const json& config) const {
    // Con lienzo propio (la Cámara 360: las dos lentes lado a lado sin achicar), las
    // capas van en ese; si no, en el del estudio.
    const json lienzo = config.is_object() ? config.value("lienzo", json::object()) : json::object();
    const bool propio = lienzo.is_object() && lienzo.value("ancho", 0) > 0 && lienzo.value("alto", 0) > 0;
    const double W = propio ? lienzo.value("ancho", 0) : m_.video_.baseWidth;
    const double H = propio ? lienzo.value("alto", 0) : m_.video_.baseHeight;
    json capas = json::array();
    int orden = 0;
    if (config.is_object())
        for (const auto& k : config.value("capas", json::array())) {
            const std::string f = k.value("fuente", std::string());
            if (f.empty() || sources_.count(f) == 0) continue;
            json capa{{"entrada", f},
                             {"x", static_cast<int>(std::lround(k.value("x", 0.0) * W))},
                             {"y", static_cast<int>(std::lround(k.value("y", 0.0) * H))},
                             {"ancho", std::max(2, static_cast<int>(std::lround(k.value("ancho", 1.0) * W)))},
                             {"alto", std::max(2, static_cast<int>(std::lround(k.value("alto", 1.0) * H)))},
                             {"alfa", k.value("opacidad", 1.0)},
                             {"orden", orden++},
                             {"visible", true},
                             {"contener", k.value("ajuste", std::string("encajar")) != "llenar"}};
            if (!k.value("productor", std::string()).empty()) capa["productor"] = k["productor"];
            // La «Mezcla de GStreamer» (config.mezclador): el pad del módulo y sus propiedades.
            if (k.contains("pad")) capa["pad"] = k["pad"];
            if (k.contains("propiedades_pad")) capa["propiedades_pad"] = k["propiedades_pad"];
            // Los efectos de la capa del compositor (como los de un elemento de la escena).
            if (k.contains("efectos")) {
                EfectosCapa ef;
                if (ef.aplicar(k["efectos"])) {
                    if (const json s = ef.sombraMotor(); !s.is_null()) capa["sombra"] = s;
                    if (k["efectos"].contains("opacidad")) capa["alfa"] = ef.opacidad;
                }
            }
            capas.push_back(capa);
        }
    json e{{"nombre", kPrefijoCompositor + nombre}, {"capas", capas}};
    if (propio) e["lienzo"] = lienzo;
    if (config.is_object() && config.contains("filtros")) e["filtros"] = config["filtros"];
    if (config.is_object() && config.contains("mezclador")) e["mezclador"] = config["mezclador"];
    return e;
}

json EscenasYFuentes::escenaMotor(const EscenaApp& e) const {
    const double W = m_.video_.baseWidth, H = m_.video_.baseHeight;
    const json& entradas = seccion(m_.estadoMotor(), "entradas");
    json capas = json::array();
    for (std::size_t i = 0; i < e.items.size(); ++i) {
        const Item& it = e.items[i];
        const auto fu = sources_.find(it.fuente);
        // «Banca con la palabra»: la cámara de la banca de su ranura (o nada).
        if (fu != sources_.end() && fu->second.banca) {
            const int r = ranuraDe(fu->second);
            if (r < 1 || r > static_cast<int>(ranurasBanca_.size()) || ranurasBanca_[r - 1].entrada.empty()) continue;
            const RanuraBanca& rb = ranurasBanca_[r - 1];
            json c{{"entrada", rb.entrada},
                   {"x", static_cast<int>(std::lround(it.rect.x * W))},
                   {"y", static_cast<int>(std::lround(it.rect.y * H))},
                   {"ancho", std::max(2, static_cast<int>(std::lround(it.rect.w * W)))},
                   {"alto", std::max(2, static_cast<int>(std::lround(it.rect.h * H)))},
                   {"orden", static_cast<int>(i)},
                   {"visible", it.visible},
                   {"contener", true}};
            if (it.efectos.opacidad < 1.0f) c["alfa"] = it.efectos.opacidad;
            if (const json s = it.efectos.sombraMotor(); !s.is_null()) c["sombra"] = s;
            // La toma entera (antes del PTZ digital), recortada acá: no mueve la cámara.
            if (rb.entera) c["productor"] = rb.entrada + ":entera:video";
            const json sale = entradas.contains(rb.entrada) ? entradas[rb.entrada].value("sale", json::object()) : json::object();
            const int w = sale.value("ancho", 0), h = sale.value("alto", 0);
            if (w > 0 && h > 0 && rb.recorte[0] + rb.recorte[1] + rb.recorte[2] + rb.recorte[3] > 0) {
                auto px = [](float f, int total) { return static_cast<int>(std::lround(std::clamp(f, 0.0f, 0.95f) * total)); };
                c["recorte"] = {{"izq", px(rb.recorte[0], w)}, {"arr", px(rb.recorte[1], h)}, {"der", px(rb.recorte[2], w)},
                                {"aba", px(rb.recorte[3], h)}};
            }
            capas.push_back(c);
            continue;
        }
        const bool comp = fu != sources_.end() && !fu->second.compProductor.empty();
        json c{{"entrada", comp ? kPrefijoCompositor + it.fuente : it.fuente},
               {"x", static_cast<int>(std::lround(it.rect.x * W))},
               {"y", static_cast<int>(std::lround(it.rect.y * H))},
               {"ancho", std::max(2, static_cast<int>(std::lround(it.rect.w * W)))},
               {"alto", std::max(2, static_cast<int>(std::lround(it.rect.h * H)))},
               {"orden", static_cast<int>(i)},
               {"visible", it.visible},
               {"contener", true}};
        if (it.efectos.opacidad < 1.0f) c["alfa"] = it.efectos.opacidad;
        if (const json s = it.efectos.sombraMotor(); !s.is_null()) c["sombra"] = s;
        if (comp) c["productor"] = fu->second.compProductor; // la composición del compositor
        // Traer: lo que se ve de esta entrada viene de otra parte.
        if (const auto t = traer_.find(it.fuente); !comp && t != traer_.end()) {
            const auto co = sources_.find(t->second.first);
            if (!t->second.first.empty() && co != sources_.end() && !co->second.compProductor.empty())
                c["productor"] = co->second.compProductor;
            else if (!t->second.second.empty())
                c["productor"] = t->second.second;
        }
        // El recorte va en píxeles de la fuente: hace falta su tamaño.
        if (it.recorte[0] + it.recorte[1] + it.recorte[2] + it.recorte[3] > 0) {
            const json sale = entradas.contains(it.fuente) ? entradas[it.fuente].value("sale", json::object()) : json::object();
            const int w = sale.value("ancho", 0), h = sale.value("alto", 0);
            if (w > 0 && h > 0) {
                auto px = [](float f, int total) { return static_cast<int>(std::lround(std::clamp(f, 0.0f, 0.95f) * total)); };
                c["recorte"] = {{"izq", px(it.recorte[0], w)}, {"arr", px(it.recorte[1], h)}, {"der", px(it.recorte[2], w)},
                                {"aba", px(it.recorte[3], h)}};
            }
        }
        capas.push_back(c);
    }
    sumarVideoDeGraficos(e, capas); // el video en vivo de los gráficos que están al aire en ella
    return {{"nombre", e.id}, {"capas", capas}}; // el motor la conoce por el id: renombrarla no lo toca
}

int EscenasYFuentes::ranuraDe(const Fuente& f) const {
    const json a = json::parse(f.spec.settingsJson.empty() ? "{}" : f.spec.settingsJson, nullptr, false);
    // Número (o texto, si vino de un formulario). Una que no existe (0: no se dibuja), no la 1: repetiría al que habla.
    const json r = a.is_object() ? a.value("ranura", json(1)) : json(1);
    const int n = r.is_number() ? r.get<int>() : r.is_string() ? std::atoi(r.get<std::string>().c_str()) : 1;
    return n < 1 ? 1 : n <= BroadcastEngine::kRanurasBanca ? n : 0;
}

void EscenasYFuentes::setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar) {
    const auto igual = [](const RanuraBanca& a, const RanuraBanca& b) {
        return a.entrada == b.entrada && a.entera == b.entera && std::equal(std::begin(a.recorte), std::end(a.recorte), std::begin(b.recorte));
    };
    if (!forzar && ranuras.size() == ranurasBanca_.size() &&
        std::equal(ranuras.begin(), ranuras.end(), ranurasBanca_.begin(), igual))
        return;
    ranurasBanca_ = std::move(ranuras);
    for (const EscenaApp& e : scenes_)
        for (const Item& i : e.items) {
            const auto f = sources_.find(i.fuente);
            if (f != sources_.end() && f->second.banca) {
                publicarEscena(e);
                break;
            }
        }
}

bool EscenasYFuentes::hayFuenteBanca() const {
    return !escenaDeRanura(0).empty();
}

std::string EscenasYFuentes::escenaDeRanura(int ranura) const {
    // 0: cualquier ranura.
    for (const EscenaApp& e : scenes_)
        for (const Item& i : e.items) {
            const auto f = sources_.find(i.fuente);
            if (f != sources_.end() && f->second.banca && (ranura == 0 || ranuraDe(f->second) == ranura)) return e.nombre;
        }
    return {};
}

void EscenasYFuentes::setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor) {
    const auto antes = traer_.find(entrada);
    if (compositor.empty() && productor.empty()) {
        if (antes == traer_.end()) return;
        traer_.erase(antes);
    } else {
        const std::pair<std::string, std::string> nuevo{compositor, productor};
        if (antes != traer_.end() && antes->second == nuevo) return;
        traer_[entrada] = nuevo;
    }
    for (const EscenaApp& e : scenes_)
        for (const Item& it : e.items)
            if (it.fuente == entrada) {
                publicarEscena(e);
                break;
            }
}

void EscenasYFuentes::publicarEscena(const EscenaApp& e) {
    if (!m_.motor_) return;
    m_.motor_->pedirFoto(); // el tamaño de una fuente nueva puede haber llegado recién
    m_.motor_->definirEscena(escenaMotor(e));
}

bool EscenasYFuentes::createScene(const std::string& name, const std::string& idPedido) {
    if (!m_.started_ || name.empty() || escena(name) != nullptr || (!idPedido.empty() && escena(idPedido) != nullptr)) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "createScene: sin motor, nombre vacio o repetido: '%1'"), {name}));
        return false;
    }
    if (name.rfind("__", 0) == 0) { // las internas (kPrefijoCompositor) viven en el mismo lugar del motor
        m_.setError(ba::i18n::traducir("emision", "los nombres de escena que empiezan con «__» son internos"));
        return false;
    }
    const std::string id = !idPedido.empty() ? idPedido : escenas::nuevoIdEscena([this](const std::string& i) { return escena(i) != nullptr; });
    scenes_.push_back(EscenaApp{id, name, {}});
    publicarEscena(scenes_.back());
    return true;
}

bool EscenasYFuentes::removeScene(const std::string& name) {
    const std::string id = idDe(name);
    auto it = std::find_if(scenes_.begin(), scenes_.end(), [&](const EscenaApp& e) { return e.id == id; });
    if (it == scenes_.end() || scenes_.size() == 1) {
        m_.setError(ba::i18n::traducir("emision", "removeScene: no existe o es la única escena"));
        return false;
    }
    // Las salidas que la tienen al aire pasan a otra con un corte.
    const std::string other = (it == scenes_.begin()) ? (it + 1)->id : scenes_.front().id;
    for (const auto& sid : m_.ordenSalidas_)
        if (m_.salidas_.at(sid)->alAire == id) setCurrentSceneCon(other, "cut", 0, sid);
    const std::vector<Item> items = it->items;
    scenes_.erase(it);
    m_.motor_->quitarEscena(id);
    for (const auto& item : items) soltarSiHuerfana(item.fuente);
    return true;
}

bool EscenasYFuentes::renameScene(const std::string& from, const std::string& to) {
    if (to.empty() || escena(to) != nullptr) {
        m_.setError(ba::i18n::traducir("emision", "renameScene: nombre vacio o repetido"));
        return false;
    }
    if (to.rfind("__", 0) == 0) {
        m_.setError(ba::i18n::traducir("emision", "los nombres de escena que empiezan con «__» son internos"));
        return false;
    }
    EscenaApp* e = escena(from);
    if (e == nullptr) return false;
    // Solo el nombre: el motor la conoce por el id, así que lo que está al aire o armado
    // sigue como estaba, sin un corte ni un cuadro de más.
    e->nombre = to;
    return true;
}

std::vector<std::string> EscenasYFuentes::sceneNames() const {
    std::vector<std::string> out;
    for (const auto& e : scenes_) out.push_back(e.nombre);
    return out;
}

std::string EscenasYFuentes::transicionMotor(const std::string& tipo) const {
    if (tipo == "cut") return "corte";
    if (tipo == "swipe" || tipo == "slide") return "empuje";
    if (tipo == "wipe" || tipo == "cortina") return "cortina";
    return "mezcla"; // fade, fade_to_color, stinger…
}

bool EscenasYFuentes::setTransition(const std::string& type, int durationMs) {
    if (!m_.started_) return false;
    transitionType_ = type.empty() ? std::string("fade") : type;
    transitionMs_ = std::max(0, durationMs);
    return true;
}

bool EscenasYFuentes::setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    if (escena(name) == nullptr) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena '%1'"), {name}));
        return false;
    }
    SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    const int dur = ms < 0 ? transitionMs_ : ms;
    const std::string t = s->alAire.empty() || dur == 0 ? "corte" : transicionMotor(tipo.empty() ? transitionType_ : tipo);
    const std::string id = idDe(name);
    const std::string e = m_.motor_->alAire(id, t, dur, s->id);
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    s->alAire = id;
    return true;
}

bool EscenasYFuentes::setCurrentScene(const std::string& name, const std::string& salida) {
    return setCurrentSceneCon(name, transitionType_, -1, salida);
}

std::string EscenasYFuentes::currentScene(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    return s != nullptr ? nombreDe(s->alAire) : std::string();
}

std::string EscenasYFuentes::currentSceneId(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    return s != nullptr ? s->alAire : std::string();
}

std::string EscenasYFuentes::armadaId(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    return s != nullptr ? s->armada : std::string();
}

bool EscenasYFuentes::transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida) {
    if (alAire != nullptr) *alAire = false;
    if (escena(name) == nullptr) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena '%1'"), {name}));
        return false;
    }
    SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    const std::string id = idDe(name);
    if (id == s->alAire) {
        m_.setError(ba::i18n::traducir("emision", "esa escena ya está al aire"));
        return false;
    }
    std::string e;
    const bool quedo = m_.motor_->tbar(id, t, soltar, &e, s->id);
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    if (quedo) {
        s->alAire = id;
        if (alAire != nullptr) *alAire = true;
    }
    return true;
}

std::string EscenasYFuentes::manualHacia(const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    return m_.motor_ && s != nullptr ? nombreDe(m_.motor_->tbarHacia(s->id)) : std::string();
}

bool EscenasYFuentes::buildScene() {
    if (escena("programa") == nullptr && !createScene("programa")) return false;
    return setCurrentScene("programa");
}

bool EscenasYFuentes::armar(const std::string& name, const std::string& salida) {
    SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    if (!name.empty() && escena(name) == nullptr) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena '%1'"), {name}));
        return false;
    }
    const std::string id = idDe(name);
    if (!m_.started_ || !m_.motor_) {
        s->armada = id; // el próximo motor la arma (resetVideo)
        return true;
    }
    // LO ARMADO ES EL BUS DE PREVIEW DEL MOTOR: una composición que vive
    // siempre y a la que armar le cambia la escena con un corte (las capas
    // nuevas reemplazan a las viejas cuando ya tienen cuadro). Antes cada
    // escena armada tenía su propia vista y su propia composición: en cada
    // TAKE se soltaba una y se armaba otra de cero (ramas, shaders) y el
    // preview parpadeaba en negro o sin capas, y además iba a 10-15 cuadros/s.
    const std::string e = m_.motor_->armar(id, s->id);
    if (!e.empty()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "armar: %1"), {e}));
        return false;
    }
    s->armada = id;
    return true;
}

bool EscenasYFuentes::ajustesMotor(const SourceSpec& spec, json& a) const {
    if (!ajustesMotorTipo(spec, a)) return false;
    // Los filtros de la cadena (las tarjetas del grafo, el PTZ digital...), el rack de
    // audio, los desfases y la clase de sincronía (estudio: sala/propia) valen para
    // cualquier tipo. Sin «estudio» acá, una imagen o un texto guardaban la clase elegida
    // pero el motor no se enteraba (el botón Sala/Propia «no hacía nada»).
    const json s = json::parse(spec.settingsJson.empty() ? "{}" : spec.settingsJson, nullptr, false);
    if (s.is_object() && a.is_object())
        for (const char* k : {"filtros", "filtros_audio", "rack", "desfase", "desfase_video", "desfase_audio", "estudio", "color_real",
                              "desentrelazar", "campo"})
            if (s.contains(k) && !a.contains(k)) a[k] = s[k];
    return true;
}

bool EscenasYFuentes::ajustesMotorTipo(const SourceSpec& spec, json& a) const {
    const json s = json::parse(spec.settingsJson.empty() ? "{}" : spec.settingsJson, nullptr, false);
    const json in = s.is_object() ? s : json::object();
    const std::string& id = spec.id;
    if (id == inputs::kMicType || id == "ba_gst_fuente") {
        a = in;
        a["nombre"] = spec.name;
        return true;
    }
    if (id == inputs::kMedioType) {
        a = in;
        a["medio"] = true;
        a["nombre"] = spec.name;
        if (a.contains("local_file") && a["local_file"].is_string()) a["local_file"] = fuentes::rutaDelEvento(a["local_file"]);
        return true;
    }
    if (esPantalla(id)) {
        a = video::ajustesPantalla(in); // la pantalla elegida (video/FuentesCaptura)
        return true;
    }
    if (id == video::kTipoVentana) {
        a = video::ajustesVentana(in);
        return true;
    }
    if (esDeApp(id)) {
        a = {{"tipo", "app"}, {"nombre", spec.name}};
        return true;
    }
    // Los tipos registrados que traen su entrada del motor (FuentesApp: Tipo::motor).
    if (fuentes::Tipo t; fuentes::tipo(id, t) && t.motor) {
        // Con el tamaño que ocupa en el lienzo (un texto se arma a ese tamaño).
        a = t.motor(fuentes::conDefectos(id, in), std::max(16, static_cast<int>(std::lround(spec.rect.w * m_.video_.baseWidth))),
                    std::max(16, static_cast<int>(std::lround(spec.rect.h * m_.video_.baseHeight))));
        a["nombre"] = spec.name;
        return true;
    }
    if (termina(id, "_output_capture")) {
        // El audio del sistema necesita un elemento propio por sistema (M0.5): todavía no.
        return false;
    }
    return false;
}

bool EscenasYFuentes::crearFuente(const SourceSpec& pedida, bool global) {
    if (sources_.count(pedida.name) > 0) return true;
    // El motor separa con «:» la entrada de lo que sale de ella («Cámara:video»): un nombre
    // con «:» no se puede crear allá. Se dice acá, y no queda una fuente «agregada» y muerta.
    if (pedida.name.find(':') != std::string::npos) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "el nombre de una fuente no puede tener «:» («%1»): usá «—» o «-»"), {pedida.name}));
        return false;
    }
    // Lo que llegue con un tipo de antes (el MCP lo acepta), al del motor; lo que no
    // tiene equivalente se avisa (InputSpecs: alMotor).
    SourceSpec spec = pedida;
    inputs::alMotor(spec);
    if (spec.id == BroadcastEngine::kTipoBanca) {
        // «Banca con la palabra»: no hay entrada en el motor; la escena la resuelve al publicarse.
        Fuente f;
        f.spec = spec;
        f.global = global;
        f.banca = true;
        sources_[spec.name] = f;
        ordenFuentes_.push_back(spec.name);
        return true;
    }
    if (spec.id == kTipoCompositor || spec.id == kTipoSalida) {
        // Una escena interna del motor (sus capas: las entradas que junta; la
        // salida de una tarjeta, su entrada a todo el lienzo).
        const json aj = json::parse(spec.settingsJson.empty() ? "{}" : spec.settingsJson, nullptr, false);
        m_.motor_->definirEscena(escenaCompositor(spec.name, configDe(spec.id, aj)));
        Fuente f;
        f.spec = spec;
        f.global = global;
        f.compProductor = m_.motor_->usarEscena(kPrefijoCompositor + spec.name);
        sources_[spec.name] = f;
        ordenFuentes_.push_back(spec.name);
        return true;
    }
    json ajustes;
    if (!ajustesMotor(spec, ajustes)) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "fuente '%1': el tipo '%2' todavía no lo arma el motor"), {spec.name, spec.id}));
        return false;
    }
    json opciones{{"global", global}, {"nivel", m_.mezcla_->nivelAplicado(spec.name, spec.volume, m_.porOmision())}, {"mute", spec.muted}};
    const std::string e = m_.motor_->agregarEntrada(spec.name, ajustes, opciones);
    if (!e.empty()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "fuente '%1': %2"), {spec.name, e}));
        return false;
    }
    Fuente f;
    f.spec = spec;
    f.global = global;
    sources_[spec.name] = f;
    ordenFuentes_.push_back(spec.name);
    // En la mezcla de cada salida entra con el mismo volumen y mute (las opciones valen para
    // todas), con el VCA de su grupo en ESA salida.
    for (auto& [id, s] : m_.salidas_)
        if (!s->porOmision()) {
            s->volumen[spec.name] = spec.volume;
            s->mute[spec.name] = spec.muted;
            if (const float n = m_.mezcla_->nivelAplicado(spec.name, spec.volume, *s); n != m_.mezcla_->nivelAplicado(spec.name, spec.volume, m_.porOmision()))
                m_.motor_->ponerNivel(spec.name, n, id);
        }
    if (esDeApp(spec.id)) fuentes::alta(spec.name, spec.id, json::parse(spec.settingsJson, nullptr, false));
    // Una fuente nueva (o rehecha: una placa que cambió de canales): lo guardado de su
    // audio se le vuelve a poner (Matriz).
    if (m_.cambioAudio_) m_.cambioAudio_(spec.name, "alta");
    // Los compositores que la juntan (creados antes que ella, p. ej. al arrancar) se rearman.
    for (const auto& [n, c] : sources_) {
        if (c.compProductor.empty()) continue;
        const json aj = json::parse(c.spec.settingsJson.empty() ? "{}" : c.spec.settingsJson, nullptr, false);
        const json config = configDe(c.spec.id, aj);
        bool usa = false;
        if (config.is_object())
            for (const auto& k : config.value("capas", json::array())) usa = usa || k.value("fuente", std::string()) == spec.name;
        if (usa) m_.motor_->definirEscena(escenaCompositor(n, config));
    }
    if (m_.motor_) m_.motor_->pedirFoto();
    return true;
}

void EscenasYFuentes::soltarSiHuerfana(const std::string& nombre) {
    auto it = sources_.find(nombre);
    if (it == sources_.end() || it->second.global) return;
    for (const auto& e : scenes_)
        for (const auto& i : e.items)
            if (i.fuente == nombre) return;
    for (const auto& [id, s] : m_.salidas_)
        for (const auto& d : s->dsk)
            if (std::find(d.contenido.begin(), d.contenido.end(), nombre) != d.contenido.end()) return;
    if (!it->second.compProductor.empty()) {
        m_.motor_->soltarEscena(kPrefijoCompositor + nombre);
        m_.motor_->quitarEscena(kPrefijoCompositor + nombre);
    } else if (!it->second.banca)
        m_.motor_->quitarEntrada(nombre);
    fuentes::baja(nombre);
    sources_.erase(it);
    m_.mezcla_->olvidarFuente(nombre);
    ordenFuentes_.erase(std::remove(ordenFuentes_.begin(), ordenFuentes_.end(), nombre), ordenFuentes_.end());
    // Como el alta: la que se va (una global, de una placa o de la matriz, no pasa por una
    // escena) sale de la mezcla y de la lista de fuentes de la foto.
    if (m_.cambioAudio_) m_.cambioAudio_(nombre, "baja");
}

bool EscenasYFuentes::addSource(const std::string& sceneName, const SourceSpec& spec) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "addSource: no existe la escena '%1'"), {sceneName}));
        return false;
    }
    if (spec.id.empty() || spec.name.empty()) {
        m_.setError(ba::i18n::traducir("emision", "addSource: fuente sin id o sin nombre"));
        return false;
    }
    if (!crearFuente(spec, false)) return false;
    for (const auto& i : e->items)
        if (i.fuente == spec.name) return true; // ya estaba en esta escena
    Item it;
    it.fuente = spec.name;
    it.rect = spec.rect;
    it.visible = spec.visible;
    std::copy(std::begin(spec.recorte), std::end(spec.recorte), std::begin(it.recorte));
    it.efectos = spec.efectos;
    e->items.push_back(it);
    if (it.recorte[0] + it.recorte[1] + it.recorte[2] + it.recorte[3] > 0) recortesPendientes_.push_back({e->id, spec.name});
    publicarEscena(*e);
    return true;
}

bool EscenasYFuentes::addSource(const SourceSpec& spec) {
    if (currentScene().empty() && !buildScene()) return false;
    return addSource(currentScene(), spec);
}

bool EscenasYFuentes::removeSource(const std::string& sceneName, const std::string& name) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) return false;
    const auto it = std::find_if(e->items.begin(), e->items.end(), [&](const Item& i) { return i.fuente == name; });
    if (it == e->items.end()) return false;
    e->items.erase(it);
    publicarEscena(*e);
    soltarSiHuerfana(name);
    return true;
}

bool EscenasYFuentes::moveSource(const std::string& sceneName, const std::string& name, int delta) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr || delta == 0) return false;
    auto it = std::find_if(e->items.begin(), e->items.end(), [&](const Item& i) { return i.fuente == name; });
    if (it == e->items.end()) return false;
    const auto i = static_cast<std::ptrdiff_t>(it - e->items.begin());
    const auto j = std::clamp<std::ptrdiff_t>(i + (delta > 0 ? 1 : -1), 0, static_cast<std::ptrdiff_t>(e->items.size()) - 1);
    if (i == j) return false;
    std::swap(e->items[static_cast<std::size_t>(i)], e->items[static_cast<std::size_t>(j)]);
    publicarEscena(*e);
    return true;
}

std::vector<std::string> EscenasYFuentes::sceneSources(const std::string& sceneName) const {
    std::vector<std::string> out;
    if (const EscenaApp* e = escena(sceneName))
        for (const auto& i : e->items) out.push_back(i.fuente);
    return out;
}

bool EscenasYFuentes::setSourceVisible(const std::string& sceneName, const std::string& name, bool visible) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) return false;
    for (auto& i : e->items)
        if (i.fuente == name) {
            i.visible = visible;
            publicarEscena(*e);
            return true;
        }
    return false;
}

bool EscenasYFuentes::sourceVisible(const std::string& sceneName, const std::string& name) const {
    if (const EscenaApp* e = escena(sceneName))
        for (const auto& i : e->items)
            if (i.fuente == name) return i.visible;
    return false;
}

bool EscenasYFuentes::setSourceRect(const std::string& sceneName, const std::string& name, const CanvasRect& rect) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) return false;
    for (auto& i : e->items)
        if (i.fuente == name) {
            i.rect = rect;
            publicarEscena(*e);
            return true;
        }
    return false;
}

bool EscenasYFuentes::setSourceCrop(const std::string& sceneName, const std::string& name, float izquierda, float arriba,
                                    float derecha, float abajo) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) return false;
    if (izquierda + derecha >= 0.95f || arriba + abajo >= 0.95f) return false;
    for (auto& i : e->items)
        if (i.fuente == name) {
            i.recorte[0] = izquierda;
            i.recorte[1] = arriba;
            i.recorte[2] = derecha;
            i.recorte[3] = abajo;
            publicarEscena(*e);
            // Sin tamaño todavía (una cámara que arranca): se vuelve a mandar cuando lo tenga.
            int w = 0, h = 0;
            if (!sourceSize(name, w, h) || w == 0) recortesPendientes_.push_back({e->id, name});
            return true;
        }
    return false;
}

bool EscenasYFuentes::setSourceEfectos(const std::string& sceneName, const std::string& name, const EfectosCapa& efectos) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) return false;
    for (auto& i : e->items)
        if (i.fuente == name) {
            i.efectos = efectos;
            publicarEscena(*e);
            return true;
        }
    return false;
}

EfectosCapa EscenasYFuentes::sourceEfectos(const std::string& sceneName, const std::string& name) const {
    if (const EscenaApp* e = escena(sceneName))
        for (const auto& i : e->items)
            if (i.fuente == name) return i.efectos;
    return {};
}

void EscenasYFuentes::aplicarRecortesPendientes() {
    if (recortesPendientes_.empty()) return;
    const auto pendientes = recortesPendientes_;
    recortesPendientes_.clear();
    if (m_.motor_) m_.motor_->pedirFoto();
    for (const auto& r : pendientes) {
        int w = 0, h = 0;
        if (!sourceSize(r.fuente, w, h) || w == 0) {
            recortesPendientes_.push_back(r); // sigue sin tamaño
            continue;
        }
        if (const EscenaApp* e = escena(r.escena)) publicarEscena(*e);
    }
}

bool EscenasYFuentes::renameSource(const std::string& from, const std::string& to) {
    if (to.empty() || sources_.count(to) > 0) {
        m_.setError(ba::i18n::traducir("emision", "renameSource: nombre vacio o repetido"));
        return false;
    }
    auto it = sources_.find(from);
    if (it == sources_.end()) return false;
    // En el motor el id es el nombre: se arma una entrada nueva con el nombre nuevo.
    Fuente f = it->second;
    f.spec.name = to;
    json ajustes;
    if (!ajustesMotor(f.spec, ajustes)) return false;
    const std::string e = m_.motor_->agregarEntrada(
        to, ajustes, json{{"global", f.global}, {"nivel", m_.mezcla_->nivelAplicado(to, f.spec.volume, m_.porOmision())}, {"mute", f.spec.muted}});
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    sources_.erase(it);
    sources_[to] = f;
    m_.mezcla_->renombrarFuente(from, to);
    fuentes::baja(from);
    if (esDeApp(f.spec.id)) fuentes::alta(to, f.spec.id, json::parse(f.spec.settingsJson, nullptr, false));
    if (m_.cambioAudio_) m_.cambioAudio_(to, "alta");
    std::replace(ordenFuentes_.begin(), ordenFuentes_.end(), from, to);
    std::replace(micInputs_.begin(), micInputs_.end(), from, to);
    for (auto& sc : scenes_) {
        bool cambio = false;
        for (auto& i : sc.items)
            if (i.fuente == from) {
                i.fuente = to;
                cambio = true;
            }
        if (cambio) publicarEscena(sc);
    }
    // En cada salida: sus capas, y su volumen y su mute (la entrada nueva llega con los
    // de la por omisión).
    for (auto& [id, s] : m_.salidas_) {
        for (auto& d : s->dsk) std::replace(d.contenido.begin(), d.contenido.end(), from, to);
        if (s->porOmision()) continue;
        if (auto v = s->volumen.extract(from)) {
            v.key() = to;
            m_.motor_->ponerNivel(to, m_.mezcla_->nivelAplicado(to, v.mapped(), *s), id);
            s->volumen.insert(std::move(v));
        }
        if (auto m = s->mute.extract(from)) {
            m.key() = to;
            m_.motor_->ponerMute(to, m.mapped(), id);
            s->mute.insert(std::move(m));
        }
    }
    m_.motor_->quitarEntrada(from);
    if (!f.pistas.empty()) m_.motor_->ponerPistas(to, f.pistas);
    return true;
}

json EscenasYFuentes::estadoEntrada(const std::string& name) const {
    if (sources_.count(name) == 0) return nullptr;
    const json& e = seccion(m_.estadoMotor(), "entradas");
    return e.contains(name) ? e[name] : json::object();
}

std::vector<std::pair<std::string, CanvasRect>> EscenasYFuentes::itemsVisibles(const std::string& scene) const {
    std::vector<std::pair<std::string, CanvasRect>> v;
    if (const EscenaApp* e = escena(scene))
        for (const auto& i : e->items)
            if (i.visible) v.emplace_back(i.fuente, i.rect);
    return v;
}

std::string EscenasYFuentes::controlEntrada(const std::string& name, const json& campos) {
    if (sources_.count(name) == 0) return ba::i18n::llenar(ba::i18n::traducir("emision", "no existe «%1»"), {name});
    return m_.motor_ ? m_.motor_->controlEntrada(name, campos) : ba::i18n::traducir("emision", "no hay motor");
}

std::string EscenasYFuentes::sourceTypeId(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.spec.id : std::string();
}

bool EscenasYFuentes::sourceSize(const std::string& name, int& width, int& height) const {
    width = height = 0;
    if (sources_.count(name) == 0) return false;
    const json& e = seccion(m_.estadoMotor(), "entradas");
    if (!e.contains(name)) return true;
    const json sale = e[name].value("sale", json::object());
    width = sale.value("ancho", 0);
    height = sale.value("alto", 0);
    return true;
}

std::string EscenasYFuentes::sourceSettingsJson(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.spec.settingsJson : std::string("{}");
}

bool EscenasYFuentes::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    const json nuevos = json::parse(settingsJson, nullptr, false);
    if (!nuevos.is_object()) {
        m_.setError(ba::i18n::traducir("emision", "settings no son JSON válido"));
        return false;
    }
    // Como OBS: lo nuevo se suma a lo que había.
    json todo = json::parse(it->second.spec.settingsJson.empty() ? "{}" : it->second.spec.settingsJson, nullptr, false);
    if (!todo.is_object()) todo = json::object();
    todo.update(nuevos);
    it->second.spec.settingsJson = todo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (it->second.banca) { // otra ranura: las escenas que la muestran se vuelven a publicar
        for (const EscenaApp& e : scenes_)
            for (const Item& i : e.items)
                if (i.fuente == name) {
                    publicarEscena(e);
                    break;
                }
        return true;
    }
    if (!it->second.compProductor.empty()) {
        m_.motor_->definirEscena(escenaCompositor(name, configDe(it->second.spec.id, todo)));
        return true;
    }
    if (esDeApp(it->second.spec.id)) fuentes::alta(name, it->second.spec.id, todo);
    json ajustes;
    if (!ajustesMotor(it->second.spec, ajustes)) return false;
    if (it->second.syncMs != 0) ajustes["desfase_audio"] = it->second.syncMs;
    const std::string e = m_.motor_->actualizarEntrada(name, ajustes);
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    return true;
}

std::vector<SourceType> EscenasYFuentes::tiposDeFuente() {
    return {{inputs::kVideoType, ba::i18n::traducir("emision", "Cámara o capturadora (USB, HDMI)"), true, true},
            {inputs::kMicType, ba::i18n::traducir("emision", "Entrada por GStreamer (pipeline propio)"), true, true},
            {inputs::kMedioType, ba::i18n::traducir("emision", "Archivo de video o audio"), true, true},
            {"ffmpeg_source", ba::i18n::traducir("emision", "Video de red (URL)"), true, true},
            {"color_source_v3", ba::i18n::traducir("emision", "Color"), true, false},
            {"text_ft2_source_v2", ba::i18n::traducir("emision", "Texto"), true, false},
            {"image_source", ba::i18n::traducir("emision", "Imagen"), true, false},
            {"screen_capture", ba::i18n::traducir("emision", "Captura de pantalla"), true, false},
            {video::kTipoVentana, ba::i18n::traducir("emision", "Captura de una aplicación"), true, false},
            {"ba_qr", ba::i18n::traducir("emision", "Código QR (Público, Conferencia, uno guardado…)"), true, false},
            {BroadcastEngine::kTipoBanca, ba::i18n::traducir("emision", "Banca con la palabra (asamblea: la cámara de quien habla)"), true, false},
            {"ba_pdf", ba::i18n::traducir("emision", "PDF (una página por vez)"), true, false},
            {"ba_grilla", ba::i18n::traducir("emision", "Grilla de personas (la gente de la Conferencia y la Asamblea)"), true, false},
            {"ba_app", ba::i18n::traducir("emision", "Lo que pinta la app"), true, false}};
}

bool EscenasYFuentes::sourceTypeExists(const std::string& typeId) const {
    if (typeId.empty()) return false;
    // Un tipo de antes vale si su versión del motor vale (lo pasa crearFuente).
    SourceSpec s;
    s.id = typeId;
    inputs::alMotor(s);
    json a;
    return s.id == BroadcastEngine::kTipoBanca || s.id == inputs::kMicType || s.id == inputs::kMedioType || ajustesMotor(s, a);
}

bool EscenasYFuentes::addGuestAudio(int slot) {
    const std::string name = "Invitado " + std::to_string(slot) + " (audio)";
    if (sources_.count(name) == 0) {
        SourceSpec spec;
        spec.id = inputs::kMicType;
        spec.name = name;
        spec.settingsJson = inputs::ajustesAudioAnillo("guest", slot);
        if (!crearFuente(spec, true)) return false;
    }
    // Lo que oye: de fábrica, el programa sin su propia voz.
    m_.motor_->retorno(slot, m_.mezcla_->retornoInvitado(slot), name);
    return true;
}

bool EscenasYFuentes::addGlobalAudio(const SourceSpec& spec) {
    if (sources_.count(spec.name) == 0 && !crearFuente(spec, true)) return false;
    if (std::find(micInputs_.begin(), micInputs_.end(), spec.name) == micInputs_.end()) micInputs_.push_back(spec.name);
    return true;
}

bool EscenasYFuentes::removeGlobalAudio(const std::string& name) {
    const auto it = std::find(micInputs_.begin(), micInputs_.end(), name);
    if (it == micInputs_.end()) return false;
    micInputs_.erase(it);
    if (auto f = sources_.find(name); f != sources_.end()) f->second.global = false;
    soltarSiHuerfana(name);
    return true;
}

std::vector<std::string> EscenasYFuentes::fuentesEnPrograma() const {
    std::vector<std::string> out;
    for (const auto& id : m_.ordenSalidas_)
        for (const std::string& f : fuentesEnPrograma(id))
            if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
    return out;
}

std::vector<std::string> EscenasYFuentes::fuentesEnPrograma(const std::string& salida) const {
    std::vector<std::string> out;
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return out;
    if (const EscenaApp* e = escena(s->alAire))
        for (const auto& i : e->items) {
            if (!i.visible) continue;
            // «Banca con la palabra»: lo que se ve es su cámara. Recortada de la toma
            // entera (digital) no cuenta: mover el PTZ de esa cámara no la cambia.
            const auto f = sources_.find(i.fuente);
            if (f != sources_.end() && f->second.banca) {
                const int r = ranuraDe(f->second);
                if (r >= 1 && r <= static_cast<int>(ranurasBanca_.size()) && !ranurasBanca_[r - 1].entrada.empty() &&
                    !ranurasBanca_[r - 1].entera)
                    out.push_back(ranurasBanca_[r - 1].entrada);
                continue;
            }
            out.push_back(i.fuente);
        }
    for (const std::string& f : fuentesDsk(s->id)) out.push_back(f);
    return out;
}

bool EscenasYFuentes::fuenteAlAire(const std::string& fuente) const {
    if (fuente.empty()) return false;
    // En alguna salida que esté saliendo.
    for (const auto& id : m_.ordenSalidas_) {
        if (!m_.emision_->haySalidaEn(id)) continue;
        const auto ss = fuentesEnPrograma(id);
        if (std::find(ss.begin(), ss.end(), fuente) != ss.end()) return true;
    }
    return false;
}

std::string EscenasYFuentes::ponerVideoDeGrafico(const std::string& grafico, const json& capas) {
    if (!capas.is_array() || capas.empty()) {
        quitarVideoDeGrafico(grafico);
        return {};
    }
    for (const auto& c : capas) {
        const std::string f = c.value("fuente", std::string());
        const auto it = sources_.find(f);
        if (it == sources_.end()) return ba::i18n::llenar(ba::i18n::traducir("emision", "no hay ninguna fuente «%1» (tiene que estar en alguna escena)"), {f});
        if (f == grafico) return ba::i18n::traducir("emision", "el gráfico no puede llevarse a sí mismo adentro");
        if (it->second.banca) return ba::i18n::llenar(ba::i18n::traducir("emision", "«%1» es una banca con la palabra: elegí la cámara"), {f});
        if (it->second.global) return ba::i18n::llenar(ba::i18n::traducir("emision", "«%1» es una entrada de audio, no tiene video"), {f});
    }
    bool enEscena = false;
    for (const EscenaApp& e : scenes_)
        for (const Item& i : e.items) enEscena = enEscena || i.fuente == grafico;
    if (!enEscena) return ba::i18n::llenar(ba::i18n::traducir("emision", "el gráfico «%1» no está en ninguna escena"), {grafico});
    videoDeGraficos_[grafico] = {{"capas", capas}, {"desde_ns", 0}};
    publicarEscenasCon(grafico);
    return {};
}

void EscenasYFuentes::empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs) {
    const auto it = videoDeGraficos_.find(grafico);
    if (it == videoDeGraficos_.end()) return;
    it->second["desde_ns"] = desdeNs;
    publicarEscenasCon(grafico);
}

void EscenasYFuentes::quitarVideoDeGrafico(const std::string& grafico) {
    if (videoDeGraficos_.erase(grafico) > 0) publicarEscenasCon(grafico);
}

void EscenasYFuentes::publicarEscenasCon(const std::string& fuente) {
    for (const EscenaApp& e : scenes_)
        for (const Item& i : e.items)
            if (i.fuente == fuente) {
                publicarEscena(e);
                break;
            }
}

void EscenasYFuentes::sumarVideoDeGraficos(const EscenaApp& e, json& capas) const {
    if (videoDeGraficos_.empty()) return;
    const double W = m_.video_.baseWidth, H = m_.video_.baseHeight;
    const json& entradas = seccion(m_.estadoMotor(), "entradas");
    // El orden de cada capa con decimales: las del video van entre el gráfico y la de al lado
    // (arriba o debajo), y al final se renumera todo de corrido.
    std::vector<double> orden;
    for (const auto& c : capas) orden.push_back(c.value("orden", 0));
    bool alguna = false;
    for (std::size_t gi = 0; gi < e.items.size(); ++gi) {
        const Item& g = e.items[gi];
        const auto v = videoDeGraficos_.find(g.fuente);
        if (v == videoDeGraficos_.end() || !g.visible) continue;
        // Dónde se ve el dibujo: su imagen (el tamaño de la fuente de gráfico) contenida en el ítem.
        int imgW = 900, imgH = 220;
        if (const auto fg = sources_.find(g.fuente); fg != sources_.end()) {
            const json a = json::parse(fg->second.spec.settingsJson.empty() ? "{}" : fg->second.spec.settingsJson, nullptr, false);
            if (a.is_object()) {
                imgW = a.value("ancho", imgW);
                imgH = a.value("alto", imgH);
            }
        }
        const graficos::video::Caja caja =
            graficos::video::dondeSeVe({g.rect.x * W, g.rect.y * H, g.rect.w * W, g.rect.h * H}, imgW, imgH);
        const std::int64_t desde = v->second.value("desde_ns", std::int64_t{0});
        int j = 0;
        for (const auto& vc : v->second.value("capas", json::array())) {
            const std::string f = vc.value("fuente", std::string());
            const auto fu = sources_.find(f);
            if (fu == sources_.end() || fu->second.banca || fu->second.global) continue;
            const bool comp = !fu->second.compProductor.empty();
            const std::string entrada = comp ? kPrefijoCompositor + f : f;
            const json sale = entradas.contains(f) ? entradas[f].value("sale", json::object()) : json::object();
            bool contener = true;
            const json claves = graficos::video::clavesEnLienzo(vc, caja, sale.value("ancho", 0), sale.value("alto", 0), &contener);
            if (claves.empty()) continue;
            // Ya está en la escena: se anima esa (y al quitarla vuelve a como estaba). Si no, o si
            // la declaración quiere una propia, una capa más.
            int existente = -1;
            if (!vc.value("propia", false))
                for (std::size_t i = 0; i < capas.size() && existente < 0; ++i)
                    if (capas[i].value("entrada", std::string()) == entrada && !capas[i].contains("claves")) existente = static_cast<int>(i);
            json c = existente >= 0 ? capas[static_cast<std::size_t>(existente)] : json{{"entrada", entrada}};
            if (comp && existente < 0) c["productor"] = fu->second.compProductor;
            // Lo de antes de arrancar: la primera clave (casi siempre con alfa 0).
            const json& k0 = claves[0];
            const json& r0 = k0["recorte"];
            const auto px = [](const json& n) { return static_cast<int>(std::lround(n.get<double>())); };
            c["x"] = px(k0["x"]);
            c["y"] = px(k0["y"]);
            c["ancho"] = px(k0["ancho"]);
            c["alto"] = px(k0["alto"]);
            c["alfa"] = k0["alfa"];
            c["recorte"] = {{"izq", px(r0["izq"])}, {"arr", px(r0["arr"])}, {"der", px(r0["der"])}, {"aba", px(r0["aba"])}};
            c["contener"] = contener;
            c["visible"] = true;
            c["claves"] = claves;
            c["claves_desde_ns"] = desde;
            const double o = static_cast<double>(gi) + (vc.value("arriba", true) ? 0.1 : -0.5) + 0.01 * ++j;
            if (existente >= 0) {
                capas[static_cast<std::size_t>(existente)] = c;
                orden[static_cast<std::size_t>(existente)] = o;
            } else {
                capas.push_back(c);
                orden.push_back(o);
            }
            alguna = true;
        }
    }
    if (!alguna) return;
    std::vector<std::size_t> idx(capas.size());
    std::iota(idx.begin(), idx.end(), std::size_t{0});
    std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) { return orden[a] < orden[b]; });
    for (std::size_t n = 0; n < idx.size(); ++n) capas[idx[n]]["orden"] = static_cast<int>(n);
}

bool EscenasYFuentes::addGuestVideo(int slot, const CanvasRect& rect) {
    SourceSpec spec = inputs::videoInvitadoSpec(slot, "guest", "Invitado " + std::to_string(slot));
    spec.rect = rect;
    return addSource(spec);
}

EscenasYFuentes::Fuente* EscenasYFuentes::fuente(const std::string& nombre) {
    const auto it = sources_.find(nombre);
    return it != sources_.end() ? &it->second : nullptr;
}

const EscenasYFuentes::Fuente* EscenasYFuentes::fuente(const std::string& nombre) const {
    const auto it = sources_.find(nombre);
    return it != sources_.end() ? &it->second : nullptr;
}

void EscenasYFuentes::reponer(const Copia& c) {
    micInputs_ = c.mics;
    scenes_ = c.escenas;
    for (const auto& e : scenes_) publicarEscena(e);
}

void EscenasYFuentes::vaciar() {
    scenes_.clear();
    sources_.clear();
    ordenFuentes_.clear();
    micInputs_.clear();
}

} // namespace ba::app
