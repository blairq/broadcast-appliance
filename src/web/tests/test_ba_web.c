/*
 * test_ba_web: arranca ba-web de verdad (master + 2 workers) contra un gateway de
 * mentira y le pega como un navegador: estáticos, gzip (RFC 9110), condicionales,
 * rangos, HEAD, keep-alive y pipelining, archivos grandes, rutas maliciosas, el respaldo
 * al gateway, el proxy con gzip, el microcaché, PROXY v1, WebSocket, POST, SSE y pedidos
 * mal formados. Lo comprimido se descomprime y se compara con el original.
 */
#include "bw.h"

#include <libdeflate.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#else
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

static int g_fallos = 0;
#define CHECK(c, msg)                                          \
    do {                                                       \
        if (!(c)) {                                            \
            fprintf(stderr, "FALLA: %s (línea %d)\n", msg, __LINE__); \
            g_fallos++;                                        \
        }                                                      \
    } while (0)

/* ------------------------------------------------------------------ gateway de mentira */
static int g_up_port = 0;
static volatile int g_api_hits = 0;
static char g_ultima_proxy[200];
static char g_ultimos_vistos[600];

static int leer_cabecera(bw_sock s, char* buf, int cap) {
    int n = 0;
    while (n < cap - 1) {
        const int r = (int)recv(s, buf + n, cap - 1 - n, 0);
        if (r <= 0) return -1;
        n += r;
        buf[n] = 0;
        if (strstr(buf, "\r\n\r\n")) return n;
    }
    return -1;
}

static void enviar_todo(bw_sock s, const char* p, size_t n) {
    while (n) {
        const int r = (int)send(s, p, (int)n, 0);
        if (r <= 0) return;
        p += r;
        n -= (size_t)r;
    }
}

