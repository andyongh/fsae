/* ae.h - Simple event-driven programming library (STB-style single header)
 *
 * USAGE
 *   In ONE .c/.cpp file, define the implementation macro before including:
 *
 *       #define AE_IMPLEMENTATION
 *       #include "ae.h"
 *
 *   In all other files, include normally (no macro needed):
 *
 *       #include "ae.h"
 *
 * OPTIONAL COMPILE-TIME KNOBS
 *   #define AE_STATIC          -- make all symbols static (single-TU use)
 *   #define NO_PROCESSOR_CLOCK -- force POSIX clock_gettime monotonic clock
 *   #define zmalloc  my_malloc  \
 *   #define zrealloc my_realloc  > override allocator before the #include
 *   #define zfree    my_free    /
 *
 * Originally written by Salvatore Sanfilippo for Jim's event-loop (Tcl),
 * later extracted as a standalone library by Redis Ltd.
 *
 * Copyright (c) 2006-2012, Redis Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef AE_H_INCLUDED
#define AE_H_INCLUDED

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>

#ifdef AE_STATIC
#  define AEDEF static
#else
#  define AEDEF extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * PUBLIC API — CONSTANTS
 * ========================================================================= */

typedef uint64_t monotime;

#define AE_OK  0
#define AE_ERR (-1)

/* Event mask bits */
#define AE_NONE     0   /* No events registered */
#define AE_READABLE 1   /* Fire when descriptor is readable */
#define AE_WRITABLE 2   /* Fire when descriptor is writable */
#define AE_BARRIER  4   /* With WRITABLE: never fire after READABLE in the
                           same iteration (useful for fsync-before-reply) */

/* aeProcessEvents() flags */
#define AE_FILE_EVENTS      (1 << 0)
#define AE_TIME_EVENTS      (1 << 1)
#define AE_ALL_EVENTS       (AE_FILE_EVENTS | AE_TIME_EVENTS)
#define AE_DONT_WAIT        (1 << 2)
#define AE_CALL_BEFORE_SLEEP (1 << 3)
#define AE_CALL_AFTER_SLEEP  (1 << 4)
#define AE_PROTECT_POLL     (1 << 5)

#define AE_NOMORE          (-1)
#define AE_DELETED_EVENT_ID (-1)

#define AE_NOTUSED(V) ((void)(V))

/* =========================================================================
 * PUBLIC API — TYPES
 * ========================================================================= */

struct aeEventLoop;
struct timeval;

typedef void  aeFileProc(struct aeEventLoop *el, int fd, void *clientData, int mask);
typedef long long aeTimeProc(struct aeEventLoop *el, long long id, void *clientData);
typedef void  aeEventFinalizerProc(struct aeEventLoop *el, void *clientData);
typedef void  aeBeforeSleepProc(struct aeEventLoop *el);
typedef void  aeAfterSleepProc(struct aeEventLoop *el, int numevents);
typedef int   aeCustomPollProc(struct aeEventLoop *el);

typedef struct aeFileEvent {
    int mask;               /* AE_READABLE | AE_WRITABLE | AE_BARRIER */
    aeFileProc *rfileProc;
    aeFileProc *wfileProc;
    void *clientData;
} aeFileEvent;

typedef struct aeTimeEvent {
    long long id;
    monotime  when;
    aeTimeProc            *timeProc;
    aeEventFinalizerProc  *finalizerProc;
    void *clientData;
    struct aeTimeEvent *prev;
    struct aeTimeEvent *next;
    int refcount;
} aeTimeEvent;

typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;

typedef struct aeEventLoop {
    int maxfd;
    int setsize;
    long long timeEventNextId;
    aeFileEvent    *events;
    aeFiredEvent   *fired;
    aeTimeEvent    *timeEventHead;
    int stop;
    void *apidata;
    aeBeforeSleepProc  *beforesleep;
    aeAfterSleepProc   *aftersleep;
    aeCustomPollProc   *custompoll;
    pthread_mutex_t poll_mutex;
    int flags;
} aeEventLoop;

/* =========================================================================
 * PUBLIC API — PROTOTYPES
 * ========================================================================= */

AEDEF aeEventLoop *aeCreateEventLoop(int setsize);
AEDEF void         aeDeleteEventLoop(aeEventLoop *el);
AEDEF void         aeStop(aeEventLoop *el);

AEDEF int   aeCreateFileEvent(aeEventLoop *el, int fd, int mask, aeFileProc *proc, void *clientData);
AEDEF void  aeDeleteFileEvent(aeEventLoop *el, int fd, int mask);
AEDEF int   aeGetFileEvents(aeEventLoop *el, int fd);
AEDEF void *aeGetFileClientData(aeEventLoop *el, int fd);

