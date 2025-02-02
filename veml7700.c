#include <math.h>
#include <stdio.h>

#include <hidapi.h>
#include "cp2112.h"
#include "stats.h"
#include "veml7700.h"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

// use the ALS_IT enum to access this
const double veml7700_i_scale[] = {0.0672,  0.0336,   0.0168, 0.0084,  [0x8]=0.1344, [0xC]=0.2688};
const int     veml7700_int_ms[] = {100,     200,      400,    800,     [0x8]=50,     [0xC]=25};
const int    veml7700_refresh[] = {600,     700,      900,    1300,    [0x8]=550,    [0xC]=525};
// "All refresh times .... are shown in the table on the next page."
// all except for 50ms and 25ms ;_;
// it appears to be a constant 500ms?

// use the ALS_GAIN enum to access this
const double veml7700_g_scale[] = {1.0, 0.5, 8.0, 4.0};
const char *veml7700_g_str[]    = {"1", "2", "1/8", "1/4"};

// to make auto-scaling logic easier
// use the enums to access these
const int v7700_more_gain[] = {ALS_GAIN_2X, -1, ALS_GAIN_4DIV, ALS_GAIN_1X};
const int v7700_less_gain[] = {ALS_GAIN_4DIV, ALS_GAIN_1X, -1, ALS_GAIN_8DIV};
const int v7700_more_int[] = {ALS_IT_200ms, ALS_IT_400ms, ALS_IT_800ms, -1, -1, -1, -1, -1, ALS_IT_100ms, -1, -1, -1, ALS_IT_50ms};
const int v7700_less_int[] = {ALS_IT_50ms, ALS_IT_100ms, ALS_IT_200ms, ALS_IT_400ms, -1, -1, -1, -1, ALS_IT_25ms, -1, -1, -1, -1};

const int veml7700_addresses[] = {0x10, END_LIST};

int veml7700_setup(hid_device *handle, enum veml7700_gain g, enum veml7700_integration i)
{
    // whatever is calling this takes care of enable
    unsigned char buf[5];
    int res;
    /*
    buf[0] = VEML_POWER;
    buf[1] = 0x00;  // low
    buf[2] = 0x00;  // high
    res =  i2c_write(handle, VEML7700_ADDR, buf, 3);
    if (res < 0) {
        return res;
    }
    */
    // put sensor in standby and change settings
    // (why did they arrange the reserved bits so that integration is split across 2 bytes?)
    buf[0] = ALS_CONF;
    //buf[1] = ((i & 0x03) << 6) | 0x01;
    //buf[2] = ((i & 0x0C) >> 2) | ((g & 0x03) << 3);
    buf[1] = 0x01;
    buf[2] = 0x00;
    res = i2c_write(handle, VEML7700_ADDR, buf, 3);
    if (res < 0) {
        return res;
    }
    // bring it out of standby
    buf[0] = ALS_CONF;
    buf[1] = ((i & 0x03) << 6);
    buf[2] = ((i & 0x0C) >> 2) | ((g & 0x03) << 3);
    res = i2c_write(handle, VEML7700_ADDR, buf, 3);
    // wait for warmup
    usleep(2500);
    return res;
}

int veml7700_check(hid_device *handle, int address, int force)
{
    int i;
    if (!force && !has_address(address, veml7700_addresses)) {
        return false;
    }
    i = read_word(handle, address, VEML_ID, 2);
    return i == 0xC481;
}

int veml7700_clear_stats(struct veml7700_state *sensor)
{
    clear_stats(&sensor->als_stats);
    clear_stats(&sensor->unf_stats);
    sensor->als_stats.unit = "lux";
    sensor->unf_stats.unit = "unfiltered";
    sensor->raw = 0;
    sensor->lux = 0;
    sensor->unf = 0;
    return 0;
}

int veml7700_read(hid_device *handle, struct veml7700_state *sensor)
{
    int raw_lux, raw_unf;
    veml7700_autoscale(sensor, sensor->raw);
    if (sensor->mode=='*' || sensor->mode=='L') {
        cancel_transfer(handle);
        raw_lux = read_word(handle, VEML7700_ADDR, ALS_DATA, 2);
    }
    if (sensor->mode=='*' || sensor->mode=='U') {
        cancel_transfer(handle);
        raw_unf = read_word(handle, VEML7700_ADDR, UNFILTERED_DATA, 2);
    }
    if (raw_lux < 0 || raw_unf < 0) {
        return 1;
    } else {
        compute_lux(sensor, raw_lux, raw_unf);
        update_stats(&sensor->als_stats, sensor->lux);
        update_stats(&sensor->unf_stats, sensor->unf);
    }
    return 0;
}

