#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>

#include <hidapi.h>
#include "cp2112.h"
#include "stats.h"
#include "tick.h"
#include "tca9548a.h"
#include "veml7700.h"
#include "ltr390uv.h"
#include "mlx90614.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

enum device_list {TCA9548A, VEML7700, LTR390UV, MLX90614, END_SENSOR_LIST};
char device_names[][20] = {"TCA9548A", "VEML7700", "LTR390UV", "MLX90614", "NONE"};
#define MAX_SENSORS 16

struct sensor_state
{
    // hardware things
    int channel;
    int address;
    char mode;
    int hw;
    // too small to be worth the trouble of malloc
    struct veml7700_state veml7700_sensor;
    struct mlx90614_state mlx90614_sensor;
    struct ltr390uv_state ltr390uv_sensor;
    int readings;  // could be different from the running_stats readings for some sensors
    // logging things
    long int next_report_time;
    int report_interval;
    char *file_name;
    char *error;
    int errors;
    int zero_halt;
    long read_ns;
    long pass_ns;
    // stuff that points into the sensor object in use
    struct timespec *wait_until;
};

volatile int force_exit;

struct tca9548a_state tca9548a_device;  // global because only 1 is supported for now

void exit_handler(int sig_num)
{
    printf("\nSaving data and cleaning up connections....\n");
    fflush(stdout);
    force_exit = 1;
}

int init_status(struct sensor_state sensors[MAX_SENSORS])
{
    int i;
    for (i=0; i<MAX_SENSORS; i++) {
        sensors[i].channel = DUMMY_CHANNEL;
        sensors[i].veml7700_sensor.gain = ALS_GAIN_8DIV;
        sensors[i].veml7700_sensor.integration = ALS_IT_100ms;
        clock_gettime(CLOCK_REALTIME, &sensors[i].veml7700_sensor.wait_until);
        sensors[i].ltr390uv_sensor.als_gain = LTR_3X;
        sensors[i].ltr390uv_sensor.als_integration = LTR_I_100MS;
        sensors[i].ltr390uv_sensor.uvs_gain = LTR_3X;
        sensors[i].ltr390uv_sensor.uvs_integration = LTR_I_100MS;
        clock_gettime(CLOCK_REALTIME, &sensors[i].ltr390uv_sensor.wait_until);
        clock_gettime(CLOCK_REALTIME, &sensors[i].mlx90614_sensor.wait_until);
        sensors[i].file_name = NULL;
        sensors[i].error = "";
        sensors[i].errors = 0;
        sensors[i].zero_halt = 0;
        sensors[i].read_ns = 0L;
        sensors[i].pass_ns = 0L;
        veml7700_clear_stats(&sensors[i].veml7700_sensor);
        ltr390uv_clear_stats(&sensors[i].ltr390uv_sensor);
        sensors[i].mlx90614_sensor.address = 0;
        mlx90614_clear_stats(&sensors[i].mlx90614_sensor);
    }
    return 0;
}

int channel_select(hid_device *handle, int channel)
{
    if (channel == MAIN_CHANNEL || channel == NO_CHANNEL) {
        //return set_gpio(handle, 0x00, 0xFC);
        return tca9548a_select_channel(handle, &tca9548a_device, -1, true);
    }
    //if (channel < 2) {
    //    return -1;
    //}
    if (channel > 7) {
        return -1;
    }
    //return set_gpio(handle, 1<<channel, 0xFC);
    return tca9548a_select_channel(handle, &tca9548a_device, channel, false);
}

int next_sensor(struct sensor_state sensors[MAX_SENSORS])
{
    // a fair scheduling algo
    // tries to give each sensor an equal number of reads per report_interval
    int i, best_i, elapsed;
    time_t t = time(NULL);
    double reads, score, best_score;
    struct sensor_state *sensor;
    best_i = -1;
    best_score = 1e10;
    for (i=MAX_SENSORS-1; i>=0; i--) {
        sensor = &sensors[i];
        if (sensor->channel == DUMMY_CHANNEL) {
            continue;
        }
        if (sensor->zero_halt) {
            continue;
        }
        reads = sensor->readings + 1 + (double)sensor->errors/2;
        elapsed = sensor->report_interval + 1 - (sensor->next_report_time - t);
        score = (double)(reads * sensor->report_interval) / (double)elapsed;
        if (score < best_score) {
            best_score = score;
            best_i = i;
        }
    }
    return best_i;
}

