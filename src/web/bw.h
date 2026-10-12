/*
 * ba-web: el servidor web público del evento (lo que entra por el túnel).
 *
 * Arquitectura de nginx: un MASTER que no atiende a nadie (abre las escuchas, lanza
 * los workers, los vuelve a lanzar si se caen y los cierra) y N WORKERS, cada uno
 * con su bucle de eventos (epoll en Linux, kqueue en macOS, WSAPoll en Windows), todo
 * no bloqueante y de un solo hilo: miles de conexiones por worker sin un hilo por
 * conexión. Un worker que se cuelga o se cae no tumba al resto ni al gateway.
 *
 * Qué hace cada worker:
 *   - Estáticos (root / alias / alias_file) desde un caché en memoria que revalida
 *     con stat como mucho una vez por segundo (open_file_cache), con ETag,
 *     Last-Modified, 304, Range (206/416) y HEAD; los archivos grandes van por
 *     sendfile (o de a pedazos en Windows) sin cargarlos en memoria.
 *   - gzip según RFC 9110: Accept-Encoding con valores q, Content-Encoding, Vary,
 *     un ETag distinto para la versión comprimida, sin Range sobre lo comprimido.
 *     Lo comprimido se guarda: no se comprime en cada pedido.
 *   - Proxy al gateway (upstream): los GET/HEAD se leen completos y se comprimen si
 *     conviene; lo demás (WebSocket, POST, subidas, SSE) pasa como túnel crudo.
 *   - Microcaché (proxy_cache_valid): una respuesta del gateway sirve a todos por un
 *     rato; si venció se sirve la vieja mientras se pide la nueva (como
 *     proxy_cache_use_stale updating) y nunca hay dos pedidos iguales en vuelo.
 *   - PROXY v1: si la conexión llega con la línea PROXY (de ba-pinggy, desde
 *     loopback), se le pasa igual al gateway: la IP del visitante no se pierde.
 *
 * Sin nada del sistema: C11 y libdeflate (vendoreada).
 */
#ifndef BW_H
#define BW_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET bw_sock;
#define BW_BAD INVALID_SOCKET
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#else
#include <strings.h>
typedef int bw_sock;
#define BW_BAD (-1)
#endif

#define BW_VERSION "1.0"

/* ---- Memoria compartida por conteo de referencias (cuerpos del caché) ---- */
typedef struct bw_blob {
    int refs;
    size_t len;
    char data[];
} bw_blob;
bw_blob* blob_new(size_t len);
bw_blob* blob_ref(bw_blob* b);
void blob_unref(bw_blob* b);

/* ---- Buffer que crece ---- */
typedef struct bw_buf {
    char* p;
    size_t len, cap;
} bw_buf;
int buf_reserve(bw_buf* b, size_t extra);
int buf_add(bw_buf* b, const void* d, size_t n);
int buf_adds(bw_buf* b, const char* s);
int buf_printf(bw_buf* b, const char* fmt, ...);
void buf_consume(bw_buf* b, size_t n);
void buf_free(bw_buf* b);

/* ---- Configuración ---- */
#define BW_MAX_LISTEN 8
#define BW_MAX_LOC 64
#define BW_MAX_HDR 16
#define BW_MAX_TYPES 48

enum { LOC_PREFIX = 0, LOC_EXACT = 1 };
enum { H_NONE = 0, H_STATIC, H_PROXY, H_RETURN };

typedef struct bw_location {
    int match;           /* LOC_PREFIX | LOC_EXACT */
    char path[256];
    int handler;         /* H_STATIC | H_PROXY | H_RETURN */
    char root[1024];     /* root: ruta + uri */
    char alias[1024];    /* alias: ruta + (uri sin el prefijo) */
    char alias_file[1024]; /* alias leído de un archivo (se relee si cambia) */
    char index[64];
    int try_upstream;    /* try_files $uri @upstream: si no está, al gateway */
    int ret_code;        /* return 301 /x/ */
    char ret_text[512];
    int cache_valid_ms;  /* proxy_cache_valid: 0 = sin microcaché */
    int cache_key_uri;   /* proxy_cache_key $uri (sin query) */
    char vistos[32];     /* ba_vistos <param>: junta los ids de mirada para el gateway */
    int nhdr;
    char hdr[BW_MAX_HDR][256]; /* add_header: «Nombre: valor» */
} bw_location;

typedef struct bw_listen {
    char host[64];
    int port;
    int proxy_protocol; /* acepta la línea PROXY v1 desde loopback */
    int real_ip_xff;    /* la IP real es la última de X-Forwarded-For (la pone el túnel) */
    int ssl;            /* HTTPS (el certificado llega por la entrada estándar) */
    char up_host[64];   /* upstream=host:puerto de esta escucha ("" = el general) */
    int up_port;
} bw_listen;

