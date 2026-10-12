/*
 * ba-web: el bucle de eventos de cada worker. Por nivel (no por flanco) en los tres
 * sistemas: un socket con datos sigue avisando hasta que se lee, así que no hay que
 * vaciarlo de una y no se pierde nada si se deja para la vuelta siguiente.
 *   Linux: epoll   ·   macOS/BSD: kqueue   ·   Windows: WSAPoll
 * `slot` es lugar del objeto para que WSAPoll lo saque del arreglo sin buscarlo.
 */
#include "bw.h"

#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
/* ------------------------------------------------------------------ epoll */
#include <sys/epoll.h>
#include <unistd.h>
static int g_ep = -1;
static struct epoll_event* g_evs = NULL;
static int g_max = 0;

int ev_init(int max) {
    g_ep = epoll_create1(EPOLL_CLOEXEC);
    g_max = max < 64 ? 64 : max;
    g_evs = (struct epoll_event*)calloc((size_t)g_max, sizeof *g_evs);
    return g_ep < 0 || !g_evs ? -1 : 0;
}
static int mascara(int rd, int wr) { return (rd ? EPOLLIN : 0) | (wr ? EPOLLOUT : 0) | EPOLLRDHUP; }
int ev_add(bw_sock fd, void* ptr, int rd, int wr, int* slot) {
    (void)slot;
    struct epoll_event e;
    memset(&e, 0, sizeof e);
    e.events = (uint32_t)mascara(rd, wr);
    e.data.ptr = ptr;
    return epoll_ctl(g_ep, EPOLL_CTL_ADD, fd, &e);
}
int ev_mod(bw_sock fd, void* ptr, int rd, int wr, int* slot) {
    (void)slot;
    struct epoll_event e;
    memset(&e, 0, sizeof e);
    e.events = (uint32_t)mascara(rd, wr);
    e.data.ptr = ptr;
    return epoll_ctl(g_ep, EPOLL_CTL_MOD, fd, &e);
}
void ev_del(bw_sock fd, int* slot) {
    (void)slot;
    epoll_ctl(g_ep, EPOLL_CTL_DEL, fd, NULL);
}
int ev_wait(bw_evt* out, int max, int timeout_ms) {
    if (max > g_max) max = g_max;
    const int n = epoll_wait(g_ep, g_evs, max, timeout_ms);
    for (int i = 0; i < n; i++) {
        out[i].ptr = g_evs[i].data.ptr;
        out[i].rd = (g_evs[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) != 0;
        out[i].wr = (g_evs[i].events & EPOLLOUT) != 0;
        out[i].err = (g_evs[i].events & EPOLLERR) != 0;
    }
    return n < 0 ? 0 : n;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
/* ----------------------------------------------------------------- kqueue */
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
static int g_kq = -1;
static struct kevent* g_evs = NULL;
static int g_max = 0;

int ev_init(int max) {
    g_kq = kqueue();
    g_max = max < 64 ? 64 : max;
    g_evs = (struct kevent*)calloc((size_t)g_max, sizeof *g_evs);
    return g_kq < 0 || !g_evs ? -1 : 0;
}
/* kqueue tiene un filtro por dirección: se prende o se apaga cada uno. `slot`
   guarda lo que estaba prendido (bit 1 lectura, bit 2 escritura). */
static int poner(bw_sock fd, void* ptr, int rd, int wr, int* slot) {
    struct kevent ch[2];
    int n = 0;
    const int antes = slot ? *slot : 0;
    const int ahora = (rd ? 1 : 0) | (wr ? 2 : 0);
    if ((antes ^ ahora) & 1) {
        EV_SET(&ch[n], fd, EVFILT_READ, (ahora & 1) ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, ptr);
        n++;
    }
    if ((antes ^ ahora) & 2) {
        EV_SET(&ch[n], fd, EVFILT_WRITE, (ahora & 2) ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, ptr);
        n++;
    }
    if (slot) *slot = ahora;
    return n ? kevent(g_kq, ch, n, NULL, 0, NULL) : 0;
}
int ev_add(bw_sock fd, void* ptr, int rd, int wr, int* slot) {
    if (slot) *slot = 0;
    return poner(fd, ptr, rd, wr, slot);
}
int ev_mod(bw_sock fd, void* ptr, int rd, int wr, int* slot) { return poner(fd, ptr, rd, wr, slot); }
void ev_del(bw_sock fd, int* slot) {
    /* Al cerrar el fd kqueue lo saca solo; acá se apaga lo que hubiera. */
    poner(fd, NULL, 0, 0, slot);
}
int ev_wait(bw_evt* out, int max, int timeout_ms) {
    if (max > g_max) max = g_max;
    struct timespec ts = {timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L};
    const int n = kevent(g_kq, NULL, 0, g_evs, max, timeout_ms < 0 ? NULL : &ts);
    for (int i = 0; i < n; i++) {
        out[i].ptr = g_evs[i].udata;
        out[i].rd = g_evs[i].filter == EVFILT_READ;
        out[i].wr = g_evs[i].filter == EVFILT_WRITE;
        out[i].err = (g_evs[i].flags & EV_ERROR) != 0;
    }
    return n < 0 ? 0 : n;
}

#else
/* ---------------------------------------------------------------- WSAPoll */
static WSAPOLLFD* g_pfd = NULL;
static void** g_ptr = NULL;
static int** g_slots = NULL;
static int g_n = 0, g_cap = 0;

int ev_init(int max) {
    g_cap = max < 64 ? 64 : max;
    g_pfd = (WSAPOLLFD*)calloc((size_t)g_cap, sizeof *g_pfd);
    g_ptr = (void**)calloc((size_t)g_cap, sizeof *g_ptr);
    g_slots = (int**)calloc((size_t)g_cap, sizeof *g_slots);
    return g_pfd && g_ptr && g_slots ? 0 : -1;
}
static short mascara(int rd, int wr) { return (short)((rd ? POLLRDNORM : 0) | (wr ? POLLWRNORM : 0)); }
int ev_add(bw_sock fd, void* ptr, int rd, int wr, int* slot) {
    if (g_n >= g_cap) {
        const int cap = g_cap * 2;
        WSAPOLLFD* p = (WSAPOLLFD*)realloc(g_pfd, sizeof *g_pfd * (size_t)cap);
        void** q = (void**)realloc(g_ptr, sizeof *g_ptr * (size_t)cap);
        int** s = (int**)realloc(g_slots, sizeof *g_slots * (size_t)cap);
        if (!p || !q || !s) return -1;
        g_pfd = p;
        g_ptr = q;
        g_slots = s;
        g_cap = cap;
    }
    g_pfd[g_n].fd = fd;
    g_pfd[g_n].events = mascara(rd, wr);
    g_pfd[g_n].revents = 0;
    g_ptr[g_n] = ptr;
    g_slots[g_n] = slot;
    if (slot) *slot = g_n;
    g_n++;
    return 0;
}
int ev_mod(bw_sock fd, void* ptr, int rd, int wr, int* slot) {
    (void)fd;
    if (!slot || *slot < 0 || *slot >= g_n) return -1;
    g_pfd[*slot].events = mascara(rd, wr);
    g_ptr[*slot] = ptr;
    return 0;
}
void ev_del(bw_sock fd, int* slot) {
    (void)fd;
    if (!slot || *slot < 0 || *slot >= g_n) return;
    const int i = *slot, last = g_n - 1;
    if (i != last) {
        g_pfd[i] = g_pfd[last];
        g_ptr[i] = g_ptr[last];
        g_slots[i] = g_slots[last];
        if (g_slots[i]) *g_slots[i] = i;
    }
    g_n--;
    *slot = -1;
}
int ev_wait(bw_evt* out, int max, int timeout_ms) {
    if (g_n == 0) {
        Sleep((DWORD)(timeout_ms < 0 ? 1000 : timeout_ms));
        return 0;
    }
    const int r = WSAPoll(g_pfd, (ULONG)g_n, timeout_ms);
    if (r <= 0) return 0;
    int k = 0;
    for (int i = 0; i < g_n && k < max; i++) {
        const short re = g_pfd[i].revents;
        if (!re) continue;
        out[k].ptr = g_ptr[i];
        out[k].rd = (re & (POLLRDNORM | POLLHUP)) != 0;
        out[k].wr = (re & POLLWRNORM) != 0;
        out[k].err = (re & (POLLERR | POLLNVAL)) != 0;
        k++;
    }
    return k;
}
#endif