static void atender_up(bw_sock s) {
    char buf[70000];
    int n = leer_cabecera(s, buf, sizeof buf);
    if (n < 0) {
        sock_close(s);
        return;
    }
    char* req = buf;
    if (!strncmp(req, "PROXY ", 6)) {
        char* eol = strstr(req, "\r\n");
        snprintf(g_ultima_proxy, sizeof g_ultima_proxy, "%.*s", (int)(eol - req), req);
        req = eol + 2;
    }
    char metodo[16] = "", path[1024] = "";
    sscanf(req, "%15s %1023s", metodo, path);
    char resp[8192];
    if (!strncmp(path, "/api/v1/evento", 14)) {
        g_api_hits++;
        const char* v = strstr(req, "X-BA-Vistos: ");
        if (v) snprintf(g_ultimos_vistos, sizeof g_ultimos_vistos, "%.*s", (int)strcspn(v + 13, "\r"), v + 13);
        char cuerpo[3000];
        int k = snprintf(cuerpo, sizeof cuerpo, "{\"activo\":true,\"hits\":%d,\"relleno\":\"", g_api_hits);
        for (int i = 0; i < 2000; i++) cuerpo[k++] = "abcdefgh"[i % 8];
        k += snprintf(cuerpo + k, sizeof cuerpo - (size_t)k, "\"}");
        n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-cache\r\n"
                     "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                     k, cuerpo);
        enviar_todo(s, resp, (size_t)n);
    } else if (!strcmp(path, "/up/html")) {
        char cuerpo[4000];
        int k = snprintf(cuerpo, sizeof cuerpo, "<!doctype html><title>gateway</title>");
        while (k < 3000) k += snprintf(cuerpo + k, sizeof cuerpo - (size_t)k, "<p>Hola desde el gateway.</p>");
        n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nETag: \"abc\"\r\n"
                     "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                     k, cuerpo);
        enviar_todo(s, resp, (size_t)n);
    } else if (!strcmp(path, "/ws")) {
        const char* r101 = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
        enviar_todo(s, r101, strlen(r101));
        char eco[256];
        const int m = (int)recv(s, eco, sizeof eco, 0);
        if (m > 0) enviar_todo(s, eco, (size_t)m);
        bw_sleep_ms(50);
    } else if (!strcmp(path, "/sse")) {
        const char* h = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n";
        enviar_todo(s, h, strlen(h));
        for (int i = 0; i < 3; i++) {
            char ev[64];
            const int m = snprintf(ev, sizeof ev, "data: %d\n\n", i);
            enviar_todo(s, ev, (size_t)m);
            bw_sleep_ms(30);
        }
    } else if (!strcmp(metodo, "POST")) {
        const char* cl = strstr(req, "Content-Length: ");
        const int largo = cl ? atoi(cl + 16) : 0;
        char* cuerpo_ini = strstr(req, "\r\n\r\n") + 4;
        int ya = (int)(buf + n - cuerpo_ini);
        char cuerpo[4096];
        memcpy(cuerpo, cuerpo_ini, (size_t)ya);
        while (ya < largo) {
            const int r = (int)recv(s, cuerpo + ya, largo - ya, 0);
            if (r <= 0) break;
            ya += r;
        }
        n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", ya);
        enviar_todo(s, resp, (size_t)n);
        enviar_todo(s, cuerpo, (size_t)ya);
    } else {
        char cuerpo[1200];
        const int k = snprintf(cuerpo, sizeof cuerpo, "upstream:%s", path);
        n = snprintf(resp, sizeof resp,
                     "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                     k, cuerpo);
        enviar_todo(s, resp, (size_t)n);
    }
    sock_close(s);
}

#ifdef _WIN32
static unsigned __stdcall hilo_conn(void* a) {
    atender_up((bw_sock)(uintptr_t)a);
    return 0;
}
#else
static void* hilo_conn(void* a) {
    atender_up((bw_sock)(intptr_t)a);
    return NULL;
}
#endif

static bw_sock g_up_ls;
#ifdef _WIN32
static unsigned __stdcall hilo_up(void* a) {
#else
static void* hilo_up(void* a) {
#endif
    (void)a;
    for (;;) {
        bw_sock s = accept(g_up_ls, NULL, NULL);
        if (s == BW_BAD) {
            bw_sleep_ms(5);
            continue;
        }
#ifdef _WIN32
        u_long cero = 0;
        ioctlsocket(s, FIONBIO, &cero);
        _beginthreadex(NULL, 0, hilo_conn, (void*)(uintptr_t)s, 0, NULL);
#else
        int fl = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, fl & ~O_NONBLOCK);
        pthread_t t;
        pthread_create(&t, NULL, hilo_conn, (void*)(intptr_t)s);
        pthread_detach(t);
#endif
    }
#ifndef _WIN32
    return NULL;
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------ cliente */
static int g_port = 0;

static bw_sock conectar(void) {
    bw_sock s = sock_connect("127.0.0.1", g_port);
    if (s == BW_BAD) return s;
#ifdef _WIN32
    u_long cero = 0;
    ioctlsocket(s, FIONBIO, &cero);
    DWORD ms = 5000; /* que un fallo no deje la prueba trabada para siempre */
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl & ~O_NONBLOCK);
    struct timeval tv = {5, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    for (int i = 0; i < 200; i++) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        struct timeval t = {0, 20000};
        if (select((int)s + 1, NULL, &w, NULL, &t) > 0) break;
    }
    return s;
}

typedef struct resp {
    int status;
    char head[8192];
    bw_buf body;
} resp;

static const char* hdr(const resp* r, const char* nombre, char* out, size_t outlen) {
    char clave[128];
    snprintf(clave, sizeof clave, "\r\n%s:", nombre);
    const char* p = r->head;
    const char* h = NULL;
    for (const char* q = p; (q = strstr(q, "\r\n")); q += 2)
        if (!strncasecmp(q, clave, strlen(clave))) {
            h = q + strlen(clave);
            break;
        }
    if (!h) return NULL;
    while (*h == ' ') h++;
    snprintf(out, outlen, "%.*s", (int)strcspn(h, "\r"), h);
    return out;
}

/* Lee una respuesta (Content-Length o hasta que cierre). `pend` guarda lo que sobró. */
static bw_buf g_pend;
static int leer_resp(bw_sock s, resp* r, int es_head) {
    memset(r, 0, sizeof *r);
    for (;;) {
        char* fin = g_pend.p ? strstr(g_pend.p, "\r\n\r\n") : NULL;
        if (fin) {
            const size_t hl = (size_t)(fin - g_pend.p) + 4;
            snprintf(r->head, sizeof r->head, "%.*s", (int)hl, g_pend.p);
            buf_consume(&g_pend, hl);
            break;
        }
        char tmp[65536];
        const int n = (int)recv(s, tmp, sizeof tmp, 0);
        if (n <= 0) return -1;
        buf_add(&g_pend, tmp, (size_t)n);
    }
    sscanf(r->head, "HTTP/1.%*d %d", &r->status);
    char v[64];
    long long cl = -1;
    if (hdr(r, "Content-Length", v, sizeof v)) cl = atoll(v);
    if (es_head || r->status == 304 || r->status == 204 || r->status == 101) cl = 0;
    if (cl >= 0) {
        while ((long long)g_pend.len < cl) {
            char tmp[65536];
            const int n = (int)recv(s, tmp, sizeof tmp, 0);
            if (n <= 0) return -1;
            buf_add(&g_pend, tmp, (size_t)n);
        }
        buf_add(&r->body, g_pend.p, (size_t)cl);
        buf_consume(&g_pend, (size_t)cl);
    } else {
        buf_add(&r->body, g_pend.p ? g_pend.p : "", g_pend.len);
        g_pend.len = 0;
        char tmp[65536];
        int n;
        while ((n = (int)recv(s, tmp, sizeof tmp, 0)) > 0) buf_add(&r->body, tmp, (size_t)n);
    }
    return 0;
}

static int pedir(const char* crudo, resp* r) {
    bw_sock s = conectar();
    if (s == BW_BAD) return -1;
    g_pend.len = 0;
    enviar_todo(s, crudo, strlen(crudo));
    const int k = leer_resp(s, r, !strncmp(crudo, "HEAD ", 5));
    sock_close(s);
    return k;
}

static bw_blob* gunzip(const bw_buf* b, size_t esperado) {
    struct libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    bw_blob* out = blob_new(esperado + 16);
    size_t real = 0;
    const enum libdeflate_result k = libdeflate_gzip_decompress(d, b->p, b->len, out->data, esperado + 16, &real);
    libdeflate_free_decompressor(d);
    if (k != LIBDEFLATE_SUCCESS) {
        blob_unref(out);
        return NULL;
    }
    out->len = real;
    return out;
}

/* ------------------------------------------------------------------ armado */
#ifdef _WIN32
/* En Windows, fopen y CreateDirectoryA toman los bytes como ANSI: una «é» en UTF-8 crearía
   otro nombre. Con la API de UTF-16, el nombre es el mismo que pide el navegador. */
static wchar_t* ancho(const char* s) {
    static wchar_t w[2048];
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 2048);
    return w;
}
#define CREAR_DIR(p) CreateDirectoryW(ancho(p), NULL)
#endif

