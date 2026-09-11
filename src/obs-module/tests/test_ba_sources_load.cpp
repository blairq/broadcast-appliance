// ============================================================================
// src/obs-module/tests/test_ba_sources_load.cpp — test de carga del módulo
// `ba-sources` (fuentes/filtros libobs)
//
// Proposito:
//   Verifica que el módulo `ba-sources` se registra y procesa audio correctamente
//   cuando libobs está disponible (feature-flag `BA_SOURCES_HAVE_OBS`):
//     1. `obs_startup` headless (sin UI, sin outputs).
//     2. Carga el módulo llamando a su entrypoint `obs_module_register` (el que
//        OBS invoca al cargar la DLL), que a su vez hace los
//        `obs_register_source` de `ba_sources.cpp` (fuente `ba_channel_preset`
//        y filtro `ba_channel_strip`).
//     3. Crea la fuente y el filtro propios vía `obs_source_create`.
//     4. Procesa UN buffer de audio a través del ChannelStrip real que el
//        filtro construyó en su `create` (recuperado con
//        `obs_source_get_private_data`), que aplica la cadena nativa de
//        `ba_core::dsp` (HPF -> EQ4 -> de-esser -> gate -> comp -> lim, cada
//        biquad RBJ), y verifica que la salida es finita y válida.
//     5. `obs_shutdown`.
//
//   Devuelve 0 si todo pasa y != 0 si algo falla (patrón de los tests del
//   arbol: cada test es un main() sin framework, registrado por CTest).
//
// Gating por feature-flag:
//   Todo lo de libobs va tras `#ifdef BA_SOURCES_HAVE_OBS`. Cuando el flag NO
//   está definido (libobs ausente: Linux nativo y cross-build MinGW de
//   validación) el test imprime `SKIP: no libobs` y devuelve 0, y se compila
//   SIN incluir ningún header de libobs (solo std). Así el path OFF nunca
//   depende de los headers/símbolos de libobs.
//
// Modelo de threading:
//   El test corre en un solo thread: `obs_startup`, registro, creación de
//   fuentes, procesado del buffer y `obs_shutdown` en secuencia. No hay
//   concurrencia: `ChannelStrip::process` muta estado de UN strip y aquí no
//   hay solape con `setParams` (que solo se llamó dentro del `create`).
//
// Estrategia de errores:
//   Sin excepciones: el test no lanza; cada comprobación incrementa un
//   contador de fallos y al final se devuelve 0 o 1. Los callbacks de audio
//   del módulo ya capturan sus propias excepciones en el borde (ver
//   `ba_sources.cpp`).
// ============================================================================

#ifdef BA_SOURCES_HAVE_OBS

// --- Path ON: libobs presente. Headers mínimos vendoreados del módulo. -------
#include "obs/obs-module.h"
#include "obs/obs.h"
#include "obs/obs-properties.h"

#include "dsp/ChannelStrip.hpp"
#include "dsp/Presets.hpp"

#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- Forward declarations de la API de libobs que el test usa y que NO están
// --- en los headers mínimos vendoreados (obs/ solo declara el subconjunto que
// --- usa `ba_sources.cpp`). En el path ON estos se resuelven contra obs.dll.
// --- Las firmas son las de obs-studio 31.0.0 (marcadas // VERIFY).
extern "C" {
// // VERIFY: obs-studio 31.0.0 (libobs/obs.h). Inicializa el contexto de
// // libobs. `profiler_store` puede ser NULL (headless).
bool obs_startup(const char *locale, const char *module_config_path,
                 void *profiler_store);
// // VERIFY: obs-studio 31.0.0 (libobs/obs.h). Cierra el contexto de libobs.
void obs_shutdown(void);
// // VERIFY: obs-studio 31.0.0 (libobs/obs-source.h). Crea una instancia de
// // fuente/filtro a partir de su id registrado. `settings`/`hotkey_data`
// // pueden ser NULL.
struct obs_source *obs_source_create(const char *id, const char *name,
                                     struct obs_data *settings,
                                     struct obs_data *hotkey_data);
// // VERIFY: obs-studio 31.0.0 (libobs/obs-source.h). Suelta una referencia.
void obs_source_release(struct obs_source *source);
// // VERIFY: obs-studio 31.0.0 (libobs/obs-data.h). Crea un contenedor de
// // settings vacío (ref-counted; liberar con obs_data_release).
struct obs_data *obs_data_create(void);
// // VERIFY: obs-studio 31.0.0 (libobs/obs-data.h). Suelta un contenedor.
void obs_data_release(struct obs_data *data);
// // VERIFY: obs-studio 31.0.0 (libobs/obs-data.h). Escribe una cadena.
void obs_data_set_string(struct obs_data *settings, const char *name,
                         const char *val);
}

