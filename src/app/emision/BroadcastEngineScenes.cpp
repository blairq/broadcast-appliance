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
#include "emision/FuentesApp.hpp"
#include "emision/InputSpecs.hpp"

#include "Motor.hpp"
#include "Salidas.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace ba::app {

using json = nlohmann::json;

namespace {


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
    return id == "screen_capture" || id == "display_capture" || id == "macos-capture" || id == "capture_source" ||
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

/// Lo que pinta la app (QImage → cuadros al motor).
bool esDeApp(const std::string& id) {
    return id == "ba_subtitulos" || id == "ba_chat_overlay" || id == "ba_grafico" || id == "ba_app" ||
           id == "ba_video_compositor" || id == "ba_placeholder";
}

} // namespace

// ============================================================================
// Efectos de una capa (transparencia, borde difuso, sombra)
// ============================================================================

bool EfectosCapa::aplicar(const json& j, std::string* error) {
    if (!j.is_object()) {
        if (error) *error = "efectos: tiene que ser un objeto";
        return false;
    }
    const auto num = [&](const json& o, const char* k, float& v, float mn, float mx) {
        if (!o.contains(k)) return true;
        if (!o[k].is_number()) {
            if (error) *error = std::string("efectos: «") + k + "» tiene que ser un número";
            return false;
        }
        v = std::clamp(o[k].get<float>(), mn, mx);
        return true;
    };
    if (!num(j, "opacidad", opacidad, 0, 1) || !num(j, "borde", borde, 0, 1000)) return false;
    if (j.contains("sombra")) {
        const json& s = j["sombra"];
        if (s.is_boolean()) {
            sombra = s.get<bool>();
            return true;
        }
        if (!s.is_object()) {
            if (error) *error = "efectos.sombra: tiene que ser un objeto (o true/false)";
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
            {"sombra",
             {{"activa", sombra}, {"color", color}, {"alfa", alfa}, {"difuminado", difuminado}, {"distancia", distancia},
              {"angulo", angulo}, {"tamano", tamano}}}};
}

json EfectosCapa::sombraMotor() const {
    if (!sombra && borde <= 0) return nullptr;
    return {{"alfa", sombra ? alfa : 0.0f}, {"color", color},          {"difuminado", difuminado}, {"distancia", distancia},
            {"angulo", angulo},             {"tamano", tamano},        {"borde", borde}};
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

const json& BroadcastEngine::estadoMotor() const {
    // La foto que publica el bucle del motor: leerla no espera el candado (ver
    // Motor::instantanea). Se guarda la copia para devolver una referencia que
    // no cambie mientras el que llama la recorre.
    if (motor_) {
        if (auto f = motor_->instantanea(); f && f.get() != estadoFoto_.get()) {
            estadoFoto_ = f;
            estadoCache_ = *f;
        }
    }
    return estadoCache_;
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
    return e;
}

json BroadcastEngine::escenaMotor(const EscenaApp& e) const {
    const double W = video_.baseWidth, H = video_.baseHeight;
    const json& entradas = estadoMotor().value("entradas", json::object());
    json capas = json::array();
    for (std::size_t i = 0; i < e.items.size(); ++i) {
        const Item& it = e.items[i];
        const auto fu = sources_.find(it.fuente);
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
    return {{"nombre", e.nombre}, {"capas", capas}};
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
        setError("createScene: sin motor, nombre vacio o repetido: '" + name + "'");
        return false;
    }
    scenes_.push_back(EscenaApp{name, {}});
    publicarEscena(scenes_.back());
    return true;
}

bool BroadcastEngine::removeScene(const std::string& name) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(), [&](const EscenaApp& e) { return e.nombre == name; });
    if (it == scenes_.end() || scenes_.size() == 1) {
        setError("removeScene: no existe o es la unica escena");
        return false;
    }
    if (currentScene_ == name) {
        const std::string other = (it == scenes_.begin()) ? (it + 1)->nombre : scenes_.front().nombre;
        setCurrentSceneCon(other, "cut", 0);
    }
    const std::vector<Item> items = it->items;
    scenes_.erase(it);
    motor_->quitarEscena(name);
    for (const auto& item : items) soltarSiHuerfana(item.fuente);
    return true;
}

