/*
 * ba-web: lo que depende del sistema (sockets, tiempo, archivos, registro) y los
 * buffers. Las rutas son UTF-8 en todos lados (en Windows se pasan a UTF-16: el
 * evento puede llamarse «Debate-La-Grande-Armée»).
 */
#include "bw.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/uio.h>
#elif defined(__linux__)
#include <sys/sendfile.h>
#endif
#endif

/* ------------------------------------------------------------------------- */
/* Blobs y buffers                                                           */
/* ------------------------------------------------------------------------- */

bw_blob* blob_new(size_t len) {
    bw_blob* b = (bw_blob*)malloc(sizeof(bw_blob) + len + 1);
    if (!b) return NULL;
    b->refs = 1;
    b->len = len;
    b->data[len] = 0;
    return b;
}
bw_blob* blob_ref(bw_blob* b) {
    if (b) b->refs++;
    return b;
}
void blob_unref(bw_blob* b) {
    if (b && --b->refs == 0) free(b);
}

int buf_reserve(bw_buf* b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 1024;
    while (cap < b->len + extra + 1) cap *= 2;
    char* p = (char*)realloc(b->p, cap);
    if (!p) return -1;
    b->p = p;
    b->cap = cap;
    return 0;
}
int buf_add(bw_buf* b, const void* d, size_t n) {
    if (buf_reserve(b, n)) return -1;
    memcpy(b->p + b->len, d, n);
    b->len += n;
    b->p[b->len] = 0;
    return 0;
}
int buf_adds(bw_buf* b, const char* s) { return buf_add(b, s, strlen(s)); }
int buf_printf(bw_buf* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[1024];
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n < sizeof tmp) return buf_add(b, tmp, (size_t)n);
    if (buf_reserve(b, (size_t)n)) return -1;
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}
void buf_consume(bw_buf* b, size_t n) {
    if (n >= b->len) {
        b->len = 0;
    } else {
        memmove(b->p, b->p + n, b->len - n);
        b->len -= n;
    }
    if (b->p) b->p[b->len] = 0;
}
void buf_free(bw_buf* b) {
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

/* ------------------------------------------------------------------------- */
/* Tiempo                                                                    */
/* ------------------------------------------------------------------------- */

int64_t now_ms(void) {
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}
int64_t wall_s(void) { return (int64_t)time(NULL); }
void bw_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static const char* kDia[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* kMes[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* Días desde 1970-01-01 de una fecha civil (Howard Hinnant). */
static int64_t dias_desde_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* IMF-fixdate (RFC 9110 §5.6.7): «Sun, 06 Nov 1994 08:49:37 GMT». */
void http_date(int64_t t, char out[32]) {
    int64_t z = t / 86400, s = t % 86400;
    if (s < 0) { s += 86400; z--; }
    const int wd = (int)((z + 4) % 7 + 7) % 7;
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    const int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
    snprintf(out, 32, "%s, %02d %s %04d %02d:%02d:%02d GMT", kDia[wd], d, kMes[m - 1], (int)y, (int)(s / 3600),
             (int)(s / 60 % 60), (int)(s % 60));
}

/* Lee una fecha HTTP. Acepta IMF-fixdate, RFC 850 y asctime (RFC 9110 §5.6.7). -1 si no. */
int64_t http_date_parse(const char* s) {
    char mes[4] = {0}, wd[16] = {0};
    int d = 0, y = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(s, "%3s, %d %3s %d %d:%d:%d GMT", wd, &d, mes, &y, &hh, &mm, &ss) == 7) {
    } else if (sscanf(s, "%15[^,], %d-%3s-%d %d:%d:%d GMT", wd, &d, mes, &y, &hh, &mm, &ss) == 7) {
        y += y < 70 ? 2000 : (y < 100 ? 1900 : 0);
    } else if (sscanf(s, "%3s %3s %d %d:%d:%d %d", wd, mes, &d, &hh, &mm, &ss, &y) == 7) {
    } else {
        return -1;
    }
    int m = 0;
    for (int i = 0; i < 12; i++)
        if (strncmp(mes, kMes[i], 3) == 0) m = i + 1;
    if (!m || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 60) return -1;
    return dias_desde_civil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}

/* ------------------------------------------------------------------------- */
/* Sockets                                                                   */
/* ------------------------------------------------------------------------- */

int sock_init(void) {
#ifdef _WIN32
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0 ? 0 : -1;
#else
    return 0;
#endif
}
int sock_nonblock(bw_sock s) {
#ifdef _WIN32
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) == 0 ? 0 : -1;
#else
    const int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) return -1;
    fcntl(s, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return 0;
#endif
}
void sock_close(bw_sock s) {
    if (s == BW_BAD) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}
int sock_would_block(void) {
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
int sock_in_progress(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EINPROGRESS || errno == EINTR;
#endif
}

static int resolver(const char* host, int port, struct sockaddr_storage* sa, socklen_t* len) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | AI_PASSIVE;
    char p[16];
    snprintf(p, sizeof p, "%d", port);
    if (getaddrinfo(host[0] ? host : NULL, p, &hints, &res) != 0 || !res) return -1;
    memcpy(sa, res->ai_addr, res->ai_addrlen);
    *len = (socklen_t)res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

bw_sock sock_listen(const char* host, int port, char* err, size_t errlen) {
    struct sockaddr_storage sa;
    socklen_t len = 0;
    if (resolver(host, port, &sa, &len)) {
        snprintf(err, errlen, "no se pudo resolver %s", host);
        return BW_BAD;
    }
    bw_sock s = socket(sa.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == BW_BAD) {
        snprintf(err, errlen, "socket() falló");
        return BW_BAD;
    }
#ifdef _WIN32
    /* En Windows SO_REUSEADDR deja que otro robe el puerto: se usa el exclusivo. */
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&one, sizeof one);
    /* Que los workers puedan heredarlo (se duplica con WSADuplicateSocket). */
#else
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    if (bind(s, (struct sockaddr*)&sa, len) != 0) {
        snprintf(err, errlen, "no se pudo escuchar en %s:%d (¿ocupado?)", host, port);
        sock_close(s);
        return BW_BAD;
    }
    if (listen(s, 511) != 0) {
        snprintf(err, errlen, "listen() falló en %s:%d", host, port);
        sock_close(s);
        return BW_BAD;
    }
    sock_nonblock(s);
    return s;
}

bw_sock sock_connect(const char* host, int port) {
    struct sockaddr_storage sa;
    socklen_t len = 0;
    if (resolver(host, port, &sa, &len)) return BW_BAD;
    bw_sock s = socket(sa.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == BW_BAD) return BW_BAD;
    sock_nonblock(s);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
    if (connect(s, (struct sockaddr*)&sa, len) != 0 && !sock_in_progress()) {
        sock_close(s);
        return BW_BAD;
    }
    return s;
}

int ncpu(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

/* ------------------------------------------------------------------------- */
/* Archivos                                                                  */
/* ------------------------------------------------------------------------- */

#ifdef _WIN32
static wchar_t* ancha(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc(sizeof(wchar_t) * (size_t)n);
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}
static int64_t filetime_a_unix(FILETIME ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}
#endif

int fs_stat(const char* path, bw_stat* st) {
    memset(st, 0, sizeof *st);
#ifdef _WIN32
    wchar_t* w = ancha(path);
    if (!w) return -1;
    WIN32_FILE_ATTRIBUTE_DATA a;
    const BOOL ok = GetFileAttributesExW(w, GetFileExInfoStandard, &a);
    free(w);
    if (!ok) return 0;
    st->exists = 1;
    st->is_dir = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st->size = ((int64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    st->mtime = filetime_a_unix(a.ftLastWriteTime);
    return 0;
#else
    struct stat s;
    if (stat(path, &s) != 0) return 0;
    st->exists = 1;
    st->is_dir = S_ISDIR(s.st_mode);
    st->size = (int64_t)s.st_size;
    st->mtime = (int64_t)s.st_mtime;
    return 0;
#endif
}

int file_open(const char* path, bw_file* f) {
#ifdef _WIN32
    wchar_t* w = ancha(path);
    if (!w) return -1;
    f->h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    free(w);
    return f->h == INVALID_HANDLE_VALUE ? -1 : 0;
#else
    f->fd = open(path, O_RDONLY | O_CLOEXEC);
    return f->fd < 0 ? -1 : 0;
#endif
}
void file_close(bw_file* f) {
#ifdef _WIN32
    if (f->h && f->h != INVALID_HANDLE_VALUE) CloseHandle(f->h);
    f->h = INVALID_HANDLE_VALUE;
#else
    if (f->fd >= 0) close(f->fd);
    f->fd = -1;
#endif
}

/* pread portátil: lee n bytes desde off. */
static int64_t file_pread(bw_file* f, char* p, int64_t n, int64_t off) {
#ifdef _WIN32
    OVERLAPPED o;
    memset(&o, 0, sizeof o);
    o.Offset = (DWORD)(off & 0xffffffff);
    o.OffsetHigh = (DWORD)(off >> 32);
    DWORD leidos = 0;
    if (!ReadFile(f->h, p, (DWORD)n, &leidos, &o)) return -1;
    return (int64_t)leidos;
#else
    ssize_t r;
    do r = pread(f->fd, p, (size_t)n, (off_t)off);
    while (r < 0 && errno == EINTR);
    return (int64_t)r;
#endif
}

int64_t file_send(bw_sock s, bw_file* f, int64_t off, int64_t n) {
    if (n <= 0) return 0;
#if defined(__linux__)
    off_t o = (off_t)off;
    ssize_t r = sendfile(s, f->fd, &o, (size_t)(n > (1 << 20) ? (1 << 20) : n));
    if (r < 0) return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
    return r == 0 ? -1 : (int64_t)r; /* 0: el archivo se achicó */
#elif defined(__APPLE__)
    off_t len = (off_t)(n > (1 << 20) ? (1 << 20) : n);
    const int r = sendfile(f->fd, s, (off_t)off, &len, NULL, 0);
    if (r < 0 && !(errno == EAGAIN || errno == EINTR)) return -1;
    if (r == 0 && len == 0) return -1; /* el archivo se achicó */
    return (int64_t)len;
#else
    /* Windows: de a 64 KiB, sin cargar el archivo. */
    char tmp[65536];
    const int64_t quiero = n > (int64_t)sizeof tmp ? (int64_t)sizeof tmp : n;
    const int64_t leidos = file_pread(f, tmp, quiero, off);
    if (leidos <= 0) return -1;
    const int r = send(s, tmp, (int)leidos, 0);
    if (r < 0) return sock_would_block() ? 0 : -1;
    return r;
#endif
}

bw_blob* fs_read_all(const char* path, size_t max) {
    bw_file f;
    if (file_open(path, &f)) return NULL;
    bw_buf b = {0};
    char tmp[65536];
    int64_t off = 0;
    for (;;) {
        const int64_t r = file_pread(&f, tmp, sizeof tmp, off);
        if (r < 0) {
            buf_free(&b);
            file_close(&f);
            return NULL;
        }
        if (r == 0) break;
        if (b.len + (size_t)r > max) {
            buf_free(&b);
            file_close(&f);
            return NULL;
        }
        buf_add(&b, tmp, (size_t)r);
        off += r;
    }
    file_close(&f);
    bw_blob* out = blob_new(b.len);
    if (out && b.len) memcpy(out->data, b.p, b.len);
    buf_free(&b);
    return out;
}

int fs_realpath(const char* path, char* out, size_t outlen) {
#ifdef _WIN32
    wchar_t* w = ancha(path);
    if (!w) return 0;
    wchar_t full[4096];
    const DWORD n = GetFullPathNameW(w, 4096, full, NULL);
    free(w);
    if (n == 0 || n >= 4096) return 0;
    for (DWORD i = 0; i < n; i++)
        if (full[i] == L'\\') full[i] = L'/';
    return WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int)outlen, NULL, NULL) > 0;
#else
    char tmp[PATH_MAX];
    if (!realpath(path, tmp)) return 0;
    if (strlen(tmp) + 1 > outlen) return 0;
    strcpy(out, tmp);
    return 1;
#endif
}

/* ------------------------------------------------------------------------- */
/* Registro                                                                  */
/* ------------------------------------------------------------------------- */
/*
 * Nada de esto puede trabar a un worker ni llenar el disco:
 *   - El worker NO escribe archivos: manda cada línea al master por un pipe NO bloqueante.
 *     Si el pipe está lleno (el disco anda lento, un ataque genera mucho), la línea se
 *     tira y se cuenta; el worker nunca espera. Líneas cortas (< PIPE_BUF): llegan enteras.
 *   - El master (que no atiende a nadie) las escribe, y rota: a los 5 MiB el archivo pasa a
 *     .1 y el .1 a .2 (el registro de accesos, 20 MiB). Tope: tres archivos de cada uno.
 *   - Sin pipe (antes de lanzar workers, o ba-web -t) escribe directo (el master).
 */

#define LOG_LINEA 480 /* < PIPE_BUF (512 en macOS): cada write llega entero */

typedef struct rotativo {
    char path[1024];
    FILE* f;
    long tope;
} rotativo;

static rotativo g_err = {"", NULL, 5L << 20};
static rotativo g_acc = {"", NULL, 20L << 20};
static int g_hay_acceso = 0;
static long g_perdidas = 0;
#ifdef _WIN32
static HANDLE g_pipe_w = NULL;
#else
static int g_pipe_w = -1;
#endif

static void rot_abrir(rotativo* r) {
    if (r->path[0] && !r->f) r->f = fopen(r->path, "a");
}
static void rot_escribir(rotativo* r, const char* linea, size_t n) {
    FILE* f = r->f ? r->f : (r->path[0] ? NULL : stderr);
    if (!f) return;
    fwrite(linea, 1, n, f);
    if (n == 0 || linea[n - 1] != '\n') fputc('\n', f);
    fflush(f);
    if (r->f && ftell(r->f) > r->tope) {
        fclose(r->f);
        r->f = NULL;
        char a[1100], b[1100];
        snprintf(a, sizeof a, "%s.1", r->path);
        snprintf(b, sizeof b, "%s.2", r->path);
        remove(b);
        rename(a, b);
        rename(r->path, a);
        rot_abrir(r);
    }
}

void log_open(const char* path) {
    snprintf(g_err.path, sizeof g_err.path, "%s", path ? path : "");
    rot_abrir(&g_err);
}
void access_open(const char* path) {
    snprintf(g_acc.path, sizeof g_acc.path, "%s", path ? path : "");
    g_hay_acceso = g_acc.path[0] != 0;
    rot_abrir(&g_acc);
}

void log_pipe_worker(intptr_t fd) {
#ifdef _WIN32
    g_pipe_w = (HANDLE)fd;
    DWORD modo = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    SetNamedPipeHandleState(g_pipe_w, &modo, NULL, NULL);
#else
    g_pipe_w = (int)fd;
    const int fl = fcntl(g_pipe_w, F_GETFL, 0);
    fcntl(g_pipe_w, F_SETFL, fl | O_NONBLOCK);
#endif
}

/* tipo: 'E' (error_log) o 'A' (acceso). */
static void emitir(char tipo, const char* linea) {
    size_t n = strlen(linea);
#ifdef _WIN32
    const int hay_pipe = g_pipe_w != NULL;
#else
    const int hay_pipe = g_pipe_w >= 0;
#endif
    if (!hay_pipe) {
        rot_escribir(tipo == 'A' ? &g_acc : &g_err, linea, n);
        return;
    }
    char buf[LOG_LINEA + 2];
    if (g_perdidas) {
        /* primero, cuántas se perdieron (si entra) */
        const int k = snprintf(buf, sizeof buf, "E(se perdieron %ld líneas del registro: el disco no daba abasto)\n", g_perdidas);
#ifdef _WIN32
        DWORD w = 0;
        if (WriteFile(g_pipe_w, buf, (DWORD)k, &w, NULL) && w == (DWORD)k) g_perdidas = 0;
#else
        if (write(g_pipe_w, buf, (size_t)k) == k) g_perdidas = 0;
#endif
    }
    if (n > LOG_LINEA - 1) n = LOG_LINEA - 1;
    buf[0] = tipo;
    memcpy(buf + 1, linea, n);
    buf[n + 1] = '\n';
#ifdef _WIN32
    DWORD w = 0;
    if (!WriteFile(g_pipe_w, buf, (DWORD)(n + 2), &w, NULL) || w != (DWORD)(n + 2)) g_perdidas++;
#else
    if (write(g_pipe_w, buf, n + 2) != (ssize_t)(n + 2)) g_perdidas++;
#endif
}

/* El master: lo que mandaron los workers, al archivo que corresponde. */
void log_master_volcar(const char* datos, size_t n) {
    static char resto[LOG_LINEA * 2];
    static size_t nresto = 0;
    for (size_t i = 0; i < n; i++) {
        if (datos[i] == '\n' || nresto >= sizeof resto - 1) {
            if (nresto > 1) rot_escribir(resto[0] == 'A' ? &g_acc : &g_err, resto + 1, nresto - 1);
            nresto = 0;
            continue;
        }
        resto[nresto++] = datos[i];
    }
}

void log_msg(const char* nivel, const char* fmt, ...) {
    char linea[LOG_LINEA];
    char fecha[32];
    const time_t t = time(NULL);
    struct tm tmv;
    int k = 0;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(fecha, sizeof fecha, "%Y-%m-%dT%H:%M:%S", &tmv);
#ifdef _WIN32
    k = snprintf(linea, sizeof linea, "%s [%s] ba-web[%lu]: ", fecha, nivel, (unsigned long)GetCurrentProcessId());
#else
    k = snprintf(linea, sizeof linea, "%s [%s] ba-web[%d]: ", fecha, nivel, (int)getpid());
#endif
    if (k < 0 || k >= (int)sizeof linea) k = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(linea + k, sizeof linea - (size_t)k, fmt, ap);
    va_end(ap);
    emitir('E', linea);
}

void access_marcar(int si) { g_hay_acceso = si; }

void access_line(const char* line) {
    if (g_hay_acceso) emitir('A', line);
}
