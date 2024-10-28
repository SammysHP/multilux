#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#include <hidapi.h>
#include "cp2112.h"
#include "veml7700.h"
#include "mlx90614.h"
#include "mqtt.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

#define MQTT_TOPIC_BUFLEN 64
#define MQTT_MSG_BUFLEN 512
#define MQTT_MEAN_BUFLEN 32

struct sensor_state
{
    // hardware things
    int channel;
    enum veml7700_gain gain;
    enum veml7700_integration integration;
    // caching
    int recent_raw;
    double recent_lux;
    double recent_unf;
    int mlx_address;
    double t_amb;
    double t_obj;
    // statistic things
    int readings;
    double als_sum;
    double als_squares;
    double unf_sum;
    double unf_squares;
    // logging things
    long int next_report_time;
    int report_interval;
    char *file_name;
    char *error;
    int errors;
    int zero_halt;
};

volatile int force_exit;

struct mosquitto *mosq = NULL;

void exit_handler(int sig_num)
{
    printf("\nSaving data and cleaning up connections....\n");
    fflush(stdout);
    force_exit = 1;
}

int init_status(struct sensor_state sensors[8])
{
    int i;
    for (i=0; i<8; i++) {
        sensors[i].channel = -1;
        sensors[i].gain = ALS_GAIN_8DIV;
        sensors[i].integration = ALS_IT_100ms;
        sensors[i].file_name = NULL;
        sensors[i].error = "";
        sensors[i].errors = 0;
        sensors[i].readings = 0;
        sensors[i].als_sum = 0;
        sensors[i].als_squares = 0;
        sensors[i].unf_sum = 0;
        sensors[i].unf_squares = 0;
        sensors[i].recent_raw = 0;
        sensors[i].recent_lux = 0;
        sensors[i].recent_unf = 0;
        sensors[i].zero_halt = 0;
        sensors[i].mlx_address = 0;
        sensors[i].t_amb = NO_TEMPERATURE;
        sensors[i].t_obj = NO_TEMPERATURE;
    }
    return 0;
}

int channel_select(hid_device *handle, int channel)
{
    if (channel == -1) {
        return set_gpio(handle, 0x00, 0xFC);
    }
    if (channel < 2) {
        return -1;
    }
    if (channel > 7) {
        return -1;
    }
    return set_gpio(handle, 1<<channel, 0xFC);
}

int setup_veml7700(hid_device *handle, enum veml7700_gain g, enum veml7700_integration i)
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

int autoscale(struct sensor_state *sensor, int raw)
{
    // returns true if another sample is needed
    // their sample algo leaves much to be desired
    // the core goal is to have 100ms integration time
    // play with gain as the primary adjustment
    int i = veml7700_int_ms[sensor->integration];
    if (i>100 && raw>200 && raw<10000) {
        sensor->integration = less_int[sensor->integration];
        if (sensor->gain != ALS_GAIN_2X) {
            sensor->gain = more_gain[sensor->gain];
        }
        return 1;
    }
    if (i<100 && raw>200 && raw<10000) {
        sensor->integration = more_int[sensor->integration];
        if (sensor->gain != ALS_GAIN_8DIV) {
            sensor->gain = less_gain[sensor->gain];
        }
        return 1;
    }
    if (raw > 100 && raw < 10000) {
        return 0;
    }
    if (raw <= 100 && more_gain[sensor->gain] != -1) {
        sensor->gain = more_gain[sensor->gain];
        return 1;
    }
    if (raw <= 100 && more_int[sensor->integration] != -1) {
        sensor->integration = more_int[sensor->integration];
        return 1;
    }
    if (raw >= 10000 && less_gain[sensor->gain] != -1) {
        sensor->gain = less_gain[sensor->gain];
        return 1;
    }
    if (raw >= 10000 && less_int[sensor->integration] != -1) {
        sensor->integration = less_int[sensor->integration];
        return 1;
    }
    return 0;
}

int compute_lux(struct sensor_state *sensor, int raw, int raw_unf)
{
    double lux, unfiltered;
    lux = (double)raw * veml7700_i_scale[sensor->integration] * veml7700_g_scale[sensor->gain];
    lux = smooth_lux_correction(lux);
    //unfiltered = read_word(handle, VEML7700_ADDR, UNFILTERED_DATA, 2);
    unfiltered = (double)raw_unf * veml7700_i_scale[sensor->integration] * veml7700_g_scale[sensor->gain];
    unfiltered = smooth_lux_correction(unfiltered);

    sensor->recent_raw = raw;
    sensor->recent_lux = lux;
    sensor->recent_unf = unfiltered;

    sensor->readings += 1;
    sensor->als_sum += lux;
    sensor->als_squares += pow(lux, 2);
    sensor->unf_sum += unfiltered;
    sensor->unf_squares += pow(unfiltered, 2);
    return 0;
}

