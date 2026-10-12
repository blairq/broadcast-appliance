/*
 * ba-web: el worker. Un bucle de eventos, un hilo, todo no bloqueante.
 *
 * Una conexión pasa por estos estados:
 *   C_READ    esperando (o leyendo) la cabecera de un pedido
 *   C_WRITE   mandando una respuesta (cabecera + memoria/archivo, o lo que llega del gateway)
 *   C_PROXY   esperando la respuesta del gateway (proxy con buffer)
 *   C_CACHE   esperando que llegue la respuesta del microcaché
 *   C_TUNNEL  túnel crudo con el gateway (WebSocket, POST, SSE…)
 * Al terminar una respuesta, si se puede seguir (keep-alive), vuelve a C_READ y atiende
 * lo que ya haya llegado (pipelining); si no, se cierra.
 *
 * Los objetos que se cierran no se liberan en el momento: van a una lista y se liberan
 * al final de la vuelta del bucle (puede quedar un evento suyo en la misma tanda).
 */
#include "bw.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef MSG_NOSIGNAL
#define BW_SEND_FLAGS MSG_NOSIGNAL
#else
#define BW_SEND_FLAGS 0
#endif

volatile int g_worker_quit = 0;

static const bw_conf* C;
static int64_t g_now;
static char g_date[32];
static int64_t g_date_s = -1;
static int g_nconns = 0;

/* ------------------------------------------------------------------------- */
/* Tipos                                                                     */
/* ------------------------------------------------------------------------- */

enum { O_LISTEN = 1, O_CONN, O_UP };
enum { C_READ = 0, C_WRITE, C_PROXY, C_CACHE, C_TUNNEL };
enum { U_BUF = 0, U_STREAM, U_TUNNEL, U_CACHE };

#define HEAD_MAX (64 * 1024)
#define STREAM_HIGH (256 * 1024)

typedef struct lst {
    int tipo;
    bw_sock fd;
    int slot;
    int armado;
    const bw_listen* lc;
} lst;

typedef struct req {
    char metodo[16];
    int v11;
    char* target;      /* tal como vino (en head) */
    char uri[4096];    /* decodificada y normalizada (para elegir y mapear) */
    char* query;       /* sin el '?', o NULL */
    const char* host;
    const char* ae;
    const char* inm;
    const char* ims;
    const char* range;
    const char* ifrange;
    const char* upgrade;
    int conn_close, conn_keep;
    int64_t clen;
    int chunked;
    size_t ini;      /* dónde empieza el pedido en c->in (después de líneas vacías) */
    size_t head_len; /* hasta el fin de la cabecera, contando desde ini */
    int es_head, es_get;
} req;

struct centry;
struct up;

typedef struct conn {
    int tipo;
    bw_sock fd;
    int slot;
    int ev_rd, ev_wr;
    int muerto;
    const bw_listen* lc;
    char ip[64];
    char proxy_line[160];
    int proxy_visto;
    int state;
    bw_buf in;
    bw_buf head;
    req r;
    int nreq;
    int keepalive;
    bw_buf out;
    size_t out_off;
    bw_blob* body;
    size_t body_off, body_end;
    bw_file file;
    int tiene_file;
    int64_t file_off, file_end;
    int resp_lista;
    int cerrar_despues;
    int64_t deadline;
    struct up* up;
    struct centry* espera;
    struct conn* espera_sig;
    struct conn *prev, *next;
    int status;
    int64_t enviados;
} conn;

typedef struct up {
    int tipo;
    bw_sock fd;
    int slot;
    int ev_rd, ev_wr;
    int muerto;
    conn* c;
    struct centry* ce;
    int modo;
    int conectado;
    bw_buf out;
    size_t out_off;
    bw_buf in;
    int head_ok;
    size_t head_len;
    int status;
    int64_t clen;
    int chunked;
    int sin_cuerpo;
    int64_t recibidos; /* bytes de cuerpo (en modo stream, ya pasados al cliente) */
    int cliente_eof;
    int up_eof;
    int64_t deadline;
    struct up *prev, *next;
} up;

typedef struct centry {
    char key[2048];
    int status;
    bw_buf hdrs; /* las del gateway, sin las de la conexión ni el largo */
    bw_blob* body;
    bw_blob* gz;
    char etag[48];
    int comprimible;
    int64_t traida;
    int valido_ms;
    int tiene;
    up* fetch;
    conn* esperando;
    struct centry* sig;
} centry;

static conn* g_conns = NULL;
static up* g_ups = NULL;
static void* g_cementerio[4096];
static int g_ncem = 0;
static lst* g_lst[BW_MAX_LISTEN];
static int g_nlst = 0;
static centry* g_cache = NULL;
static int g_ncache = 0;

/* ids de mirada vistos desde el último pedido al gateway (ba_vistos) */
#define VISTOS_MAX 4096
static uint32_t g_vistos[VISTOS_MAX * 2];
static int g_nvistos = 0;
static char g_clave[128];

static void conn_close(conn* c);
static void up_close(up* u);
static void conn_process(conn* c);
static void conn_write(conn* c);
static void finish_response(conn* c);

/* ------------------------------------------------------------------------- */
/* Utilidades                                                                */
/* ------------------------------------------------------------------------- */

static void enterrar(void* p) {
    if (g_ncem < (int)(sizeof g_cementerio / sizeof g_cementerio[0])) g_cementerio[g_ncem++] = p;
    /* si se llena (no debería: se vacía en cada vuelta) se pierde la memoria, no se rompe */
}

static void set_io(int tipo, void* o, bw_sock fd, int* slot, int* ev_rd, int* ev_wr, int rd, int wr) {
    (void)tipo;
    if (*ev_rd == rd && *ev_wr == wr) return;
    ev_mod(fd, o, rd, wr, slot);
    *ev_rd = rd;
    *ev_wr = wr;
}
#define CONN_IO(c, rd, wr) set_io(O_CONN, (c), (c)->fd, &(c)->slot, &(c)->ev_rd, &(c)->ev_wr, (rd), (wr))
#define UP_IO(u, rd, wr) set_io(O_UP, (u), (u)->fd, &(u)->slot, &(u)->ev_rd, &(u)->ev_wr, (rd), (wr))

static const char* fecha(void) {
    const int64_t s = wall_s();
    if (s != g_date_s) {
        http_date(s, g_date);
        g_date_s = s;
    }
    return g_date;
}

static const char* razon(int code) {
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 416: return "Range Not Satisfiable";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default: return "OK";
    }
}

static uint32_t fnv32(const char* s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}
static uint64_t fnv64(const char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 1099511628211ULL;
    return h;
}

/* ¿La lista de tokens (separados por coma) tiene `tok`? Sin mayúsculas. */
static int tiene_token(const char* lista, const char* tok) {
    if (!lista) return 0;
    const size_t n = strlen(tok);
    const char* p = lista;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char* ini = p;
        while (*p && *p != ',') p++;
        const char* fin = p;
        while (fin > ini && (fin[-1] == ' ' || fin[-1] == '\t')) fin--;
        if ((size_t)(fin - ini) == n && !strncasecmp(ini, tok, n)) return 1;
    }
    return 0;
}

static int es_loopback(const char* ip) {
    return !strncmp(ip, "127.", 4) || !strcmp(ip, "::1") || !strncmp(ip, "::ffff:127.", 11);
}

/* ------------------------------------------------------------------------- */
/* Tipos de contenido                                                        */
/* ------------------------------------------------------------------------- */

