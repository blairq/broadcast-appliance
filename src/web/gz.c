/*
 * ba-web: gzip (RFC 1952, con libdeflate) y la negociación de Accept-Encoding según
 * RFC 9110 §12.5.3:
 *   - cada codificación con su peso q (0 a 1, hasta tres decimales); q=0 es «no»;
 *   - «x-gzip» es lo mismo que «gzip» (RFC 9110 §8.4.1.3);
 *   - «*» vale para toda codificación que no esté nombrada;
 *   - sin Accept-Encoding, cualquiera vale, pero se manda sin comprimir (como nginx:
 *     un cliente que no la manda suele ser uno que no entiende gzip).
 *   - mayúsculas y espacios no importan.
 */
#include "bw.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <libdeflate.h>

static struct libdeflate_compressor* g_comp[13];

bw_blob* gz_compress(const char* data, size_t len, int level) {
    if (level < 1) level = 1;
    if (level > 12) level = 12;
    if (!g_comp[level]) g_comp[level] = libdeflate_alloc_compressor(level);
    if (!g_comp[level]) return NULL;
    const size_t cota = libdeflate_gzip_compress_bound(g_comp[level], len);
    bw_blob* b = blob_new(cota);
    if (!b) return NULL;
    const size_t n = libdeflate_gzip_compress(g_comp[level], data, len, b->data, cota);
    if (n == 0) {
        blob_unref(b);
        return NULL;
    }
    b->len = n;
    return b;
}

/* Peso q de un parámetro «q=0.5» (RFC 9110 §12.4.2). 1000 = 1.000. -1 si está mal. */
static int peso(const char* p, const char* fin) {
    while (p < fin && (*p == ' ' || *p == '\t')) p++;
    if (p >= fin || (*p != 'q' && *p != 'Q')) return 1000; /* otro parámetro: no cambia el peso */
    p++;
    while (p < fin && (*p == ' ' || *p == '\t')) p++;
    if (p >= fin || *p != '=') return -1;
    p++;
    while (p < fin && (*p == ' ' || *p == '\t')) p++;
    if (p >= fin) return -1;
    int v = 0;
    if (*p == '0') {
        p++;
        if (p < fin && *p == '.') {
            p++;
            int dig = 0;
            int mul = 100;
            while (p < fin && isdigit((unsigned char)*p) && dig < 3) {
                v += (*p - '0') * mul;
                mul /= 10;
                p++;
                dig++;
            }
        }
    } else if (*p == '1') {
        v = 1000;
        p++;
        if (p < fin && *p == '.') {
            p++;
            while (p < fin && *p == '0') p++;
        }
    } else {
        return -1;
    }
    return v;
}

int accepts_gzip(const char* ae) {
    if (!ae) return 0;
    int q_gzip = -1, q_star = -1;
    const char* p = ae;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        const char* ini = p;
        while (*p && *p != ',' && *p != ';' && *p != ' ' && *p != '\t') p++;
        const size_t n = (size_t)(p - ini);
        int q = 1000;
        /* parámetros hasta la próxima coma */
        while (*p && *p != ',') {
            if (*p == ';') {
                const char* pi = ++p;
                while (*p && *p != ',' && *p != ';') p++;
                const int w = peso(pi, p);
                if (w < 0) q = 0; /* mal escrito: se toma como «no» (lo prudente) */
                else if (w != 1000 || (pi < p && (*pi == 'q' || *pi == 'Q'))) q = w;
            } else {
                p++;
            }
        }
        if ((n == 4 && !strncasecmp(ini, "gzip", 4)) || (n == 6 && !strncasecmp(ini, "x-gzip", 6))) {
            if (q > q_gzip) q_gzip = q;
        } else if (n == 1 && *ini == '*') {
            q_star = q;
        }
    }
    if (q_gzip >= 0) return q_gzip > 0;
    return q_star > 0;
}

int type_compressible(const bw_conf* c, const char* ct) {
    if (!c->gzip || !ct || !*ct) return 0;
    size_t n = 0;
    while (ct[n] && ct[n] != ';' && ct[n] != ' ') n++;
    for (int i = 0; i < c->ntypes; i++)
        if (strlen(c->types[i]) == n && !strncasecmp(c->types[i], ct, n)) return 1;
    return 0;
}