AEDEF long long aeCreateTimeEvent(aeEventLoop *el, long long milliseconds,
                                  aeTimeProc *proc, void *clientData,
                                  aeEventFinalizerProc *finalizerProc);
AEDEF int aeDeleteTimeEvent(aeEventLoop *el, long long id);

AEDEF int  aeProcessEvents(aeEventLoop *el, int flags);
AEDEF int  aeWait(int fd, int mask, long long milliseconds);
AEDEF void aeMain(aeEventLoop *el);
AEDEF int  aePoll(aeEventLoop *el, struct timeval *tvp);

AEDEF int   aeGetSetSize(aeEventLoop *el);
AEDEF int   aeResizeSetSize(aeEventLoop *el, int setsize);
AEDEF void  aeSetDontWait(aeEventLoop *el, int noWait);

AEDEF void aeSetBeforeSleepProc(aeEventLoop *el, aeBeforeSleepProc *beforesleep);
AEDEF void aeSetAfterSleepProc(aeEventLoop *el, aeAfterSleepProc *aftersleep);
AEDEF void aeSetCustomPollProc(aeEventLoop *el, aeCustomPollProc *custompoll);
AEDEF void aeSetPollProtect(aeEventLoop *el, int protect);

AEDEF char *aeGetApiName(void);

#ifdef __cplusplus
}
#endif
#endif /* AE_H_INCLUDED */


/* =========================================================================
 * IMPLEMENTATION
 * Define AE_IMPLEMENTATION in exactly one translation unit before including.
 * ========================================================================= */

#ifdef AE_IMPLEMENTATION
#ifndef AE_IMPLEMENTATION_DONE
#define AE_IMPLEMENTATION_DONE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Allocator shims (override before #include if desired)
 * ---------------------------------------------------------------------- */
#ifndef zmalloc
#  define zmalloc  malloc
#  define zrealloc realloc
#  define zcalloc  calloc
#  define zfree    free
#endif

/* -------------------------------------------------------------------------
 * Panic macro
 * ---------------------------------------------------------------------- */
#ifndef panic
#  define panic(fmt, arg) do { \
       fprintf(stderr, "ae panic: " fmt "\n", arg); \
       abort(); \
   } while (0)
#endif

/* -------------------------------------------------------------------------
 * Internal mutex helpers
 * ---------------------------------------------------------------------- */
#define AE_LOCK(el) \
    if ((el)->flags & AE_PROTECT_POLL) { \
        assert(pthread_mutex_lock(&(el)->poll_mutex) == 0); \
    }

#define AE_UNLOCK(el) \
    if ((el)->flags & AE_PROTECT_POLL) { \
        assert(pthread_mutex_unlock(&(el)->poll_mutex) == 0); \
    }

/* -------------------------------------------------------------------------
 * FD_CLOEXEC helper
 * ---------------------------------------------------------------------- */
static int ae__cloexec(int fd) {
    int r, flags;
    do { r = fcntl(fd, F_GETFD); } while (r == -1 && errno == EINTR);
    if (r == -1 || (r & FD_CLOEXEC)) return r;
    flags = r | FD_CLOEXEC;
    do { r = fcntl(fd, F_SETFD, flags); } while (r == -1 && errno == EINTR);
    return r;
}

/* =========================================================================
 * MONOTONIC CLOCK
 * ========================================================================= */

static char ae__mono_info[32];
static monotime (*getMonotonicUs)(void) = NULL;

/* --- x86-64 / Linux TSC ------------------------------------------------- */
#ifndef NO_PROCESSOR_CLOCK
#  define AE__USE_PROCESSOR_CLOCK
#endif

#if defined(AE__USE_PROCESSOR_CLOCK) && \
    defined(__x86_64__) && defined(__linux__) && defined(__SIZEOF_INT128__)
#include <regex.h>
#include <x86intrin.h>

#define AE__TSC_ITERS      3
#define AE__FPMULT_SHIFT   24

static uint64_t ae__tsc_speed = UINT64_MAX;

static monotime ae__getMonotonicUs_x86(void) {
    return ((__uint128_t)__rdtsc() * ae__tsc_speed) >> AE__FPMULT_SHIFT;
}

