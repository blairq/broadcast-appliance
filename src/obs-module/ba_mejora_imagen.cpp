// ============================================================================
// src/obs-module/ba_mejora_imagen.cpp — Filtro de video «Mejorar imagen»
//
// Proposito:
//   FILTRO `ba_mejora_imagen`: limpia y afila una camara floja en la GPU, en
//   tiempo real y sin demora. Tres pasadas (data/mejora-imagen.effect):
//     1. Ruido temporal (resolucion completa): mezcla con el cuadro anterior
//        ya filtrado, salvo donde hay movimiento. Lee directo lo que entrega
//        libobs (process_filter_tech_end): no hay copia de la entrada.
//     2. Color guiado por el brillo (MEDIA resolucion, media precision): el
//        color se pega a los bordes de brillo (saca el chorreado del LED del
//        microfono, de la corbata, del 4:2:0).
//     3. Final (resolucion completa, DIRECTO a la salida): agranda ese color
//        respetando los bordes y afila el brillo (AMD FidelityFX CAS, MIT).
//   Cada efecto tiene su intensidad (0 lo apaga).
//
//   Es agnostico a la arquitectura: un shader de libobs, que libobs traduce a
//   D3D11 (Windows) u OpenGL (macOS, Linux). Pensado para que alcance en una
//   integrada (Radeon Vega, Intel UHD): dos pasadas a resolucion completa y
//   una a un cuarto, con pocas lecturas por pixel.
//
//   Mide cada pasada con temporizadores de la GPU (gs_timer) y lo publica en
//   el proc handler de la fuente: `tiempos` -> temporal, color, final (ms).
//
// Modelo de threading:
//   `video_render` corre en el hilo grafico de libobs (con el contexto ya
//   tomado). Una fuente puede dibujarse varias veces en el mismo cuadro (el
//   programa, la vista previa, un multiview): las pasadas 1 y 2 se hacen UNA
//   vez por cuadro (obs_get_video_frame_time); si no, el ruido temporal
//   mezclaria el cuadro consigo mismo. La final (barata) se hace en cada
//   dibujo, directo en el destino. `update` y `tiempos` corren en otros
//   hilos: las intensidades y los tiempos son atomicos.
//
// Estrategia de errores:
//   Si el efecto no compila o falta el archivo, el filtro queda en
//   PASS-THROUGH (la imagen pasa como vino) y lo dice una vez en el log.
// ============================================================================

#include <obs-module.h>
#include <obs.h>

#include <atomic>
#include <cstdint>
#include <new>

namespace {

/// Cuadros de temporizadores en vuelo: la GPU contesta unos cuadros despues.
constexpr int kTimers = 4;
enum Pasada { kTemporal, kColor, kFinal, kPasadas };

struct Mejora {
    obs_source_t* ctx = nullptr;
    gs_effect_t* fx = nullptr;
    gs_eparam_t* pAnterior = nullptr;
    gs_eparam_t* pMedio = nullptr;
    gs_eparam_t* pImage = nullptr;
    gs_eparam_t* pTexel = nullptr;
    gs_eparam_t* pMedioTam = nullptr;
    gs_eparam_t* pRuido = nullptr;
    gs_eparam_t* pColor = nullptr;
    gs_eparam_t* pNitidez = nullptr;

    gs_texrender_t* temporal[2] = {nullptr, nullptr}; ///< el actual y el anterior (se alternan)
    gs_texrender_t* medio = nullptr;                  ///< color guiado + brillo, media resolucion

    int actual = 0;
    bool hayAnterior = false;
    bool listo = false;       ///< hay un cuadro procesado para dibujar
    bool finalMedida = false; ///< la pasada final de este cuadro ya se midio
    std::uint64_t ultimoCuadro = 0;
    std::uint32_t cx = 0, cy = 0;

    gs_timer_range_t* rango[kTimers] = {};
    gs_timer_t* timer[kTimers][kPasadas] = {};
    bool enVuelo[kTimers] = {};
    int slot = 0;

