#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include "ae.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

/* --- Platform Detection --- */
#ifdef __linux__
#define HAVE_EPOLL 1
#endif

#if (defined(__APPLE__) && defined(MAC_OS_10_6_DETECTED)) || defined(__DragonFly__) || defined(__FreeBSD__) || \
    defined(__OpenBSD__) || defined(__NetBSD__)
#define HAVE_KQUEUE 1
#endif

/* --- Allocator Shims --- */
#ifndef zmalloc
#define zmalloc  malloc
#define zrealloc realloc
#define zcalloc  calloc
#define zfree    free
#endif

/* --- Panic Macro --- */
#ifndef panic
#define panic(fmt, ...) do { \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__); \
    abort(); \
} while (0)
#endif

/* --- Internal Helpers --- */

static inline int anetCloexec(int fd) {
    int r;
    int flags;

    do {
        r = fcntl(fd, F_GETFD);
    } while (r == -1 && errno == EINTR);

    if (r == -1 || (r & FD_CLOEXEC)) return r;

    flags = r | FD_CLOEXEC;

    do {
        r = fcntl(fd, F_SETFD, flags);
    } while (r == -1 && errno == EINTR);

    return r;
}

/* --- Monotonic Clock Internal Implementations --- */

typedef enum monotonic_clock_type {
    MONOTONIC_CLOCK_POSIX,
    MONOTONIC_CLOCK_HW,
} monotonic_clock_type;

static char monotonic_info_string[32];
static monotime (*getMonotonicUs)(void) = NULL;

#ifndef NO_PROCESSOR_CLOCK
#define USE_PROCESSOR_CLOCK
#endif

#if defined(USE_PROCESSOR_CLOCK) && defined(__x86_64__) && defined(__linux__) && defined(__SIZEOF_INT128__)
#include <regex.h>
#include <x86intrin.h>

#define TSC_CALIBRATION_ITERATIONS 3
#define MONO_FPMULT_SHIFT 24

static uint64_t mono_ticks_speed = UINT64_MAX; /* Fixed-point: (1 << MONO_FPMULT_SHIFT) / ticks_per_us */

static monotime getMonotonicUs_x86(void) {
    return ((__uint128_t)__rdtsc() * mono_ticks_speed) >> MONO_FPMULT_SHIFT;
}

static void monotonicInit_x86linux(void) {
    const int bufflen = 256;
    char buf[bufflen];
    regex_t constTscRegex;
    const size_t nmatch = 2;
    regmatch_t pmatch[nmatch];
    int constantTsc = 0;
    int rc;

    for (int i = 0; i < TSC_CALIBRATION_ITERATIONS; ++i) {
        struct timespec start, end;
        uint64_t tsc_start, tsc_end;

        clock_gettime(CLOCK_MONOTONIC, &start);
        tsc_start = __rdtsc();
        usleep(10000);
        tsc_end = __rdtsc();
        clock_gettime(CLOCK_MONOTONIC, &end);

        uint64_t elapsed_us = (end.tv_sec - start.tv_sec) * 1000000ULL + (end.tv_nsec - start.tv_nsec) / 1000;
        uint64_t tsc_elapsed = tsc_end - tsc_start;
        double sample_ticks_per_us = (double)tsc_elapsed / (double)elapsed_us;
        uint64_t sample_mult = (uint64_t)((double)(1ULL << MONO_FPMULT_SHIFT) / sample_ticks_per_us);

        if (sample_mult < mono_ticks_speed) {
            mono_ticks_speed = sample_mult;
        }
    }

    rc = regcomp(&constTscRegex, "^flags\\s+:.* constant_tsc", REG_EXTENDED);
    assert(rc == 0);

    FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        while (fgets(buf, bufflen, cpuinfo) != NULL) {
            if (regexec(&constTscRegex, buf, nmatch, pmatch, 0) == 0) {
                constantTsc = 1;
                break;
            }
        }
        fclose(cpuinfo);
    }
    regfree(&constTscRegex);

    if (mono_ticks_speed == UINT64_MAX) {
        fprintf(stderr, "monotonic: x86 linux, unable to determine clock rate");
        return;
    }
    if (!constantTsc) {
        fprintf(stderr, "monotonic: x86 linux, 'constant_tsc' flag not present");
        return;
    }

    double ticks_per_us = (double)(1ULL << MONO_FPMULT_SHIFT) / (double)mono_ticks_speed;
    snprintf(monotonic_info_string, sizeof(monotonic_info_string), "X86 TSC @ %.2f ticks/us", ticks_per_us);
    getMonotonicUs = getMonotonicUs_x86;
}
#endif

#if defined(USE_PROCESSOR_CLOCK) && defined(__aarch64__)
static long mono_ticksPerMicrosecond = 0;