typedef struct bw_conf {
    int workers;            /* 0 = auto */
    int worker_connections;
    int keepalive_timeout_ms;
    int keepalive_requests;
    int header_timeout_ms;
    int send_timeout_ms;
    int proxy_connect_ms;
    int proxy_read_ms;
    int proxy_stream_ms;
    int gzip;
    int gzip_level;
    int gzip_min;
    int ntypes;
    char types[BW_MAX_TYPES][64];
    size_t file_cache_max;   /* el archivo más grande que se guarda en memoria */
    size_t file_cache_total; /* tope del caché de archivos por worker */
    size_t proxy_buffer_max; /* lo más grande que se lee completo para comprimir */
    int nlisten;
    bw_listen listen[BW_MAX_LISTEN];
    char upstream_host[64];
    int upstream_port;
    int nloc;
    bw_location loc[BW_MAX_LOC];
    /* Límites contra abusos (por IP real y por worker; con N workers, hasta N veces). */
    int limit_conn_per_ip;     /* conexiones abiertas a la vez */
    int limit_tunnels_per_ip;  /* túneles al gateway (WebSocket, POST…) a la vez */
    double limit_up_rate;      /* pedidos por segundo que llegan al gateway */
    int limit_up_burst;        /* ráfaga permitida */
    int upstream_max_conns;    /* conexiones al gateway a la vez (protege su hilo por conexión) */
    int vistos_per_ip;         /* ids de mirada por IP y por ventana */
    char error_log[1024]; /* "" = stderr */
    char access_log[1024]; /* "" = sin registro */
} bw_conf;

int conf_load(const char* path, bw_conf* c, char* err, size_t errlen);
void conf_defaults(bw_conf* c);
const bw_location* conf_match(const bw_conf* c, const char* uri);

/* ---- Plataforma ---- */
int64_t now_ms(void);
int64_t wall_s(void);
void bw_sleep_ms(int ms);
int sock_init(void);
int sock_nonblock(bw_sock s);
void sock_close(bw_sock s);
int sock_would_block(void);
int sock_in_progress(void);
bw_sock sock_listen(const char* host, int port, char* err, size_t errlen);
bw_sock sock_connect(const char* host, int port);
int ncpu(void);
void http_date(int64_t t, char out[32]);
int64_t http_date_parse(const char* s);

typedef struct bw_stat {
    int exists, is_dir;
    int64_t size, mtime;
} bw_stat;
int fs_stat(const char* path, bw_stat* st);
/* Lee todo el archivo (hasta max bytes). NULL si falla o se pasa. */
bw_blob* fs_read_all(const char* path, size_t max);
/* La ruta real (sin enlaces) para ver que no se sale de la raíz. 0 si falla. */
int fs_realpath(const char* path, char* out, size_t outlen);

/* Un archivo abierto para mandarlo de a pedazos. */
typedef struct bw_file {
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
} bw_file;
int file_open(const char* path, bw_file* f);
void file_close(bw_file* f);
/* Manda hasta n bytes del archivo desde off al socket. Devuelve lo mandado, 0 si
   el socket está lleno, -1 si hubo error. */
int64_t file_send(bw_sock s, bw_file* f, int64_t off, int64_t n);

/* ---- Registro ---- */
void log_open(const char* path);
void log_msg(const char* nivel, const char* fmt, ...);
void access_open(const char* path);
void access_line(const char* line);
/* El worker manda sus líneas al master por este pipe (no bloqueante; si se llena, se tiran). */
void log_pipe_worker(intptr_t fd);
/* El worker de Windows: hay registro de accesos (lo escribe el master), sin abrir el archivo. */
void access_marcar(int si);
/* El master escribe (y rota) lo que le llegó por el pipe. */
void log_master_volcar(const char* datos, size_t n);

/* ---- gzip ---- */
bw_blob* gz_compress(const char* data, size_t len, int level);
/* ¿El cliente acepta gzip? (RFC 9110 §12.5.3, con valores q) */
int accepts_gzip(const char* accept_encoding);
int type_compressible(const bw_conf* c, const char* content_type);

/* ---- Bucle de eventos ---- */
typedef struct bw_evt {
    void* ptr;
    int rd, wr, err;
} bw_evt;
int ev_init(int max);
int ev_add(bw_sock fd, void* ptr, int rd, int wr, int* slot);
int ev_mod(bw_sock fd, void* ptr, int rd, int wr, int* slot);
void ev_del(bw_sock fd, int* slot);
int ev_wait(bw_evt* out, int max, int timeout_ms);

/* ---- HTTPS (tls.c) ---- */
typedef struct bw_tls bw_tls;
int tls_leer_entrada(void);                 /* el master: el PEM de la entrada estándar */
void tls_pem(const char** pem, size_t* n);  /* el PEM leído (para pasárselo a los workers) */
void tls_set_pem(char* pem, size_t n);      /* el worker de Windows: el que le pasó el master */
int tls_iniciar_worker(char* err, size_t errlen);
int tls_activo(void);
bw_tls* tls_nuevo(bw_sock fd);
void tls_liberar(bw_tls* t);
int tls_handshake(bw_tls* t);               /* 1 listo, 0 esperar, -1 falló */
int tls_quiere_escribir(const bw_tls* t);
int tls_recv(bw_tls* t, char* p, int n);    /* >0, 0 esperar, -1 error, -2 cerró */
int tls_send(bw_tls* t, const char* p, int n); /* >0, 0 esperar, -1 error */
size_t tls_pendiente(const bw_tls* t);
/* Lee n bytes del archivo desde off (para mandarlo cifrado: sin sendfile). */
int64_t file_read_at(bw_file* f, char* p, int64_t n, int64_t off);

/* ---- Worker ---- */
int worker_run(const bw_conf* c, bw_sock* listeners, int nlisteners, int64_t master_pid);
extern volatile int g_worker_quit;

#endif