static void ae__monotonicInit_x86linux(void) {
    regex_t re;
    regmatch_t pm[2];
    int constantTsc = 0;
    char buf[256];

    for (int i = 0; i < AE__TSC_ITERS; i++) {
        struct timespec s, e;
        uint64_t ts, te;
        clock_gettime(CLOCK_MONOTONIC, &s); ts = __rdtsc();
        usleep(10000);
        te = __rdtsc();              clock_gettime(CLOCK_MONOTONIC, &e);
        uint64_t us = (e.tv_sec - s.tv_sec) * 1000000ULL + (e.tv_nsec - s.tv_nsec) / 1000;
        uint64_t m  = (uint64_t)((double)(1ULL << AE__FPMULT_SHIFT) / ((double)(te - ts) / (double)us));
        if (m < ae__tsc_speed) ae__tsc_speed = m;
    }

    assert(regcomp(&re, "^flags\\s+:.* constant_tsc", REG_EXTENDED) == 0);
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        while (fgets(buf, sizeof(buf), f))
            if (regexec(&re, buf, 2, pm, 0) == 0) { constantTsc = 1; break; }
        fclose(f);
    }
    regfree(&re);

    if (ae__tsc_speed == UINT64_MAX) {
        fprintf(stderr, "ae: x86 TSC: unable to determine clock rate\n"); return;
    }
    if (!constantTsc) {
        fprintf(stderr, "ae: x86 TSC: 'constant_tsc' flag not present\n"); return;
    }
    double tpu = (double)(1ULL << AE__FPMULT_SHIFT) / (double)ae__tsc_speed;
    snprintf(ae__mono_info, sizeof(ae__mono_info), "X86 TSC @ %.2f ticks/us", tpu);
    getMonotonicUs = ae__getMonotonicUs_x86;
}
#endif /* x86-64 TSC */

/* --- aarch64 CNTVCT ------------------------------------------------------ */
#if defined(AE__USE_PROCESSOR_CLOCK) && defined(__aarch64__)
static long ae__aarch64_tpm = 0;

static inline uint64_t ae__cntvct(void) {
    uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v;
}
static inline uint32_t ae__cntfrq(void) {
    uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return (uint32_t)v;
}

static monotime ae__getMonotonicUs_aarch64(void) {
    return ae__cntvct() / ae__aarch64_tpm;
}

static void ae__monotonicInit_aarch64(void) {
    ae__aarch64_tpm = (long)ae__cntfrq() / 1000L / 1000L;
    if (ae__aarch64_tpm == 0) {
        fprintf(stderr, "ae: aarch64 CNTVCT: unable to determine clock rate\n"); return;
    }
    snprintf(ae__mono_info, sizeof(ae__mono_info),
             "ARM CNTVCT @ %ld ticks/us", ae__aarch64_tpm);
    getMonotonicUs = ae__getMonotonicUs_aarch64;
}
#endif /* aarch64 */