bool BroadcastEngine::renameScene(const std::string& from, const std::string& to) {
    if (to.empty() || escena(to) != nullptr) {
        setError("renameScene: nombre vacio o repetido");
        return false;
    }
    EscenaApp* e = escena(from);
    if (e == nullptr) return false;
    const bool alAire = currentScene_ == from;
    e->nombre = to;
    publicarEscena(*e);
    if (alAire) {
        currentScene_ = to;
        motor_->alAire(to, "corte", 0);
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

bool BroadcastEngine::setCurrentSceneCon(const std::string& name, const std::string& tipo, int ms) {
    if (escena(name) == nullptr) {
        setError("no existe la escena '" + name + "'");
        return false;
    }
    const int dur = ms < 0 ? transitionMs_ : ms;
    const std::string t = currentScene_.empty() || dur == 0 ? "corte" : transicionMotor(tipo.empty() ? transitionType_ : tipo);
    const std::string e = motor_->alAire(name, t, dur);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    currentScene_ = name;
    return true;
}

bool BroadcastEngine::setCurrentScene(const std::string& name) { return setCurrentSceneCon(name, transitionType_, -1); }

bool BroadcastEngine::transicionManual(const std::string& name, float t, bool soltar, bool* alAire) {
    if (alAire != nullptr) *alAire = false;
    if (escena(name) == nullptr) {
        setError("no existe la escena '" + name + "'");
        return false;
    }
    if (name == currentScene_) {
        setError("esa escena ya está al aire");
        return false;
    }
    std::string e;
    const bool quedo = motor_->tbar(name, t, soltar, &e);
    if (!e.empty()) {
        setError(e);
        return false;
    }
    if (quedo) {
        currentScene_ = name;
        if (alAire != nullptr) *alAire = true;
    }
    return true;
}

std::string BroadcastEngine::manualHacia() const { return motor_ ? motor_->tbarHacia() : std::string(); }

bool BroadcastEngine::buildScene() {
    if (escena("programa") == nullptr && !createScene("programa")) return false;
    return setCurrentScene("programa");
}

// ============================================================================
// Fuentes
// ============================================================================

bool BroadcastEngine::ajustesMotor(const SourceSpec& spec, json& a) const {
    if (!ajustesMotorTipo(spec, a)) return false;
    // Los filtros de la cadena (las tarjetas del grafo, el PTZ digital...) y el
    // rack de audio valen para cualquier tipo.
    const json s = json::parse(spec.settingsJson.empty() ? "{}" : spec.settingsJson, nullptr, false);
    if (s.is_object() && a.is_object())
        for (const char* k : {"filtros", "filtros_audio", "rack", "desfase", "desfase_video", "color_real", "desentrelazar", "campo"})
            if (s.contains(k) && !a.contains(k)) a[k] = s[k];
    return true;
}

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
        return true;
    }
    if (id == "ffmpeg_source") {
        // Los archivos llegan acá ya como ba_gst_medio (migrarMedio); queda la red.
        if (in.value("is_local_file", true)) {
            a = in;
            a["medio"] = true;
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
        a = {{"tipo", "imagen"}, {"archivo", in.value("file", std::string())}};
        return true;
    }
    if (esPantalla(id)) {
        a = {{"tipo", "pantalla"}, {"indice", in.value("display", in.value("monitor", 0))}, {"cursor", in.value("show_cursor", true)}};
        return true;
    }
    if (esDeApp(id)) {
        a = {{"tipo", "app"}, {"nombre", spec.name}};
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
        setError("fuente '" + spec.name + "': el tipo '" + spec.id + "' todavía no lo arma el motor");
        return false;
    }
    json opciones{{"global", global}, {"nivel", spec.volume}, {"mute", spec.muted}};
    const std::string e = motor_->agregarEntrada(spec.name, ajustes, opciones);
    if (!e.empty()) {
        setError("fuente '" + spec.name + "': " + e);
        return false;
    }
    Fuente f;
    f.spec = spec;
    f.global = global;
    sources_[spec.name] = f;
    ordenFuentes_.push_back(spec.name);
    if (esDeApp(spec.id)) fuentes::alta(spec.name, spec.id, json::parse(spec.settingsJson, nullptr, false));
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
    for (const auto& d : dsk_)
        if (std::find(d.contenido.begin(), d.contenido.end(), nombre) != d.contenido.end()) return;
    if (!it->second.compProductor.empty()) {
        motor_->soltarEscena(kPrefijoCompositor + nombre);
        motor_->quitarEscena(kPrefijoCompositor + nombre);
    } else
        motor_->quitarEntrada(nombre);
    fuentes::baja(nombre);
    sources_.erase(it);
    ordenFuentes_.erase(std::remove(ordenFuentes_.begin(), ordenFuentes_.end(), nombre), ordenFuentes_.end());
}

bool BroadcastEngine::addSource(const std::string& sceneName, const SourceSpec& spec) {
    EscenaApp* e = escena(sceneName);
    if (e == nullptr) {
        setError("addSource: no existe la escena '" + sceneName + "'");
        return false;
    }
    if (spec.id.empty() || spec.name.empty()) {
        setError("addSource: fuente sin id o sin nombre");
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
    if (currentScene_.empty() && !buildScene()) return false;
    return addSource(currentScene_, spec);
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
        setError("renameSource: nombre vacio o repetido");
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
        to, ajustes, json{{"global", f.global}, {"nivel", f.spec.volume}, {"mute", f.spec.muted}});
    if (!e.empty()) {
        setError(e);
        return false;
    }
    sources_.erase(it);
    sources_[to] = f;
    fuentes::baja(from);
    if (esDeApp(f.spec.id)) fuentes::alta(to, f.spec.id, json::parse(f.spec.settingsJson, nullptr, false));
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
    for (auto& d : dsk_) std::replace(d.contenido.begin(), d.contenido.end(), from, to);
    motor_->quitarEntrada(from);
    if (!f.pistas.empty()) motor_->ponerPistas(to, f.pistas);
    return true;
}

json BroadcastEngine::estadoEntrada(const std::string& name) const {
    if (sources_.count(name) == 0) return nullptr;
    const json& e = estadoMotor().value("entradas", json::object());
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
    if (sources_.count(name) == 0) return "no existe «" + name + "»";
    return motor_ ? motor_->controlEntrada(name, campos) : std::string("no hay motor");
}

std::string BroadcastEngine::sourceTypeId(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.spec.id : std::string();
}

void BroadcastEngine::holdShowing(const std::string&, bool) {}

bool BroadcastEngine::sourceSize(const std::string& name, int& width, int& height) const {
    width = height = 0;
    if (sources_.count(name) == 0) return false;
    const json& e = estadoMotor().value("entradas", json::object());
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
        setError("settings no son JSON valido");
        return false;
    }
    // Como OBS: lo nuevo se suma a lo que había.
    json todo = json::parse(it->second.spec.settingsJson.empty() ? "{}" : it->second.spec.settingsJson, nullptr, false);
    if (!todo.is_object()) todo = json::object();
    todo.update(nuevos);
    it->second.spec.settingsJson = todo.dump();
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
    return {{inputs::kVideoType, "Cámara o capturadora (USB, HDMI)", true, true},
            {inputs::kMicType, "Entrada por GStreamer (pipeline propio)", true, true},
            {inputs::kMedioType, "Archivo de video o audio", true, true},
            {"ffmpeg_source", "Video de red (URL)", true, true},
            {"color_source_v3", "Color", true, false},
            {"text_ft2_source_v2", "Texto", true, false},
            {"image_source", "Imagen", true, false},
            {"screen_capture", "Captura de pantalla", true, false},
            {"ba_app", "Lo que pinta la app", true, false}};
}

