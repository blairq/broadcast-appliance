/*
 * ba-web: la configuración, con la sintaxis de nginx (un subconjunto):
 *
 *   worker_processes auto;            # o un número
 *   worker_connections 4096;
 *   keepalive_timeout 65;             # segundos (o 65s, 500ms)
 *   gzip on;  gzip_comp_level 6;  gzip_min_length 256;  gzip_types text/html …;
 *   open_file_cache_max 1m;  open_file_cache_total 64m;  proxy_buffer_max 4m;
 *   listen 127.0.0.1:8785 proxy_protocol;
 *   upstream 127.0.0.1:8786;
 *   error_log /ruta/ba-web.log;       access_log off;
 *   location /evento/_/ { alias /ruta/Web/evento/; try_files $uri @upstream; add_header Cache-Control no-cache; }
 *   location /evento/   { alias_file /ruta/Runtime/evento-web.dir; index portada.html; try_files $uri @upstream; }
 *   location = /api/v1/evento { proxy_pass; proxy_cache_valid 1s; proxy_cache_key $uri; ba_vistos v; }
 *   location / { proxy_pass; }
 *   location = /invitados { return 301 /invitados/; }
 *
 * Una directiva que no se conoce es un error (con la línea), como en nginx.
 * Elección de location: primero la exacta (=), si no el prefijo más largo.
 */
#include "bw.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kTiposGzip[] = {"text/html", "text/css", "text/plain", "text/xml", "text/javascript",
                                   "text/csv", "text/markdown", "application/javascript", "application/json",
                                   "application/xml", "application/manifest+json", "application/wasm",
                                   "image/svg+xml", "image/x-icon", "font/ttf", "font/otf",
                                   "application/vnd.ms-fontobject", NULL};

void conf_defaults(bw_conf* c) {
    memset(c, 0, sizeof *c);
    c->workers = 0;
    c->worker_connections = 4096;
    c->keepalive_timeout_ms = 65000;
    c->keepalive_requests = 1000;
    c->header_timeout_ms = 30000;
    c->send_timeout_ms = 60000;
    c->proxy_connect_ms = 5000;
    c->proxy_read_ms = 60000;
    c->proxy_stream_ms = 3600000;
    c->gzip = 1;
    c->gzip_level = 6;
    c->gzip_min = 256;
    for (int i = 0; kTiposGzip[i]; i++) snprintf(c->types[c->ntypes++], sizeof c->types[0], "%s", kTiposGzip[i]);
    c->file_cache_max = 1u << 20;
    c->file_cache_total = 64u << 20;
    c->proxy_buffer_max = 4u << 20;
    snprintf(c->upstream_host, sizeof c->upstream_host, "127.0.0.1");
    c->upstream_port = 0;
}

/* ---- Lexer: palabras, «"…"», «'…'», ';', '{', '}' y comentarios con '#' ---- */
typedef struct lexer {
    const char* p;
    int linea;
} lexer;

enum { T_FIN = 0, T_PAL, T_PYC, T_ABRE, T_CIERRA, T_ERR };

static int token(lexer* lx, char* out, size_t outlen) {
    for (;;) {
        while (*lx->p && isspace((unsigned char)*lx->p)) {
            if (*lx->p == '\n') lx->linea++;
            lx->p++;
        }
        if (*lx->p == '#') {
            while (*lx->p && *lx->p != '\n') lx->p++;
            continue;
        }
        break;
    }
    if (!*lx->p) return T_FIN;
    const char c = *lx->p;
    if (c == ';') { lx->p++; return T_PYC; }
    if (c == '{') { lx->p++; return T_ABRE; }
    if (c == '}') { lx->p++; return T_CIERRA; }
    size_t n = 0;
    if (c == '"' || c == '\'') {
        lx->p++;
        while (*lx->p && *lx->p != c) {
            char ch = *lx->p++;
            if (ch == '\\' && *lx->p) ch = *lx->p++;
            if (ch == '\n') lx->linea++;
            if (n + 1 < outlen) out[n++] = ch;
        }
        if (*lx->p != c) return T_ERR;
        lx->p++;
    } else {
        while (*lx->p && !isspace((unsigned char)*lx->p) && *lx->p != ';' && *lx->p != '{' && *lx->p != '}') {
            if (n + 1 < outlen) out[n++] = *lx->p;
            lx->p++;
        }
    }
    out[n] = 0;
    return T_PAL;
}

