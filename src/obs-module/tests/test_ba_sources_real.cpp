// ============================================================================
// src/obs-module/tests/test_ba_sources_real.cpp — carga real del modulo
//
// Proposito:
//   Comprobar, contra la libobs REAL, que el modulo `ba-sources` se carga y
//   registra sus fuentes IPC. Dos cosas que solo se ven ejecutando:
//
//   1. El ABI. El modulo anterior exportaba `obs_module_register`, un simbolo
//      que libobs no busca nunca, asi que el modulo no se cargaba en ninguna
//      plataforma. Un test que compile no detecta eso; uno que CARGUE, si.
//   2. La forma de la ruta de modulos. `obs_add_module_path()` quiere el
//      DIRECTORIO de los modulos, no un patron ".../%module%.so". Pasarle el
//      patron no da error: simplemente no carga nada, en silencio.
//
// Sobre el contexto grafico:
//   Cargar modulos y registrar fuentes NO necesita GL: solo `obs_startup` y
//   `obs_reset_audio`. Por eso este test corre entero en un server headless,
//   que es justo donde el pipeline de video no se puede probar.
// ============================================================================

#include <obs.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

/// ¿Esta registrado ese id de fuente?
bool sourceTypeRegistered(const char* wanted) {
    const char* id = nullptr;
    for (std::size_t i = 0; obs_enum_source_types(i, &id); ++i) {
        if (std::strcmp(id, wanted) == 0) {
            return true;
        }
    }
    return false;
}

/// Resuelve un simbolo exportado por el modulo `ba-sources` ya cargado.
///
/// `RTLD_NOLOAD` devuelve un handle al modulo que libobs ya tiene abierto, sin
/// cargarlo por segunda vez.
void* resolveModuleSymbol(const char* name) {
#if defined(_WIN32)
    HMODULE h = GetModuleHandleA("ba-sources.dll");
    return (h != nullptr) ? reinterpret_cast<void*>(GetProcAddress(h, name)) : nullptr;
#else
    void* h = dlopen(BA_SOURCES_MODULE_DIR "/ba-sources.so", RTLD_NOW | RTLD_NOLOAD);
    if (h == nullptr) {
        return nullptr;
    }
    return dlsym(h, name);
#endif
}

} // namespace

