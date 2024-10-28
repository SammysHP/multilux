#include <mosquitto.h>

#define MQTT_KEEPALIVE_SEC 60

int mqtt_send(
    struct mosquitto *mosq,
    const char *topic,
    const int msglen, const char *msg)
{
    if (!mosq) return 0;
    return mosquitto_publish(mosq, NULL, topic, msglen, msg, 0, 0) == MOSQ_ERR_SUCCESS;
}

void mqtt_connect_callback(
    struct mosquitto *mosq, void *obj, int rc)
{
    if (rc == 0) {
        fprintf(stderr, "Connection to MQTT broker established\n");
    } else {
        fprintf(stderr, "Failed to connect to MQTT broker\n");
    }
}

void mqtt_disconnect_callback(
    struct mosquitto *mosq, void *obj, int rc)
{
    fprintf(stderr, "Connection to MQTT broker closed\n");
}

struct mosquitto* mqtt_connect(
    const char *host, const int port,
    const char *username, const char *password,
    const int use_tls)
{
    struct mosquitto *mosq = NULL;

    if (!(mosq = mosquitto_new(NULL, 1, NULL))) {
        fprintf(stderr, "Failed to initialize mosquitto\n");
        goto out_error;
    }

    if (username && password) {
        if (mosquitto_username_pw_set(mosq, username, password) != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Failed to set username/password\n");
            goto out_error;
        }
    }

    mosquitto_connect_callback_set(mosq, mqtt_connect_callback);

    mosquitto_disconnect_callback_set(mosq, mqtt_disconnect_callback);

    if (use_tls) {
        mosquitto_tls_set(mosq, NULL, "/etc/ssl/certs", NULL, NULL, NULL);
        mosquitto_tls_insecure_set(mosq, true);
        mosquitto_tls_opts_set(mosq, 0, NULL, NULL);
    }

    if (mosquitto_loop_start(mosq) != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to start mosquitto loop\n");
        goto out_error;
    }

    fprintf(
        stderr,
        "Connecting to MQTT broker at %s:%d%s\n",
        host,
        port,
        (use_tls ? " using TLS" : ""));

    if (mosquitto_connect_async(mosq, host, port, MQTT_KEEPALIVE_SEC) == MOSQ_ERR_INVAL) {
        fprintf(stderr, "Invalid MQTT connection settings\n");
        goto out_error;
    }

    return mosq;

out_error:
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    return NULL;
}
