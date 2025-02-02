#include <stdio.h>
#include <hidapi.h>
#include "cp2112.h"
#include "stats.h"
#include "ltr390uv.h"

// use the ltr_rate enum to access this
const int ltr_rate_ms[] = {25, 50, 100, 200, 500, 1000, 2000, 2000};
const int ltr_faster_rate[] = {
    LTR_R_25MS, LTR_R_25MS, LTR_R_50MS, LTR_R_100MS, LTR_R_200MS, LTR_R_500MS, LTR_R_1000MS, LTR_R_2000MS};
const int ltr_slower_rate[] = {
    LTR_R_50MS, LTR_R_100MS, LTR_R_200MS, LTR_R_500MS, LTR_R_1000MS, LTR_R_2000MS, LTR_R_2000MS};

// use the ltr_gain enum to access this
const int ltr_gain_scale[] = {1, 3, 6, 9, 18};

// to make auto-scaling logic easier
// use the enums to access these
const int ltr_more_gain[] = {LTR_3X, LTR_6X, LTR_9X, LTR_9X};
const int ltr_less_gain[] = {LTR_1X, LTR_1X, LTR_3X, LTR_9X};

// use the ltr_integration enum to access this
// (suddenly this pattern is much less efficient than with the VEML7700)
int ltr_int_ms[128];
int ltr_more_int[128];
int ltr_less_int[128];

const int ltr390uv_addresses[] = {0x53, END_LIST};

int ltr390uv_init()
{
    ltr_int_ms[LTR_I_400MS] = 400;
    ltr_int_ms[LTR_I_200MS] = 200;
    ltr_int_ms[LTR_I_100MS] = 100;
    ltr_int_ms[LTR_I_50MS] = 50;
    ltr_int_ms[LTR_I_25MS] = 25;
    ltr_int_ms[LTR_I_13MS] = 13;

    ltr_more_int[LTR_I_400MS] = LTR_I_400MS;
    ltr_more_int[LTR_I_200MS] = LTR_I_400MS;
    ltr_more_int[LTR_I_100MS] = LTR_I_200MS;
    ltr_more_int[LTR_I_50MS] = LTR_I_100MS;
    ltr_more_int[LTR_I_25MS] = LTR_I_50MS;
    ltr_more_int[LTR_I_13MS] = LTR_I_25MS;

    ltr_less_int[LTR_I_400MS] = LTR_I_200MS;
    ltr_less_int[LTR_I_200MS] = LTR_I_100MS;
    ltr_less_int[LTR_I_100MS] = LTR_I_50MS;
    ltr_less_int[LTR_I_50MS] = LTR_I_25MS;
    ltr_less_int[LTR_I_25MS] = LTR_I_13MS;
    ltr_less_int[LTR_I_13MS] = LTR_I_13MS;

    return 0;
}

int ltr390uv_clear_stats(struct ltr390uv_state *sensor)
{
    clear_stats(&sensor->als_stats);
    clear_stats(&sensor->uvs_stats);
    sensor->als_stats.unit = "lux";
    sensor->uvs_stats.unit = "uW/cm^2 UV";
    sensor->lux = 0;
    sensor->uv_uw = 0;
    sensor->uvi = 0;
    return 0;
}

int config_ltr390uv(hid_device *handle, int mode, int gain, int integration, int rate)
{
    // whatever is calling this takes care of enable
    unsigned char buf[5];
    int res;
    // put it into standby
    buf[0] = LTR_CONTROL;
    buf[1] = (mode << 3) & 0x8;
    res = i2c_write(handle, LTR390UV_ADDR, buf, 2);
    if (res < 0) {
        return res;
    }
    // set up integration
    while (ltr_int_ms[integration] > ltr_rate_ms[rate]) {
        rate = ltr_slower_rate[rate];
    }
    buf[0] = LTR_RATE;
    buf[1] = (integration & 0x70) | (rate & 0x07);
    res = i2c_write(handle, LTR390UV_ADDR, buf, 2);
    if (res < 0) {
        return res;
    }
    // set up gain
    buf[0] = LTR_GAIN;
    buf[1] = gain & 0x07;
    res = i2c_write(handle, LTR390UV_ADDR, buf, 2);
    if (res < 0) {
        return res;
    }
    // bring it out of standby
    buf[0] = LTR_CONTROL;
    buf[1] = 0x02 | ((mode << 3) & 0x8);
    res = i2c_write(handle, LTR390UV_ADDR, buf, 2);
    if (res < 0) {
        return res;
    }
    return 0;
}