int next_sensor2(struct sensor_state sensors[MAX_SENSORS])
{
    // finds whatever is most expired
    // returns sensor index if its ready to run
    // otherwise returns -1*ms to wait for a sensor
    // probably should use a heap but there isn't much going on
    int i, best_i, ms, best_ms;
    struct sensor_state *sensor;
    best_i = -1;
    best_ms = -1000;
    for (i=MAX_SENSORS-1; i>=0; i--) {
        sensor = &sensors[i];
        if (sensor->channel == DUMMY_CHANNEL) {
            continue;
        }
        if (sensor->zero_halt) {
            continue;
        }
        ms = tick_missed(sensor->wait_until);
        if (ms > best_ms) {
            best_i = i;
            best_ms = ms;
        }
    }
    if (best_ms < 0) {
        return best_ms;
    }
    return best_i;
}

char pretty_channel(int c)
{
    if (c == MAIN_CHANNEL) {
        return '*';
    }
    return c + 48;
}

int show_status(struct sensor_state sensors[MAX_SENSORS])
{
    char item[25];
    char chan;
    int i;
    struct sensor_state *s;
    long sum_pass = 0L, sum_sensor = 0L;
    printf("\r");
    for (i=0; i<MAX_SENSORS; i++) {
        s = &sensors[i];
        if (s->channel == DUMMY_CHANNEL) {
            continue;
        }
        sum_pass += s->pass_ns;
        sum_sensor += s->read_ns;
        chan = pretty_channel(s->channel);
        if (s->zero_halt) {
            snprintf(item, 24, "%c-0x%X: DONE", chan, s->address);
        } else if (strlen(s->error)) {
            snprintf(item, 24, "%c-0x%X: %s", chan, s->address, s->error);
        } else if (s->hw == VEML7700) {
            snprintf(item, 24, "%c: %.2flx", chan, s->veml7700_sensor.lux);
        } else if (s->hw == MLX90614) {
            snprintf(item, 24, "%c-0x%X: %.2fC", chan, s->address, s->mlx90614_sensor.t_obj);
        } else if (s->hw == LTR390UV) {
            switch (s->mode) {
            case 'L':
                snprintf(item, 24, "%c: %.2flx", chan, s->ltr390uv_sensor.lux); break;
            case 'U':
                snprintf(item, 24, "%c: %.2fuW", chan, s->ltr390uv_sensor.uv_uw); break;
            default:
                snprintf(item, 24, "%c: %.2flx %.2fuW", chan, s->ltr390uv_sensor.lux, s->ltr390uv_sensor.uv_uw); break;
            }
        }
        printf("%-24s", item);
    }
    printf("i2c: %.0f%%  ", 100*(double)sum_sensor/(double)sum_pass);
    fflush(stdout);
    return 0;
}

