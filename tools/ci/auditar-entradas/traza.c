// tools/ci/auditar-entradas/traza.c — qué archivos abre un proceso (macOS, DYLD_INSERT_LIBRARIES)
//
// Interpone open/openat/fopen (lo que se abre para LEER: «O»; para escribir: «W»),
// stat/lstat/fstatat/access («S»)
// y opendir («D»), y anota cada ruta que empieza con BA_TRAZA_PREFIJO en
// BA_TRAZA_DIR/<pid>.log. Solo para auditar las entradas de los tests (auditar.py); no va
// en ningún programa. Los hijos que son binarios del sistema (SIP) no heredan la traza.
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DYLD_INTERPOSE(_r, _o) \
    __attribute__((used)) static struct { const void* r; const void* o; } _interpose_##_o \
    __attribute__((section("__DATA,__interpose"))) = {(const void*)(unsigned long)&_r, (const void*)(unsigned long)&_o};

static __thread int dentro = 0;

static void anotar_(char op, int dirfd, const char* p) {
    if (!p || dentro) return;
    const char* pref = getenv("BA_TRAZA_PREFIJO");
    const char* dir = getenv("BA_TRAZA_DIR");
    if (!pref || !dir) return;
    dentro = 1;
    char abs[PATH_MAX * 2];
    if (p[0] == '/') {
        snprintf(abs, sizeof abs, "%s", p);
    } else {
        char base[PATH_MAX];
        base[0] = 0;
        if (dirfd == AT_FDCWD || dirfd < 0) {
            if (!getcwd(base, sizeof base)) base[0] = 0;
        } else if (fcntl(dirfd, F_GETPATH, base) == -1) {
            base[0] = 0;
        }
        snprintf(abs, sizeof abs, "%s/%s", base, p);
    }
    if (strncmp(abs, pref, strlen(pref)) == 0) {
        char f[PATH_MAX];
        snprintf(f, sizeof f, "%s/%d.log", dir, (int)getpid());
        int fd = open(f, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            size_t n = strlen(abs);
            abs[n] = '\n';
            char pre[2] = {op, ' '};
            write(fd, pre, 2);
            write(fd, abs, n + 1);
            close(fd);
        }
    }
    dentro = 0;
}

#define anotar(d, p) anotar_('O', d, p)
#define anotar_s(d, p) anotar_('S', d, p)
static int mi_open(const char* p, int fl, ...) {
    int modo = 0;
    if (fl & O_CREAT) { va_list a; va_start(a, fl); modo = va_arg(a, int); va_end(a); }
    if (!(fl & (O_WRONLY | O_RDWR))) anotar(AT_FDCWD, p); else anotar_('W', AT_FDCWD, p);
    return open(p, fl, modo);
}
DYLD_INTERPOSE(mi_open, open)

static int mi_openat(int d, const char* p, int fl, ...) {
    int modo = 0;
    if (fl & O_CREAT) { va_list a; va_start(a, fl); modo = va_arg(a, int); va_end(a); }
    if (!(fl & (O_WRONLY | O_RDWR))) anotar(d, p); else anotar_('W', d, p);
    return openat(d, p, fl, modo);
}
DYLD_INTERPOSE(mi_openat, openat)

static FILE* mi_fopen(const char* p, const char* m) {
    if (m && m[0] == 'r' && !strchr(m, '+')) anotar(AT_FDCWD, p); else anotar_('W', AT_FDCWD, p);
    return fopen(p, m);
}
DYLD_INTERPOSE(mi_fopen, fopen)

static int mi_stat(const char* p, struct stat* s) { anotar_s(AT_FDCWD, p); return stat(p, s); }
DYLD_INTERPOSE(mi_stat, stat)
static int mi_lstat(const char* p, struct stat* s) { anotar_s(AT_FDCWD, p); return lstat(p, s); }
DYLD_INTERPOSE(mi_lstat, lstat)
static int mi_fstatat(int d, const char* p, struct stat* s, int f) { anotar_s(d, p); return fstatat(d, p, s, f); }
DYLD_INTERPOSE(mi_fstatat, fstatat)
static int mi_access(const char* p, int m) { anotar_s(AT_FDCWD, p); return access(p, m); }
DYLD_INTERPOSE(mi_access, access)
static DIR* mi_opendir(const char* p) { anotar_('D', AT_FDCWD, p); return opendir(p); }
DYLD_INTERPOSE(mi_opendir, opendir)
