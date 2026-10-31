// ============================================================================
// src/app/emision/BroadcastEngineScenes.cpp — Escenas, transiciones, fuentes y audio
//
// Parte de BroadcastEngine (ver BroadcastEngine.hpp). El modelo (qué escenas
// hay, qué fuentes tienen, dónde y con qué recorte) vive acá; el motor recibe
// cada escena ya traducida a capas en píxeles del lienzo y cada fuente ya
// traducida a los ajustes de su entrada.
//
// LOS TIPOS: app.json guarda los nombres de siempre (los de OBS y los propios).
// `ajustesMotor` los traduce:
//   ba_gst_fuente / ba_gst_medio        tal cual (el motor lee los mismos ajustes)
//   ffmpeg_source (archivo)              un medio; (red) una entrada por URL
//   color_source_v3, text_ft2_source_v2,
//   image_source                         tipo=color / texto / imagen del motor
//   cámaras, micrófonos, invitados, NDI  por las funciones de InputSpecs (ya eran GStreamer)
//   capturas de pantalla                 tipo=pantalla
//   lo que pinta la app (gráficos, chat, subtítulos)   tipo=app
// ============================================================================

#include "emision/BroadcastEngine.hpp"
#include "i18n/I18n.hpp"
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


/// Color de OBS (entero ABGR: 0xAABBGGRR) → "#AARRGGBB".
std::string colorObs(const json& v, const char* def) {
    if (!v.is_number()) return def;
    const auto c = static_cast<std::uint32_t>(v.get<std::int64_t>());
    const unsigned a = (c >> 24) & 0xFF, b = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, r = c & 0xFF;
    char out[16];
    std::snprintf(out, sizeof out, "#%02X%02X%02X%02X", a, r, g, b);
    return out;
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

// ============================================================================
// Escenas y transicion
// ============================================================================

BroadcastEngine::EscenaApp* BroadcastEngine::escena(const std::string& nombre) {
    for (auto& e : scenes_)
        if (e.nombre == nombre) return &e;
    return nullptr;
}

const BroadcastEngine::EscenaApp* BroadcastEngine::escena(const std::string& nombre) const {
    for (const auto& e : scenes_)
        if (e.nombre == nombre) return &e;
    return nullptr;
}

std::string BroadcastEngine::errorDeCompositor(const std::string& nombre) const {
    const json e = estadoMotor().value("video", json::object()).value("errores_escenas", json::object());
    const auto it = e.find(kPrefijoCompositor + nombre);
    return it != e.end() && it->is_string() ? it->get<std::string>() : std::string();
}

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

json BroadcastEngine::escenaCompositor(const std::string& nombre, const json& config) const {
    // Con lienzo propio (la Cámara 360: las dos lentes lado a lado sin achicar), las
    // capas van en ese; si no, en el del estudio.
    const json lienzo = config.is_object() ? config.value("lienzo", json::object()) : json::object();
    const bool propio = lienzo.is_object() && lienzo.value("ancho", 0) > 0 && lienzo.value("alto", 0) > 0;
    const double W = propio ? lienzo.value("ancho", 0) : video_.baseWidth;
    const double H = propio ? lienzo.value("alto", 0) : video_.baseHeight;
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

json BroadcastEngine::escenaMotor(const EscenaApp& e) const {
    const double W = video_.baseWidth, H = video_.baseHeight;
    const json& entradas = seccion(estadoMotor(), "entradas");
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
    return {{"nombre", e.nombre}, {"capas", capas}};
}

int BroadcastEngine::ranuraDe(const Fuente& f) const {
    const json a = json::parse(f.spec.settingsJson.empty() ? "{}" : f.spec.settingsJson, nullptr, false);
    // Número (o texto, si vino de un formulario). Una que no existe (0: no se dibuja), no la 1: repetiría al que habla.
    const json r = a.is_object() ? a.value("ranura", json(1)) : json(1);
    const int n = r.is_number() ? r.get<int>() : r.is_string() ? std::atoi(r.get<std::string>().c_str()) : 1;
    return n < 1 ? 1 : n <= kRanurasBanca ? n : 0;
}

void BroadcastEngine::setRanurasBanca(std::vector<RanuraBanca> ranuras, bool forzar) {
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

bool BroadcastEngine::hayFuenteBanca() const {
    return !escenaDeRanura(0).empty();
}

std::string BroadcastEngine::escenaDeRanura(int ranura) const {
    // 0: cualquier ranura.
    for (const EscenaApp& e : scenes_)
        for (const Item& i : e.items) {
            const auto f = sources_.find(i.fuente);
            if (f != sources_.end() && f->second.banca && (ranura == 0 || ranuraDe(f->second) == ranura)) return e.nombre;
        }
    return {};
}

void BroadcastEngine::setTraer(const std::string& entrada, const std::string& compositor, const std::string& productor) {
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

void BroadcastEngine::publicarEscena(const EscenaApp& e) {
    if (!motor_) return;
    motor_->pedirFoto(); // el tamaño de una fuente nueva puede haber llegado recién
    motor_->definirEscena(escenaMotor(e));
}

bool BroadcastEngine::createScene(const std::string& name) {
    if (!started_ || name.empty() || escena(name) != nullptr) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "createScene: sin motor, nombre vacio o repetido: '%1'"), {name}));
        return false;
    }
    if (name.rfind("__", 0) == 0) { // las internas (kPrefijoCompositor) viven en el mismo lugar del motor
        setError(ba::i18n::traducir("emision", "los nombres de escena que empiezan con «__» son internos"));
        return false;
    }
    scenes_.push_back(EscenaApp{name, {}});
    publicarEscena(scenes_.back());
    return true;
}

