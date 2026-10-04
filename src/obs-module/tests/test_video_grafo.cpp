// ============================================================================
// src/obs-module/tests/test_video_grafo.cpp — el grafo de video en la GPU real
//
// El filtro `ba_video_grafo` y la fuente `ba_video_salida`, dentro de una
// libobs real con su modulo grafico (OpenGL en macOS/Linux, D3D11 en
// Windows): asi se prueba que video-grafo.effect COMPILA en ese backend y que
// cada tarjeta hace lo suyo:
//   1. in -> out: la imagen pasa igual.
//   2. color: saturacion 0 deja gris.
//   3. croma: el fondo verde se va (alfa 0) y lo rojo queda.
//   4. recorte y espejo.
//   5. una rama: la escena con croma y una SALIDA con la imagen original.
//   6. una salida sin que nadie dibuje su entrada: la despierta ella.
//   7. `out` sin nada conectado: negro.
//
// Sin display (un CI headless) no hay contexto grafico: sale con 77 (SKIP).
// ============================================================================

#include <obs.h>
#if defined(BA_TEST_GST_RAIZ)
#include <gst/gst.h>
#endif
#include <util/platform.h>

#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

constexpr std::uint32_t W = 64, H = 64;
gs_texture_t* g_tex = nullptr;

const char* pruebaNombre(void*) { return "prueba"; }
void* pruebaCrear(obs_data_t*, obs_source_t*) { return &g_tex; }
void pruebaDestruir(void*) {}
std::uint32_t pruebaAncho(void*) { return W; }
std::uint32_t pruebaAlto(void*) { return H; }
void pruebaDibujar(void*, gs_effect_t*) {
    if (g_tex == nullptr) return;
    gs_effect_t* fx = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), g_tex);
    while (gs_effect_loop(fx, "Draw"))
        gs_draw_sprite(g_tex, 0, W, H);
}

/// Mitad izquierda verde (fondo), un cuadrado rojo en el medio, mitad derecha gris claro.
void cargarImagen() {
    std::vector<std::uint8_t> im(static_cast<std::size_t>(W) * H * 4);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x) {
            std::uint8_t* p = &im[(static_cast<std::size_t>(y) * W + x) * 4];
            const bool cuadrado = x >= 12 && x < 20 && y >= 28 && y < 36;
            if (cuadrado) {
                p[0] = 220, p[1] = 30, p[2] = 30;
            } else if (x < W / 2) {
                p[0] = 0, p[1] = 177, p[2] = 64; // verde croma
            } else {
                p[0] = 200, p[1] = 200, p[2] = 200;
            }
            p[3] = 255;
        }
    obs_enter_graphics();
    if (g_tex == nullptr) g_tex = gs_texture_create(W, H, GS_RGBA, 1, nullptr, GS_DYNAMIC);
    gs_texture_set_image(g_tex, im.data(), W * 4, false);
    obs_leave_graphics();
}

