#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"
#include "esp_netif.h" 

#define AP_SSID      "SmartLock_Config"
#define AP_PASSWORD  "12345678"
#define AP_MAX_CONN  4 

/**
 * @brief Inicializa e inicia o Ponto de Acesso (Soft AP).
 *
 * @return O ponteiro para a interface de rede (esp_netif_t*) do AP, ou NULL em caso de falha.
 */
esp_netif_t *start_wifi_soft_ap(void);

#endif 