// ============================================================================
// src/obs-module/ba_video_estudio.cpp — Tomas y compositores (ver ba_video_estudio.hpp)
//
// FUENTE `ba_video_compositor`: muestra un compositor del grafo en una escena.
// Ajustes: `nodo` (el id del compositor en la app) y `config` (su
// configuración, JSON; la mantiene al día la app, video/VideoGrafos).
// ============================================================================

#include "ba_video_estudio.hpp"

#include <obs.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace ba::video {
namespace {

struct Toma {
    gs_texrender_t* tex = nullptr;
    std::uint32_t cx = 0, cy = 0;
    std::uint64_t cuadro = 0;
};

struct Capa {
    std::string toma, fuente;
    float x = 0, y = 0, ancho = 1, alto = 1, opacidad = 1;
    bool llenar = false; ///< estirar a la caja (si no, encaja respetando la proporción)
};

struct Compositor {
    std::uint32_t ancho = 1920, alto = 1080;
    std::uint32_t fondo = 0xFF000000; ///< 0xAARRGGBB
    std::vector<Capa> capas;
    gs_texrender_t* tex = nullptr;
    std::uint64_t cuadro = 0;
    bool ocupado = false;
};

// Todo esto se toca en el hilo gráfico, salvo la configuración (update de
// las fuentes y filtros, cualquier hilo): un candado para los mapas.
std::mutex g_lock;
std::map<std::string, Toma>& tomas() {
    static std::map<std::string, Toma> m;
    return m;
}
std::map<std::string, Compositor>& compositores() {
    static std::map<std::string, Compositor> m;
    return m;
}
std::set<std::string>& enCurso() {
    static std::set<std::string> m;
    return m;
}

gs_effect_t* efecto() {
    static gs_effect_t* fx = nullptr;
    static bool probado = false;
    if (!probado) {
        probado = true;
        char* ruta = obs_module_file("video-grafo.effect");
        if (ruta != nullptr) fx = gs_effect_create_from_file(ruta, nullptr);
        bfree(ruta);
        if (fx == nullptr) blog(LOG_WARNING, "[ba-sources] Estudio de video: no se pudo cargar video-grafo.effect");
    }
    return fx;
}

/// Dibuja `tex` en (x, y, w, h) del render target actual con la técnica `tecnica`.
void dibujarTex(gs_texture_t* tex, float x, float y, float w, float h, const char* tecnica, float opacidad) {
    gs_effect_t* fx = efecto();
    if (fx == nullptr || tex == nullptr) return;
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
    if (gs_eparam_t* o = gs_effect_get_param_by_name(fx, "opacidad")) gs_effect_set_float(o, opacidad);
    gs_matrix_push();
    gs_matrix_translate3f(x, y, 0.0f);
    while (gs_effect_loop(fx, tecnica))
        gs_draw_sprite(tex, 0, static_cast<std::uint32_t>(std::max(1.0f, w)), static_cast<std::uint32_t>(std::max(1.0f, h)));
    gs_matrix_pop();
}

/// Dibuja la entrada `fuente` aparte (su filtro llena sus tomas).
void despertar(const std::string& fuente) {
    {
        std::lock_guard<std::mutex> l(g_lock);
        if (enCurso().count(fuente)) return;
    }
    obs_source_t* o = obs_get_source_by_name(fuente.c_str());
    if (o == nullptr) return;
    const std::uint32_t w = obs_source_get_width(o), h = obs_source_get_height(o);
    // Uno por profundidad: despertar una entrada puede despertar otra (un
    // compositor que recibe de un compositor) y cada una dibuja en el suyo.
    static std::vector<gs_texrender_t*> aparte;
    static size_t profundidad = 0;
    if (w != 0 && h != 0 && profundidad < 8) {
        while (aparte.size() <= profundidad) aparte.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
        gs_texrender_t* t = aparte[profundidad++];
        gs_texrender_reset(t);
        if (gs_texrender_begin(t, w, h)) {
            gs_ortho(0.0f, static_cast<float>(w), 0.0f, static_cast<float>(h), -100.0f, 100.0f);
            obs_source_video_render(o);
            gs_texrender_end(t);
        }
        --profundidad;
    }
    obs_source_release(o);
}

} // namespace