double compute_celsius(int n)
{
    double c = (double)n * MLX_DEG_PER_COUNT;
    return (c - 273.15) + MLX_VDD_OFFSET_DEGREES;
}

int next_sensor(struct sensor_state sensors[8])
{
    // a fair scheduling algo
    // tries to give each sensor an equal number of reads per report_interval
    int i, best_i, elapsed;
    time_t t = time(NULL);
    double reads, score, best_score;
    struct sensor_state *sensor;
    best_i = -1;
    best_score = 1e10;
    for (i=7; i>=0; i--) {
        sensor = &sensors[i];
        if (sensor->channel < 0) {
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

int show_status(struct sensor_state sensors[8])
{
    char item[25];
    int i;
    struct sensor_state *s;
    printf("\r");
    for (i=0; i<8; i++) {
        s = &sensors[i];
        if (s->channel == -1) {
            continue;
        }
        if (s->zero_halt) {
            snprintf(item, 24, "%i: DONE", s->channel);
        } else if (strlen(s->error)) {
            snprintf(item, 24, "%i: %s", s->channel, s->error);
        } else if (s->mlx_address) {
            snprintf(item, 24, "%i: %.2flx %.2fC", s->channel, s->recent_lux, s->t_obj);
        } else {
            snprintf(item, 24, "%i: %.2flx", s->channel, s->recent_lux);
        }
        printf("%-24s", item);
    }
    fflush(stdout);
    return 0;
}

int maybe_log(struct sensor_state *sensor, int force)
{
    FILE *f;
    time_t t;
    struct timeval tv;
    double mean = -1, stddev;
    const char *g;
    int i;
    char fulltime[30];
    char mqtt_topic[MQTT_TOPIC_BUFLEN];
    char mqtt_msg[MQTT_MSG_BUFLEN];
    char als_mean_s[MQTT_MEAN_BUFLEN] = "null";
    char als_stddev_s[MQTT_MEAN_BUFLEN] = "null";
    char unf_mean_s[MQTT_MEAN_BUFLEN] = "null";
    char unf_stddev_s[MQTT_MEAN_BUFLEN] = "null";

    // has enough time elapsed?
    // time() and gettimeofday() return slightly different seconds
    t = time(NULL);
    gettimeofday(&tv, NULL);
    const double timestamp = tv.tv_sec + tv.tv_usec / 1e6;
    if (!force && sensor->next_report_time > t) {
        return 0;
    }

    f = fopen(sensor->file_name, "a");
    if (f == NULL) {
        sensor->error = "bad file";
        return -1;
    }
    strftime(fulltime, 30, "%a %b %d %H:%M:%S %Y", localtime(&t));
    fprintf(f, "%s\t%ld\t", fulltime, t);

    if (sensor->readings) {
        mean = sensor->als_sum / (double)sensor->readings;
        stddev = sqrt(fmax(0.0, sensor->als_squares / (double)sensor->readings - pow(mean, 2)));
        fprintf(f, "%.4f\t%.4f\t", mean, stddev);
        snprintf(als_mean_s,   MQTT_MEAN_BUFLEN, "%.4f", mean);
        snprintf(als_stddev_s, MQTT_MEAN_BUFLEN, "%.4f", stddev);
    } else {
        fprintf(f, "\t\t");
    }
   
    // not the best place for this but the average is here
    if (mean == 0.0 && sensor->gain == ALS_GAIN_2X && sensor->integration == ALS_IT_800ms) {
        sensor->zero_halt = 1;
    }
 
    if (sensor->readings) {
        mean = sensor->unf_sum / (double)sensor->readings;
        stddev = sqrt(fmax(0.0, sensor->unf_squares / (double)sensor->readings - pow(mean, 2)));
        fprintf(f, "%.4f\t%.4f\t%i\t", mean, stddev, sensor->readings);
        snprintf(unf_mean_s,   MQTT_MEAN_BUFLEN, "%.4f", mean);
        snprintf(unf_stddev_s, MQTT_MEAN_BUFLEN, "%.4f", stddev);
    } else {
        fprintf(f, "\t\t%i\t", sensor->readings);
    }

    if (sensor->t_amb > NO_TEMPERATURE) {
        fprintf(f, "%.2f\t%.2f\t", sensor->t_amb, sensor->t_obj);
        snprintf(mqtt_msg, MQTT_MSG_BUFLEN,
            "{"
                "\"timestamp\": %.6f, "
                "\"ambient\": %.2f, "
                "\"object\": %.2f"
            "}",
            timestamp,
            sensor->t_amb,
            sensor->t_obj);
        mqtt_send(
            mosq,
            "/multilux/mlx90614",
            strlen(mqtt_msg), mqtt_msg);
    } else {
        fprintf(f, "\t\t");
    }

    g = veml7700_g_str[sensor->gain];
    i = veml7700_int_ms[sensor->integration];
    fprintf(f, "%s\t%ims\t%s\n", g, i, sensor->error);

    snprintf(mqtt_msg, MQTT_MSG_BUFLEN,
        "{"
            "\"timestamp\": %.6f, "
            "\"als_mean\": %s, "
            "\"als_stddev\": %s, "
            "\"unf_mean\": %s, "
            "\"unf_stddev\": %s, "
            "\"readings\": %d, "
            "\"gain\": \"%s\", "
            "\"integration\": %d, "
            "\"error\": \"%.50s\""
        "}",
        timestamp,
        als_mean_s,
        als_stddev_s,
        unf_mean_s,
        unf_stddev_s,
        sensor->readings,
        g,
        i,
        sensor->error);
    snprintf(mqtt_topic, MQTT_TOPIC_BUFLEN, "/multilux/veml7700/%d", sensor->channel);
    mqtt_send(
        mosq,
        mqtt_topic,
        strlen(mqtt_msg), mqtt_msg);

    // clean up
    fclose(f);
    sensor->next_report_time += sensor->report_interval;
    sensor->readings = 0;
    sensor->als_sum = 0;
    sensor->als_squares = 0;
    sensor->unf_sum = 0;
    sensor->unf_squares = 0;
    sensor->error = "";
    sensor->errors = 0;
    sensor->t_amb = NO_TEMPERATURE;
    sensor->t_obj = NO_TEMPERATURE;

    return 0;
}

int exists(char *file_name)
{
    struct stat buf;
    return !stat(file_name, &buf);
}

int maybe_header(char *file_name)
{
    FILE *f;
    if (exists(file_name)) {
        return 0;
    }
    f = fopen(file_name, "a");
    if (f == NULL) {
        return -1;
    }
    fprintf(f, "full time\tseconds\tlux mean\tlux stddev\tunfiltered mean\tunf stddev\treadings\tambient C\tobject C\tgain\tintegration\terror\n");
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
    printf("multilux [--noblink] [--slow] channel_number:integrate_seconds:file_name.tsv [more channels]\n\n");
    printf("    --noblink disables the indicator LEDs.\n");
    printf("    --slow runs I2C at 20kHz instead of 100kHz.\n\n");
    printf("    channel_number is the GPIO that enables a particular sensor.  Must be between 2 and 7.\n");
    printf("    integrate_seconds is the duration to average readings.\n");
    printf("    file_name will have data appended to it. ':' cannot appear in the file name.\n\n");
    printf("Channels may all use different integrate_seconds.\n\n");
    printf("Up to 6 channels are supported.  Connect the '3Vo' pin on the VEML7700 to a GPIO of the CP2112.\n\n");
    printf("GPIO 5/6/7 are labeled on the CP2112.  GPIO2=WAK, GPIO3=INT, GPIO4=RST.  GPIO 0 and 1 are hardwired to the LEDs and unavailable.\n\n");
    printf("Temperature readings require an MLX90614 sensor.  If it is detected it is assumed to be part of channel #2.  The power for the MLX90614 should be connected to the CP2112's VCC rail.  Not to any GPIO!  (Powering it from a GPIO will create a short circuit.)\n\n");
    printf("You may add or remove channels at any time by pressing control-c to exit the application.  Edit the channel options and restart the application.  (This is why it appends to the data file.)\n\n");
    printf("The output file is tab-separated with the following columns:\n");
    printf("    human readable time\n");
    printf("    seconds since epoch (for graphing the runtime)\n");
    printf("    lux average (for graphing the runtime)\n");
    printf("    lux standard deviation (stability of output or graph line thickness)\n");
    printf("    unfiltered average (useful for IR lights)\n");
    printf("    unfiltered standard deviation\n");
    printf("    number of samples in the average\n");
    printf("    the ambient and object temperatures\n");
    printf("    several fields for debugging: gain, integration, error messages\n");
    return 0;
}

int parse_args(struct sensor_state sensors[8], int argc, char *argv[])
{
    // returns the number of channels
    // channel_number:integrate_seconds:file_name
    int res, i, channel, duration, count;
    char *name;
    count = 0;
    for (i=1; i<argc; i++) {
        if (strlen(argv[i]) < 1) {
            continue;
        }
        if (argv[i][0] == '-') {
            continue;
        }

        res = sscanf(argv[i], "%d:%d:%ms", &channel, &duration, &name);
        if (res != 3) {
            printf("Could not parse '%s'\n", argv[i]);
            continue;
        }
        if (channel<2 || channel>7) {
            printf("Channel %i outside of 2-7 range.\n", channel);
            continue;
        }
        if (duration < 1) {
            printf("Channel %i duration set to 1 instead of %i.\n", channel, duration);
            duration = 1;
        }
        sensors[channel].channel = channel;
        sensors[channel].report_interval = duration;
        sensors[channel].file_name = name;
        sensors[channel].next_report_time = time(NULL) + duration;
        count++;
    }
    return count;
}

int main(int argc, char *argv[])
{
    //(void)argc;
    //(void)argv;
    int i, res, raw_lux, raw_unf, raw_ir, total_channels;
    hid_device *handle;

    struct sensor_state sensors[8];
    struct sensor_state *sensor;

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
    } else {
        res = setup_i2c(handle, I2C_NORMAL_SPEED);
    }
    if (res < 0) {
        printf("Unable to configure I2C.\n");
        return 1;
    }

    init_status(sensors);
    total_channels = parse_args(sensors, argc, argv);
    if (total_channels < 1) {
        printf("No inputs were specified.\n\n");
        return show_help();
    }

    raw_ir = read_word(handle, MLX90614_ADDRESS, T_AMB, 3);
    if (raw_ir > 0) {
        printf("MLX90614 thermal sensor detected and applied to channel 2.\n");
        sensors[2].mlx_address = MLX90614_ADDRESS;
    }

    for (i=0; i<8; i++) {
        if (sensors[i].file_name) {
            maybe_header(sensors[i].file_name);
        }
    }

    mosquitto_lib_init();

    // TODO Read from command line
    mosq = mqtt_connect(
        "localhost", 1883,
        NULL, NULL,
        0);

    signal(SIGINT, exit_handler);
    printf("Press control-c at any time to pause data collection and change the channel configuration.\n");

    while (!force_exit) {
        i = next_sensor(sensors);
        if (i<0) {
            break;
        }
        sensor = &sensors[i];
        channel_select(handle, sensor->channel);
        usleep(3*1000);  // wait for GPIO to stabilize
        res = setup_veml7700(handle, sensor->gain, sensor->integration);
        if (res < 0) {
            sensor->error = "bad lux conf";
            sensor->errors += 1;
            continue;
        }
        // wait for integration and refresh
        //usleep((veml7700_int_ms[sensor->integration] + 500) * 1000);
        // the stock 500mS seems like overkill
        // 1.5 integrations seems fine
        usleep(veml7700_int_ms[sensor->integration] * 1000 * 3 / 2);

        cancel_transfer(handle);
        raw_lux = read_word(handle, VEML7700_ADDR, ALS_DATA, 2);
        cancel_transfer(handle);
        raw_unf = read_word(handle, VEML7700_ADDR, UNFILTERED_DATA, 2);
        if (raw_lux < 0 || raw_unf < 0) {
            sensor->error = "bad lux read";
            sensor->errors += 1;
        } else {
            compute_lux(sensor, raw_lux, raw_unf);
        }

        // maybe grab a temperature reading
        // hopefully the previous I2C comms didn't mess up the temperature reading
        if (sensor->mlx_address && sensor->readings == 1 && sensor->t_amb == NO_TEMPERATURE) {
            raw_ir = read_word(handle, sensor->mlx_address, T_AMB, 3);
            sensor->t_amb = compute_celsius(raw_ir & 0xFFFF);
            raw_ir = read_word(handle, sensor->mlx_address, T_OBJ1, 3);
            sensor->t_obj = compute_celsius(raw_ir & 0xFFFF);
        }

        if (force_exit) {
            break;
        }

        //printf("ch: %i   lux: %.3f    unfiltered: %.3f    raw: %i    int: %ims    gain: %s\n", sensor->channel, sensor->recent_lux, sensor->recent_unf, sensor->recent_raw, veml7700_int_ms[sensor->integration], veml7700_g_str[sensor->gain]);
        show_status(sensors);
        maybe_log(sensor, 0);

        if (sensor->errors == 0) {
            autoscale(sensor, raw_lux);  // its free when prepped it for the next sample
        }
        channel_select(handle, -1);
    }

    for (i=0; i<8; i++) {
        if (sensors[i].file_name) {
            maybe_log(&sensors[i], 1);
        }
    }

    channel_select(handle, -1);
    hid_close(handle);
    hid_exit();

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
#ifdef _WIN32
    system("pause");
#endif
    return 0;
}

