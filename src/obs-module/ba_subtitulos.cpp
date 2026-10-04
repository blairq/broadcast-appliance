// ============================================================================
// src/obs-module/ba_subtitulos.cpp — Fuente «Subtítulos» (ba_subtitulos)
//
// Proposito:
//   Un cuadro de la escena que muestra, como subtítulos de televisión, lo que
//   llega por una SALIDA DE TEXTO de la app (la transcripción de «Voz a texto»
//   o su traducción: agent/FlujosTexto, agent/Traductor). La app le pasa cada
//   frase con el proc `agregar(texto)` (agent/Subtitulos.cpp); acá se:
//     - parte en líneas de `caracteres` como máximo, sin cortar palabras, y
//       en bloques de `lineas` líneas (lo que entra en pantalla a la vez);
//     - muestra cada bloque el tiempo que lleva leerlo (`cps` caracteres por
//       segundo), nunca menos de `minimo` segundos; si se acumulan bloques
//       (habla rápido), los que esperan se aceleran para no quedar atrás;
//     - borra el cuadro `borrar` segundos después del último.
//   Cada línea va centrada, con su propio fondo (como en la tele), dentro de
//   un cuadro de tamaño FIJO (`ancho` x lo que ocupan `lineas` líneas): así se
//   ubica en la escena una vez y no salta cuando cambia el texto. Las líneas
//   se apoyan abajo.
//
//   El texto lo dibuja la fuente de texto de libobs (text_ft2_source_v2), una
//   privada por línea.
//
// Modelo de threading:
//   `agregar` lo llama la app desde su hilo; `video_tick` y `video_render`
//   corren en el hilo gráfico. La cola va bajo un mutex; las fuentes hijas se
//   crean y actualizan en el tick (hilo gráfico) para no pelear con el render.
// ============================================================================

#include <graphics/vec4.h>
#include <obs-module.h>
#include <obs.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr int kMaxLineas = 4;

struct Subs {
    obs_source_t* ctx = nullptr;
    obs_source_t* lineas[kMaxLineas] = {};

    // Ajustes (los escribe update, los lee tick/render: bajo mu).
    std::mutex mu;
    int nLineas = 2, caracteres = 42, ancho = 1600, tamano = 56;
    double minimo = 1.5, cps = 17, borrar = 4;
    bool fondo = true, contorno = false, sombra = true;
    uint32_t colorTexto = 0xFFFFFFFF, colorFondo = 0xB4000000;
    obs_data_t* font = nullptr;

    // Cola de bloques y lo que se muestra.
    std::deque<std::vector<std::string>> cola;
    std::vector<std::string> actual;
    double restante = 0; ///< lo que le queda al bloque actual
    double vacio = 0;    ///< desde que terminó el último (para borrar)
    bool cambio = true;  ///< hay que pasarles el texto/estilo a las hijas
};

const char* nombre(void*) { return obs_module_text("Subtítulos"); }

// --- Partir en líneas y bloques ------------------------------------------------

/// Largo en caracteres (UTF-8: cuenta los que no son de continuación).
size_t largo(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        n += (c & 0xC0) != 0x80;
    return n;
}

