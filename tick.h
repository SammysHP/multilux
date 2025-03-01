#ifndef TICK_H
#define TICK_H

#include <time.h>

int tick_increment(struct timespec *ts, int ms);
int tick_sync_increment(struct timespec *ts, int ms);
int tick_ready(struct timespec *ts);
int tick_difference(struct timespec *ts1, struct timespec *ts2);
long tick_elapsed_ns(struct timespec *ts);
int tick_missed(struct timespec *ts);

#endif /* TICK_H */

