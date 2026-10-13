// ============================================================================
// src/lspdsp/ba_lspdsp.cpp — ver ba_lspdsp.h
//
// Se compila con el MISMO compilador y las mismas opciones que LSP (sin
// excepciones ni RTTI) y se enlaza con sus objetos (lsp-common-lib, lsp-lltl-lib,
// lsp-runtime-lib, lsp-dsp-lib, lsp-dsp-units): tools/deps/lspdsp.sh. El uso de
// cada medidor sigue al plugin Referencer de LSP (lsp-plugins-referencer), que
// mide lo mismo: pico, true peak, RMS, LUFS M/S/I, correlación y paneo.
// ============================================================================
#define BA_LSPDSP_COMPILANDO 1
#include "ba_lspdsp.h"

#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/meters/Correlometer.h>
#include <lsp-plug.in/dsp-units/meters/ILUFSMeter.h>
#include <lsp-plug.in/dsp-units/meters/LoudnessMeter.h>
#include <lsp-plug.in/dsp-units/meters/Panometer.h>
#include <lsp-plug.in/dsp-units/meters/TruePeakMeter.h>
#include <lsp-plug.in/dsp-units/misc/broadcast.h>
#include <lsp-plug.in/dsp-units/misc/envelope.h>
#include <lsp-plug.in/dsp-units/misc/windows.h>
#include <lsp-plug.in/dsp-units/noise/Generator.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/dsp-units/util/LatencyDetector.h>
#include <lsp-plug.in/dsp-units/util/ResponseTaker.h>
#include <lsp-plug.in/dsp-units/util/Sidechain.h>
#include <lsp-plug.in/dsp-units/util/SyncChirpProcessor.h>
#include <lsp-plug.in/dsp-units/sampling/Sample.h>

#include <initializer_list>
#include <algorithm>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace lsp;

namespace {

// Bloque de trabajo: lo de entrada se procesa en trozos de este tamaño (los
// búferes de salida de los medidores van por muestra).
constexpr size_t kBloque = 1024;

void iniciarDsp() {
    static bool hecho = false;
    if (!hecho) {
        dsp::init();
        hecho = true;
    }
}

float aDb(float g) { return g > 1e-10f ? dspu::gain_to_db(g) : -200.0f; }

} // namespace

