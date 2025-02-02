#ifndef STATS_H
#define STATS_H

struct running_stats
{
    int readings;
    char *unit;
    double min;
    double max;
    double sum;
    double squares;
    double mean;
    double stddev;
};

//extern const char *running_stats_header;
extern const char running_stats_header[][20];

int clear_stats(struct running_stats *stats);
int update_stats(struct running_stats *stats, double value);
int stats_tsv_header(struct running_stats *stats, FILE *f);
int stats_tsv_row(struct running_stats *stats, FILE *f);

#endif /* STATS_H */