static void escribir(const char* path, const char* datos, size_t n) {
#ifdef _WIN32
    FILE* f = _wfopen(ancho(path), L"wb");
#else
    FILE* f = fopen(path, "wb");
#endif
    fwrite(datos, 1, n, f);
    fclose(f);
}

static int puerto_libre(void) {
    char err[100];
    bw_sock s = sock_listen("127.0.0.1", 0, err, sizeof err);
    struct sockaddr_in sa;
    socklen_t len = sizeof sa;
    getsockname(s, (struct sockaddr*)&sa, &len);
    const int p = ntohs(sa.sin_port);
    sock_close(s);
    return p;
}

int main(void) {
    sock_init();
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    char dir[512];
#ifdef _WIN32
    snprintf(dir, sizeof dir, "%s\\ba-web-test-%lu", getenv("TEMP"), (unsigned long)GetCurrentProcessId());
    CREAR_DIR(dir);
#else
    snprintf(dir, sizeof dir, "/tmp/ba-web-test-%d", (int)getpid());
    mkdir(dir, 0755);
#endif
    char p[1024];
    snprintf(p, sizeof p, "%s/s", dir);
#ifdef _WIN32
    CREAR_DIR(p);
    snprintf(p, sizeof p, "%s/s/sub", dir);
    CREAR_DIR(p);
    snprintf(p, sizeof p, "%s/s/Armée", dir);
    CREAR_DIR(p);
#else
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/s/sub", dir);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/s/Armée", dir);
    mkdir(p, 0755);