static const struct {
    const char* ext;
    const char* tipo;
} kMime[] = {
    {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
    {"css", "text/css; charset=utf-8"}, {"js", "application/javascript; charset=utf-8"},
    {"mjs", "application/javascript; charset=utf-8"}, {"json", "application/json; charset=utf-8"},
    {"map", "application/json; charset=utf-8"}, {"txt", "text/plain; charset=utf-8"},
    {"md", "text/markdown; charset=utf-8"}, {"csv", "text/csv; charset=utf-8"},
    {"xml", "application/xml; charset=utf-8"}, {"svg", "image/svg+xml"}, {"png", "image/png"},
    {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"webp", "image/webp"},
    {"avif", "image/avif"}, {"ico", "image/x-icon"}, {"woff", "font/woff"}, {"woff2", "font/woff2"},
    {"ttf", "font/ttf"}, {"otf", "font/otf"}, {"mp4", "video/mp4"}, {"m4v", "video/mp4"},
    {"webm", "video/webm"}, {"mov", "video/quicktime"}, {"m4a", "audio/mp4"}, {"mp3", "audio/mpeg"},
    {"ogg", "audio/ogg"}, {"oga", "audio/ogg"}, {"opus", "audio/ogg"}, {"wav", "audio/wav"},
    {"pdf", "application/pdf"}, {"wasm", "application/wasm"}, {"webmanifest", "application/manifest+json"},
    {"vtt", "text/vtt; charset=utf-8"}, {"srt", "text/plain; charset=utf-8"}, {"zip", "application/zip"},
    {NULL, NULL}};

static const char* mime_de(const char* path) {
    const char* punto = strrchr(path, '.');
    const char* barra = strrchr(path, '/');
    if (!punto || (barra && punto < barra)) return "application/octet-stream";
    for (int i = 0; kMime[i].ext; i++)
        if (!strcasecmp(punto + 1, kMime[i].ext)) return kMime[i].tipo;
    return "application/octet-stream";
}

/* ------------------------------------------------------------------------- */
/* Caché de archivos (como open_file_cache): revalida con stat cada 1 s      */
/* ------------------------------------------------------------------------- */

typedef struct fentry {
    char* path;
    uint32_t h;
    int existe, es_dir, permitido;
    int64_t size, mtime;
    char etag[48], etag_gz[52], lastmod[32];
    const char* tipo;
    bw_blob* data;
    bw_blob* gz;
    int64_t revisado;
    struct fentry *hsig, *lprev, *lsig;
} fentry;

#define FBUCKETS 4096
static fentry* g_fh[FBUCKETS];
static fentry *g_lru = NULL, *g_lru_fin = NULL;
static size_t g_fbytes = 0;
static int g_nf = 0;

static void lru_sacar(fentry* e) {
    if (e->lprev) e->lprev->lsig = e->lsig;
    else g_lru = e->lsig;
    if (e->lsig) e->lsig->lprev = e->lprev;
    else g_lru_fin = e->lprev;
    e->lprev = e->lsig = NULL;
}
static void lru_poner(fentry* e) {
    e->lsig = g_lru;
    e->lprev = NULL;
    if (g_lru) g_lru->lprev = e;
    g_lru = e;
    if (!g_lru_fin) g_lru_fin = e;
}
static void fentry_soltar_datos(fentry* e) {
    if (e->data) g_fbytes -= e->data->len;
    if (e->gz) g_fbytes -= e->gz->len;
    blob_unref(e->data);
    blob_unref(e->gz);
    e->data = e->gz = NULL;
}
static void fentry_borrar(fentry* e) {
    fentry** pp = &g_fh[e->h % FBUCKETS];
    while (*pp && *pp != e) pp = &(*pp)->hsig;
    if (*pp) *pp = e->hsig;
    lru_sacar(e);
    fentry_soltar_datos(e);
    free(e->path);
    free(e);
    g_nf--;
}
static void fcache_recortar(void) {
    while ((g_fbytes > C->file_cache_total || g_nf > 20000) && g_lru_fin) fentry_borrar(g_lru_fin);
}

/* La raíz (real) contra la que se compara, para no salirse por un enlace. */
static int dentro_de(const char* path, const char* base) {
    char rp[4096], rb[4096];
    if (!fs_realpath(path, rp, sizeof rp) || !fs_realpath(base, rb, sizeof rb)) return 0;
    const size_t n = strlen(rb);
    if (strncmp(rp, rb, n) != 0) return 0;
    return rp[n] == 0 || rp[n] == '/' || (n > 0 && rb[n - 1] == '/');
}

static fentry* fcache_get(const char* path, const char* base) {
    const uint32_t h = fnv32(path, strlen(path));
    fentry* e = g_fh[h % FBUCKETS];
    while (e && (e->h != h || strcmp(e->path, path))) e = e->hsig;
    if (e && g_now - e->revisado < 1000) {
        lru_sacar(e);
        lru_poner(e);
        return e;
    }
    bw_stat st;
    fs_stat(path, &st);
    if (!e) {
        e = (fentry*)calloc(1, sizeof *e);
        if (!e) return NULL;
        e->path = strdup(path);
        e->h = h;
        e->hsig = g_fh[h % FBUCKETS];
        g_fh[h % FBUCKETS] = e;
        g_nf++;
        lru_poner(e);
        e->mtime = -1;
    } else {
        lru_sacar(e);
        lru_poner(e);
    }
    e->revisado = g_now;
    if (st.exists != e->existe || st.is_dir != e->es_dir || st.size != e->size || st.mtime != e->mtime) {
        fentry_soltar_datos(e);
        e->existe = st.exists;
        e->es_dir = st.is_dir;
        e->size = st.size;
        e->mtime = st.mtime;
        e->permitido = st.exists ? dentro_de(path, base) : 0;
        snprintf(e->etag, sizeof e->etag, "\"%llx-%llx\"", (unsigned long long)st.mtime,
                 (unsigned long long)st.size);
        snprintf(e->etag_gz, sizeof e->etag_gz, "\"%llx-%llx-gz\"", (unsigned long long)st.mtime,
                 (unsigned long long)st.size);
        http_date(st.mtime, e->lastmod);
        e->tipo = mime_de(path);
    }
    if (e->existe && !e->es_dir && !e->data && (size_t)e->size <= C->file_cache_max) {
        e->data = fs_read_all(path, C->file_cache_max);
        if (e->data) {
            g_fbytes += e->data->len;
            if ((int64_t)e->data->len != e->size) {
                /* cambió mientras se leía: vale lo leído */
                e->size = (int64_t)e->data->len;
                snprintf(e->etag, sizeof e->etag, "\"%llx-%llx\"", (unsigned long long)e->mtime,
                         (unsigned long long)e->size);
                snprintf(e->etag_gz, sizeof e->etag_gz, "\"%llx-%llx-gz\"", (unsigned long long)e->mtime,
                         (unsigned long long)e->size);
            }
        }
    }
    fcache_recortar();
    return e;
}

/* alias_file: el directorio que dice el archivo (se relee si cambió, como mucho cada 1 s). */
static struct {
    char dir[1024];
    int64_t revisado, mtime, size;
} g_alias[BW_MAX_LOC];

static const char* alias_de_archivo(const bw_location* l) {
    const int i = (int)(l - C->loc);
    if (g_now - g_alias[i].revisado < 1000 && g_alias[i].revisado) return g_alias[i].dir;
    g_alias[i].revisado = g_now;
    bw_stat st;
    fs_stat(l->alias_file, &st);
    if (!st.exists) {
        g_alias[i].dir[0] = 0;
        g_alias[i].mtime = -1;
        return g_alias[i].dir;
    }
    if (st.mtime == g_alias[i].mtime && st.size == g_alias[i].size) return g_alias[i].dir;
    g_alias[i].mtime = st.mtime;
    g_alias[i].size = st.size;
    bw_blob* b = fs_read_all(l->alias_file, 4096);
    g_alias[i].dir[0] = 0;
    if (b) {
        size_t n = b->len;
        while (n && (b->data[n - 1] == '\n' || b->data[n - 1] == '\r' || b->data[n - 1] == ' ')) n--;
        if (n < sizeof g_alias[i].dir) {
            memcpy(g_alias[i].dir, b->data, n);
            g_alias[i].dir[n] = 0;
        }
        blob_unref(b);
    }
    return g_alias[i].dir;
}

/* ------------------------------------------------------------------------- */
/* Respuestas                                                                */
/* ------------------------------------------------------------------------- */

static void cabecera_conexion(conn* c, bw_buf* b) {
    if (c->cerrar_despues || !c->keepalive || g_worker_quit) {
        c->cerrar_despues = 1;
        buf_adds(b, "Connection: close\r\n");
    } else if (!c->r.v11) {
        buf_adds(b, "Connection: keep-alive\r\n");
    }
}

static void respuesta_simple(conn* c, int code, const char* extra, const char* texto, int cerrar) {
    char cuerpo[1024];
    if (!texto) snprintf(cuerpo, sizeof cuerpo, "%d %s\n", code, razon(code));
    else snprintf(cuerpo, sizeof cuerpo, "%s", texto);
    if (cerrar) c->cerrar_despues = 1;
    c->out.len = 0;
    c->out_off = 0;
    buf_printf(&c->out, "HTTP/1.1 %d %s\r\nServer: ba-web\r\nDate: %s\r\n", code, razon(code), fecha());
    if (code != 204 && code != 304)
        buf_printf(&c->out, "Content-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\n", strlen(cuerpo));
    if (extra) buf_adds(&c->out, extra);
    cabecera_conexion(c, &c->out);
    buf_adds(&c->out, "\r\n");
    if (!c->r.es_head && code != 204 && code != 304) buf_adds(&c->out, cuerpo);
    c->status = code;
    c->resp_lista = 1;
    c->state = C_WRITE;
    conn_write(c);
}

/* ¿Algún entity-tag de la lista coincide (comparación débil, RFC 9110 §8.8.3.2)? */
static int etag_coincide(const char* lista, const char* etag) {
    if (!lista) return 0;
    const char* p = lista;
    const char* et = etag;
    if (!strncmp(et, "W/", 2)) et += 2;
    const size_t n = strlen(et);
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        if (*p == '*') return 1;
        if (!strncmp(p, "W/", 2)) p += 2;
        const char* ini = p;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') p++;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ',') p++;
        }
        if ((size_t)(p - ini) == n && !strncmp(ini, et, n)) return 1;
        while (*p && *p != ',') p++;
    }
    return 0;
}

/* Range: un solo rango «bytes=a-b», «a-» o «-n». 1 si vale (a,b), 0 si se ignora,
   -1 si no se puede satisfacer (416). Varios rangos se ignoran (se manda todo). */
static int rango(const char* r, int64_t size, int64_t* a, int64_t* b) {
    while (*r == ' ') r++;
    if (strncasecmp(r, "bytes", 5)) return 0;
    r += 5;
    while (*r == ' ') r++;
    if (*r++ != '=') return 0;
    while (*r == ' ') r++;
    if (strchr(r, ',')) return 0;
    int64_t x = -1, y = -1;
    if (isdigit((unsigned char)*r)) {
        x = 0;
        while (isdigit((unsigned char)*r)) {
            if (x > (INT64_MAX - 9) / 10) return 0;
            x = x * 10 + (*r++ - '0');
        }
    }
    while (*r == ' ') r++;
    if (*r++ != '-') return 0;
    while (*r == ' ') r++;
    if (isdigit((unsigned char)*r)) {
        y = 0;
        while (isdigit((unsigned char)*r)) {
            if (y > (INT64_MAX - 9) / 10) return 0;
            y = y * 10 + (*r++ - '0');
        }
    }
    while (*r == ' ') r++;
    if (*r) return 0;
    if (x < 0 && y < 0) return 0;
    if (x < 0) { /* sufijo: los últimos y bytes */
        if (y == 0 || size == 0) return -1;
        *a = y >= size ? 0 : size - y;
        *b = size - 1;
        return 1;
    }
    if (y >= 0 && y < x) return 0;
    if (x >= size) return -1;
    *a = x;
    *b = (y < 0 || y >= size) ? size - 1 : y;
    return 1;
}