extern "C" {

int ba_lspdsp_version(void) { return BA_LSPDSP_VERSION; }
const char* ba_lspdsp_version_lsp(void) { return "1.2.35"; }

// --- Medidores ------------------------------------------------------------------

struct ba_medidores {
    int canales = 1;
    dspu::TruePeakMeter tp[2];
    dspu::Sidechain rms[2];
    dspu::LoudnessMeter momentaneo, corto;
    dspu::ILUFSMeter integrado;
    dspu::Correlometer correlacion;
    dspu::Panometer paneo;
    float b1[kBloque], b2[kBloque];
    // Lo acumulado desde la última lectura.
    float pico[2] = {0, 0}, tpMax[2] = {0, 0}, rmsUlt[2] = {0, 0};
    float m = 0, s = 0, i = 0, corr = 1, pan = 0.5f;
};

ba_medidores* ba_medidores_crear(int canales, int frecuencia) {
    iniciarDsp();
    ba_medidores* m = new (std::nothrow) ba_medidores();
    if (m == nullptr) return nullptr;
    m->canales = canales >= 2 ? 2 : 1;
    const size_t n = static_cast<size_t>(m->canales);
    for (size_t c = 0; c < n; ++c) {
        m->tp[c].init();
        m->tp[c].set_sample_rate(static_cast<uint32_t>(frecuencia));
        m->tp[c].update_settings(); // sin esto no arma el sobremuestreo y da 0
        m->rms[c].init(1, dspu::bs::LUFS_MEASURE_PERIOD_MS);
        m->rms[c].set_mode(dspu::SCM_RMS);
        m->rms[c].set_source(dspu::SCS_MIDDLE);
        m->rms[c].set_reactivity(300.0f);
        m->rms[c].set_sample_rate(static_cast<size_t>(frecuencia));
    }
    m->momentaneo.init(n, dspu::bs::LUFS_MOMENTARY_PERIOD);
    m->momentaneo.set_period(dspu::bs::LUFS_MOMENTARY_PERIOD);
    m->corto.init(n, dspu::bs::LUFS_SHORT_TERM_PERIOD);
    m->corto.set_period(dspu::bs::LUFS_SHORT_TERM_PERIOD);
    m->integrado.init(n, 0, dspu::bs::LUFS_MOMENTARY_PERIOD);
    m->integrado.set_weighting(dspu::bs::WEIGHT_K);
    for (dspu::LoudnessMeter* lm : {&m->momentaneo, &m->corto}) {
        lm->set_weighting(dspu::bs::WEIGHT_K);
        lm->set_sample_rate(static_cast<size_t>(frecuencia));
    }
    m->integrado.set_sample_rate(static_cast<size_t>(frecuencia));
    if (n == 2) {
        m->momentaneo.set_designation(0, dspu::bs::CHANNEL_LEFT);
        m->momentaneo.set_designation(1, dspu::bs::CHANNEL_RIGHT);
        m->corto.set_designation(0, dspu::bs::CHANNEL_LEFT);
        m->corto.set_designation(1, dspu::bs::CHANNEL_RIGHT);
        m->integrado.set_active(0, true);
        m->integrado.set_active(1, true);
        m->integrado.set_designation(0, dspu::bs::CHANNEL_LEFT);
        m->integrado.set_designation(1, dspu::bs::CHANNEL_RIGHT);
    } else {
        m->momentaneo.set_designation(0, dspu::bs::CHANNEL_CENTER);
        m->corto.set_designation(0, dspu::bs::CHANNEL_CENTER);
        m->integrado.set_active(0, true);
        m->integrado.set_designation(0, dspu::bs::CHANNEL_CENTER);
    }
    m->momentaneo.set_active(0, true);
    m->corto.set_active(0, true);
    if (n == 2) {
        m->momentaneo.set_active(1, true);
        m->corto.set_active(1, true);
    }
    // Correlación y paneo: en ventanas de 300 ms, como el Referencer.
    const size_t periodo = static_cast<size_t>(frecuencia * 0.3f);
    m->correlacion.init(periodo);
    m->correlacion.set_period(periodo);
    m->correlacion.clear();
    m->paneo.init(periodo);
    m->paneo.set_period(periodo);
    m->paneo.set_pan_law(dspu::PAN_LAW_EQUAL_POWER);
    m->paneo.set_default_pan(0.5f);
    m->paneo.clear();
    return m;
}

void ba_medidores_procesar(ba_medidores* m, const float* izq, const float* der, size_t muestras) {
    if (m == nullptr || izq == nullptr) return;
    const bool estereo = m->canales == 2 && der != nullptr;
    for (size_t hecho = 0; hecho < muestras;) {
        const size_t n = muestras - hecho < kBloque ? muestras - hecho : kBloque;
        const float* l = izq + hecho;
        const float* r = estereo ? der + hecho : l;
        const float* ch[2] = {l, r};
        for (int c = 0; c < (estereo ? 2 : 1); ++c) {
            m->pico[c] = fmaxf(m->pico[c], dsp::abs_max(ch[c], n));
            // Como el Referencer: process() deja el true peak por muestra y se toma el
            // máximo (process_max mira solo una parte del búfer sobremuestreado).
            m->tp[c].process(m->b1, ch[c], n);
            m->tpMax[c] = fmaxf(m->tpMax[c], dsp::abs_max(m->b1, n));
            const float* in[1] = {ch[c]};
            m->rms[c].process(m->b1, in, n);
            m->rmsUlt[c] = m->b1[n - 1];
        }
        m->momentaneo.bind(0, nullptr, l, 0);
        if (estereo) m->momentaneo.bind(1, nullptr, r, 0);
        m->momentaneo.process(m->b2, n, dspu::bs::DBFS_TO_LUFS_SHIFT_GAIN);
        m->m = m->b2[n - 1];
        m->corto.bind(0, nullptr, l, 0);
        if (estereo) m->corto.bind(1, nullptr, r, 0);
        m->corto.process(m->b2, n, dspu::bs::DBFS_TO_LUFS_SHIFT_GAIN);
        m->s = m->b2[n - 1];
        m->integrado.bind(0, l);
        if (estereo) m->integrado.bind(1, r);
        m->integrado.process(m->b2, n, dspu::bs::DBFS_TO_LUFS_SHIFT_GAIN);
        m->i = m->b2[n - 1];
        if (estereo) {
            m->correlacion.process(m->b2, l, r, n);
            m->corr = m->b2[n - 1];
            m->paneo.process(m->b2, l, r, n);
            m->pan = m->b2[n - 1];
        }
        hecho += n;
    }
}

void ba_medidores_leer(ba_medidores* m, ba_medidores_lectura* out) {
    if (m == nullptr || out == nullptr) return;
    for (int c = 0; c < 2; ++c) {
        const int k = m->canales == 2 ? c : 0;
        out->pico_db[c] = aDb(m->pico[k]);
        out->true_peak_db[c] = aDb(m->tpMax[k]);
        out->rms_db[c] = aDb(m->rmsUlt[k]);
    }
    out->lufs_m = aDb(m->m);
    out->lufs_s = aDb(m->s);
    out->lufs_i = aDb(m->i);
    out->correlacion = m->canales == 2 ? m->corr : 1.0f;
    out->paneo = m->canales == 2 ? m->pan : 0.5f;
    m->pico[0] = m->pico[1] = 0;
    m->tpMax[0] = m->tpMax[1] = 0;
}

void ba_medidores_reiniciar_integrado(ba_medidores* m) {
    if (m != nullptr) m->integrado.clear();
}

void ba_medidores_liberar(ba_medidores* m) {
    if (m == nullptr) return;
    for (int c = 0; c < 2; ++c) {
        m->tp[c].destroy();
        m->rms[c].destroy();
    }
    m->momentaneo.destroy();
    m->corto.destroy();
    m->integrado.destroy();
    m->correlacion.destroy();
    m->paneo.destroy();
    delete m;
}

// --- Analizador -----------------------------------------------------------------

struct ba_analizador {
    dspu::Analyzer a;
    int canales = 1;
    int frecuencia = 48000;
    // Las frecuencias pedidas la última vez (se recalculan si cambian).
    float desde = 0, hasta = 0;
    size_t n = 0;
    uint32_t* idx = nullptr;
    float* frq = nullptr;
};

namespace {
// Arma el analizador de LSP de cero con esta configuración. Reconfigurar en caliente
// uno que ya venía procesando (set_rank/set_envelope + reconfigure) da niveles
// absurdos compilado con gcc (Linux): +110 dB y la envolvente al revés; uno nuevo
// anda en los tres sistemas. Configurar no es del hilo de audio: se puede reservar.
bool armarAnalizador(ba_analizador* a, int rango, int ventana, int envolvente, float reactividad) {
    a->a.destroy();
    a->a.construct();
    // Hasta FFT de 32768 (rango 15) y hasta 192 kHz; se refresca al menos 10 veces por segundo.
    if (!a->a.init(static_cast<size_t>(a->canales), 15, 192000, 10.0f)) return false;
    a->a.set_sample_rate(static_cast<size_t>(a->frecuencia));
    a->a.set_rate(20.0f);
    for (int c = 0; c < a->canales; ++c) a->a.enable_channel(static_cast<size_t>(c), true);
    const int r = rango < 10 ? 10 : rango > 15 ? 15 : rango;
    a->a.set_rank(static_cast<size_t>(r));
    a->a.set_window(static_cast<size_t>(ventana));
    a->a.set_envelope(static_cast<size_t>(envolvente));
    a->a.set_reactivity(reactividad > 0 ? reactividad : 0.2f);
    a->a.set_activity(true);
    a->a.reconfigure();
    a->n = 0; // las frecuencias se recalculan
    return true;
}
} // namespace

ba_analizador* ba_analizador_crear(int canales, int frecuencia, int rango) {
    iniciarDsp();
    ba_analizador* a = new (std::nothrow) ba_analizador();
    if (a == nullptr) return nullptr;
    a->canales = canales < 1 ? 1 : canales;
    a->frecuencia = frecuencia;
    a->a.construct();
    if (!armarAnalizador(a, rango, BA_VENTANA_HANN, BA_ENVOLVENTE_ROSA, 0.2f)) {
        a->a.destroy();
        delete a;
        return nullptr;
    }
    return a;
}

void ba_analizador_configurar(ba_analizador* a, int rango, int ventana, int envolvente, float reactividad) {
    if (a != nullptr) armarAnalizador(a, rango, ventana, envolvente, reactividad);
}

void ba_analizador_procesar(ba_analizador* a, const float* const* canales, size_t muestras) {
    if (a == nullptr || canales == nullptr) return;
    if (a->a.needs_reconfiguration()) a->a.reconfigure();
    a->a.process(canales, muestras);
}

int ba_analizador_leer(ba_analizador* a, int canal, float desde, float hasta, size_t n, float* frq, float* db) {
    if (a == nullptr || db == nullptr || n == 0 || canal < 0 || canal >= a->canales) return -1;
    if (n != a->n || desde != a->desde || hasta != a->hasta) {
        free(a->idx);
        free(a->frq);
        a->idx = static_cast<uint32_t*>(malloc(n * sizeof(uint32_t)));
        a->frq = static_cast<float*>(malloc(n * sizeof(float)));
        if (a->idx == nullptr || a->frq == nullptr) return -1;
        a->a.get_frequencies(a->frq, a->idx, desde, hasta, n);
        a->n = n;
        a->desde = desde;
        a->hasta = hasta;
    }
    if (!a->a.get_spectrum(static_cast<size_t>(canal), db, a->idx, n)) return -1;
    for (size_t k = 0; k < n; ++k) db[k] = aDb(db[k]);
    if (frq != nullptr) memcpy(frq, a->frq, n * sizeof(float));
    return 0;
}

void ba_analizador_liberar(ba_analizador* a) {
    if (a == nullptr) return;
    a->a.destroy();
    free(a->idx);
    free(a->frq);
    delete a;
}

// --- Ruido ----------------------------------------------------------------------

struct ba_ruido {
    dspu::NoiseGenerator g;
};

ba_ruido* ba_ruido_crear(int frecuencia, int color, int generador, float amplitud) {
    iniciarDsp();
    ba_ruido* r = new (std::nothrow) ba_ruido();
    if (r == nullptr) return nullptr;
    r->g.init();
    r->g.set_sample_rate(static_cast<size_t>(frecuencia));
    // El orden de LSP no es el de la API: se traduce de a uno.
    r->g.set_generator(generador == BA_RUIDO_MLS ? dspu::NG_GEN_MLS
                       : generador == BA_RUIDO_VELVET ? dspu::NG_GEN_VELVET
                                                      : dspu::NG_GEN_LCG);
    dspu::ng_color_t c = dspu::NG_COLOR_WHITE;
    switch (color) {
        case BA_RUIDO_ROSA: c = dspu::NG_COLOR_PINK; break;
        case BA_RUIDO_ROJO: c = dspu::NG_COLOR_RED; break;
        case BA_RUIDO_AZUL: c = dspu::NG_COLOR_BLUE; break;
        case BA_RUIDO_VIOLETA: c = dspu::NG_COLOR_VIOLET; break;
        default: break;
    }
    r->g.set_noise_color(c);
    r->g.set_amplitude(amplitud);
    return r;
}

void ba_ruido_generar(ba_ruido* r, float* dst, size_t muestras) {
    if (r != nullptr && dst != nullptr) r->g.process_overwrite(dst, muestras);
}

void ba_ruido_liberar(ba_ruido* r) { delete r; }

// --- Latencia -------------------------------------------------------------------

struct ba_latencia {
    dspu::LatencyDetector d;
};

ba_latencia* ba_latencia_crear(int frecuencia, float duracion_s) {
    iniciarDsp();
    ba_latencia* l = new (std::nothrow) ba_latencia();
    if (l == nullptr) return nullptr;
    l->d.init();
    l->d.set_sample_rate(static_cast<size_t>(frecuencia));
    l->d.set_duration(duracion_s > 0 ? duracion_s : 0.15f);
    l->d.set_op_fading(0.03f);
    l->d.set_op_pause(0.025f);
    l->d.set_peak_threshold(0.5f);
    l->d.set_abs_threshold(0.01f);
    l->d.set_ip_detection(1.0f);
    l->d.update_settings();
    return l;
}

void ba_latencia_arrancar(ba_latencia* l) {
    if (l != nullptr) l->d.start_capture();
}

void ba_latencia_procesar(ba_latencia* l, float* salida, const float* entrada, size_t muestras) {
    if (l == nullptr || salida == nullptr || entrada == nullptr) return;
    if (l->d.needs_update()) l->d.update_settings();
    l->d.process(salida, entrada, muestras);
}

int ba_latencia_resultado(ba_latencia* l, float* ms) {
    if (l == nullptr) return 0;
    if (!l->d.cycle_complete()) return 0;
    if (ms != nullptr) *ms = l->d.latency_detected() ? l->d.get_latency_seconds() * 1000.0f : -1.0f;
    return 1;
}

void ba_latencia_liberar(ba_latencia* l) {
    if (l == nullptr) return;
    l->d.destroy();
    delete l;
}

// --- Medición con barrido (lo del Profiler de LSP) ------------------------------

struct ba_medicion {
    dspu::SyncChirpProcessor scp;
    dspu::ResponseTaker rt;
    int frecuencia = 48000;
    size_t latencia = 0;
    bool analizada = false;
    // La IR (lineal) y dónde está el sonido directo, después de analizar.
    std::vector<float> ir;
    size_t pico = 0;
    float b[kBloque];
};

// Los valores del Profiler de LSP (lsp-plugins-profiler/src/main/plug/profiler.cpp).
constexpr float kReactividad = 0.085f; // ventana del seguidor de la envolvente de la IR [s]
constexpr double kTolerancia = 3.0;    // dB sobre el piso de ruido donde se da por terminada la IR

ba_medicion* ba_medicion_crear(int frecuencia, float duracion_s, float f_desde, float f_hasta, float amplitud,
                               float latencia_ms) {
    iniciarDsp();
    ba_medicion* m = new (std::nothrow) ba_medicion();
    if (m == nullptr) return nullptr;
    m->frecuencia = frecuencia;
    if (!m->scp.init()) {
        delete m;
        return nullptr;
    }
    m->scp.set_chirp_synthesis_method(dspu::SCP_SYNTH_BANDLIMITED);
    m->scp.set_chirp_initial_frequency(f_desde > 0 ? f_desde : 20.0);
    m->scp.set_chirp_final_frequency(f_hasta > 0 ? f_hasta : 20000.0);
    m->scp.set_chirp_amplitude(amplitud > 0 ? amplitud : 0.5f);
    m->scp.set_fader_fading_method(dspu::SCP_FADE_RAISED_COSINES);
    m->scp.set_fader_fadein(0.500f);
    m->scp.set_fader_fadeout(0.020f);
    m->scp.set_oversampler_mode(dspu::OM_LANCZOS_8X2);
    m->scp.set_sample_rate(static_cast<size_t>(frecuencia));
    m->scp.set_chirp_duration(duracion_s < 1 ? 1.0f : duracion_s > 50 ? 50.0f : duracion_s);
    if (m->scp.reconfigure() != STATUS_OK) {
        m->scp.destroy();
        delete m;
        return nullptr;
    }
    m->rt.init();
    m->rt.set_sample_rate(static_cast<size_t>(frecuencia));
    m->rt.set_op_fading(0.030f);
    m->rt.set_op_pause(0.025f);
    m->rt.set_op_tail(m->scp.get_chirp_duration_seconds());
    m->latencia = latencia_ms > 0 ? static_cast<size_t>(latencia_ms * 0.001f * frecuencia) : 0;
    m->rt.set_latency_samples(static_cast<ssize_t>(m->latencia));
    if (m->rt.reconfigure(m->scp.get_chirp()) != STATUS_OK) {
        m->rt.destroy();
        m->scp.destroy();
        delete m;
        return nullptr;
    }
    return m;
}

void ba_medicion_arrancar(ba_medicion* m) {
    if (m == nullptr) return;
    m->analizada = false;
    m->rt.start_capture();
}

void ba_medicion_procesar(ba_medicion* m, float* salida, const float* entrada, size_t muestras) {
    if (m == nullptr || salida == nullptr || entrada == nullptr) return;
    if (m->rt.needs_update()) m->rt.update_settings();
    for (size_t hecho = 0; hecho < muestras;) {
        const size_t n = muestras - hecho < kBloque ? muestras - hecho : kBloque;
        // Como el Profiler: process_in toma lo capturado y process_out escribe lo que se emite.
        m->rt.process_in(m->b, entrada + hecho, n);
        m->rt.process_out(salida + hecho, m->b, n);
        hecho += n;
    }
}

int ba_medicion_terminada(ba_medicion* m) { return m != nullptr && m->rt.cycle_complete() ? 1 : 0; }

size_t ba_medicion_duracion_muestras(ba_medicion* m) {
    if (m == nullptr || m->scp.get_chirp() == nullptr) return 0;
    // Pausa y fundidos a los dos lados, el barrido, la cola (= el barrido) y la latencia.
    const size_t extras = static_cast<size_t>((0.030f + 0.025f) * 2 * m->frecuencia);
    return m->scp.get_chirp()->length() * 2 + extras + m->latencia;
}

int ba_medicion_analizar(ba_medicion* m, int algoritmo, ba_medicion_resultado* r) {
    if (m == nullptr || r == nullptr || !m->rt.cycle_complete()) return -1;
    dspu::Sample* cap = m->rt.get_capture();
    if (cap == nullptr) return -1;
    dspu::Sample* datos[1] = {cap};
    size_t desde[1] = {m->rt.get_capture_start()};
    if (m->scp.do_linear_convolutions(datos, desde, 1, 0) != STATUS_OK) return -1;
    // La IR es la convolución entera: el tiempo cero está en el medio (como lo
    // cuenta SyncChirpProcessor) y lo positivo dura lo que la cola de la toma.
    dspu::Sample* cr = m->scp.get_convolution_result();
    if (cr == nullptr || cr->length() < 4) return -1;
    const float* x = cr->channel(0);
    const size_t medio = cr->length() / 2 - 1;
    const size_t largo = m->scp.get_chirp() != nullptr ? m->scp.get_chirp()->length() : cr->length() - medio;
    const size_t n = medio + largo < cr->length() ? medio + largo : cr->length();
    // El sonido directo: el pico, buscado solo en el tiempo positivo.
    size_t p = medio;
    float mx = 0;
    for (size_t k = medio; k < n; ++k)
        if (fabsf(x[k]) > mx) {
            mx = fabsf(x[k]);
            p = k;
        }
    m->ir.assign(x + medio, x + n);
    m->pico = p - medio;
    r->directo_ms = static_cast<float>(p - medio) * 1000.0f / static_cast<float>(m->frecuencia);
    r->pico_db = aDb(mx);
    // El ruido: el RMS del último 10 % de la parte de la IR posterior al pico.
    const size_t cola0 = p + (n - p) * 9 / 10;
    double e = 0;
    for (size_t k = cola0; k < n; ++k) e += static_cast<double>(x[k]) * x[k];
    const double rmsCola = n > cola0 ? sqrt(e / static_cast<double>(n - cola0)) : 0;
    r->snr_db = rmsCola > 0 ? 20.0f * log10f(mx / static_cast<float>(rmsCola)) : 120.0f;

    // El RT de LSP, desde el directo (no desde el tiempo cero: el viaje del sonido no es reverberación).
    const dspu::scp_rtcalc_t alg = algoritmo == BA_RT_T30   ? dspu::SCP_RT_T_30
                                   : algoritmo == BA_RT_T20 ? dspu::SCP_RT_T_20
                                                            : dspu::SCP_RT_EDT_0;
    if (m->scp.postprocess_linear_convolution(0, static_cast<ssize_t>(p - medio), alg, kReactividad, kTolerancia) !=
        STATUS_OK)
        return -1;
    r->rt60_s = m->scp.get_reverberation_time_seconds();
    r->correlacion = m->scp.get_reverberation_correlation();
    r->limite_integracion_s = m->scp.get_integration_limit_seconds();
    r->ruido_suficiente = m->scp.get_background_noise_optimality() ? 1 : 0;
    m->analizada = true;
    return 0;
}

size_t ba_medicion_ir(ba_medicion* m, float* dst, size_t max, float antes_ms) {
    if (m == nullptr || dst == nullptr || !m->analizada || m->ir.empty()) return 0;
    const size_t antes = static_cast<size_t>(antes_ms > 0 ? antes_ms * 0.001f * m->frecuencia : 0);
    const size_t desde = m->pico > antes ? m->pico - antes : 0;
    const float pk = fabsf(m->ir[m->pico]) > 0 ? fabsf(m->ir[m->pico]) : 1.0f;
    size_t k = 0;
    for (; k < max && desde + k < m->ir.size(); ++k) dst[k] = m->ir[desde + k] / pk;
    return k;
}

int ba_medicion_respuesta(ba_medicion* m, float desde, float hasta, size_t n, float ventana_ms, float* frq, float* db) {
    if (m == nullptr || db == nullptr || n == 0 || !m->analizada || desde <= 0 || hasta <= desde) return -1;
    const double sr = m->frecuencia;
    // La ventana: 2 ms antes del directo y `ventana_ms` después, con la última décima
    // en media Hann (sin escalón al cortar).
    const size_t pre = static_cast<size_t>(0.002 * sr);
    const size_t i0 = m->pico > pre ? m->pico - pre : 0;
    size_t len = static_cast<size_t>((ventana_ms > 1 ? ventana_ms : 1) * 0.001 * sr) + pre;
    if (i0 + len > m->ir.size()) len = m->ir.size() - i0;
    std::vector<double> w(len);
    const size_t cae = len / 10 > 0 ? len / 10 : 1;
    for (size_t k = 0; k < len; ++k) {
        double g = 1.0;
        if (k + cae >= len) g = 0.5 * (1.0 + cos(M_PI * static_cast<double>(k + cae - len + 1) / static_cast<double>(cae)));
        w[k] = m->ir[i0 + k] * g;
    }
    for (size_t j = 0; j < n; ++j) {
        const double f = desde * pow(static_cast<double>(hasta) / desde, n > 1 ? static_cast<double>(j) / (n - 1) : 0.0);
        // La transformada en esa frecuencia (Goertzel: barata y exacta en f).
        const double c = 2.0 * cos(2.0 * M_PI * f / sr);
        double s1 = 0, s2 = 0;
        for (size_t k = 0; k < len; ++k) {
            const double s0 = w[k] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double pot = s1 * s1 + s2 * s2 - c * s1 * s2;
        db[j] = pot > 1e-30 ? static_cast<float>(10.0 * log10(pot)) : -300.0f;
        if (frq != nullptr) frq[j] = static_cast<float>(f);
    }
    return 0;
}

namespace {
// Un pasabanda de segundo orden (RBJ), aplicado ida y vuelta (fase cero, más empinado).
void pasabanda(std::vector<double>& x, double fc, double sr) {
    const double w0 = 2.0 * M_PI * fc / sr, q = 1.4142;
    const double alfa = sin(w0) / (2.0 * q), a0 = 1.0 + alfa;
    const double b0 = alfa / a0, b2 = -alfa / a0, a1 = -2.0 * cos(w0) / a0, a2 = (1.0 - alfa) / a0;
    for (int pasada = 0; pasada < 2; ++pasada) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (size_t k = 0; k < x.size(); ++k) {
            const double xi = x[k];
            const double y = b0 * xi + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = xi;
            y2 = y1;
            y1 = y;
            x[k] = y;
        }
        std::reverse(x.begin(), x.end());
    }
}
// RT desde la curva de decaimiento (dB): la recta entre `arriba` y `abajo` dB, extrapolada a -60.
float rtDe(const std::vector<double>& edc, double sr, double arriba, double abajo) {
    size_t i = 0;
    while (i < edc.size() && edc[i] > arriba) ++i;
    size_t j = i;
    while (j < edc.size() && edc[j] > abajo) ++j;
    if (j >= edc.size() || j <= i + 2) return -1;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double nn = static_cast<double>(j - i);
    for (size_t k = i; k < j; ++k) {
        const double t = static_cast<double>(k) / sr;
        sx += t;
        sy += edc[k];
        sxx += t * t;
        sxy += t * edc[k];
    }
    const double pendiente = (nn * sxy - sx * sy) / (nn * sxx - sx * sx); // dB/s (negativa)
    return pendiente < 0 ? static_cast<float>(-60.0 / pendiente) : -1;
}
} // namespace

int ba_medicion_rt_bandas(ba_medicion* m, const float* centros, size_t n, float* edt, float* t20, float* t30) {
    if (m == nullptr || centros == nullptr || !m->analizada) return -1;
    const double sr = m->frecuencia;
    const size_t pre = static_cast<size_t>(0.002 * sr);
    const size_t i0 = m->pico > pre ? m->pico - pre : 0;
    for (size_t b = 0; b < n; ++b) {
        std::vector<double> x(m->ir.begin() + static_cast<std::ptrdiff_t>(i0), m->ir.end());
        if (centros[b] > 0) pasabanda(x, centros[b], sr);
        // El piso de ruido: la energía media del último 10 %; se integra solo hasta donde
        // la envolvente (10 ms) baja a 3 dB sobre ese piso (como la tolerancia de LSP).
        const size_t len = x.size();
        const size_t cola0 = len * 9 / 10;
        double ruido = 0;
        for (size_t k = cola0; k < len; ++k) ruido += x[k] * x[k];
        ruido /= static_cast<double>(len - cola0 > 0 ? len - cola0 : 1);
        const size_t vent = static_cast<size_t>(0.010 * sr);
        size_t fin = len;
        double acum = 0;
        for (size_t k = 0; k < len; ++k) {
            acum += x[k] * x[k];
            if (k >= vent) acum -= x[k - vent] * x[k - vent];
            if (k > vent && k > static_cast<size_t>(0.02 * sr) && acum / static_cast<double>(vent) < ruido * 2.0) {
                fin = k;
                break;
            }
        }
        // Schroeder: la energía que queda de acá al final, sin el ruido.
        std::vector<double> edc(fin);
        double resto = 0;
        for (size_t k = fin; k-- > 0;) {
            resto += x[k] * x[k] - ruido;
            edc[k] = resto > 0 ? resto : 1e-30;
        }
        const double e0 = edc.empty() ? 1.0 : edc[0];
        for (double& v : edc) v = 10.0 * log10(v / e0);
        if (edt != nullptr) edt[b] = rtDe(edc, sr, -0.0001, -10.0);
        if (t20 != nullptr) t20[b] = rtDe(edc, sr, -5.0, -25.0);
        if (t30 != nullptr) t30[b] = rtDe(edc, sr, -5.0, -35.0);
    }
    return 0;
}

void ba_medicion_liberar(ba_medicion* m) {
    if (m == nullptr) return;
    m->rt.destroy();
    m->scp.destroy();
    delete m;
}

} // extern "C"
