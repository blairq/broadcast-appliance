// ============================================================================
// src/obs-module/ba_video_grafo.cpp — El grafo de video de una entrada
//
// Proposito:
//   FILTRO `ba_video_grafo`: corre en la GPU un grafo de TARJETAS armado con
//   cables (como el rack de audio): la entrada (`in`) pasa por tarjetas de
//   una pasada (color, croma, recorte/espejo) y llega a la escena (`out`).
//   Una salida puede ir a varias tarjetas: el grafo se ramifica.
//
//   FUENTE `ba_video_salida`: una tarjeta «Salida» publica la imagen de ese
//   punto del grafo como una fuente con nombre, que se pone en cualquier
//   escena (p. ej. «Cámara 1» al aire sin tocar y «Cámara 1 · croma» en otra).
//
//   El grafo llega como JSON en el ajuste `grafo` (lo arma la app, ver
//   src/app/video/VideoGrafos):
//     {"nodos":[{"id","tipo","params":{…}}], "cables":[{"desde","hacia"}]}
//   tipos: in, out, color, croma, recorte, salida.
//
// Modelo de threading:
//   `update` (cualquier hilo) arma un Plan nuevo y lo cambia bajo un mutex;
//   `video_render` (hilo grafico) toma el plan vigente. Las tarjetas se
//   calculan UNA vez por cuadro (obs_get_video_frame_time), aunque la fuente
//   se dibuje varias veces (programa, vista previa, multiview). Las salidas
//   se registran por nombre en un mapa global con su propio mutex; sus
//   texturas solo se tocan en el hilo grafico.
//
// Estrategia de errores:
//   Si el efecto no compila, o el grafo no tiene camino a la escena, la imagen
//   pasa como vino (o negro si `out` no recibe nada, que es lo pedido).
// ============================================================================

#include <obs-module.h>
#include <obs.h>

#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {

enum class Tipo { Color, Croma, Recorte, Salida };

/// Una tarjeta del plan, en el orden en que se calcula.
struct Paso {
    Tipo tipo = Tipo::Color;
    int entrada = -1; ///< el paso que la alimenta (-1 = la entrada de la fuente)
    // color
    float brillo = 0.0f, contraste = 1.0f, saturacion = 1.0f, gamma = 1.0f;
    // croma
    float claveCb = 0.0f, claveCr = 0.0f, similitud = 0.4f, suavidad = 0.08f, derrame = 0.1f;
    // recorte
    float izq = 0.0f, arr = 0.0f, der = 0.0f, aba = 0.0f, espejoX = 0.0f, espejoY = 0.0f;
    // salida
    std::string nombre;
};

struct Plan {
    std::vector<Paso> pasos;
    /// Lo que llega a la escena: -1 = la entrada tal cual, -2 = nada (negro).
    int escena = -1;
    bool trivial() const { return pasos.empty() && escena == -1; }
};

// --- Las salidas, por nombre -------------------------------------------------

struct Salida {
    obs_source_t* ctx = nullptr;
    std::string fuente;                ///< la entrada de la que sale (para despertarla)
    gs_texrender_t* tex = nullptr;     ///< lo ultimo que le mando el grafo
    std::uint32_t cx = 0, cy = 0;
    std::uint64_t cuadro = 0;          ///< cuando se lleno
    gs_texrender_t* despertar = nullptr; ///< para dibujar la entrada si nadie la dibuja
};

std::mutex g_salidasLock;
std::map<std::string, Salida*>& salidas() {
    static std::map<std::string, Salida*> m;
    return m;
}

// --- El filtro -----------------------------------------------------------------

struct Grafo {
    obs_source_t* ctx = nullptr;
    gs_effect_t* fx = nullptr;
    gs_eparam_t *pImage = nullptr, *pTexel = nullptr, *pBrillo = nullptr, *pContraste = nullptr,
                *pSaturacion = nullptr, *pGamma = nullptr, *pClave = nullptr, *pSimilitud = nullptr,
                *pSuavidad = nullptr, *pDerrame = nullptr, *pBorde = nullptr, *pEspejo = nullptr;