void dibujando(const std::string& fuente, bool si) {
    std::lock_guard<std::mutex> l(g_lock);
    if (si)
        enCurso().insert(fuente);
    else
        enCurso().erase(fuente);
}

void publicarToma(const std::string& nombre, const std::string& fuente, gs_texture_t* tex, std::uint32_t cx,
                  std::uint32_t cy) {
    (void)fuente;
    if (tex == nullptr || cx == 0 || cy == 0) return;
    gs_texrender_t* destino = nullptr;
    {
        std::lock_guard<std::mutex> l(g_lock);
        Toma& t = tomas()[nombre];
        if (t.tex == nullptr) t.tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        destino = t.tex;
    }
    gs_texrender_reset(destino);
    if (!gs_texrender_begin(destino, cx, cy)) return;
    gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    dibujarTex(tex, 0, 0, static_cast<float>(cx), static_cast<float>(cy), "Copiar", 1.0f);
    gs_blend_state_pop();
    gs_texrender_end(destino);
    std::lock_guard<std::mutex> l(g_lock);
    Toma& t = tomas()[nombre];
    t.cx = cx;
    t.cy = cy;
    t.cuadro = obs_get_video_frame_time();
}

gs_texture_t* leerToma(const std::string& nombre, const std::string& fuente, std::uint32_t* cx, std::uint32_t* cy) {
    const std::uint64_t ahora = obs_get_video_frame_time();
    bool fresca = false;
    {
        std::lock_guard<std::mutex> l(g_lock);
        const auto it = tomas().find(nombre);
        fresca = it != tomas().end() && it->second.cuadro == ahora;
    }
    if (!fresca && !fuente.empty()) despertar(fuente);
    std::lock_guard<std::mutex> l(g_lock);
    const auto it = tomas().find(nombre);
    if (it == tomas().end() || it->second.cx == 0) return nullptr;
    if (cx != nullptr) *cx = it->second.cx;
    if (cy != nullptr) *cy = it->second.cy;
    return gs_texrender_get_texture(it->second.tex);
}

void configurarCompositor(const std::string& id, const char* config) {
    obs_data_t* d = obs_data_create_from_json(config != nullptr && *config != '\0' ? config : "{}");
    if (d == nullptr) return;
    Compositor nuevo;
    obs_data_set_default_int(d, "ancho", 1920);
    obs_data_set_default_int(d, "alto", 1080);
    obs_data_set_default_int(d, "fondo", 0xFF000000LL);
    nuevo.ancho = static_cast<std::uint32_t>(std::clamp<long long>(obs_data_get_int(d, "ancho"), 16, 8192));
    nuevo.alto = static_cast<std::uint32_t>(std::clamp<long long>(obs_data_get_int(d, "alto"), 16, 8192));
    nuevo.fondo = static_cast<std::uint32_t>(obs_data_get_int(d, "fondo"));
    obs_data_array_t* a = obs_data_get_array(d, "capas");
    for (size_t i = 0; a != nullptr && i < obs_data_array_count(a); ++i) {
        obs_data_t* c = obs_data_array_item(a, i);
        obs_data_set_default_double(c, "ancho", 1.0);
        obs_data_set_default_double(c, "alto", 1.0);
        obs_data_set_default_double(c, "opacidad", 1.0);
        Capa k;
        k.toma = obs_data_get_string(c, "toma");
        k.fuente = obs_data_get_string(c, "fuente");
        k.x = static_cast<float>(obs_data_get_double(c, "x"));
        k.y = static_cast<float>(obs_data_get_double(c, "y"));
        k.ancho = static_cast<float>(obs_data_get_double(c, "ancho"));
        k.alto = static_cast<float>(obs_data_get_double(c, "alto"));
        k.opacidad = static_cast<float>(std::clamp(obs_data_get_double(c, "opacidad"), 0.0, 1.0));
        k.llenar = std::string(obs_data_get_string(c, "ajuste")) == "llenar";
        if (!k.toma.empty()) nuevo.capas.push_back(std::move(k));
        obs_data_release(c);
    }
    obs_data_array_release(a);
    obs_data_release(d);
    std::lock_guard<std::mutex> l(g_lock);
    Compositor& c = compositores()[id];
    c.ancho = nuevo.ancho;
    c.alto = nuevo.alto;
    c.fondo = nuevo.fondo;
    c.capas = std::move(nuevo.capas);
}