/* «65», «65s», «500ms», «2m», «1h» → milisegundos. El número solo son segundos. */
static int tiempo_ms(const char* s, int* out) {
    char* fin = NULL;
    const double v = strtod(s, &fin);
    if (fin == s || v < 0) return -1;
    double mul = 1000;
    if (!strcmp(fin, "ms")) mul = 1;
    else if (!strcmp(fin, "s") || !*fin) mul = 1000;
    else if (!strcmp(fin, "m")) mul = 60000;
    else if (!strcmp(fin, "h")) mul = 3600000;
    else return -1;
    *out = (int)(v * mul);
    return 0;
}
/* «256», «64k», «1m», «1g» → bytes. */
static int tamano(const char* s, size_t* out) {
    char* fin = NULL;
    const double v = strtod(s, &fin);
    if (fin == s || v < 0) return -1;
    double mul = 1;
    if (*fin == 'k' || *fin == 'K') mul = 1024;
    else if (*fin == 'm' || *fin == 'M') mul = 1024 * 1024;
    else if (*fin == 'g' || *fin == 'G') mul = 1024.0 * 1024 * 1024;
    else if (*fin) return -1;
    *out = (size_t)(v * mul);
    return 0;
}
static int onoff(const char* s, int* out) {
    if (!strcmp(s, "on")) *out = 1;
    else if (!strcmp(s, "off")) *out = 0;
    else return -1;
    return 0;
}
/* «127.0.0.1:8785», «8785», «[::1]:8785», «localhost:80». */
static int host_puerto(const char* s, char* host, size_t hl, int* port) {
    const char* dp = strrchr(s, ':');
    if (s[0] == '[') {
        const char* cb = strchr(s, ']');
        if (!cb || cb[1] != ':') return -1;
        snprintf(host, hl, "%.*s", (int)(cb - s - 1), s + 1);
        *port = atoi(cb + 2);
    } else if (dp) {
        snprintf(host, hl, "%.*s", (int)(dp - s), s);
        *port = atoi(dp + 1);
    } else {
        snprintf(host, hl, "0.0.0.0");
        *port = atoi(s);
    }
    return *port > 0 && *port < 65536 ? 0 : -1;
}

#define MAXARGS 64
#define ERR(...)                                                     \
    do {                                                             \
        char _m[300];                                                \
        snprintf(_m, sizeof _m, __VA_ARGS__);                        \
        snprintf(err, errlen, "%s:%d: %s", nombre, lx.linea, _m);    \
        goto fallo;                                                  \
    } while (0)