int maybe_log(struct sensor_state *sensor, int force)
{
    FILE *f;
    time_t t;
    char fulltime[30];

    // has enough time elapsed?
    t = time(NULL);
    if (!force && sensor->next_report_time > t) {
        return 0;
    }

    f = fopen(sensor->file_name, "a");
    if (f == NULL) {
        sensor->error = "bad file";
        return -1;
    }
    strftime(fulltime, 30, "%a %b %d %H:%M:%S %Y", localtime(&t));
    fprintf(f, "%s\t%ld", fulltime, t);

    switch (sensor->hw) {
        case VEML7700:
            veml7700_tsv_row(&(sensor->veml7700_sensor), f);
            clear_stats(&sensor->veml7700_sensor.als_stats);
            clear_stats(&sensor->veml7700_sensor.unf_stats);
            break;
        case LTR390UV:
            ltr390uv_tsv_row(&(sensor->ltr390uv_sensor), f);
            clear_stats(&sensor->ltr390uv_sensor.als_stats);
            clear_stats(&sensor->ltr390uv_sensor.uvs_stats);
            break;
        case MLX90614:
            mlx90614_tsv_row(&(sensor->mlx90614_sensor), f);
            clear_stats(&sensor->mlx90614_sensor.t_obj_stats);
            clear_stats(&sensor->mlx90614_sensor.t_amb_stats);
            //sensor->mlx90614_sensor.t_amb = NO_TEMPERATURE;
            //sensor->mlx90614_sensor.t_obj = NO_TEMPERATURE;
            break;
    }
    fprintf(f, "\t%i\t%i\t%s", (int)round((double)sensor->read_ns/1e6), sensor->errors, sensor->error);
    fprintf(f, "\n");

    // clean up
    fclose(f);
    sensor->next_report_time += sensor->report_interval;
    sensor->read_ns = 0L;
    sensor->pass_ns = 1L;
    sensor->error = "";
    sensor->errors = 0;
    return 0;
}

int exists(char *file_name)
{
    struct stat buf;
    return !stat(file_name, &buf);
}

int maybe_header(struct sensor_state *sensor)
{
    FILE *f;
    if (exists(sensor->file_name)) {
        return 0;
    }
    f = fopen(sensor->file_name, "a");
    if (f == NULL) {
        return -1;
    }
    fprintf(f, "full time\tseconds");

    switch (sensor->hw) {
        case VEML7700:
            veml7700_tsv_header(&(sensor->veml7700_sensor), f);
            break;
        case LTR390UV:
            ltr390uv_tsv_header(&(sensor->ltr390uv_sensor), f);
            break;
        case MLX90614:
            mlx90614_tsv_header(&(sensor->mlx90614_sensor), f);
            break;
    }

    fprintf(f, "\ti2c ms\terrors\terror msg");
    fprintf(f, "\n");
    fclose(f);
    return 0;
}

int has_arg(char *flag, int argc, char *argv[])
{
    int i;
    for (i=1; i<argc; i++) {
        if (!strcmp(flag, argv[i])) {
            return 1;
        }
    }
    return 0;
}

int show_help()
{
    printf("multilux [--noblink] [--slow] channel_num-i2c_addr-data_chan:integrate_seconds:file_name.tsv [more channels]\n\n");
    printf("    --scan searches for all devices on the bus.  It produces channel_number-i2c_address pairs and then exits.\n");
    printf("    --noblink disables the indicator LEDs.\n");
    printf("    --slow runs I2C at 20kHz instead of 100kHz.\n");
    printf("    --fast runs I2C at 400kHz instead of 100kHz.\n\n");
    printf("    channel_num is the multipexer channel that enables a particular bus.  Must be * (for the main bus) or between 0 and 7.\n");
    printf("    i2c_addr is the hex address a particular device.  Must be between 0x01 and 0x7F.\n");
    printf("    data_chan is which data channels to log from a device.  Each sensor has unique 1-letter options.  * will log all.\n");
    printf("    For example '2-0x10-L' looks on channel #2 for a device at 0x10 (VEML7700) and records only the Lux channel.\n\n");
    printf("    integrate_seconds is the duration to average readings.\n");
    printf("    file_name will have data appended to it. ':' cannot appear in the file name.\n\n");
    printf("HARDWARE\n");
    printf("The hardware consists of 2 main pieces: the CP2112 USB-I2C adapter and the TCA9548A multiplexer.  ");
    printf("At the present time only a single multiplexer is supported.  Up to 16 devices are supported.  Devices may all use different integrate_seconds.  ");
    printf("Every SDA and SCL line used will need its own pullup resistor.  That is up to 18 if all 8 channels are used.  (2 for the CP2112 and 2*8 for each output of the CA9548A.)  ");
    printf("1k-10k ohms is recommended.  (Standard mode is usually fine with 10k.  Fast mode will do better with resistors nearer to 1k.)  Remember to connect the TCA9548A's reset pin to Vcc.\n\n");
    printf("You may add or remove channels at any time by pressing control-c to exit the application.  Edit the channel options and restart the application.  (This is why it appends to the data file.)\n\n");
    printf("The output file is tab-separated with the following columns:\n");
    printf("    human readable time\n");
    printf("    seconds since epoch (use this for graphing)\n");
    printf("    average\n");
    printf("    standard deviation (stability of output or graph line thickness)\n");
    printf("    minimum\n");
    printf("    maximum\n");
    printf("    number of samples in the average\n");
    printf("    several fields for debugging: device settings, error count, error messages\n\n");
    printf("Currently supported sensors:\n");
    printf("    VEML7700: lux\n");
    printf("    LTR390UV: lux, UVB\n");
    printf("    MLX90614: IR temperature, ambient temperature\n\n");
    printf("Pending sensors: TCS34725 (RGB), INA226 (current and voltage), accelerometer, time of flight, mass, thermocouple, magnetometer.  Or other high-quality sensors that you ask for.\n");
    return 0;
}

