#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64

/**
 * @brief Tenta carregar credenciais Wi-Fi do NVS.
 * @return true se as credenciais foram encontradas e carregadas.
 */
bool nvs_manager_load_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len);

/**
 * @brief Salva credenciais Wi-Fi no NVS.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t nvs_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Apaga credenciais Wi-Fi do NVS.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t nvs_manager_erase_credentials(void);

#endif 