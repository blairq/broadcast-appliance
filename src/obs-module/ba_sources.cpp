// ============================================================================
// src/obs-module/ba_sources.cpp — módulo `ba-sources` (fuentes/filtros libobs)
//
// Proposito:
//   Modulo propio de libobs que se despliega en obs-plugins/64bit/ (spec 5.1).
//   Registra (spec 7.2/7.3) sobre el DSP nativo de ba_core::dsp:
//     - FILTRO de audio `ba_channel_strip`: envuelve ba::dsp::ChannelStrip
//       (HPF -> EQ4 -> de-esser -> gate -> comp -> lim). RT-safe, con
//       suavizado de parámetros al cambiar de preset (sin clicks).
//     - FUENTE async de audio `ba_channel_preset`: expone un preset de canal
//       como señal de referencia audible: sintetiza una señal de prueba de
//       baja amplitud (voz sintética con modulación de amplitud y componente
//       aguda) y la procesa con el ChannelStrip cargado con el preset elegido.
//   Los tres presets de fabrica (ba::dsp::presetPodium/Lavalier/Handheld) se
//   exponen como propiedad configurable (selector desplegable) del filtro y de
//   la fuente, con overrides individuales por encima (HPF, de-esser, gate,
//   comp, limitador y EQ de 4 bandas).
//
// Modelo de threading:
//   Los callbacks de audio (`filter_audio`, `audio_render`) corren en el thread
//   de audio de libobs: sin asignacion, sin locks, sin I/O, sin excepciones.
//   `create`/`destroy`/`update`/`get_properties` corren en el thread de
//   control. `obs_source_get_private_data` recupera el puntero que devolvio
//   `create`.
//
// Estrategia de errores:
//   Sin excepciones en el camino de audio: los callbacks de audio y `update`
//   capturan cualquier excepcion en el borde (regla: ninguna excepcion C++
//   cruza un callback C de libobs) y devuelven un valor seguro. El DSP de
//   ba_core no lanza; el catch es defensivo. `build_params` clampa todo a
//   rangos seguros gracias a los clamps internos de ChannelStrip::setParams.
//
// Todo lo de libobs va tras #ifdef _WIN32: en Linux el target se compila como
// esqueleto (exporta solo ba_sources_placeholder) para que el arbol compile.
// ============================================================================

#ifdef _WIN32

#include "obs/obs-module.h"
#include "obs/obs-properties.h"
#include "obs/obs.h"

#include "dsp/Biquad.hpp"
#include "dsp/ChannelStrip.hpp"
#include "dsp/Presets.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// --- Constantes --------------------------------------------------------------
constexpr double kFs = 48000.0;                   // reloj de libobs (spec 7.4)
constexpr double kSmoothMs = 5.0;                 // fade de ganancia al cambiar de preset
constexpr double kTwoPi = 6.28318530717958647692; // 2*pi (sin depender de M_PI)

// --- Helpers de propiedades --------------------------------------------------

/// Añade a `props` un selector desplegable de preset de canal.
///
/// Opciones: "podium" (atril), "lavalier" (corbatero), "handheld" (de mano) y
/// "neutral" (cadena neutra sin preset). El valor devuelto es el nombre interno
/// que se lee con `obs_data_get_string(s, "preset")`.
///
/// :param props: propiedades a las que se añade la lista.
/// :param name: nombre interno de la propiedad.
/// :param desc: descripcion para mostrar en la UI.
/// :returns: la propiedad creada (NULL si falla).
obs_property* add_preset_list(obs_properties* props, const char* name, const char* desc) {
    static const obs_property_list_item items[] = {
        {"podium", "Atril / cuello de cisne"},
        {"lavalier", "Corbatero / lavalier"},
        {"handheld", "Microfono de mano"},
        {"neutral", "Neutro (sin preset)"},
    };
    return obs_properties_add_list(props, name, desc, OBS_PROPERTY_LIST, items, 4, 0);
}