static void agregar_headers_loc(bw_buf* b, const bw_location* l) {
    if (!l) return;
    for (int i = 0; i < l->nhdr; i++) {
        buf_adds(b, l->hdr[i]);
        buf_adds(b, "\r\n");
    }
}

/* Manda un archivo del caché (o del disco, si es grande). */
static void servir_archivo(conn* c, const bw_location* l, fentry* e) {
    req* r = &c->r;
    const int comprimible = type_compressible(C, e->tipo);
    int64_t a = 0, b = e->size - 1;
    int parcial = 0;
    if (r->range && e->size > 0) {
        int vale = 1;
        if (r->ifrange) {
            /* If-Range: un ETag fuerte igual al nuestro, o la fecha exacta (RFC 9110 §13.1.5) */
            const char* v = r->ifrange;
            while (*v == ' ') v++;
            if (*v == '"') vale = !strcmp(v, e->etag);
            else if (!strncmp(v, "W/", 2)) vale = 0;
            else vale = http_date_parse(v) == e->mtime;
        }
        if (vale) {
            const int k = rango(r->range, e->size, &a, &b);
            if (k < 0) {
                char extra[128];
                snprintf(extra, sizeof extra, "Content-Range: bytes */%lld\r\n", (long long)e->size);
                respuesta_simple(c, 416, extra, NULL, 0);
                return;
            }
            parcial = k == 1;
        }
    } else if (r->range && e->size == 0) {
        respuesta_simple(c, 416, "Content-Range: bytes */0\r\n", NULL, 0);
        return;
    }
    int usar_gz = 0;
    if (!parcial && comprimible && e->data && e->size >= C->gzip_min && accepts_gzip(r->ae)) {
        if (!e->gz) {
            e->gz = gz_compress(e->data->data, e->data->len, C->gzip_level);
            if (e->gz) g_fbytes += e->gz->len;
        }
        usar_gz = e->gz && e->gz->len < e->data->len;
    }
    const char* etag = usar_gz ? e->etag_gz : e->etag;
    /* condicionales (RFC 9110 §13.2.2): If-None-Match manda sobre If-Modified-Since */
    int no_cambio = 0;
    if (r->inm) no_cambio = etag_coincide(r->inm, etag);
    else if (r->ims) {
        const int64_t t = http_date_parse(r->ims);
        no_cambio = t >= 0 && e->mtime <= t;
    }
    bw_buf* o = &c->out;
    o->len = 0;
    c->out_off = 0;
    if (no_cambio) {
        buf_printf(o, "HTTP/1.1 304 Not Modified\r\nServer: ba-web\r\nDate: %s\r\nETag: %s\r\nLast-Modified: %s\r\n",
                   fecha(), etag, e->lastmod);
        if (comprimible) buf_adds(o, "Vary: Accept-Encoding\r\n");
        agregar_headers_loc(o, l);
        cabecera_conexion(c, o);
        buf_adds(o, "\r\n");
        c->status = 304;
        c->resp_lista = 1;
        c->state = C_WRITE;
        conn_write(c);
        return;
    }
    const int64_t largo = usar_gz ? (int64_t)e->gz->len : (parcial ? b - a + 1 : e->size);
    c->status = parcial ? 206 : 200;
    buf_printf(o, "HTTP/1.1 %d %s\r\nServer: ba-web\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
                  "Last-Modified: %s\r\nETag: %s\r\n",
               c->status, razon(c->status), fecha(), e->tipo, (long long)largo, e->lastmod, etag);
    if (usar_gz) buf_adds(o, "Content-Encoding: gzip\r\n");
    else buf_adds(o, "Accept-Ranges: bytes\r\n");
    if (comprimible) buf_adds(o, "Vary: Accept-Encoding\r\n");
    if (parcial) buf_printf(o, "Content-Range: bytes %lld-%lld/%lld\r\n", (long long)a, (long long)b, (long long)e->size);
    agregar_headers_loc(o, l);
    cabecera_conexion(c, o);
    buf_adds(o, "\r\n");
    if (!r->es_head && largo > 0) {
        if (usar_gz) {
            c->body = blob_ref(e->gz);
            c->body_off = 0;
            c->body_end = e->gz->len;
        } else if (e->data) {
            c->body = blob_ref(e->data);
            c->body_off = (size_t)a;
            c->body_end = (size_t)(a + largo);
        } else if (file_open(e->path, &c->file) == 0) {
            c->tiene_file = 1;
            c->file_off = a;
            c->file_end = a + largo;
        } else {
            respuesta_simple(c, 500, NULL, NULL, 1);
            return;
        }
    }
    c->resp_lista = 1;
    c->state = C_WRITE;
    conn_write(c);
}