bool BroadcastEngine::removeScene(const std::string& name) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(), [&](const EscenaApp& e) { return e.nombre == name; });
    if (it == scenes_.end() || scenes_.size() == 1) {
        setError(ba::i18n::traducir("emision", "removeScene: no existe o es la unica escena"));
        return false;
    }
    // Las salidas que la tienen al aire pasan a otra con un corte.
    const std::string other = (it == scenes_.begin()) ? (it + 1)->nombre : scenes_.front().nombre;
    for (const auto& id : ordenSalidas_)
        if (salidas_.at(id)->alAire == name) setCurrentSceneCon(other, "cut", 0, id);
    const std::vector<Item> items = it->items;
    scenes_.erase(it);
    motor_->quitarEscena(name);
    for (const auto& item : items) soltarSiHuerfana(item.fuente);
    return true;
}

bool BroadcastEngine::renameScene(const std::string& from, const std::string& to) {
    if (to.empty() || escena(to) != nullptr) {
        setError(ba::i18n::traducir("emision", "renameScene: nombre vacio o repetido"));
        return false;
    }
    if (to.rfind("__", 0) == 0) {
        setError(ba::i18n::traducir("emision", "los nombres de escena que empiezan con «__» son internos"));
        return false;
    }
    EscenaApp* e = escena(from);
    if (e == nullptr) return false;
    e->nombre = to;
    publicarEscena(*e);
    for (const auto& [id, s] : salidas_) {
        if (s->alAire == from) {
            s->alAire = to;
            motor_->alAire(to, "corte", 0, id);
        }
        if (s->armada == from) armar(to, id);
    }
    motor_->quitarEscena(from);
    return true;
}

std::vector<std::string> BroadcastEngine::sceneNames() const {
    std::vector<std::string> out;
    for (const auto& e : scenes_) out.push_back(e.nombre);
    return out;
}

std::string BroadcastEngine::transicionMotor(const std::string& tipo) const {
    if (tipo == "cut") return "corte";
    if (tipo == "swipe" || tipo == "slide") return "empuje";
    if (tipo == "wipe" || tipo == "cortina") return "cortina";
    return "mezcla"; // fade, fade_to_color, stinger…
}

bool BroadcastEngine::setTransition(const std::string& type, int durationMs) {
    if (!started_) return false;
    transitionType_ = type.empty() ? std::string("fade") : type;
    transitionMs_ = std::max(0, durationMs);
    return true;
}

bool BroadcastEngine::setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms, const std::string& salida) {
    if (escena(name) == nullptr) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena '%1'"), {name}));
        return false;
    }
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    const int dur = ms < 0 ? transitionMs_ : ms;
    const std::string t = s->alAire.empty() || dur == 0 ? "corte" : transicionMotor(tipo.empty() ? transitionType_ : tipo);
    const std::string e = motor_->alAire(name, t, dur, s->id);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    s->alAire = name;
    return true;
}

bool BroadcastEngine::setCurrentScene(const std::string& name, const std::string& salida) {
    return setCurrentSceneCon(name, transitionType_, -1, salida);
}

const std::string& BroadcastEngine::currentScene(const std::string& salida) const {
    static const std::string kNinguna;
    const Salida* s = laSalida(salida);
    return s != nullptr ? s->alAire : kNinguna;
}

bool BroadcastEngine::transicionManual(const std::string& name, float t, bool soltar, bool* alAire, const std::string& salida) {
    if (alAire != nullptr) *alAire = false;
    if (escena(name) == nullptr) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la escena '%1'"), {name}));
        return false;
    }
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    if (name == s->alAire) {
        setError(ba::i18n::traducir("emision", "esa escena ya está al aire"));
        return false;
    }
    std::string e;
    const bool quedo = motor_->tbar(name, t, soltar, &e, s->id);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    if (quedo) {
        s->alAire = name;
        if (alAire != nullptr) *alAire = true;
    }
    return true;
}

std::string BroadcastEngine::manualHacia(const std::string& salida) const {
    const Salida* s = laSalida(salida);
    return motor_ && s != nullptr ? motor_->tbarHacia(s->id) : std::string();
}

bool BroadcastEngine::buildScene() {
    if (escena("programa") == nullptr && !createScene("programa")) return false;
    return setCurrentScene("programa");
}

// ============================================================================
// Fuentes
// ============================================================================