/// Añade a `props` la propiedad de una banda EQ (activa, tipo, Hz, dB, Q).
///
/// :param props: propiedades a las que se añaden las subpropiedades de la banda.
/// :param i: indice de la banda (0..3); se usan claves `eq<i>_*`.
void add_eq_band(obs_properties* props, int i) {
    char key[64];
    char desc[64];

    std::snprintf(key, sizeof(key), "eq%d_enabled", i);
    std::snprintf(desc, sizeof(desc), "Banda EQ %d (activa)", i + 1);
    obs_properties_add_bool(props, key, desc);

    std::snprintf(key, sizeof(key), "eq%d_type", i);
    std::snprintf(desc, sizeof(desc), "Banda EQ %d (tipo)", i + 1);
    static const obs_property_list_item type_items[] = {
        {"peaking", "Pico"},
        {"lowshelf", "Estanteria baja"},
        {"highshelf", "Estanteria alta"},
        {"bandpass", "Pasa banda"},
    };
    obs_properties_add_list(props, key, desc, OBS_PROPERTY_LIST, type_items, 4, 0);

    std::snprintf(key, sizeof(key), "eq%d_freq", i);
    std::snprintf(desc, sizeof(desc), "Banda EQ %d (Hz)", i + 1);
    obs_property* p = obs_properties_add_double(props, key, desc);
    obs_property_double_set_range(p, 20.0, 20000.0, 1.0);

    std::snprintf(key, sizeof(key), "eq%d_gain", i);
    std::snprintf(desc, sizeof(desc), "Banda EQ %d (dB)", i + 1);
    p = obs_properties_add_double(props, key, desc);
    obs_property_double_set_range(p, -30.0, 30.0, 0.5);

    std::snprintf(key, sizeof(key), "eq%d_q", i);
    std::snprintf(desc, sizeof(desc), "Banda EQ %d (Q)", i + 1);
    p = obs_properties_add_double(props, key, desc);
    obs_property_double_set_range(p, 0.1, 10.0, 0.05);
}

/// Construye la superficie de propiedades completa del channel strip.
///
/// Incluye el selector de preset y los overrides de HPF, de-esser, gate,
/// compresor, limitador y las 4 bandas de EQ. Se usa tanto en el filtro como
/// (en una version reducida) en la fuente.
///
/// :returns: propiedades nuevas (el propietario las destruye con
///           `obs_properties_destroy`).
obs_properties* build_full_properties() {
    obs_properties* props = obs_properties_create();
    add_preset_list(props, "preset", "Preset de canal");

    // HPF
    obs_property* p = obs_properties_add_double(props, "hpf_freq", "HPF (Hz)");
    obs_property_double_set_range(p, 20.0, 2000.0, 1.0);
    p = obs_properties_add_double(props, "hpf_slope", "HPF pendiente (dB/oct)");
    obs_property_double_set_range(p, 12.0, 24.0, 6.0);

    // De-esser
    obs_properties_add_bool(props, "deesser_enabled", "De-esser");
    p = obs_properties_add_double(props, "deesser_freq", "De-esser Freq (Hz)");
    obs_property_double_set_range(p, 2000.0, 12000.0, 100.0);
    p = obs_properties_add_double(props, "deesser_ratio", "De-esser Ratio");
    obs_property_double_set_range(p, 1.0, 20.0, 0.5);
    p = obs_properties_add_double(props, "deesser_threshold", "De-esser Umbral (dB)");
    obs_property_double_set_range(p, -60.0, 0.0, 1.0);

    // Gate / expander
    obs_properties_add_bool(props, "gate_enabled", "Gate");
    p = obs_properties_add_double(props, "gate_threshold", "Gate Umbral (dB)");
    obs_property_double_set_range(p, -80.0, 0.0, 1.0);
    p = obs_properties_add_double(props, "gate_range", "Gate Rango (dB)");
    obs_property_double_set_range(p, 0.0, 60.0, 1.0);

    // Compresor
    obs_properties_add_bool(props, "comp_enabled", "Compresor");
    p = obs_properties_add_double(props, "comp_ratio", "Comp Ratio");
    obs_property_double_set_range(p, 1.0, 20.0, 0.5);
    p = obs_properties_add_double(props, "comp_threshold", "Comp Umbral (dB)");
    obs_property_double_set_range(p, -60.0, 0.0, 1.0);
    p = obs_properties_add_double(props, "comp_knee", "Comp Knee (dB)");
    obs_property_double_set_range(p, 0.0, 30.0, 1.0);

    // Limitador
    obs_properties_add_bool(props, "lim_enabled", "Limitador");
    p = obs_properties_add_double(props, "lim_ceiling", "Limitador Ceiling (dB)");
    obs_property_double_set_range(p, -20.0, 0.0, 0.5);

    // EQ de 4 bandas
    for (int i = 0; i < 4; ++i) {
        add_eq_band(props, i);
    }
    return props;
}

