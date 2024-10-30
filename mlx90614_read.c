#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

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

struct cmdargs
{
    char *host;
    int port;
    char *username;
    char *password;
    char *topic;
    double interval;
    bool tls;
    bool quiet;
};

void exit_handler(int sig_num)
{
    force_exit = 1;
}

double compute_celsius(int n)
{
    double c = (double)n * MLX_DEG_PER_COUNT;
    return (c - 273.15) + MLX_VDD_OFFSET_DEGREES;
}

bool parse_arguments(int argc, char **argv, struct cmdargs *args)
{
    const static struct option long_options[] = {
        {"host",                required_argument, 0, 'h' },
        {"port",                required_argument, 0, 'p' },
        {"username",            required_argument, 0, 'u' },
        {"password",            required_argument, 0, 'w' },
        {"topic",               required_argument, 0, 't' },
        {"interval",            required_argument, 0, 'i' },
        {"tls",                 no_argument,       0, 's' },
        {"quiet",               no_argument,       0, 'q' },
        {0, 0, 0, 0}
    };

  while (true) {
      const int opt = getopt_long(argc, argv, "h:p:u:w:t:i:sq", long_options, NULL);

      if (opt == -1) break;

      switch (opt) {
          case 'h':
              {
                  if (!optarg || !strlen(optarg)) return false;
                  args->host = strdup(optarg);
                  break;
              }

          case 'p':
              {
                  char *end;
                  const long port = strtol(optarg, &end, 10);
                  if (*end != '\0' || end == optarg || port < 0 || port > 65535) return false;
                  args->port = port;
                  break;
              }

          case 'u':
              {
                  if (!optarg || !strlen(optarg)) return false;
                  args->username = strdup(optarg);
                  break;
              }

          case 'w':
              {
                  if (!optarg || !strlen(optarg)) return false;
                  args->password = strdup(optarg);
                  break;
              }

          case 't':
              {
                  if (!optarg || !strlen(optarg)) return false;
                  args->topic = strdup(optarg);
                  break;
              }

          case 'i':
              {
                  char *end;
                  const double interval = strtod(optarg, &end);
                  if (*end != '\0' || end == optarg || interval <= 0) return false;
                  args->interval = interval;
                  break;
              }

          case 's':
              {
                  args->tls = true;
                  break;
              }

          case 'q':
              {
                  args->quiet = true;
                  break;
              }

          default:
              return false;
        }
  }

  return true;
}

int main(int argc, char *argv[])
{
    int res, raw_ir;

    struct cmdargs args = {
        .host = "",
        .port = 1883,
        .username = NULL,
        .password = NULL,
        .topic = "multilux/mlx90614",
        .interval = 1.0,
        .tls = false,
        .quiet = false,
    };

    if (!parse_arguments(argc, argv, &args)) {
        fprintf(stderr, "Failed to parse arguments.\n");
        exit(EXIT_FAILURE);
    }

    mosquitto_lib_init();

    if (hid_version()->major != HID_API_VERSION_MAJOR || hid_version()->minor != HID_API_VERSION_MINOR || hid_version()->patch != HID_API_VERSION_PATCH) {
        fprintf(stderr, "Warning: compile-time version is different than runtime version of hidapi.\n\n");
    }

    if (hid_init()) {
        fprintf(stderr, "Unable to initialize USB.\n");
        return 1;
    }

    // serial number could be a way to handle multiple boards
    hid_device *handle = hid_open(CP2112_VID, CP2112_PID, NULL);
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

    struct mosquitto *mosq = NULL;

    if (*args.host) {
        mosq = mqtt_connect(
            args.host, args.port,
            args.username, args.password,
            args.tls);
    }

    signal(SIGINT, exit_handler);

    while (!force_exit) {
        raw_ir = read_word(handle, MLX90614_ADDRESS, T_AMB, 3);
        double t_amb = compute_celsius(raw_ir & 0xFFFF);
        raw_ir = read_word(handle, MLX90614_ADDRESS, T_OBJ1, 3);
        double t_obj = compute_celsius(raw_ir & 0xFFFF);

        struct timeval tv;
        gettimeofday(&tv, NULL);
        const double timestamp = tv.tv_sec + tv.tv_usec / 1e6;

        if (!args.quiet) {
            printf(
                "%.6f\t%.2f\t%.2f\n",
                timestamp, t_amb, t_obj);
            fflush(stdout);
        }

        if (mosq) {
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
                args.topic,
                strlen(mqtt_msg), mqtt_msg);
        }

        usleep(args.interval * 1000 * 1000);
    }

    hid_close(handle);
    hid_exit();

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
