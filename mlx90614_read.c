#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#include <hidapi.h>
#include "cp2112.h"
#include "mlx90614.h"
#include "mqtt.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

#define MQTT_MSG_BUFLEN 512

volatile int force_exit;

void exit_handler(int sig_num)
{
    force_exit = 1;
}

double compute_celsius(int n)
{
    double c = (double)n * MLX_DEG_PER_COUNT;
    return (c - 273.15) + MLX_VDD_OFFSET_DEGREES;
}

int main(int argc, char *argv[])
{
    int res, raw_ir;
    hid_device *handle;

    mosquitto_lib_init();

    if (hid_version()->major != HID_API_VERSION_MAJOR || hid_version()->minor != HID_API_VERSION_MINOR || hid_version()->patch != HID_API_VERSION_PATCH) {
        fprintf(stderr, "Warning: compile-time version is different than runtime version of hidapi.\n\n");
    }

    if (hid_init()) {
        fprintf(stderr, "Unable to initialize USB.\n");
        return 1;
    }

    // serial number could be a way to handle multiple boards
    handle = hid_open(CP2112_VID, CP2112_PID, NULL);
    if (!handle) {
        fprintf(stderr, "Unable to open CP2112.\n");
        return 1;
    }

    // a normal device reset isn't compatible with windows?
    // maybe cancelling is good enough
    cancel_transfer(handle);

    res = setup_gpio(handle, 1);
    if (res < 0) {
        fprintf(stderr, "Unable to configure GPIO.\n");
        return 1;
    }

    res = setup_i2c(handle, I2C_NORMAL_SPEED);
    if (res < 0) {
        fprintf(stderr, "Unable to configure I2C.\n");
        return 1;
    }

    raw_ir = read_word(handle, MLX90614_ADDRESS, T_AMB, 3);
    if (raw_ir <= 0) {
        fprintf(stderr, "MLX90614 thermal sensor not detected.\n");
        return 1;
    }

    struct mosquitto *mosq = mqtt_connect(
        "localhost", 1883,
        NULL, NULL,
        0);

    signal(SIGINT, exit_handler);

    while (!force_exit) {
        raw_ir = read_word(handle, MLX90614_ADDRESS, T_AMB, 3);
        double t_amb = compute_celsius(raw_ir & 0xFFFF);
        raw_ir = read_word(handle, MLX90614_ADDRESS, T_OBJ1, 3);
        double t_obj = compute_celsius(raw_ir & 0xFFFF);

        struct timeval tv;
        gettimeofday(&tv, NULL);
        const double timestamp = tv.tv_sec + tv.tv_usec / 1e6;

        printf(
            "%.6f\t"
            "%.2f\t"
            "%.2f\n",
            timestamp,
            t_amb,
            t_obj);
        fflush(stdout);

        char mqtt_msg[MQTT_MSG_BUFLEN];
        snprintf(mqtt_msg, MQTT_MSG_BUFLEN,
            "{"
                "\"timestamp\": %.6f, "
                "\"ambient\": %.2f, "
                "\"object\": %.2f"
            "}",
            timestamp,
            t_amb,
            t_obj);
        mqtt_send(
            mosq,
            "/multilux/mlx90614",
            strlen(mqtt_msg), mqtt_msg);

        usleep(1 * 1000 * 1000);
    }

    hid_close(handle);
    hid_exit();

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