    std::mutex planLock;
    std::shared_ptr<const Plan> plan = std::make_shared<Plan>();

    gs_texrender_t* entrada = nullptr;
    std::vector<gs_texrender_t*> texs; ///< una por paso
    std::uint64_t ultimoCuadro = 0;
    bool listo = false;
    std::uint32_t cx = 0, cy = 0;
};

float numero(obs_data_t* p, const char* k, double def) {
    return static_cast<float>(obs_data_has_user_value(p, k) ? obs_data_get_double(p, k) : def);
}

/// RGB (0..1) -> Cb, Cr de BT.709 (lo mismo que CromaDe en el efecto).
void cromaDe(float r, float g, float b, float& cb, float& cr) {
    cb = -0.1146f * r - 0.3854f * g + 0.5f * b + 0.5f;
    cr = 0.5f * r - 0.4542f * g - 0.0458f * b + 0.5f;
}

/// El JSON del grafo -> el plan (orden de calculo desde la entrada).
std::shared_ptr<Plan> armarPlan(const char* json) {
    auto plan = std::make_shared<Plan>();
    if (json == nullptr || *json == '\0') return plan;
    obs_data_t* d = obs_data_create_from_json(json);
    if (d == nullptr) return plan;
    struct Nodo {
        std::string id, tipo;
        obs_data_t* params = nullptr;
        std::string desde; ///< quien la alimenta (el primer cable que llega)
    };
    std::vector<Nodo> nodos;
    obs_data_array_t* an = obs_data_get_array(d, "nodos");
    for (size_t i = 0; an != nullptr && i < obs_data_array_count(an); ++i) {
        obs_data_t* n = obs_data_array_item(an, i);
        Nodo x;
        x.id = obs_data_get_string(n, "id");
        x.tipo = obs_data_get_string(n, "tipo");
        x.params = obs_data_get_obj(n, "params");
        nodos.push_back(std::move(x));
        obs_data_release(n);
    }
    obs_data_array_release(an);
    obs_data_array_t* ac = obs_data_get_array(d, "cables");
    for (size_t i = 0; ac != nullptr && i < obs_data_array_count(ac); ++i) {
        obs_data_t* c = obs_data_array_item(ac, i);
        const std::string desde = obs_data_get_string(c, "desde"), hacia = obs_data_get_string(c, "hacia");
        for (auto& n : nodos)
            if (n.id == hacia && n.desde.empty()) n.desde = desde;
        obs_data_release(c);
    }
    obs_data_array_release(ac);
    obs_data_release(d);

    // Desde la entrada hacia adelante: cada nodo se calcula cuando ya se
    // calculo quien lo alimenta. Lo que no cuelga de la entrada no se calcula.
    std::map<std::string, int> indice; // id -> paso (-1 = in)
    for (const auto& n : nodos)
        if (n.tipo == "in") indice[n.id] = -1;
    bool avanzo = true;
    while (avanzo) {
        avanzo = false;
        for (const auto& n : nodos) {
            if (n.tipo == "in" || n.tipo == "out" || indice.count(n.id) || !indice.count(n.desde)) continue;
            Paso p;
            p.entrada = indice[n.desde];
            obs_data_t* q = n.params;
            if (n.tipo == "color") {
                p.tipo = Tipo::Color;
                p.brillo = numero(q, "brillo", 0.0);
                p.contraste = numero(q, "contraste", 1.0);
                p.saturacion = numero(q, "saturacion", 1.0);
                p.gamma = numero(q, "gamma", 1.0);
            } else if (n.tipo == "croma") {
                p.tipo = Tipo::Croma;
                // El color de fondo como 0xRRGGBB (de fabrica, verde).
                const long long c = q != nullptr && obs_data_has_user_value(q, "clave") ? obs_data_get_int(q, "clave")
                                                                                       : 0x00B140;
                cromaDe(static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(c & 0xFF) / 255.0f, p.claveCb, p.claveCr);
                p.similitud = numero(q, "similitud", 0.4);
                p.suavidad = numero(q, "suavidad", 0.08);
                p.derrame = numero(q, "derrame", 0.1);
            } else if (n.tipo == "recorte") {
                p.tipo = Tipo::Recorte;
                p.izq = numero(q, "izquierda", 0.0);
                p.arr = numero(q, "arriba", 0.0);
                p.der = numero(q, "derecha", 0.0);
                p.aba = numero(q, "abajo", 0.0);
                p.espejoX = q != nullptr && obs_data_get_bool(q, "espejoH") ? 1.0f : 0.0f;
                p.espejoY = q != nullptr && obs_data_get_bool(q, "espejoV") ? 1.0f : 0.0f;
            } else if (n.tipo == "salida") {
                p.tipo = Tipo::Salida;
                p.nombre = q != nullptr ? obs_data_get_string(q, "nombre") : "";
            } else {
                continue; // un tipo que este filtro no conoce: no se calcula
            }
            indice[n.id] = static_cast<int>(plan->pasos.size());
            plan->pasos.push_back(std::move(p));
            avanzo = true;
        }
    }
    plan->escena = -2;
    for (const auto& n : nodos)
        if (n.tipo == "out") plan->escena = indice.count(n.desde) ? indice[n.desde] : -2;
    for (auto& n : nodos) obs_data_release(n.params);
    return plan;
}

