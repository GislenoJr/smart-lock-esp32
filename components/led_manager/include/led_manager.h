#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include "esp_err.h"

#define LED_RED_GPIO    GPIO_NUM_2
#define LED_GREEN_GPIO  GPIO_NUM_4
#define LED_BLUE_GPIO   GPIO_NUM_16


typedef enum {
    STATUS_AP_CONFIG = 0,   // Azul, Piscando Rápido (Aguardando configuração)
    STATUS_STA_CONNECTING,  // Amarelo, Piscando Lento (Tentando reconexão)
    STATUS_STA_CONNECTED,   // Verde, Fixo (Online/Operação Normal)
    STATUS_RESETTING,       // Vermelho, Fixo (Pronto para reset/erro crítico)
    STATUS_OFFLINE_LOGIC    // Amarelo, Piscando Lento (Modo Offline, lógica da próxima etapa)
} led_status_t;

/**
 * @brief Inicializa os pinos GPIO do LED
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t led_manager_init(void);

/**
 * @brief Define o estado visual do LED com base no estado do sistema.
 * @param status O novo estado desejado 
 */
void led_manager_set_status(led_status_t status);

#endif