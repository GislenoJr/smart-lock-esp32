#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *broker_uri;
    const char *username;
    const char *password;
    const char *client_id;     // opcional
    const char *topic_events;  // opcional
    int  qos;
    bool retain;
} mqtt_manager_config_t;

typedef void (*mqtt_manager_conn_cb_t)(bool connected);

// callback quando chegar mensagem (DATA)
typedef void (*mqtt_manager_msg_cb_t)(const char *topic, const char *payload, int payload_len);

bool mqtt_manager_init(const mqtt_manager_config_t *cfg, mqtt_manager_conn_cb_t cb);
bool mqtt_manager_start(void);
bool mqtt_manager_stop(void);

bool mqtt_manager_is_connected(void);

// publish genérico
bool mqtt_manager_publish(const char *topic, const char *payload, int qos, bool retain);

// subscribe
bool mqtt_manager_subscribe(const char *topic, int qos);

// set callback para mensagens recebidas
void mqtt_manager_set_message_callback(mqtt_manager_msg_cb_t cb);

// helper (se você usa)
bool mqtt_manager_publish_event(const char *json_payload);