void siguienteCuadro() {
    const std::uint64_t t = obs_get_video_frame_time();
    for (int i = 0; i < 200 && obs_get_video_frame_time() == t; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

/// Dibuja la fuente (con sus filtros) sobre negro transparente y baja el RGBA.
std::vector<std::uint8_t> dibujar(obs_source_t* src) {
    std::vector<std::uint8_t> sal(static_cast<std::size_t>(W) * H * 4, 0);
    obs_enter_graphics();
    gs_texrender_t* tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t* st = gs_stagesurface_create(W, H, GS_RGBA);
    if (gs_texrender_begin(tr, W, H)) {
        struct vec4 nada;
        vec4_zero(&nada);
        gs_clear(GS_CLEAR_COLOR, &nada, 0.0f, 0);
        gs_ortho(0.0f, static_cast<float>(W), 0.0f, static_cast<float>(H), -100.0f, 100.0f);
        gs_blend_state_push();
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        obs_source_video_render(src);
        gs_blend_state_pop();
        gs_texrender_end(tr);
        gs_stage_texture(st, gs_texrender_get_texture(tr));
        std::uint8_t* datos = nullptr;
        std::uint32_t paso = 0;
        if (gs_stagesurface_map(st, &datos, &paso)) {
            for (std::uint32_t y = 0; y < H; ++y)
                std::memcpy(&sal[static_cast<std::size_t>(y) * W * 4],
                            datos + static_cast<std::size_t>(y) * paso, W * 4);
            gs_stagesurface_unmap(st);
        }
    }
    gs_stagesurface_destroy(st);
    gs_texrender_destroy(tr);
    obs_leave_graphics();
    return sal;
}

int px(const std::vector<std::uint8_t>& im, std::uint32_t x, std::uint32_t y, int c) {
    return im[(static_cast<std::size_t>(y) * W + x) * 4 + c];
}

void ponerGrafo(obs_source_t* f, const std::string& json) {
    obs_data_t* s = obs_data_create();
    obs_data_set_string(s, "grafo", json.c_str());
    obs_source_update(f, s);
    obs_data_release(s);
    siguienteCuadro();
}

/// Un grafo lineal in -> [tarjeta] -> out.
std::string lineal(const std::string& tipo, const std::string& params) {
    return R"({"nodos":[{"id":"in","tipo":"in"},{"id":"t","tipo":")" + tipo + R"(","params":)" + params +
           R"(},{"id":"out","tipo":"out"}],"cables":[{"desde":"in","hacia":"t"},{"desde":"t","hacia":"out"}]})";
}

} // namespace

int main() {
    if (!obs_startup("en-US", nullptr, nullptr)) {
        std::printf("SKIP: obs_startup fallo (sesion sin plataforma grafica)\n");
        return 77;
    }
    obs_add_data_path(BA_TEST_OBS_DATA_DIR "/libobs/");
    struct obs_video_info ovi = {};
    ovi.graphics_module = BA_TEST_GRAPHICS_MODULE;
    ovi.fps_num = 60;
    ovi.fps_den = 1;
    ovi.base_width = ovi.output_width = W;
    ovi.base_height = ovi.output_height = H;
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
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, TRUE); // el GStreamer vendoreado (extern/)
#endif
#if defined(BA_TEST_OBS_FILTERS)
    // obs-filters (el de OBS, vendoreado), para la tarjeta de un filtro de OBS.
    obs_module_t* filtros = nullptr;
    if (obs_open_module(&filtros, BA_TEST_OBS_FILTERS "/Contents/MacOS/obs-filters",
                        BA_TEST_OBS_FILTERS "/Contents/Resources") == MODULE_SUCCESS)
        obs_init_module(filtros);
#endif
    obs_load_all_modules();
    obs_post_load_modules();

    struct obs_source_info prueba = {};
    prueba.id = "ba_prueba_cuadros";
    prueba.type = OBS_SOURCE_TYPE_INPUT;
    prueba.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    prueba.get_name = pruebaNombre;
    prueba.create = pruebaCrear;
    prueba.destroy = pruebaDestruir;
    prueba.get_width = pruebaAncho;
    prueba.get_height = pruebaAlto;
    prueba.video_render = pruebaDibujar;
    obs_register_source(&prueba);
    // Publica: la salida la busca por nombre.
    obs_source_t* fuente = obs_source_create("ba_prueba_cuadros", "cuadros", nullptr, nullptr);
    cargarImagen();

    obs_source_t* f = obs_source_create_private("ba_video_grafo", "grafo", nullptr);
    CHECK(f != nullptr, "existe el filtro ba_video_grafo");
    if (f == nullptr) return 1;
    obs_source_filter_add(fuente, f);

    // 1. in -> out
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"out","tipo":"out"}],"cables":[{"desde":"in","hacia":"out"}]})");
    auto im = dibujar(fuente);
    CHECK(px(im, 50, 10, 0) == 200 && px(im, 5, 5, 1) == 177 && px(im, 15, 30, 0) == 220,
          "in -> out: la imagen pasa igual");

    // 2. color: saturacion 0
    ponerGrafo(f, lineal("color", R"({"saturacion":0})"));
    im = dibujar(fuente);
    std::printf("   color: el verde quedo en %d %d %d\n", px(im, 5, 5, 0), px(im, 5, 5, 1), px(im, 5, 5, 2));
    CHECK(std::abs(px(im, 5, 5, 0) - px(im, 5, 5, 1)) <= 2 &&
              std::abs(px(im, 5, 5, 1) - px(im, 5, 5, 2)) <= 2,
          "color: con saturacion 0 queda gris");
    ponerGrafo(f, lineal("color", R"({"brillo":0.2})"));
    im = dibujar(fuente);
    CHECK(px(im, 50, 10, 0) > 230, "color: brillo +0,2 aclara");

    // 3. croma (verde de fabrica)
    ponerGrafo(f, lineal("croma", "{}"));
    im = dibujar(fuente);
    std::printf("   croma: fondo alfa %d, rojo alfa %d (%d,%d,%d), gris alfa %d\n", px(im, 5, 5, 3),
                px(im, 15, 31, 3), px(im, 15, 31, 0), px(im, 15, 31, 1), px(im, 15, 31, 2),
                px(im, 50, 10, 3));
    CHECK(px(im, 5, 5, 3) < 10, "croma: el fondo verde se va");
    CHECK(px(im, 15, 31, 3) > 245 && px(im, 15, 31, 0) > 180, "croma: lo rojo queda");
    CHECK(px(im, 50, 10, 3) > 245, "croma: lo gris queda");

    // 4. recorte y espejo
    ponerGrafo(f, lineal("recorte", R"({"izquierda":0.5})"));
    im = dibujar(fuente);
    CHECK(px(im, 5, 5, 3) == 0 && px(im, 50, 10, 0) == 200, "recorte: la mitad izquierda se va");
    ponerGrafo(f, lineal("recorte", R"({"espejoH":true})"));
    im = dibujar(fuente);
    CHECK(px(im, 5, 5, 0) == 200 && px(im, 60, 5, 1) == 177, "espejo: izquierda y derecha se dan vuelta");

    // 5. rama: la escena con croma, la salida «S» con lo original
    obs_data_t* ss = obs_data_create();
    obs_data_set_string(ss, "fuente", "cuadros");
    obs_source_t* salida = obs_source_create("ba_video_salida", "S", ss, nullptr);
    obs_data_release(ss);
    CHECK(salida != nullptr, "existe la fuente ba_video_salida");
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"c","tipo":"croma"},{"id":"s","tipo":"salida","params":{"nombre":"S"}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"c"},{"desde":"c","hacia":"out"},{"desde":"in","hacia":"s"}]})");
    im = dibujar(fuente);
    auto is = dibujar(salida);
    CHECK(px(im, 5, 5, 3) < 10, "rama: la escena tiene el croma");
    CHECK(px(is, 5, 5, 3) == 255 && px(is, 5, 5, 1) == 177, "rama: la salida tiene la imagen original");

    // 6. la salida sola (nadie dibuja la entrada en este cuadro): la despierta
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"col","tipo":"color","params":{"saturacion":0}},{"id":"s","tipo":"salida","params":{"nombre":"S"}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"out"},{"desde":"in","hacia":"col"},{"desde":"col","hacia":"s"}]})");
    siguienteCuadro();
    is = dibujar(salida);
    std::printf("   salida sola: verde -> %d %d %d\n", px(is, 5, 5, 0), px(is, 5, 5, 1), px(is, 5, 5, 2));
    CHECK(px(is, 5, 5, 3) == 255 && std::abs(px(is, 5, 5, 0) - px(is, 5, 5, 1)) <= 2,
          "salida sola: despierta a su entrada y muestra el gris");

    // 8. una tarjeta que corre un filtro de OBS: el PTZ digital (zoom 2 a la
    //    derecha: la mitad derecha, gris, llena todo).
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"ptz","tipo":"obs","params":{"filtro":"ba_ptz_digital","ajustes":{}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"ptz"},{"desde":"ptz","hacia":"out"}]})");
    {
        // El filtro de la tarjeta es privado: se lo mueve por su proc, como el PTZ de la app.
        im = dibujar(fuente); // crea la instancia
        CHECK(px(im, 50, 10, 0) == 200 && px(im, 5, 5, 1) == 177,
              "tarjeta PTZ sin zoom: la imagen pasa igual");
    }
    // Cambiar los ajustes NO recrea el filtro (sigue el mismo, con su estado).
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"ptz","tipo":"obs","params":{"filtro":"ba_ptz_digital","ajustes":{"x":1}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"ptz"},{"desde":"ptz","hacia":"out"}]})");
    im = dibujar(fuente);
    CHECK(px(im, 50, 10, 0) == 200, "tarjeta PTZ: un ajuste nuevo no la rompe");
    // Un filtro que no existe: la tarjeta no se calcula y `out` queda sin nada.
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"x","tipo":"obs","params":{"filtro":"no_existe"}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"x"},{"desde":"x","hacia":"out"}]})");
    im = dibujar(fuente);
    CHECK(px(im, 50, 10, 3) == 0, "un filtro que no existe: no se dibuja (y no se cae)");
