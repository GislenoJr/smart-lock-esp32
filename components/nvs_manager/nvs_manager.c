// components/nvs_manager/nvs_manager.c

#include "nvs_manager.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG_NVS = "NVS_MANAGER";
static const char *NVS_NAMESPACE = "storage";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASS = "pass";

// Implementação da função pública para carregar credenciais
bool nvs_manager_load_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t manipulador_nvs;
    esp_err_t resultado = nvs_open(NVS_NAMESPACE, NVS_READONLY, &manipulador_nvs);
    
    if (resultado != ESP_OK) {
        ESP_LOGI(TAG_NVS, "NVS nao aberto ou sem credenciais salvas.");
        return false;
    }

    size_t tamanho_necessario;
    bool credenciais_encontradas = false;

    resultado = nvs_get_str(manipulador_nvs, NVS_KEY_SSID, NULL, &tamanho_necessario);
    
    if (resultado == ESP_OK && tamanho_necessario > 0 && tamanho_necessario <= ssid_len) {
        nvs_get_str(manipulador_nvs, NVS_KEY_SSID, ssid, &ssid_len);

        resultado = nvs_get_str(manipulador_nvs, NVS_KEY_PASS, NULL, &tamanho_necessario);
        if (resultado == ESP_OK && tamanho_necessario > 0 && tamanho_necessario <= password_len) {
            nvs_get_str(manipulador_nvs, NVS_KEY_PASS, password, &password_len);
            credenciais_encontradas = true;
        }
    }

    nvs_close(manipulador_nvs);
    
    if (credenciais_encontradas) {
        ESP_LOGI(TAG_NVS, "Credenciais carregadas do NVS. SSID: %s", ssid);
    }
    return credenciais_encontradas;
}

// Implementação da função pública para salvar credenciais
esp_err_t nvs_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t manipulador_nvs;
    esp_err_t resultado = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &manipulador_nvs);
    
    if (resultado != ESP_OK) {
        ESP_LOGE(TAG_NVS, "Erro ao abrir NVS (%s)", esp_err_to_name(resultado));
        return resultado;
    }

    resultado = nvs_set_str(manipulador_nvs, NVS_KEY_SSID, ssid);
    if (resultado == ESP_OK) {
        resultado = nvs_set_str(manipulador_nvs, NVS_KEY_PASS, password);
    }
    nvs_commit(manipulador_nvs);
    nvs_close(manipulador_nvs);

    if (resultado == ESP_OK) {
        ESP_LOGI(TAG_NVS, "Credenciais salvas com sucesso no NVS.");
    }
    return resultado;
}

// Implementação da função pública para apagar credenciais
esp_err_t nvs_manager_erase_credentials(void)
{
    nvs_handle_t manipulador_nvs;
    esp_err_t resultado = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &manipulador_nvs);
    
    if (resultado != ESP_OK) {
        ESP_LOGE(TAG_NVS, "Erro ao abrir NVS (%s)", esp_err_to_name(resultado));
        return resultado;
    }

    resultado = nvs_erase_key(manipulador_nvs, NVS_KEY_SSID);
    if (resultado == ESP_OK || resultado == ESP_ERR_NVS_NOT_FOUND) {
        resultado = nvs_erase_key(manipulador_nvs, NVS_KEY_PASS);
    }

    if (resultado == ESP_OK || resultado == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(manipulador_nvs);
        ESP_LOGW(TAG_NVS, "Credenciais Wi-Fi apagadas do NVS. Resultado commit: %s", esp_err_to_name(resultado));
        resultado = ESP_OK;
    } else {
        ESP_LOGE(TAG_NVS, "Erro ao apagar chaves do NVS: %s", esp_err_to_name(resultado));
    }

    nvs_close(manipulador_nvs);
    return resultado;
}