int setup_ltr390uv(hid_device *handle, struct ltr390uv_state *sensor)
{
    // whatever is calling this takes care of enable
    if (sensor->uv_mode) {
        return config_ltr390uv(handle, 1, sensor->uvs_gain, sensor->uvs_integration, sensor->uvs_rate);
    }
    return config_ltr390uv(handle, 0, sensor->als_gain, sensor->als_integration, sensor->als_rate);
}

int ltr390uv_check(hid_device *handle, int address, int force)
{
    int i;
    if (!force && !has_address(address, ltr390uv_addresses)) {
        return false;
    }
    i = read_word(handle, address, LTR_ID, 1);
    return (i & 0xF0) == 0xB0;
}

int ltr390uv_done(hid_device *handle)
{
    int i;
    i = read_word(handle, LTR390UV_ADDR, LTR_STATUS, 1);
    return (i & 0x08) > 0;
}

double ltr_raw_to_uv(struct ltr390uv_state *sensor)
{
    // this is kind of approximated
    double uv;
    uv = 0.1 * (double)sensor->uvs_raw;
    uv *= 400 / (double)ltr_int_ms[sensor->uvs_integration];
    uv *= 18 / (double)ltr_gain_scale[sensor->uvs_gain];
    sensor->uv_uw = uv;
    return uv;
}

// the simple definition of UVI is (X mW/m^2) / (25 mW/m^2)  or (2.5 uW/cm^2)
// but the datasheet appears to recommend 2.3? 

double ltr_raw_to_lux(struct ltr390uv_state *sensor)
{
    double lux;
    lux = 0.6 * (double)sensor->als_raw;
    lux *= 100 / (double)ltr_int_ms[sensor->als_integration];
    lux *= 1 / (double)ltr_gain_scale[sensor->als_gain];
    sensor->lux = lux;
    return lux;
}

int ltr390uv_read_raw(hid_device *handle, struct ltr390uv_state *sensor)
{
    int b0, b1, b2;
    while (!ltr390uv_done(handle)) {;}
    // what happens if you request several bytes?
    if (sensor->uv_mode) {
        b0 = read_word(handle, LTR390UV_ADDR, LTR_UVS0, 1);
        b1 = read_word(handle, LTR390UV_ADDR, LTR_UVS1, 1);
        b2 = read_word(handle, LTR390UV_ADDR, LTR_UVS2, 1);
    } else {
        b0 = read_word(handle, LTR390UV_ADDR, LTR_ALS0, 1);
        b1 = read_word(handle, LTR390UV_ADDR, LTR_ALS1, 1);
        b2 = read_word(handle, LTR390UV_ADDR, LTR_ALS2, 1);
    }
    return (b2 & 0xF) << 16 | b1 << 8 | b0;
}

int ltr390uv_read(hid_device *handle, struct ltr390uv_state *sensor)
{
    // switch to UV configuration
    if (sensor->mode=='*' || sensor->mode=='U') {
        sensor->uv_mode = 1;
        ltr_autoscale(sensor);
        setup_ltr390uv(handle, sensor);
        sensor->uvs_raw = ltr390uv_read_raw(handle, sensor);
        ltr_raw_to_uv(sensor);
        update_stats(&sensor->uvs_stats, sensor->uv_uw);
    }
    
    // switch to lux configuration
    if (sensor->mode=='*' || sensor->mode=='L') {
        sensor->uv_mode = 0;
        ltr_autoscale(sensor);
        setup_ltr390uv(handle, sensor);
        sensor->als_raw = ltr390uv_read_raw(handle, sensor);
        ltr_raw_to_lux(sensor);
        update_stats(&sensor->als_stats, sensor->lux);
    }
    return 0;
}