#if defined(BA_TEST_OBS_FILTERS)
    // 9. un filtro de obs-filters de verdad: «Color Correction», saturación -1 = gris.
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"cc","tipo":"obs","params":{"filtro":"color_filter_v2","ajustes":{"saturation":-1.0}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"cc"},{"desde":"cc","hacia":"out"}]})");
    for (int i = 0; i < 4; ++i) { // el plan nuevo entra en el próximo tic de video
        im = dibujar(fuente);
        siguienteCuadro();
    }
    std::printf("   color_filter_v2: el verde quedo en %d %d %d (alfa %d)\n", px(im, 5, 5, 0),
                px(im, 5, 5, 1), px(im, 5, 5, 2), px(im, 5, 5, 3));
    CHECK(px(im, 5, 5, 3) > 245 && std::abs(px(im, 5, 5, 0) - px(im, 5, 5, 1)) <= 3,
          "tarjeta color_filter_v2 (obs-filters): saturacion -1 deja gris");
#endif

#if defined(BA_TEST_GST_RAIZ)
    // 10. GStreamer: una tarjeta `gst` (videobalance) y la fuente ba_gst_fuente.
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"vb","tipo":"gst","params":{"elemento":"videobalance","propiedades":{"saturation":0.0}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"vb"},{"desde":"vb","hacia":"out"}]})");
    // GStreamer se inicia en su hilo (la primera vez revisa sus plugins):
    // hasta entonces la tarjeta deja pasar la imagen. Se espera hasta 30 s.
    for (int i = 0; i < 1500; ++i) {
        im = dibujar(fuente);
        siguienteCuadro();
        if (std::abs(px(im, 5, 5, 0) - px(im, 5, 5, 1)) <= 6) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("   gst videobalance saturation=0: el verde quedo en %d %d %d (alfa %d)\n", px(im, 5, 5, 0),
                px(im, 5, 5, 1), px(im, 5, 5, 2), px(im, 5, 5, 3));
    CHECK(px(im, 5, 5, 3) > 245 && std::abs(px(im, 5, 5, 0) - px(im, 5, 5, 1)) <= 6,
          "tarjeta gst videobalance: saturacion 0 deja gris");
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"vb","tipo":"gst","params":{"elemento":"videobalance","propiedades":{"saturation":1.0}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"vb"},{"desde":"vb","hacia":"out"}]})");
    for (int i = 0; i < 4; ++i) {
        im = dibujar(fuente);
        siguienteCuadro();
    }
    CHECK(px(im, 5, 5, 1) > px(im, 5, 5, 0) + 80,
          "tarjeta gst: la propiedad cambia en vivo (vuelve el verde)");
    // Con el locale de la app (decimales con coma): la propiedad llega igual.
    if (std::setlocale(LC_NUMERIC, "es_AR.UTF-8") != nullptr ||
        std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr) {
        ponerGrafo(
            f,
            R"({"nodos":[{"id":"in","tipo":"in"},{"id":"vb","tipo":"gst","params":{"elemento":"videobalance","propiedades":{"saturation":0.5}}},{"id":"out","tipo":"out"}],
                        "cables":[{"desde":"in","hacia":"vb"},{"desde":"vb","hacia":"out"}]})");
        for (int i = 0; i < 6; ++i) {
            im = dibujar(fuente);
            siguienteCuadro();
        }
        std::printf("   gst con coma decimal (saturation 0,5): %d %d %d\n", px(im, 5, 5, 0), px(im, 5, 5, 1),
                    px(im, 5, 5, 2));
        CHECK(px(im, 5, 5, 0) > 30 && px(im, 5, 5, 1) < 160,
              "tarjeta gst con el locale de la app (coma decimal): saturación 0,5 se aplica");
        std::setlocale(LC_NUMERIC, "C");
    }
    // Un módulo de OpenGL: la tarjeta lo envuelve con glupload/gldownload.
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"gl","tipo":"gst","params":{"elemento":"glcolorbalance","propiedades":{"saturation":0.0}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"gl"},{"desde":"gl","hacia":"out"}]})");
    for (int i = 0; i < 100; ++i) {
        im = dibujar(fuente);
        siguienteCuadro();
        if (i > 4 && std::abs(px(im, 5, 5, 0) - px(im, 5, 5, 1)) <= 6) break;
    }
    std::printf("   gst glcolorbalance saturation=0: %d %d %d (alfa %d)\n", px(im, 5, 5, 0), px(im, 5, 5, 1),
                px(im, 5, 5, 2), px(im, 5, 5, 3));
    CHECK(px(im, 5, 5, 3) > 245 && std::abs(px(im, 5, 5, 0) - px(im, 5, 5, 1)) <= 6,
          "tarjeta gst OpenGL (glcolorbalance): saturacion 0 deja gris");
    // Un enum por su nick (gleffects: «heat», la paleta de cámara térmica).
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"ge","tipo":"gst","params":{"elemento":"gleffects","propiedades":{"effect":"heat"}}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"ge"},{"desde":"ge","hacia":"out"}]})");
    for (int i = 0; i < 100; ++i) {
        im = dibujar(fuente);
        siguienteCuadro();
        if (i > 4 && (px(im, 5, 5, 0) != 0 || px(im, 5, 5, 1) != 177)) break;
    }
    std::printf("   gst gleffects heat: el verde quedo en %d %d %d\n", px(im, 5, 5, 0), px(im, 5, 5, 1),
                px(im, 5, 5, 2));
    CHECK(px(im, 5, 5, 0) != 0 || px(im, 5, 5, 1) != 177,
          "tarjeta gst gleffects (enum por nick): cambia la imagen");
    ponerGrafo(
        f,
        R"({"nodos":[{"id":"in","tipo":"in"},{"id":"x","tipo":"gst","params":{"elemento":"no-existe-este"}},{"id":"out","tipo":"out"}],
                    "cables":[{"desde":"in","hacia":"x"},{"desde":"x","hacia":"out"}]})");
    im = dibujar(fuente);
    CHECK(px(im, 50, 10, 0) == 200, "tarjeta gst con un modulo que no existe: la imagen pasa igual");

    obs_data_t* gs = obs_data_create();
    obs_data_set_string(
        gs, "pipeline",
        "videotestsrc pattern=red is-live=true ! video/x-raw,width=64,height=64,framerate=30/1");
    obs_source_t* gf = obs_source_create("ba_gst_fuente", "senal", gs, nullptr);
    obs_data_release(gs);
    CHECK(gf != nullptr, "existe la fuente ba_gst_fuente");
    std::vector<std::uint8_t> ig;
    for (int i = 0; i < 60 && gf != nullptr; ++i) {
        siguienteCuadro();
        ig = dibujar(gf);
        if (px(ig, 32, 32, 0) > 200) break;
    }
    std::printf("   ba_gst_fuente (videotestsrc red): %d %d %d\n", px(ig, 32, 32, 0), px(ig, 32, 32, 1),
                px(ig, 32, 32, 2));
    CHECK(px(ig, 32, 32, 0) > 200 && px(ig, 32, 32, 1) < 50, "fuente GStreamer: llegan los cuadros (rojo)");
    // Como en la app: una tarjeta gst sobre la fuente GStreamer (asíncrona), a 1280x720.
    {
        obs_data_t* gs2 = obs_data_create();
        obs_data_set_string(gs2, "pipeline",
                            "videotestsrc pattern=smpte-rp-219 is-live=true ! "
                            "video/x-raw,width=1280,height=720,framerate=30/1");
        obs_source_t* barras = obs_source_create("ba_gst_fuente", "barras", gs2, nullptr);
        obs_data_release(gs2);
        obs_source_t* g2 = obs_source_create_private("ba_video_grafo", "grafo2", nullptr);
        obs_source_filter_add(barras, g2);
        ponerGrafo(
            g2,
            R"({"nodos":[{"id":"in","tipo":"in"},{"id":"vb","tipo":"gst","params":{"elemento":"videobalance","propiedades":{"saturation":0.0}}},{"id":"out","tipo":"out"}],
                        "cables":[{"desde":"in","hacia":"vb"},{"desde":"vb","hacia":"out"}]})");
        std::vector<std::uint8_t> ib;
        int gris = 0;
        for (int i = 0; i < 200 && gris == 0; ++i) {
            siguienteCuadro();
            ib = dibujar(barras); // W x H del test: la fuente se dibuja escalada
            // Una barra de color (la amarilla, ~x 0.3): con saturación 0 queda gris.
            const int x = W * 3 / 10, y = H / 4;
            if (px(ib, x, y, 3) > 0 && std::abs(px(ib, x, y, 0) - px(ib, x, y, 2)) <= 8) gris = 1;
        }
        const int x = W * 3 / 10, y = H / 4;
        std::printf("   gst sobre ba_gst_fuente 1280x720: %d %d %d (alfa %d)\n", px(ib, x, y, 0),
                    px(ib, x, y, 1), px(ib, x, y, 2), px(ib, x, y, 3));
        CHECK(gris == 1,
              "tarjeta gst sobre una fuente GStreamer (asíncrona, 1280x720): saturación 0 deja gris");
        obs_source_filter_remove(barras, g2);
        obs_source_release(g2);
        obs_source_release(barras);
    }
    obs_source_release(gf);