    std::atomic<float> ruido{0.6f}, colorFuerza{0.7f}, nitidezFuerza{0.5f};
    std::atomic<float> ms[kPasadas] = {};
};

/// Fuerza maxima de la mezcla temporal: con 1.0 un cuadro quieto no cambiaria nunca.
constexpr float kRuidoMax = 0.85f;

const char* nombre(void*) { return obs_module_text("Mejorar imagen (BA)"); }

void defaults(obs_data_t* s) {
    obs_data_set_default_int(s, "ruido", 60);
    obs_data_set_default_int(s, "color", 70);
    obs_data_set_default_int(s, "nitidez", 50);
}

void actualizar(void* data, obs_data_t* s) {
    auto* m = static_cast<Mejora*>(data);
    m->ruido = kRuidoMax * static_cast<float>(obs_data_get_int(s, "ruido")) / 100.0f;
    m->colorFuerza = static_cast<float>(obs_data_get_int(s, "color")) / 100.0f;
    m->nitidezFuerza = static_cast<float>(obs_data_get_int(s, "nitidez")) / 100.0f;
}

obs_properties_t* propiedades(void*) {
    obs_properties_t* p = obs_properties_create();
    obs_properties_add_int_slider(p, "ruido", obs_module_text("Ruido (temporal)"), 0, 100, 1);
    obs_properties_add_int_slider(p, "color", obs_module_text("Color pegado al brillo"), 0, 100, 1);
    obs_properties_add_int_slider(p, "nitidez", obs_module_text("Nitidez"), 0, 100, 1);
    return p;
}

/// proc `tiempos`: lo que tarda cada pasada en la GPU (ms, promedio movil).
void tiempos(void* data, calldata_t* cd) {
    auto* m = static_cast<Mejora*>(data);
    calldata_set_float(cd, "temporal", m->ms[kTemporal].load());
    calldata_set_float(cd, "color", m->ms[kColor].load());
    calldata_set_float(cd, "final", m->ms[kFinal].load());
}

void liberarGraficos(Mejora* m) {
    gs_effect_destroy(m->fx);
    gs_texrender_destroy(m->temporal[0]);
    gs_texrender_destroy(m->temporal[1]);
    gs_texrender_destroy(m->medio);
    for (int s = 0; s < kTimers; ++s) {
        gs_timer_range_destroy(m->rango[s]);
        for (int p = 0; p < kPasadas; ++p) gs_timer_destroy(m->timer[s][p]);
    }
    m->fx = nullptr;
}

void* crear(obs_data_t* settings, obs_source_t* ctx) {
    auto* m = new (std::nothrow) Mejora();
    if (m == nullptr) {
        return nullptr;
    }
    m->ctx = ctx;
    char* ruta = obs_module_file("mejora-imagen.effect");
    char* errores = nullptr;
    obs_enter_graphics();
    if (ruta != nullptr) {
        m->fx = gs_effect_create_from_file(ruta, &errores);
    }
    if (m->fx != nullptr) {
        m->pImage = gs_effect_get_param_by_name(m->fx, "image");
        m->pAnterior = gs_effect_get_param_by_name(m->fx, "anterior");
        m->pMedio = gs_effect_get_param_by_name(m->fx, "medio");
        m->pTexel = gs_effect_get_param_by_name(m->fx, "texel");
        m->pMedioTam = gs_effect_get_param_by_name(m->fx, "medioTam");
        m->pRuido = gs_effect_get_param_by_name(m->fx, "ruido");
        m->pColor = gs_effect_get_param_by_name(m->fx, "color");
        m->pNitidez = gs_effect_get_param_by_name(m->fx, "nitidez");
    }
    m->temporal[0] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    m->temporal[1] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    // Media precision: el color es una diferencia (puede ser negativa) y en 8
    // bits haria escalones en la piel.
    m->medio = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
    for (int s = 0; s < kTimers; ++s) {
        m->rango[s] = gs_timer_range_create();
        for (int p = 0; p < kPasadas; ++p) m->timer[s][p] = gs_timer_create();
    }
    obs_leave_graphics();
    if (m->fx == nullptr) {
        blog(LOG_WARNING, "[ba-sources] Mejorar imagen: no se pudo cargar %s (%s); la imagen pasa sin filtrar",
             ruta != nullptr ? ruta : "mejora-imagen.effect", errores != nullptr ? errores : "no esta el archivo");
    }
    bfree(errores);
    bfree(ruta);
    actualizar(m, settings);
    proc_handler_add(obs_source_get_proc_handler(ctx), "void tiempos(out float temporal, out float color, out float final)",
                     tiempos, m);
    return m;
}

void destruir(void* data) {
    auto* m = static_cast<Mejora*>(data);
    obs_enter_graphics();
    liberarGraficos(m);
    obs_leave_graphics();
    delete m;
}

/// Junta lo que la GPU ya contesto de cuadros anteriores (sin esperarla).
void leerTiempos(Mejora* m) {
    for (int s = 0; s < kTimers; ++s) {
        if (!m->enVuelo[s] || s == m->slot) continue;
        bool disjunto = false;
        std::uint64_t frecuencia = 0;
        if (!gs_timer_range_get_data(m->rango[s], &disjunto, &frecuencia)) continue;
        std::uint64_t ticks[kPasadas] = {};
        bool todos = true;
        for (int p = 0; p < kPasadas; ++p) todos = todos && gs_timer_get_data(m->timer[s][p], &ticks[p]);
        if (!todos) continue;
        m->enVuelo[s] = false;
        // Todo en cero: el driver no implementa las consultas de tiempo (el
        // OpenGL de Apple Silicon). Queda sin medir, no en «0 ms».
        if (disjunto || frecuencia == 0 || (ticks[kTemporal] == 0 && ticks[kColor] == 0 && ticks[kFinal] == 0)) continue;
        for (int p = 0; p < kPasadas; ++p) {
            const float v = static_cast<float>(1000.0 * static_cast<double>(ticks[p]) / static_cast<double>(frecuencia));
            const float viejo = m->ms[p].load();
            m->ms[p] = viejo == 0.0f ? v : viejo * 0.9f + v * 0.1f;
        }
    }
}

/// Pasadas 1 y 2 del cuadro actual (una vez por cuadro).
bool procesar(Mejora* m) {
    leerTiempos(m);
    m->slot = (m->slot + 1) % kTimers;
    const bool medir = !m->enVuelo[m->slot];
    if (medir) gs_timer_range_begin(m->rango[m->slot]);

    struct vec2 texel;
    vec2_set(&texel, 1.0f / static_cast<float>(m->cx), 1.0f / static_cast<float>(m->cy));
    gs_effect_set_vec2(m->pTexel, &texel);
    gs_effect_set_float(m->pRuido, m->ruido.load());

    // 1) Ruido temporal, leyendo directo lo que dibuja la fuente.
    const int siguiente = m->actual ^ 1;
    gs_texrender_t* destino = m->temporal[siguiente];
    gs_texrender_reset(destino);
    if (!gs_texrender_begin(destino, m->cx, m->cy)) {
        if (medir) gs_timer_range_end(m->rango[m->slot]);
        return false;
    }
    gs_ortho(0.0f, static_cast<float>(m->cx), 0.0f, static_cast<float>(m->cy), -100.0f, 100.0f);
    const bool ok = obs_source_process_filter_begin(m->ctx, GS_RGBA, OBS_NO_DIRECT_RENDERING);
    if (ok) {
        const bool conAnterior = m->hayAnterior && m->ruido.load() > 0.0f;
        if (conAnterior) {
            gs_effect_set_texture(m->pAnterior, gs_texrender_get_texture(m->temporal[m->actual]));
        }
        if (medir) gs_timer_begin(m->timer[m->slot][kTemporal]);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_process_filter_tech_end(m->ctx, m->fx, m->cx, m->cy, conAnterior ? "Temporal" : "Copiar");
        gs_blend_state_pop();
        if (medir) gs_timer_end(m->timer[m->slot][kTemporal]);
    }
    gs_texrender_end(destino);
    if (!ok) {
        if (medir) gs_timer_range_end(m->rango[m->slot]);
        return false;
    }
    m->actual = siguiente;
    m->hayAnterior = m->ruido.load() > 0.0f;
    gs_texture_t* limpio = gs_texrender_get_texture(destino);

    // 2) Color guiado, a media resolucion. Se hace siempre (es barato): la
    //    pasada final lo lee aunque la intensidad sea 0.
    const std::uint32_t mx = (m->cx + 1) / 2, my = (m->cy + 1) / 2;
    gs_texrender_reset(m->medio);
    if (gs_texrender_begin(m->medio, mx, my)) {
        gs_ortho(0.0f, static_cast<float>(mx), 0.0f, static_cast<float>(my), -100.0f, 100.0f);
        gs_effect_set_texture(m->pImage, limpio);
        if (medir) gs_timer_begin(m->timer[m->slot][kColor]);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        while (gs_effect_loop(m->fx, "ColorMedio")) gs_draw_sprite(limpio, 0, mx, my);
        gs_blend_state_pop();
        if (medir) gs_timer_end(m->timer[m->slot][kColor]);
        gs_texrender_end(m->medio);
    }
    m->finalMedida = !medir; // si no se mide este cuadro, la final tampoco
    return true;
}

void dibujar(void* data, gs_effect_t*) {
    auto* m = static_cast<Mejora*>(data);
    obs_source_t* objetivo = obs_filter_get_target(m->ctx);
    const std::uint32_t cx = objetivo != nullptr ? obs_source_get_base_width(objetivo) : 0;
    const std::uint32_t cy = objetivo != nullptr ? obs_source_get_base_height(objetivo) : 0;
    if (m->fx == nullptr || cx == 0 || cy == 0) {
        obs_source_skip_video_filter(m->ctx);
        return;
    }
    if (cx != m->cx || cy != m->cy) {
        m->cx = cx;
        m->cy = cy;
        m->hayAnterior = false; // otro tamaño: el cuadro anterior no sirve
        m->listo = false;
    }
    const std::uint64_t cuadro = obs_get_video_frame_time();
    if (cuadro != m->ultimoCuadro || !m->listo) {
        if (!procesar(m)) {
            obs_source_skip_video_filter(m->ctx);
            return;
        }
        m->ultimoCuadro = cuadro;
        m->listo = true;
    }

    // 3) Final, directo en el destino.
    gs_texture_t* limpio = gs_texrender_get_texture(m->temporal[m->actual]);
    struct vec2 texel, medioTam;
    vec2_set(&texel, 1.0f / static_cast<float>(m->cx), 1.0f / static_cast<float>(m->cy));
    vec2_set(&medioTam, static_cast<float>((m->cx + 1) / 2), static_cast<float>((m->cy + 1) / 2));
    gs_effect_set_texture(m->pImage, limpio);
    gs_effect_set_texture(m->pMedio, gs_texrender_get_texture(m->medio));
    gs_effect_set_vec2(m->pTexel, &texel);
    gs_effect_set_vec2(m->pMedioTam, &medioTam);
    gs_effect_set_float(m->pColor, m->colorFuerza.load());
    gs_effect_set_float(m->pNitidez, m->nitidezFuerza.load());
    const bool medir = !m->finalMedida;
    if (medir) gs_timer_begin(m->timer[m->slot][kFinal]);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    while (gs_effect_loop(m->fx, "Final")) gs_draw_sprite(limpio, 0, m->cx, m->cy);
    gs_blend_state_pop();
    if (medir) {
        gs_timer_end(m->timer[m->slot][kFinal]);
        gs_timer_range_end(m->rango[m->slot]);
        m->enVuelo[m->slot] = true;
        m->finalMedida = true;
    }
}

} // namespace

extern "C" {

static struct obs_source_info ba_mejora_imagen_info = {};

/// Registra el filtro de video. La llama `obs_module_load` (ba_module.cpp).
void ba_register_video_filters(void) {
    ba_mejora_imagen_info.id = "ba_mejora_imagen";
    ba_mejora_imagen_info.type = OBS_SOURCE_TYPE_FILTER;
    ba_mejora_imagen_info.output_flags = OBS_SOURCE_VIDEO;
    ba_mejora_imagen_info.get_name = nombre;
    ba_mejora_imagen_info.create = crear;
    ba_mejora_imagen_info.destroy = destruir;
    ba_mejora_imagen_info.update = actualizar;
    ba_mejora_imagen_info.get_properties = propiedades;
    ba_mejora_imagen_info.get_defaults = defaults;
    ba_mejora_imagen_info.video_render = dibujar;
    obs_register_source(&ba_mejora_imagen_info);
}

} // extern "C"