/// Construye un `StripParams` a partir del preset elegido + overrides del
/// usuario. El preset de fabrica da la base; los overrides lo refinan. `fs` se
/// fija a 48 kHz (reloj de libobs). Los clamps de rangos seguros los hace
/// `ChannelStrip::setParams` internamente.
///
/// :param s: settings de la fuente/filtro (obs_data).
/// :returns: `StripParams` ya armado para `ChannelStrip::setParams`.
ba::dsp::StripParams build_params(obs_data* s) {
    const char* preset = obs_data_get_string(s, "preset");
    ba::dsp::StripParams p;
    if (preset && std::strcmp(preset, "lavalier") == 0) {
        p = ba::dsp::presetLavalier();
    } else if (preset && std::strcmp(preset, "handheld") == 0) {
        p = ba::dsp::presetHandheld();
    } else if (preset && std::strcmp(preset, "neutral") == 0) {
        p = ba::dsp::StripParams{}; // cadena neutra por defecto
    } else {
        p = ba::dsp::presetPodium(); // por defecto: atril
    }
    p.fs = kFs;

    // --- Overrides HPF ---
    p.hpfFreq = obs_data_get_double(s, "hpf_freq");
    p.hpfSlopeDbOct = obs_data_get_double(s, "hpf_slope");

    // --- Overrides de-esser ---
    p.deEsserEnabled = obs_data_get_bool(s, "deesser_enabled");
    p.deEsserFreq = obs_data_get_double(s, "deesser_freq");
    p.deEsserRatio = obs_data_get_double(s, "deesser_ratio");
    p.deEsserThresholdDb = obs_data_get_double(s, "deesser_threshold");

    // --- Overrides gate ---
    p.gateEnabled = obs_data_get_bool(s, "gate_enabled");
    p.gateThresholdDb = obs_data_get_double(s, "gate_threshold");
    p.gateRangeDb = obs_data_get_double(s, "gate_range");

    // --- Overrides comp ---
    p.compEnabled = obs_data_get_bool(s, "comp_enabled");
    p.compRatio = obs_data_get_double(s, "comp_ratio");
    p.compThresholdDb = obs_data_get_double(s, "comp_threshold");
    p.compKneeDb = obs_data_get_double(s, "comp_knee");

    // --- Overrides limitador ---
    p.limEnabled = obs_data_get_bool(s, "lim_enabled");
    p.limCeilingDb = obs_data_get_double(s, "lim_ceiling");

    // --- Overrides EQ 4 bandas ---
    for (int i = 0; i < 4; ++i) {
        char key[64];
        std::snprintf(key, sizeof(key), "eq%d_enabled", i);
        p.eq[i].enabled = obs_data_get_bool(s, key);
        if (!p.eq[i].enabled) {
            continue;
        }
        std::snprintf(key, sizeof(key), "eq%d_type", i);
        const char* t = obs_data_get_string(s, key);
        if (t && std::strcmp(t, "lowshelf") == 0) {
            p.eq[i].type = ba::dsp::BiquadType::LowShelf;
        } else if (t && std::strcmp(t, "highshelf") == 0) {
            p.eq[i].type = ba::dsp::BiquadType::HighShelf;
        } else if (t && std::strcmp(t, "bandpass") == 0) {
            p.eq[i].type = ba::dsp::BiquadType::BandPass;
        } else {
            p.eq[i].type = ba::dsp::BiquadType::Peaking;
        }
        std::snprintf(key, sizeof(key), "eq%d_freq", i);
        p.eq[i].freq = obs_data_get_double(s, key);
        std::snprintf(key, sizeof(key), "eq%d_gain", i);
        p.eq[i].gainDb = obs_data_get_double(s, key);
        std::snprintf(key, sizeof(key), "eq%d_q", i);
        p.eq[i].q = obs_data_get_double(s, key);
    }
    return p;
}