#endif

    // 11. Lo que cruza entradas: una toma, el compositor (fuente) y `traer`.
    {
        // La entrada publica la toma «cam» (una rama, como una salida).
        ponerGrafo(
            f,
            R"({"nodos":[{"id":"in","tipo":"in"},{"id":"t","tipo":"toma","params":{"nombre":"cam"}},{"id":"out","tipo":"out"}],
                        "cables":[{"desde":"in","hacia":"out"},{"desde":"in","hacia":"t"}]})");
        // Un compositor de 64x64: la toma en la mitad derecha, estirada; el resto, fondo negro.
        const char* config =
            R"({"ancho":64,"alto":64,"capas":[{"toma":"cam","fuente":"cuadros","x":0.5,"y":0,"ancho":0.5,"alto":1,"ajuste":"llenar"}]})";
        obs_data_t* cs = obs_data_create();
        obs_data_set_string(cs, "nodo", "estudio.compositor");
        obs_data_set_string(cs, "config", config);
        obs_source_t* comp = obs_source_create("ba_video_compositor", "comp", cs, nullptr);
        obs_data_release(cs);
        CHECK(comp != nullptr && obs_source_get_width(comp) == 64,
              "existe la fuente ba_video_compositor (64x64)");
        siguienteCuadro();
        // Nadie dibujó «cuadros» en este cuadro: el compositor la despierta.
        auto ic = dibujar(comp);
        std::printf("   compositor: fondo %d %d %d %d · izq de la toma %d %d %d · der %d %d %d\n",
                    px(ic, 10, 10, 0), px(ic, 10, 10, 1), px(ic, 10, 10, 2), px(ic, 10, 10, 3),
                    px(ic, 40, 10, 0), px(ic, 40, 10, 1), px(ic, 40, 10, 2), px(ic, 60, 10, 0),
                    px(ic, 60, 10, 1), px(ic, 60, 10, 2));
        CHECK(px(ic, 10, 10, 3) == 255 && px(ic, 10, 10, 0) < 5 && px(ic, 10, 10, 1) < 5,
              "compositor: el fondo es negro");
        CHECK(px(ic, 40, 10, 1) > 150 && px(ic, 40, 10, 0) < 30,
              "compositor: la mitad izquierda de la toma (verde)");
        CHECK(px(ic, 60, 10, 0) > 180 && px(ic, 60, 10, 1) > 180,
              "compositor: la mitad derecha de la toma (gris)");
        // La escena de la entrada recibe el compositor (que la recibe a ella):
        // sin recursión, con la toma fresca de este mismo cuadro.
        std::string g2 =
            R"({"nodos":[{"id":"in","tipo":"in"},{"id":"t","tipo":"toma","params":{"nombre":"cam"}},
                            {"id":"tr","tipo":"traer","params":{"compositor":"estudio.compositor","config":")";
        for (const char* c = config; *c; ++c) {
            if (*c == '"') g2 += '\\';
            g2 += *c;
        }
        g2 +=
            R"("}},{"id":"out","tipo":"out"}],"cables":[{"desde":"in","hacia":"t"},{"desde":"tr","hacia":"out"}]})";
        ponerGrafo(f, g2);
        for (int i = 0; i < 3; ++i) {
            im = dibujar(fuente);
            siguienteCuadro();
        }
        std::printf("   traer: %d %d %d · %d %d %d\n", px(im, 10, 10, 0), px(im, 10, 10, 1),
                    px(im, 10, 10, 2), px(im, 40, 10, 0), px(im, 40, 10, 1), px(im, 40, 10, 2));
        CHECK(px(im, 10, 10, 1) < 5 && px(im, 40, 10, 1) > 150 && px(im, 60, 10, 0) > 180,
              "traer: la escena de la entrada muestra el compositor que la recibe");
        obs_source_release(comp);
    }

    // 13. rostros: baja un cuadro chico mientras la app lo pide (sin que nadie
    // dibuje la entrada: el grafo la despierta) y marca las caras que le mandan.
    {
        ponerGrafo(f, lineal("rostros", R"({"cada":100,"ancho":32})"));
        std::vector<std::uint8_t> bgra;
        long long ancho = 0, alto = 0, serie = 0;
        const auto pedir = [&]() {
            calldata_t cd = {};
            calldata_set_string(&cd, "nodo", "t");
            calldata_set_ptr(&cd, "destino", &bgra);
            proc_handler_call(obs_source_get_proc_handler(f), "analisis", &cd);
            ancho = calldata_int(&cd, "ancho");
            alto = calldata_int(&cd, "alto");
            serie = calldata_int(&cd, "serie");
            calldata_free(&cd);
        };
        pedir(); // el primer pedido la prende
        for (int i = 0; i < 300 && serie < 2; ++i) {
            siguienteCuadro();
            pedir();
        }
        std::printf("   rostros: %lldx%lld, serie %lld, bytes %zu\n", ancho, alto, serie, bgra.size());
        // Pide 32 pero el mínimo es 160: la imagen de 64 baja entera.
        CHECK(serie >= 2 && ancho == 64 && alto == 64 && bgra.size() == 64u * 64u * 4u,
              "rostros: baja cuadros chicos mientras se piden (despertando la entrada)");
        const std::uint8_t* q = bgra.data() + (5 * 64 + 5) * 4;
        std::printf("   rostros: el verde en BGRA = %d %d %d %d\n", q[0], q[1], q[2], q[3]);
        CHECK(bgra.size() == 16384 && q[1] > 160 && q[2] < 30 && q[0] > 40 && q[0] < 90,
              "rostros: el cuadro llega en BGRA");
        // Sin pedidos, deja de bajar: en 6,5 s sin pedir, a uno cada 100 ms
        // bajaría ~65; con el corte a los 3 s, ~30.
        const long long antes = serie;
        std::this_thread::sleep_for(std::chrono::milliseconds(6500));
        pedir();
        std::printf("   rostros: %lld cuadros en 6,5 s sin pedidos\n", serie - antes);
        CHECK(serie - antes <= 45, "rostros: sin pedidos deja de bajar cuadros");
        // Marcar: una caja en el cuarto de arriba a la izquierda.
        ponerGrafo(f, lineal("rostros", R"({"cada":100,"ancho":32,"marcar":true})"));
        {
            calldata_t cd = {};
            calldata_set_string(&cd, "nodo", "t");
            calldata_set_string(&cd, "cajas", R"([{"x":0.0,"y":0.0,"ancho":0.5,"alto":0.5}])");
            proc_handler_call(obs_source_get_proc_handler(f), "caras", &cd);
            calldata_free(&cd);
        }
        for (int i = 0; i < 3; ++i) {
            im = dibujar(fuente);
            siguienteCuadro();
        }
        std::printf("   rostros marcar: borde %d %d %d · adentro %d %d %d\n", px(im, 0, 10, 0),
                    px(im, 0, 10, 1), px(im, 0, 10, 2), px(im, 10, 10, 0), px(im, 10, 10, 1),
                    px(im, 10, 10, 2));
        CHECK(px(im, 0, 10, 1) > 180 && px(im, 0, 10, 0) < 80 && px(im, 0, 10, 2) < 140 &&
                  px(im, 10, 10, 1) == 177,
              "rostros: marca la caja (borde verde, adentro igual)");
    }

    // 12. retardo: con la imagen quieta, pasa igual (y no se cae al cambiar de largo).
    ponerGrafo(f, lineal("retardo", R"({"cuadros":3})"));
    for (int i = 0; i < 6; ++i) {
        im = dibujar(fuente);
        siguienteCuadro();
    }
    CHECK(px(im, 50, 10, 0) == 200 && px(im, 5, 5, 1) == 177,
          "retardo de 3 cuadros: la imagen (quieta) llega igual");
    ponerGrafo(f, lineal("retardo", R"({"cuadros":0})"));
    im = dibujar(fuente);
    CHECK(px(im, 50, 10, 0) == 200, "retardo de 0: pasa directo");

    // 14. normalizar: sin diferencia entre lo declarado y lo real, no cambia;
    // 709 declarado pero en verdad 601: decodificar con 601 resta más Cb y Cr
    // (negativos en un verde), así que sube el rojo y el verde.
    ponerGrafo(f, lineal("normalizar", R"({"matriz_declarada":"709","matriz_real":"709"})"));
    im = dibujar(fuente);
    CHECK(std::abs(px(im, 5, 5, 1) - 177) <= 2 && std::abs(px(im, 5, 5, 0) - 0) <= 2,
          "normalizar igual a igual: no cambia");
    ponerGrafo(f, lineal("normalizar", R"({"matriz_declarada":"709","matriz_real":"601"})"));
    im = dibujar(fuente);
    std::printf("   normalizar 709->601: el verde (0,177,64) quedo en %d %d %d\n", px(im, 5, 5, 0),
                px(im, 5, 5, 1), px(im, 5, 5, 2));
    CHECK(px(im, 5, 5, 0) > 8 && px(im, 5, 5, 1) > 190 && px(im, 5, 5, 3) == 255,
          "normalizar 709->601: corrige el color");
    // retardo en ms: 100 ms a 60 fps = 6 cuadros (pasa igual con imagen quieta).
    ponerGrafo(f, lineal("retardo", R"({"ms":100})"));
    for (int i = 0; i < 10; ++i) {
        im = dibujar(fuente);
        siguienteCuadro();
    }
    CHECK(px(im, 50, 10, 0) == 200, "retardo en ms: la imagen llega");

