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
#include <lsp-plug.in/dsp-units/util/Sidechain.h>

#include <initializer_list>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

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

} // extern "C"
