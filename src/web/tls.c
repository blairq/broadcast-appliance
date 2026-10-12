/*
 * ba-web: HTTPS (para la red local) con mbedTLS, sin bloquear.
 *
 * El certificado y la clave llegan por la entrada estándar del master (el gateway los saca
 * de Credenciales/, cifrados, y los pasa por un pipe: nunca tocan el disco en claro). Cada
 * worker arma su propia configuración con su propio generador de números al azar: si lo
 * heredaran del master con fork(), todos sacarían LOS MISMOS números (y en TLS eso es
 * romper el cifrado). TLS 1.2 (ECDHE + AES-GCM/ChaCha20), como el terminador del gateway:
 * lo entiende cualquier navegador.
 *
 * Todo no bloqueante: mbedTLS pide «quiero leer» o «quiero escribir» y la conexión espera
 * ese evento. Lo que mbedTLS ya descifró y tiene guardado no lo avisa el sistema (el socket
 * está vacío): por eso tls_pendiente(), y http.c lo mira después de leer.
 */
#include "bw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifndef _WIN32
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef MSG_NOSIGNAL
#define TLS_SEND_FLAGS MSG_NOSIGNAL
#else
#define TLS_SEND_FLAGS 0
#endif

struct bw_tls {
    mbedtls_ssl_context ssl;
    bw_sock fd;
    int quiere_escribir; /* el último «espera» fue por escribir */
};

static mbedtls_entropy_context g_entropia;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt g_crt;
static mbedtls_pk_context g_clave;
static mbedtls_ssl_config g_conf;
static const char* g_alpn[2] = {"http/1.1", NULL};
static int g_listo = 0;

/* ---- El PEM que llegó por la entrada estándar ---- */
static char* g_pem = NULL;
static size_t g_npem = 0;

int tls_leer_entrada(void) {
    bw_buf b = {0};
    char tmp[8192];
    for (;;) {
#ifdef _WIN32
        DWORD n = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), tmp, sizeof tmp, &n, NULL) || n == 0) break;
#else
        const ssize_t n = read(0, tmp, sizeof tmp);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
#endif
        if (b.len + (size_t)n > (1u << 16)) break; /* más de 64 KiB no es un certificado */
        buf_add(&b, tmp, (size_t)n);
    }
    g_pem = b.p;
    g_npem = b.len;
    return g_npem > 0 ? 0 : -1;
}

void tls_pem(const char** pem, size_t* n) {
    *pem = g_pem;
    *n = g_npem;
}

void tls_set_pem(char* pem, size_t n) {
    g_pem = pem;
    g_npem = n;
}

/* Separa el PEM en los certificados y la clave (el bloque «… PRIVATE KEY»). */
static int separar(const char* pem, size_t n, bw_buf* crt, bw_buf* clave) {
    const char* p = pem;
    const char* fin = pem + n;
    while (p < fin) {
        const char* ini = strstr(p, "-----BEGIN ");
        if (!ini || ini >= fin) break;
        const char* cierre = strstr(ini, "-----END ");
        if (!cierre) break;
        const char* eol = strstr(cierre + 9, "-----");
        if (!eol) break;
        eol += 5;
        const size_t largo = (size_t)(eol - ini);
        int es_clave = 0; /* «… PRIVATE KEY» en el bloque (sin memmem: no está en Windows) */
        for (size_t i = 0; i + 11 <= largo && !es_clave; i++) es_clave = !memcmp(ini + i, "PRIVATE KEY", 11);
        bw_buf* destino = es_clave ? clave : crt;
        buf_add(destino, ini, largo);
        buf_adds(destino, "\n");
        p = eol;
    }
    return crt->len && clave->len ? 0 : -1;
}