#if defined(BA_TEST_GST_RAIZ)
    // 15. La fuente con colchón: con 600 ms de latencia no muestra nada antes
    // de tiempo, y su `estado` dice reloj, formato y colorimetría.
    {
        obs_data_t* gs3 = obs_data_create();
        obs_data_set_string(
            gs3, "pipeline",
            "videotestsrc pattern=blue is-live=true ! video/x-raw,width=64,height=64,framerate=30/1");
        obs_data_set_int(gs3, "latencia", 600);
        obs_source_t* lat = obs_source_create("ba_gst_fuente", "colchon", gs3, nullptr);
        obs_data_release(gs3);
        const std::uint64_t t0 = os_gettime_ns();
        std::uint64_t primero = 0;
        for (int i = 0; i < 300 && primero == 0; ++i) {
            siguienteCuadro();
            std::vector<std::uint8_t> il = dibujar(lat);
            if (px(il, 32, 32, 2) > 200) primero = os_gettime_ns();
        }
        const double ms = primero == 0 ? -1 : static_cast<double>(primero - t0) / 1e6;
        std::printf("   colchón de 600 ms: el primer cuadro apareció a los %.0f ms\n", ms);
        CHECK(ms >= 550, "fuente con latencia: no presenta antes de tiempo");
        calldata_t cd = {};
        proc_handler_call(obs_source_get_proc_handler(lat), "estado", &cd);
        const char* info = calldata_string(&cd, "info");
        std::printf("   estado: %s\n", info != nullptr ? info : "(nada)");
        const std::string si = info != nullptr ? info : "";
        CHECK(si.find("\"reloj\":\"sistema\"") != std::string::npos &&
                  si.find("\"latencia_ms\":600") != std::string::npos &&
                  si.find("\"colorimetria\":\"bt709") != std::string::npos &&
                  si.find("\"origen\":{") != std::string::npos &&
                  si.find("\"ancho\":64") != std::string::npos,
              "fuente: el estado informa reloj, latencia, colorimetría y tamaño");
        calldata_free(&cd);
        obs_source_release(lat);
    }