// ============================================================================
// FILTRO de audio: `ba_channel_strip`
//
// Envuelve un ba::dsp::ChannelStrip por canal de entrada. El callback de
// audio aplica el strip in-place a la mezcla (RT-safe). `update` (thread de
// control) re-arma los parámetros con suavizado para cambiar de preset sin
// clicks.
// ============================================================================

/// Estado privado del filtro: un ChannelStrip por fuente.
struct ChannelStripFilter {
    ba::dsp::ChannelStrip strip;
};

/// Crea el filtro: asigna el ChannelStrip y aplica los parámetros iniciales.
///
/// :param settings: settings iniciales de la fuente.
/// :param source: fuente en creacion.
/// :returns: puntero al estado privado (se libera en `destroy`).
void* channel_strip_create(obs_data* settings, obs_source* source) {
    auto* f = new ChannelStripFilter();
    f->strip.setParams(build_params(settings), kSmoothMs);
    (void)source;
    return f;
}

/// Destruye el filtro y libera el estado privado.
///
/// :param source: fuente a destruir.
void channel_strip_destroy(obs_source* source) {
    delete static_cast<ChannelStripFilter*>(obs_source_get_private_data(source));
}

/// Actualiza los parámetros del strip desde las nuevas settings (thread de
/// control). Captura cualquier excepcion en el borde: si falla, el strip queda
/// con sus parametros previos.
///
/// :param source: fuente del filtro.
/// :param settings: nuevas settings.
void channel_strip_update(obs_source* source, obs_data* settings) {
    try {
        auto* f = static_cast<ChannelStripFilter*>(obs_source_get_private_data(source));
        if (f) {
            f->strip.setParams(build_params(settings), kSmoothMs);
        }
    } catch (...) {
        // Sin excepciones en el borde: el strip conserva sus parametros previos.
    }
}

/// Callback de audio del filtro (thread de audio, RT-safe). Aplica el
/// ChannelStrip in-place a la mezcla si es F32 entrelazado mono-plano; si no,
/// deja el buffer intacto (pass-through). Captura excepciones en el borde.
///
/// :param filter: fuente del filtro.
/// :param in: mezcla de audio de entrada (se modifica in-place).
/// :param timestamp: marca de tiempo a devolver.
/// :returns: el timestamp recibido (sin retardo).
uint64_t channel_strip_filter_audio(obs_source* filter, const audio_mix* in, uint64_t timestamp) {
    try {
        auto* f = static_cast<ChannelStripFilter*>(obs_source_get_private_data(filter));
        if (f && in && in->plane_ptr && in->format == AUDIO_FORMAT_F32 && in->planes == 1 &&
            in->frame_count > 0) {
            float* buf = static_cast<float*>(in->plane_ptr[0]);
            f->strip.process(buf, static_cast<int>(in->frame_count));
        }
    } catch (...) {
        // Pass-through si algo falla: no tocar el buffer.
    }
    return timestamp;
}