// Entry point del módulo (definido en ba_sources.cpp, enlace C, exportado de
// la DLL). // VERIFY: firma de obs_module_register en obs-studio 31.0.0.
extern "C" void obs_module_register(struct obs_module *module, obs_log_t log,
                                    const char *local, const char *module_name);

namespace {

// --- Estado de comprobaciones ------------------------------------------------
int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);            \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

constexpr double kFs = 48000.0; // reloj de libobs (spec 7.4)
constexpr int kFrames = 480;    // 10 ms a 48 kHz: UN buffer de audio

// Callback de log de libobs: lo pasa `obs_module_register`; no se usa.
int test_log(int level, const char *line, va_list args) {
    (void)level;
    (void)line;
    (void)args;
    return 0;
}

// --- Generación de señal de prueba ------------------------------------------
// Rellena `buf` con 2*n floats (estéreo interleaved) de una voz sintética de
// baja amplitud: armónicos graves + componente aguda (sibilancia) con
// modulación de amplitud tipo sílaba (3 Hz). Es la misma señal que sintetiza
// la fuente `ba_channel_preset`, de modo que el de-esser/gate/comp del
// ChannelStrip tienen algo real que actuar. Determinista (sin aleatorio).
void fillTestSignal(float *buf, int n) {
    constexpr double kTwoPi = 6.28318530717958647692;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kFs;
        const double env = 0.5 + 0.5 * std::sin(kTwoPi * 3.0 * t);
        double s = 0.0;
        s += 0.50 * std::sin(kTwoPi * 150.0 * t);
        s += 0.30 * std::sin(kTwoPi * 300.0 * t);
        s += 0.20 * std::sin(kTwoPi * 450.0 * t);
        s += 0.15 * std::sin(kTwoPi * 6000.0 * t);
        s *= env * 0.15; // ~ -16 dBFS de pico
        const float sample = static_cast<float>(s);
        buf[2 * i] = sample;
        buf[2 * i + 1] = sample;
    }
}

// --- Caso 1: registro del módulo --------------------------------------------
// Llama al entrypoint del módulo y verifica que las dos fuentes quedaron
// registradas (crear una instancia de cada id debe devolver no-NULL).
void testModuleRegister() {
    obs_module_register(nullptr, &test_log, "", "ba-sources");

    // Fuente de preset de canal.
    struct obs_data *srcSettings = obs_data_create();
    obs_data_set_string(srcSettings, "preset", "podium");
    struct obs_source *src =
        obs_source_create("ba_channel_preset", "test_preset", srcSettings,
                          nullptr);
    CHECK(src != nullptr);
    if (src) {
        obs_source_release(src);
    }

    // Filtro de channel strip.
    struct obs_data *filtSettings = obs_data_create();
    obs_data_set_string(filtSettings, "preset", "lavalier");
    struct obs_source *filt =
        obs_source_create("ba_channel_strip", "test_strip", filtSettings,
                          nullptr);
    CHECK(filt != nullptr);
    if (filt) {
        obs_source_release(filt);
    }

    obs_data_release(srcSettings);
    obs_data_release(filtSettings);
}

