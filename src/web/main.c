/*
 * ba-web: el master. No atiende a nadie: lee la configuración, abre las escuchas, lanza
 * los workers y los vuelve a lanzar si se caen. Como en nginx:
 *   ba-web -c ba-web.conf [--padre <pid>]   arranca (si el padre muere, se cierra)
 *   ba-web -t -c ba-web.conf                 solo revisa la configuración
 *   ba-web -v                                la versión
 * Señales (POSIX): TERM/INT cierra (los workers terminan lo que están mandando, hasta
 * 5 s); HUP relee la configuración y cambia los workers sin cortar a nadie.
 *
 * POSIX: los workers son fork() del master y heredan las escuchas.
 * Windows: no hay fork; el master se relanza a sí mismo con --worker y le pasa cada
 * escucha duplicada con WSADuplicateSocket por la entrada estándar (como nginx en
 * Windows).
 */
#include "bw.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#endif

static void uso(void) {
    fprintf(stderr, "uso: ba-web -c <archivo.conf> [-t] [--padre <pid>]\n");
}

static int workers_de(const bw_conf* c) {
    if (c->workers > 0) return c->workers;
    int n = ncpu();
    if (n > 4) n = 4; /* la máquina también emite: el sitio no necesita más */
    return n < 1 ? 1 : n;
}

static int padre_vivo(int64_t pid) {
    if (pid <= 0) return 1;
#ifdef _WIN32
    static HANDLE h = NULL;
    if (!h) h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return 0;
    return WaitForSingleObject(h, 0) != WAIT_OBJECT_0;
#else
    return getppid() == (pid_t)pid;
#endif
}

#ifndef _WIN32
/* ======================================================================== */
/* POSIX                                                                    */
/* ======================================================================== */

static volatile sig_atomic_t g_salir = 0, g_releer = 0;
static void al_salir(int s) {
    (void)s;
    g_salir = 1;
}
static void al_releer(int s) {
    (void)s;
    g_releer = 1;
}
static void al_salir_worker(int s) {
    (void)s;
    g_worker_quit = 1;
}

#define MAXW 64
typedef struct {
    pid_t pid;
    int gen;
    int64_t desde;
} wslot;

static int g_log_pipe[2] = {-1, -1};

static pid_t lanzar(const bw_conf* c, bw_sock* ls, int n, pid_t master) {
    const pid_t pid = fork();
    if (pid != 0) return pid;
    /* el worker: su registro va al master por el pipe (no escribe archivos) */
    if (g_log_pipe[1] >= 0) {
        close(g_log_pipe[0]);
        log_pipe_worker(g_log_pipe[1]);
    }
    signal(SIGTERM, al_salir_worker);
    signal(SIGQUIT, al_salir_worker);
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
#if defined(__linux__)
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    _exit(worker_run(c, ls, n, (int64_t)master));
}

/* Cada worker necesita descriptores para sus conexiones, las del gateway y los archivos
   grandes: se sube el tope blando hasta lo que haga falta (o el duro). Si no alcanza, se
   achica worker_connections: aceptar sin descriptores haría girar al worker. */
static void ajustar_descriptores(bw_conf* c) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) return;
    const rlim_t quiero = (rlim_t)c->worker_connections * 2 + 128;
    if (rl.rlim_cur < quiero) {
        rlim_t nuevo = quiero;
        if (rl.rlim_max != RLIM_INFINITY && nuevo > rl.rlim_max) nuevo = rl.rlim_max;
#ifdef __APPLE__
        if (nuevo > (rlim_t)OPEN_MAX) nuevo = (rlim_t)OPEN_MAX; /* macOS no acepta más que OPEN_MAX */
#endif
        rl.rlim_cur = nuevo;
        setrlimit(RLIMIT_NOFILE, &rl);
        getrlimit(RLIMIT_NOFILE, &rl);
    }
    if (rl.rlim_cur < quiero) {
        const int maximo = rl.rlim_cur > 256 ? (int)((rl.rlim_cur - 128) / 2) : (int)(rl.rlim_cur / 3);
        if (maximo < c->worker_connections) {
            log_msg("warn", "solo %llu descriptores: worker_connections baja de %d a %d", (unsigned long long)rl.rlim_cur,
                    c->worker_connections, maximo < 16 ? 16 : maximo);
            c->worker_connections = maximo < 16 ? 16 : maximo;
        }
    }
}

