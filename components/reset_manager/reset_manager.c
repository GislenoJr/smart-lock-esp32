#include "reset_manager.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include "esp_system.h"
#include <stdbool.h>

// =======================
// Definições
// =======================
#define TAG_RESET "RESET_MANAGER"
#define RESET_BUTTON_GPIO GPIO_NUM_17  // GPIO do botão de reset
#define HOLD_TIME_SECONDS 10           // Tempo para reset de fábrica
#define TASK_DELAY_MS 100              // Intervalo de verificação

// =======================
// Função interna de reinício
// =======================
static void reiniciar_esp32(void) {
    ESP_LOGE(TAG_RESET, "==>> REINICIANDO ESP32 <<==");
    
    // Pequeno delay para garantir que os logs sejam enviados
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Reinicia o sistema
    esp_restart();
}

// =======================
// Tarefa de monitoramento do botão
// =======================
static void monitorar_botao_reset(void *argumento) {
    const int contador_maximo = (HOLD_TIME_SECONDS * 1000) / TASK_DELAY_MS;
    int contador_pressao = 0;
    bool estado_anterior = false;

    // Configura o GPIO do botão
    gpio_reset_pin(RESET_BUTTON_GPIO);
    gpio_set_direction(RESET_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(RESET_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG_RESET, "Monitor iniciado - GPIO %d. Segure por %d segundos para reset.",
             RESET_BUTTON_GPIO, HOLD_TIME_SECONDS);

    while (1) {
        // Lê o estado do botão (ativo em LOW devido ao pull-up)
        bool estado_atual = (gpio_get_level(RESET_BUTTON_GPIO) == 0);

        if (estado_atual) {
            // Botão pressionado
            if (!estado_anterior) {
                ESP_LOGI(TAG_RESET, "Botão pressionado. Iniciando contagem...");
            }

            contador_pressao++;
            estado_anterior = true;

            // Verifica se atingiu o tempo para reset
            if (contador_pressao >= contador_maximo) {
                ESP_LOGE(TAG_RESET, "RESET DE FÁBRICA DETECTADO!");
                
                // 1. Apaga credenciais Wi-Fi
                ESP_LOGI(TAG_RESET, "Apagando credenciais do Wi-Fi...");
                nvs_manager_erase_credentials();
                
                // 2. Reinicia o ESP32
                reiniciar_esp32();
                
                // Esta linha nunca será executada (devido ao restart)
                vTaskDelete(NULL);
            } 
            // Log a cada segundo
            else if (contador_pressao % (1000 / TASK_DELAY_MS) == 0) {
                int segundos = contador_pressao * TASK_DELAY_MS / 1000;
                ESP_LOGW(TAG_RESET, "Botão pressionado: %d segundos...", segundos);
            }

        } else {
            // Botão liberado
            if (estado_anterior && contador_pressao > 0) {
                ESP_LOGI(TAG_RESET, "Botão liberado após %d ms.", 
                         contador_pressao * TASK_DELAY_MS);
            }
            contador_pressao = 0;
            estado_anterior = false;
        }

        // Aguarda próximo ciclo
        vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    }
}

// =======================
// Função pública (conforme header)
// =======================
void reset_manager_start_monitor(void) {
    // Cria a tarefa de monitoramento
    BaseType_t result = xTaskCreate(
        monitorar_botao_reset,    // Função da tarefa
        "reset_monitor",          // Nome da tarefa
        3072,                     // Stack size
        NULL,                     // Parâmetros
        5,                        // Prioridade (acima do default)
        NULL                      // Handle (não necessário)
    );

    if (result == pdPASS) {
        ESP_LOGI(TAG_RESET, "Tarefa de monitoramento criada com sucesso.");
    } else {
        ESP_LOGE(TAG_RESET, "FALHA ao criar tarefa de monitoramento!");
    }
}