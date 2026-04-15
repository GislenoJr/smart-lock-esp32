#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACCESS_GRANTED = 1,
    ACCESS_DENIED  = 2
} access_result_t;

/**
 * Inicializa SPIFFS e prepara fila offline.
 */
bool access_log_init(void);

/**
 * Enfileira evento localmente (quando MQTT não puder enviar).
 * Escreve 1 evento por linha (JSON Lines) em /spiffs/access_queue.log
 */
bool access_log_enqueue_event(const uint8_t *uid, size_t uid_len,
                              access_result_t result,
                              const char *person,
                              const char *reason);

/**
 * Tenta publicar imediatamente via MQTT.
 * Se falhar, faz enqueue local.
 */
bool access_log_publish_or_queue(const uint8_t *uid, size_t uid_len,
                                 access_result_t result,
                                 const char *person,
                                 const char *reason);

/**
 * Envia tudo que estiver na fila offline (se MQTT estiver conectado).
 * Retorna true se conseguiu processar (mesmo que fila vazia).
 */
bool access_log_flush_pending(void);

#ifdef __cplusplus
}
#endif