const char* nombre(void*) { return obs_module_text("Grafo de video (BA)"); }

void actualizar(void* data, obs_data_t* s) {
    auto* g = static_cast<Grafo*>(data);
    auto plan = armarPlan(obs_data_get_string(s, "grafo"));
    std::lock_guard<std::mutex> l(g->planLock);
    g->plan = std::move(plan);
    g->listo = false;
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* g = new (std::nothrow) Grafo();
    if (g == nullptr) return nullptr;
    g->ctx = ctx;
    char* ruta = obs_module_file("video-grafo.effect");
    char* errores = nullptr;
    obs_enter_graphics();
    if (ruta != nullptr) g->fx = gs_effect_create_from_file(ruta, &errores);
    if (g->fx != nullptr) {
        g->pImage = gs_effect_get_param_by_name(g->fx, "image");
        g->pTexel = gs_effect_get_param_by_name(g->fx, "texel");
        g->pBrillo = gs_effect_get_param_by_name(g->fx, "brillo");
        g->pContraste = gs_effect_get_param_by_name(g->fx, "contraste");
        g->pSaturacion = gs_effect_get_param_by_name(g->fx, "saturacion");
        g->pGamma = gs_effect_get_param_by_name(g->fx, "gamma");
        g->pClave = gs_effect_get_param_by_name(g->fx, "clave");
        g->pSimilitud = gs_effect_get_param_by_name(g->fx, "similitud");
        g->pSuavidad = gs_effect_get_param_by_name(g->fx, "suavidad");
        g->pDerrame = gs_effect_get_param_by_name(g->fx, "derrame");
        g->pBorde = gs_effect_get_param_by_name(g->fx, "borde");
        g->pEspejo = gs_effect_get_param_by_name(g->fx, "espejo");
    }
    g->entrada = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    obs_leave_graphics();
    if (g->fx == nullptr) {
        blog(LOG_WARNING, "[ba-sources] Grafo de video: no se pudo cargar %s (%s); la imagen pasa sin tocar",
             ruta != nullptr ? ruta : "video-grafo.effect", errores != nullptr ? errores : "no esta el archivo");
    }
    bfree(errores);
    bfree(ruta);
    actualizar(g, settings);
    return g;
}