/* --- POSIX fallback ------------------------------------------------------- */
static monotime ae__getMonotonicUs_posix(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

static void ae__monotonicInit_posix(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    snprintf(ae__mono_info, sizeof(ae__mono_info), "POSIX clock_gettime");
    getMonotonicUs = ae__getMonotonicUs_posix;
}

static void ae__monotonicInit(void) {
#if defined(AE__USE_PROCESSOR_CLOCK) && \
    defined(__x86_64__) && defined(__linux__) && defined(__SIZEOF_INT128__)
    if (!getMonotonicUs) ae__monotonicInit_x86linux();
#endif
#if defined(AE__USE_PROCESSOR_CLOCK) && defined(__aarch64__)
    if (!getMonotonicUs) ae__monotonicInit_aarch64();
#endif
    if (!getMonotonicUs) ae__monotonicInit_posix();
}

/* =========================================================================
 * MULTIPLEXING BACKEND
 * Priority: evport > epoll > kqueue > select
 * ========================================================================= */

/* --- Solaris event ports ------------------------------------------------- */
#if defined(__sun) && defined(HAVE_EVPORT)
#include <port.h>
#include <poll.h>

#define AE__EVPORT_BATCHSZ 512
typedef struct {
    int portfd;
    uint_t npending;
    int  pending_fds  [AE__EVPORT_BATCHSZ];
    int  pending_masks[AE__EVPORT_BATCHSZ];
} aeApiState;

static int aeApiCreate(aeEventLoop *el) {
    aeApiState *s = zmalloc(sizeof(*s));
    if (!s) return -1;
    s->portfd = port_create();
    if (s->portfd == -1) { zfree(s); return -1; }
    ae__cloexec(s->portfd);
    s->npending = 0;
    for (int i = 0; i < AE__EVPORT_BATCHSZ; i++) {
        s->pending_fds[i]   = -1;
        s->pending_masks[i] = AE_NONE;
    }
    el->apidata = s; return 0;
}
static int  aeApiResize(aeEventLoop *el, int sz) { (void)el; (void)sz; return 0; }
static void aeApiFree (aeEventLoop *el) { aeApiState *s = el->apidata; close(s->portfd); zfree(s); }

static int ae__evport_lookup(aeApiState *s, int fd) {
    for (uint_t i = 0; i < s->npending; i++)
        if (s->pending_fds[i] == fd) return (int)i;
    return -1;
}
static int ae__evport_assoc(int portfd, int fd, int mask) {
    int ev = 0;
    if (mask & AE_READABLE) ev |= POLLIN;
    if (mask & AE_WRITABLE) ev |= POLLOUT;
    return port_associate(portfd, PORT_SOURCE_FD, fd, ev, (void *)(uintptr_t)mask);
}
static int aeApiAddEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    int full = mask | el->events[fd].mask;
    int p    = ae__evport_lookup(s, fd);
    if (p != -1) { s->pending_masks[p] |= full; return 0; }
    return ae__evport_assoc(s->portfd, fd, full);
}
static void aeApiDelEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    int p = ae__evport_lookup(s, fd);
    if (p != -1) {
        s->pending_masks[p] &= ~mask;
        if (s->pending_masks[p] == AE_NONE) s->pending_fds[p] = -1;
        return;
    }
    int full = el->events[fd].mask;
    if (full == AE_NONE) {
        if (port_dissociate(s->portfd, PORT_SOURCE_FD, fd) != 0) { perror("port_dissociate"); abort(); }
    } else if (ae__evport_assoc(s->portfd, fd, full) != 0) { abort(); }
}
static int aeApiPoll(aeEventLoop *el, struct timeval *tvp) {
    aeApiState *s = el->apidata;
    port_event_t ev[AE__EVPORT_BATCHSZ];
    struct timespec ts, *tsp = NULL;

    for (uint_t i = 0; i < s->npending; i++) {
        if (s->pending_fds[i] == -1) continue;
        if (ae__evport_assoc(s->portfd, s->pending_fds[i], s->pending_masks[i]) != 0) abort();
        s->pending_masks[i] = AE_NONE; s->pending_fds[i] = -1;
    }
    s->npending = 0;

    if (tvp) { ts.tv_sec = tvp->tv_sec; ts.tv_nsec = tvp->tv_usec * 1000; tsp = &ts; }

    uint_t n = 1;
    if (port_getn(s->portfd, ev, AE__EVPORT_BATCHSZ, &n, tsp) == -1 && (errno != ETIME || n == 0)) {
        if (errno == ETIME || errno == EINTR) return 0;
        panic("aeApiPoll: port_getn, %s", strerror(errno));
    }
    s->npending = n;
    for (uint_t i = 0; i < n; i++) {
        int mask = 0;
        if (ev[i].portev_events & POLLIN)  mask |= AE_READABLE;
        if (ev[i].portev_events & POLLOUT) mask |= AE_WRITABLE;
        el->fired[i].fd   = ev[i].portev_object;
        el->fired[i].mask = mask;
        s->pending_fds[i]   = ev[i].portev_object;
        s->pending_masks[i] = (uintptr_t)ev[i].portev_user;
    }
    return (int)n;
}
static char *aeApiName(void) { return "evport"; }

/* --- Linux epoll --------------------------------------------------------- */
#elif defined(__linux__)
#include <sys/epoll.h>

typedef struct { int epfd; struct epoll_event *events; } aeApiState;

static int aeApiCreate(aeEventLoop *el) {
    aeApiState *s = zmalloc(sizeof(*s));
    if (!s) return -1;
    s->events = zmalloc(sizeof(struct epoll_event) * el->setsize);
    if (!s->events) { zfree(s); return -1; }
    s->epfd = epoll_create(1024);
    if (s->epfd == -1) { zfree(s->events); zfree(s); return -1; }
    ae__cloexec(s->epfd);
    el->apidata = s; return 0;
}
static int aeApiResize(aeEventLoop *el, int sz) {
    aeApiState *s = el->apidata;
    s->events = zrealloc(s->events, sizeof(struct epoll_event) * sz);
    return 0;
}
static void aeApiFree(aeEventLoop *el) {
    aeApiState *s = el->apidata; close(s->epfd); zfree(s->events); zfree(s);
}
static int aeApiAddEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    struct epoll_event ee = {0};
    int op = (el->events[fd].mask == AE_NONE) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    mask |= el->events[fd].mask;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    return (epoll_ctl(s->epfd, op, fd, &ee) == -1) ? -1 : 0;
}
static void aeApiDelEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    struct epoll_event ee = {0};
    mask = el->events[fd].mask;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (mask != AE_NONE) epoll_ctl(s->epfd, EPOLL_CTL_MOD, fd, &ee);
    else                 epoll_ctl(s->epfd, EPOLL_CTL_DEL, fd, &ee);
}
static int aeApiPoll(aeEventLoop *el, struct timeval *tvp) {
    aeApiState *s = el->apidata;
    int ms = tvp ? (int)(tvp->tv_sec * 1000 + (tvp->tv_usec + 999) / 1000) : -1;
    int n  = epoll_wait(s->epfd, s->events, el->setsize, ms);
    if (n > 0) {
        for (int j = 0; j < n; j++) {
            int mask = 0;
            struct epoll_event *e = s->events + j;
            if (e->events & EPOLLIN)  mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE | AE_READABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE | AE_READABLE;
            el->fired[j].fd   = e->data.fd;
            el->fired[j].mask = mask;
        }
    } else if (n == -1 && errno != EINTR) {
        panic("aeApiPoll: epoll_wait, %s", strerror(errno));
    }
    return (n < 0) ? 0 : n;
}
static char *aeApiName(void) { return "epoll"; }