bool BroadcastEngine::ajustesMotor(const SourceSpec& spec, json& a) const {
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

namespace {
/// Una ruta de archivo de una fuente (imagen, video) RELATIVA es relativa a la carpeta del
/// evento: así una plantilla trae sus archivos (Media/bandera.gif) y un evento sigue
/// andando en un pendrive aunque cambie la letra de la unidad o la carpeta. Antes quedaba
/// relativa a la carpeta de trabajo del proceso, que la app no fija.
std::string rutaDelEvento(const std::string& r) {
    if (r.empty()) return r;
    const std::filesystem::path p(std::u8string(r.begin(), r.end()));
    if (p.is_absolute()) return r;
    const std::u8string u = (ba::platform::configRoot() / p).lexically_normal().u8string();
    return std::string(u.begin(), u.end());
}
} // namespace

bool BroadcastEngine::ajustesMotorTipo(const SourceSpec& spec, json& a) const {
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
        if (a.contains("local_file") && a["local_file"].is_string()) a["local_file"] = rutaDelEvento(a["local_file"]);
        return true;
    }
    if (id == "ffmpeg_source") {
        // Los archivos llegan acá ya como ba_gst_medio (migrarMedio); queda la red.
        if (in.value("is_local_file", true)) {
            a = in;
            a["medio"] = true;
            if (a.contains("local_file") && a["local_file"].is_string()) a["local_file"] = rutaDelEvento(a["local_file"]);
            return true;
        }
        a = {{"uri", in.value("input", std::string())}, {"nombre", spec.name}, {"audio", true}};
        if (in.contains("buffering_mb") || in.value("low_latency", false)) a["latencia"] = in.value("low_latency", false) ? 50 : 200;
        return true;
    }
    if (id == "color_source_v3" || id == "color_source") {
        a = {{"tipo", "color"},
             {"color", colorObs(in.value("color", json()), "#FFD1D1D1")},
             {"ancho", in.value("width", video_.baseWidth)},
             {"alto", in.value("height", video_.baseHeight)}};
        return true;
    }
    if (id == "text_ft2_source_v2" || id == "text_ft2_source" || id == "text_gdiplus" || id == "text_gdiplus_v2" ||
        id == "ba_texto") {
        const json f = in.value("font", json::object());
        const int px = std::max(8, f.value("size", 48));
        const int flags = f.value("flags", 0); // OBS: 1 negrita, 2 cursiva
        const int w = std::max(16, static_cast<int>(std::lround(spec.rect.w * video_.baseWidth)));
        const int h = std::max(16, static_cast<int>(std::lround(spec.rect.h * video_.baseHeight)));
        a = {{"tipo", "texto"},
             {"texto", in.value("text", std::string())},
             {"fuente", f.value("face", std::string("Sans"))},
             {"tamano", std::max(6, px * 3 / 4)}, // OBS da píxeles; pango, puntos
             {"negrita", (flags & 1) != 0 || id == "ba_texto"},
             {"color", colorObs(in.value("color1", in.value("color", json())), "#FFFFFFFF")},
             {"contorno", in.value("outline", false)},
             {"color_contorno", "#FF000000"},
             {"alineacion", "centro"},
             {"vertical", "centro"},
             {"ancho", w},
             {"alto", h}};
        return true;
    }
    if (id == "image_source") {
        a = {{"tipo", "imagen"}, {"archivo", rutaDelEvento(in.value("file", std::string()))}};
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
        a = t.motor(fuentes::conDefectos(id, in));
        a["nombre"] = spec.name;
        return true;
    }
    if (termina(id, "_output_capture")) {
        // El audio del sistema necesita un elemento propio por sistema (M0.5): todavía no.
        return false;
    }
    return false;
}

bool BroadcastEngine::crearFuente(const SourceSpec& pedida, bool global) {
    if (sources_.count(pedida.name) > 0) return true;
    // El motor separa con «:» la entrada de lo que sale de ella («Cámara:video»): un nombre
    // con «:» no se puede crear allá. Se dice acá, y no queda una fuente «agregada» y muerta.
    if (pedida.name.find(':') != std::string::npos) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "el nombre de una fuente no puede tener «:» («%1»): usá «—» o «-»"), {pedida.name}));
        return false;
    }
    // Lo viejo de OBS que ya tiene versión GStreamer (InputSpecs).
    SourceSpec spec = pedida;
    inputs::migrarMedio(spec);
    inputs::migrarVideoInvitado(spec);
    inputs::migrarNdi(spec);
    inputs::migrarCamara(spec);
    inputs::migrarMicrofono(spec);
    if (spec.id == "ba_guest_audio") {
        const json s = json::parse(spec.settingsJson, nullptr, false);
        spec.id = inputs::kMicType;
        spec.settingsJson = inputs::ajustesAudioAnillo("guest", s.is_object() ? s.value("slot", 1) : 1);
    }
    if (spec.id == kTipoBanca) {
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
        motor_->definirEscena(escenaCompositor(spec.name, configDe(spec.id, aj)));
        Fuente f;
        f.spec = spec;
        f.global = global;
        f.compProductor = motor_->usarEscena(kPrefijoCompositor + spec.name);
        sources_[spec.name] = f;
        ordenFuentes_.push_back(spec.name);
        return true;
    }
    json ajustes;
    if (!ajustesMotor(spec, ajustes)) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "fuente '%1': el tipo '%2' todavía no lo arma el motor"), {spec.name, spec.id}));
        return false;
    }
    json opciones{{"global", global}, {"nivel", nivelAplicado(spec.name, spec.volume)}, {"mute", spec.muted}};
    const std::string e = motor_->agregarEntrada(spec.name, ajustes, opciones);
    if (!e.empty()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "fuente '%1': %2"), {spec.name, e}));
        return false;
    }
    Fuente f;
    f.spec = spec;
    f.global = global;
    sources_[spec.name] = f;
    ordenFuentes_.push_back(spec.name);
    // En la mezcla de cada salida entra con el mismo volumen y mute (las opciones valen para todas).
    for (auto& [id, s] : salidas_)
        if (!s->porOmision()) {
            s->volumen[spec.name] = spec.volume;
            s->mute[spec.name] = spec.muted;
        }
    if (esDeApp(spec.id)) fuentes::alta(spec.name, spec.id, json::parse(spec.settingsJson, nullptr, false));
    // Una fuente nueva (o rehecha: una placa que cambió de canales): lo guardado de su
    // audio se le vuelve a poner (Matriz).
    if (cambioAudio_) cambioAudio_(spec.name, "alta");
    // Los compositores que la juntan (creados antes que ella, p. ej. al arrancar) se rearman.
    for (const auto& [n, c] : sources_) {
        if (c.compProductor.empty()) continue;
        const json aj = json::parse(c.spec.settingsJson.empty() ? "{}" : c.spec.settingsJson, nullptr, false);
        const json config = configDe(c.spec.id, aj);
        bool usa = false;
        if (config.is_object())
            for (const auto& k : config.value("capas", json::array())) usa = usa || k.value("fuente", std::string()) == spec.name;
        if (usa) motor_->definirEscena(escenaCompositor(n, config));
    }
    if (motor_) motor_->pedirFoto();
    return true;
}

