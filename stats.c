#include <math.h>
#include <stdio.h>
#include <string.h>
#include "stats.h"

int clear_stats(struct running_stats *stats)
{
    stats->readings = 0;
    stats->sum = 0;
    stats->squares = 0;
    stats->min = INFINITY;
    stats->max = -INFINITY;
    stats->mean = NAN;
    stats->stddev = NAN;
    return 0;
}

int update_stats(struct running_stats *stats, double value)
{
    stats->readings += 1;
    if (value < stats->min) {
        stats->min = value;
    }
    if (value > stats->max) {
        stats->max = value;
    }
    stats->sum += value;
    stats->squares += pow(value, 2);
    stats->mean = stats->sum / (double)stats->readings;
    stats->stddev = sqrt(stats->squares / (double)stats->readings - pow(stats->mean, 2));
    return 0;
}

// each element has the "unit" prepended and tabs added between
// zero-length string to mark the end
const char running_stats_header[][20] = {"mean", "stdev", "min", "max", "readings", ""};

int stats_tsv_header(struct running_stats *stats, FILE *f)
{
    int i;
    for (i=0; strlen(running_stats_header[i]) > 0; i++) {
        fprintf(f, "\t%s %s", stats->unit, running_stats_header[i]);
    }
    return 0;
}

int stats_tsv_row(struct running_stats *stats, FILE *f)
{
    fprintf(f, "\t%.4f\t%.4f\t%.4f\t%.4f\t%i", stats->mean, stats->stddev, stats->min, stats->max, stats->readings);
    return 0;
}