double ltr_normalize(int raw, int integration)
{
    switch (integration) {
    case LTR_I_13MS:
        return (double)raw / (double)(1<<13);
    case LTR_I_25MS:
        return (double)raw / (double)(1<<16);
    case LTR_I_50MS:
        return (double)raw / (double)(1<<17);
    case LTR_I_100MS:
        return (double)raw / (double)(1<<18);
    case LTR_I_200MS:
        return (double)raw / (double)(1<<19);
    case LTR_I_400MS:
        return (double)raw / (double)(1<<20);
    }
    return -1;
}

int ltr_autoscale(struct ltr390uv_state *sensor)
{
    // returns true if another sample is needed
    // based on a lot of guesswork
    // could refactor this with a lot of pointers
    int needed = 0;
    double raw;
    double margin = 0.1;
    
    // als scaling
    if (!sensor->uv_mode) {
        raw = ltr_normalize(sensor->als_raw, sensor->als_integration);
        if (raw < 2*margin) {
            sensor->als_gain = ltr_more_gain[sensor->als_gain];
            needed = 1;
        }
        if (raw < 1*margin) {
            sensor->als_integration = ltr_more_int[sensor->als_integration];
            needed = 1;
        }
        if (raw > 1-2*margin) {
            sensor->als_gain = ltr_less_gain[sensor->als_gain];
            needed = 1;
        }
        if (raw > 1-1*margin) {
            sensor->als_integration = ltr_less_int[sensor->als_integration];
            needed = 1;
        }
    }

    // uvs scaling
    if (sensor->uv_mode) {
        raw = ltr_normalize(sensor->uvs_raw, sensor->uvs_integration);
        if (raw < 2*margin) {
            sensor->uvs_gain = ltr_more_gain[sensor->uvs_gain];
            needed = 1;
        }
        if (raw < 1*margin) {
            sensor->uvs_integration = ltr_more_int[sensor->uvs_integration];
            needed = 1;
        }
        if (raw > 1-2*margin) {
            sensor->uvs_gain = ltr_less_gain[sensor->uvs_gain];
            needed = 1;
        }
        if (raw > 1-1*margin) {
            sensor->uvs_integration = ltr_less_int[sensor->uvs_integration];
            needed = 1;
        }
    }

    return needed;
}

int ltr390uv_process_mode(struct ltr390uv_state *sensor, char c)
{
    switch(c) {
        case '*': // all
        case 'U': // uv
        case 'L': // lux
            sensor->mode = c;
            return 0;
    }
    sensor->mode = '?';
    return 1;
}

int ltr390uv_tsv_header(struct ltr390uv_state *sensor, FILE *f)
{
    // the caller handles opening and closing the file
    if (sensor->mode == '*' || sensor->mode == 'L') {
        stats_tsv_header(&(sensor->als_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'U') {
        stats_tsv_header(&(sensor->uvs_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'L') {
        fprintf(f, ltr390uv_debug_als_header);
    }
    if (sensor->mode == '*' || sensor->mode == 'U') {
        fprintf(f, ltr390uv_debug_uvs_header);
    }
    return 0;
}

int ltr390uv_tsv_row(struct ltr390uv_state *sensor, FILE *f)
{
    // the caller handles opening and closing the file
    if (sensor->mode == '*' || sensor->mode == 'L') {
        stats_tsv_row(&(sensor->als_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'U') {
        stats_tsv_row(&(sensor->uvs_stats), f);
    }
    if (sensor->mode == '*' || sensor->mode == 'L') {
        fprintf(f, "\t%i\t%i", ltr_gain_scale[sensor->als_gain], ltr_int_ms[sensor->als_integration]);
    }
    if (sensor->mode == '*' || sensor->mode == 'U') {
        fprintf(f, "\t%i\t%i", ltr_gain_scale[sensor->uvs_gain], ltr_int_ms[sensor->uvs_integration]);
    }
    return 0;
}

char *ltr390uv_debug_als_header = "\tlux gain\tlux integration ms";
char *ltr390uv_debug_uvs_header = "\tUV gain\tUV integration ms";
char *ltr390uv_mode_help = "Valid modes for the LTR390UV are * (all), L (lux), U (UVB).";