void BroadcastEngine::soltarSiHuerfana(const std::string& nombre) {
    auto it = sources_.find(nombre);
    if (it == sources_.end() || it->second.global) return;
    for (const auto& e : scenes_)
        for (const auto& i : e.items)
            if (i.fuente == nombre) return;
    for (const auto& [id, s] : salidas_)
        for (const auto& d : s->dsk)
            if (std::find(d.contenido.begin(), d.contenido.end(), nombre) != d.contenido.end()) return;
    if (!it->second.compProductor.empty()) {
        motor_->soltarEscena(kPrefijoCompositor + nombre);
        motor_->quitarEscena(kPrefijoCompositor + nombre);
    } else if (!it->second.banca)
        motor_->quitarEntrada(nombre);
    fuentes::baja(nombre);
    sources_.erase(it);
    if (pfl_ == nombre) pfl_.clear();
    ordenFuentes_.erase(std::remove(ordenFuentes_.begin(), ordenFuentes_.end(), nombre), ordenFuentes_.end());
}

bool BroadcastEngine::addSource(const std::string& sceneName, const SourceSpec& spec) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "addSource: no existe la escena '%1'"), {sceneName}));
        return false;
    }
    if (spec.id.empty() || spec.name.empty()) {
        setError(ba::i18n::traducir("emision", "addSource: fuente sin id o sin nombre"));
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
    if (it.recorte[0] + it.recorte[1] + it.recorte[2] + it.recorte[3] > 0) recortesPendientes_.push_back({sceneName, spec.name});
    publicarEscena(*e);
    return true;
}

bool BroadcastEngine::addSource(const SourceSpec& spec) {
    if (currentScene().empty() && !buildScene()) return false;
    return addSource(currentScene(), spec);
}

bool BroadcastEngine::removeSource(const std::string& sceneName, const std::string& name) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) return false;
    const auto it = std::find_if(e->items.begin(), e->items.end(), [&](const Item& i) { return i.fuente == name; });
    if (it == e->items.end()) return false;
    e->items.erase(it);
    publicarEscena(*e);
    soltarSiHuerfana(name);
    return true;
}

