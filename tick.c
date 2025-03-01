#include <time.h>
#include <stdbool.h>
#include "tick.h"

// timespec_get() versus clock_gettime()
//printf("%ld.%09ld\n", ts->tv_sec, ts->tv_nsec);

int tick_increment(struct timespec *ts, int ms)
{
    ts->tv_sec += ((long)ms / 1000L);
    ts->tv_nsec += ((long)(ms % 1000) * 1000000L);
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1L;
        ts->tv_nsec -= 1000000000L;
    }
    return 0;
}

int tick_sync_increment(struct timespec *ts, int ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    return tick_increment(ts, ms);
}

int tick_ready(struct timespec *ts)
{
    // return true when now >= ts
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    if (now.tv_sec > ts->tv_sec) {
        return true;
    }
    if (now.tv_sec < ts->tv_sec) {
        return false;
    }
    if (now.tv_nsec < ts->tv_nsec) {
        return false;
    }
    return true;
}

int tick_difference(struct timespec *ts1, struct timespec *ts2)
{
    // ts1 - ts2, returns ms
    time_t s = ts1->tv_sec - ts2->tv_sec;
    long ns = ts1->tv_nsec - ts2->tv_nsec;
    return (int)(s*1000) + (int)(ns/1000000L);
}

long tick_elapsed_ns(struct timespec *ts)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    time_t s = now.tv_sec - ts->tv_sec;
    long ns = now.tv_nsec - ts->tv_nsec;
    return (long)(s)*1000000000L + ns;
}

int tick_missed(struct timespec *ts)
{
    // ms elapsed since ts
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return tick_difference(&now, ts);
}