#endif

#if defined(BA_TEST_GST_RAIZ)
    // Diagnóstico a mano (no corre si no se pide): BA_TEST_URI=rtsp://… abre
    // esa fuente de red real y muestra qué píxeles llegan.
    if (const char* uri = std::getenv("BA_TEST_URI")) {
        obs_data_t* gu = obs_data_create();
        obs_data_set_string(gu, "uri", uri);
        obs_data_set_int(gu, "latencia", 200);
        obs_data_set_bool(gu, "audio", false);
        obs_source_t* red = obs_source_create("ba_gst_fuente", "red", gu, nullptr);
        obs_data_release(gu);
        std::vector<std::uint8_t> ir;
        for (int i = 0; i < 600; ++i) {
            siguienteCuadro();
            ir = dibujar(red);
            if (i % 60 == 59)
                std::printf("   uri: %ux%u, centro %d %d %d %d\n", obs_source_get_width(red),
                            obs_source_get_height(red), px(ir, 32, 32, 0), px(ir, 32, 32, 1),
                            px(ir, 32, 32, 2), px(ir, 32, 32, 3));
        }
        calldata_t cd = {};
        proc_handler_call(obs_source_get_proc_handler(red), "estado", &cd);
        std::printf("   estado: %s\n", calldata_string(&cd, "info"));
        calldata_free(&cd);
        obs_source_release(red);
    }