/* 1 si respondió, 0 si no está (y se puede probar el upstream). */
static int servir_estatico(conn* c, const bw_location* l) {
    req* r = &c->r;
    const char* base = NULL;
    const char* rel = r->uri;
    if (l->root[0]) {
        base = l->root;
    } else if (l->alias[0]) {
        base = l->alias;
        rel = r->uri + strlen(l->path);
    } else if (l->alias_file[0]) {
        base = alias_de_archivo(l);
        rel = r->uri + strlen(l->path);
        if (!base[0]) return 0;
    } else {
        return 0;
    }
    while (*rel == '/') rel++;
    char path[4096];
    const size_t nb = strlen(base);
    const int dir_uri = r->uri[strlen(r->uri) - 1] == '/';
    snprintf(path, sizeof path, "%s%s%s", base, (nb && base[nb - 1] == '/') ? "" : "/", rel);
    if (dir_uri || !*rel) {
        if (!l->index[0]) return 0;
        const size_t n = strlen(path);
        snprintf(path + n, sizeof path - n, "%s%s", (n && path[n - 1] == '/') ? "" : "/", l->index);
    }
    fentry* e = fcache_get(path, base);
    if (!e || !e->existe || !e->permitido) return 0;
    if (e->es_dir) {
        if (!dir_uri) {
            /* como nginx: el directorio sin la barra se redirige con la barra */
            char loc[4400];
            snprintf(loc, sizeof loc, "Location: %s/%s%s\r\n", r->uri, r->query ? "?" : "", r->query ? r->query : "");
            if (strpbrk(loc + 10, "\r\n") != loc + strlen(loc) - 2) return 0;
            respuesta_simple(c, 301, loc, NULL, 0);
            return 1;
        }
        return 0;
    }
    servir_archivo(c, l, e);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Proxy al gateway                                                          */
/* ------------------------------------------------------------------------- */

static up* up_nuevo(void) {
    bw_sock s = sock_connect(C->upstream_host, C->upstream_port);
    if (s == BW_BAD) return NULL;
    up* u = (up*)calloc(1, sizeof *u);
    if (!u) {
        sock_close(s);
        return NULL;
    }
    u->tipo = O_UP;
    u->fd = s;
    u->clen = -1;
    u->deadline = g_now + C->proxy_connect_ms;
    if (ev_add(s, u, 0, 1, &u->slot)) {
        sock_close(s);
        free(u);
        return NULL;
    }
    u->ev_wr = 1;
    u->next = g_ups;
    if (g_ups) g_ups->prev = u;
    g_ups = u;
    return u;
}

static void up_close(up* u) {
    if (u->muerto) return;
    u->muerto = 1;
    ev_del(u->fd, &u->slot);
    sock_close(u->fd);
    if (u->prev) u->prev->next = u->next;
    else g_ups = u->next;
    if (u->next) u->next->prev = u->prev;
    if (u->c && u->c->up == u) u->c->up = NULL;
    buf_free(&u->out);
    buf_free(&u->in);
    enterrar(u);
}

static void proxy_iniciar(conn* c, int tunel) {
    up* u = up_nuevo();
    if (!u) {
        respuesta_simple(c, 502, NULL, "502 Bad Gateway\n", 1);
        return;
    }
    u->c = c;
    c->up = u;
    if (c->proxy_line[0]) buf_adds(&u->out, c->proxy_line);
    if (tunel) {
        /* todo lo que mandó el cliente, tal cual (cuerpo y lo que venga después) */
        buf_add(&u->out, c->in.p + c->r.ini, c->in.len - c->r.ini);
        c->in.len = 0;
        u->modo = U_TUNNEL;
        c->state = C_TUNNEL;
        c->cerrar_despues = 1;
        CONN_IO(c, 1, 0);
    } else {
        buf_add(&u->out, c->in.p + c->r.ini, c->r.head_len);
        u->modo = U_BUF;
        c->state = C_PROXY;
        CONN_IO(c, 0, 0);
    }
}

/* Lee la cabecera de la respuesta del gateway. 1 lista, 0 falta, -1 mal. */
static int up_leer_cabecera(up* u) {
    const char* fin = NULL;
    for (size_t i = 0; i + 1 < u->in.len; i++) {
        if (u->in.p[i] == '\n' && u->in.p[i + 1] == '\n') { fin = u->in.p + i + 2; break; }
        if (i + 3 < u->in.len && !memcmp(u->in.p + i, "\r\n\r\n", 4)) { fin = u->in.p + i + 4; break; }
    }
    if (!fin) return u->in.len > HEAD_MAX ? -1 : 0;
    u->head_len = (size_t)(fin - u->in.p);
    int maj = 0, min = 0, st = 0;
    if (sscanf(u->in.p, "HTTP/%d.%d %d", &maj, &min, &st) != 3 || st < 100 || st > 999) return -1;
    u->status = st;
    u->clen = -1;
    const char* p = strchr(u->in.p, '\n') + 1;
    while (p < fin) {
        const char* eol = memchr(p, '\n', (size_t)(fin - p));
        if (!eol) break;
        const char* dp = memchr(p, ':', (size_t)(eol - p));
        if (dp) {
            const size_t nn = (size_t)(dp - p);
            const char* v = dp + 1;
            while (*v == ' ' || *v == '\t') v++;
            if (nn == 14 && !strncasecmp(p, "Content-Length", 14)) u->clen = strtoll(v, NULL, 10);
            else if (nn == 17 && !strncasecmp(p, "Transfer-Encoding", 17)) u->chunked = 1;
        }
        p = eol + 1;
    }
    if (u->chunked) u->clen = -1;
    u->sin_cuerpo = u->c && u->c->r.es_head;
    if (st < 200 || st == 204 || st == 304) u->sin_cuerpo = 1;
    u->head_ok = 1;
    return 1;
}

/* Valor de una cabecera de la respuesta del gateway (copia en out). */
static int up_header(const up* u, const char* nombre, char* out, size_t outlen) {
    const size_t n = strlen(nombre);
    const char* p = memchr(u->in.p, '\n', u->head_len);
    const char* fin = u->in.p + u->head_len;
    if (!p) return 0;
    p++;
    while (p < fin) {
        const char* eol = memchr(p, '\n', (size_t)(fin - p));
        if (!eol) break;
        if ((size_t)(eol - p) > n && p[n] == ':' && !strncasecmp(p, nombre, n)) {
            const char* v = p + n + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char* e = eol;
            while (e > v && (e[-1] == '\r' || e[-1] == ' ')) e--;
            snprintf(out, outlen, "%.*s", (int)(e - v), v);
            return 1;
        }
        p = eol + 1;
    }
    return 0;
}

/* Las cabeceras del gateway sin las de la conexión (y, si se comprime, sin el largo,
   con el ETag de la versión comprimida y con Vary). Línea de estado incluida. */
static void reescribir(const up* u, bw_buf* o, int gz, int vary) {
    const char* p = u->in.p;
    const char* fin = u->in.p + u->head_len;
    const char* eol = memchr(p, '\n', (size_t)(fin - p));
    size_t n = (size_t)(eol - p);
    if (n && p[n - 1] == '\r') n--;
    buf_add(o, p, n);
    buf_adds(o, "\r\n");
    p = eol + 1;
    int vary_ok = 0;
    while (p < fin) {
        eol = memchr(p, '\n', (size_t)(fin - p));
        if (!eol) break;
        n = (size_t)(eol - p);
        if (n && p[n - 1] == '\r') n--;
        if (n == 0) break;
        const char* dp = memchr(p, ':', n);
        const size_t nn = dp ? (size_t)(dp - p) : n;
#define ES(x) (nn == sizeof(x) - 1 && !strncasecmp(p, x, nn))
        int saltar = ES("Connection") || ES("Keep-Alive") || ES("Proxy-Connection");
        if (gz && (ES("Content-Length") || ES("Accept-Ranges") || ES("Transfer-Encoding"))) saltar = 1;
        if (gz && ES("ETag") && dp) {
            /* el ETag de la versión comprimida es otro (RFC 9110 §8.8.3) */
            const char* v = dp + 1;
            while (*v == ' ') v++;
            const char* e = p + n;
            if (e > v && e[-1] == '"') {
                buf_adds(o, "ETag: ");
                buf_add(o, v, (size_t)(e - v - 1));
                buf_adds(o, "-gz\"\r\n");
            }
            saltar = 1;
        }
        if (ES("Vary") && dp) {
            if (vary || gz) {
                char v[512];
                snprintf(v, sizeof v, "%.*s", (int)(n - nn - 1), dp + 1);
                if (tiene_token(v, "Accept-Encoding") || strchr(v, '*')) vary_ok = 1;
                else {
                    buf_add(o, p, n);
                    buf_adds(o, ", Accept-Encoding\r\n");
                    vary_ok = 1;
                    saltar = 1;
                }
            }
        }
#undef ES
        if (!saltar) {
            buf_add(o, p, n);
            buf_adds(o, "\r\n");
        }
        p = eol + 1;
    }
    if ((vary || gz) && !vary_ok) buf_adds(o, "Vary: Accept-Encoding\r\n");
}

static void proxy_respuesta_completa(conn* c, up* u) {
    /* modo U_BUF: el cuerpo entero en u->in; comprimir y mandar */
    const char* cuerpo = u->in.p + u->head_len;
    const size_t nb = (size_t)u->clen;
    bw_blob* gz = gz_compress(cuerpo, nb, C->gzip_level);
    c->out.len = 0;
    c->out_off = 0;
    if (gz && gz->len < nb) {
        reescribir(u, &c->out, 1, 1);
        buf_printf(&c->out, "Content-Encoding: gzip\r\nContent-Length: %zu\r\n", gz->len);
        c->body = gz;
        c->body_off = 0;
        c->body_end = gz->len;
    } else {
        blob_unref(gz);
        reescribir(u, &c->out, 0, 1);
        bw_blob* b = blob_new(nb);
        if (b) memcpy(b->data, cuerpo, nb);
        c->body = b;
        c->body_off = 0;
        c->body_end = b ? nb : 0;
    }
    c->status = u->status;
    cabecera_conexion(c, &c->out);
    buf_adds(&c->out, "\r\n");
    up_close(u);
    c->resp_lista = 1;
    c->state = C_WRITE;
    conn_write(c);
}

/* Pasa a modo stream: la cabecera reescrita y lo que ya llegó del cuerpo. */
static void proxy_a_stream(conn* c, up* u) {
    char ct[256] = "", ce[128] = "", cc[256] = "";
    up_header(u, "Content-Type", ct, sizeof ct);
    up_header(u, "Content-Encoding", ce, sizeof ce);
    up_header(u, "Cache-Control", cc, sizeof cc);
    const int vary = u->status == 200 && !ce[0] && type_compressible(C, ct) && !strstr(cc, "no-transform");
    c->out.len = 0;
    c->out_off = 0;
    reescribir(u, &c->out, 0, vary);
    if (!u->sin_cuerpo && u->clen < 0) c->cerrar_despues = 1; /* termina cuando cierra el gateway */
    c->status = u->status;
    cabecera_conexion(c, &c->out);
    buf_adds(&c->out, "\r\n");
    u->modo = U_STREAM;
    c->state = C_WRITE;
    u->deadline = g_now + (u->clen < 0 ? C->proxy_stream_ms : C->proxy_read_ms);
    const size_t ya = u->in.len - u->head_len;
    if (!u->sin_cuerpo && ya) {
        size_t pasar = ya;
        if (u->clen >= 0 && (int64_t)pasar > u->clen) pasar = (size_t)u->clen;
        buf_add(&c->out, u->in.p + u->head_len, pasar);
        u->recibidos = (int64_t)pasar;
    }
    u->in.len = 0;
    if (u->sin_cuerpo || (u->clen >= 0 && u->recibidos >= u->clen)) {
        up_close(u);
        c->resp_lista = 1;
    }
    conn_write(c);
}

static void proxy_decidir(conn* c, up* u) {
    char ct[256] = "", ce[128] = "", cc[256] = "";
    up_header(u, "Content-Type", ct, sizeof ct);
    up_header(u, "Content-Encoding", ce, sizeof ce);
    up_header(u, "Cache-Control", cc, sizeof cc);
    const int candidato = c->r.es_get && u->status == 200 && !ce[0] && !u->chunked && u->clen >= C->gzip_min &&
                          (size_t)u->clen <= C->proxy_buffer_max && type_compressible(C, ct) &&
                          !strstr(cc, "no-transform") && accepts_gzip(c->r.ae);
    if (!candidato) {
        proxy_a_stream(c, u);
        return;
    }
    u->modo = U_BUF; /* se espera el cuerpo entero */
    if ((int64_t)(u->in.len - u->head_len) >= u->clen) proxy_respuesta_completa(c, u);
}

/* ------------------------------------------------------------------------- */
/* Microcaché (proxy_cache_valid)                                            */
/* ------------------------------------------------------------------------- */

static void vistos_anotar(const char* query, const char* nombre) {
    if (!query || !nombre[0] || !g_clave[0]) return;
    const size_t n = strlen(nombre);
    const char* p = query;
    while (p && *p) {
        if (!strncmp(p, nombre, n) && p[n] == '=') {
            const char* v = p + n + 1;
            const char* e = strchr(v, '&');
            const size_t len = e ? (size_t)(e - v) : strlen(v);
            if (!len || len > 64) return;
            uint32_t h = fnv32(v, len);
            if (!h) h = 1;
            /* conjunto abierto de 2×VISTOS_MAX lugares */
            uint32_t i = h % (VISTOS_MAX * 2);
            while (g_vistos[i] && g_vistos[i] != h) i = (i + 1) % (VISTOS_MAX * 2);
            if (!g_vistos[i] && g_nvistos < VISTOS_MAX) {
                g_vistos[i] = h;
                g_nvistos++;
            }
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static void cache_servir(conn* c, centry* e) {
    req* r = &c->r;
    c->out.len = 0;
    c->out_off = 0;
    int usar_gz = 0;
    if (e->comprimible && e->body && e->body->len >= (size_t)C->gzip_min && accepts_gzip(r->ae)) {
        if (!e->gz) e->gz = gz_compress(e->body->data, e->body->len, C->gzip_level);
        usar_gz = e->gz && e->gz->len < e->body->len;
    }
    char etag[56];
    snprintf(etag, sizeof etag, "%.*s%s\"", (int)strlen(e->etag) - 1, e->etag, usar_gz ? "-gz" : "");
    if (e->status == 200 && r->inm && etag_coincide(r->inm, etag)) {
        buf_printf(&c->out, "HTTP/1.1 304 Not Modified\r\nServer: ba-web\r\nDate: %s\r\nETag: %s\r\n", fecha(), etag);
        if (e->comprimible) buf_adds(&c->out, "Vary: Accept-Encoding\r\n");
        c->status = 304;
    } else {
        bw_blob* b = usar_gz ? e->gz : e->body;
        buf_printf(&c->out, "HTTP/1.1 %d %s\r\nServer: ba-web\r\nDate: %s\r\n", e->status, razon(e->status), fecha());
        buf_add(&c->out, e->hdrs.p, e->hdrs.len);
        if (e->status == 200) buf_printf(&c->out, "ETag: %s\r\n", etag);
        if (usar_gz) buf_adds(&c->out, "Content-Encoding: gzip\r\n");
        if (e->comprimible) buf_adds(&c->out, "Vary: Accept-Encoding\r\n");
        buf_printf(&c->out, "Content-Length: %zu\r\n", b ? b->len : (size_t)0);
        if (!r->es_head && b && b->len) {
            c->body = blob_ref(b);
            c->body_off = 0;
            c->body_end = b->len;
        }
        c->status = e->status;
    }
    cabecera_conexion(c, &c->out);
    buf_adds(&c->out, "\r\n");
    c->resp_lista = 1;
    c->state = C_WRITE;
    conn_write(c);
}

static void cache_despertar(centry* e, int ok) {
    conn* w = e->esperando;
    e->esperando = NULL;
    while (w) {
        conn* sig = w->espera_sig;
        w->espera = NULL;
        w->espera_sig = NULL;
        if (!w->muerto) {
            if (ok || e->tiene) cache_servir(w, e);
            else respuesta_simple(w, 502, NULL, "502 Bad Gateway\n", 0);
        }
        w = sig;
    }
}

static void cache_traer(centry* e, const conn* quien) {
    up* u = up_nuevo();
    if (!u) {
        cache_despertar(e, 0);
        return;
    }
    u->modo = U_CACHE;
    u->ce = e;
    e->fetch = u;
    const char* host = quien && quien->r.host ? quien->r.host : "127.0.0.1";
    char limpio[256];
    snprintf(limpio, sizeof limpio, "%s", host);
    if (strpbrk(limpio, "\r\n")) snprintf(limpio, sizeof limpio, "127.0.0.1");
    buf_printf(&u->out, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: ba-web/" BW_VERSION "\r\nConnection: close\r\n",
               e->key, limpio);
    if (g_clave[0] && g_nvistos) {
        buf_printf(&u->out, "X-BA-Clave: %s\r\nX-BA-Vistos: ", g_clave);
        int k = 0;
        for (int i = 0; i < VISTOS_MAX * 2; i++)
            if (g_vistos[i]) {
                buf_printf(&u->out, k++ ? ",%08x" : "%08x", g_vistos[i]);
                g_vistos[i] = 0;
            }
        g_nvistos = 0;
        buf_adds(&u->out, "\r\n");
    }
    buf_adds(&u->out, "\r\n");
}

static void cache_guardar(centry* e, up* u) {
    e->status = u->status;
    e->hdrs.len = 0;
    /* las cabeceras que valen para todos: sin las de la conexión, el largo, la fecha ni el servidor */
    const char* p = memchr(u->in.p, '\n', u->head_len) + 1;
    const char* fin = u->in.p + u->head_len;
    char ct[256] = "", ce[64] = "", cc[256] = "";
    while (p < fin) {
        const char* eol = memchr(p, '\n', (size_t)(fin - p));
        if (!eol) break;
        size_t n = (size_t)(eol - p);
        if (n && p[n - 1] == '\r') n--;
        if (!n) break;
        const char* dp = memchr(p, ':', n);
        const size_t nn = dp ? (size_t)(dp - p) : n;
#define ES(x) (nn == sizeof(x) - 1 && !strncasecmp(p, x, nn))
        if (!(ES("Connection") || ES("Keep-Alive") || ES("Content-Length") || ES("Transfer-Encoding") ||
              ES("Date") || ES("Server") || ES("ETag") || ES("Set-Cookie") || ES("Vary"))) {
            buf_add(&e->hdrs, p, n);
            buf_adds(&e->hdrs, "\r\n");
        }
#undef ES
        p = eol + 1;
    }
    up_header(u, "Content-Type", ct, sizeof ct);
    up_header(u, "Content-Encoding", ce, sizeof ce);
    up_header(u, "Cache-Control", cc, sizeof cc);
    const char* cuerpo = u->in.p + u->head_len;
    size_t nb = u->in.len - u->head_len;
    if (u->clen >= 0 && (size_t)u->clen < nb) nb = (size_t)u->clen;
    blob_unref(e->body);
    blob_unref(e->gz);
    e->gz = NULL;
    e->body = blob_new(nb);
    if (e->body) memcpy(e->body->data, cuerpo, nb);
    e->comprimible = !ce[0] && type_compressible(C, ct) && !strstr(cc, "no-transform");
    snprintf(e->etag, sizeof e->etag, "\"%016llx\"", (unsigned long long)fnv64(cuerpo, nb));
    e->tiene = 1;
    e->traida = g_now;
    /* solo los 200 se guardan: lo demás se le da a los que esperaban y se vuelve a pedir */
    if (u->status != 200) e->traida = g_now - e->valido_ms;
}

static centry* cache_buscar(const char* key, int valido_ms) {
    for (centry* e = g_cache; e; e = e->sig)
        if (!strcmp(e->key, key)) return e;
    if (g_ncache >= 256) {
        /* tope: se va la más vieja que no esté en uso */
        centry **pp = &g_cache, **viejo = NULL;
        for (; *pp; pp = &(*pp)->sig)
            if (!(*pp)->fetch && !(*pp)->esperando && (!viejo || (*pp)->traida < (*viejo)->traida)) viejo = pp;
        if (viejo) {
            centry* v = *viejo;
            *viejo = v->sig;
            blob_unref(v->body);
            blob_unref(v->gz);
            buf_free(&v->hdrs);
            free(v);
            g_ncache--;
        }
    }
    centry* e = (centry*)calloc(1, sizeof *e);
    if (!e) return NULL;
    snprintf(e->key, sizeof e->key, "%s", key);
    e->valido_ms = valido_ms;
    e->sig = g_cache;
    g_cache = e;
    g_ncache++;
    return e;
}

static void cache_pedido(conn* c, const bw_location* l) {
    req* r = &c->r;
    char key[2048];
    if (l->cache_key_uri) snprintf(key, sizeof key, "%s", r->uri);
    else snprintf(key, sizeof key, "%s%s%s", r->uri, r->query ? "?" : "", r->query ? r->query : "");
    /* la uri decodificada podría tener espacios o '\r': para pedirla, se usa la original */
    for (char* k = key; *k; k++)
        if ((unsigned char)*k <= ' ' || *k == 0x7f) {
            proxy_iniciar(c, 0);
            return;
        }
    vistos_anotar(r->query, l->vistos);
    centry* e = cache_buscar(key, l->cache_valid_ms);
    if (!e) {
        proxy_iniciar(c, 0);
        return;
    }
    const int fresca = e->tiene && g_now - e->traida < e->valido_ms;
    if (fresca) {
        cache_servir(c, e);
        return;
    }
    if (!e->fetch) cache_traer(e, c);
    if (e->tiene && e->status == 200) {
        /* vencida: se sirve la que hay mientras llega la nueva */
        cache_servir(c, e);
        return;
    }
    c->espera = e;
    c->espera_sig = e->esperando;
    e->esperando = c;
    c->state = C_CACHE;
    CONN_IO(c, 0, 0);
    c->deadline = g_now + C->proxy_read_ms;
}

/* ------------------------------------------------------------------------- */
/* Parser HTTP/1.1                                                           */
/* ------------------------------------------------------------------------- */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodifica %XX y normaliza: sin «//», «.» ni «..» (si se sale de la raíz, error). */
static int normalizar(const char* path, size_t n, char* out, size_t outlen) {
    char dec[4096];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = path[i];
        if (ch == '%') {
            if (i + 2 >= n) return -1;
            const int a = hexval(path[i + 1]), b = hexval(path[i + 2]);
            if (a < 0 || b < 0) return -1;
            ch = (char)(a * 16 + b);
            i += 2;
            if (ch == 0) return -1;
        }
        if (ch == '\\') return -1;
        if (k + 1 >= sizeof dec) return -2;
        dec[k++] = ch;
    }
    dec[k] = 0;
    if (dec[0] != '/') return -1;
    size_t o = 0;
    size_t i = 0;
    while (i < k) {
        while (i < k && dec[i] == '/') i++;
        size_t j = i;
        while (j < k && dec[j] != '/') j++;
        const size_t seg = j - i;
        if (seg == 1 && dec[i] == '.') {
        } else if (seg == 2 && dec[i] == '.' && dec[i + 1] == '.') {
            if (o == 0) return -1;
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
        } else if (seg) {
            if (o + seg + 2 >= outlen) return -2;
            out[o++] = '/';
            memcpy(out + o, dec + i, seg);
            o += seg;
        }
        i = j;
    }
    /* la barra final se respeta (directorio) */
    if (o == 0 || (k > 0 && dec[k - 1] == '/') || (k >= 2 && dec[k - 1] == '.' && dec[k - 2] == '/')) {
        if (o + 2 >= outlen) return -2;
        if (o == 0 || out[o - 1] != '/') out[o++] = '/';
    }
    out[o] = 0;
    return 0;
}

static int es_tchar(char c) {
    return isalnum((unsigned char)c) || strchr("!#$%&'*+-.^_`|~", c);
}

/* 0 bien, o el código de error. */
static int parsear(conn* c, size_t ini, size_t largo) {
    req* r = &c->r;
    memset(r, 0, sizeof *r);
    r->clen = -1;
    r->ini = ini;
    r->head_len = largo;
    c->head.len = 0;
    buf_add(&c->head, c->in.p + ini, largo);
    char* p = c->head.p;
    char* fin = p + c->head.len;
    /* línea de pedido */
    char* eol = memchr(p, '\n', (size_t)(fin - p));
    if (!eol) return 400;
    *eol = 0;
    if (eol > p && eol[-1] == '\r') eol[-1] = 0;
    char* sp1 = strchr(p, ' ');
    if (!sp1 || sp1 - p >= (long)sizeof r->metodo || sp1 == p) return 400;
    for (char* m = p; m < sp1; m++)
        if (!es_tchar(*m)) return 400;
    memcpy(r->metodo, p, (size_t)(sp1 - p));
    r->metodo[sp1 - p] = 0;
    char* target = sp1 + 1;
    char* sp2 = strchr(target, ' ');
    if (!sp2 || sp2 == target) return 400;
    *sp2 = 0;
    const char* ver = sp2 + 1;
    if (!strcmp(ver, "HTTP/1.1")) r->v11 = 1;
    else if (!strcmp(ver, "HTTP/1.0")) r->v11 = 0;
    else if (!strncmp(ver, "HTTP/", 5)) return 505;
    else return 400;
    if (strlen(target) >= 8192) return 414;
    r->target = target;
    r->es_get = !strcmp(r->metodo, "GET");
    r->es_head = !strcmp(r->metodo, "HEAD");
    /* cabeceras */
    int nhost = 0;
    p = eol + 1;
    while (p < fin) {
        eol = memchr(p, '\n', (size_t)(fin - p));
        if (!eol) break;
        *eol = 0;
        if (eol > p && eol[-1] == '\r') eol[-1] = 0;
        if (!*p) break;
        if (*p == ' ' || *p == '\t') return 400; /* obs-fold: RFC 9112 §5.2 deja rechazarlo */
        char* dp = strchr(p, ':');
        if (!dp || dp == p) return 400;
        for (char* q = p; q < dp; q++)
            if (!es_tchar(*q)) return 400; /* «Nombre :» o raros: RFC 9112 §5.1 */
        *dp = 0;
        char* v = dp + 1;
        while (*v == ' ' || *v == '\t') v++;
        char* e = v + strlen(v);
        while (e > v && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!strcasecmp(p, "Host")) {
            r->host = v;
            nhost++;
        } else if (!strcasecmp(p, "Content-Length")) {
            if (!*v) return 400;
            int64_t n = 0;
            for (const char* d = v; *d; d++) {
                if (!isdigit((unsigned char)*d) || n > (INT64_MAX - 9) / 10) return 400;
                n = n * 10 + (*d - '0');
            }
            if (r->clen >= 0 && r->clen != n) return 400;
            r->clen = n;
        } else if (!strcasecmp(p, "Transfer-Encoding")) {
            r->chunked = 1; /* lo que sea: el cuerpo no se sabe cuánto mide */
        } else if (!strcasecmp(p, "Connection")) {
            if (tiene_token(v, "close")) r->conn_close = 1;
            if (tiene_token(v, "keep-alive")) r->conn_keep = 1;
        } else if (!strcasecmp(p, "Accept-Encoding")) {
            r->ae = v;
        } else if (!strcasecmp(p, "If-None-Match")) {
            r->inm = v;
        } else if (!strcasecmp(p, "If-Modified-Since")) {
            r->ims = v;
        } else if (!strcasecmp(p, "Range")) {
            r->range = v;
        } else if (!strcasecmp(p, "If-Range")) {
            r->ifrange = v;
        } else if (!strcasecmp(p, "Upgrade")) {
            r->upgrade = v;
        }
        p = eol + 1;
    }
    if (r->chunked && r->clen >= 0) return 400; /* las dos: contrabando de pedidos (RFC 9112 §6.1) */
    if (r->v11 && nhost != 1) return 400;
    if (!r->v11 && nhost > 1) return 400;
    /* destino: forma origen «/…» o absoluta «http://host/…» */
    const char* path = target;
    if (!strncasecmp(path, "http://", 7) || !strncasecmp(path, "https://", 8)) {
        path = strchr(path + (path[4] == ':' ? 7 : 8), '/');
        if (!path) path = "/";
    } else if (path[0] != '/') {
        return (!strcmp(target, "*") && !strcmp(r->metodo, "OPTIONS")) ? 0 : 400;
    }
    const char* q = strchr(path, '?');
    const size_t np = q ? (size_t)(q - path) : strlen(path);
    if (q) r->query = (char*)q + 1;
    const char* hash = memchr(path, '#', np); /* no debería venir, pero si viene no cuenta */
    const int k = normalizar(path, hash ? (size_t)(hash - path) : np, r->uri, sizeof r->uri);
    if (k == -2) return 414;
    if (k < 0) return 400;
    c->keepalive = r->v11 ? !r->conn_close : (r->conn_keep && !r->conn_close);
    if (c->nreq + 1 >= C->keepalive_requests) c->keepalive = 0;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Conexiones                                                                */
/* ------------------------------------------------------------------------- */

static void reset_salida(conn* c) {
    c->out.len = 0;
    c->out_off = 0;
    blob_unref(c->body);
    c->body = NULL;
    c->body_off = c->body_end = 0;
    if (c->tiene_file) file_close(&c->file);
    c->tiene_file = 0;
    c->resp_lista = 0;
}

static void registrar(conn* c) {
    if (!C->access_log[0] || !c->r.metodo[0]) return;
    char linea[1200];
    snprintf(linea, sizeof linea, "%s - - [%s] \"%s %.900s %s\" %d %lld", c->ip, fecha(), c->r.metodo,
             c->r.target ? c->r.target : "-", c->r.v11 ? "HTTP/1.1" : "HTTP/1.0", c->status, (long long)c->enviados);
    access_line(linea);
    c->r.metodo[0] = 0;
}

static void sacar_de_espera(conn* c) {
    if (!c->espera) return;
    conn** pp = &c->espera->esperando;
    while (*pp && *pp != c) pp = &(*pp)->espera_sig;
    if (*pp) *pp = c->espera_sig;
    c->espera = NULL;
    c->espera_sig = NULL;
}

static void conn_close(conn* c) {
    if (c->muerto) return;
    c->muerto = 1;
    if (c->state != C_READ || c->in.len) registrar(c);
    if (c->up) up_close(c->up);
    sacar_de_espera(c);
    ev_del(c->fd, &c->slot);
    sock_close(c->fd);
    if (c->prev) c->prev->next = c->next;
    else g_conns = c->next;
    if (c->next) c->next->prev = c->prev;
    reset_salida(c);
    buf_free(&c->in);
    buf_free(&c->head);
    buf_free(&c->out);
    g_nconns--;
    enterrar(c);
}

static void finish_response(conn* c) {
    registrar(c);
    reset_salida(c);
    if (c->cerrar_despues || !c->keepalive || g_worker_quit) {
        conn_close(c);
        return;
    }
    /* lo que quede en c->in después de este pedido es el próximo (pipelining) */
    size_t usado = c->r.ini + c->r.head_len;
    if (c->r.clen > 0) usado += (size_t)c->r.clen;
    if (usado > c->in.len) {
        conn_close(c); /* el cuerpo no llegó entero: no se sabe dónde empieza lo próximo */
        return;
    }
    buf_consume(&c->in, usado);
    memset(&c->r, 0, sizeof c->r);
    c->nreq++;
    c->status = 0;
    c->enviados = 0;
    c->state = C_READ;
    c->deadline = g_now + (c->in.len ? C->header_timeout_ms : C->keepalive_timeout_ms);
    CONN_IO(c, 1, 0);
    if (c->in.len) conn_process(c);
}

static void conn_write(conn* c) {
    for (;;) {
        if (c->muerto) return;
        if (c->out_off < c->out.len) {
            const int n = (int)send(c->fd, c->out.p + c->out_off, (int)(c->out.len - c->out_off), BW_SEND_FLAGS);
            if (n < 0) {
                if (sock_would_block()) break;
                conn_close(c);
                return;
            }
            c->out_off += (size_t)n;
            c->enviados += n;
            c->deadline = g_now + C->send_timeout_ms;
            if (c->out_off >= c->out.len) {
                c->out.len = 0;
                c->out_off = 0;
                /* si se había frenado al gateway por tener mucho para mandar, que siga */
                if (c->up && !c->up->muerto && c->up->modo == U_STREAM) UP_IO(c->up, 1, c->up->ev_wr);
                if (c->up && !c->up->muerto && c->up->modo == U_TUNNEL) UP_IO(c->up, 1, c->up->ev_wr);
            }
            continue;
        }
        if (c->body && c->body_off < c->body_end) {
            const int n =
                (int)send(c->fd, c->body->data + c->body_off, (int)(c->body_end - c->body_off), BW_SEND_FLAGS);
            if (n < 0) {
                if (sock_would_block()) break;
                conn_close(c);
                return;
            }
            c->body_off += (size_t)n;
            c->enviados += n;
            c->deadline = g_now + C->send_timeout_ms;
            continue;
        }
        if (c->tiene_file && c->file_off < c->file_end) {
            const int64_t n = file_send(c->fd, &c->file, c->file_off, c->file_end - c->file_off);
            if (n < 0) {
                conn_close(c);
                return;
            }
            if (n == 0) break;
            c->file_off += n;
            c->enviados += n;
            c->deadline = g_now + C->send_timeout_ms;
            continue;
        }
        /* todo mandado */
        if (c->state == C_TUNNEL) {
            if (!c->up) {
                conn_close(c); /* el gateway ya cerró y no queda nada */
                return;
            }
            CONN_IO(c, !c->up->cliente_eof && c->up->out.len - c->up->out_off < STREAM_HIGH, 0);
            return;
        }
        if (c->resp_lista) {
            finish_response(c);
            return;
        }
        CONN_IO(c, 0, 0); /* esperando más del gateway */
        return;
    }
    CONN_IO(c, c->state == C_TUNNEL && c->up && !c->up->cliente_eof, 1);
}

static void despachar(conn* c) {
    req* r = &c->r;
    if (!r->uri[0]) { /* OPTIONS * */
        if (C->upstream_port) proxy_iniciar(c, 1);
        else respuesta_simple(c, 204, "Allow: GET, HEAD, OPTIONS\r\n", NULL, 0);
        return;
    }
    const bw_location* l = conf_match(C, r->uri);
    if (!l) {
        respuesta_simple(c, 404, NULL, NULL, 0);
        return;
    }
    const int con_cuerpo = r->chunked || r->clen > 0;
    const int tunel = r->upgrade || con_cuerpo || !(r->es_get || r->es_head);
    switch (l->handler) {
    case H_RETURN: {
        char extra[700] = "";
        if (l->ret_code >= 300 && l->ret_code < 400) {
            snprintf(extra, sizeof extra, "Location: %s\r\n", l->ret_text);
            respuesta_simple(c, l->ret_code, extra, NULL, 0);
        } else {
            respuesta_simple(c, l->ret_code, NULL, l->ret_text[0] ? l->ret_text : NULL, 0);
        }
        return;
    }
    case H_STATIC:
        if (!(r->es_get || r->es_head) || r->upgrade) {
            if (l->try_upstream) proxy_iniciar(c, 1);
            else respuesta_simple(c, 405, "Allow: GET, HEAD\r\n", NULL, con_cuerpo);
            return;
        }
        if (con_cuerpo) c->cerrar_despues = 1; /* un GET con cuerpo: no se lee, se cierra al final */
        if (servir_estatico(c, l)) return;
        if (l->try_upstream) proxy_iniciar(c, tunel);
        else respuesta_simple(c, 404, NULL, NULL, 0);
        return;
    case H_PROXY:
        if (l->cache_valid_ms > 0 && !tunel) cache_pedido(c, l);
        else proxy_iniciar(c, tunel);
        return;
    default:
        respuesta_simple(c, 500, NULL, NULL, 1);
    }
}

static void conn_process(conn* c) {
    if (c->state != C_READ || c->muerto) return;
    /* PROXY v1 (solo la primera vez y solo desde loopback) */
    if (c->lc->proxy_protocol && !c->proxy_visto) {
        static const char kp[] = "PROXY ";
        const size_t m = c->in.len < 6 ? c->in.len : 6;
        if (memcmp(c->in.p, kp, m) == 0) {
            if (c->in.len < 6) return;
            char* eol = memchr(c->in.p, '\n', c->in.len < 108 ? c->in.len : 108);
            if (!eol) {
                if (c->in.len >= 108) {
                    conn_close(c);
                }
                return;
            }
            const size_t n = (size_t)(eol - c->in.p) + 1;
            if (!es_loopback(c->ip) || n >= sizeof c->proxy_line || eol == c->in.p || eol[-1] != '\r') {
                conn_close(c);
                return;
            }
            memcpy(c->proxy_line, c->in.p, n);
            c->proxy_line[n] = 0;
            char fam[8], src[64];
            if (sscanf(c->proxy_line, "PROXY %7s %63s", fam, src) == 2 && strcmp(fam, "UNKNOWN"))
                snprintf(c->ip, sizeof c->ip, "%s", src);
            buf_consume(&c->in, n);
        }
        c->proxy_visto = 1;
    }
    /* líneas vacías antes del pedido: se ignoran (RFC 9112 §2.2) */
    size_t ini = 0;
    while (ini < c->in.len && (c->in.p[ini] == '\r' || c->in.p[ini] == '\n')) ini++;
    if (ini == c->in.len) {
        if (ini > 1024) conn_close(c);
        return;
    }
    size_t fin = 0;
    for (size_t i = ini; i + 1 < c->in.len; i++) {
        if (c->in.p[i] == '\n' && c->in.p[i + 1] == '\n') { fin = i + 2; break; }
        if (i + 3 < c->in.len && !memcmp(c->in.p + i, "\r\n\r\n", 4)) { fin = i + 4; break; }
        if (i - ini > HEAD_MAX) break;
    }
    if (!fin) {
        if (c->in.len - ini > HEAD_MAX) {
            c->r.ini = ini;
            respuesta_simple(c, 431, NULL, NULL, 1);
        } else {
            c->deadline = g_now + C->header_timeout_ms;
        }
        return;
    }
    const int err = parsear(c, ini, fin - ini);
    if (err) {
        respuesta_simple(c, err, NULL, NULL, 1);
        return;
    }
    despachar(c);
}

static void conn_leer(conn* c) {
    if (c->state == C_TUNNEL) {
        up* u = c->up;
        if (!u) {
            conn_close(c);
            return;
        }
        if (buf_reserve(&u->out, 16384)) {
            conn_close(c);
            return;
        }
        const int n = (int)recv(c->fd, u->out.p + u->out.len, 16384, 0);
        if (n < 0) {
            if (!sock_would_block()) conn_close(c);
            return;
        }
        if (n == 0) {
            u->cliente_eof = 1;
            CONN_IO(c, 0, c->ev_wr);
            if (u->out_off >= u->out.len) {
#ifdef _WIN32
                shutdown(u->fd, SD_SEND);
#else
                shutdown(u->fd, SHUT_WR);
#endif
            }
            return;
        }
        u->out.len += (size_t)n;
        c->deadline = u->deadline = g_now + C->proxy_stream_ms;
        if (u->conectado) UP_IO(u, u->ev_rd, 1);
        if (u->out.len - u->out_off >= STREAM_HIGH) CONN_IO(c, 0, c->ev_wr);
        return;
    }
    if (c->state != C_READ) {
        /* no se esperaba leer: ¿se cerró? */
        char tmp[1];
        const int n = (int)recv(c->fd, tmp, 0, 0);
        (void)n;
        CONN_IO(c, 0, c->ev_wr);
        return;
    }
    if (buf_reserve(&c->in, 16384)) {
        conn_close(c);
        return;
    }
    const int n = (int)recv(c->fd, c->in.p + c->in.len, (int)(c->in.cap - c->in.len - 1), 0);
    if (n < 0) {
        if (!sock_would_block()) conn_close(c);
        return;
    }
    if (n == 0) {
        conn_close(c);
        return;
    }
    c->in.len += (size_t)n;
    c->in.p[c->in.len] = 0;
    conn_process(c);
}

/* ------------------------------------------------------------------------- */
/* El gateway                                                                */
/* ------------------------------------------------------------------------- */

static void up_falla(up* u, int code) {
    conn* c = u->c;
    centry* e = u->ce;
    const int stream = u->modo == U_STREAM;
    up_close(u);
    if (e) {
        e->fetch = NULL;
        if (e->tiene) e->traida = g_now - e->valido_ms + 1000; /* se reintenta en 1 s */
        cache_despertar(e, 0);
        return;
    }
    if (!c || c->muerto) return;
    if (c->state == C_PROXY) respuesta_simple(c, code, NULL, NULL, 1);
    else if (c->state == C_TUNNEL || stream) {
        c->cerrar_despues = 1;
        c->resp_lista = 1;
        if (c->out_off >= c->out.len) conn_close(c);
        else conn_write(c);
    }
}

static void up_escribir(up* u) {
    if (!u->conectado) {
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(u->fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        if (err) {
            up_falla(u, 502);
            return;
        }
        u->conectado = 1;
        u->deadline = g_now + (u->modo == U_TUNNEL ? C->proxy_stream_ms : C->proxy_read_ms);
    }
    while (u->out_off < u->out.len) {
        const int n = (int)send(u->fd, u->out.p + u->out_off, (int)(u->out.len - u->out_off), BW_SEND_FLAGS);
        if (n < 0) {
            if (sock_would_block()) {
                UP_IO(u, 1, 1);
                return;
            }
            up_falla(u, 502);
            return;
        }
        u->out_off += (size_t)n;
    }
    u->out.len = u->out_off = 0;
    UP_IO(u, 1, 0);
    if (u->modo == U_TUNNEL && u->c) {
        if (u->cliente_eof) {
#ifdef _WIN32
            shutdown(u->fd, SD_SEND);
#else
            shutdown(u->fd, SHUT_WR);
#endif
        } else {
            CONN_IO(u->c, 1, u->c->ev_wr);
        }
    }
}

static void up_leer(up* u) {
    conn* c = u->c;
    if (u->modo == U_TUNNEL || u->modo == U_STREAM) {
        if (!c || c->muerto) {
            up_close(u);
            return;
        }
        if (c->out.len - c->out_off >= STREAM_HIGH) {
            UP_IO(u, 0, u->ev_wr); /* el cliente no da abasto: se frena */
            return;
        }
        if (buf_reserve(&c->out, 65536)) {
            up_falla(u, 502);
            return;
        }
        int quiero = 65536;
        if (u->modo == U_STREAM && u->clen >= 0 && u->clen - u->recibidos < quiero) quiero = (int)(u->clen - u->recibidos);
        const int n = quiero > 0 ? (int)recv(u->fd, c->out.p + c->out.len, quiero, 0) : 0;
        if (n < 0) {
            if (!sock_would_block()) up_falla(u, 502);
            return;
        }
        if (n == 0) {
            /* el gateway terminó */
            if (u->modo == U_STREAM && u->clen >= 0 && u->recibidos < u->clen) c->cerrar_despues = 1;
            if (u->modo == U_TUNNEL) c->cerrar_despues = 1;
            up_close(u);
            c->resp_lista = 1;
            if (c->state == C_TUNNEL) c->state = C_WRITE;
            conn_write(c);
            return;
        }
        c->out.len += (size_t)n;
        u->recibidos += n;
        u->deadline = c->deadline =
            g_now + ((u->modo == U_TUNNEL || u->clen < 0) ? C->proxy_stream_ms : C->proxy_read_ms);
        if (u->modo == U_STREAM && u->clen >= 0 && u->recibidos >= u->clen) {
            up_close(u);
            c->resp_lista = 1;
        }
        conn_write(c);
        return;
    }
    /* U_BUF y U_CACHE: se junta la respuesta */
    if (buf_reserve(&u->in, 65536)) {
        up_falla(u, 502);
        return;
    }
    const int n = (int)recv(u->fd, u->in.p + u->in.len, (int)(u->in.cap - u->in.len - 1), 0);
    if (n < 0) {
        if (!sock_would_block()) up_falla(u, 502);
        return;
    }
    u->deadline = g_now + C->proxy_read_ms;
    if (n > 0) {
        u->in.len += (size_t)n;
        u->in.p[u->in.len] = 0;
    }
    if (!u->head_ok) {
        const int k = up_leer_cabecera(u);
        if (k < 0 || (k == 0 && n == 0)) {
            up_falla(u, 502);
            return;
        }
        if (k == 0) return;
        if (u->modo == U_BUF) {
            if (!c || c->muerto) {
                up_close(u);
                return;
            }
            proxy_decidir(c, u);
            return;
        }
    }
    const int64_t cuerpo = (int64_t)(u->in.len - u->head_len);
    if (u->modo == U_CACHE) {
        if ((size_t)cuerpo > C->proxy_buffer_max) {
            up_falla(u, 502);
            return;
        }
        if (n == 0 || (u->clen >= 0 && cuerpo >= u->clen) || u->sin_cuerpo) {
            centry* e = u->ce;
            if (u->clen >= 0 && cuerpo < u->clen) {
                up_falla(u, 502);
                return;
            }
            cache_guardar(e, u);
            e->fetch = NULL;
            up_close(u);
            cache_despertar(e, 1);
        }
        return;
    }
    /* U_BUF con el cuerpo en camino */
    if (!c || c->muerto) {
        up_close(u);
        return;
    }
    if (cuerpo >= u->clen) proxy_respuesta_completa(c, u);
    else if (n == 0) up_falla(u, 502);
}

/* ------------------------------------------------------------------------- */
/* Aceptar, vencimientos y el bucle                                          */
/* ------------------------------------------------------------------------- */

static void lst_armar(int si) {
    for (int i = 0; i < g_nlst; i++) {
        lst* l = g_lst[i];
        if (l->armado == si) continue;
        int rd = si, wr = 0;
        set_io(O_LISTEN, l, l->fd, &l->slot, &l->armado, &wr, rd, 0);
        l->armado = si;
    }
}

static void aceptar(lst* l) {
    for (int k = 0; k < 64 && g_nconns < C->worker_connections; k++) {
        struct sockaddr_storage sa;
        socklen_t len = sizeof sa;
        bw_sock s = accept(l->fd, (struct sockaddr*)&sa, &len);
        if (s == BW_BAD) return; /* otro worker se la llevó, o no hay más */
        sock_nonblock(s);
        conn* c = (conn*)calloc(1, sizeof *c);
        if (!c) {
            sock_close(s);
            return;
        }
        c->tipo = O_CONN;
        c->fd = s;
        c->lc = l->lc;
#ifndef _WIN32
        c->file.fd = -1;
#endif
        if (sa.ss_family == AF_INET)
            inet_ntop(AF_INET, &((struct sockaddr_in*)&sa)->sin_addr, c->ip, sizeof c->ip);
        else if (sa.ss_family == AF_INET6)
            inet_ntop(AF_INET6, &((struct sockaddr_in6*)&sa)->sin6_addr, c->ip, sizeof c->ip);
        c->deadline = g_now + C->header_timeout_ms;
        if (ev_add(s, c, 1, 0, &c->slot)) {
            sock_close(s);
            free(c);
            return;
        }
        c->ev_rd = 1;
        c->next = g_conns;
        if (g_conns) g_conns->prev = c;
        g_conns = c;
        g_nconns++;
    }
    if (g_nconns >= C->worker_connections) lst_armar(0);
}

static void vencimientos(void) {
    for (conn* c = g_conns; c;) {
        conn* sig = c->next;
        if (c->up && !c->up->muerto) {
            /* esperando al gateway (proxy, stream o túnel): el vencimiento lo lleva su up */
        } else if (!c->muerto && g_now > c->deadline) {
            if (c->state == C_CACHE) {
                sacar_de_espera(c);
                respuesta_simple(c, 504, NULL, NULL, 1);
            } else {
                conn_close(c);
            }
        } else if (g_worker_quit && c->state == C_READ && c->in.len == 0) {
            conn_close(c);
        }
        c = sig;
    }
    for (up* u = g_ups; u;) {
        up* sig = u->next;
        if (!u->muerto && g_now > u->deadline) up_falla(u, 504);
        u = sig;
    }
}

static int master_vivo(int64_t master_pid) {
    if (master_pid <= 0) return 1;
#ifdef _WIN32
    static HANDLE h = NULL;
    if (!h) h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)master_pid);
    if (!h) return 0;
    return WaitForSingleObject(h, 0) != WAIT_OBJECT_0;
#else
    return getppid() == (pid_t)master_pid;
#endif
}

int worker_run(const bw_conf* conf, bw_sock* listeners, int n, int64_t master_pid) {
    C = conf;
    const char* clave = getenv("BA_WEB_CLAVE");
    if (clave) snprintf(g_clave, sizeof g_clave, "%s", clave);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    if (ev_init(conf->worker_connections + 16)) {
        log_msg("error", "no se pudo crear el bucle de eventos");
        return 1;
    }
    g_now = now_ms();
    for (int i = 0; i < n; i++) {
        lst* l = (lst*)calloc(1, sizeof *l);
        l->tipo = O_LISTEN;
        l->fd = listeners[i];
        l->lc = &conf->listen[i];
        ev_add(l->fd, l, 1, 0, &l->slot);
        l->armado = 1;
        g_lst[g_nlst++] = l;
    }
    bw_evt evs[512];
    int64_t ultimo_barrido = g_now;
    int64_t quit_desde = 0;
    for (;;) {
        const int k = ev_wait(evs, 512, 500);
        g_now = now_ms();
        for (int i = 0; i < k; i++) {
            const int tipo = *(int*)evs[i].ptr;
            if (tipo == O_LISTEN) {
                if (!g_worker_quit) aceptar((lst*)evs[i].ptr);
            } else if (tipo == O_CONN) {
                conn* c = (conn*)evs[i].ptr;
                if (c->muerto) continue;
                if (evs[i].err && !evs[i].rd && !evs[i].wr) {
                    conn_close(c);
                    continue;
                }
                if (evs[i].rd) conn_leer(c);
                if (!c->muerto && evs[i].wr) conn_write(c);
            } else if (tipo == O_UP) {
                up* u = (up*)evs[i].ptr;
                if (u->muerto) continue;
                if (evs[i].err && !u->conectado) {
                    up_falla(u, 502);
                    continue;
                }
                if (evs[i].wr) up_escribir(u);
                if (!u->muerto && evs[i].rd) {
                    if (!u->conectado) up_escribir(u);
                    if (!u->muerto) up_leer(u);
                }
            }
        }
        for (int i = 0; i < g_ncem; i++) free(g_cementerio[i]);
        g_ncem = 0;
        if (g_nconns < C->worker_connections && !g_worker_quit) lst_armar(1);
        if (g_now - ultimo_barrido >= 500) {
            ultimo_barrido = g_now;
            vencimientos();
            for (int i = 0; i < g_ncem; i++) free(g_cementerio[i]);
            g_ncem = 0;
            if (!master_vivo(master_pid)) g_worker_quit = 1;
        }
        if (g_worker_quit) {
            if (!quit_desde) {
                quit_desde = g_now;
                lst_armar(0);
                for (int i = 0; i < g_nlst; i++) {
                    ev_del(g_lst[i]->fd, &g_lst[i]->slot);
                    sock_close(g_lst[i]->fd);
                }
                g_nlst = 0;
            }
            if (g_nconns == 0 || g_now - quit_desde > 5000) break;
        }
    }
    return 0;
}
