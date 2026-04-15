#include "rfid_reader.h"
#include "mfrc522.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG_RFID = "RFID_READER";
static rc522_handle_t manipulador_rc522 = NULL;

static rfid_uid_cb_t s_uid_cb = NULL;

/**
 * @brief Converte serial_number (uint64) em bytes e retorna tamanho.
 * Mantém big-endian para bater com representação hex natural do valor.
 */
static size_t serial_to_uid_bytes(uint64_t serial, uint8_t *uid_out, size_t uid_out_max)
{
    if (!uid_out || uid_out_max < RFID_UID_MAX_LEN) return 0;

    size_t len = 0;
    uint64_t tmp = serial;

    while (tmp > 0) {
        len++;
        tmp >>= 8;
    }

    if (len == 0) len = 1;                  // serial == 0 (caso raro)
    if (len > RFID_UID_MAX_LEN) len = RFID_UID_MAX_LEN;

    for (size_t i = 0; i < len; i++) {
        uid_out[i] = (uint8_t)((serial >> (8 * (len - 1 - i))) & 0xFF);
    }

    return len;
}

/**
 * @brief Função chamada quando o RC522 detecta um novo cartão.
 */
static void rfid_event_handler(void* handler_arg, esp_event_base_t base_evento,
                               int32_t id_evento, void* dados_evento)
{
    (void)handler_arg;

    rc522_event_data_t *dados_rc522 = (rc522_event_data_t *) dados_evento;

    if (base_evento == RC522_EVENTS && id_evento == RC522_EVENT_TAG_SCANNED) {
        rc522_tag_t *tag = (rc522_tag_t *) dados_rc522->ptr;

        ESP_LOGW(TAG_RFID, "TAG DETECTADA! serial_number=0x%llX", (unsigned long long)tag->serial_number);

        if (s_uid_cb) {
            uint8_t uid[RFID_UID_MAX_LEN] = {0};
            size_t uid_len = serial_to_uid_bytes(tag->serial_number, uid, sizeof(uid));

            if (uid_len > 0) {
                s_uid_cb(uid, uid_len);
            } else {
                ESP_LOGE(TAG_RFID, "Falha ao converter UID para bytes.");
            }
        } else {
            ESP_LOGW(TAG_RFID, "Callback nao configurado (s_uid_cb == NULL).");
        }
    }
}

/**
 * @brief Inicialização do leitor RFID com callback
 */
esp_err_t rfid_reader_init(rfid_uid_cb_t cb)
{
    s_uid_cb = cb;

    rc522_config_t configuracao = {
        .transport = RC522_TRANSPORT_SPI,
        .spi.host = SPI2_HOST,
        .spi.miso_gpio = RFID_PIN_MISO,
        .spi.mosi_gpio = RFID_PIN_MOSI,
        .spi.sck_gpio  = RFID_PIN_CLK,
        .spi.sda_gpio  = RFID_PIN_CS,
        .spi.clock_speed_hz = RC522_DEFAULT_SPI_CLOCK_SPEED_HZ,
    };

    esp_err_t resultado = rc522_create(&configuracao, &manipulador_rc522);
    if (resultado != ESP_OK) {
        ESP_LOGE(TAG_RFID, "Falha ao criar RC522: %s", esp_err_to_name(resultado));
        return resultado;
    }

    rc522_register_events(manipulador_rc522, RC522_EVENT_TAG_SCANNED, rfid_event_handler, NULL);

    resultado = rc522_start(manipulador_rc522);
    if (resultado != ESP_OK) {
        ESP_LOGE(TAG_RFID, "Falha ao iniciar RC522: %s", esp_err_to_name(resultado));
        return resultado;
    }

    ESP_LOGI(TAG_RFID, "Leitor RFID inicializado e monitoramento iniciado.");
    return ESP_OK;
}
