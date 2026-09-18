// ============================================================================
// src/obs-module/ba_dsp_sources.cpp — Channel strip y fuente de preset (libobs real)
//
// Proposito:
//   Expone el DSP nativo de `ba_core::dsp` dentro de OBS:
//     - FILTRO `ba_channel_strip`: aplica `ba::dsp::ChannelStrip` (HPF -> EQ4 ->
//       de-esser -> gate -> compresor -> limitador) a cualquier fuente de audio.
//     - FUENTE `ba_channel_preset`: sintetiza una voz de prueba y la pasa por el
//       strip, para oir el caracter de cada preset sin necesidad de microfono.
//
//   Es el port del antiguo `ba_sources.cpp`, que estaba escrito contra unos
//   headers aproximados en `obs/` cuyas firmas NO son las de libobs. Las
//   diferencias no eran cosmeticas:
//     * `destroy`/`update`/`get_properties` reciben el PUNTERO PRIVADO, no la
//       fuente (el original llamaba a `obs_source_get_private_data`, que no
//       existe en la API real).
//     * `filter_audio` es `obs_audio_data*(void*, obs_audio_data*)`, no una
//       funcion que devuelve un timestamp sobre un `audio_mix`.
//     * `obs_properties_add_list` toma tipo y formato del combo, no un array de
//       items; los rangos se declaran al crear la propiedad.
//     * Las fuentes asincronas no tienen `audio_render`: empujan audio con
//       `obs_source_output_audio` desde un hilo propio.
//     * `obs_source_info` no tiene `description`; el nombre visible sale de
//       `get_name`.
//
// Modelo de threading:
//   `filter_audio` corre en el hilo de audio de libobs y es RT-safe: no asigna,
//   no bloquea, no hace I/O (el scratch se reserva en `create`). `update` y
//   `get_properties` corren en el hilo de control. La fuente de preset tiene su
//   propio hilo productor, que se une en `destroy`.
//
// Estrategia de errores:
//   Ninguna excepcion cruza el borde C de libobs: cada callback tiene su catch.
//   Ante cualquier fallo el filtro degrada a PASS-THROUGH (deja el audio como
//   vino) en vez de silenciar, que durante una emision es lo menos danino.
// ============================================================================

#include <obs-module.h>
#include <obs.h>
#include <util/platform.h> // os_gettime_ns

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "dsp/ChannelStrip.hpp"
#include "dsp/Presets.hpp"