int tls_iniciar_worker(char* err, size_t errlen) {
    if (!g_pem || !g_npem) {
        snprintf(err, errlen, "https: no llegó el certificado (por la entrada estándar)");
        return -1;
    }
    mbedtls_entropy_init(&g_entropia);
    mbedtls_ctr_drbg_init(&g_drbg);
    mbedtls_x509_crt_init(&g_crt);
    mbedtls_pk_init(&g_clave);
    mbedtls_ssl_config_init(&g_conf);
    /* personalización con el pid y la hora: cada worker, su propia secuencia */
    char pers[64];
#ifdef _WIN32
    snprintf(pers, sizeof pers, "ba-web-%lu-%lld", (unsigned long)GetCurrentProcessId(), (long long)now_ms());
#else
    snprintf(pers, sizeof pers, "ba-web-%d-%lld", (int)getpid(), (long long)now_ms());
#endif
    if (mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropia, (const unsigned char*)pers, strlen(pers))) {
        snprintf(err, errlen, "https: no se pudo iniciar el generador al azar");
        return -1;
    }
    bw_buf crt = {0}, clave = {0};
    if (separar(g_pem, g_npem, &crt, &clave)) {
        snprintf(err, errlen, "https: el PEM recibido no tiene certificado y clave");
        buf_free(&crt);
        buf_free(&clave);
        return -1;
    }
    int k = mbedtls_x509_crt_parse(&g_crt, (const unsigned char*)crt.p, crt.len + 1);
    if (!k)
        k = mbedtls_pk_parse_key(&g_clave, (const unsigned char*)clave.p, clave.len + 1, NULL, 0,
                                 mbedtls_ctr_drbg_random, &g_drbg);
    mbedtls_platform_zeroize(clave.p, clave.cap);
    buf_free(&crt);
    buf_free(&clave);
    if (k) {
        snprintf(err, errlen, "https: certificado o clave inválidos (%d)", k);
        return -1;
    }
    if (mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) ||
        mbedtls_ssl_conf_own_cert(&g_conf, &g_crt, &g_clave)) {
        snprintf(err, errlen, "https: no se pudo armar la configuración TLS");
        return -1;
    }
    mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_drbg);
    mbedtls_ssl_conf_min_tls_version(&g_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(&g_conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_alpn_protocols(&g_conf, g_alpn);
    mbedtls_ssl_conf_renegotiation(&g_conf, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
    /* la clave del certificado ya está adentro de g_clave: el PEM del worker se borra */
    mbedtls_platform_zeroize(g_pem, g_npem);
    g_listo = 1;
    return 0;
}

int tls_activo(void) { return g_listo; }

/* ---- E/S del socket para mbedTLS (no bloqueante) ---- */
static int bio_send(void* ctx, const unsigned char* buf, size_t len) {
    bw_tls* t = (bw_tls*)ctx;
    const int r = (int)send(t->fd, (const char*)buf, (int)len, TLS_SEND_FLAGS);
    if (r >= 0) return r;
    return sock_would_block() ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}
static int bio_recv(void* ctx, unsigned char* buf, size_t len) {
    bw_tls* t = (bw_tls*)ctx;
    const int r = (int)recv(t->fd, (char*)buf, (int)len, 0);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_CONN_EOF;
    return sock_would_block() ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

bw_tls* tls_nuevo(bw_sock fd) {
    if (!g_listo) return NULL;
    bw_tls* t = (bw_tls*)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fd = fd;
    mbedtls_ssl_init(&t->ssl);
    if (mbedtls_ssl_setup(&t->ssl, &g_conf)) {
        mbedtls_ssl_free(&t->ssl);
        free(t);
        return NULL;
    }
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
    return t;
}

void tls_liberar(bw_tls* t) {
    if (!t) return;
    mbedtls_ssl_close_notify(&t->ssl); /* de cortesía: si no entra, no se espera */
    mbedtls_ssl_free(&t->ssl);
    free(t);
}

int tls_quiere_escribir(const bw_tls* t) { return t->quiere_escribir; }

/* 1 listo, 0 hay que esperar (ver tls_quiere_escribir), -1 falló. */
int tls_handshake(bw_tls* t) {
    const int r = mbedtls_ssl_handshake(&t->ssl);
    if (r == 0) return 1;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        t->quiere_escribir = r == MBEDTLS_ERR_SSL_WANT_WRITE;
        return 0;
    }
    return -1;
}

/* >0 bytes, 0 hay que esperar, -1 error, -2 el otro cerró. */
int tls_recv(bw_tls* t, char* p, int n) {
    const int r = mbedtls_ssl_read(&t->ssl, (unsigned char*)p, (size_t)n);
    if (r > 0) return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        t->quiere_escribir = r == MBEDTLS_ERR_SSL_WANT_WRITE;
        return 0;
    }
    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF) return -2;
    return -1;
}

/* >0 bytes mandados, 0 hay que esperar, -1 error. Si devuelve 0, la próxima vez se llama
   con los mismos datos (mbedTLS lo exige). */
int tls_send(bw_tls* t, const char* p, int n) {
    if (n > 16384) n = 16384; /* un registro TLS por vez */
    const int r = mbedtls_ssl_write(&t->ssl, (const unsigned char*)p, (size_t)n);
    if (r > 0) return r;
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        t->quiere_escribir = r == MBEDTLS_ERR_SSL_WANT_WRITE;
        return 0;
    }
    return -1;
}

size_t tls_pendiente(const bw_tls* t) { return mbedtls_ssl_get_bytes_avail(&t->ssl); }