#endif

#if defined(BA_TEST_GST_RAIZ) && defined(BA_TEST_PYTHON) &&                                                  \
    !defined(_WIN32) // (lanza el plugin con sleep | python: Unix)
    // 11. Un plugin de Python (docs/PLUGINS.md): su tarjeta en el grafo
    // (`baplugin`) y su entrada de video y audio (`bapluginsrc` adentro de
    // ba_gst_fuente). Python puro, sin numpy: corre con el Python vendoreado.
    {
        char plantilla[] = "/tmp/ba-plugin-pruebaXXXXXX";
        const std::string dir = mkdtemp(plantilla);
        const std::string id = dir.substr(dir.rfind('/') + 1);
        {
            std::FILE* j = std::fopen((dir + "/plugin.json").c_str(), "w");
            std::fputs("{\"principal\": \"plugin.py\"}", j);
            std::fclose(j);
            std::FILE* py = std::fopen((dir + "/plugin.py").c_str(), "w");
            std::fputs(R"PY(import array, time
import ba_plugin as ba
plugin = ba.Plugin()
T = bytes(255 - i for i in range(256))

@plugin.tarjeta("negativo")
def negativo(c, p):
    a = bytearray(bytes(c.datos).translate(T))
    a[3::4] = bytes(c.datos)[3::4]
    c.datos[:] = a

@plugin.tarjeta("mitad", medio="audio")
def mitad(b, p):
    a = array.array("f"); a.frombytes(bytes(b.datos))
    b.datos[:] = array.array("f", (x * 0.5 for x in a)).tobytes()

e = plugin.entrada("Prueba", video=True, audio=True)

@plugin.al_iniciar
def video():
    cuadro = bytes([200, 100, 50, 255]) * (64 * 36)
    while plugin.andando:
        e.enviar_video(cuadro, ancho=64, alto=36, fps=30)
        time.sleep(1 / 30)

@plugin.al_iniciar
def audio():
    blk = array.array("f", [0.4] * 960).tobytes()
    while plugin.andando:
        e.enviar_audio(blk, 48000, 2)
        time.sleep(0.01)

plugin.correr()
)PY",
                       py);
            std::fclose(py);
        }
        // stdin abierto (si se cierra, el plugin termina): un `sleep`.
        const std::string cmd = "sleep 60 | \"" BA_TEST_PYTHON "\" -I -B -u \"" BA_TEST_PLUGINS_SDK
                                "/correr.py\" \"" +
                                dir + "\"";
        std::FILE* proc = popen(cmd.c_str(), "r");
        char linea[4096] = {};
        int puerto = 0;
        long pid = 0;
        if (proc != nullptr && std::fgets(linea, sizeof linea, proc) != nullptr) {
            const std::string l = linea;
            const auto pm = l.find("\"medios\": "), pp = l.find("\"pid\": ");
            if (pm != std::string::npos) puerto = std::atoi(l.c_str() + pm + 10);
            if (pp != std::string::npos) pid = std::atol(l.c_str() + pp + 7);
        }
        std::printf("   plugin: %s (pid %ld, medios en %d)\n", id.c_str(), pid, puerto);
        CHECK(puerto > 0, "plugin: arranca y anuncia su puerto de medios");
        // El puerto por el proc global, como lo hace la app.
        {
            calldata_t cd = {};
            calldata_set_string(&cd, "plugin", id.c_str());
            calldata_set_int(&cd, "puerto", puerto);
            proc_handler_call(obs_get_proc_handler(), "ba_plugin_puerto", &cd);
            calldata_free(&cd);
        }
        // La tarjeta: el negativo del cuadro de prueba.
        ponerGrafo(
            f,
            (R"({"nodos":[{"id":"in","tipo":"in"},{"id":"pl","tipo":"gst","params":{"elemento":"baplugin","propiedades":{"plugin":")" +
             id +
             R"(","tarjeta":"negativo"}}},{"id":"out","tipo":"out"}],"cables":[{"desde":"in","hacia":"pl"},{"desde":"pl","hacia":"out"}]})")
                .c_str());
        std::vector<std::uint8_t> base;
        for (int i = 0; i < 300; ++i) {
            im = dibujar(fuente);
            siguienteCuadro();
            if (px(im, 5, 5, 1) < 80 && px(im, 5, 5, 3) > 245) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("   plugin tarjeta negativo: %d %d %d (alfa %d)\n", px(im, 5, 5, 0), px(im, 5, 5, 1),
                    px(im, 5, 5, 2), px(im, 5, 5, 3));
        CHECK(px(im, 5, 5, 1) < 80 && px(im, 5, 5, 0) > 170 && px(im, 5, 5, 3) > 245,
              "plugin: la tarjeta de Python invierte el cuadro (verde -> magenta)");
        // La entrada: video y audio del plugin, por ba_gst_fuente.
        obs_data_t* pd = obs_data_create();
        obs_data_set_string(pd, "pipeline", ("bapluginsrc plugin=" + id + " entrada=Prueba").c_str());
        obs_data_set_string(pd, "pipeline_audio",
                            ("bapluginsrc plugin=" + id + " entrada=Prueba medio=audio").c_str());
        obs_data_set_bool(pd, "audio", true);
        obs_data_set_bool(pd, "normalizar", false);
        obs_data_set_int(pd, "latencia", 100);
        obs_source_t* ent = obs_source_create("ba_gst_fuente", "plugin-entrada", pd, nullptr);
        obs_data_release(pd);
        std::vector<std::uint8_t> ie;
        for (int i = 0; i < 400; ++i) {
            siguienteCuadro();
            ie = dibujar(ent);
            if (obs_source_get_width(ent) == 64 && px(ie, 32, 32, 3) > 245) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("   plugin entrada: %ux%u, centro %d %d %d\n", obs_source_get_width(ent),
                    obs_source_get_height(ent), px(ie, 32, 32, 0), px(ie, 32, 32, 1), px(ie, 32, 32, 2));
        CHECK(obs_source_get_width(ent) == 64 && obs_source_get_height(ent) == 36,
              "plugin: la entrada de video llega con su tamaño");
        CHECK(std::abs(px(ie, 32, 32, 0) - 200) < 20 && std::abs(px(ie, 32, 32, 1) - 100) < 20,
              "plugin: la entrada trae el color que manda");
        {
            calldata_t cd = {};
            proc_handler_call(obs_source_get_proc_handler(ent), "estado", &cd);
            const char* info = calldata_string(&cd, "info");
            std::printf("   plugin entrada estado: %s\n", info != nullptr ? info : "(nada)");
            calldata_free(&cd);
        }
        obs_source_release(ent);
        // Un pipeline suelto: la tarjeta de audio (mitad) sobre el audio del plugin.
        GError* gerr = nullptr;
        GstElement* pa = gst_parse_launch(("bapluginsrc plugin=" + id +
                                           " entrada=Prueba medio=audio ! baplugin plugin=" + id +
                                           " tarjeta=mitad ! appsink name=s sync=false")
                                              .c_str(),
                                          &gerr);
        CHECK(pa != nullptr && gerr == nullptr, "plugin: pipeline de audio con bapluginsrc ! baplugin");
        if (pa != nullptr) {
            gst_element_set_state(pa, GST_STATE_PLAYING);
            GstElement* sk = gst_bin_get_by_name(GST_BIN(pa), "s");
            float muestra = 0;
            for (int i = 0; i < 20 && muestra == 0; ++i) {
                GstSample* smp = nullptr;
                g_signal_emit_by_name(sk, "try-pull-sample", static_cast<GstClockTime>(500 * GST_MSECOND),
                                      &smp);
                if (smp != nullptr) {
                    GstMapInfo mi;
                    GstBuffer* b = gst_sample_get_buffer(smp);
                    if (gst_buffer_map(b, &mi, GST_MAP_READ)) {
                        if (mi.size >= 4) std::memcpy(&muestra, mi.data, 4);
                        gst_buffer_unmap(b, &mi);
                    }
                    gst_sample_unref(smp);
                }
            }
            std::printf("   plugin audio: 0.4 por la tarjeta «mitad» -> %.3f\n", muestra);
            CHECK(std::abs(muestra - 0.2f) < 0.01f,
                  "plugin: el audio del plugin pasa por su tarjeta de audio (0.4 -> 0.2)");
            gst_object_unref(sk);
            gst_element_set_state(pa, GST_STATE_NULL);
            gst_object_unref(pa);
        }
        if (gerr != nullptr) g_error_free(gerr);
        // Sin el plugin: la tarjeta deja pasar la imagen (no se traba).
        if (pid > 0) {
#if defined(_WIN32)
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
                TerminateProcess(h, 1);
                CloseHandle(h);
            }
#else
            kill(static_cast<pid_t>(pid), SIGTERM);
#endif
        }
        for (int i = 0; i < 100; ++i) {
            im = dibujar(fuente);
            siguienteCuadro();
            if (px(im, 5, 5, 1) > 150) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(px(im, 5, 5, 1) > 150, "plugin caído: la tarjeta deja pasar la imagen original");
        if (proc != nullptr) pclose(proc);
        std::system(("rm -rf \"" + dir + "\"").c_str());
    }
#endif

    // 7. out sin nada: negro
    ponerGrafo(f, R"({"nodos":[{"id":"in","tipo":"in"},{"id":"out","tipo":"out"}],"cables":[]})");
    im = dibujar(fuente);
    CHECK(px(im, 50, 10, 3) == 0 && px(im, 50, 10, 0) == 0, "out sin nada: no se dibuja");

    obs_source_release(salida);
    obs_source_filter_remove(fuente, f);
    obs_source_release(f);
    obs_source_release(fuente);
    obs_enter_graphics();
    gs_texture_destroy(g_tex);
    obs_leave_graphics();
    obs_shutdown();
    std::printf(g_failures == 0 ? "test_video_grafo: OK\n" : "test_video_grafo: %d fallos\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