int parse_args(struct sensor_state sensors[MAX_SENSORS], int argc, char *argv[])
{
    // returns the number of channels
    // channel-0xaddress-mode:integrate_seconds:file_name
    int res, i, channel, address, duration, count, t;
    char mode;
    char *name;
    count = 0;
    t = (int)time(NULL);
    for (i=1; i<argc; i++) {
        if (strlen(argv[i]) < 1) {
            continue;
        }
        if (argv[i][0] == '-') {
            continue;
        }
        if (count > MAX_SENSORS) {
            printf("%i sensor limit exceeded.\n", MAX_SENSORS);
            continue;
        }

        res = sscanf(argv[i], "*-%x-%c:%u:%ms", &address, &mode, &duration, &name);
        if (res == 4) {
            channel = MAIN_CHANNEL;
        } else {
            res = sscanf(argv[i], "%u-%x-%c:%u:%ms", &channel, &address, &mode, &duration, &name);
            if (res == 5) {
                if (channel<0 || channel>7) {
                    printf("Channel '%i' outside of 0-7 range.\n", channel);
                    continue;
                }
            } else {
                printf("Could not parse '%s'\n", argv[i]);
                continue;
            }
        }
        if (address<1 || address>127) {
            printf("Address '0x%X' outside of 1-127 range.\n", address);
            continue;
        }
        if (duration < 1) {
            if (channel == MAIN_CHANNEL) {
                printf("Sensor '*-0x%X-%c' duration set to 1 instead of %i.\n", address, mode, duration);
            } else {
                printf("Sensor '%i-0x%X-%c' duration set to 1 instead of %i.\n", channel, mode, address, duration);
            }
            duration = 1;
        }
        sensors[count].channel = channel;
        sensors[count].address = address;
        sensors[count].mode = mode;
        sensors[count].report_interval = duration;
        sensors[count].file_name = name;
        //sensors[count].next_report_time = time(NULL) + duration;
        sensors[count].next_report_time = (1 + t/duration) * duration;
        count++;
    }
    return count;
}

int probe_address(hid_device *handle, int address)
{
    // the enable line is set outside of this
    // returns a device_list enum of what is detected at the address
    if (tca9548a_check(handle, address, false)) {
	return TCA9548A;
    }
    if (veml7700_check(handle, address, false)) {
	return VEML7700;
    }
    if (ltr390uv_check(handle, address, false)) {
	return LTR390UV;
    }
    if (mlx90614_check(handle, address, false)) {
        return MLX90614;
    }
    return END_SENSOR_LIST;
}

int tca9548a_scan(hid_device *handle)
{
    int address, r;
    for (address=1; address<=127; address++) {
        r = tca9548a_check(handle, address, false);
        if (r) {
            return address;
        }
    }
    return -1;
}