namespace {

/// Reloj del proyecto (spec §7.4).
constexpr double kFs = 48000.0;
/// Suavizado al cambiar de preset, para que no haya clicks.
constexpr double kSmoothMs = 5.0;
constexpr double kTwoPi = 6.28318530717958647692;
/// Frames por bloque del scratch interleaved del filtro.
constexpr int kFilterChunkFrames = 1024;

// ============================================================================
// Propiedades y armado de parametros
// ============================================================================

/// Agrega el selector de preset de canal.
void addPresetList(obs_properties_t* props) {
    obs_property_t* list = obs_properties_add_list(props, "preset", obs_module_text("Preset"),
                                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(list, obs_module_text("Atril"), "podium");
    obs_property_list_add_string(list, obs_module_text("Corbatero"), "lavalier");
    obs_property_list_add_string(list, obs_module_text("De mano"), "handheld");
    obs_property_list_add_string(list, obs_module_text("Neutro"), "neutral");
}

/// Agrega las cinco propiedades de una banda de EQ.
void addEqBand(obs_properties_t* props, int i) {
    char key[64];
    char desc[96];

    std::snprintf(key, sizeof(key), "eq%d_enabled", i);
    std::snprintf(desc, sizeof(desc), "%s %d", obs_module_text("Banda EQ"), i + 1);
    obs_properties_add_bool(props, key, desc);

    std::snprintf(key, sizeof(key), "eq%d_type", i);
    std::snprintf(desc, sizeof(desc), "%s %d — %s", obs_module_text("Banda EQ"), i + 1,
                  obs_module_text("Tipo"));
    obs_property_t* t =
        obs_properties_add_list(props, key, desc, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(t, obs_module_text("Pico"), "peaking");
    obs_property_list_add_string(t, obs_module_text("Estanteria baja"), "lowshelf");
    obs_property_list_add_string(t, obs_module_text("Estanteria alta"), "highshelf");
    obs_property_list_add_string(t, obs_module_text("Pasa banda"), "bandpass");

    std::snprintf(key, sizeof(key), "eq%d_freq", i);
    std::snprintf(desc, sizeof(desc), "%s %d (Hz)", obs_module_text("Banda EQ"), i + 1);
    obs_properties_add_float_slider(props, key, desc, 20.0, 20000.0, 1.0);

    std::snprintf(key, sizeof(key), "eq%d_gain", i);
    std::snprintf(desc, sizeof(desc), "%s %d (dB)", obs_module_text("Banda EQ"), i + 1);
    obs_properties_add_float_slider(props, key, desc, -30.0, 30.0, 0.5);

    std::snprintf(key, sizeof(key), "eq%d_q", i);
    std::snprintf(desc, sizeof(desc), "%s %d (Q)", obs_module_text("Banda EQ"), i + 1);
    obs_properties_add_float_slider(props, key, desc, 0.1, 10.0, 0.05);
}

/// Superficie completa del channel strip: preset + overrides.
obs_properties_t* buildFullProperties() {
    obs_properties_t* props = obs_properties_create();
    addPresetList(props);

    obs_properties_add_float_slider(props, "hpf_freq", obs_module_text("HPF (Hz)"), 20.0, 2000.0, 1.0);
    obs_properties_add_float_slider(props, "hpf_slope", obs_module_text("HPF pendiente (dB/oct)"), 12.0, 24.0,
                                    6.0);

    obs_properties_add_bool(props, "deesser_enabled", obs_module_text("De-esser"));
    obs_properties_add_float_slider(props, "deesser_freq", obs_module_text("De-esser (Hz)"), 2000.0, 12000.0,
                                    100.0);
    obs_properties_add_float_slider(props, "deesser_ratio", obs_module_text("De-esser ratio"), 1.0, 20.0,
                                    0.5);
    obs_properties_add_float_slider(props, "deesser_threshold", obs_module_text("De-esser umbral (dB)"),
                                    -60.0, 0.0, 1.0);

    obs_properties_add_bool(props, "gate_enabled", obs_module_text("Gate"));
    obs_properties_add_float_slider(props, "gate_threshold", obs_module_text("Gate umbral (dB)"), -80.0, 0.0,
                                    1.0);
    obs_properties_add_float_slider(props, "gate_range", obs_module_text("Gate rango (dB)"), 0.0, 60.0, 1.0);

    obs_properties_add_bool(props, "comp_enabled", obs_module_text("Compresor"));
    obs_properties_add_float_slider(props, "comp_ratio", obs_module_text("Comp ratio"), 1.0, 20.0, 0.5);
    obs_properties_add_float_slider(props, "comp_threshold", obs_module_text("Comp umbral (dB)"), -60.0, 0.0,
                                    1.0);
    obs_properties_add_float_slider(props, "comp_knee", obs_module_text("Comp knee (dB)"), 0.0, 30.0, 1.0);

    obs_properties_add_bool(props, "lim_enabled", obs_module_text("Limitador"));
    obs_properties_add_float_slider(props, "lim_ceiling", obs_module_text("Limitador techo (dB)"), -20.0, 0.0,
                                    0.5);

    for (int i = 0; i < 4; ++i) {
        addEqBand(props, i);
    }
    return props;
}

/// Preset de fabrica a partir del nombre guardado en los ajustes.
ba::dsp::StripParams presetByName(const char* name) {
    if (name != nullptr && std::strcmp(name, "lavalier") == 0) {
        return ba::dsp::presetLavalier();
    }
    if (name != nullptr && std::strcmp(name, "handheld") == 0) {
        return ba::dsp::presetHandheld();
    }
    if (name != nullptr && std::strcmp(name, "neutral") == 0) {
        return ba::dsp::StripParams{};
    }
    return ba::dsp::presetPodium();
}

/// Aplica un override de tipo double SOLO si el usuario lo fijo.
///
/// `obs_data_get_double` devuelve 0.0 para una clave sin valor, asi que
/// aplicarlo a ciegas PISA el preset con ceros: el original hacia justo eso y
/// dejaba el strip con HPF a 0 Hz, ratio 0 y umbrales a 0 apenas se cargaba,
/// de modo que elegir un preset no cambiaba practicamente nada.
void overrideDouble(obs_data_t* s, const char* key, double& dst) {
    if (obs_data_has_user_value(s, key)) {
        dst = obs_data_get_double(s, key);
    }
}

void overrideBool(obs_data_t* s, const char* key, bool& dst) {
    if (obs_data_has_user_value(s, key)) {
        dst = obs_data_get_bool(s, key);
    }
}

/// Arma los parametros del strip: preset de fabrica + overrides del usuario.
ba::dsp::StripParams buildParams(obs_data_t* s) {
    ba::dsp::StripParams p = presetByName(obs_data_get_string(s, "preset"));
    p.fs = kFs;

    overrideDouble(s, "hpf_freq", p.hpfFreq);
    overrideDouble(s, "hpf_slope", p.hpfSlopeDbOct);

    overrideBool(s, "deesser_enabled", p.deEsserEnabled);
    overrideDouble(s, "deesser_freq", p.deEsserFreq);
    overrideDouble(s, "deesser_ratio", p.deEsserRatio);
    overrideDouble(s, "deesser_threshold", p.deEsserThresholdDb);

    overrideBool(s, "gate_enabled", p.gateEnabled);
    overrideDouble(s, "gate_threshold", p.gateThresholdDb);
    overrideDouble(s, "gate_range", p.gateRangeDb);

    overrideBool(s, "comp_enabled", p.compEnabled);
    overrideDouble(s, "comp_ratio", p.compRatio);
    overrideDouble(s, "comp_threshold", p.compThresholdDb);
    overrideDouble(s, "comp_knee", p.compKneeDb);

    overrideBool(s, "lim_enabled", p.limEnabled);
    overrideDouble(s, "lim_ceiling", p.limCeilingDb);

    for (int i = 0; i < 4; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "eq%d_enabled", i);
        overrideBool(s, key, p.eq[i].enabled);
        if (!p.eq[i].enabled) {
            continue;
        }
        std::snprintf(key, sizeof(key), "eq%d_type", i);
        if (obs_data_has_user_value(s, key)) {
            const char* t = obs_data_get_string(s, key);
            if (t != nullptr && std::strcmp(t, "lowshelf") == 0) {
                p.eq[i].type = ba::dsp::BiquadType::LowShelf;
            } else if (t != nullptr && std::strcmp(t, "highshelf") == 0) {
                p.eq[i].type = ba::dsp::BiquadType::HighShelf;
            } else if (t != nullptr && std::strcmp(t, "bandpass") == 0) {
                p.eq[i].type = ba::dsp::BiquadType::BandPass;
            } else {
                p.eq[i].type = ba::dsp::BiquadType::Peaking;
            }
        }
        std::snprintf(key, sizeof(key), "eq%d_freq", i);
        overrideDouble(s, key, p.eq[i].freq);
        std::snprintf(key, sizeof(key), "eq%d_gain", i);
        overrideDouble(s, key, p.eq[i].gainDb);
        std::snprintf(key, sizeof(key), "eq%d_q", i);
        overrideDouble(s, key, p.eq[i].q);
    }
    return p;
}

/// Valores por defecto: el preset de atril, que es el de fabrica.
void presetDefaults(obs_data_t* settings) { obs_data_set_default_string(settings, "preset", "podium"); }

// ============================================================================
// FILTRO `ba_channel_strip`
// ============================================================================

struct StripFilter {
    ba::dsp::ChannelStrip strip;
    std::vector<float> scratch; // interleaved estereo, reservado en create
    std::mutex paramsLock;      // update (control) vs process (audio)
};

const char* stripGetName(void*) { return obs_module_text("Channel strip (BA)"); }

void* stripCreate(obs_data_t* settings, obs_source_t* source) {
    (void)source;
    auto* f = new (std::nothrow) StripFilter();
    if (f == nullptr) {
        return nullptr;
    }
    try {
        // El scratch se reserva ACA, en el hilo de control: el camino de audio
        // no debe asignar.
        f->scratch.assign(static_cast<std::size_t>(2) * kFilterChunkFrames, 0.0f);
        f->strip.setParams(buildParams(settings), kSmoothMs);
    } catch (...) {
        // Queda con el estado neutro; el filtro degrada a pass-through.
    }
    return f;
}

void stripDestroy(void* data) { delete static_cast<StripFilter*>(data); }

void stripUpdate(void* data, obs_data_t* settings) {
    auto* f = static_cast<StripFilter*>(data);
    if (f == nullptr) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(f->paramsLock);
        f->strip.setParams(buildParams(settings), kSmoothMs);
    } catch (...) {
        // Conserva los parametros previos.
    }
}

obs_properties_t* stripProperties(void*) { return buildFullProperties(); }

void stripDefaults(obs_data_t* settings) { presetDefaults(settings); }

} // namespace