/* --- BSD / macOS kqueue -------------------------------------------------- */
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
      defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/event.h>

typedef struct { int kqfd; struct kevent *events; char *eventsMask; } aeApiState;

#define AE__KQ_MASK_SZ(n)    (((n)+3)/4)
#define AE__KQ_MASK_OFF(fd)  (((fd)%4)*2)
#define AE__KQ_MASK_ENC(fd,m) (((m)&0x3)<<AE__KQ_MASK_OFF(fd))

static inline int  ae__kq_getMask  (const char *m, int fd) { return (m[fd/4] >> AE__KQ_MASK_OFF(fd)) & 0x3; }
static inline void ae__kq_addMask  (char *m, int fd, int mask) { m[fd/4] |=  AE__KQ_MASK_ENC(fd, mask); }
static inline void ae__kq_resetMask(char *m, int fd)           { m[fd/4] &= ~AE__KQ_MASK_ENC(fd, 0x3); }

static int aeApiCreate(aeEventLoop *el) {
    aeApiState *s = zmalloc(sizeof(*s));
    if (!s) return -1;
    s->events = zmalloc(sizeof(struct kevent) * el->setsize);
    if (!s->events) { zfree(s); return -1; }
    s->kqfd = kqueue();
    if (s->kqfd == -1) { zfree(s->events); zfree(s); return -1; }
    ae__cloexec(s->kqfd);
    s->eventsMask = zmalloc(AE__KQ_MASK_SZ(el->setsize));
    memset(s->eventsMask, 0, AE__KQ_MASK_SZ(el->setsize));
    el->apidata = s; return 0;
}
static int aeApiResize(aeEventLoop *el, int sz) {
    aeApiState *s = el->apidata;
    s->events     = zrealloc(s->events, sizeof(struct kevent) * sz);
    s->eventsMask = zrealloc(s->eventsMask, AE__KQ_MASK_SZ(sz));
    memset(s->eventsMask, 0, AE__KQ_MASK_SZ(sz));
    return 0;
}
static void aeApiFree(aeEventLoop *el) {
    aeApiState *s = el->apidata;
    close(s->kqfd); zfree(s->events); zfree(s->eventsMask); zfree(s);
}
static int aeApiAddEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    struct kevent evs[2]; int n = 0;
    if (mask & AE_READABLE) EV_SET(evs + n++, fd, EVFILT_READ,  EV_ADD, 0, 0, NULL);
    if (mask & AE_WRITABLE) EV_SET(evs + n++, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
    return kevent(s->kqfd, evs, n, NULL, 0, NULL);
}
static void aeApiDelEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    struct kevent evs[2]; int n = 0;
    if (mask & AE_READABLE) EV_SET(evs + n++, fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
    if (mask & AE_WRITABLE) EV_SET(evs + n++, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(s->kqfd, evs, n, NULL, 0, NULL);
}
static int aeApiPoll(aeEventLoop *el, struct timeval *tvp) {
    aeApiState *s = el->apidata;
    int n;
    if (tvp) {
        struct timespec ts = { tvp->tv_sec, tvp->tv_usec * 1000 };
        n = kevent(s->kqfd, NULL, 0, s->events, el->setsize, &ts);
    } else {
        n = kevent(s->kqfd, NULL, 0, s->events, el->setsize, NULL);
    }
    if (n < 0 && errno != EINTR) panic("aeApiPoll: kevent, %s", strerror(errno));
    if (n <= 0) return 0;

    for (int j = 0; j < n; j++) {
        struct kevent *e = s->events + j;
        int mask = (e->filter == EVFILT_READ) ? AE_READABLE : AE_WRITABLE;
        ae__kq_addMask(s->eventsMask, (int)e->ident, mask);
    }
    int numevents = 0;
    for (int j = 0; j < n; j++) {
        int fd   = (int)s->events[j].ident;
        int mask = ae__kq_getMask(s->eventsMask, fd);
        if (mask) {
            el->fired[numevents].fd   = fd;
            el->fired[numevents].mask = mask;
            ae__kq_resetMask(s->eventsMask, fd);
            numevents++;
        }
    }
    return numevents;
}
static char *aeApiName(void) { return "kqueue"; }

/* --- POSIX select fallback ----------------------------------------------- */
#else
#include <sys/select.h>

typedef struct { fd_set rfds, wfds, _rfds, _wfds; } aeApiState;

static int aeApiCreate(aeEventLoop *el) {
    aeApiState *s = zmalloc(sizeof(*s));
    if (!s) return -1;
    FD_ZERO(&s->rfds); FD_ZERO(&s->wfds);
    el->apidata = s; return 0;
}
static int aeApiResize(aeEventLoop *el, int sz) {
    (void)el; return (sz >= FD_SETSIZE) ? -1 : 0;
}
static void aeApiFree(aeEventLoop *el) { zfree(el->apidata); }
static int aeApiAddEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    if (mask & AE_READABLE) FD_SET(fd, &s->rfds);
    if (mask & AE_WRITABLE) FD_SET(fd, &s->wfds);
    return 0;
}
static void aeApiDelEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *s = el->apidata;
    if (mask & AE_READABLE) FD_CLR(fd, &s->rfds);
    if (mask & AE_WRITABLE) FD_CLR(fd, &s->wfds);
}
static int aeApiPoll(aeEventLoop *el, struct timeval *tvp) {
    aeApiState *s = el->apidata;
    memcpy(&s->_rfds, &s->rfds, sizeof(fd_set));
    memcpy(&s->_wfds, &s->wfds, sizeof(fd_set));
    int n = select(el->maxfd + 1, &s->_rfds, &s->_wfds, NULL, tvp);
    if (n < 0 && errno != EINTR) panic("aeApiPoll: select, %s", strerror(errno));
    if (n <= 0) return 0;
    int numevents = 0;
    for (int j = 0; j <= el->maxfd; j++) {
        aeFileEvent *fe = &el->events[j];
        if (fe->mask == AE_NONE) continue;
        int mask = 0;
        if ((fe->mask & AE_READABLE) && FD_ISSET(j, &s->_rfds)) mask |= AE_READABLE;
        if ((fe->mask & AE_WRITABLE) && FD_ISSET(j, &s->_wfds)) mask |= AE_WRITABLE;
        if (mask) {
            el->fired[numevents].fd   = j;
            el->fired[numevents].mask = mask;
            numevents++;
        }
    }
    return numevents;
}
static char *aeApiName(void) { return "select"; }
#endif /* backend selection */

