/*
 * cmake/ndi-win/pthread.h — lo minimo de pthreads sobre Win32 para libNoIDea.
 *
 * libNoIDea (extensions/discovery.c) usa un hilo y un mutex con pthreads, que
 * con el ABI de MSVC no existe. Esto cubre exactamente lo que usa: crear y
 * esperar un hilo, y un mutex (SRWLOCK, sin recursos que liberar).
 */
#pragma once
#include <windows.h>
#include <process.h>
#include <stdlib.h>

typedef HANDLE pthread_t;
typedef SRWLOCK pthread_mutex_t;
typedef void pthread_attr_t;
typedef void pthread_mutexattr_t;

struct ba_pthread_start {
    void *(*fn)(void *);
    void *arg;
};

static unsigned __stdcall ba_pthread_trampoline(void *p) {
    struct ba_pthread_start s = *(struct ba_pthread_start *)p;
    free(p);
    s.fn(s.arg);
    return 0;
}

static __inline int pthread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    (void)attr;
    struct ba_pthread_start *s = (struct ba_pthread_start *)malloc(sizeof *s);
    if (s == NULL) return -1;
    s->fn = fn;
    s->arg = arg;
    *t = (HANDLE)_beginthreadex(NULL, 0, ba_pthread_trampoline, s, 0, NULL);
    if (*t == NULL) {
        free(s);
        return -1;
    }
    return 0;
}

static __inline int pthread_join(pthread_t t, void **ret) {
    if (ret != NULL) *ret = NULL;
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    return 0;
}

static __inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a;
    InitializeSRWLock(m);
    return 0;
}
static __inline int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m;
    return 0;
}
static __inline int pthread_mutex_lock(pthread_mutex_t *m) {
    AcquireSRWLockExclusive(m);
    return 0;
}
static __inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    ReleaseSRWLockExclusive(m);
    return 0;
}