/// Callback de audio del filtro. libobs entrega PLANAR float; el DSP nativo
/// trabaja sobre estereo INTERLEAVED, asi que se re-mapea por bloques usando
/// el scratch ya reservado (sin asignar en el camino de audio).
///
/// Se EXPORTA (no vive en el namespace anonimo) para que las pruebas puedan
/// resolverla con dlsym sobre el modulo ya cargado y ejercitar el DSP sin
/// depender del mezclador de libobs, que necesita el pipeline de video y por
/// tanto un contexto grafico.
extern "C" MODULE_EXPORT struct obs_audio_data* ba_channel_strip_filter_audio(void* data,
                                                                              struct obs_audio_data* audio) {
    auto* f = static_cast<StripFilter*>(data);
    if (f == nullptr || audio == nullptr || audio->frames == 0 || audio->data[0] == nullptr) {
        return audio;
    }
    try {
        const std::size_t chunkCap = f->scratch.size() / 2u;
        if (chunkCap == 0) {
            return audio; // sin scratch: pass-through
        }
        // El numero de canales lo define el mezclador, no el frame.
        const std::size_t channels = audio_output_get_channels(obs_get_audio());
        auto* p0 = reinterpret_cast<float*>(audio->data[0]);
        float* p1 =
            (channels >= 2 && audio->data[1] != nullptr) ? reinterpret_cast<float*>(audio->data[1]) : nullptr;

        std::lock_guard<std::mutex> lock(f->paramsLock);
        std::uint32_t offset = 0;
        std::uint32_t remaining = audio->frames;
        while (remaining > 0) {
            const int chunk =
                static_cast<int>((remaining < static_cast<std::uint32_t>(chunkCap)) ? remaining : chunkCap);
            float* sc = f->scratch.data();
            for (int i = 0; i < chunk; ++i) {
                const float l = p0[offset + static_cast<std::uint32_t>(i)];
                // Mono: se duplica a ambos lados para que el strip (estereo)
                // vea la misma señal y el resultado sea simetrico.
                const float r = (p1 != nullptr) ? p1[offset + static_cast<std::uint32_t>(i)] : l;
                sc[2 * i] = l;
                sc[2 * i + 1] = r;
            }
            f->strip.process(sc, chunk);
            for (int i = 0; i < chunk; ++i) {
                p0[offset + static_cast<std::uint32_t>(i)] = sc[2 * i];
                if (p1 != nullptr) {
                    p1[offset + static_cast<std::uint32_t>(i)] = sc[2 * i + 1];
                }
            }
            offset += static_cast<std::uint32_t>(chunk);
            remaining -= static_cast<std::uint32_t>(chunk);
        }
    } catch (...) {
        // Pass-through: mejor dejar pasar el audio que silenciarlo al aire.
    }
    return audio;
}

