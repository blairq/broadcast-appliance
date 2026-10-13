/* test_lspdsp.c — ba-lspdsp mide lo que tiene que medir (señales conocidas)
 *
 * - Seno de 1 kHz a -23 dBFS en los dos canales: -23 LUFS (EBU Tech 3341), pico
 *   -23 dBFS, RMS -26 dB, correlación 1, paneo 0,5.
 * - El analizador encuentra el pico en 1 kHz.
 * - Ruido rosa (generador de LSP) con envolvente rosa: plano (±3 dB) de 100 Hz a 10 kHz.
 * - Latencia: un retardo de 480 muestras a 48 kHz se mide como 10 ms.
 */
#include "ba_lspdsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallas = 0;
static void check(int ok, const char* que, double valor) {
    printf("%s: %s (%.2f)\n", ok ? "ok" : "FALLA", que, valor);
    if (!ok) ++fallas;
}

#define SR 48000
#define N (SR * 6) /* 6 s */

int main(void) {
    printf("ba-lspdsp %d (LSP %s)\n", ba_lspdsp_version(), ba_lspdsp_version_lsp());
    static float l[N], r[N];
    const float amp = powf(10.0f, -23.0f / 20.0f);
    for (int i = 0; i < N; ++i) l[i] = r[i] = amp * sinf(2.0f * 3.14159265f * 1000.0f * i / SR);

    /* Medidores */
    ba_medidores* m = ba_medidores_crear(2, SR);
    check(m != NULL, "se crean los medidores", 0);
    for (int i = 0; i < N; i += 480) ba_medidores_procesar(m, l + i, r + i, 480);
    ba_medidores_lectura x;
    ba_medidores_leer(m, &x);
    check(fabsf(x.lufs_m + 23.0f) < 0.3f, "LUFS momentáneo -23", x.lufs_m);
    check(fabsf(x.lufs_s + 23.0f) < 0.3f, "LUFS corto -23", x.lufs_s);
    check(fabsf(x.lufs_i + 23.0f) < 0.3f, "LUFS integrado -23", x.lufs_i);
    check(fabsf(x.pico_db[0] + 23.0f) < 0.1f, "pico -23 dBFS", x.pico_db[0]);
    check(fabsf(x.true_peak_db[0] + 23.0f) < 0.3f, "true peak -23", x.true_peak_db[0]);
    check(fabsf(x.rms_db[0] + 26.0f) < 0.5f, "RMS -26 dB", x.rms_db[0]);
    check(x.correlacion > 0.99f, "correlación 1", x.correlacion);
    check(fabsf(x.paneo - 0.5f) < 0.01f, "paneo al centro", x.paneo);
    ba_medidores_leer(m, &x);
    ba_medidores_liberar(m);

    /* Analizador: el seno */
    ba_analizador* a = ba_analizador_crear(1, SR, 13);
    check(a != NULL, "se crea el analizador", 0);
    const float* ch[1] = {l};
    for (int i = 0; i < N; i += 512) {
        ch[0] = l + i;
        ba_analizador_procesar(a, ch, 512);
    }
    enum { P = 256 };
    float frq[P], db[P];
    check(ba_analizador_leer(a, 0, 20.0f, 20000.0f, P, frq, db) == 0, "lee el espectro", 0);
    int k = 0;
    for (int i = 1; i < P; ++i)
        if (db[i] > db[k]) k = i;
    check(fabsf(frq[k] - 1000.0f) < 60.0f, "el pico está en 1 kHz", frq[k]);

    /* Ruido rosa con envolvente rosa: plano */
    static float rosa[N];
    ba_ruido* ru = ba_ruido_crear(SR, BA_RUIDO_ROSA, BA_RUIDO_LCG, 0.25f);
    check(ru != NULL, "se crea el ruido rosa", 0);
    ba_ruido_generar(ru, rosa, N);
    ba_ruido_liberar(ru);
    ba_analizador_configurar(a, 13, BA_VENTANA_HANN, BA_ENVOLVENTE_ROSA, 1.0f);
    for (int i = 0; i < N; i += 512) {
        ch[0] = rosa + i;
        ba_analizador_procesar(a, ch, 512);
    }
    ba_analizador_leer(a, 0, 100.0f, 10000.0f, P, frq, db);
    float mn = 1e9f, mx = -1e9f;
    /* Suavizado de a 16 puntos: el ruido tiene su variación propia. */
    for (int i = 0; i + 16 <= P; i += 16) {
        float s = 0;
        for (int j = 0; j < 16; ++j) s += db[i + j];
        s /= 16;
        if (s < mn) mn = s;
        if (s > mx) mx = s;
    }
    check(mx - mn < 6.0f, "ruido rosa plano de 100 Hz a 10 kHz (dB de diferencia)", mx - mn);
    ba_analizador_liberar(a);

    /* Latencia: lo que sale vuelve 480 muestras después */
    ba_latencia* lat = ba_latencia_crear(SR, 0.15f);
    check(lat != NULL, "se crea el detector de latencia", 0);
    ba_latencia_arrancar(lat);
    static float sal[N], ent[N], linea[N + 480];
    memset(linea, 0, sizeof linea);
    float ms = -2;
    int listo = 0;
    for (int i = 0; i < N && !listo; i += 256) {
        memcpy(ent, linea + i, 256 * sizeof(float));
        ba_latencia_procesar(lat, sal, ent, 256);
        memcpy(linea + i + 480, sal, 256 * sizeof(float));
        listo = ba_latencia_resultado(lat, &ms);
    }
    check(listo && fabsf(ms - 10.0f) < 0.5f, "latencia de 10 ms", ms);
    ba_latencia_liberar(lat);

    printf(fallas ? "\n%d fallas\n" : "\ntodo ok\n", fallas);
    return fallas ? 1 : 0;
}