bool BroadcastEngine::moveSource(const std::string& sceneName, const std::string& name, int delta) {
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

std::vector<std::string> BroadcastEngine::sceneSources(const std::string& sceneName) const {
    std::vector<std::string> out;
    if (const EscenaApp* e = escena(sceneName))
        for (const auto& i : e->items) out.push_back(i.fuente);
    return out;
}

bool BroadcastEngine::setSourceVisible(const std::string& sceneName, const std::string& name, bool visible) {
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

bool BroadcastEngine::sourceVisible(const std::string& sceneName, const std::string& name) const {
    if (const EscenaApp* e = escena(sceneName))
        for (const auto& i : e->items)
            if (i.fuente == name) return i.visible;
    return false;
}

bool BroadcastEngine::setSourceRect(const std::string& sceneName, const std::string& name, const CanvasRect& rect) {
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

bool BroadcastEngine::setSourceCrop(const std::string& sceneName, const std::string& name, float izquierda, float arriba,
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
            if (!sourceSize(name, w, h) || w == 0) recortesPendientes_.push_back({sceneName, name});
            return true;
        }
    return false;
}

bool BroadcastEngine::setSourceEfectos(const std::string& sceneName, const std::string& name, const EfectosCapa& efectos) {
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

EfectosCapa BroadcastEngine::sourceEfectos(const std::string& sceneName, const std::string& name) const {
    if (const EscenaApp* e = escena(sceneName))
        for (const auto& i : e->items)
            if (i.fuente == name) return i.efectos;
    return {};
}

void BroadcastEngine::aplicarRecortesPendientes() {
    if (recortesPendientes_.empty()) return;
    const auto pendientes = recortesPendientes_;
    recortesPendientes_.clear();
    if (motor_) motor_->pedirFoto();
    for (const auto& r : pendientes) {
        int w = 0, h = 0;
        if (!sourceSize(r.fuente, w, h) || w == 0) {
            recortesPendientes_.push_back(r); // sigue sin tamaño
            continue;
        }
        if (const EscenaApp* e = escena(r.escena)) publicarEscena(*e);
    }
}

bool BroadcastEngine::renameSource(const std::string& from, const std::string& to) {
    if (to.empty() || sources_.count(to) > 0) {
        setError(ba::i18n::traducir("emision", "renameSource: nombre vacio o repetido"));
        return false;
    }
    auto it = sources_.find(from);
    if (it == sources_.end()) return false;
    // En el motor el id es el nombre: se arma una entrada nueva con el nombre nuevo.
    Fuente f = it->second;
    f.spec.name = to;
    json ajustes;
    if (!ajustesMotor(f.spec, ajustes)) return false;
    const std::string e = motor_->agregarEntrada(
        to, ajustes, json{{"global", f.global}, {"nivel", nivelAplicado(to, f.spec.volume)}, {"mute", f.spec.muted}});
    if (!e.empty()) {
        setError(e);
        return false;
    }
    sources_.erase(it);
    sources_[to] = f;
    if (pfl_ == from) pfl_ = to;
    fuentes::baja(from);
    if (esDeApp(f.spec.id)) fuentes::alta(to, f.spec.id, json::parse(f.spec.settingsJson, nullptr, false));
    if (cambioAudio_) cambioAudio_(to, "alta");
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
    for (auto& [id, s] : salidas_) {
        for (auto& d : s->dsk) std::replace(d.contenido.begin(), d.contenido.end(), from, to);
        if (s->porOmision()) continue;
        if (auto v = s->volumen.extract(from)) {
            v.key() = to;
            motor_->ponerNivel(to, nivelAplicado(to, v.mapped()), id);
            s->volumen.insert(std::move(v));
        }
        if (auto m = s->mute.extract(from)) {
            m.key() = to;
            motor_->ponerMute(to, m.mapped(), id);
            s->mute.insert(std::move(m));
        }
    }
    motor_->quitarEntrada(from);
    if (!f.pistas.empty()) motor_->ponerPistas(to, f.pistas);
    return true;
}

json BroadcastEngine::estadoEntrada(const std::string& name) const {
    if (sources_.count(name) == 0) return nullptr;
    const json& e = seccion(estadoMotor(), "entradas");
    return e.contains(name) ? e[name] : json::object();
}

std::vector<std::pair<std::string, CanvasRect>> BroadcastEngine::itemsVisibles(const std::string& scene) const {
    std::vector<std::pair<std::string, CanvasRect>> v;
    if (const EscenaApp* e = escena(scene))
        for (const auto& i : e->items)
            if (i.visible) v.emplace_back(i.fuente, i.rect);
    return v;
}

bool BroadcastEngine::ponerReloj(const std::string& tipo, const std::string& servidor, int puerto, int dominio) {
    if (tipo == relojTipo_ && servidor == relojServidor_ && puerto == relojPuerto_ && dominio == relojDominio_) return true;
    relojTipo_ = tipo;
    relojServidor_ = servidor;
    relojPuerto_ = puerto;
    relojDominio_ = dominio;
    // Todo el estudio corre con ese reloj: se arma de nuevo con lo que había.
    return !started_ || resetVideo(video_);
}

std::string BroadcastEngine::publicarReloj(int puerto) {
    relojPublicar_ = std::max(0, puerto);
    return motor_ ? motor_->publicarReloj(relojPublicar_) : std::string();
}

std::string BroadcastEngine::controlEntrada(const std::string& name, const json& campos) {
    if (sources_.count(name) == 0) return ba::i18n::llenar(ba::i18n::traducir("emision", "no existe «%1»"), {name});
    return motor_ ? motor_->controlEntrada(name, campos) : ba::i18n::traducir("emision", "no hay motor");
}

std::string BroadcastEngine::sourceTypeId(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.spec.id : std::string();
}

void BroadcastEngine::holdShowing(const std::string&, bool) {}

bool BroadcastEngine::sourceSize(const std::string& name, int& width, int& height) const {
    width = height = 0;
    if (sources_.count(name) == 0) return false;
    const json& e = seccion(estadoMotor(), "entradas");
    if (!e.contains(name)) return true;
    const json sale = e[name].value("sale", json::object());
    width = sale.value("ancho", 0);
    height = sale.value("alto", 0);
    return true;
}

std::string BroadcastEngine::sourceSettingsJson(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.spec.settingsJson : std::string("{}");
}

bool BroadcastEngine::updateSourceSettings(const std::string& name, const std::string& settingsJson) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    const json nuevos = json::parse(settingsJson, nullptr, false);
    if (!nuevos.is_object()) {
        setError(ba::i18n::traducir("emision", "settings no son JSON valido"));
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
        motor_->definirEscena(escenaCompositor(name, configDe(it->second.spec.id, todo)));
        return true;
    }
    if (esDeApp(it->second.spec.id)) fuentes::alta(name, it->second.spec.id, todo);
    json ajustes;
    if (!ajustesMotor(it->second.spec, ajustes)) return false;
    if (it->second.syncMs != 0) ajustes["desfase_audio"] = it->second.syncMs;
    const std::string e = motor_->actualizarEntrada(name, ajustes);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    return true;
}

std::vector<SourceType> BroadcastEngine::inputTypes() const {
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
            {kTipoBanca, ba::i18n::traducir("emision", "Banca con la palabra (asamblea: la cámara de quien habla)"), true, false},
            {"ba_pdf", ba::i18n::traducir("emision", "PDF (una página por vez)"), true, false},
            {"ba_grilla", ba::i18n::traducir("emision", "Grilla de personas (la gente de la Conferencia y la Asamblea)"), true, false},
            {"ba_app", ba::i18n::traducir("emision", "Lo que pinta la app"), true, false}};
}

bool BroadcastEngine::sourceTypeExists(const std::string& typeId) const {
    if (typeId.empty()) return false;
    SourceSpec s;
    s.id = typeId;
    json a;
    return typeId == kTipoBanca || typeId == inputs::kMicType || typeId == inputs::kMedioType || typeId == "ba_guest_audio" ||
           typeId == "ba_guest_video" || typeId == "ba_ndi_input" || typeId == inputs::kVideoType ||
           typeId == inputs::kAudioInLibobs || ajustesMotor(s, a);
}

std::vector<BroadcastEngine::ListItem> BroadcastEngine::sourceTypeListItems(const std::string&, const std::string&,
                                                                           const std::string&) const {
    // Los dispositivos los listan video/Camaras y audio/EntradasAudio con el monitor de
    // dispositivos de GStreamer (este era el respaldo de libobs): no hay más que esto.
    return {};
}

// ============================================================================
// Invitados y entradas globales
// ============================================================================

bool BroadcastEngine::addGuestAudio(int slot) {
    const std::string name = "Invitado " + std::to_string(slot) + " (audio)";
    if (sources_.count(name) == 0) {
        SourceSpec spec;
        spec.id = inputs::kMicType;
        spec.name = name;
        spec.settingsJson = inputs::ajustesAudioAnillo("guest", slot);
        if (!crearFuente(spec, true)) return false;
    }
    // Lo que oye: de fábrica, el programa sin su propia voz.
    const auto r = retornos_.find(slot);
    motor_->retorno(slot, r != retornos_.end() ? r->second : "mix_minus", name);
    return true;
}

bool BroadcastEngine::addGlobalAudio(const SourceSpec& spec) {
    if (sources_.count(spec.name) == 0 && !crearFuente(spec, true)) return false;
    if (std::find(micInputs_.begin(), micInputs_.end(), spec.name) == micInputs_.end()) micInputs_.push_back(spec.name);
    return true;
}

int BroadcastEngine::adoptPendingAudio() { return 0; }

bool BroadcastEngine::removeGlobalAudio(const std::string& name) {
    const auto it = std::find(micInputs_.begin(), micInputs_.end(), name);
    if (it == micInputs_.end()) return false;
    micInputs_.erase(it);
    if (auto f = sources_.find(name); f != sources_.end()) f->second.global = false;
    soltarSiHuerfana(name);
    return true;
}

bool BroadcastEngine::addGuestVideo(int slot) { return addGuestVideo(slot, CanvasRect{0.5f, 0.5f, 0.5f, 0.5f}); }

bool BroadcastEngine::addGuestVideo(int slot, const CanvasRect& rect) {
    SourceSpec spec;
    spec.id = "ba_guest_video";
    spec.name = "Invitado " + std::to_string(slot);
    spec.settingsJson = "{\"slot\":" + std::to_string(slot) + "}";
    spec.rect = rect;
    return addSource(spec);
}

void BroadcastEngine::setRetornoInvitado(int slot, const std::string& modo) {
    retornos_[slot] = modo;
    const std::string name = "Invitado " + std::to_string(slot) + " (audio)";
    if (motor_ && sources_.count(name) > 0) motor_->retorno(slot, modo, name);
}

std::string BroadcastEngine::retornoInvitado(int slot) const {
    const auto it = retornos_.find(slot);
    return it != retornos_.end() ? it->second : "mix_minus";
}

// ============================================================================
// Audio por fuente
// ============================================================================

std::vector<std::string> BroadcastEngine::audioSources() const {
    std::vector<std::string> out;
    const json& e = seccion(estadoMotor(), "entradas");
    for (const auto& n : ordenFuentes_)
        if (e.contains(n) && e[n].value("audio", false)) out.push_back(n);
    return out;
}

std::vector<std::string> BroadcastEngine::sourceNames() const { return ordenFuentes_; }

bool BroadcastEngine::setSourceVolume(const std::string& name, float mul, const std::string& salida) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    const float v = std::max(0.0f, mul);
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    if (!s->porOmision()) {
        // La mezcla de otra salida: solo su fader en su bus (la fuente y su matriz son de la por omisión).
        s->volumen[name] = v;
        return motor_->ponerNivel(name, nivelAplicado(name, v), s->id).empty();
    }
    const bool cambia = it->second.spec.volume != v;
    it->second.spec.volume = v;
    const bool ok = motor_->ponerNivel(name, nivelAplicado(name, v)).empty();
    if (cambia && cambioAudio_) cambioAudio_(name, "volumen");
    return ok;
}