int perform_scan(hid_device *handle)
{
    int enable, address, r;
    int main_bus[128];
    char *sensor_options[4];
    sensor_options[TCA9548A] = tca9548a_mode_help;
    sensor_options[VEML7700] = veml7700_mode_help;
    sensor_options[LTR390UV] = ltr390uv_mode_help;
    sensor_options[MLX90614] = mlx90614_mode_help;
    printf("Scanning for devices....\n");
    for (enable=-1; enable<=7; enable++) {
        r = channel_select(handle, enable);
        for (address=1; address<=127; address++) {
            if (enable != MAIN_CHANNEL && main_bus[address] != END_SENSOR_LIST) {
                continue;
            }
            r = probe_address(handle, address);
            if (enable == MAIN_CHANNEL) {
                main_bus[address] = r;
            }
            if (r != END_SENSOR_LIST) {
                // it would be nice if this also got a reading from each sensor
                printf("%c-0x%X = %s    %s\n", pretty_channel(enable), address, device_names[r], sensor_options[r]);
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    //(void)argc;
    //(void)argv;
    int i, res, total_channels, err;
    hid_device *handle;

    struct sensor_state sensors[MAX_SENSORS];
    struct sensor_state *sensor;
    struct timespec ts_sensor, ts_pass;
    struct timespec ts_io_1, ts_io_2;

    if (argc == 1) {
        return show_help();
    }
    if (has_arg("-h", argc, argv) || has_arg("--help", argc, argv)) {
        return show_help();
    }

    if (hid_version()->major == HID_API_VERSION_MAJOR && hid_version()->minor == HID_API_VERSION_MINOR && hid_version()->patch == HID_API_VERSION_PATCH) {
    }
    else {
        printf("Warning: compile-time version is different than runtime version of hidapi.\n\n");
    }

    if (hid_init()) {
        printf("Unable to initialize USB.\n");
        return -1;
    }

    // serial number could be a way to handle multiple boards
    handle = hid_open(CP2112_VID, CP2112_PID, NULL);
    if (!handle) {
        printf("Unable to open CP2112.\n");
        return 1;
    }

    // a normal device reset isn't compatible with windows?
    // maybe cancelling is good enough
    cancel_transfer(handle);

    res = setup_gpio(handle, !has_arg("--noblink", argc, argv));
    if (res < 0) {
        printf("Unable to configure GPIO.\n");
        return 1;
    }

    if (has_arg("--slow", argc, argv)) {
        res = setup_i2c(handle, I2C_SLOW_SPEED);
    } else if (has_arg("--fast", argc, argv)) {
        res = setup_i2c(handle, I2C_FAST_SPEED);
    } else {
        res = setup_i2c(handle, I2C_NORMAL_SPEED);
    }
    if (res < 0) {
        printf("Unable to configure I2C.\n");
        return 1;
    }

    // find the multiplexer
    tca9548a_device.address = tca9548a_scan(handle);

    if (has_arg("--scan", argc, argv)) {
        perform_scan(handle);
        return cleanup(handle);
    }

    init_status(sensors);
    total_channels = parse_args(sensors, argc, argv);
    if (total_channels < 1) {
        printf("No inputs were specified.\n\n");
        cleanup(handle);
        return show_help();
    }

    err = 0;
    // figure out what hardware is actually at the specified location
    for (i=0; i<total_channels; i++) {
        channel_select(handle, sensors[i].channel);
        res = probe_address(handle, sensors[i].address);
        sensors[i].hw = res;
        switch (res) {
            case VEML7700:
                if (veml7700_process_mode(&sensors[i].veml7700_sensor, sensors[i].mode)) {
                    printf("%s\n", veml7700_mode_help);
                    err = 1;
                }
                sensors[i].wait_until = &sensors[i].veml7700_sensor.wait_until;
                break;
            case LTR390UV:
                if (ltr390uv_process_mode(&sensors[i].ltr390uv_sensor, sensors[i].mode)) {
                    printf("%s\n", ltr390uv_mode_help);
                    err = 1;
                }
                sensors[i].wait_until = &sensors[i].ltr390uv_sensor.wait_until;
                break;
            case MLX90614:
                sensors[i].mlx90614_sensor.address = sensors[i].address;
                if (mlx90614_process_mode(&sensors[i].mlx90614_sensor, sensors[i].mode)) {
                    printf("%s\n", mlx90614_mode_help);
                    err = 1;
                }
                sensors[i].wait_until = &sensors[i].mlx90614_sensor.wait_until;
                break;
            default:
                printf("Could not detect an i2c device at ");
                if (sensors[i].channel == MAIN_CHANNEL) {
                    printf("*");
                } else {
                    printf("%i", sensors[i].channel);
                }
                printf("-0x%X\n", sensors[i].address);
                err = 1;
                break;
        }
        if (err) {
            channel_select(handle, NO_CHANNEL);
            cleanup(handle);
            return 1;
        }
    }

    for (i=0; i<MAX_SENSORS; i++) {
        if (sensors[i].file_name) {
            maybe_header(&sensors[i]);
        }
    }

    signal(SIGINT, exit_handler);
    printf("Press control-c at any time to stop data collection and change the channel configuration.\n");

    // config
    for (i=0; i<MAX_SENSORS; i++) {
        sensor = &sensors[i];
        switch (sensor->hw) {
            case VEML7700:
                res = veml7700_setup(handle, &sensor->veml7700_sensor, 1);
                veml7700_tick(&sensor->veml7700_sensor);
                usleep(2500);
                break;
            case LTR390UV:
                res = setup_ltr390uv(handle, &sensor->ltr390uv_sensor, 1);
                break;
            case MLX90614:
                break;
        }
        if (res < 0) {
            channel_select(handle, NO_CHANNEL);
            sensor->error = "bad conf";
            sensor->errors += 1;
            continue;
        }
    }

    while (!force_exit) {
        if (ts_pass.tv_sec == 0L) {
            clock_gettime(CLOCK_REALTIME, &ts_pass);
        }
        i = next_sensor2(sensors);
        if (i<0) {
            usleep(-i * 1000);
            continue;
        }

        sensor = &sensors[i];
        clock_gettime(CLOCK_REALTIME, &ts_sensor);
        channel_select(handle, sensor->channel);
        switch (sensor->hw) {
            case VEML7700:
                res = veml7700_read(handle, &sensor->veml7700_sensor);
                break;
            case LTR390UV:
                res = ltr390uv_read(handle, &sensor->ltr390uv_sensor);
                break;
            case MLX90614:
                res = mlx90614_read(handle, &sensor->mlx90614_sensor);
                break;
        }
        sensor->read_ns += tick_elapsed_ns(&ts_sensor);
        sensor->pass_ns += tick_elapsed_ns(&ts_pass);
        ts_pass.tv_sec = 0L;
        sensor->readings++;
        if (res < 0) {
            //channel_select(handle, NO_CHANNEL);
            sensor->error = "bad read";
            sensor->errors++;
            sensor->readings--;
            continue;
        }

        if (force_exit) {
            break;
        }

        clock_gettime(CLOCK_REALTIME, &ts_io_1);
        //printf("ch: %i   lux: %.3f    unfiltered: %.3f    raw: %i    int: %ims    gain: %s\n", sensor->channel, sensor->recent_lux, sensor->recent_unf, sensor->recent_raw, veml7700_int_ms[sensor->integration], veml7700_g_str[sensor->gain]);
        show_status(sensors);
        maybe_log(sensor, 0);
        //channel_select(handle, NO_CHANNEL);
        clock_gettime(CLOCK_REALTIME, &ts_io_2);
    }

    for (i=0; i<MAX_SENSORS; i++) {
        sensor = &sensors[i];
        if (sensor->file_name) {
            maybe_log(sensor, 1);
        }
    }

    channel_select(handle, NO_CHANNEL);
    cleanup(handle);
    return 0;
}