/* =========================================================================
 * INTERNAL HELPERS
 * ========================================================================= */

static int64_t ae__usUntilEarliestTimer(aeEventLoop *el) {
    aeTimeEvent *te = el->timeEventHead;
    if (!te) return -1;
    aeTimeEvent *earliest = NULL;
    while (te) {
        if (te->id != AE_DELETED_EVENT_ID && (!earliest || te->when < earliest->when))
            earliest = te;
        te = te->next;
    }
    monotime now = getMonotonicUs();
    return (now >= earliest->when) ? 0 : (int64_t)(earliest->when - now);
}

static int ae__processTimeEvents(aeEventLoop *el) {
    int processed = 0;
    long long maxId = el->timeEventNextId - 1;
    monotime now = getMonotonicUs();
    aeTimeEvent *te = el->timeEventHead;

    while (te) {
        if (te->id == AE_DELETED_EVENT_ID) {
            aeTimeEvent *next = te->next;
            if (te->refcount) { te = next; continue; }
            if (te->prev) te->prev->next = te->next; else el->timeEventHead = te->next;
            if (te->next) te->next->prev = te->prev;
            if (te->finalizerProc) { te->finalizerProc(el, te->clientData); now = getMonotonicUs(); }
            zfree(te); te = next; continue;
        }
        if (te->id > maxId) { te = te->next; continue; }
        if (te->when <= now) {
            long long id = te->id;
            te->refcount++;
            long long ret = te->timeProc(el, id, te->clientData);
            te->refcount--;
            processed++;
            now = getMonotonicUs();
            if (ret != AE_NOMORE) te->when = now + (monotime)ret * 1000;
            else                  te->id   = AE_DELETED_EVENT_ID;
        }
        te = te->next;
    }
    return processed;
}

/* =========================================================================
 * PUBLIC FUNCTION IMPLEMENTATIONS
 * ========================================================================= */

