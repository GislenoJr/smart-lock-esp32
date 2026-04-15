#include "access_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>   // unlink()

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "mqtt_manager.h"

static const char *TAG = "access_log";

#define MOUNT_POINT          "/spiffs"
#define QUEUE_FILE           MOUNT_POINT "/access_queue.log"
#define QUEUE_FILE_TMP       MOUNT_POINT "/access_queue.tmp"
#define MAX_QUEUE_BYTES      (80 * 1024)   // limite (ajuste conforme necessidade)
#define MAX_LINE_LEN         256

static void uid_to_hex(const uint8_t *uid, size_t len, char *out, size_t out_len) {
    static const char *hex = "0123456789ABCDEF";
    size_t needed = len * 2 + 1;
    if (!out || out_len < needed) {
        if (out && out_len) out[0] = 0;
        return;
    }
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[(uid[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[uid[i] & 0x0F];
    }
    out[len * 2] = 0;
}

static const char* res_str(access_result_t r) {
    return (r == ACCESS_GRANTED) ? "GRANTED" : "DENIED";
}

static size_t file_size_bytes(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (size_t)st.st_size;
}

static void rotate_if_needed(void) {
    size_t sz = file_size_bytes(QUEUE_FILE);
    if (sz > MAX_QUEUE_BYTES) {
        ESP_LOGW(TAG, "Fila excedeu %u bytes. Rotacionando (apagando fila).", (unsigned)MAX_QUEUE_BYTES);
        unlink(QUEUE_FILE);
    }
}

static bool spiffs_mount_once(void) {
    static bool mounted = false;
    if (mounted) return true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = NULL,
        .max_files = 4,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar SPIFFS: %s", esp_err_to_name(ret));
        return false;
    }
    mounted = true;
    ESP_LOGI(TAG, "SPIFFS montado em %s", MOUNT_POINT);
    return true;
}

bool access_log_init(void) {
    if (!spiffs_mount_once()) return false;
    // garante arquivo existir
    FILE *f = fopen(QUEUE_FILE, "a");
    if (!f) {
        ESP_LOGE(TAG, "Nao foi possivel criar/abrir fila: %s", QUEUE_FILE);
        return false;
    }
    fclose(f);
    rotate_if_needed();
    return true;
}

static bool build_event_json(const uint8_t *uid, size_t uid_len,
                             access_result_t result,
                             const char *person,
                             const char *reason,
                             char *out, size_t out_len) {
    if (!out || out_len < 64) return false;

    char uid_hex[32] = {0};
    uid_to_hex(uid, uid_len, uid_hex, sizeof(uid_hex));

    // timestamp: se não tiver SNTP, use uptime_ms
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    // Obs: se você já implementou SNTP e tiver epoch, você pode trocar por "ts": epoch.
    // Aqui fica genérico com uptime_ms para garantir sempre.
    const char *p = person ? person : "";
    const char *r = reason ? reason : "";

    int n = snprintf(out, out_len,
                     "{\"uptime_ms\":%lld,\"uid\":\"%s\",\"result\":\"%s\",\"person\":\"%s\",\"reason\":\"%s\"}",
                     (long long)uptime_ms, uid_hex, res_str(result), p, r);
    return (n > 0 && (size_t)n < out_len);
}

bool access_log_enqueue_event(const uint8_t *uid, size_t uid_len,
                              access_result_t result,
                              const char *person,
                              const char *reason) {
    if (!spiffs_mount_once()) return false;

    char json[MAX_LINE_LEN] = {0};
    if (!build_event_json(uid, uid_len, result, person, reason, json, sizeof(json))) {
        ESP_LOGE(TAG, "Falha ao montar JSON do evento");
        return false;
    }

    rotate_if_needed();

    FILE *f = fopen(QUEUE_FILE, "a");
    if (!f) {
        ESP_LOGE(TAG, "Falha ao abrir fila para append");
        return false;
    }
    fprintf(f, "%s\n", json);
    fclose(f);
    return true;
}

bool access_log_publish_or_queue(const uint8_t *uid, size_t uid_len,
                                 access_result_t result,
                                 const char *person,
                                 const char *reason) {
    char json[MAX_LINE_LEN] = {0};
    if (!build_event_json(uid, uid_len, result, person, reason, json, sizeof(json))) {
        return false;
    }

    if (mqtt_manager_is_connected() && mqtt_manager_publish_event(json)) {
        return true;
    }

    // fallback local
    return access_log_enqueue_event(uid, uid_len, result, person, reason);
}

bool access_log_flush_pending(void) {
    if (!spiffs_mount_once()) return false;
    if (!mqtt_manager_is_connected()) return true; // nada a fazer

    FILE *in = fopen(QUEUE_FILE, "r");
    if (!in) {
        // se não existe ainda, ok
        return true;
    }

    FILE *out = fopen(QUEUE_FILE_TMP, "w");
    if (!out) {
        fclose(in);
        ESP_LOGE(TAG, "Falha ao abrir tmp para flush");
        return false;
    }

    char line[MAX_LINE_LEN] = {0};
    bool any_failed = false;

    while (fgets(line, sizeof(line), in)) {
        // remove \n
        size_t len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = 0;
        }
        if (len == 0) continue;

        // tenta publicar cada linha
        if (!mqtt_manager_publish_event(line)) {
            // se falhar, mantém na fila
            fprintf(out, "%s\n", line);
            any_failed = true;
        }
    }

    fclose(in);
    fclose(out);

    // Substitui fila pela tmp
    unlink(QUEUE_FILE);
    rename(QUEUE_FILE_TMP, QUEUE_FILE);

    if (any_failed) {
        ESP_LOGW(TAG, "Flush parcial: alguns eventos permaneceram na fila.");
    } else {
        ESP_LOGI(TAG, "Flush completo: fila enviada e limpa.");
    }

    return true;
}