static int correr(const char* conf_path, int64_t padre) {
    bw_conf* conf = (bw_conf*)calloc(1, sizeof *conf);
    char err[512];
    if (conf_load(conf_path, conf, err, sizeof err)) {
        fprintf(stderr, "ba-web: %s\n", err);
        return 1;
    }
    log_open(conf->error_log);
    access_open(conf->access_log);
    ajustar_descriptores(conf);
    bw_sock ls[BW_MAX_LISTEN];
    for (int i = 0; i < conf->nlisten; i++) {
        ls[i] = sock_listen(conf->listen[i].host, conf->listen[i].port, err, sizeof err);
        if (ls[i] == BW_BAD) {
            log_msg("error", "%s", err);
            return 1;
        }
    }
    signal(SIGTERM, al_salir);
    signal(SIGINT, al_salir);
    signal(SIGQUIT, al_salir);
    signal(SIGHUP, al_releer);
    signal(SIGPIPE, SIG_IGN);
    if (pipe(g_log_pipe) == 0) {
        fcntl(g_log_pipe[0], F_SETFL, fcntl(g_log_pipe[0], F_GETFL, 0) | O_NONBLOCK);
        fcntl(g_log_pipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(g_log_pipe[1], F_SETFD, FD_CLOEXEC);
    }
    const pid_t yo = getpid();
    wslot w[MAXW];
    memset(w, 0, sizeof w);
    int gen = 1;
    int nw = workers_de(conf);
    for (int i = 0; i < nw; i++) {
        w[i].pid = lanzar(conf, ls, conf->nlisten, yo);
        w[i].gen = gen;
        w[i].desde = now_ms();
    }
    log_msg("info", "listo: %d workers, escuchando en %s:%d", nw, conf->listen[0].host, conf->listen[0].port);
    int64_t ultimo_padre = now_ms();
    while (!g_salir) {
        int st = 0;
        pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
            for (int i = 0; i < MAXW; i++) {
                if (w[i].pid != p) continue;
                const int de_ahora = w[i].gen == gen;
                w[i].pid = 0;
                if (de_ahora && !g_salir) {
                    /* uno por cada 10 s; si se caen seguido, un resumen (no llena el disco) */
                    static int64_t ultimo_aviso = 0;
                    static int callados = 0;
                    if (now_ms() - ultimo_aviso >= 10000) {
                        if (WIFSIGNALED(st))
                            log_msg("warn", "el worker %d se cayó (señal %d): se relanza%s", (int)p, WTERMSIG(st),
                                    callados ? " (y se relanzaron otros antes, sin anotar)" : "");
                        else
                            log_msg("warn", "el worker %d terminó (código %d): se relanza", (int)p, WEXITSTATUS(st));
                        if (callados) log_msg("warn", "%d relanzamientos más en los últimos 10 s", callados);
                        ultimo_aviso = now_ms();
                        callados = 0;
                    } else {
                        callados++;
                    }
                    /* si se cae enseguida una y otra vez, que no gire: espera un poco */
                    if (now_ms() - w[i].desde < 1000) bw_sleep_ms(500);
                    w[i].pid = lanzar(conf, ls, conf->nlisten, yo);
                    w[i].gen = gen;
                    w[i].desde = now_ms();
                }
            }
        }
        if (g_releer) {
            g_releer = 0;
            bw_conf* nueva = (bw_conf*)calloc(1, sizeof *nueva);
            if (conf_load(conf_path, nueva, err, sizeof err)) {
                log_msg("error", "no se releyó la configuración: %s", err);
                free(nueva);
            } else {
                ajustar_descriptores(nueva);
                int mismas = nueva->nlisten == conf->nlisten;
                for (int i = 0; mismas && i < conf->nlisten; i++)
                    mismas = !strcmp(nueva->listen[i].host, conf->listen[i].host) &&
                             nueva->listen[i].port == conf->listen[i].port;
                if (!mismas) {
                    log_msg("warn", "cambiaron las escuchas: hace falta reiniciar ba-web");
                    free(nueva);
                } else {
                    /* los nuevos workers con la nueva; los viejos terminan lo suyo y se van */
                    gen++;
                    const int viejos = gen - 1;
                    nw = workers_de(nueva);
                    int lanzados = 0;
                    for (int i = 0; i < MAXW && lanzados < nw; i++) {
                        if (w[i].pid) continue;
                        w[i].pid = lanzar(nueva, ls, nueva->nlisten, yo);
                        w[i].gen = gen;
                        w[i].desde = now_ms();
                        lanzados++;
                    }
                    for (int i = 0; i < MAXW; i++)
                        if (w[i].pid && w[i].gen == viejos) kill(w[i].pid, SIGQUIT);
                    conf = nueva; /* la vieja la siguen usando los workers viejos (son otra copia) */
                    log_msg("info", "configuración releída: %d workers", nw);
                }
            }
        }
        if (now_ms() - ultimo_padre >= 1000) {
            ultimo_padre = now_ms();
            if (!padre_vivo(padre)) {
                log_msg("warn", "el gateway ya no está: se cierra");
                g_salir = 1;
            }
        }
        /* lo que mandaron los workers, al archivo (acá sí se puede esperar al disco); se
           espera en el pipe, así se vacía apenas llega algo */
        if (g_log_pipe[0] >= 0) {
            struct pollfd pf = {g_log_pipe[0], POLLIN, 0};
            if (poll(&pf, 1, 100) > 0)
                for (;;) {
                    char tmp[65536];
                    const ssize_t r = read(g_log_pipe[0], tmp, sizeof tmp);
                    if (r <= 0) break;
                    log_master_volcar(tmp, (size_t)r);
                }
        } else {
            bw_sleep_ms(100);
        }
    }
    for (int i = 0; i < MAXW; i++)
        if (w[i].pid) kill(w[i].pid, SIGTERM);
    const int64_t t0 = now_ms();
    for (;;) {
        int vivos = 0;
        for (int i = 0; i < MAXW; i++) {
            if (!w[i].pid) continue;
            if (waitpid(w[i].pid, NULL, WNOHANG) == w[i].pid) w[i].pid = 0;
            else vivos++;
        }
        if (!vivos) break;
        if (now_ms() - t0 > 6000) {
            for (int i = 0; i < MAXW; i++)
                if (w[i].pid) kill(w[i].pid, SIGKILL);
            break;
        }
        bw_sleep_ms(50);
    }
    log_msg("info", "cerrado");
    return 0;
}

