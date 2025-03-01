#include <stdio.h>
#include <hidapi.h>
#include "cp2112.h"
#include "stats.h"
#include "tick.h"
#include "mlx90614.h"

// Can potentially get 900 readings/second from this sensor.
// There is a huge amount of IIR/FIR filtering options.
// The exact integration time is hard to pin down.
#define MLX_SAMPLE_TIME 200

//const int mlx90614_addresses[] = {ANY_ADDRESS, END_LIST};
const int mlx90614_addresses[] = {0x5A, END_LIST};

double compute_celsius(int n)
{
    double c = (double)n * MLX_DEG_PER_COUNT;
    return (c - 273.15) + MLX_VDD_OFFSET_DEGREES;
}

int mlx90614_read(hid_device *handle, struct mlx90614_state *sensor)
{
    int i;
    // optional 3rd byte is CRC
    if (sensor->mode=='*' || sensor->mode=='A') {
        i = read_word(handle, sensor->address, T_AMB, 3);
        sensor->t_amb = compute_celsius(i & 0xFFFF);
        update_stats(&sensor->t_amb_stats, sensor->t_amb);
    }
    if (sensor->mode=='*' || sensor->mode=='O') {
        i = read_word(handle, sensor->address, T_OBJ1, 3);
        sensor->t_obj = compute_celsius(i & 0xFFFF);
        update_stats(&sensor->t_obj_stats, sensor->t_obj);
    }
    tick_sync_increment(&sensor->wait_until, MLX_SAMPLE_TIME);
    return 0;
}

int mlx90614_clear_stats(struct mlx90614_state *sensor)
{
    sensor->t_amb = NO_TEMPERATURE;
    sensor->t_obj = NO_TEMPERATURE;
    clear_stats(&sensor->t_amb_stats);
    clear_stats(&sensor->t_obj_stats);
    sensor->t_amb_stats.unit = "ambient C";
    sensor->t_obj_stats.unit = "object C";
    return 0;
}

int mlx90614_check(hid_device *handle, int address, int force)
{
    int i;
    if (!force && !has_address(address, mlx90614_addresses)) {
        return false;
    }
    i = read_word(handle, address, MLX_ADDRESS, 2) & 0x00FF;
    return i == address;
}

int mlx90614_process_mode(struct mlx90614_state *sensor, char c)
{
    switch(c) {
        case '*': // all
        case 'O': // object
        case 'A': // ambient
            sensor->mode = c;
            return 0;
    }
    sensor->mode = '?';
    return 1;
}

int mlx90614_tsv_header(struct mlx90614_state *sensor, FILE *f)
{
    // the caller handles opening and closing the file
    if (sensor->mode == '*' || sensor->mode == 'O') {
        stats_tsv_header(&(sensor->t_obj_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'A') {
        stats_tsv_header(&(sensor->t_amb_stats), f);
    }
    fprintf(f, mlx90614_debug_header);
    return 0;
}

int mlx90614_tsv_row(struct mlx90614_state *sensor, FILE *f)
{
    // the caller handles opening and closing the file
    if (sensor->mode == '*' || sensor->mode == 'O') {
        stats_tsv_row(&(sensor->t_obj_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'A') {
        stats_tsv_row(&(sensor->t_amb_stats), f);
    }
    return 0;
}

char *mlx90614_debug_header = "";
char *mlx90614_mode_help = "Valid modes for the MLX90614 are * (all), O (object), A (ambient).";