bool BroadcastEngine::sourceTypeExists(const std::string& typeId) const {
    if (typeId.empty()) return false;
    SourceSpec s;
    s.id = typeId;
    json a;
    return typeId == inputs::kMicType || typeId == inputs::kMedioType || typeId == "ba_guest_audio" ||
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
    const json& e = estadoMotor().value("entradas", json::object());
    for (const auto& n : ordenFuentes_)
        if (e.contains(n) && e[n].value("audio", false)) out.push_back(n);
    return out;
}

std::vector<std::string> BroadcastEngine::sourceNames() const { return ordenFuentes_; }

bool BroadcastEngine::setSourceVolume(const std::string& name, float mul) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    it->second.spec.volume = std::max(0.0f, mul);
    return motor_->ponerNivel(name, it->second.spec.volume).empty();
}

float BroadcastEngine::sourceVolume(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() ? it->second.spec.volume : 0.0f;
}

bool BroadcastEngine::setSourceMuted(const std::string& name, bool muted) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    it->second.spec.muted = muted;
    return motor_->ponerMute(name, muted).empty();
}

bool BroadcastEngine::sourceMuted(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() && it->second.spec.muted;
}

float BroadcastEngine::sourcePeakDb(const std::string& name) const {
    const json& p = estadoMotor().value("programa", json::object());
    const json e = p.is_object() ? p.value("entradas", json::object()) : json::object();
    if (!e.contains(name)) return -100.0f;
    return static_cast<float>(std::max(-100.0, e[name].value("pico_db", -100.0)));
}