AEDEF aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *el;
    int i;

    ae__monotonicInit();

    if (!(el = zmalloc(sizeof(*el)))) goto err;
    el->events = zmalloc(sizeof(aeFileEvent) * setsize);
    el->fired  = zmalloc(sizeof(aeFiredEvent) * setsize);
    if (!el->events || !el->fired) goto err;

    el->setsize         = setsize;
    el->timeEventHead   = NULL;
    el->timeEventNextId = 1;
    el->stop            = 0;
    el->maxfd           = -1;
    el->beforesleep     = NULL;
    el->aftersleep      = NULL;
    el->custompoll      = NULL;
    el->flags           = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    if (pthread_mutex_init(&el->poll_mutex, &attr) != 0) goto err;

    if (aeApiCreate(el) == -1) goto err;
    for (i = 0; i < setsize; i++) el->events[i].mask = AE_NONE;
    return el;

err:
    if (el) { zfree(el->events); zfree(el->fired); zfree(el); }
    return NULL;
}

AEDEF int aeGetSetSize(aeEventLoop *el) { return el->setsize; }

AEDEF void aeSetDontWait(aeEventLoop *el, int noWait) {
    if (noWait) el->flags |=  AE_DONT_WAIT;
    else        el->flags &= ~AE_DONT_WAIT;
}

AEDEF int aeResizeSetSize(aeEventLoop *el, int setsize) {
    AE_LOCK(el);
    int ret = AE_OK;
    if (setsize == el->setsize) goto done;
    if (el->maxfd >= setsize)   goto err;
    if (aeApiResize(el, setsize) == -1) goto err;

    el->events  = zrealloc(el->events, sizeof(aeFileEvent)  * setsize);
    el->fired   = zrealloc(el->fired,  sizeof(aeFiredEvent) * setsize);
    el->setsize = setsize;
    for (int i = el->maxfd + 1; i < setsize; i++) el->events[i].mask = AE_NONE;
    goto done;
err: ret = AE_ERR;
done: AE_UNLOCK(el); return ret;
}

AEDEF void aeDeleteEventLoop(aeEventLoop *el) {
    aeApiFree(el);
    zfree(el->events);
    zfree(el->fired);
    aeTimeEvent *te = el->timeEventHead, *next;
    while (te) {
        next = te->next;
        if (te->finalizerProc) te->finalizerProc(el, te->clientData);
        zfree(te); te = next;
    }
    zfree(el);
}

AEDEF void aeStop(aeEventLoop *el) { el->stop = 1; }

AEDEF int aeCreateFileEvent(aeEventLoop *el, int fd, int mask,
                            aeFileProc *proc, void *clientData) {
    AE_LOCK(el);
    int ret = AE_ERR;
    if (fd >= el->setsize) { errno = ERANGE; goto done; }

    aeFileEvent *fe = &el->events[fd];
    if (aeApiAddEvent(el, fd, mask) == -1) goto done;
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
    fe->clientData = clientData;
    if (fd > el->maxfd) el->maxfd = fd;
    ret = AE_OK;
done: AE_UNLOCK(el); return ret;
}

AEDEF void aeDeleteFileEvent(aeEventLoop *el, int fd, int mask) {
    AE_LOCK(el);
    if (fd >= el->setsize) goto done;

    aeFileEvent *fe = &el->events[fd];
    if (fe->mask == AE_NONE) goto done;
    if (mask & AE_WRITABLE) mask |= AE_BARRIER;
    mask = mask & fe->mask;
    fe->mask &= ~mask;

    if (fd == el->maxfd && fe->mask == AE_NONE) {
        int j;
        for (j = el->maxfd - 1; j >= 0; j--)
            if (el->events[j].mask != AE_NONE) break;
        el->maxfd = j;
    }
    if (mask & (AE_READABLE | AE_WRITABLE)) aeApiDelEvent(el, fd, mask);
done: AE_UNLOCK(el);
}

AEDEF void *aeGetFileClientData(aeEventLoop *el, int fd) {
    if (fd >= el->setsize) return NULL;
    aeFileEvent *fe = &el->events[fd];
    return (fe->mask == AE_NONE) ? NULL : fe->clientData;
}

AEDEF int aeGetFileEvents(aeEventLoop *el, int fd) {
    if (fd >= el->setsize) return 0;
    return el->events[fd].mask;
}

AEDEF long long aeCreateTimeEvent(aeEventLoop *el, long long milliseconds,
                                  aeTimeProc *proc, void *clientData,
                                  aeEventFinalizerProc *finalizerProc) {
    long long id = el->timeEventNextId++;
    aeTimeEvent *te = zmalloc(sizeof(*te));
    if (!te) return AE_ERR;
    te->id            = id;
    te->when          = getMonotonicUs() + (monotime)milliseconds * 1000;
    te->timeProc      = proc;
    te->finalizerProc = finalizerProc;
    te->clientData    = clientData;
    te->prev          = NULL;
    te->next          = el->timeEventHead;
    te->refcount      = 0;
    if (te->next) te->next->prev = te;
    el->timeEventHead = te;
    return id;
}