#else
/* ======================================================================== */
/* Windows                                                                  */
/* ======================================================================== */

static volatile LONG g_salir = 0;
static BOOL WINAPI al_ctrl(DWORD t) {
    (void)t;
    InterlockedExchange(&g_salir, 1);
    return TRUE;
}

/* El worker: recibe las escuchas por la entrada estándar. */
static int correr_worker(const char* conf_path, int64_t master) {
    bw_conf* conf = (bw_conf*)calloc(1, sizeof *conf);
    char err[512];
    if (conf_load(conf_path, conf, err, sizeof err)) return 1;
    /* el registro va al master por el pipe de stderr (no se abren los archivos: si el worker
       los tuviera abiertos, el master no los podría rotar) */
    log_pipe_worker((intptr_t)GetStdHandle(STD_ERROR_HANDLE));
    access_marcar(conf->access_log[0] != 0);
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    bw_sock ls[BW_MAX_LISTEN];
    for (int i = 0; i < conf->nlisten; i++) {
        WSAPROTOCOL_INFOW pi;
        DWORD leidos = 0, total = 0;
        while (total < sizeof pi) {
            if (!ReadFile(in, (char*)&pi + total, (DWORD)(sizeof pi - total), &leidos, NULL) || !leidos) return 1;
            total += leidos;
        }
        ls[i] = WSASocketW(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &pi, 0, WSA_FLAG_OVERLAPPED);
        if (ls[i] == INVALID_SOCKET) return 1;
        sock_nonblock(ls[i]);
    }
    return worker_run(conf, ls, conf->nlisten, master);
}

typedef struct {
    HANDLE proc;
    DWORD pid;
    int64_t desde;
} wslot;

static HANDLE g_log_r = NULL, g_log_w = NULL;

static int lanzar_win(const char* conf_path, bw_sock* ls, int n, wslot* w) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t confw[2048];
    MultiByteToWideChar(CP_UTF8, 0, conf_path, -1, confw, 2048);
    wchar_t linea[4096];
    _snwprintf(linea, 4096, L"\"%ls\" --worker %lu -c \"%ls\"", exe, (unsigned long)GetCurrentProcessId(), confw);
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rd;
    si.hStdOutput = g_log_w ? g_log_w : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = g_log_w ? g_log_w : GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, linea, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return -1;
    }
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    for (int i = 0; i < n; i++) {
        WSAPROTOCOL_INFOW info;
        if (WSADuplicateSocketW(ls[i], pi.dwProcessId, &info) != 0) break;
        DWORD escritos = 0;
        WriteFile(wr, &info, sizeof info, &escritos, NULL);
    }
    CloseHandle(wr);
    w->proc = pi.hProcess;
    w->pid = pi.dwProcessId;
    w->desde = now_ms();
    return 0;
}