gs_texture_t* componer(const std::string& id, std::uint32_t* cx, std::uint32_t* cy) {
    const std::uint64_t ahora = obs_get_video_frame_time();
    Compositor copia;
    {
        std::lock_guard<std::mutex> l(g_lock);
        const auto it = compositores().find(id);
        if (it == compositores().end()) return nullptr;
        Compositor& c = it->second;
        if (cx != nullptr) *cx = c.ancho;
        if (cy != nullptr) *cy = c.alto;
        if (c.tex == nullptr) c.tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
        // Ya calculado en este cuadro, o calculándose (un compositor que se
        // pide a sí mismo por una vuelta): lo que hay.
        if (c.cuadro == ahora || c.ocupado) return gs_texrender_get_texture(c.tex);
        c.ocupado = true;
        copia.ancho = c.ancho;
        copia.alto = c.alto;
        copia.fondo = c.fondo;
        copia.capas = c.capas;
        copia.tex = c.tex;
    }
    // Primero las tomas (despertar dibuja aparte: no adentro del lienzo).
    struct Lista {
        gs_texture_t* tex;
        std::uint32_t cx, cy;
    };
    std::vector<Lista> listas;
    for (const auto& k : copia.capas) {
        std::uint32_t w = 0, h = 0;
        gs_texture_t* t = leerToma(k.toma, k.fuente, &w, &h);
        listas.push_back({t, w, h});
    }
    gs_texrender_reset(copia.tex);
    if (gs_texrender_begin(copia.tex, copia.ancho, copia.alto)) {
        const float W = static_cast<float>(copia.ancho), H = static_cast<float>(copia.alto);
        struct vec4 fondo;
        vec4_from_rgba(&fondo, ((copia.fondo >> 16) & 0xFF) | (copia.fondo & 0xFF00) | ((copia.fondo & 0xFF) << 16) |
                                   (copia.fondo & 0xFF000000));
        // Premultiplicado.
        fondo.x *= fondo.w;
        fondo.y *= fondo.w;
        fondo.z *= fondo.w;
        gs_clear(GS_CLEAR_COLOR, &fondo, 0.0f, 0);
        gs_ortho(0.0f, W, 0.0f, H, -100.0f, 100.0f);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
        for (size_t i = 0; i < copia.capas.size(); ++i) {
            const Capa& k = copia.capas[i];
            const Lista& t = listas[i];
            if (t.tex == nullptr || t.cx == 0 || t.cy == 0 || k.opacidad <= 0.0f) continue;
            float x = k.x * W, y = k.y * H, w = k.ancho * W, h = k.alto * H;
            if (!k.llenar) {
                // Encaja en la caja respetando la proporción de la toma, centrada.
                const float escala = std::min(w / static_cast<float>(t.cx), h / static_cast<float>(t.cy));
                const float wf = static_cast<float>(t.cx) * escala, hf = static_cast<float>(t.cy) * escala;
                x += (w - wf) / 2;
                y += (h - hf) / 2;
                w = wf;
                h = hf;
            }
            dibujarTex(t.tex, x, y, w, h, "Capa", k.opacidad);
        }
        gs_blend_state_pop();
        gs_texrender_end(copia.tex);
    }
    std::lock_guard<std::mutex> l(g_lock);
    Compositor& c = compositores()[id];
    c.cuadro = ahora;
    c.ocupado = false;
    return gs_texrender_get_texture(c.tex);
}

// --- La fuente `ba_video_compositor` -------------------------------------------

