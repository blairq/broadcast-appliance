// ============================================================================
// src/obs-module/tests/test_mejora_imagen.cpp — los filtros de video en la GPU real
//
// «Mejorar imagen» (ba_mejora_imagen) y el PTZ digital (ba_ptz_digital).
//
// Proposito:
//   Correr el filtro `ba_mejora_imagen` dentro de una libobs real, con su
//   modulo grafico (OpenGL en macOS/Linux, D3D11 en Windows): asi se prueba
//   que el .effect COMPILA en ese backend (libobs lo traduce distinto en cada
//   uno) y que cada pasada hace lo que tiene que hacer:
//     1. Ruido temporal: una imagen quieta con ruido queda mas limpia.
//     2. Movimiento: un cambio grande pasa enseguida (sin estela).
//     3. Color: un color corrido sobre una zona de brillo parejo se va.
//     4. Nitidez: un borde queda con mas contraste.
//
//   Con argumentos es una herramienta: pasa un clip RGBA crudo por el filtro
//   real y mide cuanto tarda (lo que se uso para ajustar el filtro):
//     test_mejora_imagen <entrada.rgba> <ancho> <alto> <cuadros> <salida.rgba>
//                        [ruido color nitidez (0-100)]
//
// Sin display (un CI headless) no hay contexto grafico: sale con 77 (SKIP).
// ============================================================================

#include <obs.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

// --- Una fuente de prueba que dibuja una textura que el test va cambiando ----
std::uint32_t g_w = 64, g_h = 64;
gs_texture_t* g_tex = nullptr;

const char* pruebaNombre(void*) { return "prueba"; }
void* pruebaCrear(obs_data_t*, obs_source_t*) { return &g_tex; }
void pruebaDestruir(void*) {}
std::uint32_t pruebaAncho(void*) { return g_w; }
std::uint32_t pruebaAlto(void*) { return g_h; }
void pruebaDibujar(void*, gs_effect_t*) {
    if (g_tex == nullptr) return;
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), g_tex);
    while (gs_effect_loop(fx, "Draw"))
        gs_draw_sprite(g_tex, 0, g_w, g_h);
}

/// Cuadro RGBA (bytes) -> la textura que dibuja la fuente.
void cargar(const std::vector<std::uint8_t>& rgba) {
    obs_enter_graphics();
    if (g_tex == nullptr) {
        g_tex = gs_texture_create(g_w, g_h, GS_RGBA, 1, nullptr, GS_DYNAMIC);
    }
    gs_texture_set_image(g_tex, rgba.data(), g_w * 4, false);
    obs_leave_graphics();
}