namespace {

// ============================================================================
// FUENTE `ba_channel_preset`
// ============================================================================

struct PresetSource {
    obs_source_t* source = nullptr;
    ba::dsp::ChannelStrip strip;
    std::mutex paramsLock;
    std::thread worker;
    std::atomic<bool> stop{false};
    double phase = 0.0;

    /// Genera la voz de prueba, la pasa por el strip y la entrega a libobs.
    ///
    /// Las fuentes asincronas de libobs no tienen callback de "render de
    /// audio": empujan con `obs_source_output_audio` desde su propio hilo. Por
    /// eso hay hilo productor y no una funcion que libobs llame.
    void run() {
        constexpr int kBlock = 480; // 10 ms a 48 kHz
        std::vector<float> buf(static_cast<std::size_t>(kBlock) * 2u, 0.0f);

        // El timestamp se deriva de un CONTADOR DE MUESTRAS, no del reloj de
        // pared. Sellar cada bloque con `os_gettime_ns()` parece lo natural
        // pero deriva: `sleep_for(10 ms)` duerme 10 ms O MAS, asi que los
        // sellos avanzan mas rapido que el audio que representan. libobs lo
        // detecta ("Audio timestamp exceeded TS_SMOOTHING_THRESHOLD") y
        // descarta el bloque: la fuente entra en la escena y no suena.
        const std::uint64_t startNs = os_gettime_ns();
        std::uint64_t samplesSent = 0;

        while (!stop.load(std::memory_order_relaxed)) {
            {
                std::lock_guard<std::mutex> lock(paramsLock);
                for (int i = 0; i < kBlock; ++i) {
                    const double t = phase;
                    // Modulacion tipo silaba (3 Hz) para que gate y compresor
                    // tengan algo a lo que reaccionar.
                    const double env = 0.5 + 0.5 * std::sin(kTwoPi * 3.0 * t);
                    double s = 0.0;
                    s += 0.50 * std::sin(kTwoPi * 150.0 * t);
                    s += 0.30 * std::sin(kTwoPi * 300.0 * t);
                    s += 0.20 * std::sin(kTwoPi * 450.0 * t);
                    // Componente aguda: le da trabajo al de-esser.
                    s += 0.15 * std::sin(kTwoPi * 6000.0 * t);
                    s *= env * 0.15; // ~ -16 dBFS de pico
                    buf[static_cast<std::size_t>(2 * i)] = static_cast<float>(s);
                    buf[static_cast<std::size_t>(2 * i + 1)] = static_cast<float>(s);
                    phase += 1.0 / kFs;
                }
                strip.process(buf.data(), kBlock);
            }

            const std::uint64_t ts =
                startNs + static_cast<std::uint64_t>(samplesSent * 1000000000ull / 48000ull);

            struct obs_source_audio out = {};
            out.data[0] = reinterpret_cast<const std::uint8_t*>(buf.data());
            out.frames = static_cast<std::uint32_t>(kBlock);
            out.speakers = SPEAKERS_STEREO;
            out.format = AUDIO_FORMAT_FLOAT; // interleaved
            out.samples_per_sec = static_cast<std::uint32_t>(kFs);
            out.timestamp = ts;
            obs_source_output_audio(source, &out);
            samplesSent += static_cast<std::uint64_t>(kBlock);

            // Ritmo contra la MISMA linea de tiempo que los sellos, para no
            // adelantarse ni quedarse atras respecto del reloj real.
            const std::uint64_t nextDue =
                startNs + static_cast<std::uint64_t>(samplesSent * 1000000000ull / 48000ull);
            const std::uint64_t now = os_gettime_ns();
            if (nextDue > now) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(nextDue - now));
            }
        }
    }
};

