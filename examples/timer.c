/* gcc -o timer timer.c -lpthread -Wall -Wextra -Wpedantic 2>&1 && echo "=== BUILD OK ===" && ./timer */

#define AE_IMPLEMENTATION
#include "ae.h"

#include <stdio.h>

static long long timer_cb(aeEventLoop *el, long long id, void *data) {
    (void)data; (void)id;
    printf("timer fired\n");
    aeStop(el);
    return AE_NOMORE;
}

int main(void) {
    aeEventLoop *el = aeCreateEventLoop(64);
    if (!el) { fprintf(stderr, "failed\n"); return 1; }
    printf("backend: %s\n", aeGetApiName());
    aeCreateTimeEvent(el, 1, timer_cb, NULL, NULL);
    aeMain(el);
    aeDeleteEventLoop(el);
    return 0;
}