#endif
    /* un html que se comprime bien */
    bw_buf html = {0};
    buf_adds(&html, "<!doctype html><html><head><title>Prueba</title></head><body>");
    for (int i = 0; i < 300; i++) buf_printf(&html, "<p>Párrafo %d de la página del evento.</p>\n", i);
    buf_adds(&html, "</body></html>");
    snprintf(p, sizeof p, "%s/s/index.html", dir);
    escribir(p, html.p, html.len);
    snprintf(p, sizeof p, "%s/s/Armée/página.html", dir);
    escribir(p, "<p>acentos</p>", 14);
    /* uno grande (va por sendfile, sin caché en memoria) */
    const size_t grande_n = 3u << 20;
    char* grande = (char*)malloc(grande_n);
    for (size_t i = 0; i < grande_n; i++) grande[i] = (char)((i * 2654435761u) >> 13);
    snprintf(p, sizeof p, "%s/s/video.mp4", dir);
    escribir(p, grande, grande_n);
    /* el alias que sale de un archivo */
    char alias_file[1024];
    snprintf(alias_file, sizeof alias_file, "%s/alias.dir", dir);
    snprintf(p, sizeof p, "%s/s\n", dir);
    escribir(alias_file, p, strlen(p));

    /* el gateway de mentira */
    char err[200];
    g_up_port = puerto_libre();
    g_up_ls = sock_listen("127.0.0.1", g_up_port, err, sizeof err);
#ifdef _WIN32
    _beginthreadex(NULL, 0, hilo_up, NULL, 0, NULL);
#else
    pthread_t t;
    pthread_create(&t, NULL, hilo_up, NULL);
#endif
    g_port = puerto_libre();
    char conf[1024];
    snprintf(conf, sizeof conf, "%s/ba-web.conf", dir);
    bw_buf cf = {0};
    buf_printf(&cf,
               "worker_processes 2;\nlisten 127.0.0.1:%d proxy_protocol;\nupstream 127.0.0.1:%d;\n"
               "gzip on; gzip_comp_level 6;\nopen_file_cache_max 1m;\n"
               "location /s/ { alias %s/s/; index index.html; try_files $uri @upstream; add_header Cache-Control no-cache; }\n"
               "location /f/ { alias_file %s; try_files $uri =404; }\n"
               "location = /api/v1/evento { proxy_pass; proxy_cache_valid 2s; proxy_cache_key $uri; ba_vistos v; }\n"
               "location = /r { return 301 /s/; }\n"
               "location / { proxy_pass; }\n",
               g_port, g_up_port, dir, alias_file);
    escribir(conf, cf.p, cf.len);

    /* ba-web -t y después de verdad (BA_WEB_EXE en el entorno manda: para correr la prueba en
       otra máquina que la que compiló, p. ej. la PC con Windows) */
    const char* exe = getenv("BA_WEB_EXE") && *getenv("BA_WEB_EXE") ? getenv("BA_WEB_EXE") : BA_WEB_EXE;
#ifdef _WIN32
    _putenv("BA_WEB_CLAVE=clave-de-prueba");
    char linea[2048];
    snprintf(linea, sizeof linea, "\"%s\" -c \"%s\"", exe, conf);
    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    CHECK(CreateProcessA(NULL, linea, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi), "lanzar ba-web");
#else
    setenv("BA_WEB_CLAVE", "clave-de-prueba", 1);
    char* targ[] = {(char*)exe, (char*)"-t", (char*)"-c", conf, NULL};
    pid_t pid;
    int st = 0;
    posix_spawn(&pid, exe, NULL, NULL, targ, environ);
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "ba-web -t acepta la configuración");
    char* args[] = {(char*)exe, (char*)"-c", conf, NULL};
    posix_spawn(&pid, exe, NULL, NULL, args, environ);
