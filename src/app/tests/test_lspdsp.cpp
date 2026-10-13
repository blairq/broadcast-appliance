// test_lspdsp.cpp — ba-lspdsp (el DSP de LSP) mide lo que tiene que medir, cargada
// como la carga la app (audio/LspDsp). Señales conocidas, sin sonido:
// - Seno de 1 kHz a -23 dBFS en los dos canales: -23 LUFS (EBU Tech 3341), pico y
//   true peak -23 dBFS, RMS -26 dB, correlación 1, paneo 0,5.
// - El analizador encuentra el pico en 1 kHz.
// - Ruido rosa (generador de LSP) con envolvente rosa: plano de 100 Hz a 10 kHz.
// - Latencia: un retardo de 480 muestras a 48 kHz se mide como 10 ms.
// Sin la biblioteca (tools/deps/lspdsp.sh): SKIP.
#include "audio/LspDsp.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using std::fabs;

namespace {
int fallas = 0;
void check(bool ok, const char* que, double valor) {
    std::printf("%s: %s (%.2f)\n", ok ? "ok" : "FALLA", que, valor);
    if (!ok) ++fallas;
}
} // namespace

#define SR 48000
#define N (SR * 6) /* 6 s */

int main() {
    const ba::app::audio::lspdsp::Api* L = ba::app::audio::lspdsp::api();
    if (L == nullptr) {
        std::printf("SKIP: %s\n", ba::app::audio::lspdsp::error().c_str());
        return 77;
    }
    std::printf("cargada de %s\n", ba::app::audio::lspdsp::ruta().c_str());
    std::printf("ba-lspdsp %d (LSP %s)\n", L->version(), L->versionLsp());
    static float l[N], r[N];
    const float amp = powf(10.0f, -23.0f / 20.0f);
    for (int i = 0; i < N; ++i) l[i] = r[i] = amp * sinf(2.0f * 3.14159265f * 1000.0f * i / SR);

    /* Medidores */
    ba_medidores* m = L->medidoresCrear(2, SR);
    check(m != nullptr, "se crean los medidores", 0);
    for (int i = 0; i < N; i += 480) L->medidoresProcesar(m, l + i, r + i, 480);
    ba_medidores_lectura x;
    L->medidoresLeer(m, &x);
    check(fabsf(x.lufs_m + 23.0f) < 0.3f, "LUFS momentáneo -23", x.lufs_m);
    check(fabsf(x.lufs_s + 23.0f) < 0.3f, "LUFS corto -23", x.lufs_s);
    check(fabsf(x.lufs_i + 23.0f) < 0.3f, "LUFS integrado -23", x.lufs_i);
    check(fabsf(x.pico_db[0] + 23.0f) < 0.1f, "pico -23 dBFS", x.pico_db[0]);
    check(fabsf(x.true_peak_db[0] + 23.0f) < 0.3f, "true peak -23", x.true_peak_db[0]);
    check(fabsf(x.rms_db[0] + 26.0f) < 0.5f, "RMS -26 dB", x.rms_db[0]);
    check(x.correlacion > 0.99f, "correlación 1", x.correlacion);
    check(fabsf(x.paneo - 0.5f) < 0.01f, "paneo al centro", x.paneo);
    L->medidoresLeer(m, &x);
    L->medidoresLiberar(m);

    /* Analizador: el seno */
    ba_analizador* a = L->analizadorCrear(1, SR, 13);
    check(a != nullptr, "se crea el analizador", 0);
    const float* ch[1] = {l};
    for (int i = 0; i < N; i += 512) {
        ch[0] = l + i;
        L->analizadorProcesar(a, ch, 512);
    }
    enum { P = 256 };
    float frq[P], db[P];
    check(L->analizadorLeer(a, 0, 20.0f, 20000.0f, P, frq, db) == 0, "lee el espectro", 0);
    int k = 0;
    for (int i = 1; i < P; ++i)
        if (db[i] > db[k]) k = i;
    check(fabsf(frq[k] - 1000.0f) < 60.0f, "el pico está en 1 kHz", frq[k]);

    /* Ruido rosa con envolvente rosa: plano */
    static float rosa[N];
    ba_ruido* ru = L->ruidoCrear(SR, BA_RUIDO_ROSA, BA_RUIDO_LCG, 0.25f);
    check(ru != nullptr, "se crea el ruido rosa", 0);
    L->ruidoGenerar(ru, rosa, N);
    L->ruidoLiberar(ru);
    L->analizadorConfigurar(a, 13, BA_VENTANA_HANN, BA_ENVOLVENTE_ROSA, 1.0f);
    for (int i = 0; i < N; i += 512) {
        ch[0] = rosa + i;
        L->analizadorProcesar(a, ch, 512);
    }
    L->analizadorLeer(a, 0, 100.0f, 10000.0f, P, frq, db);
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
    L->analizadorLiberar(a);

    /* Latencia: lo que sale vuelve 480 muestras después */
    ba_latencia* lat = L->latenciaCrear(SR, 0.15f);
    check(lat != nullptr, "se crea el detector de latencia", 0);
    L->latenciaArrancar(lat);
    static float sal[N], ent[N], linea[N + 480];
    memset(linea, 0, sizeof linea);
    float ms = -2;
    int listo = 0;
    for (int i = 0; i < N && !listo; i += 256) {
        memcpy(ent, linea + i, 256 * sizeof(float));
        L->latenciaProcesar(lat, sal, ent, 256);
        memcpy(linea + i + 480, sal, 256 * sizeof(float));
        listo = L->latenciaResultado(lat, &ms);
    }
    check(listo && fabsf(ms - 10.0f) < 0.5f, "latencia de 10 ms", ms);
    L->latenciaLiberar(lat);

    /* Medición con barrido, en una sala simulada (no suena nada: la "sala" es una
       convolución). La IR: el directo a los 10 ms, una cola difusa que cae 60 dB en
       0,6 s, un modo de 80 Hz que dura el doble y un piso de ruido a -80 dB. */
    {
        constexpr int SRM = 16000, BLQ = 128;
        const float rt60 = 0.6f;
        const int largoIr = static_cast<int>(0.9f * SRM);
        static float ir[16000];
        unsigned semilla = 12345;
        auto azar = [&semilla]() {
            semilla = semilla * 1664525u + 1013904223u;
            return static_cast<float>(semilla >> 8) / 8388608.0f - 1.0f;
        };
        memset(ir, 0, sizeof ir);
        const int directo = SRM / 100; /* 10 ms */
        ir[directo] = 1.0f;
        for (int k = directo + SRM / 500; k < largoIr; ++k) {
            const float t = static_cast<float>(k - directo) / SRM;
            ir[k] += 0.08f * azar() * powf(10.0f, -3.0f * t / rt60);
            ir[k] += 0.02f * sinf(2.0f * 3.14159265f * 80.0f * t) * powf(10.0f, -3.0f * t / (2 * rt60));
        }
        ba_medicion* me = L->medicionCrear(SRM, 2.0f, 20.0f, 7000.0f, 0.5f, 0.0f);
        check(me != nullptr, "se crea la medición", 0);
        const size_t total = L->medicionDuracionMuestras(me);
        check(total > 2 * 2 * SRM, "la medición dura el barrido y su cola (s)", static_cast<float>(total) / SRM);
        std::vector<float> emitido(total + 4 * SRM, 0.0f);
        float ent[BLQ];
        L->medicionArrancar(me);
        size_t i = 0;
        for (; i + BLQ <= emitido.size() && !L->medicionTerminada(me); i += BLQ) {
            for (int j = 0; j < BLQ; ++j) {
                const long n = static_cast<long>(i) + j;
                double acc = 0;
                for (int k = directo; k < largoIr && k <= n; ++k) acc += ir[k] * emitido[n - k];
                ent[j] = static_cast<float>(acc) + 1e-4f * azar();
            }
            L->medicionProcesar(me, emitido.data() + i, ent, BLQ);
        }
        check(L->medicionTerminada(me), "la medición termina (s)", static_cast<float>(i) / SRM);
        ba_medicion_resultado r{};
        check(L->medicionAnalizar(me, BA_RT_T20, &r) == 0, "se analiza", 0);
        std::printf("    rt60 %.3f corr %.3f limite %.3f ruido %d directo %.2f ms pico %.1f snr %.1f\n", r.rt60_s,
                    r.correlacion, r.limite_integracion_s, r.ruido_suficiente, r.directo_ms, r.pico_db, r.snr_db);
        check(fabsf(r.directo_ms - 10.0f) < 0.5f, "el directo llega a los 10 ms", r.directo_ms);
        check(r.snr_db > 40.0f, "relación señal/ruido de la IR (dB)", r.snr_db);
        /* El RT de banda ancha no es 0,6: el modo de 80 Hz (que dura el doble) manda en
           la parte baja de la curva. Se compara con el Schroeder propio sin filtro. */
        const float ancha = 0;
        float t20Ancha = 0;
        L->medicionRtBandas(me, &ancha, 1, nullptr, &t20Ancha, nullptr);
        check(t20Ancha > 0.65f && fabsf(r.rt60_s - t20Ancha) < 0.1f, "RT60 de LSP (T20) = Schroeder de banda ancha",
              r.rt60_s - t20Ancha);
        const float centros[3] = {500.0f, 1000.0f, 2000.0f};
        float edt[3], t20[3], t30[3];
        check(L->medicionRtBandas(me, centros, 3, edt, t20, t30) == 0, "RT por bandas", 0);
        for (int b = 0; b < 3; ++b) {
            std::printf("    %g Hz: EDT %.3f T20 %.3f T30 %.3f\n", centros[b], edt[b], t20[b], t30[b]);
            check(fabsf(t20[b] - rt60) < 0.1f, "T20 por banda cerca de 0,6 s", t20[b]);
            check(fabsf(t30[b] - rt60) < 0.1f, "T30 por banda cerca de 0,6 s", t30[b]);
        }
        enum { NR = 64 };
        float fr[NR], dbr[NR];
        check(L->medicionRespuesta(me, 40.0f, 400.0f, NR, 400.0f, fr, dbr) == 0, "respuesta en frecuencia", 0);
        int km = 0;
        for (int k = 1; k < NR; ++k)
            if (dbr[k] > dbr[km]) km = k;
        check(fabsf(fr[km] - 80.0f) < 6.0f, "el modo de 80 Hz se ve en la respuesta (Hz)", fr[km]);
        static float irLeida[2000];
        const size_t nIr = L->medicionIr(me, irLeida, 2000, 2.0f);
        check(nIr == 2000 && fabsf(irLeida[32] - 1.0f) < 0.05f, "la IR normalizada arranca en el directo", irLeida[32]);
        L->medicionLiberar(me);
    }

    std::printf(fallas ? "\n%d fallas\n" : "\ntodo ok\n", fallas);
    return fallas ? 1 : 0;
}