void destruir(void* data) {
    auto* g = static_cast<Grafo*>(data);
    obs_enter_graphics();
    gs_effect_destroy(g->fx);
    gs_texrender_destroy(g->entrada);
    for (gs_texrender_t* t : g->texs) gs_texrender_destroy(t);
    obs_leave_graphics();
    delete g;
}

/// Dibuja `tex` (cx x cy) con la tecnica pedida en el render target actual.
void pasada(Grafo* g, gs_texture_t* tex, const char* tecnica) {
    gs_effect_set_texture(g->pImage, tex);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    while (gs_effect_loop(g->fx, tecnica)) gs_draw_sprite(tex, 0, g->cx, g->cy);
    gs_blend_state_pop();
}

/// Copia `tex` a la textura de la salida `nombre` (si existe).
void aSalida(Grafo* g, const std::string& nombre, gs_texture_t* tex) {
    std::lock_guard<std::mutex> l(g_salidasLock);
    auto it = salidas().find(nombre);
    if (it == salidas().end()) return;
    Salida* s = it->second;
    gs_texrender_reset(s->tex);
    if (!gs_texrender_begin(s->tex, g->cx, g->cy)) return;
    gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
    pasada(g, tex, "Copiar");
    gs_texrender_end(s->tex);
    s->cx = g->cx;
    s->cy = g->cy;
    s->cuadro = obs_get_video_frame_time();
}

/// Las tarjetas del cuadro actual (una vez por cuadro).
bool procesar(Grafo* g, const Plan& plan) {
    // La entrada, tal como llega a este filtro (con los filtros de antes).
    gs_texrender_reset(g->entrada);
    if (!gs_texrender_begin(g->entrada, g->cx, g->cy)) return false;
    struct vec4 nada;
    vec4_zero(&nada);
    gs_clear(GS_CLEAR_COLOR, &nada, 0.0f, 0);
    gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
    const bool ok = obs_source_process_filter_begin(g->ctx, GS_RGBA, OBS_NO_DIRECT_RENDERING);
    if (ok) {
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_process_filter_tech_end(g->ctx, g->fx, g->cx, g->cy, "Copiar");
        gs_blend_state_pop();
    }
    gs_texrender_end(g->entrada);
    if (!ok) return false;

    while (g->texs.size() < plan.pasos.size()) g->texs.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
    const auto texDe = [&](int i) -> gs_texture_t* {
        return i < 0 ? gs_texrender_get_texture(g->entrada) : gs_texrender_get_texture(g->texs[static_cast<size_t>(i)]);
    };
    struct vec2 texel;
    vec2_set(&texel, 1.0f / static_cast<float>(g->cx), 1.0f / static_cast<float>(g->cy));
    gs_effect_set_vec2(g->pTexel, &texel);
    for (size_t i = 0; i < plan.pasos.size(); ++i) {
        const Paso& p = plan.pasos[i];
        gs_texture_t* en = texDe(p.entrada);
        gs_texrender_t* destino = g->texs[i];
        gs_texrender_reset(destino);
        if (!gs_texrender_begin(destino, g->cx, g->cy)) continue;
        gs_ortho(0.0f, static_cast<float>(g->cx), 0.0f, static_cast<float>(g->cy), -100.0f, 100.0f);
        switch (p.tipo) {
        case Tipo::Color:
            gs_effect_set_float(g->pBrillo, p.brillo);
            gs_effect_set_float(g->pContraste, p.contraste);
            gs_effect_set_float(g->pSaturacion, p.saturacion);
            gs_effect_set_float(g->pGamma, p.gamma);
            pasada(g, en, "Color");
            break;
        case Tipo::Croma: {
            struct vec2 clave;
            vec2_set(&clave, p.claveCb, p.claveCr);
            gs_effect_set_vec2(g->pClave, &clave);
            gs_effect_set_float(g->pSimilitud, p.similitud);
            gs_effect_set_float(g->pSuavidad, p.suavidad);
            gs_effect_set_float(g->pDerrame, p.derrame);
            pasada(g, en, "Croma");
            break;
        }
        case Tipo::Recorte: {
            struct vec4 borde;
            vec4_set(&borde, p.izq, p.arr, p.der, p.aba);
            struct vec2 espejo;
            vec2_set(&espejo, p.espejoX, p.espejoY);
            gs_effect_set_vec4(g->pBorde, &borde);
            gs_effect_set_vec2(g->pEspejo, &espejo);
            pasada(g, en, "Recorte");
            break;
        }
        case Tipo::Salida:
            // Pasa lo que recibe (lo que la siga ve lo mismo) y lo publica.
            pasada(g, en, "Copiar");
            break;
        }
        gs_texrender_end(destino);
        if (p.tipo == Tipo::Salida && !p.nombre.empty()) aSalida(g, p.nombre, gs_texrender_get_texture(destino));
    }
    return true;
}