#endif
    resp r;
    char v[256], q[4096];
    int listo = 0;
    for (int i = 0; i < 100 && !listo; i++) {
        bw_sleep_ms(50);
        bw_sock s = sock_connect("127.0.0.1", g_port);
        if (s != BW_BAD) {
            bw_sleep_ms(20);
            listo = pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0;
            sock_close(s);
        }
    }
    CHECK(listo, "ba-web atiende");

    /* --- gzip (RFC 9110 §12.5.3) --- */
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip, deflate, br\r\n\r\n", &r) == 0, "gzip: pedido");
    CHECK(r.status == 200, "gzip: 200");
    CHECK(hdr(&r, "Content-Encoding", v, sizeof v) && !strcmp(v, "gzip"), "gzip: Content-Encoding");
    CHECK(hdr(&r, "Vary", v, sizeof v) && strstr(v, "Accept-Encoding"), "gzip: Vary");
    CHECK(hdr(&r, "ETag", v, sizeof v) && strstr(v, "-gz\""), "gzip: ETag propio de lo comprimido");
    CHECK(!hdr(&r, "Accept-Ranges", v, sizeof v), "gzip: sin Accept-Ranges");
    CHECK(hdr(&r, "Cache-Control", v, sizeof v) && !strcmp(v, "no-cache"), "add_header");
    CHECK(r.body.len < html.len / 3, "gzip: comprime");
    bw_blob* d = gunzip(&r.body, html.len);
    CHECK(d && d->len == html.len && !memcmp(d->data, html.p, html.len), "gzip: descomprime igual al original");
    blob_unref(d);
    char etag_gz[128];
    hdr(&r, "ETag", etag_gz, sizeof etag_gz);

    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200, "sin AE: 200");
    CHECK(!hdr(&r, "Content-Encoding", v, sizeof v), "sin AE: sin comprimir");
    CHECK(r.body.len == html.len && !memcmp(r.body.p, html.p, html.len), "sin AE: el archivo tal cual");
    CHECK(hdr(&r, "Vary", v, sizeof v), "sin AE: Vary igual (para los cachés)");
    CHECK(hdr(&r, "Accept-Ranges", v, sizeof v), "sin AE: Accept-Ranges");
    char etag[128];
    hdr(&r, "ETag", etag, sizeof etag);
    CHECK(strcmp(etag, etag_gz) != 0, "ETag distinto para cada versión");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip;q=0, deflate\r\n\r\n", &r) == 0 &&
              !hdr(&r, "Content-Encoding", v, sizeof v), "gzip;q=0: sin comprimir");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: *\r\n\r\n", &r) == 0 &&
              hdr(&r, "Content-Encoding", v, sizeof v), "*: comprime");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: *;q=0\r\n\r\n", &r) == 0 &&
              !hdr(&r, "Content-Encoding", v, sizeof v), "*;q=0: sin comprimir");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: x-gzip\r\n\r\n", &r) == 0 &&
              hdr(&r, "Content-Encoding", v, sizeof v), "x-gzip: comprime");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: GZIP;Q=0.5\r\n\r\n", &r) == 0 &&
              hdr(&r, "Content-Encoding", v, sizeof v), "GZIP;Q=0.5: comprime (sin mayúsculas)");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: br, identity\r\n\r\n", &r) == 0 &&
              !hdr(&r, "Content-Encoding", v, sizeof v), "br: sin comprimir");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip;q=0.000\r\n\r\n", &r) == 0 &&
              !hdr(&r, "Content-Encoding", v, sizeof v), "gzip;q=0.000: sin comprimir");

    /* --- condicionales --- */
    snprintf(q, sizeof q, "GET /s/index.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n\r\n", etag);
    CHECK(pedir(q, &r) == 0 && r.status == 304 && r.body.len == 0, "If-None-Match: 304");
    snprintf(q, sizeof q, "GET /s/index.html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\nIf-None-Match: \"otro\", W/%s\r\n\r\n", etag_gz);
    CHECK(pedir(q, &r) == 0 && r.status == 304, "If-None-Match débil en una lista: 304");
    snprintf(q, sizeof q, "GET /s/index.html HTTP/1.1\r\nHost: x\r\nIf-None-Match: %s\r\n\r\n", etag_gz);
    CHECK(pedir(q, &r) == 0 && r.status == 200, "If-None-Match de la otra versión: 200");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Fri, 31 Dec 2100 23:59:59 GMT\r\n\r\n", &r) == 0 &&
              r.status == 304, "If-Modified-Since: 304");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nIf-Modified-Since: Thu, 01 Jan 1970 00:00:01 GMT\r\n\r\n", &r) == 0 &&
              r.status == 200, "If-Modified-Since viejo: 200");

    /* --- rangos --- */
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nRange: bytes=0-9\r\nAccept-Encoding: gzip\r\n\r\n", &r) == 0 &&
              r.status == 206 && r.body.len == 10 && !memcmp(r.body.p, html.p, 10), "Range 0-9: 206, 10 bytes, sin gzip");
    snprintf(q, sizeof q, "bytes 0-9/%zu", html.len);
    CHECK(hdr(&r, "Content-Range", v, sizeof v) && !strcmp(v, q), "Content-Range");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nRange: bytes=-5\r\n\r\n", &r) == 0 && r.status == 206 &&
              r.body.len == 5 && !memcmp(r.body.p, html.p + html.len - 5, 5), "Range -5: los últimos 5");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nRange: bytes=99999999-\r\n\r\n", &r) == 0 && r.status == 416,
          "Range fuera: 416");
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\nRange: bytes=0-1,5-6\r\n\r\n", &r) == 0 && r.status == 200,
          "varios rangos: se manda todo");
    snprintf(q, sizeof q, "GET /s/index.html HTTP/1.1\r\nHost: x\r\nRange: bytes=0-9\r\nIf-Range: \"viejo\"\r\n\r\n");
    CHECK(pedir(q, &r) == 0 && r.status == 200, "If-Range que no coincide: todo");
    snprintf(q, sizeof q, "GET /s/index.html HTTP/1.1\r\nHost: x\r\nRange: bytes=0-9\r\nIf-Range: %s\r\n\r\n", etag);
    CHECK(pedir(q, &r) == 0 && r.status == 206, "If-Range que coincide: 206");

    /* --- grande (sendfile), con rango --- */
    CHECK(pedir("GET /s/video.mp4 HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n\r\n", &r) == 0 && r.status == 200 &&
              r.body.len == grande_n && !memcmp(r.body.p, grande, grande_n), "archivo grande entero");
    CHECK(hdr(&r, "Content-Type", v, sizeof v) && !strcmp(v, "video/mp4"), "video/mp4");
    CHECK(pedir("GET /s/video.mp4 HTTP/1.1\r\nHost: x\r\nRange: bytes=1000000-1000999\r\n\r\n", &r) == 0 &&
              r.status == 206 && r.body.len == 1000 && !memcmp(r.body.p, grande + 1000000, 1000), "rango del grande");

    /* --- HEAD, keep-alive y pipelining --- */
    {
        bw_sock s = conectar();
        g_pend.len = 0;
        const char* dos = "HEAD /s/index.html HTTP/1.1\r\nHost: x\r\n\r\nGET /s/index.html HTTP/1.1\r\nHost: x\r\n\r\n";
        enviar_todo(s, dos, strlen(dos));
        CHECK(leer_resp(s, &r, 1) == 0 && r.status == 200 && r.body.len == 0, "HEAD: sin cuerpo");
        snprintf(q, sizeof q, "%zu", html.len);
        CHECK(hdr(&r, "Content-Length", v, sizeof v) && !strcmp(v, q), "HEAD: Content-Length del GET");
        CHECK(leer_resp(s, &r, 0) == 0 && r.status == 200 && r.body.len == html.len, "pipelining: el segundo");
        const char* tres = "GET /up/html HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n\r\n";
        enviar_todo(s, tres, strlen(tres));
        CHECK(leer_resp(s, &r, 0) == 0 && r.status == 200, "keep-alive: un proxy en la misma conexión");
        CHECK(hdr(&r, "Content-Encoding", v, sizeof v), "proxy: comprime lo del gateway");
        CHECK(hdr(&r, "ETag", v, sizeof v) && !strcmp(v, "\"abc-gz\""), "proxy: ETag de lo comprimido");
        d = gunzip(&r.body, 8000);
        CHECK(d && strstr(d->data, "Hola desde el gateway"), "proxy: descomprime");
        blob_unref(d);
        const char* cuatro = "GET /s/index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
        enviar_todo(s, cuatro, strlen(cuatro));
        CHECK(leer_resp(s, &r, 0) == 0 && r.status == 200, "y otro más después del proxy");
        CHECK(hdr(&r, "Connection", v, sizeof v) && !strcmp(v, "close"), "Connection: close");
        sock_close(s);
    }
    CHECK(pedir("GET /s/index.html HTTP/1.0\r\n\r\n", &r) == 0 && r.status == 200, "HTTP/1.0 sin Host");

    /* --- rutas --- */
    CHECK(pedir("GET /s/../../etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 400, "../ : 400");
    CHECK(pedir("GET /s/%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 400, "%2e%2e: 400");
    CHECK(pedir("GET /s/x%00.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 400, "%00: 400");
    CHECK(pedir("GET /s/sub HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 301 &&
              hdr(&r, "Location", v, sizeof v) && !strcmp(v, "/s/sub/"), "directorio sin barra: 301");
    CHECK(pedir("GET /s/./index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200, "/./: normaliza");
    CHECK(pedir("GET /s//index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200, "//: normaliza");
    CHECK(pedir("GET /s/Arm%C3%A9e/p%C3%A1gina.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200 &&
              r.body.len == 14, "acentos en la ruta (UTF-8)");
    CHECK(pedir("GET /s/ HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200 && r.body.len == html.len, "index");
    CHECK(pedir("GET /f/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200, "alias_file");
    CHECK(pedir("GET /f/nada.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 404, "alias_file: =404");
    CHECK(pedir("GET /s/noesta.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 404 &&
              strstr(r.body.p, "upstream:/s/noesta.html"), "try_files: al gateway");
    CHECK(pedir("GET /r HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 301 && hdr(&r, "Location", v, sizeof v) &&
              !strcmp(v, "/s/"), "return 301");
    CHECK(pedir("DELETE /f/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 405, "estático: 405");

    /* --- pedidos mal formados --- */
    CHECK(pedir("GET / HTTP/1.1\r\n\r\n", &r) == 0 && r.status == 400, "1.1 sin Host: 400");
    CHECK(pedir("GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", &r) == 0 && r.status == 400, "dos Host: 400");
    CHECK(pedir("POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc", &r) == 0 &&
              r.status == 400, "CL y TE juntos: 400");
    CHECK(pedir("GET / HTTP/2.0\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 505, "HTTP/2.0 en texto: 505");
    CHECK(pedir("GET / HTTP/1.1\r\nHost: x\r\n Doblada: si\r\n\r\n", &r) == 0 && r.status == 400, "obs-fold: 400");
    CHECK(pedir("GET / HTTP/1.1\r\nHost : x\r\n\r\n", &r) == 0 && r.status == 400, "«Nombre :»: 400");
    CHECK(pedir("\r\n\r\nGET /s/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200,
          "líneas vacías antes del pedido: se ignoran");
    CHECK(pedir("GET /s/index.html HTTP/1.1\nHost: x\n\n", &r) == 0 && r.status == 400,
          "LF solos: 400 (el gateway lee CRLF: si no, entenderían distinto el pedido)");

    /* --- microcaché --- */
    const int antes = g_api_hits;
    for (int i = 0; i < 30; i++) {
        snprintf(q, sizeof q, "GET /api/v1/evento?v=mirada%d HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\n\r\n", i);
        CHECK(pedir(q, &r) == 0 && r.status == 200, "api: 200");
    }
    CHECK(g_api_hits - antes <= 2, "microcaché: al gateway como mucho una vez por worker");
    CHECK(hdr(&r, "Content-Encoding", v, sizeof v), "microcaché: gzip");
    CHECK(hdr(&r, "Cache-Control", v, sizeof v) && !strcmp(v, "no-cache"), "microcaché: cabeceras del gateway");
    d = gunzip(&r.body, 4000);
    CHECK(d && strstr(d->data, "\"activo\":true"), "microcaché: descomprime");
    blob_unref(d);
    hdr(&r, "ETag", v, sizeof v);
    snprintf(q, sizeof q, "GET /api/v1/evento HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip\r\nIf-None-Match: %s\r\n\r\n", v);
    resp r2;
    CHECK(pedir(q, &r2) == 0 && (r2.status == 304 || r2.status == 200), "microcaché: If-None-Match");
    bw_sleep_ms(2200);
    for (int i = 0; i < 4; i++) pedir("GET /api/v1/evento?v=otra HTTP/1.1\r\nHost: x\r\n\r\n", &r);
    bw_sleep_ms(300);
    CHECK(strstr(g_ultimos_vistos, ",") || strlen(g_ultimos_vistos) == 8, "ba_vistos: los ids van al gateway");

    /* --- PROXY v1 --- */
    g_ultima_proxy[0] = 0;
    CHECK(pedir("PROXY TCP4 203.0.113.7 127.0.0.1 5555 80\r\nGET /up/html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 &&
              r.status == 200, "PROXY v1: atiende");
    CHECK(strstr(g_ultima_proxy, "203.0.113.7") != NULL, "PROXY v1: la IP llega al gateway");
    CHECK(pedir("PROXY TCP4 203.0.113.7 127.0.0.1 5555 80\r\nGET /s/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 &&
              r.status == 200, "PROXY v1 en un estático");

    /* --- túneles --- */
    {
        bw_sock s = conectar();
        g_pend.len = 0;
        const char* ws = "GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
        enviar_todo(s, ws, strlen(ws));
        char b[512] = "";
        int n = 0;
        while (!strstr(b, "\r\n\r\n") && n < (int)sizeof b - 1) {
            const int k = (int)recv(s, b + n, (int)sizeof b - 1 - n, 0);
            if (k <= 0) break;
            n += k;
            b[n] = 0;
        }
        CHECK(!strncmp(b, "HTTP/1.1 101", 12), "WebSocket: 101");
        enviar_todo(s, "hola", 4);
        char e[16] = "";
        const char* resto = strstr(b, "\r\n\r\n") + 4;
        int m = (int)strlen(resto);
        memcpy(e, resto, (size_t)m);
        while (m < 4) {
            const int k = (int)recv(s, e + m, 4 - m, 0);
            if (k <= 0) break;
            m += k;
        }
        CHECK(!memcmp(e, "hola", 4), "WebSocket: eco por el túnel");
        sock_close(s);
    }
    CHECK(pedir("POST /eco HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhola gatewa", &r) == 0 && r.status == 200 &&
              r.body.len == 11 && !memcmp(r.body.p, "hola gatewa", 11), "POST: túnel con cuerpo");
    CHECK(pedir("GET /sse HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 200 && strstr(r.body.p, "data: 2"),
          "SSE: llega todo hasta que cierra");
    CHECK(pedir("GET /nada HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && r.status == 404 && strstr(r.body.p, "upstream:/nada"),
          "proxy: un 404 del gateway pasa tal cual");

    /* --- que el archivo cambie se ve en 1 s --- */
    snprintf(p, sizeof p, "%s/s/index.html", dir);
    bw_sleep_ms(1100);
    escribir(p, "<p>nuevo, más largo que antes para que cambie el tamaño</p>", strlen("<p>nuevo, más largo que antes para que cambie el tamaño</p>"));
    bw_sleep_ms(1200);
    CHECK(pedir("GET /s/index.html HTTP/1.1\r\nHost: x\r\n\r\n", &r) == 0 && strstr(r.body.p, "nuevo"),
          "el cambio de un archivo se ve en ~1 s");

#ifdef _WIN32
    TerminateProcess(pi.hProcess, 0);
#else
    kill(pid, SIGTERM);
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "ba-web cierra con SIGTERM");
#endif
    printf(g_fallos ? "test_ba_web: %d fallos\n" : "test_ba_web: OK\n", g_fallos);
    return g_fallos ? 1 : 0;
}