double lame_lux_correction(double n)
{
    return 6.0135e-13 * pow(n, 4) + -9.3924e-9 * pow(n, 3) + 8.1488e-5 * pow(n, 2) + 1.0023 * n;
}

double smooth_lux_correction(double n)
{
    double lower = 1000;
    double upper = 1500;
    double partial = (upper - n) / (upper - lower);
    if (n < lower) {
        return n;
    }
    if (n > upper) {
        return lame_lux_correction(n);
    }
    return partial * lame_lux_correction(n) + (1 - partial) * n;   
}

int veml7700_autoscale(struct veml7700_state *sensor, int raw)
{
    // returns true if another sample is needed
    // their sample algo leaves much to be desired
    // the core goal is to have 100ms integration time
    // play with gain as the primary adjustment
    int i = veml7700_int_ms[sensor->integration];
    if (i>100 && raw>200 && raw<10000) {
        sensor->integration = v7700_less_int[sensor->integration];
        if (sensor->gain != ALS_GAIN_2X) {
            sensor->gain = v7700_more_gain[sensor->gain];
        }
        return 1;
    }
    if (i<100 && raw>200 && raw<10000) {
        sensor->integration = v7700_more_int[sensor->integration];
        if (sensor->gain != ALS_GAIN_8DIV) {
            sensor->gain = v7700_less_gain[sensor->gain];
        }
        return 1;
    }
    if (raw > 100 && raw < 10000) {
        return 0;
    }
    if (raw <= 100 && v7700_more_gain[sensor->gain] != -1) {
        sensor->gain = v7700_more_gain[sensor->gain];
        return 1;
    }
    if (raw <= 100 && v7700_more_int[sensor->integration] != -1) {
        sensor->integration = v7700_more_int[sensor->integration];
        return 1;
    }
    if (raw >= 10000 && v7700_less_gain[sensor->gain] != -1) {
        sensor->gain = v7700_less_gain[sensor->gain];
        return 1;
    }
    if (raw >= 10000 && v7700_less_int[sensor->integration] != -1) {
        sensor->integration = v7700_less_int[sensor->integration];
        return 1;
    }
    return 0;
}

int compute_lux(struct veml7700_state *sensor, int raw, int raw_unf)
{
    double lux, unfiltered;
    lux = (double)raw * veml7700_i_scale[sensor->integration] * veml7700_g_scale[sensor->gain];
    lux = smooth_lux_correction(lux);
    //unfiltered = read_word(handle, VEML7700_ADDR, UNFILTERED_DATA, 2);
    unfiltered = (double)raw_unf * veml7700_i_scale[sensor->integration] * veml7700_g_scale[sensor->gain];
    unfiltered = smooth_lux_correction(unfiltered);

    sensor->raw = raw;
    sensor->lux = lux;
    sensor->unf = unfiltered;

    return 0;
}

int veml7700_process_mode(struct veml7700_state *sensor, char c)
{
    switch(c) {
        case '*': // all
        case 'U': // unfiltered
        case 'L': // lux
            sensor->mode = c;
            return 0;
    }
    sensor->mode = '?';
    return 1;
}

int veml7700_tsv_header(struct veml7700_state *sensor, FILE *f)
{
    // the caller handles opening and closing the file
    if (sensor->mode == '*' || sensor->mode == 'L') {
        stats_tsv_header(&(sensor->als_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'U') {
        stats_tsv_header(&(sensor->unf_stats), f);
    }
    fprintf(f, veml7700_debug_header);
    return 0;
}

int veml7700_tsv_row(struct veml7700_state *sensor, FILE *f)
{
    // the caller handles opening and closing the file
    if (sensor->mode == '*' || sensor->mode == 'L') {
        stats_tsv_row(&(sensor->als_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'U') {
        stats_tsv_row(&(sensor->unf_stats), f);
    }
    fprintf(f, "\t%s\t%i", veml7700_g_str[sensor->gain], veml7700_int_ms[sensor->integration]);
    return 0;
}

char *veml7700_debug_header = "\tgain\tintegration ms";
char *veml7700_mode_help = "Valid modes for the VEML7700 are * (all), L (lux), U (unfiltered).";