std::string BroadcastEngine::grupoDeAudio(const std::string& nombre) {
    std::string g;
    for (char c : nombre) {
        if (c >= '0' && c <= '9') continue;
        if (c == ' ' && (g.empty() || g.back() == ' ')) continue;
        g.push_back(c);
    }
    while (!g.empty() && g.back() == ' ') g.pop_back();
    // «Invitado  (audio)» queda con un espacio solo antes del paréntesis.
    for (std::size_t p = g.find(" )"); p != std::string::npos; p = g.find(" )")) g.erase(p, 1);
    return g;
}

float BroadcastEngine::nivelAplicado(const std::string& name, float volumen) const {
    if (gananciaGrupo_.empty()) return volumen;
    const auto it = gananciaGrupo_.find(grupoDeAudio(name));
    return it != gananciaGrupo_.end() ? volumen * it->second : volumen;
}

void BroadcastEngine::setGananciaGrupo(const std::string& grupo, float mul) {
    const float g = std::max(0.0f, mul);
    if (std::abs(g - gananciaGrupo(grupo)) < 1e-6f) return;
    if (std::abs(g - 1.0f) < 1e-6f) gananciaGrupo_.erase(grupo);
    else gananciaGrupo_[grupo] = g;
    for (auto& [nombre, f] : sources_) {
        if (grupoDeAudio(nombre) != grupo) continue;
        if (motor_) motor_->ponerNivel(nombre, nivelAplicado(nombre, f.spec.volume)); // sin audio no tiene nivel: no importa
        // El VCA es del grupo en todas las salidas (cada una con su fader).
        for (const auto& [id, s] : salidas_)
            if (motor_ && !s->porOmision()) motor_->ponerNivel(nombre, nivelAplicado(nombre, sourceVolume(nombre, id)), id);
        if (cambioAudio_) cambioAudio_(nombre, "volumen");
    }
}

float BroadcastEngine::gananciaGrupo(const std::string& grupo) const {
    const auto it = gananciaGrupo_.find(grupo);
    return it != gananciaGrupo_.end() ? it->second : 1.0f;
}

float BroadcastEngine::sourceVolume(const std::string& name, const std::string& salida) const {
    auto it = sources_.find(name);
    if (it == sources_.end()) return 0.0f;
    const Salida* s = laSalida(salida);
    if (s == nullptr || s->porOmision()) return it->second.spec.volume;
    const auto v = s->volumen.find(name);
    return v != s->volumen.end() ? v->second : it->second.spec.volume;
}

bool BroadcastEngine::setSourceMuted(const std::string& name, bool muted, const std::string& salida) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    Salida* s = laSalida(salida);
    if (s == nullptr) return false;
    if (!s->porOmision()) {
        s->mute[name] = muted;
        return motor_->ponerMute(name, muted, s->id).empty();
    }
    const bool cambia = it->second.spec.muted != muted;
    it->second.spec.muted = muted;
    const bool ok = motor_->ponerMute(name, muted).empty();
    if (cambia && cambioAudio_) cambioAudio_(name, "mute");
    return ok;
}

bool BroadcastEngine::sourceMuted(const std::string& name, const std::string& salida) const {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    const Salida* s = laSalida(salida);
    if (s == nullptr || s->porOmision()) return it->second.spec.muted;
    const auto m = s->mute.find(name);
    return m != s->mute.end() ? m->second : it->second.spec.muted;
}

float BroadcastEngine::sourcePeakDb(const std::string& name) const {
    // Lo que entrega la entrada (después de su rack, vaya a donde vaya), como los
    // vúmetros del Mezclador. El del bus Programa marcaba -100 con audio si la
    // fuente no iba al Programa (un micrófono que sale solo por un parlante).
    if (motor_)
        if (const auto niveles = motor_->niveles())
            for (const auto& n : *niveles)
                if (!n.bus && n.id == name) return std::max(-100.0f, std::max(n.pico[0], n.pico[1]));
    const json& e = seccion(seccion(estadoMotor(), "programa"), "entradas");
    if (!e.contains(name)) return -100.0f;
    return static_cast<float>(std::max(-100.0, e[name].value("pico_db", -100.0)));
}