void dibujar(void* data, gs_effect_t*) {
    auto* g = static_cast<Grafo*>(data);
    std::shared_ptr<const Plan> plan;
    {
        std::lock_guard<std::mutex> l(g->planLock);
        plan = g->plan;
    }
    obs_source_t* objetivo = obs_filter_get_target(g->ctx);
    const std::uint32_t cx = objetivo != nullptr ? obs_source_get_base_width(objetivo) : 0;
    const std::uint32_t cy = objetivo != nullptr ? obs_source_get_base_height(objetivo) : 0;
    if (g->fx == nullptr || cx == 0 || cy == 0 || plan->trivial()) {
        obs_source_skip_video_filter(g->ctx);
        return;
    }
    if (cx != g->cx || cy != g->cy) {
        g->cx = cx;
        g->cy = cy;
        g->listo = false;
    }
    const std::uint64_t cuadro = obs_get_video_frame_time();
    if (cuadro != g->ultimoCuadro || !g->listo) {
        if (!procesar(g, *plan)) {
            obs_source_skip_video_filter(g->ctx);
            return;
        }
        g->ultimoCuadro = cuadro;
        g->listo = true;
    }
    if (plan->escena == -2) return; // `out` sin nada conectado: no se dibuja nada
    gs_texture_t* tex = plan->escena < 0 ? gs_texrender_get_texture(g->entrada)
                                         : gs_texrender_get_texture(g->texs[static_cast<size_t>(plan->escena)]);
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
    while (gs_effect_loop(fx, "Draw")) gs_draw_sprite(tex, 0, g->cx, g->cy);
}

// --- La fuente «Salida» ------------------------------------------------------

const char* salidaNombre(void*) { return obs_module_text("Salida de grafo de video (BA)"); }

void salidaActualizar(void* data, obs_data_t* s) {
    auto* x = static_cast<Salida*>(data);
    x->fuente = obs_data_get_string(s, "fuente");
}

void* salidaCrear(obs_data_t* settings, obs_source_t* ctx) {
    auto* x = new (std::nothrow) Salida();
    if (x == nullptr) return nullptr;
    x->ctx = ctx;
    obs_enter_graphics();
    x->tex = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    x->despertar = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    obs_leave_graphics();
    salidaActualizar(x, settings);
    std::lock_guard<std::mutex> l(g_salidasLock);
    salidas()[obs_source_get_name(ctx)] = x;
    return x;
}

void salidaDestruir(void* data) {
    auto* x = static_cast<Salida*>(data);
    {
        std::lock_guard<std::mutex> l(g_salidasLock);
        for (auto it = salidas().begin(); it != salidas().end();)
            it = it->second == x ? salidas().erase(it) : std::next(it);
    }
    obs_enter_graphics();
    gs_texrender_destroy(x->tex);
    gs_texrender_destroy(x->despertar);
    obs_leave_graphics();
    delete x;
}