static inline uint64_t __cntvct(void) {
    uint64_t virtual_timer_value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(virtual_timer_value));
    return virtual_timer_value;
}

static inline uint32_t cntfrq_hz(void) {
    uint64_t virtual_freq_value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(virtual_freq_value));
    return (uint32_t)virtual_freq_value;
}

static monotime getMonotonicUs_aarch64(void) {
    return __cntvct() / mono_ticksPerMicrosecond;
}

static void monotonicInit_aarch64(void) {
    mono_ticksPerMicrosecond = (long)cntfrq_hz() / 1000L / 1000L;
    if (mono_ticksPerMicrosecond == 0) {
        fprintf(stderr, "monotonic: aarch64, unable to determine clock rate");
        return;
    }

    snprintf(monotonic_info_string, sizeof(monotonic_info_string), "ARM CNTVCT @ %ld ticks/us",
             mono_ticksPerMicrosecond);
    getMonotonicUs = getMonotonicUs_aarch64;
}
#endif

static monotime getMonotonicUs_posix(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

static void monotonicInit_posix(void) {
    struct timespec ts;
    int rc = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(rc == 0);

    snprintf(monotonic_info_string, sizeof(monotonic_info_string), "POSIX clock_gettime");
    getMonotonicUs = getMonotonicUs_posix;
}

static const char *monotonicInit(void) {
#if defined(USE_PROCESSOR_CLOCK) && defined(__x86_64__) && defined(__linux__) && defined(__SIZEOF_INT128__)
    if (getMonotonicUs == NULL) monotonicInit_x86linux();
#endif

#if defined(USE_PROCESSOR_CLOCK) && defined(__aarch64__)
    if (getMonotonicUs == NULL) monotonicInit_aarch64();
#endif

    if (getMonotonicUs == NULL) monotonicInit_posix();

    return monotonic_info_string;
}

/* --- Multiplexing Layer Selection --- */

#ifdef HAVE_EVPORT
#include <port.h>
#include <poll.h>
static int evport_debug = 0;
#define MAX_EVENT_BATCHSZ 512

typedef struct aeApiState {
    int portfd;                           /* event port */
    uint_t npending;                      /* # of pending fds */
    int pending_fds[MAX_EVENT_BATCHSZ];   /* pending fds */
    int pending_masks[MAX_EVENT_BATCHSZ]; /* pending fds' masks */
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    int i;
    aeApiState *state = zmalloc(sizeof(aeApiState));
    if (!state) return -1;

    state->portfd = port_create();
    if (state->portfd == -1) {
        zfree(state);
        return -1;
    }
    anetCloexec(state->portfd);

    state->npending = 0;

    for (i = 0; i < MAX_EVENT_BATCHSZ; i++) {
        state->pending_fds[i] = -1;
        state->pending_masks[i] = AE_NONE;
    }

    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    (void)eventLoop;
    (void)setsize;
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;
    close(state->portfd);
    zfree(state);
}

static int aeApiLookupPending(aeApiState *state, int fd) {
    uint_t i;
    for (i = 0; i < state->npending; i++) {
        if (state->pending_fds[i] == fd) return (i);
    }
    return (-1);
}

static int aeApiAssociate(const char *where, int portfd, int fd, int mask) {
    int events = 0;
    int rv, err;

    if (mask & AE_READABLE) events |= POLLIN;
    if (mask & AE_WRITABLE) events |= POLLOUT;

    if (evport_debug) fprintf(stderr, "%s: port_associate(%d, 0x%x) = ", where, fd, events);

    rv = port_associate(portfd, PORT_SOURCE_FD, fd, events, (void *)(uintptr_t)mask);
    err = errno;

    if (evport_debug) fprintf(stderr, "%d (%s)\n", rv, rv == 0 ? "no error" : strerror(err));

    if (rv == -1) {
        fprintf(stderr, "%s: port_associate: %s\n", where, strerror(err));
        if (err == EAGAIN) fprintf(stderr, "aeApiAssociate: event port limit exceeded.");
    }
    return rv;
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    int fullmask, pfd;

    if (evport_debug) fprintf(stderr, "aeApiAddEvent: fd %d mask 0x%x\n", fd, mask);

    fullmask = mask | eventLoop->events[fd].mask;
    pfd = aeApiLookupPending(state, fd);

    if (pfd != -1) {
        if (evport_debug) fprintf(stderr, "aeApiAddEvent: adding to pending fd %d\n", fd);
        state->pending_masks[pfd] |= fullmask;
        return 0;
    }
    return (aeApiAssociate("aeApiAddEvent", state->portfd, fd, fullmask));
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    int fullmask, pfd;

    if (evport_debug) fprintf(stderr, "del fd %d mask 0x%x\n", fd, mask);

    pfd = aeApiLookupPending(state, fd);

    if (pfd != -1) {
        if (evport_debug) fprintf(stderr, "deleting event from pending fd %d\n", fd);
        state->pending_masks[pfd] &= ~mask;
        if (state->pending_masks[pfd] == AE_NONE) state->pending_fds[pfd] = -1;
        return;
    }

    fullmask = eventLoop->events[fd].mask;
    if (fullmask == AE_NONE) {
        if (evport_debug) fprintf(stderr, "aeApiDelEvent: port_dissociate(%d)\n", fd);
        if (port_dissociate(state->portfd, PORT_SOURCE_FD, fd) != 0) {
            perror("aeApiDelEvent: port_dissociate");
            abort();
        }
    } else if (aeApiAssociate("aeApiDelEvent", state->portfd, fd, fullmask) != 0) {
        abort();
    }
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    struct timespec timeout, *tsp;
    uint_t mask, i;
    uint_t nevents;
    port_event_t event[MAX_EVENT_BATCHSZ];

    for (i = 0; i < state->npending; i++) {
        if (state->pending_fds[i] == -1)
            continue;

        if (aeApiAssociate("aeApiPoll", state->portfd, state->pending_fds[i], state->pending_masks[i]) != 0) {
            abort();
        }
        state->pending_masks[i] = AE_NONE;
        state->pending_fds[i] = -1;
    }

    state->npending = 0;

    if (tvp != NULL) {
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        tsp = &timeout;
    } else {
        tsp = NULL;
    }

    nevents = 1;
    if (port_getn(state->portfd, event, MAX_EVENT_BATCHSZ, &nevents, tsp) == -1 && (errno != ETIME || nevents == 0)) {
        if (errno == ETIME || errno == EINTR) return 0;
        panic("aeApiPoll: port_getn, %s", strerror(errno));
    }

    state->npending = nevents;

    for (i = 0; i < nevents; i++) {
        mask = 0;
        if (event[i].portev_events & POLLIN) mask |= AE_READABLE;
        if (event[i].portev_events & POLLOUT) mask |= AE_WRITABLE;

        eventLoop->fired[i].fd = event[i].portev_object;
        eventLoop->fired[i].mask = mask;

        if (evport_debug) fprintf(stderr, "aeApiPoll: fd %d mask 0x%x\n", (int)event[i].portev_object, mask);

        state->pending_fds[i] = event[i].portev_object;
        state->pending_masks[i] = (uintptr_t)event[i].portev_user;
    }
    return nevents;
}

static char *aeApiName(void) {
    return "evport";
}
#else
#ifdef HAVE_EPOLL
#include <sys/epoll.h>

typedef struct aeApiState {
    int epfd;
    struct epoll_event *events;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    state->events = zmalloc(sizeof(struct epoll_event) * eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }
    state->epfd = epoll_create(1024);
    if (state->epfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }
    anetCloexec(state->epfd);
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;
    state->events = zrealloc(state->events, sizeof(struct epoll_event) * setsize);
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;
    close(state->epfd);
    zfree(state->events);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0};
    int op = eventLoop->events[fd].mask == AE_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= eventLoop->events[fd].mask;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) return -1;
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0};

    mask = eventLoop->events[fd].mask;
    ee.events = 0;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd, state->events, eventLoop->setsize,
                        tvp ? (tvp->tv_sec * 1000 + (tvp->tv_usec + 999) / 1000) : -1);
    if (retval > 0) {
        int j;
        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events + j;

            if (e->events & EPOLLIN) mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE | AE_READABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE | AE_READABLE;
            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    } else if (retval == -1 && errno != EINTR) {
        panic("aeApiPoll: epoll_wait, %s", strerror(errno));
    }
    return numevents;
}