// --- Caso 2: procesado de UN buffer a través del filtro ---------------------
// Crea el filtro `ba_channel_strip` y recupera el ChannelStrip real que su
// `create` construyó (vía `obs_source_get_private_data`). Le pasa un buffer de
// audio y verifica que la salida es finita y válida: sin NaN/Inf y |x|
// acotado (el strip termina en limitador -1 dBFS).
//
// Nota sobre el cast: el estado privado del filtro es un struct anónimo cuyo
// PRIMER miembro es `ba::dsp::ChannelStrip` (sin funciones virtuales, sin
// vtable), por lo que la dirección del struct coincide con la del ChannelStrip
// y el cast es bien definido. Así se ejercita el DSP real del módulo, no una
// copia.
void testFilterProcessesAudio() {
    struct obs_data *settings = obs_data_create();
    // Lavalier: cadena completa activa (de-esser + gate + comp + lim + EQ).
    obs_data_set_string(settings, "preset", "lavalier");
    struct obs_source *filt =
        obs_source_create("ba_channel_strip", "test_strip_audio", settings,
                          nullptr);
    obs_data_release(settings);
    CHECK(filt != nullptr);
    if (!filt) {
        return;
    }

    // ChannelStrip real construido por el `create` del filtro.
    void *priv = obs_source_get_private_data(filt);
    CHECK(priv != nullptr);
    if (!priv) {
        obs_source_release(filt);
        return;
    }
    ba::dsp::ChannelStrip *strip = static_cast<ba::dsp::ChannelStrip *>(priv);

    // Buffer estéreo interleaved (2*kFrames floats) con la señal de prueba.
    float *buf = new float[static_cast<std::size_t>(2 * kFrames)];
    fillTestSignal(buf, kFrames);

    // Procesa UN buffer a través del ChannelStrip del filtro (in-place).
    strip->process(buf, kFrames);

    // La salida debe ser finita y válida en cada muestra.
    bool allFinite = true;
    bool bounded = true;
    for (int i = 0; i < 2 * kFrames; ++i) {
        const float x = buf[i];
        if (!std::isfinite(x)) {
            allFinite = false;
        }
        // El strip termina en limitador (-1 dBFS); con margen amplio se acepta
        // |x| < 1.5 (mismo criterio que test_channel_strip de ba_core).
        if (std::fabs(x) >= 1.5) {
            bounded = false;
        }
    }
    CHECK(allFinite);
    CHECK(bounded);

    delete[] buf;
    obs_source_release(filt);
}

} // namespace

int main() {
    // 1) Arranque headless de libobs (sin UI ni outputs).
    if (!obs_startup("en-US", nullptr, nullptr)) {
        std::printf("test_ba_sources_load: obs_startup falló\n");
        return 1;
    }

    // 2) Carga el módulo (registra fuente y filtro) y verifica el registro.
    testModuleRegister();

    // 3) Procesa un buffer a través del ChannelStrip del filtro.
    testFilterProcessesAudio();

    // 4) Cierre de libobs.
    obs_shutdown();

    if (g_failures == 0) {
        std::printf("test_ba_sources_load: OK\n");
        return 0;
    }
    std::printf("test_ba_sources_load: %d fallo(s)\n", g_failures);
    return 1;
}

#else // !BA_SOURCES_HAVE_OBS: libobs ausente (Linux nativo / cross MinGW)

// --- Path OFF: solo std (sin headers de libobs). ----------------------------
// libobs no está en este entorno, así que el test se salta: imprime el motivo
// y devuelve 0 para que CTest lo marque como PASS (skip). No incluye ningún
// header de libobs ni de ba_core en este path: compila con solo std.
#include <cstdio>

int main() {
    std::printf("SKIP: no libobs\n");
    return 0;
}

#endif // BA_SOURCES_HAVE_OBS