/// Devuelve la superficie de propiedades completa del channel strip.
///
/// :param source: fuente del filtro (no se usa).
/// :returns: propiedades nuevas.
obs_properties* channel_strip_get_properties(obs_source* source) {
    (void)source;
    return build_full_properties();
}

/// Descripcion del filtro `ba_channel_strip` (registrada con obs_register_source).
const obs_source_info channel_strip_info = {
    .id = "ba_channel_strip",
    .description = "Broadcast Appliance - Channel Strip (DSP nativo ba_core)",
    .unversioned_id = "ba_channel_strip",
    .create = channel_strip_create,
    .destroy = channel_strip_destroy,
    .update = channel_strip_update,
    .get_properties = channel_strip_get_properties,
    .filter_audio = channel_strip_filter_audio,
    .filter_video = nullptr,
};

// ============================================================================
// FUENTE async de audio: `ba_channel_preset`
//
// Expone un preset de canal como señal de referencia audible: sintetiza una
// voz sintetica de baja amplitud (armónicos graves + componente aguda para el
// de-esser, con modulación de amplitud tipo sílaba para que el gate/comp se
// activen) y la procesa con el ChannelStrip cargado con el preset elegido.
// El ingeniero puede oir el caracter de cada preset (podium/lavalier/handheld)
// sin microfono.
// ============================================================================

/// Estado privado de la fuente: un ChannelStrip + fase de la senal de prueba.
struct ChannelPresetSource {
    ba::dsp::ChannelStrip strip;
    double phase = 0.0; // fase de la senal de prueba, en segundos
};

/// Crea la fuente: asigna el ChannelStrip y aplica el preset inicial.
///
/// :param settings: settings iniciales.
/// :param source: fuente en creacion.
/// :returns: puntero al estado privado.
void* channel_preset_create(obs_data* settings, obs_source* source) {
    auto* src = new ChannelPresetSource();
    src->strip.setParams(build_params(settings), kSmoothMs);
    (void)source;
    return src;
}

/// Destruye la fuente y libera el estado privado.
///
/// :param source: fuente a destruir.
void channel_preset_destroy(obs_source* source) {
    delete static_cast<ChannelPresetSource*>(obs_source_get_private_data(source));
}

/// Cambia el preset del strip (thread de control). Captura excepciones en el
/// borde.
///
/// :param source: fuente.
/// :param settings: nuevas settings.
void channel_preset_update(obs_source* source, obs_data* settings) {
    try {
        auto* src = static_cast<ChannelPresetSource*>(obs_source_get_private_data(source));
        if (src) {
            src->strip.setParams(build_params(settings), kSmoothMs);
        }
    } catch (...) {
        // El strip conserva sus parametros previos.
    }
}

