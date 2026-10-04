// ============================================================================
// src/obs-module/ba_ptz_digital.cpp — Filtro de video «PTZ digital»
//
// Proposito:
//   FILTRO `ba_ptz_digital`: pan, tilt y zoom sin mover la camara, recortando
//   y ampliando la imagen en la GPU (data/ptz-digital.effect, bicubica). Es el
//   PTZ que tiene CUALQUIER entrada de video; el modulo PTZ de la app
//   (src/app/ptz) lo maneja igual que a una camara motorizada:
//     - mover(vx, vy, vz): velocidades -1..1, como un joystick (0 = quieto).
//       Arranca y frena con una rampa corta, como una cabeza de verdad.
//     - ir(x, y, zoom, segundos): a una posicion (un preset), con una curva
//       suave; 0 segundos = corte.
//     - posicion -> x, y, zoom, moviendo: para guardar presets.
//   La posicion es el centro de la ventana (0..1 del cuadro) y el zoom (1 =
//   todo el cuadro, hasta kZoomMax). La ventana nunca sale del cuadro.
//
//   Sin zoom y centrado el filtro se saltea (obs_source_skip_video_filter):
//   tenerlo puesto en todas las entradas no cuesta nada.
//
// Modelo de threading:
//   `video_tick` y `video_render` corren en el hilo grafico; los proc
//   (`mover`, `ir`, `posicion`) los llama la app desde su hilo: el estado va
//   bajo un mutex (son unos pocos floats, sin contencion real).
//
// Estrategia de errores:
//   Sin el efecto (no compila o falta el archivo), PASS-THROUGH y un aviso en
//   el log.
// ============================================================================

#include <obs-module.h>
#include <obs.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <new>

namespace {

constexpr double kZoomMax = 8.0;
/// A velocidad 1, la ventana recorre medio ancho de ella misma por segundo.
constexpr double kPanPorSegundo = 0.5;
/// A velocidad 1, el zoom se duplica en ~1,4 s.
constexpr double kZoomPorSegundo = 0.5;
/// Rampa de arranque y frenado del joystick (constante de tiempo, s).
constexpr double kRampa = 0.12;
/// Por debajo de esta velocidad (ya imperceptible) se considera quieto: el
/// frenado tarda ~0,5 s.
constexpr double kQuieto = 0.02;

struct Ptz {
    obs_source_t* ctx = nullptr;
    gs_effect_t* fx = nullptr;
    gs_eparam_t* pTexel = nullptr;
    gs_eparam_t* pOrigen = nullptr;
    gs_eparam_t* pEscala = nullptr;