AEDEF int aeDeleteTimeEvent(aeEventLoop *el, long long id) {
    aeTimeEvent *te = el->timeEventHead;
    while (te) {
        if (te->id == id) { te->id = AE_DELETED_EVENT_ID; return AE_OK; }
        te = te->next;
    }
    return AE_ERR;
}

AEDEF int aePoll(aeEventLoop *el, struct timeval *tvp) {
    AE_LOCK(el);
    int ret = aeApiPoll(el, tvp);
    AE_UNLOCK(el);
    return ret;
}

AEDEF int aeProcessEvents(aeEventLoop *el, int flags) {
    int processed = 0, numevents;

    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    if (el->maxfd != -1 || ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        struct timeval tv, *tvp = NULL;
        int64_t usUntilTimer;

        if (el->beforesleep && (flags & AE_CALL_BEFORE_SLEEP)) el->beforesleep(el);

        if (el->custompoll) {
            numevents = el->custompoll(el);
        } else {
            if ((flags & AE_DONT_WAIT) || (el->flags & AE_DONT_WAIT)) {
                tv.tv_sec = tv.tv_usec = 0; tvp = &tv;
            } else if (flags & AE_TIME_EVENTS) {
                usUntilTimer = ae__usUntilEarliestTimer(el);
                if (usUntilTimer >= 0) {
                    tv.tv_sec  = (long)(usUntilTimer / 1000000);
                    tv.tv_usec = (long)(usUntilTimer % 1000000);
                    tvp = &tv;
                }
            }
            numevents = aeApiPoll(el, tvp);
        }

        if (!(flags & AE_FILE_EVENTS)) numevents = 0;
        if (el->aftersleep && (flags & AE_CALL_AFTER_SLEEP)) el->aftersleep(el, numevents);

        for (int j = 0; j < numevents; j++) {
            int fd   = el->fired[j].fd;
            int mask = el->fired[j].mask;
            aeFileEvent *fe = &el->events[fd];
            int fired  = 0;
            int invert = fe->mask & AE_BARRIER;

            if (!invert && (fe->mask & mask & AE_READABLE)) {
                fe->rfileProc(el, fd, fe->clientData, mask); fired++;
                fe = &el->events[fd];
            }
            if (fe->mask & mask & AE_WRITABLE) {
                if (!fired || fe->wfileProc != fe->rfileProc) {
                    fe->wfileProc(el, fd, fe->clientData, mask); fired++;
                }
            }
            if (invert) {
                fe = &el->events[fd];
                if ((fe->mask & mask & AE_READABLE) && (!fired || fe->wfileProc != fe->rfileProc)) {
                    fe->rfileProc(el, fd, fe->clientData, mask); fired++;
                }
            }
            processed++;
        }
    }
    if (flags & AE_TIME_EVENTS) processed += ae__processTimeEvents(el);
    return processed;
}

AEDEF int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd = {0};
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    int r = poll(&pfd, 1, (int)milliseconds);
    if (r != 1) return r;

    int ret = 0;
    if (pfd.revents & POLLIN)  ret |= AE_READABLE;
    if (pfd.revents & POLLOUT) ret |= AE_WRITABLE;
    if (pfd.revents & POLLERR) ret |= AE_WRITABLE;
    if (pfd.revents & POLLHUP) ret |= AE_WRITABLE;
    return ret;
}

AEDEF void aeMain(aeEventLoop *el) {
    el->stop = 0;
    while (!el->stop)
        aeProcessEvents(el, AE_ALL_EVENTS | AE_CALL_BEFORE_SLEEP | AE_CALL_AFTER_SLEEP);
}

AEDEF char *aeGetApiName(void) { return aeApiName(); }

AEDEF void aeSetBeforeSleepProc(aeEventLoop *el, aeBeforeSleepProc *p) { el->beforesleep = p; }
AEDEF void aeSetAfterSleepProc (aeEventLoop *el, aeAfterSleepProc  *p) { el->aftersleep  = p; }
AEDEF void aeSetCustomPollProc (aeEventLoop *el, aeCustomPollProc  *p) { el->custompoll  = p; }

AEDEF void aeSetPollProtect(aeEventLoop *el, int protect) {
    if (protect) el->flags |=  AE_PROTECT_POLL;
    else         el->flags &= ~AE_PROTECT_POLL;
}

#endif /* AE_IMPLEMENTATION_DONE */
#endif /* AE_IMPLEMENTATION */