static char *aeApiName(void) {
    return "epoll";
}
#else
#ifdef HAVE_KQUEUE
#include <sys/event.h>

typedef struct aeApiState {
    int kqfd;
    struct kevent *events;
    char *eventsMask;
} aeApiState;

#define EVENT_MASK_MALLOC_SIZE(sz) (((sz) + 3) / 4)
#define EVENT_MASK_OFFSET(fd) ((fd) % 4 * 2)
#define EVENT_MASK_ENCODE(fd, mask) (((mask) & 0x3) << EVENT_MASK_OFFSET(fd))

static inline int getEventMask(const char *eventsMask, int fd) {
    return (eventsMask[fd / 4] >> EVENT_MASK_OFFSET(fd)) & 0x3;
}

static inline void addEventMask(char *eventsMask, int fd, int mask) {
    eventsMask[fd / 4] |= EVENT_MASK_ENCODE(fd, mask);
}

static inline void resetEventMask(char *eventsMask, int fd) {
    eventsMask[fd / 4] &= ~EVENT_MASK_ENCODE(fd, 0x3);
}

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    state->events = zmalloc(sizeof(struct kevent) * eventLoop->setsize);
    if (!state->events) {
        zfree(state);
        return -1;
    }
    state->kqfd = kqueue();
    if (state->kqfd == -1) {
        zfree(state->events);
        zfree(state);
        return -1;
    }
    anetCloexec(state->kqfd);
    state->eventsMask = zmalloc(EVENT_MASK_MALLOC_SIZE(eventLoop->setsize));
    memset(state->eventsMask, 0, EVENT_MASK_MALLOC_SIZE(eventLoop->setsize));
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    state->events = zrealloc(state->events, sizeof(struct kevent) * setsize);
    state->eventsMask = zrealloc(state->eventsMask, EVENT_MASK_MALLOC_SIZE(setsize));
    memset(state->eventsMask, 0, EVENT_MASK_MALLOC_SIZE(setsize));
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->kqfd);
    zfree(state->events);
    zfree(state->eventsMask);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct kevent evs[2];
    int nch = 0;

    if (mask & AE_READABLE) EV_SET(evs + nch++, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (mask & AE_WRITABLE) EV_SET(evs + nch++, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);

    return kevent(state->kqfd, evs, nch, NULL, 0, NULL);
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct kevent evs[2];
    int nch = 0;

    if (mask & AE_READABLE) EV_SET(evs + nch++, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    if (mask & AE_WRITABLE) EV_SET(evs + nch++, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

    kevent(state->kqfd, evs, nch, NULL, 0, NULL);
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    if (tvp != NULL) {
        struct timespec timeout;
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        retval = kevent(state->kqfd, NULL, 0, state->events, eventLoop->setsize, &timeout);
    } else {
        retval = kevent(state->kqfd, NULL, 0, state->events, eventLoop->setsize, NULL);
    }

    if (retval > 0) {
        int j;

        for (j = 0; j < retval; j++) {
            struct kevent *e = state->events + j;
            int fd = e->ident;
            int mask = 0;

            if (e->filter == EVFILT_READ)
                mask = AE_READABLE;
            else if (e->filter == EVFILT_WRITE)
                mask = AE_WRITABLE;
            addEventMask(state->eventsMask, fd, mask);
        }

        numevents = 0;
        for (j = 0; j < retval; j++) {
            struct kevent *e = state->events + j;
            int fd = e->ident;
            int mask = getEventMask(state->eventsMask, fd);

            if (mask) {
                eventLoop->fired[numevents].fd = fd;
                eventLoop->fired[numevents].mask = mask;
                resetEventMask(state->eventsMask, fd);
                numevents++;
            }
        }
    } else if (retval == -1 && errno != EINTR) {
        panic("aeApiPoll: kevent, %s", strerror(errno));
    }
    return numevents;
}

static char *aeApiName(void) {
    return "kqueue";
}
#else
#include <sys/select.h>

typedef struct aeApiState {
    fd_set rfds, wfds;
    fd_set _rfds, _wfds;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    FD_ZERO(&state->rfds);
    FD_ZERO(&state->wfds);
    eventLoop->apidata = state;
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    AE_NOTUSED(eventLoop);
    if (setsize >= FD_SETSIZE) return -1;
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    zfree(eventLoop->apidata);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;

    if (mask & AE_READABLE) FD_SET(fd, &state->rfds);
    if (mask & AE_WRITABLE) FD_SET(fd, &state->wfds);
    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;

    if (mask & AE_READABLE) FD_CLR(fd, &state->rfds);
    if (mask & AE_WRITABLE) FD_CLR(fd, &state->wfds);
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, j, numevents = 0;

    memcpy(&state->_rfds, &state->rfds, sizeof(fd_set));
    memcpy(&state->_wfds, &state->wfds, sizeof(fd_set));

    retval = select(eventLoop->maxfd + 1, &state->_rfds, &state->_wfds, NULL, tvp);
    if (retval > 0) {
        for (j = 0; j <= eventLoop->maxfd; j++) {
            int mask = 0;
            aeFileEvent *fe = &eventLoop->events[j];

            if (fe->mask == AE_NONE) continue;
            if (fe->mask & AE_READABLE && FD_ISSET(j, &state->_rfds)) mask |= AE_READABLE;
            if (fe->mask & AE_WRITABLE && FD_ISSET(j, &state->_wfds)) mask |= AE_WRITABLE;
            eventLoop->fired[numevents].fd = j;
            eventLoop->fired[numevents].mask = mask;
            numevents++;
        }
    } else if (retval == -1 && errno != EINTR) {
        panic("aeApiPoll: select, %s", strerror(errno));
    }
    return numevents;
}

static char *aeApiName(void) {
    return "select";
}
#endif
#endif
#endif

#endif /* __INTERNAL_H__ */