/// Espera a que libobs pase al cuadro siguiente: el filtro procesa una vez por cuadro.
void siguienteCuadro() {
    const std::uint64_t t = obs_get_video_frame_time();
    for (int i = 0; i < 200 && obs_get_video_frame_time() == t; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

/// Dibuja la fuente (con sus filtros) y baja el resultado RGBA. Devuelve los ms de GPU+bajada.
double dibujar(obs_source_t* src, std::vector<std::uint8_t>& salida) {
    salida.assign(static_cast<std::size_t>(g_w) * g_h * 4, 0);
    obs_enter_graphics();
    const auto t0 = std::chrono::steady_clock::now();
    gs_texrender_t* tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t* st = gs_stagesurface_create(g_w, g_h, GS_RGBA);
    if (gs_texrender_begin(tr, g_w, g_h)) {
        struct vec4 negro;
        vec4_zero(&negro);
        gs_clear(GS_CLEAR_COLOR, &negro, 0.0f, 0);
        gs_ortho(0.0f, static_cast<float>(g_w), 0.0f, static_cast<float>(g_h), -100.0f, 100.0f);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_video_render(src);
        gs_blend_state_pop();
        gs_texrender_end(tr);
        gs_stage_texture(st, gs_texrender_get_texture(tr));
        std::uint8_t* datos = nullptr;
        std::uint32_t paso = 0;
        if (gs_stagesurface_map(st, &datos, &paso)) {
            for (std::uint32_t y = 0; y < g_h; ++y) {
                std::memcpy(&salida[static_cast<std::size_t>(y) * g_w * 4],
                            datos + static_cast<std::size_t>(y) * paso, static_cast<std::size_t>(g_w) * 4);
            }
            gs_stagesurface_unmap(st);
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    gs_stagesurface_destroy(st);
    gs_texrender_destroy(tr);
    obs_leave_graphics();
    return ms;
}

obs_source_t* conFiltro(obs_source_t* fuente, int ruido, int color, int nitidez) {
    obs_data_t* s = obs_data_create();
    obs_data_set_int(s, "ruido", ruido);
    obs_data_set_int(s, "color", color);
    obs_data_set_int(s, "nitidez", nitidez);
    obs_source_t* f = obs_source_create_private("ba_mejora_imagen", "mejora", s);
    obs_data_release(s);
    if (f != nullptr) obs_source_filter_add(fuente, f);
    return f;
}

void sacar(obs_source_t* fuente, obs_source_t* f) {
    obs_source_filter_remove(fuente, f);
    obs_source_release(f);
}

double px(const std::vector<std::uint8_t>& im, std::uint32_t x, std::uint32_t y, int c) {
    return im[(static_cast<std::size_t>(y) * g_w + x) * 4 + c] / 255.0;
}

void llenar(std::vector<std::uint8_t>& im, std::uint32_t x, std::uint32_t y, double r, double g, double b) {
    auto a = [](double v) {
        return static_cast<std::uint8_t>(std::lround(std::fmin(1.0, std::fmax(0.0, v)) * 255));
    };
    std::uint8_t* p = &im[(static_cast<std::size_t>(y) * g_w + x) * 4];
    p[0] = a(r);
    p[1] = a(g);
    p[2] = a(b);
    p[3] = 255;
}

// --- Las pruebas ---------------------------------------------------------------

void pruebaFuente(obs_source_t* fuente) {
    std::vector<std::uint8_t> im(static_cast<std::size_t>(g_w) * g_h * 4), sal;
    for (std::uint32_t y = 0; y < g_h; ++y)
        for (std::uint32_t x = 0; x < g_w; ++x)
            llenar(im, x, y, 0.2, 0.4, 0.6);
    cargar(im);
    siguienteCuadro();
    dibujar(fuente, sal);
    std::printf("sin filtro: %.3f %.3f %.3f (era 0.2 0.4 0.6)\n", px(sal, 8, 8, 0), px(sal, 8, 8, 1),
                px(sal, 8, 8, 2));
    CHECK(std::fabs(px(sal, 8, 8, 1) - 0.4) < 0.01, "la fuente de prueba se dibuja (sin el filtro)");
}

void pruebaRuido(obs_source_t* fuente) {
    obs_source_t* f = conFiltro(fuente, 100, 0, 0);
    CHECK(f != nullptr, "se crea el filtro");
    std::mt19937 rng(7);
    std::normal_distribution<double> ruido(0.0, 0.06);
    std::vector<std::uint8_t> im(static_cast<std::size_t>(g_w) * g_h * 4), sal;
    double varEnt = 0, varSal = 0;
    int n = 0;
    for (int cuadro = 0; cuadro < 20; ++cuadro) {
        for (std::uint32_t y = 0; y < g_h; ++y)
            for (std::uint32_t x = 0; x < g_w; ++x) {
                const double v = 0.5 + ruido(rng);
                llenar(im, x, y, v, v, v);
            }
        cargar(im);
        siguienteCuadro();
        dibujar(fuente, sal);
        if (cuadro >= 10) { // ya asentado
            for (std::uint32_t y = 4; y < g_h - 4; ++y)
                for (std::uint32_t x = 4; x < g_w - 4; ++x) {
                    varEnt += std::pow(px(im, x, y, 1) - 0.5, 2);
                    varSal += std::pow(px(sal, x, y, 1) - 0.5, 2);
                    ++n;
                }
        }
    }
    const double dEnt = std::sqrt(varEnt / n), dSal = std::sqrt(varSal / n);
    std::printf("ruido: desvio de entrada %.4f, de salida %.4f (%.0f%%)\n", dEnt, dSal, 100 * dSal / dEnt);
    CHECK(dSal < 0.7 * dEnt, "el ruido temporal limpia una imagen quieta");

    // Movimiento: todo pasa a blanco de golpe -> la salida sigue enseguida.
    for (std::uint32_t y = 0; y < g_h; ++y)
        for (std::uint32_t x = 0; x < g_w; ++x)
            llenar(im, x, y, 1, 1, 1);
    cargar(im);
    siguienteCuadro();
    dibujar(fuente, sal);
    std::printf("movimiento: tras el cambio el centro vale %.3f (era 0.5, ahora 1.0)\n",
                px(sal, g_w / 2, g_h / 2, 1));
    CHECK(px(sal, g_w / 2, g_h / 2, 1) > 0.95, "un cambio grande no deja estela");
    sacar(fuente, f);
}

void pruebaColor(obs_source_t* fuente) {
    obs_source_t* f = conFiltro(fuente, 0, 100, 0);
    // Izquierda: rojo oscuro (el LED). Derecha: blanco de brillo 0.8, con una
    // columna pegada al borde del MISMO brillo pero teñida (el color corrido).
    std::vector<std::uint8_t> im(static_cast<std::size_t>(g_w) * g_h * 4), sal;
    const std::uint32_t borde = g_w / 2;
    for (std::uint32_t y = 0; y < g_h; ++y)
        for (std::uint32_t x = 0; x < g_w; ++x) {
            if (x < borde)
                llenar(im, x, y, 0.6, 0.1, 0.1);
            else if (x == borde)
                llenar(im, x, y, 0.9, 0.77, 0.77);
            else
                llenar(im, x, y, 0.8, 0.8, 0.8);
        }
    cargar(im);
    siguienteCuadro();
    dibujar(fuente, sal);
    const double antes = px(im, borde, g_h / 2, 0) - px(im, borde, g_h / 2, 1);
    const double despues = px(sal, borde, g_h / 2, 0) - px(sal, borde, g_h / 2, 1);
    std::printf("color: tinte rojo de la columna corrida %.3f -> %.3f; el rojo del LED %.3f -> %.3f\n", antes,
                despues, px(im, 4, g_h / 2, 0), px(sal, 4, g_h / 2, 0));
    // Una columna de UN pixel es el peor caso para el color a media resolucion
    // (comparte bloque 2x2 con una blanca); lo real son varios pixeles.
    CHECK(despues < 0.6 * antes, "el color corrido sobre un brillo parejo baja al menos un 40%");
    CHECK(std::fabs(px(sal, 4, g_h / 2, 0) - 0.6) < 0.03, "el rojo del LED (lejos del borde) queda igual");
    sacar(fuente, f);
}

void pruebaNitidez(obs_source_t* fuente) {
    obs_source_t* f = conFiltro(fuente, 0, 0, 100);
    std::vector<std::uint8_t> im(static_cast<std::size_t>(g_w) * g_h * 4), sal;
    const std::uint32_t borde = g_w / 2;
    for (std::uint32_t y = 0; y < g_h; ++y)
        for (std::uint32_t x = 0; x < g_w; ++x) {
            const double v = x < borde ? 0.3 : 0.7;
            llenar(im, x, y, v, v, v);
        }
    cargar(im);
    siguienteCuadro();
    dibujar(fuente, sal);
    std::printf("nitidez: junto al borde %.3f | %.3f (era 0.300 | 0.700)\n", px(sal, borde - 1, g_h / 2, 1),
                px(sal, borde, g_h / 2, 1));
    CHECK(px(sal, borde, g_h / 2, 1) > 0.75 && px(sal, borde - 1, g_h / 2, 1) < 0.25,
          "el borde gana contraste");
    CHECK(std::fabs(px(sal, 4, g_h / 2, 1) - 0.3) < 0.01, "lo plano queda igual");
    sacar(fuente, f);
}

// --- PTZ digital -----------------------------------------------------------------

struct Pos {
    double x, y, zoom;
    bool moviendo;
};

Pos posicion(obs_source_t* f) {
    calldata_t cd;
    calldata_init(&cd);
    proc_handler_call(obs_source_get_proc_handler(f), "posicion", &cd);
    Pos p{calldata_float(&cd, "x"), calldata_float(&cd, "y"), calldata_float(&cd, "zoom"),
          calldata_bool(&cd, "moviendo")};
    calldata_free(&cd);
    return p;
}

void ir(obs_source_t* f, double x, double y, double zoom, double segundos) {
    calldata_t cd;
    calldata_init(&cd);
    calldata_set_float(&cd, "x", x);
    calldata_set_float(&cd, "y", y);
    calldata_set_float(&cd, "zoom", zoom);
    calldata_set_float(&cd, "segundos", segundos);
    proc_handler_call(obs_source_get_proc_handler(f), "ir", &cd);
    calldata_free(&cd);
}

void mover(obs_source_t* f, double vx, double vy, double vz) {
    calldata_t cd;
    calldata_init(&cd);
    calldata_set_float(&cd, "vx", vx);
    calldata_set_float(&cd, "vy", vy);
    calldata_set_float(&cd, "vz", vz);
    proc_handler_call(obs_source_get_proc_handler(f), "mover", &cd);
    calldata_free(&cd);
}

void pruebaPtzDigital(obs_source_t* fuente) {
    obs_source_t* f = obs_source_create_private("ba_ptz_digital", "ptz", nullptr);
    CHECK(f != nullptr, "se crea el PTZ digital");
    if (f == nullptr) return;
    obs_source_filter_add(fuente, f);
    // Mitad izquierda oscura, mitad derecha clara.
    std::vector<std::uint8_t> im(static_cast<std::size_t>(g_w) * g_h * 4), sal;
    for (std::uint32_t y = 0; y < g_h; ++y)
        for (std::uint32_t x = 0; x < g_w; ++x) {
            const double v = x < g_w / 2 ? 0.2 : 0.8;
            llenar(im, x, y, v, v, v);
        }
    cargar(im);
    siguienteCuadro();
    dibujar(fuente, sal);
    CHECK(std::fabs(px(sal, g_w / 4, g_h / 2, 1) - 0.2) < 0.01, "sin zoom la imagen pasa igual");

    // Zoom 2 centrado en x = 0.75: se ve solo la mitad clara.
    ir(f, 0.75, 0.5, 2.0, 0.0);
    siguienteCuadro();
    dibujar(fuente, sal);
    std::printf(
        "ptz digital: con zoom 2 a la derecha, x=1/8 vale %.3f, x=7/8 vale %.3f (la mitad clara: 0.8)\n",
        px(sal, g_w / 8, g_h / 2, 1), px(sal, g_w * 7 / 8, g_h / 2, 1));
    CHECK(px(sal, g_w / 8, g_h / 2, 1) > 0.75 && px(sal, g_w * 7 / 8, g_h / 2, 1) > 0.75,
          "el zoom recorta la ventana pedida");

    // La ventana no sale del cuadro.
    ir(f, 0.05, 0.5, 2.0, 0.0);
    Pos p = posicion(f);
    CHECK(std::fabs(p.x - 0.25) < 1e-6, "la ventana queda dentro del cuadro");

    // El joystick: a velocidad 1 hacia la derecha avanza, con la rampa.
    ir(f, 0.5, 0.5, 2.0, 0.0);
    mover(f, 1.0, 0.0, 0.0);
    for (int i = 0; i < 30; ++i)
        siguienteCuadro(); // ~0,5 s a 60 fps
    p = posicion(f);
    std::printf("ptz digital: tras ~0,5 s a velocidad 1, x = %.3f (salio de 0.5), moviendo = %d\n", p.x,
                p.moviendo);
    CHECK(p.x > 0.53 && p.moviendo, "a velocidad 1 la ventana se mueve");
    mover(f, 0.0, 0.0, 0.0);
    for (int i = 0; i < 40; ++i)
        siguienteCuadro();
    CHECK(!posicion(f).moviendo, "al soltar el joystick frena");

    // Ir a un preset en 0,3 s: a mitad de camino esta entre medio, al final llega.
    ir(f, 0.5, 0.5, 1.0, 0.3);
    for (int i = 0; i < 40; ++i)
        siguienteCuadro();
    p = posicion(f);
    CHECK(std::fabs(p.zoom - 1.0) < 1e-6 && !p.moviendo, "llega al preset");
    sacar(fuente, f);
}

/// Modo herramienta: un clip crudo por el filtro real.
double mediana(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

/// Modo herramienta: un clip crudo por el filtro real. Mide (mediana) dibujar
/// y bajar cada cuadro con el filtro y sin el: la diferencia es el filtro.
int clip(obs_source_t* fuente, int argc, char** argv) {
    const char* entrada = argv[1];
    const int cuadros = std::atoi(argv[4]);
    const char* salidaRuta = argv[5];
    const int r = argc > 6 ? std::atoi(argv[6]) : 60, c = argc > 7 ? std::atoi(argv[7]) : 70,
              n = argc > 8 ? std::atoi(argv[8]) : 50;
    obs_source_t* f = conFiltro(fuente, r, c, n);
    FILE* fe = std::fopen(entrada, "rb");
    FILE* fs = std::fopen(salidaRuta, "wb");
    if (fe == nullptr || fs == nullptr || f == nullptr) {
        std::printf("no se pudo abrir el clip o crear el filtro\n");
        return 1;
    }
    std::vector<std::vector<std::uint8_t>> clip;
    std::vector<std::uint8_t> im(static_cast<std::size_t>(g_w) * g_h * 4), sal;
    while (static_cast<int>(clip.size()) < cuadros && std::fread(im.data(), 1, im.size(), fe) == im.size()) {
        clip.push_back(im);
    }
    std::fclose(fe);
    std::vector<double> con, sin;
    for (const auto& cuadro : clip) {
        cargar(cuadro);
        siguienteCuadro();
        con.push_back(dibujar(fuente, sal));
        std::fwrite(sal.data(), 1, sal.size(), fs);
    }
    std::fclose(fs);
    // Lo que midio la GPU, pasada por pasada (proc `tiempos` del filtro; en
    // 0 si el driver no implementa las consultas de tiempo).
    calldata_t cd;
    calldata_init(&cd);
    if (proc_handler_call(obs_source_get_proc_handler(f), "tiempos", &cd)) {
        std::printf("GPU por pasada: temporal %.3f ms, color %.3f ms, final %.3f ms\n",
                    calldata_float(&cd, "temporal"), calldata_float(&cd, "color"),
                    calldata_float(&cd, "final"));
    }
    calldata_free(&cd);
    obs_source_set_enabled(f, false);
    for (const auto& cuadro : clip) {
        cargar(cuadro);
        siguienteCuadro();
        sin.push_back(dibujar(fuente, sal));
    }
    const double mc = mediana(con), ms = mediana(sin);
    std::printf("%zu cuadros %ux%u (mediana, dibujar + bajar): %.2f ms con el filtro, %.2f ms sin el -> "
                "filtro %.2f ms\n",
                clip.size(), g_w, g_h, mc, ms, mc - ms);
    sacar(fuente, f);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const bool modoClip = argc >= 6;
    if (modoClip) {
        g_w = static_cast<std::uint32_t>(std::atoi(argv[2]));
        g_h = static_cast<std::uint32_t>(std::atoi(argv[3]));
    }
    if (!obs_startup("en-US", nullptr, nullptr)) {
        std::printf("SKIP: obs_startup fallo (sesion sin plataforma grafica)\n");
        return 77;
    }
    obs_add_data_path(BA_TEST_OBS_DATA_DIR "/libobs/");

    struct obs_video_info ovi = {};
    ovi.graphics_module = BA_TEST_GRAPHICS_MODULE;
    ovi.fps_num = 60;
    ovi.fps_den = 1;
    ovi.base_width = ovi.output_width = g_w;
    ovi.base_height = ovi.output_height = g_h;
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_PARTIAL;
    ovi.gpu_conversion = true;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    const int rc = obs_reset_video(&ovi);
    if (rc != OBS_VIDEO_SUCCESS) {
        std::printf("SKIP: obs_reset_video fallo (%d): no hay contexto grafico\n", rc);
        obs_shutdown();
        return 77;
    }

    obs_add_module_path(BA_SOURCES_MODULE_DIR, BA_SOURCES_MODULE_DIR "/%module%");
    obs_load_all_modules();
    obs_post_load_modules();

    struct obs_source_info prueba = {};
    prueba.id = "ba_prueba_cuadros";
    prueba.type = OBS_SOURCE_TYPE_INPUT;
    prueba.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW; // dibuja con su propio efecto
    prueba.get_name = pruebaNombre;
    prueba.create = pruebaCrear;
    prueba.destroy = pruebaDestruir;
    prueba.get_width = pruebaAncho;
    prueba.get_height = pruebaAlto;
    prueba.video_render = pruebaDibujar;
    obs_register_source(&prueba);
    obs_source_t* fuente = obs_source_create_private("ba_prueba_cuadros", "cuadros", nullptr);

    int rcFinal = 0;
    if (modoClip) {
        rcFinal = clip(fuente, argc, argv);
    } else {
        pruebaFuente(fuente);
        pruebaRuido(fuente);
        pruebaColor(fuente);
        pruebaNitidez(fuente);
        pruebaPtzDigital(fuente);
        rcFinal = g_failures == 0 ? 0 : 1;
        std::printf(g_failures == 0 ? "OK: los filtros de video andan en la GPU\n" : "%d fallos\n",
                    g_failures);
    }

    obs_source_release(fuente);
    obs_enter_graphics();
    gs_texture_destroy(g_tex);
    obs_leave_graphics();
    obs_shutdown();
    return rcFinal;
}
