#ifndef RFID_READER_H
#define RFID_READER_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include <stddef.h>
#include <stdint.h>

#define RFID_PIN_MOSI   GPIO_NUM_23
#define RFID_PIN_MISO   GPIO_NUM_19
#define RFID_PIN_CLK    GPIO_NUM_18
#define RFID_PIN_CS     GPIO_NUM_5
#define RFID_PIN_RST    GPIO_NUM_22

// RC522 pode ler UID 4, 7 ou 10 bytes
#define RFID_UID_MAX_LEN 10

typedef struct {
    uint8_t uid[RFID_UID_MAX_LEN];
    uint8_t uid_len;
} rfid_tag_t;

// Callback para entregar o UID lido ao "main" (controle de acesso)
typedef void (*rfid_uid_cb_t)(const uint8_t *uid, size_t uid_len);

/**
 * @brief Inicializa o barramento SPI e o monitoramento do RC522,
 *        registrando callback para quando uma TAG for lida.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t rfid_reader_init(rfid_uid_cb_t cb);

#endif // RFID_READER_H
