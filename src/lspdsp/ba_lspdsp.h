/* ============================================================================
 * src/lspdsp/ba_lspdsp.h — el DSP de LSP (lsp-dsp-units) detrás de una API en C
 *
 * Por qué en C: en Windows LSP se compila con MinGW y la app con el ABI de MSVC;
 * las clases de C++ no cruzan esa frontera, las funciones en C sí. La biblioteca
 * (ba-lspdsp.dylib/.dll/.so) se compila con el mismo compilador que LSP, viaja en
 * el bundle (LGPL: aparte y reemplazable) y la app la carga en tiempo de ejecución
 * (src/app/audio/LspDsp.hpp). Ver docs/planes/PLAN-MEZCLADOR.md, fase L0.
 *
 * Reglas: objetos opacos (crear/procesar/leer/liberar), muestras float32 sin
 * intercalar (un puntero por canal), nada de excepciones ni memoria que cruce:
 * quien llama da los arreglos. Procesar es apto para el hilo de audio (no reserva
 * memoria); crear y configurar, no.
 * ==========================================================================*/
#ifndef BA_LSPDSP_H
#define BA_LSPDSP_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(BA_LSPDSP_COMPILANDO)
#    define BA_LSPDSP_API __declspec(dllexport)
#  else
#    define BA_LSPDSP_API __declspec(dllimport)
#  endif
#else
#  define BA_LSPDSP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Versión de esta API (sube si cambia algo de abajo) y de LSP. */
#define BA_LSPDSP_VERSION 1
BA_LSPDSP_API int ba_lspdsp_version(void);
BA_LSPDSP_API const char* ba_lspdsp_version_lsp(void);

/* --- Medidores de un par estéreo (o mono: der = NULL) ---------------------------
 * Loudness EBU R128 (momentáneo 400 ms, corto 3 s, integrado con compuerta), true
 * peak (sobremuestreado), pico, RMS, correlación (-1…+1) y paneo (0 = izq, 0,5 =
 * centro, 1 = der). Todo con los medidores de LSP. */
typedef struct ba_medidores ba_medidores;
typedef struct {
    float pico_db[2];      /* pico de muestra por canal desde la última lectura */
    float true_peak_db[2]; /* true peak por canal desde la última lectura */
    float rms_db[2];       /* RMS (300 ms) por canal */
    float lufs_m;          /* momentáneo (400 ms) */
    float lufs_s;          /* corto (3 s) */
    float lufs_i;          /* integrado (con compuerta, desde el último reinicio) */
    float correlacion;     /* -1…+1 (mono: 1) */
    float paneo;           /* 0…1 (mono: 0,5) */
} ba_medidores_lectura;
BA_LSPDSP_API ba_medidores* ba_medidores_crear(int canales, int frecuencia);
BA_LSPDSP_API void ba_medidores_procesar(ba_medidores* m, const float* izq, const float* der, size_t muestras);
/* Llena `out` y reinicia los picos (no el integrado). */
BA_LSPDSP_API void ba_medidores_leer(ba_medidores* m, ba_medidores_lectura* out);
BA_LSPDSP_API void ba_medidores_reiniciar_integrado(ba_medidores* m);
BA_LSPDSP_API void ba_medidores_liberar(ba_medidores* m);

/* --- Analizador de espectro (el FFT del Spectrum Analyzer de LSP) ----------------
 * rango: log2 del tamaño de la FFT (10 = 1024 … 15 = 32768). ventana y envolvente:
 * las de LSP (abajo). La envolvente rosa compensa -3 dB/oct: un ruido rosa se ve
 * plano, como en un RTA. reactividad en segundos (cuánto tarda en seguir). */
enum { BA_VENTANA_HANN = 0, BA_VENTANA_HAMMING = 1, BA_VENTANA_BLACKMAN = 2, BA_VENTANA_BLACKMAN_HARRIS = 11 };
enum { BA_ENVOLVENTE_VIOLETA = 0, BA_ENVOLVENTE_AZUL = 1, BA_ENVOLVENTE_BLANCA = 2, BA_ENVOLVENTE_ROSA = 3, BA_ENVOLVENTE_MARRON = 4 };
typedef struct ba_analizador ba_analizador;
BA_LSPDSP_API ba_analizador* ba_analizador_crear(int canales, int frecuencia, int rango);
BA_LSPDSP_API void ba_analizador_configurar(ba_analizador* a, int rango, int ventana, int envolvente, float reactividad);
BA_LSPDSP_API void ba_analizador_procesar(ba_analizador* a, const float* const* canales, size_t muestras);
/* `n` puntos entre `desde` y `hasta` Hz en escala logarítmica: frecuencias en
 * `frq` (puede ser NULL) y nivel en dB del canal en `db`. 0 si salió. */
BA_LSPDSP_API int ba_analizador_leer(ba_analizador* a, int canal, float desde, float hasta, size_t n, float* frq, float* db);
BA_LSPDSP_API void ba_analizador_liberar(ba_analizador* a);

/* --- Generador de ruido (blanco, rosa, rojo, azul, violeta; LCG, MLS o velvet) ---- */
typedef struct ba_ruido ba_ruido;
enum { BA_RUIDO_BLANCO = 0, BA_RUIDO_ROSA = 1, BA_RUIDO_ROJO = 2, BA_RUIDO_AZUL = 3, BA_RUIDO_VIOLETA = 4 };
enum { BA_RUIDO_LCG = 0, BA_RUIDO_MLS = 1, BA_RUIDO_VELVET = 2 };
BA_LSPDSP_API ba_ruido* ba_ruido_crear(int frecuencia, int color, int generador, float amplitud);
BA_LSPDSP_API void ba_ruido_generar(ba_ruido* r, float* dst, size_t muestras);
BA_LSPDSP_API void ba_ruido_liberar(ba_ruido* r);

/* --- Detector de latencia (el del Latency Meter de LSP) --------------------------
 * Emite un chirp por `salida` (lo que devuelve procesar en `dst`) y lo busca en lo
 * que vuelve por `entrada`. Se arranca, se procesa en bloques y se lee. */
typedef struct ba_latencia ba_latencia;
BA_LSPDSP_API ba_latencia* ba_latencia_crear(int frecuencia, float duracion_s);
BA_LSPDSP_API void ba_latencia_arrancar(ba_latencia* l);
BA_LSPDSP_API void ba_latencia_procesar(ba_latencia* l, float* salida, const float* entrada, size_t muestras);
/* 1 si ya terminó; `ms` la latencia (o -1 si no la encontró). */
BA_LSPDSP_API int ba_latencia_resultado(ba_latencia* l, float* ms);
BA_LSPDSP_API void ba_latencia_liberar(ba_latencia* l);

#ifdef __cplusplus
}
#endif

#endif /* BA_LSPDSP_H */