static int correr(const char* conf_path, int64_t padre) {
    bw_conf* conf = (bw_conf*)calloc(1, sizeof *conf);
    char err[512];
    if (conf_load(conf_path, conf, err, sizeof err)) {
        fprintf(stderr, "ba-web: %s\n", err);
        return 1;
    }
    log_open(conf->error_log);
    access_open(conf->access_log);
    bw_sock ls[BW_MAX_LISTEN];
    for (int i = 0; i < conf->nlisten; i++) {
        ls[i] = sock_listen(conf->listen[i].host, conf->listen[i].port, err, sizeof err);
        if (ls[i] == BW_BAD) {
            log_msg("error", "%s", err);
            return 1;
        }
    }
    SetConsoleCtrlHandler(al_ctrl, TRUE);
    {
        SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
        if (CreatePipe(&g_log_r, &g_log_w, &sa, 1 << 16)) SetHandleInformation(g_log_r, HANDLE_FLAG_INHERIT, 0);
        else g_log_r = g_log_w = NULL;
    }
    /* los workers mueren con el master: un Job que los cierra al cerrarse */
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    const int nw = workers_de(conf);
    wslot w[64];
    memset(w, 0, sizeof w);
    for (int i = 0; i < nw; i++) {
        if (lanzar_win(conf_path, ls, conf->nlisten, &w[i]) == 0 && job) AssignProcessToJobObject(job, w[i].proc);
    }
    log_msg("info", "listo: %d workers, escuchando en %s:%d", nw, conf->listen[0].host, conf->listen[0].port);
    while (!g_salir) {
        for (int i = 0; i < nw; i++) {
            if (w[i].proc && WaitForSingleObject(w[i].proc, 0) == WAIT_OBJECT_0) {
                DWORD code = 0;
                GetExitCodeProcess(w[i].proc, &code);
                CloseHandle(w[i].proc);
                w[i].proc = NULL;
                log_msg("warn", "el worker %lu terminó (código %lu): se relanza", (unsigned long)w[i].pid,
                        (unsigned long)code);
                if (now_ms() - w[i].desde < 1000) bw_sleep_ms(500);
                if (lanzar_win(conf_path, ls, conf->nlisten, &w[i]) == 0 && job) AssignProcessToJobObject(job, w[i].proc);
            }
        }
        if (!padre_vivo(padre)) {
            log_msg("warn", "el gateway ya no está: se cierra");
            break;
        }
        DWORD hay = 0;
        while (g_log_r && PeekNamedPipe(g_log_r, NULL, 0, NULL, &hay, NULL) && hay) {
            char tmp[16384];
            DWORD leidos = 0;
            if (!ReadFile(g_log_r, tmp, hay < sizeof tmp ? hay : (DWORD)sizeof tmp, &leidos, NULL) || !leidos) break;
            log_master_volcar(tmp, leidos);
        }
        bw_sleep_ms(100);
    }
    for (int i = 0; i < nw; i++)
        if (w[i].proc) TerminateProcess(w[i].proc, 0);
    log_msg("info", "cerrado");
    return 0;
}
#endif

int main(int argc, char** argv) {
    const char* conf_path = NULL;
    int solo_probar = 0;
    int64_t padre = 0;
#ifdef _WIN32
    int64_t worker_de = 0;
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) conf_path = argv[++i];
        else if (!strcmp(argv[i], "-t")) solo_probar = 1;
        else if (!strcmp(argv[i], "-v")) {
            printf("ba-web/" BW_VERSION "\n");
            return 0;
        } else if (!strcmp(argv[i], "--padre") && i + 1 < argc) padre = atoll(argv[++i]);
#ifdef _WIN32
        else if (!strcmp(argv[i], "--worker") && i + 1 < argc) worker_de = atoll(argv[++i]);
#endif
        else {
            uso();
            return 2;
        }
    }
    if (!conf_path) {
        uso();
        return 2;
    }
    if (sock_init()) return 1;
    if (solo_probar) {
        bw_conf* c = (bw_conf*)calloc(1, sizeof *c);
        char err[512];
        if (conf_load(conf_path, c, err, sizeof err)) {
            fprintf(stderr, "ba-web: %s\n", err);
            return 1;
        }
        printf("ba-web: la configuración %s está bien (%d location, %d workers)\n", conf_path, c->nloc, workers_de(c));
        return 0;
    }
#ifdef _WIN32
    if (worker_de) return correr_worker(conf_path, worker_de);
#endif
    return correr(conf_path, padre);
}