float BroadcastEngine::sourceOutputPeakDb(const std::string& name) const {
    const json& es = seccion(estadoMotor(), "entradas");
    if (!es.is_object() || !es.contains(name)) return -100.0f;
    return static_cast<float>(std::max(-100.0, es[name].value("pico_audio_db", -100.0)));
}

bool BroadcastEngine::setSourceMonitoring(const std::string& name, bool on) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    // La escucha fija: las marcadas suenan juntas. Si además está en PFL, sigue sonando.
    const bool cambia = it->second.monitor != on;
    it->second.monitor = on;
    const std::string e = motor_->ponerEscucha(name, on || pfl_ == name);
    if (!e.empty()) setError(e);
    if (cambia && cambioAudio_) cambioAudio_(name, "escucha");
    return e.empty();
}

bool BroadcastEngine::sourceMonitoring(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() && (it->second.monitor || pfl_ == name);
}

bool BroadcastEngine::sourceEscuchaFija(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() && it->second.monitor;
}

bool BroadcastEngine::setPfl(const std::string& name) {
    if (name == pfl_) return true;
    if (!name.empty() && sources_.count(name) == 0) return false;
    const std::string antes = pfl_;
    pfl_ = name;
    // El que estaba en PFL vuelve a lo que diga su escucha fija; el nuevo suena.
    if (auto it = sources_.find(antes); it != sources_.end()) {
        const std::string e = motor_->ponerEscucha(antes, it->second.monitor);
        if (!e.empty()) setError(e);
    }
    bool ok = true;
    if (!name.empty()) {
        const std::string e = motor_->ponerEscucha(name, true);
        if (!e.empty()) {
            setError(e);
            ok = false;
        }
    }
    if (cambioAudio_) {
        if (!antes.empty()) cambioAudio_(antes, "escucha");
        if (!name.empty()) cambioAudio_(name, "escucha");
    }
    return ok;
}

bool BroadcastEngine::setSourceSyncOffset(const std::string& name, int ms) {
    auto it = sources_.find(name);
    if (it == sources_.end()) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente '%1'"), {name}));
        return false;
    }
    const int v = std::clamp(ms, -5000, 5000);
    const bool cambia = it->second.syncMs != v;
    it->second.syncMs = v;
    json ajustes;
    if (!ajustesMotor(it->second.spec, ajustes)) return false;
    ajustes["desfase_audio"] = it->second.syncMs; // solo el audio; cambia andando (el offset del pad)
    const bool ok = motor_->actualizarEntrada(name, ajustes).empty();
    if (cambia && cambioAudio_) cambioAudio_(name, "retardo");
    return ok;
}

int BroadcastEngine::sourceSyncOffset(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.syncMs : 0;
}

bool BroadcastEngine::setSourcePistas(const std::string& name, const std::vector<int>& pistas) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    const std::string e = motor_->ponerPistas(name, pistas);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    const bool cambia = it->second.pistas != pistas;
    it->second.pistas = pistas;
    if (cambia && cambioAudio_) cambioAudio_(name, "pistas");
    return true;
}

std::vector<int> BroadcastEngine::sourcePistas(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.pistas : std::vector<int>{};
}

// ============================================================================
// Medios
// ============================================================================

bool BroadcastEngine::playMedia(const std::string& name, const std::string& file, bool loop, float volume) {
    if (file.empty()) {
        setError(ba::i18n::traducir("emision", "no se dijo que archivo reproducir"));
        return false;
    }
    const json s{{"local_file", file}, {"is_local_file", true}, {"looping", loop}, {"clear_on_media_end", true}};
    if (sources_.count(name) > 0) {
        // Ya existe: se le cambia el archivo y arranca de nuevo (otra fuente dejaría la anterior sonando).
        updateSourceSettings(name, s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        setSourceVolume(name, volume);
        setSourceMuted(name, false);
        return restartMedia(name);
    }
    SourceSpec spec;
    spec.id = inputs::kMedioType;
    spec.name = name;
    spec.settingsJson = s.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    spec.volume = volume;
    return addGlobalAudio(spec);
}

bool BroadcastEngine::restartMedia(const std::string& name) {
    if (sources_.count(name) == 0) {
        setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente de medios \"%1\""), {name}));
        return false;
    }
    const std::string e = motor_->controlMedio(name, "reiniciar");
    if (!e.empty()) setError(e);
    return e.empty();
}

bool BroadcastEngine::seekMedia(const std::string& name, std::int64_t ms) {
    if (sources_.count(name) == 0) return false;
    return motor_->controlMedio(name, "ir", std::max<std::int64_t>(0, ms)).empty();
}

bool BroadcastEngine::pararMedia(const std::string& name) {
    if (sources_.count(name) == 0) return false;
    return motor_->controlMedio(name, "parar").empty();
}

bool BroadcastEngine::pausarMedia(const std::string& name, bool pausa) {
    if (sources_.count(name) == 0) return false;
    return motor_->controlMedio(name, pausa ? "pausa" : "play").empty();
}

bool BroadcastEngine::stopMedia(const std::string& name) {
    if (sources_.count(name) == 0) return false;
    motor_->controlMedio(name, "parar");
    return removeGlobalAudio(name);
}

std::vector<BroadcastEngine::MediaInfo> BroadcastEngine::mediaSources() const {
    std::vector<MediaInfo> out;
    if (!motor_) return out;
    for (const auto& n : ordenFuentes_) {
        const json m = motor_->medio(n);
        if (m.is_null()) continue;
        out.push_back({n, m.value("estado", std::string("none")), m.value("duracion_ms", std::int64_t{0}),
                       m.value("tiempo_ms", std::int64_t{0})});
    }
    return out;
}