/// La entrada de la que sale, con referencia (o nulo).
obs_source_t* origen(Salida* x) { return x->fuente.empty() ? nullptr : obs_get_source_by_name(x->fuente.c_str()); }

std::uint32_t salidaAncho(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (x->cx != 0) return x->cx;
    obs_source_t* o = origen(x);
    const std::uint32_t w = o != nullptr ? obs_source_get_width(o) : 0;
    obs_source_release(o);
    return w;
}

std::uint32_t salidaAlto(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (x->cy != 0) return x->cy;
    obs_source_t* o = origen(x);
    const std::uint32_t h = o != nullptr ? obs_source_get_height(o) : 0;
    obs_source_release(o);
    return h;
}

/// Mientras la salida se ve, su entrada tiene que dar cuadros aunque no este
/// en ninguna escena visible (las camaras solo suben cuadros si se muestran).
void salidaMostrar(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (obs_source_t* o = origen(x)) {
        obs_source_inc_showing(o);
        obs_source_release(o);
    }
}

void salidaOcultar(void* data) {
    auto* x = static_cast<Salida*>(data);
    if (obs_source_t* o = origen(x)) {
        obs_source_dec_showing(o);
        obs_source_release(o);
    }
}

void salidaDibujar(void* data, gs_effect_t*) {
    auto* x = static_cast<Salida*>(data);
    // Si en este cuadro nadie dibujo la entrada, el grafo no corrio: se la
    // dibuja aparte (y su grafo llena esta salida).
    if (x->cuadro != obs_get_video_frame_time()) {
        if (obs_source_t* o = origen(x)) {
            const std::uint32_t w = obs_source_get_width(o), h = obs_source_get_height(o);
            if (w != 0 && h != 0) {
                gs_texrender_reset(x->despertar);
                if (gs_texrender_begin(x->despertar, w, h)) {
                    gs_ortho(0.0f, static_cast<float>(w), 0.0f, static_cast<float>(h), -100.0f, 100.0f);
                    obs_source_video_render(o);
                    gs_texrender_end(x->despertar);
                }
            }
            obs_source_release(o);
        }
    }
    if (x->cx == 0 || x->cy == 0) return;
    gs_texture_t* tex = gs_texrender_get_texture(x->tex);
    if (tex == nullptr) return;
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
    while (gs_effect_loop(fx, "Draw")) gs_draw_sprite(tex, 0, x->cx, x->cy);
}

} // namespace

extern "C" {

static struct obs_source_info ba_video_grafo_info = {};
static struct obs_source_info ba_video_salida_info = {};

/// Registra el filtro del grafo y la fuente de sus salidas. La llama
/// `ba_register_video_filters` (ba_mejora_imagen.cpp).
void ba_register_video_grafo(void) {
    ba_video_grafo_info.id = "ba_video_grafo";
    ba_video_grafo_info.type = OBS_SOURCE_TYPE_FILTER;
    ba_video_grafo_info.output_flags = OBS_SOURCE_VIDEO;
    ba_video_grafo_info.get_name = nombre;
    ba_video_grafo_info.create = crear;
    ba_video_grafo_info.destroy = destruir;
    ba_video_grafo_info.update = actualizar;
    ba_video_grafo_info.video_render = dibujar;
    obs_register_source(&ba_video_grafo_info);

    ba_video_salida_info.id = "ba_video_salida";
    ba_video_salida_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_video_salida_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    ba_video_salida_info.get_name = salidaNombre;
    ba_video_salida_info.create = salidaCrear;
    ba_video_salida_info.destroy = salidaDestruir;
    ba_video_salida_info.update = salidaActualizar;
    ba_video_salida_info.get_width = salidaAncho;
    ba_video_salida_info.get_height = salidaAlto;
    ba_video_salida_info.show = salidaMostrar;
    ba_video_salida_info.hide = salidaOcultar;
    ba_video_salida_info.video_render = salidaDibujar;
    obs_register_source(&ba_video_salida_info);
}

} // extern "C"