    std::mutex mu;
    double x = 0.5, y = 0.5, zoom = 1.0; ///< posicion actual
    double vx = 0, vy = 0, vz = 0;       ///< velocidad pedida (-1..1)
    double ax = 0, ay = 0, az = 0;       ///< velocidad aplicada (con rampa)
    bool yendo = false;                  ///< hacia un preset
    double desdeX = 0, desdeY = 0, desdeZ = 1, haciaX = 0, haciaY = 0, haciaZ = 1;
    double t = 0, duracion = 0;
};

void limitar(Ptz* p) {
    p->zoom = std::clamp(p->zoom, 1.0, kZoomMax);
    const double medio = 0.5 / p->zoom;
    p->x = std::clamp(p->x, medio, 1.0 - medio);
    p->y = std::clamp(p->y, medio, 1.0 - medio);
}

const char* nombre(void*) { return obs_module_text("PTZ digital (BA)"); }

// --- proc: lo que usa la app -------------------------------------------------

void procMover(void* data, calldata_t* cd) {
    auto* p = static_cast<Ptz*>(data);
    std::lock_guard<std::mutex> l(p->mu);
    p->vx = std::clamp(calldata_float(cd, "vx"), -1.0, 1.0);
    p->vy = std::clamp(calldata_float(cd, "vy"), -1.0, 1.0);
    p->vz = std::clamp(calldata_float(cd, "vz"), -1.0, 1.0);
    if (p->vx != 0 || p->vy != 0 || p->vz != 0) p->yendo = false; // el joystick manda
}

void procIr(void* data, calldata_t* cd) {
    auto* p = static_cast<Ptz*>(data);
    std::lock_guard<std::mutex> l(p->mu);
    p->vx = p->vy = p->vz = p->ax = p->ay = p->az = 0;
    const double z = std::clamp(calldata_float(cd, "zoom"), 1.0, kZoomMax);
    const double medio = 0.5 / z;
    p->haciaZ = z;
    p->haciaX = std::clamp(calldata_float(cd, "x"), medio, 1.0 - medio);
    p->haciaY = std::clamp(calldata_float(cd, "y"), medio, 1.0 - medio);
    p->duracion = std::max(0.0, calldata_float(cd, "segundos"));
    if (p->duracion <= 0.0) {
        p->x = p->haciaX;
        p->y = p->haciaY;
        p->zoom = p->haciaZ;
        p->yendo = false;
        return;
    }
    p->desdeX = p->x;
    p->desdeY = p->y;
    p->desdeZ = p->zoom;
    p->t = 0;
    p->yendo = true;
}

void procPosicion(void* data, calldata_t* cd) {
    auto* p = static_cast<Ptz*>(data);
    std::lock_guard<std::mutex> l(p->mu);
    calldata_set_float(cd, "x", p->x);
    calldata_set_float(cd, "y", p->y);
    calldata_set_float(cd, "zoom", p->zoom);
    calldata_set_bool(cd, "moviendo", p->yendo || p->ax != 0 || p->ay != 0 || p->az != 0);
}

// --- ciclo de libobs -------------------------------------------------------------

void* crear(obs_data_t*, obs_source_t* ctx) {
    auto* p = new (std::nothrow) Ptz();
    if (p == nullptr) return nullptr;
    p->ctx = ctx;
    char* ruta = obs_module_file("ptz-digital.effect");
    char* errores = nullptr;
    obs_enter_graphics();
    if (ruta != nullptr) p->fx = gs_effect_create_from_file(ruta, &errores);
    if (p->fx != nullptr) {
        p->pTexel = gs_effect_get_param_by_name(p->fx, "texel");
        p->pOrigen = gs_effect_get_param_by_name(p->fx, "origen");
        p->pEscala = gs_effect_get_param_by_name(p->fx, "escala");
    }
    obs_leave_graphics();
    if (p->fx == nullptr) {
        blog(LOG_WARNING, "[ba-sources] PTZ digital: no se pudo cargar %s (%s); la imagen pasa sin recortar",
             ruta != nullptr ? ruta : "ptz-digital.effect",
             errores != nullptr ? errores : "no esta el archivo");
    }
    bfree(errores);
    bfree(ruta);
    proc_handler_t* ph = obs_source_get_proc_handler(ctx);
    proc_handler_add(ph, "void mover(in float vx, in float vy, in float vz)", procMover, p);
    proc_handler_add(ph, "void ir(in float x, in float y, in float zoom, in float segundos)", procIr, p);
    proc_handler_add(ph, "void posicion(out float x, out float y, out float zoom, out bool moviendo)",
                     procPosicion, p);
    return p;
}

void destruir(void* data) {
    auto* p = static_cast<Ptz*>(data);
    obs_enter_graphics();
    gs_effect_destroy(p->fx);
    obs_leave_graphics();
    delete p;
}

/// Curva suave (arranca y llega despacio) para ir a un preset.
double suave(double u) { return u * u * (3.0 - 2.0 * u); }

void tick(void* data, float segundos) {
    auto* p = static_cast<Ptz*>(data);
    const double dt = std::clamp(static_cast<double>(segundos), 0.0, 0.1);
    std::lock_guard<std::mutex> l(p->mu);
    if (p->yendo) {
        p->t += dt;
        const double u = suave(std::min(1.0, p->t / p->duracion));
        // El zoom se interpola en escala logaritmica: se ve parejo.
        p->zoom = std::exp(std::log(p->desdeZ) + (std::log(p->haciaZ) - std::log(p->desdeZ)) * u);
        p->x = p->desdeX + (p->haciaX - p->desdeX) * u;
        p->y = p->desdeY + (p->haciaY - p->desdeY) * u;
        if (p->t >= p->duracion) p->yendo = false;
        limitar(p);
        return;
    }
    const double k = 1.0 - std::exp(-dt / kRampa);
    p->ax += (p->vx - p->ax) * k;
    p->ay += (p->vy - p->ay) * k;
    p->az += (p->vz - p->az) * k;
    if (std::fabs(p->ax) < kQuieto && p->vx == 0) p->ax = 0;
    if (std::fabs(p->ay) < kQuieto && p->vy == 0) p->ay = 0;
    if (std::fabs(p->az) < kQuieto && p->vz == 0) p->az = 0;
    p->zoom *= std::exp(p->az * kZoomPorSegundo * dt);
    p->x += p->ax * kPanPorSegundo * dt / p->zoom;
    p->y += p->ay * kPanPorSegundo * dt / p->zoom;
    limitar(p);
}

void dibujar(void* data, gs_effect_t*) {
    auto* p = static_cast<Ptz*>(data);
    obs_source_t* objetivo = obs_filter_get_target(p->ctx);
    const uint32_t cx = objetivo != nullptr ? obs_source_get_base_width(objetivo) : 0;
    const uint32_t cy = objetivo != nullptr ? obs_source_get_base_height(objetivo) : 0;
    double x, y, zoom;
    {
        std::lock_guard<std::mutex> l(p->mu);
        x = p->x;
        y = p->y;
        zoom = p->zoom;
    }
    if (p->fx == nullptr || cx == 0 || cy == 0 ||
        (zoom < 1.0005 && std::fabs(x - 0.5) < 1e-4 && std::fabs(y - 0.5) < 1e-4)) {
        obs_source_skip_video_filter(p->ctx);
        return;
    }
    if (!obs_source_process_filter_begin(p->ctx, GS_RGBA, OBS_NO_DIRECT_RENDERING)) return;
    struct vec2 texel, origen, escala;
    vec2_set(&texel, 1.0f / static_cast<float>(cx), 1.0f / static_cast<float>(cy));
    vec2_set(&escala, static_cast<float>(1.0 / zoom), static_cast<float>(1.0 / zoom));
    vec2_set(&origen, static_cast<float>(x - 0.5 / zoom), static_cast<float>(y - 0.5 / zoom));
    gs_effect_set_vec2(p->pTexel, &texel);
    gs_effect_set_vec2(p->pOrigen, &origen);
    gs_effect_set_vec2(p->pEscala, &escala);
    obs_source_process_filter_tech_end(p->ctx, p->fx, cx, cy, "Recorte");
}

} // namespace

extern "C" {

static struct obs_source_info ba_ptz_digital_info = {};

/// Registra el filtro. La llama `ba_register_video_filters` (ba_mejora_imagen.cpp).
void ba_register_ptz_digital(void) {
    ba_ptz_digital_info.id = "ba_ptz_digital";
    ba_ptz_digital_info.type = OBS_SOURCE_TYPE_FILTER;
    ba_ptz_digital_info.output_flags = OBS_SOURCE_VIDEO;
    ba_ptz_digital_info.get_name = nombre;
    ba_ptz_digital_info.create = crear;
    ba_ptz_digital_info.destroy = destruir;
    ba_ptz_digital_info.video_tick = tick;
    ba_ptz_digital_info.video_render = dibujar;
    obs_register_source(&ba_ptz_digital_info);
}

} // extern "C"