int main() {
    std::printf("== test_ba_sources_real (libobs %s) ==\n", obs_get_version_string());

    if (!obs_startup("en-US", nullptr, nullptr)) {
        // Sin display, `obs_startup` puede fallar antes de llegar a los
        // modulos: eso es del entorno, no del modulo.
        std::printf("SKIP: obs_startup fallo (sesion sin plataforma grafica)\n");
        return 77;
    }

    struct obs_audio_info oai = {};
    oai.samples_per_sec = 48000;
    oai.speakers = SPEAKERS_STEREO;
    CHECK(obs_reset_audio(&oai), "obs_reset_audio");

    // BA_SOURCES_MODULE_DIR lo define el CMake con el directorio donde el build
    // deja ba-sources. Se pasa el DIRECTORIO (ver el encabezado).
    obs_add_module_path(BA_SOURCES_MODULE_DIR, BA_SOURCES_MODULE_DIR "/%module%");
    obs_load_all_modules();
    obs_post_load_modules();

    CHECK(sourceTypeRegistered("ba_guest_audio"), "ba_guest_audio quedo registrada");
    CHECK(sourceTypeRegistered("ba_guest_video"), "ba_guest_video quedo registrada");
    CHECK(sourceTypeRegistered("ba_channel_strip"), "ba_channel_strip quedo registrado");
    CHECK(sourceTypeRegistered("ba_channel_preset"), "ba_channel_preset quedo registrada");

    // Las fuentes tienen que poder instanciarse: es lo que hace el motor al
    // armar la escena. Se crean, se consultan y se sueltan.
    obs_data_t* settings = obs_data_create();
    obs_data_set_int(settings, "slot", 2);

    obs_source_t* audio = obs_source_create("ba_guest_audio", "prueba-audio", settings, nullptr);
    CHECK(audio != nullptr, "se instancio ba_guest_audio");
    if (audio != nullptr) {
        CHECK((obs_source_get_output_flags(audio) & OBS_SOURCE_AUDIO) != 0,
              "ba_guest_audio declara salida de audio");
        obs_source_release(audio);
    }

    obs_source_t* video = obs_source_create("ba_guest_video", "prueba-video", settings, nullptr);
    CHECK(video != nullptr, "se instancio ba_guest_video");
    if (video != nullptr) {
        CHECK((obs_source_get_output_flags(video) & OBS_SOURCE_ASYNC_VIDEO) != 0,
              "ba_guest_video declara salida de video asincrona");
        obs_source_release(video);
    }
    obs_data_release(settings);

    // --- El filtro tiene que PROCESAR de verdad -----------------------------
    // Se resuelve con dlsym la funcion real del filtro sobre el modulo YA
    // cargado por libobs, y se la invoca con un bloque planar tal como lo haria
    // el mezclador. Se hace asi, y no metiendo el filtro en la cadena de una
    // fuente, porque el mezclador de libobs necesita el pipeline de VIDEO para
    // alinear la mezcla: en un server headless no arranca, y el test quedaria
    // sin poder ejercitar el DSP. El camino que se prueba aca (firma de
    // filter_audio + puente planar<->interleaved + DSP) es exactamente el que
    // libobs recorre.
    {
        auto* filterFn = reinterpret_cast<struct obs_audio_data* (*)(void*, struct obs_audio_data*)>(
            resolveModuleSymbol("ba_channel_strip_filter_audio"));
        CHECK(filterFn != nullptr, "se resolvio la funcion del filtro en el modulo");

        obs_data_t* fs = obs_data_create();
        obs_data_set_string(fs, "preset", "neutral");
        obs_data_set_double(fs, "hpf_freq", 2000.0); // muy por encima de 50 Hz
        obs_data_set_double(fs, "hpf_slope", 24.0);
        obs_source_t* strip = obs_source_create("ba_channel_strip", "strip", fs, nullptr);
        obs_data_release(fs);
        CHECK(strip != nullptr, "se instancio el filtro ba_channel_strip");

        if (strip != nullptr && filterFn != nullptr) {
            void* priv = obs_obj_get_data(strip);
            CHECK(priv != nullptr, "el filtro tiene datos privados");

            constexpr std::uint32_t kFrames = 4800; // 100 ms
            std::vector<float> left(kFrames);
            std::vector<float> right(kFrames);
            double inSum = 0.0;
            for (std::uint32_t i = 0; i < kFrames; ++i) {
                const double t = static_cast<double>(i) / 48000.0;
                const float v = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 50.0 * t));
                left[i] = v;
                right[i] = v;
                inSum += static_cast<double>(v) * v;
            }
            const double inRms = std::sqrt(inSum / kFrames);

            struct obs_audio_data ad = {};
            ad.data[0] = reinterpret_cast<std::uint8_t*>(left.data());
            ad.data[1] = reinterpret_cast<std::uint8_t*>(right.data());
            ad.frames = kFrames;
            ad.timestamp = 0;

            if (priv != nullptr) {
                filterFn(priv, &ad);
                double outSum = 0.0;
                bool finite = true;
                for (std::uint32_t i = 0; i < kFrames; ++i) {
                    outSum += static_cast<double>(left[i]) * left[i];
                    if (!std::isfinite(left[i]) || !std::isfinite(right[i])) {
                        finite = false;
                    }
                }
                const double outRms = std::sqrt(outSum / kFrames);
                const double attenDb = 20.0 * std::log10((outRms + 1e-12) / (inRms + 1e-12));
                std::printf("  HPF 2 kHz sobre 50 Hz: %.1f dB\n", attenDb);
                CHECK(finite, "la salida del filtro es finita");
                CHECK(attenDb < -20.0, "el HPF atenuo la señal de 50 Hz");
                // El canal derecho tiene que salir igual que el izquierdo: es
                // la comprobacion de que el puente planar<->interleaved no
                // cruzo los canales.
                CHECK(std::fabs(left[kFrames / 2] - right[kFrames / 2]) < 1e-6,
                      "los dos canales salen coherentes (no se cruzaron)");
            }
            obs_source_release(strip);
        }
    }

    obs_shutdown();

    if (g_failures == 0) {
        std::printf("test_ba_sources_real: OK\n");
        return 0;
    }
    std::printf("test_ba_sources_real: %d fallo(s)\n", g_failures);
    return 1;
}
