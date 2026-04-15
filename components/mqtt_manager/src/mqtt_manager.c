#include "mqtt_manager.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"

#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define MQTTMGR_HAS_CRT_BUNDLE 1
#else
#define MQTTMGR_HAS_CRT_BUNDLE 0
#endif

static const char *TAG = "mqtt_manager";

static mqtt_manager_config_t g_cfg;
static esp_mqtt_client_handle_t g_client = NULL;
static bool g_connected = false;
static mqtt_manager_conn_cb_t g_conn_cb = NULL;

static void build_default_client_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // client id: smartlock-<mac>
    snprintf(out, out_len, "smartlock-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_id;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            g_connected = true;
            ESP_LOGI(TAG, "MQTT conectado");
            if (g_conn_cb) g_conn_cb(true);
            break;

        case MQTT_EVENT_DISCONNECTED:
            g_connected = false;
            ESP_LOGW(TAG, "MQTT desconectado");
            if (g_conn_cb) g_conn_cb(false);
            break;

        case MQTT_EVENT_ERROR:
            g_connected = false;
            ESP_LOGE(TAG, "MQTT erro");
            if (g_conn_cb) g_conn_cb(false);
            break;

        default:
            break;
    }
}

bool mqtt_manager_init(const mqtt_manager_config_t *cfg, mqtt_manager_conn_cb_t cb)
{
    if (!cfg || !cfg->broker_uri || !cfg->username || !cfg->password || !cfg->topic_events) {
        ESP_LOGE(TAG, "Config MQTT invalida (campos obrigatorios ausentes).");
        return false;
    }

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg = *cfg;
    g_conn_cb = cb;

    char client_id_buf[48] = {0};
    const char *client_id = g_cfg.client_id;
    if (!client_id) {
        build_default_client_id(client_id_buf, sizeof(client_id_buf));
        client_id = client_id_buf;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_cfg.broker_uri,
        .credentials.username = g_cfg.username,
        .credentials.authentication.password = g_cfg.password,
        .credentials.client_id = client_id,
#if MQTTMGR_HAS_CRT_BUNDLE
        // HiveMQ Cloud -> TLS via bundle de certificados do IDF
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
#endif
        .network.disable_auto_reconnect = false,
    };

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!g_client) {
        ESP_LOGE(TAG, "Falha ao criar cliente MQTT");
        return false;
    }

    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "MQTT init ok. URI=%s Topic=%s QoS=%d",
             g_cfg.broker_uri, g_cfg.topic_events, g_cfg.qos);

    return true;
}

bool mqtt_manager_start(void)
{
    if (!g_client) {
        ESP_LOGE(TAG, "mqtt_manager_start: cliente nao inicializado");
        return false;
    }

    esp_err_t err = esp_mqtt_client_start(g_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar MQTT: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool mqtt_manager_stop(void)
{
    if (!g_client) return true;

    esp_err_t err = esp_mqtt_client_stop(g_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao parar MQTT: %s", esp_err_to_name(err));
        return false;
    }

    g_connected = false;
    return true;
}

bool mqtt_manager_is_connected(void)
{
    return g_connected;
}

/**
 * @brief Publish genérico em qualquer tópico (usado pelo main para logs).
 */
bool mqtt_manager_publish(const char *topic, const char *payload, int qos, bool retain)
{
    if (!g_client || !topic || !payload) return false;
    if (!g_connected) return false;

    int msg_id = esp_mqtt_client_publish(
        g_client,
        topic,
        payload,
        0,               // len 0 => strlen
        qos,
        retain ? 1 : 0
    );

    if (msg_id == -1) {
        ESP_LOGW(TAG, "Publish falhou (msg_id=-1) topic=%s", topic);
        return false;
    }
    return true;
}

/**
 * @brief Mantém compatibilidade: publica no tópico padrão configurado.
 */
bool mqtt_manager_publish_event(const char *json_payload)
{
    if (!g_client || !json_payload) return false;
    if (!g_connected) return false;

    int msg_id = esp_mqtt_client_publish(
        g_client,
        g_cfg.topic_events,
        json_payload,
        0,              // len 0 => strlen
        g_cfg.qos,
        g_cfg.retain
    );

    if (msg_id == -1) {
        ESP_LOGW(TAG, "Publish falhou (msg_id=-1)");
        return false;
    }
    return true;
}