/// Callback de audio async (thread de audio, RT-safe). Rellena `frame->data[0]`
/// con la voz sintetica entrelazada, la procesa con el ChannelStrip del preset
/// elegido y devuelve el timestamp. Sin asignacion ni I/O. Captura excepciones
/// en el borde (rellena de silencio si falla).
///
/// :param source: fuente.
/// :param frame: frame de audio a rellenar (libobs preasigna data[0] y frames).
/// :param ts: marca de tiempo.
/// :returns: el timestamp recibido.
uint64_t channel_preset_audio_render(obs_source* source, frame_data* frame, uint64_t ts) {
    try {
        auto* src = static_cast<ChannelPresetSource*>(obs_source_get_private_data(source));
        if (!src || !frame) {
            return ts;
        }
        frame->format = AUDIO_FORMAT_F32;
        frame->channels = 2;
        frame->samples_per_sec = static_cast<uint32_t>(kFs);
        int n = static_cast<int>(frame->frames);
        float* buf = static_cast<float*>(frame->data[0]);
        if (!buf || n <= 0) {
            return ts;
        }
        for (int i = 0; i < n; ++i) {
            const double t = src->phase;
            // Modulacion de amplitud tipo silaba (3 Hz): gate/comp se activan.
            const double env = 0.5 + 0.5 * std::sin(kTwoPi * 3.0 * t);
            // Voz sintetica: armonicos graves + componente aguda (sibilancia)
            // para que el de-esser (lavalier) tenga algo que actuar.
            double s = 0.0;
            s += 0.50 * std::sin(kTwoPi * 150.0 * t);
            s += 0.30 * std::sin(kTwoPi * 300.0 * t);
            s += 0.20 * std::sin(kTwoPi * 450.0 * t);
            s += 0.15 * std::sin(kTwoPi * 6000.0 * t);
            s *= env * 0.15; // ~ -16 dBFS de pico
            buf[2 * i] = static_cast<float>(s);
            buf[2 * i + 1] = static_cast<float>(s);
            src->phase += 1.0 / kFs;
        }
        src->strip.process(buf, n);
        frame->timestamp = ts;
    } catch (...) {
        // Rellena de silencio si algo falla para no soltar basura.
        if (frame) {
            float* buf = static_cast<float*>(frame->data[0]);
            if (buf) {
                std::memset(buf, 0, static_cast<size_t>(frame->frames) * 2 * sizeof(float));
            }
        }
    }
    return ts;
}

/// Devuelve las propiedades de la fuente: solo el selector de preset (los
/// overrides finos viven en el filtro).
///
/// :param source: fuente (no se usa).
/// :returns: propiedades nuevas.
obs_properties* channel_preset_get_properties(obs_source* source) {
    (void)source;
    obs_properties* props = obs_properties_create();
    add_preset_list(props, "preset", "Preset de canal");
    return props;
}

/// Descripcion de la fuente `ba_channel_preset` (registrada con obs_register_source).
const obs_source_info channel_preset_info = {
    .id = "ba_channel_preset",
    .description = "Broadcast Appliance - Fuente de preset de canal (referencia audible)",
    .unversioned_id = "ba_channel_preset",
    .create = channel_preset_create,
    .destroy = channel_preset_destroy,
    .update = channel_preset_update,
    .video_render = nullptr,
    .audio_render = channel_preset_audio_render,
    .get_properties = channel_preset_get_properties,
};

} // namespace

// ============================================================================
// Entrypoint del modulo (alcance global, linkage C)
//
// Fuera del namespace anonimo: OBS busca obs_module_register/obs_module_
// unregister como simbolos exportados de la DLL. En el namespace anonimo
// tendrian enlace interno y no se exportarian.
// ============================================================================

/// Registro del modulo: OBS llama a esto al cargar la DLL. Registra la fuente
/// de preset y el filtro de channel strip.
///
/// :param module: puntero al modulo (lo gestiona libobs).
/// :param log: callback de log de libobs.
/// :param local: ruta local del modulo.
/// :param module_name: nombre del modulo.
extern "C" void obs_module_register(struct obs_module* module, obs_log_t log, const char* local,
                                    const char* module_name) {
    (void)module;
    (void)log;
    (void)local;
    (void)module_name;
    // VERIFY: firma de obs_module_register en obs-studio 31.0.0.
    obs_register_source(&channel_preset_info);
    obs_register_source(&channel_strip_info);
}

/// Desregistro del modulo: libobs limpia los registros al descargar la DLL; no
/// hay accion explicita.
extern "C" void obs_module_unregister(void) {
    // Los registros de fuentes/filtros los limpia el host al descargar el modulo.
}

#else // !_WIN32: esqueleto para que el arbol compile en Linux

/// Simbolo minimo exportado en Linux (esqueleto): garantiza que la shared lib
/// tenga al menos un simbolo. El contenido real de libobs esta tras
/// #ifdef _WIN32.
///
/// :returns: 0 siempre.
extern "C" int ba_sources_placeholder() { return 0; }

#endif // _WIN32