int conf_load(const char* path, bw_conf* c, char* err, size_t errlen) {
    const char* nombre = path;
    conf_defaults(c);
    bw_blob* texto = fs_read_all(path, 1u << 20);
    if (!texto) {
        snprintf(err, errlen, "no se pudo leer %s", path);
        return -1;
    }
    lexer lx = {texto->data, 1};
    char args[MAXARGS][1024];
    bw_location* loc = NULL; /* dentro de un bloque location */
    int tipos_propios = 0;
    for (;;) {
        int n = 0, t;
        while ((t = token(&lx, args[n < MAXARGS ? n : MAXARGS - 1], sizeof args[0])) == T_PAL)
            if (n < MAXARGS) n++;
        if (t == T_ERR) ERR("comillas sin cerrar");
        if (t == T_FIN) {
            if (n) ERR("falta ';' al final");
            if (loc) ERR("falta '}'");
            break;
        }
        if (t == T_CIERRA) {
            if (n) ERR("falta ';' antes de '}'");
            if (!loc) ERR("'}' de más");
            if (loc->handler == H_NONE) ERR("location %s sin qué hacer (root, alias, proxy_pass o return)", loc->path);
            loc = NULL;
            continue;
        }
        if (n == 0) ERR("directiva vacía");
        const char* d = args[0];
        if (t == T_ABRE) {
            if (strcmp(d, "location")) ERR("bloque '%s' desconocido (solo location)", d);
            if (loc) ERR("location adentro de otra location");
            if (c->nloc >= BW_MAX_LOC) ERR("demasiadas location");
            loc = &c->loc[c->nloc++];
            memset(loc, 0, sizeof *loc);
            if (n == 3 && !strcmp(args[1], "=")) {
                loc->match = LOC_EXACT;
                snprintf(loc->path, sizeof loc->path, "%s", args[2]);
            } else if (n == 3 && !strcmp(args[1], "^~")) {
                snprintf(loc->path, sizeof loc->path, "%s", args[2]);
            } else if (n == 2) {
                snprintf(loc->path, sizeof loc->path, "%s", args[1]);
            } else {
                ERR("location: «location [=] /ruta {»");
            }
            if (loc->path[0] != '/') ERR("location: la ruta empieza con /");
            continue;
        }
        /* t == T_PYC: una directiva con sus argumentos */
        if (loc) {
            if (!strcmp(d, "root") && n == 2) {
                snprintf(loc->root, sizeof loc->root, "%s", args[1]);
                loc->handler = H_STATIC;
            } else if (!strcmp(d, "alias") && n == 2) {
                snprintf(loc->alias, sizeof loc->alias, "%s", args[1]);
                loc->handler = H_STATIC;
            } else if (!strcmp(d, "alias_file") && n == 2) {
                snprintf(loc->alias_file, sizeof loc->alias_file, "%s", args[1]);
                loc->handler = H_STATIC;
            } else if (!strcmp(d, "index") && n == 2) {
                snprintf(loc->index, sizeof loc->index, "%s", args[1]);
            } else if (!strcmp(d, "try_files")) {
                if (n == 3 && !strcmp(args[1], "$uri") && !strcmp(args[2], "@upstream")) loc->try_upstream = 1;
                else if (n == 3 && !strcmp(args[1], "$uri") && !strcmp(args[2], "=404")) loc->try_upstream = 0;
                else ERR("try_files: solo «$uri @upstream» o «$uri =404»");
            } else if (!strcmp(d, "proxy_pass") && n <= 2) {
                loc->handler = H_PROXY;
            } else if (!strcmp(d, "return") && (n == 2 || n == 3)) {
                loc->handler = H_RETURN;
                loc->ret_code = atoi(args[1]);
                if (loc->ret_code < 200 || loc->ret_code > 599) ERR("return: código inválido");
                if (n == 3) snprintf(loc->ret_text, sizeof loc->ret_text, "%s", args[2]);
            } else if (!strcmp(d, "proxy_cache_valid") && n == 2) {
                if (tiempo_ms(args[1], &loc->cache_valid_ms)) ERR("proxy_cache_valid: tiempo inválido");
            } else if (!strcmp(d, "proxy_cache_key") && n == 2) {
                if (!strcmp(args[1], "$uri")) loc->cache_key_uri = 1;
                else if (!strcmp(args[1], "$request_uri")) loc->cache_key_uri = 0;
                else ERR("proxy_cache_key: $uri o $request_uri");
            } else if (!strcmp(d, "ba_vistos") && n == 2) {
                snprintf(loc->vistos, sizeof loc->vistos, "%s", args[1]);
            } else if (!strcmp(d, "add_header") && n >= 3) {
                if (loc->nhdr >= BW_MAX_HDR) ERR("demasiados add_header");
                char v[1024] = "";
                for (int i = 2; i < n; i++) {
                    if (i > 2) strncat(v, " ", sizeof v - strlen(v) - 1);
                    strncat(v, args[i], sizeof v - strlen(v) - 1);
                }
                if (strpbrk(args[1], "\r\n:") || strpbrk(v, "\r\n")) ERR("add_header: cabecera inválida");
                snprintf(loc->hdr[loc->nhdr++], sizeof loc->hdr[0], "%s: %s", args[1], v);
            } else {
                ERR("directiva '%s' desconocida (o mal usada) adentro de location", d);
            }
            continue;
        }
        if (!strcmp(d, "worker_processes") && n == 2) {
            c->workers = !strcmp(args[1], "auto") ? 0 : atoi(args[1]);
            if (c->workers < 0 || c->workers > 64) ERR("worker_processes: auto o 1-64");
        } else if (!strcmp(d, "worker_connections") && n == 2) {
            c->worker_connections = atoi(args[1]);
            if (c->worker_connections < 16) ERR("worker_connections: al menos 16");
        } else if (!strcmp(d, "keepalive_timeout") && n == 2) {
            if (tiempo_ms(args[1], &c->keepalive_timeout_ms)) ERR("keepalive_timeout: tiempo inválido");
        } else if (!strcmp(d, "keepalive_requests") && n == 2) {
            c->keepalive_requests = atoi(args[1]);
        } else if (!strcmp(d, "client_header_timeout") && n == 2) {
            if (tiempo_ms(args[1], &c->header_timeout_ms)) ERR("client_header_timeout: tiempo inválido");
        } else if (!strcmp(d, "send_timeout") && n == 2) {
            if (tiempo_ms(args[1], &c->send_timeout_ms)) ERR("send_timeout: tiempo inválido");
        } else if (!strcmp(d, "proxy_connect_timeout") && n == 2) {
            if (tiempo_ms(args[1], &c->proxy_connect_ms)) ERR("proxy_connect_timeout: tiempo inválido");
        } else if (!strcmp(d, "proxy_read_timeout") && n == 2) {
            if (tiempo_ms(args[1], &c->proxy_read_ms)) ERR("proxy_read_timeout: tiempo inválido");
        } else if (!strcmp(d, "proxy_stream_timeout") && n == 2) {
            if (tiempo_ms(args[1], &c->proxy_stream_ms)) ERR("proxy_stream_timeout: tiempo inválido");
        } else if (!strcmp(d, "gzip") && n == 2) {
            if (onoff(args[1], &c->gzip)) ERR("gzip: on u off");
        } else if (!strcmp(d, "gzip_comp_level") && n == 2) {
            c->gzip_level = atoi(args[1]);
            if (c->gzip_level < 1 || c->gzip_level > 12) ERR("gzip_comp_level: 1-12");
        } else if (!strcmp(d, "gzip_min_length") && n == 2) {
            size_t v = 0;
            if (tamano(args[1], &v)) ERR("gzip_min_length: tamaño inválido");
            c->gzip_min = (int)v;
        } else if (!strcmp(d, "gzip_types") && n >= 2) {
            if (!tipos_propios) c->ntypes = 0;
            tipos_propios = 1;
            for (int i = 1; i < n && c->ntypes < BW_MAX_TYPES; i++)
                snprintf(c->types[c->ntypes++], sizeof c->types[0], "%s", args[i]);
        } else if (!strcmp(d, "open_file_cache_max") && n == 2) {
            if (tamano(args[1], &c->file_cache_max)) ERR("open_file_cache_max: tamaño inválido");
        } else if (!strcmp(d, "open_file_cache_total") && n == 2) {
            if (tamano(args[1], &c->file_cache_total)) ERR("open_file_cache_total: tamaño inválido");
        } else if (!strcmp(d, "proxy_buffer_max") && n == 2) {
            if (tamano(args[1], &c->proxy_buffer_max)) ERR("proxy_buffer_max: tamaño inválido");
        } else if (!strcmp(d, "listen") && (n == 2 || n == 3)) {
            if (c->nlisten >= BW_MAX_LISTEN) ERR("demasiados listen");
            bw_listen* l = &c->listen[c->nlisten++];
            memset(l, 0, sizeof *l);
            if (host_puerto(args[1], l->host, sizeof l->host, &l->port)) ERR("listen: dirección inválida");
            if (n == 3) {
                if (strcmp(args[2], "proxy_protocol")) ERR("listen: solo «proxy_protocol» como opción");
                l->proxy_protocol = 1;
            }
        } else if (!strcmp(d, "upstream") && n == 2) {
            if (host_puerto(args[1], c->upstream_host, sizeof c->upstream_host, &c->upstream_port))
                ERR("upstream: dirección inválida");
        } else if (!strcmp(d, "error_log") && n == 2) {
            snprintf(c->error_log, sizeof c->error_log, "%s", strcmp(args[1], "stderr") ? args[1] : "");
        } else if (!strcmp(d, "access_log") && n == 2) {
            snprintf(c->access_log, sizeof c->access_log, "%s", strcmp(args[1], "off") ? args[1] : "");
        } else {
            ERR("directiva '%s' desconocida (o mal usada)", d);
        }
    }
    blob_unref(texto);
    if (c->nlisten == 0) {
        snprintf(err, errlen, "%s: falta «listen»", nombre);
        return -1;
    }
    for (int i = 0; i < c->nloc; i++)
        if ((c->loc[i].handler == H_PROXY || c->loc[i].try_upstream) && c->upstream_port == 0) {
            snprintf(err, errlen, "%s: location %s usa el upstream y falta «upstream»", nombre, c->loc[i].path);
            return -1;
        }
    return 0;
fallo:
    blob_unref(texto);
    return -1;
}

const bw_location* conf_match(const bw_conf* c, const char* uri) {
    const bw_location* mejor = NULL;
    size_t largo = 0;
    for (int i = 0; i < c->nloc; i++) {
        const bw_location* l = &c->loc[i];
        if (l->match == LOC_EXACT) {
            if (!strcmp(l->path, uri)) return l;
            continue;
        }
        const size_t n = strlen(l->path);
        if (n >= largo && !strncmp(l->path, uri, n)) {
            mejor = l;
            largo = n;
        }
    }
    return mejor;
}