/// Palabras separadas por espacios; el chino (sin espacios) se corta por caracteres.
std::vector<std::string> palabras(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        const std::string ch = s.substr(i, n);
        i += n;
        if (ch == " " || ch == "\n" || ch == "\t") {
            if (!w.empty()) out.push_back(w), w.clear();
        } else if (n == 3 && c >= 0xE3 && c <= 0xE9) {
            // CJK: cada ideograma es una «palabra» (se puede cortar entre ellos).
            if (!w.empty()) out.push_back(w), w.clear();
            out.push_back(ch);
        } else {
            w += ch;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::vector<std::vector<std::string>> bloques(const std::string& texto, size_t caracteres, size_t nLineas) {
    std::vector<std::string> lineas;
    std::string l;
    for (const auto& p : palabras(texto)) {
        const bool cjk = largo(p) == 1 && p.size() == 3;
        const std::string sep = l.empty() || cjk ? "" : " ";
        if (!l.empty() && largo(l) + largo(sep) + largo(p) > caracteres) {
            lineas.push_back(l);
            l.clear();
        }
        l += (l.empty() ? "" : sep) + p;
    }
    if (!l.empty()) lineas.push_back(l);
    // Bloques de nLineas; si el último quedaría con una sola línea corta y el
    // anterior lleno, se reparte mejor (2 + 1 se ve peor que pasar una línea).
    std::vector<std::vector<std::string>> out;
    for (size_t i = 0; i < lineas.size(); i += nLineas)
        out.emplace_back(lineas.begin() + static_cast<long>(i),
                         lineas.begin() + static_cast<long>(std::min(lineas.size(), i + nLineas)));
    return out;
}

double duracion(const std::vector<std::string>& b, double cps, double minimo) {
    size_t n = 0;
    for (const auto& l : b)
        n += largo(l);
    return std::max(minimo, static_cast<double>(n) / std::max(1.0, cps));
}

// --- proc ---------------------------------------------------------------------

void procAgregar(void* data, calldata_t* cd) {
    auto* s = static_cast<Subs*>(data);
    const char* t = calldata_string(cd, "texto");
    if (t == nullptr || *t == '\0') return;
    std::lock_guard<std::mutex> l(s->mu);
    for (auto& b : bloques(t, static_cast<size_t>(s->caracteres), static_cast<size_t>(s->nLineas)))
        s->cola.push_back(std::move(b));
    // Si se atrasó mucho (alguien habla sin parar), se descarta lo más viejo.
    while (s->cola.size() > 12)
        s->cola.pop_front();
}

void procLimpiar(void* data, calldata_t*) {
    auto* s = static_cast<Subs*>(data);
    std::lock_guard<std::mutex> l(s->mu);
    s->cola.clear();
    s->actual.clear();
    s->restante = 0;
    s->cambio = true;
}

// --- Ciclo de vida -------------------------------------------------------------

void actualizar(void* data, obs_data_t* settings) {
    auto* s = static_cast<Subs*>(data);
    std::lock_guard<std::mutex> l(s->mu);
    s->nLineas = std::clamp(static_cast<int>(obs_data_get_int(settings, "lineas")), 1, kMaxLineas);
    s->caracteres = std::clamp(static_cast<int>(obs_data_get_int(settings, "caracteres")), 10, 120);
    s->ancho = std::clamp(static_cast<int>(obs_data_get_int(settings, "ancho")), 200, 7680);
    s->minimo = obs_data_get_double(settings, "minimo");
    s->cps = obs_data_get_double(settings, "cps");
    s->borrar = obs_data_get_double(settings, "borrar");
    s->fondo = obs_data_get_bool(settings, "fondo");
    s->contorno = obs_data_get_bool(settings, "contorno");
    s->sombra = obs_data_get_bool(settings, "sombra");
    s->colorTexto = static_cast<uint32_t>(obs_data_get_int(settings, "color"));
    s->colorFondo = static_cast<uint32_t>(obs_data_get_int(settings, "color_fondo"));
    if (s->font != nullptr) obs_data_release(s->font);
    s->font = obs_data_get_obj(settings, "font");
    s->tamano = s->font != nullptr ? std::max(8, static_cast<int>(obs_data_get_int(s->font, "size"))) : 56;
    s->cambio = true;
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* s = new (std::nothrow) Subs();
    if (s == nullptr) return nullptr;
    s->ctx = ctx;
    proc_handler_t* ph = obs_source_get_proc_handler(ctx);
    proc_handler_add(ph, "void agregar(string texto)", procAgregar, s);
    proc_handler_add(ph, "void limpiar()", procLimpiar, s);
    for (int i = 0; i < kMaxLineas; ++i) {
        obs_data_t* d = obs_data_create();
        s->lineas[i] = obs_source_create_private("text_ft2_source_v2", "subtitulo-linea", d);
        obs_data_release(d);
    }
    actualizar(s, settings);
    return s;
}

void destruir(void* data) {
    auto* s = static_cast<Subs*>(data);
    for (auto* l : s->lineas)
        if (l != nullptr) obs_source_release(l);
    if (s->font != nullptr) obs_data_release(s->font);
    delete s;
}

void defectos(obs_data_t* d) {
    obs_data_set_default_int(d, "lineas", 2);
    obs_data_set_default_int(d, "caracteres", 42);
    obs_data_set_default_int(d, "ancho", 1600);
    obs_data_set_default_double(d, "minimo", 1.5);
    obs_data_set_default_double(d, "cps", 17);
    obs_data_set_default_double(d, "borrar", 4);
    obs_data_set_default_bool(d, "fondo", true);
    obs_data_set_default_bool(d, "contorno", false);
    obs_data_set_default_bool(d, "sombra", true);
    obs_data_set_default_int(d, "color", 0xFFFFFFFF);
    obs_data_set_default_int(d, "color_fondo", 0xB4000000);
    obs_data_t* f = obs_data_create();
    obs_data_set_default_string(f, "face", "Helvetica");
    obs_data_set_default_int(f, "size", 56);
    obs_data_set_default_obj(d, "font", f);
    obs_data_release(f);
}

obs_properties_t* propiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    // La lista de salidas de texto la da la app (proc global `ba_flujos_texto`).
    obs_property_t* fl = obs_properties_add_list(p, "flujo", "Qué muestra (salida de texto)",
                                                 OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
    calldata_t cd;
    calldata_init(&cd);
    if (proc_handler_call(obs_get_proc_handler(), "ba_flujos_texto", &cd)) {
        const char* lista = calldata_string(&cd, "lista"); // una por línea
        std::string l = lista != nullptr ? lista : "";
        size_t i = 0;
        while (i < l.size()) {
            const size_t j = l.find('\n', i);
            const std::string n = l.substr(i, j == std::string::npos ? std::string::npos : j - i);
            if (!n.empty()) obs_property_list_add_string(fl, n.c_str(), n.c_str());
            if (j == std::string::npos) break;
            i = j + 1;
        }
    }
    calldata_free(&cd);
    obs_property_set_long_description(
        fl, "La transcripción de una «Voz a texto» o una traducción (Audio → Traducción).");
    obs_properties_add_font(p, "font", "Letra");
    obs_properties_add_color_alpha(p, "color", "Color del texto");
    obs_properties_add_bool(p, "fondo", "Fondo detrás de cada línea");
    obs_properties_add_color_alpha(p, "color_fondo", "Color del fondo");
    obs_properties_add_bool(p, "contorno", "Contorno");
    obs_properties_add_bool(p, "sombra", "Sombra");
    obs_properties_add_int(p, "lineas", "Líneas a la vez", 1, kMaxLineas, 1);
    obs_properties_add_int(p, "caracteres", "Caracteres por línea (máximo)", 10, 120, 1);
    obs_properties_add_int(p, "ancho", "Ancho del cuadro (px)", 200, 7680, 10);
    obs_properties_add_float(p, "cps", "Velocidad de lectura (caracteres por segundo)", 5, 40, 0.5);
    obs_properties_add_float(p, "minimo", "Tiempo mínimo en pantalla (s)", 0.5, 10, 0.1);
    obs_properties_add_float(p, "borrar", "Se borra después de (s)", 1, 60, 0.5);
    obs_properties_add_button(p, "limpiar", "Borrar ahora",
                              [](obs_properties_t*, obs_property_t*, void* data) {
                                  procLimpiar(data, nullptr);
                                  return false;
                              });
    return p;
}

int altoLinea(const Subs* s) { return static_cast<int>(s->tamano * 1.35) + (s->fondo ? s->tamano / 4 : 0); }

uint32_t ancho(void* data) {
    auto* s = static_cast<Subs*>(data);
    std::lock_guard<std::mutex> l(s->mu);
    return static_cast<uint32_t>(s->ancho);
}
uint32_t alto(void* data) {
    auto* s = static_cast<Subs*>(data);
    std::lock_guard<std::mutex> l(s->mu);
    return static_cast<uint32_t>(s->nLineas * altoLinea(s));
}

void tick(void* data, float segundos) {
    auto* s = static_cast<Subs*>(data);
    std::lock_guard<std::mutex> l(s->mu);
    if (!s->actual.empty()) {
        // Con bloques esperando, el que se ve se apura (hasta el doble).
        s->restante -=
            segundos *
            (s->cola.empty() ? 1.0 : std::min(2.0, 1.0 + 0.35 * static_cast<double>(s->cola.size())));
        if (s->restante <= 0 && !s->cola.empty()) {
            s->actual = std::move(s->cola.front());
            s->cola.pop_front();
            s->restante = duracion(s->actual, s->cps, s->minimo);
            s->cambio = true;
        } else if (s->restante <= 0) {
            s->vacio += segundos;
            if (s->vacio >= s->borrar) {
                s->actual.clear();
                s->cambio = true;
            }
        }
    } else if (!s->cola.empty()) {
        s->actual = std::move(s->cola.front());
        s->cola.pop_front();
        s->restante = duracion(s->actual, s->cps, s->minimo);
        s->vacio = 0;
        s->cambio = true;
    }
    if (s->restante > 0) s->vacio = 0;
    if (!s->cambio) return;
    s->cambio = false;
    for (int i = 0; i < kMaxLineas; ++i) {
        obs_data_t* d = obs_data_create();
        // La letra con valores EXPLÍCITOS: los «por defecto» de un objeto no se
        // copian al pasarlo a otra fuente, y la de texto de libobs se quedaba con
        // su tamaño de fábrica (256: letras cuatro veces más grandes que el cuadro).
        obs_data_t* f = obs_data_create();
        obs_data_set_string(f, "face",
                            s->font != nullptr ? obs_data_get_string(s->font, "face") : "Helvetica");
        obs_data_set_string(f, "style",
                            s->font != nullptr ? obs_data_get_string(s->font, "style") : "Regular");
        obs_data_set_int(f, "size", s->tamano);
        obs_data_set_int(f, "flags", s->font != nullptr ? obs_data_get_int(s->font, "flags") : 0);
        obs_data_set_obj(d, "font", f);
        obs_data_release(f);
        obs_data_set_string(d, "text",
                            i < static_cast<int>(s->actual.size()) ? s->actual[static_cast<size_t>(i)].c_str()
                                                                   : "");
        obs_data_set_int(d, "color1", s->colorTexto);
        obs_data_set_int(d, "color2", s->colorTexto);
        obs_data_set_bool(d, "outline", s->contorno);
        obs_data_set_bool(d, "drop_shadow", s->sombra && !s->fondo);
        obs_data_set_bool(d, "word_wrap", false);
        obs_source_update(s->lineas[i], d);
        obs_data_release(d);
    }
}

void dibujarCaja(float x, float y, float w, float h, uint32_t abgr) {
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t* color = gs_effect_get_param_by_name(solid, "color");
    struct vec4 c;
    vec4_from_rgba(&c, abgr); // libobs guarda ABGR: vec4_from_rgba lo lee en ese orden
    gs_effect_set_vec4(color, &c);
    gs_technique_t* tech = gs_effect_get_technique(solid, "Solid");
    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);
    gs_matrix_push();
    gs_matrix_translate3f(x, y, 0.0f);
    gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    gs_matrix_pop();
    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

void render(void* data, gs_effect_t*) {
    auto* s = static_cast<Subs*>(data);
    int n, W, H, tam;
    bool fondo;
    uint32_t colorFondo;
    {
        std::lock_guard<std::mutex> l(s->mu);
        n = static_cast<int>(s->actual.size());
        W = s->ancho;
        H = altoLinea(s);
        tam = s->tamano;
        fondo = s->fondo;
        colorFondo = s->colorFondo;
    }
    if (n == 0) return;
    int nCaja;
    {
        std::lock_guard<std::mutex> l(s->mu);
        nCaja = s->nLineas;
    }
    const float padX = static_cast<float>(tam) * 0.35f, padY = static_cast<float>(tam) * 0.08f;
    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
    // Abajo de todo la última línea: el bloque se apoya en el borde inferior.
    for (int i = 0; i < n; ++i) {
        obs_source_t* l = s->lineas[i];
        const float w = static_cast<float>(obs_source_get_width(l));
        const float h = static_cast<float>(obs_source_get_height(l));
        if (w <= 0 || h <= 0) continue;
        const float y = static_cast<float>((nCaja - n + i) * H) + (static_cast<float>(H) - h) / 2.0f;
        const float x = (static_cast<float>(W) - w) / 2.0f;
        if (fondo) dibujarCaja(x - padX, y - padY, w + 2 * padX, h + 2 * padY, colorFondo);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        obs_source_video_render(l);
        gs_matrix_pop();
    }
    gs_blend_state_pop();
}

void enumHijas(void* data, obs_source_enum_proc_t cb, void* param) {
    auto* s = static_cast<Subs*>(data);
    for (auto* l : s->lineas)
        if (l != nullptr) cb(s->ctx, l, param);
}

} // namespace

extern "C" void ba_register_subtitulos(void) {
    obs_source_info info = {};
    info.id = "ba_subtitulos";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = nombre;
    info.create = crear;
    info.destroy = destruir;
    info.update = actualizar;
    info.get_defaults = defectos;
    info.get_properties = propiedades;
    info.get_width = ancho;
    info.get_height = alto;
    info.video_tick = tick;
    info.video_render = render;
    info.enum_active_sources = enumHijas;
    info.icon_type = OBS_ICON_TYPE_TEXT;
    obs_register_source(&info);
}