bool BroadcastEngine::setSourceMonitoring(const std::string& name, bool on) {
    auto it = sources_.find(name);
    if (it == sources_.end()) return false;
    // Las marcadas suenan juntas en los auriculares (la interfaz hace el PFL
    // exclusivo apagando las demás).
    it->second.monitor = on;
    const std::string e = motor_->ponerEscucha(name, on);
    if (!e.empty()) setError(e);
    return e.empty();
}

bool BroadcastEngine::sourceMonitoring(const std::string& name) const {
    auto it = sources_.find(name);
    return it != sources_.end() && it->second.monitor;
}

bool BroadcastEngine::setSourceSyncOffset(const std::string& name, int ms) {
    auto it = sources_.find(name);
    if (it == sources_.end()) {
        setError("no existe la fuente '" + name + "'");
        return false;
    }
    it->second.syncMs = std::clamp(ms, -5000, 5000);
    json ajustes;
    if (!ajustesMotor(it->second.spec, ajustes)) return false;
    ajustes["desfase_audio"] = it->second.syncMs; // solo el audio; cambia andando (el offset del pad)
    return motor_->actualizarEntrada(name, ajustes).empty();
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
    it->second.pistas = pistas;
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
        setError("no se dijo que archivo reproducir");
        return false;
    }
    const json s{{"local_file", file}, {"is_local_file", true}, {"looping", loop}, {"clear_on_media_end", true}};
    if (sources_.count(name) > 0) {
        // Ya existe: se le cambia el archivo y arranca de nuevo (otra fuente dejaría la anterior sonando).
        updateSourceSettings(name, s.dump());
        setSourceVolume(name, volume);
        setSourceMuted(name, false);
        return restartMedia(name);
    }
    SourceSpec spec;
    spec.id = inputs::kMedioType;
    spec.name = name;
    spec.settingsJson = s.dump();
    spec.volume = volume;
    return addGlobalAudio(spec);
}

bool BroadcastEngine::restartMedia(const std::string& name) {
    if (sources_.count(name) == 0) {
        setError("no existe la fuente de medios \"" + name + "\"");
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
    // Lo pedido (las salidas son del hilo de salidas: ver enSalidas).
    std::lock_guard<std::mutex> l(pedidosM_);
    return streamPedido_ || grabacionPedida_ || !extrasPedidos_.empty();
}

bool BroadcastEngine::resetEncoders() {
    if (outputsActive()) {
        setError("no se puede cambiar la codificacion con salidas activas");
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
        setError("no se puede cambiar la resolucion con salidas activas");
        return false;
    }
    // El lienzo es de todo el motor: se arma uno nuevo con todo lo que había.
    const auto escenas = scenes_;
    const auto fuentes = sources_;
    const auto orden = ordenFuentes_;
    const auto mics = micInputs_;
    const std::string alAire = currentScene_;
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
    micInputs_ = mics;
    scenes_ = escenas;
    for (const auto& e : scenes_) publicarEscena(e);
    if (!alAire.empty()) setCurrentSceneCon(alAire, "cut", 0);
    publicarCapas(0);
    for (const auto& [slot, modo] : retornos_) setRetornoInvitado(slot, modo);
    if (feeds) publishReturnFeeds();
    if (preview) enablePreview(video.outputWidth / 2, video.outputHeight / 2);
    return true;
}

} // namespace ba::app