namespace {

struct FuenteCompositor {
    obs_source_t* ctx = nullptr;
    std::mutex lock;
    std::string nodo;
    std::uint32_t ancho = 1920, alto = 1080;
    std::vector<std::string> entradas; ///< las que alimentan (para que den cuadros)
    bool mostrando = false;
};

void mostrarEntradas(const std::vector<std::string>& es, bool si) {
    for (const auto& e : es)
        if (obs_source_t* o = obs_get_source_by_name(e.c_str())) {
            if (si)
                obs_source_inc_showing(o);
            else
                obs_source_dec_showing(o);
            obs_source_release(o);
        }
}

const char* compNombre(void*) { return obs_module_text("Compositor del grafo de video (BA)"); }

void compActualizar(void* data, obs_data_t* s) {
    auto* f = static_cast<FuenteCompositor*>(data);
    const std::string nodo = obs_data_get_string(s, "nodo");
    const char* config = obs_data_get_string(s, "config");
    configurarCompositor(nodo, config);
    std::vector<std::string> es;
    obs_data_t* d = obs_data_create_from_json(config != nullptr && *config != '\0' ? config : "{}");
    std::uint32_t w = 1920, h = 1080;
    if (d != nullptr) {
        if (obs_data_has_user_value(d, "ancho")) w = static_cast<std::uint32_t>(obs_data_get_int(d, "ancho"));
        if (obs_data_has_user_value(d, "alto")) h = static_cast<std::uint32_t>(obs_data_get_int(d, "alto"));
        obs_data_array_t* a = obs_data_get_array(d, "capas");
        for (size_t i = 0; a != nullptr && i < obs_data_array_count(a); ++i) {
            obs_data_t* c = obs_data_array_item(a, i);
            const std::string e = obs_data_get_string(c, "fuente");
            if (!e.empty() && std::find(es.begin(), es.end(), e) == es.end()) es.push_back(e);
            obs_data_release(c);
        }
        obs_data_array_release(a);
        obs_data_release(d);
    }
    std::lock_guard<std::mutex> l(f->lock);
    f->nodo = nodo;
    f->ancho = std::max<std::uint32_t>(16, w);
    f->alto = std::max<std::uint32_t>(16, h);
    // Si se está viendo, las entradas nuevas empiezan a dar cuadros y las
    // que salieron dejan de hacerlo por este compositor.
    if (f->mostrando && es != f->entradas) {
        mostrarEntradas(es, true);
        mostrarEntradas(f->entradas, false);
    }
    f->entradas = std::move(es);
}

void* compCrear(obs_data_t* s, obs_source_t* ctx) {
    auto* f = new (std::nothrow) FuenteCompositor();
    if (f == nullptr) return nullptr;
    f->ctx = ctx;
    compActualizar(f, s);
    return f;
}

void compDestruir(void* data) {
    auto* f = static_cast<FuenteCompositor*>(data);
    if (f->mostrando) mostrarEntradas(f->entradas, false);
    delete f;
}

std::uint32_t compAncho(void* data) { return static_cast<FuenteCompositor*>(data)->ancho; }
std::uint32_t compAlto(void* data) { return static_cast<FuenteCompositor*>(data)->alto; }

void compMostrar(void* data) {
    auto* f = static_cast<FuenteCompositor*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    f->mostrando = true;
    mostrarEntradas(f->entradas, true);
}

void compOcultar(void* data) {
    auto* f = static_cast<FuenteCompositor*>(data);
    std::lock_guard<std::mutex> l(f->lock);
    f->mostrando = false;
    mostrarEntradas(f->entradas, false);
}

void compDibujar(void* data, gs_effect_t*) {
    auto* f = static_cast<FuenteCompositor*>(data);
    std::string nodo;
    std::uint32_t w = 0, h = 0;
    {
        std::lock_guard<std::mutex> l(f->lock);
        nodo = f->nodo;
        w = f->ancho;
        h = f->alto;
    }
    std::uint32_t cx = 0, cy = 0;
    gs_texture_t* tex = componer(nodo, &cx, &cy);
    if (tex == nullptr) return;
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
    while (gs_effect_loop(fx, "Draw")) gs_draw_sprite(tex, 0, w, h);
}

obs_properties_t* compPropiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_text(p, "info", obs_module_text("Se arma en la página Video (el grafo del estudio)."), OBS_TEXT_INFO);
    return p;
}

struct obs_source_info g_info = {};

} // namespace
} // namespace ba::video

extern "C" void ba_register_video_estudio(void) {
    using namespace ba::video;
    g_info.id = "ba_video_compositor";
    g_info.type = OBS_SOURCE_TYPE_INPUT;
    g_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    g_info.get_name = compNombre;
    g_info.create = compCrear;
    g_info.destroy = compDestruir;
    g_info.update = compActualizar;
    g_info.get_width = compAncho;
    g_info.get_height = compAlto;
    g_info.show = compMostrar;
    g_info.hide = compOcultar;
    g_info.video_render = compDibujar;
    g_info.get_properties = compPropiedades;
    obs_register_source(&g_info);
}