// ============================================================================
// Capacidades
// ============================================================================

std::string BroadcastEngine::graphicsDeviceName() const {
#if defined(__APPLE__)
    return "Apple (VideoToolbox, GStreamer)";
#else
    return "GStreamer";
#endif
}

std::vector<BroadcastEngine::EncoderInfo> BroadcastEngine::videoEncoders() const {
    std::vector<EncoderInfo> out;
    for (const char* f : {"vtenc_h264_hw", "vtenc_h264", "nvh264enc", "qsvh264enc", "d3d12h264enc", "amfh264enc", "vah264enc",
                          "mfh264enc", "x264enc"}) {
        if (!motor::hayFabrica(f)) continue;
        const std::string id = f;
        out.push_back({id, id, "h264", id != "x264enc"});
    }
    return out;
}

bool BroadcastEngine::outputsActive() const {
    // Lo pedido (las salidas son del hilo de salidas: ver enSalidas), en todas las salidas.
    std::lock_guard<std::mutex> l(pedidosM_);
    for (const auto& [id, s] : salidas_)
        if (s->streamPedido || s->grabacionPedida || !s->extrasPedidos.empty()) return true;
    return false;
}

bool BroadcastEngine::resetEncoders() {
    if (outputsActive()) {
        setError(ba::i18n::traducir("emision", "no se puede cambiar la codificacion con salidas activas"));
        return false;
    }
    return true;
}

std::vector<std::string> BroadcastEngine::videoEncoderCandidates(const EncoderConfig& enc) const {
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

bool BroadcastEngine::resetVideo(const VideoConfig& video) {
    if (!started_) return false;
    if (outputsActive()) {
        setError(ba::i18n::traducir("emision", "no se puede cambiar la resolucion con salidas activas"));
        return false;
    }
    // El lienzo es de todo el motor: se arma uno nuevo con todo lo que había.
    const auto escenas = scenes_;
    const auto fuentes = sources_;
    const auto orden = ordenFuentes_;
    const auto mics = micInputs_;
    // Las salidas: lo que tenía al aire y armado cada una, y la mezcla de las que no
    // son la por omisión (las capas, como siempre, vuelven vacías).
    struct Antes {
        std::string id, nombre, alAire, armada;
        std::map<std::string, float> volumen;
        std::map<std::string, bool> mute;
    };
    std::vector<Antes> salidas;
    for (const auto& s : lasSalidas()) salidas.push_back({s->id, s->nombre, s->alAire, s->armada, s->volumen, s->mute});
    const bool feeds = returnFeedsActive_;
    const bool preview = previewActive_;
    stopReturnFeeds();
    disablePreview();
    shutdown();
    if (!startup(paths_, video, audio_)) return false;
    for (const auto& n : orden) {
        const auto& f = fuentes.at(n);
        crearFuente(f.spec, f.global);
        if (f.syncMs != 0) setSourceSyncOffset(n, f.syncMs);
        if (f.pistas != std::vector<int>{1}) setSourcePistas(n, f.pistas);
        if (f.monitor) setSourceMonitoring(n, true);
    }
    // El PFL que estaba sonando sigue sonando en el motor nuevo.
    if (!pfl_.empty() && sources_.count(pfl_) > 0) motor_->ponerEscucha(pfl_, true);
    micInputs_ = mics;
    scenes_ = escenas;
    for (const auto& e : scenes_) publicarEscena(e);
    for (const Antes& a : salidas) {
        if (a.id != kSalidaPorOmision && !crearSalida(a.id, a.nombre)) continue;
        if (a.id != kSalidaPorOmision) {
            for (const auto& [n, v] : a.volumen) setSourceVolume(n, v, a.id);
            for (const auto& [n, m] : a.mute) setSourceMuted(n, m, a.id);
        }
        if (!a.alAire.empty()) setCurrentSceneCon(a.alAire, "cut", 0, a.id);
        if (!a.armada.empty()) armar(a.armada, a.id);
        publicarCapas(*laSalida(a.id), 0);
    }
    for (const auto& [slot, modo] : retornos_) setRetornoInvitado(slot, modo);
    if (feeds) publishReturnFeeds();
    if (preview) enablePreview(video.outputWidth / 2, video.outputHeight / 2);
    return true;
}

// ============================================================================
// Video en vivo dentro de un gráfico (graficos/VideoEnGrafico.hpp)
//
// La cámara que entra con el zócalo es una capa más de la escena del gráfico, con
// claves que el motor interpola cuadro a cuadro (Composicion: Capa::claves). No toca
// el modelo de la escena (no se guarda, no se ve en el editor de escenas): se suma al
// traducirla para el motor, y al sacarla la escena vuelve a ser la de siempre.
// ============================================================================

std::string BroadcastEngine::ponerVideoDeGrafico(const std::string& grafico, const json& capas) {
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

void BroadcastEngine::empezarVideoDeGrafico(const std::string& grafico, std::int64_t desdeNs) {
    const auto it = videoDeGraficos_.find(grafico);
    if (it == videoDeGraficos_.end()) return;
    it->second["desde_ns"] = desdeNs;
    publicarEscenasCon(grafico);
}

void BroadcastEngine::quitarVideoDeGrafico(const std::string& grafico) {
    if (videoDeGraficos_.erase(grafico) > 0) publicarEscenasCon(grafico);
}

void BroadcastEngine::publicarEscenasCon(const std::string& fuente) {
    for (const EscenaApp& e : scenes_)
        for (const Item& i : e.items)
            if (i.fuente == fuente) {
                publicarEscena(e);
                break;
            }
}

void BroadcastEngine::sumarVideoDeGraficos(const EscenaApp& e, json& capas) const {
    if (videoDeGraficos_.empty()) return;
    const double W = video_.baseWidth, H = video_.baseHeight;
    const json& entradas = seccion(estadoMotor(), "entradas");
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

} // namespace ba::app