const char* presetGetName(void*) { return obs_module_text("Preset de canal (BA)"); }

void* presetCreate(obs_data_t* settings, obs_source_t* source) {
    auto* src = new (std::nothrow) PresetSource();
    if (src == nullptr) {
        return nullptr;
    }
    src->source = source;
    try {
        src->strip.setParams(buildParams(settings), kSmoothMs);
        src->worker = std::thread([src] { src->run(); });
    } catch (...) {
        // Sin hilo no hay señal, pero la fuente existe y no tumba al host.
    }
    return src;
}

void presetDestroy(void* data) {
    auto* src = static_cast<PresetSource*>(data);
    if (src == nullptr) {
        return;
    }
    src->stop.store(true);
    if (src->worker.joinable()) {
        src->worker.join();
    }
    delete src;
}

void presetUpdate(void* data, obs_data_t* settings) {
    auto* src = static_cast<PresetSource*>(data);
    if (src == nullptr) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(src->paramsLock);
        src->strip.setParams(buildParams(settings), kSmoothMs);
    } catch (...) {
    }
}

obs_properties_t* presetProperties(void*) {
    obs_properties_t* props = obs_properties_create();
    addPresetList(props);
    return props;
}

} // namespace

// ============================================================================
// Registro
// ============================================================================

extern "C" {

static struct obs_source_info ba_channel_strip_info = {};
static struct obs_source_info ba_channel_preset_info = {};

/// Registra el filtro de channel strip y la fuente de preset.
/// La llama `obs_module_load` (ba_module.cpp).
void ba_register_dsp_sources(void) {
    ba_channel_strip_info.id = "ba_channel_strip";
    ba_channel_strip_info.type = OBS_SOURCE_TYPE_FILTER;
    ba_channel_strip_info.output_flags = OBS_SOURCE_AUDIO;
    ba_channel_strip_info.get_name = stripGetName;
    ba_channel_strip_info.create = stripCreate;
    ba_channel_strip_info.destroy = stripDestroy;
    ba_channel_strip_info.update = stripUpdate;
    ba_channel_strip_info.get_properties = stripProperties;
    ba_channel_strip_info.get_defaults = stripDefaults;
    ba_channel_strip_info.filter_audio = ba_channel_strip_filter_audio;
    obs_register_source(&ba_channel_strip_info);

    ba_channel_preset_info.id = "ba_channel_preset";
    ba_channel_preset_info.type = OBS_SOURCE_TYPE_INPUT;
    ba_channel_preset_info.output_flags = OBS_SOURCE_AUDIO;
    ba_channel_preset_info.get_name = presetGetName;
    ba_channel_preset_info.create = presetCreate;
    ba_channel_preset_info.destroy = presetDestroy;
    ba_channel_preset_info.update = presetUpdate;
    ba_channel_preset_info.get_properties = presetProperties;
    ba_channel_preset_info.get_defaults = stripDefaults;
    ba_channel_preset_info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
    obs_register_source(&ba_channel_preset_info);
}

} // extern "C"
