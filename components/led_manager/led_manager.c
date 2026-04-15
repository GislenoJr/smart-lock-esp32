// components/led_manager/led_manager.c

#include "led_manager.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG_LED = "LED_MANAGER";

// Configurações do PWM
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_RESOLUTION LEDC_TIMER_10_BIT 
#define LEDC_FREQUENCY 5000 

typedef struct {
    int gpio_num;
    ledc_channel_t canal;
} led_channel_config_t;

static led_channel_config_t led_config[] = {
    {LED_RED_GPIO, LEDC_CHANNEL_0},
    {LED_GREEN_GPIO, LEDC_CHANNEL_1},
    {LED_BLUE_GPIO, LEDC_CHANNEL_2}
};

static led_status_t status_atual = STATUS_AP_CONFIG;
static EventGroupHandle_t grupo_eventos_led;

typedef struct {
    uint16_t r_duty;
    uint16_t g_duty;
    uint16_t b_duty;
    int tempo_on_ms; 
    int tempo_off_ms; 
} led_efeito_t;

static const led_efeito_t efeitos_status[] = {
    // 0. STATUS_AP_CONFIG (Azul Piscando Rápido)
    { 0, 0, 1023, 200, 200 }, 
    // 1. STATUS_STA_CONNECTING (Amarelo Piscando Lento)
    { 1023, 1023, 0, 1000, 1000 },
    // 2. STATUS_STA_CONNECTED (Verde Fixo)
    { 0, 1023, 0, 0, 0 },
    // 3. STATUS_RESETTING (Vermelho Fixo)
    { 1023, 0, 0, 0, 0 },
    // 4. STATUS_OFFLINE_LOGIC (Amarelo Piscando Lento) - Usado no modo de resiliência
    { 1023, 1023, 0, 1000, 1000 }
};

/**
 * @brief Aplica os valores de brilho (Duty Cycle) nos canais PWM.
 */
static void definir_cor_led(uint16_t r_duty, uint16_t g_duty, uint16_t b_duty)
{
    ledc_set_duty(LEDC_MODE, led_config[0].canal, r_duty);
    ledc_update_duty(LEDC_MODE, led_config[0].canal);

    ledc_set_duty(LEDC_MODE, led_config[1].canal, g_duty);
    ledc_update_duty(LEDC_MODE, led_config[1].canal);
    
    ledc_set_duty(LEDC_MODE, led_config[2].canal, b_duty);
    ledc_update_duty(LEDC_MODE, led_config[2].canal);
}

/**
 * @brief Tarefa FreeRTOS que executa a lógica de piscagem.
 */
static void led_task(void *parametro)
{
    while (1) {
        const led_efeito_t *efeito = &efeitos_status[status_atual];

        if (efeito->tempo_on_ms == 0) {
            definir_cor_led(efeito->r_duty, efeito->g_duty, efeito->b_duty);
            vTaskDelay(pdMS_TO_TICKS(1000)); 
        } else {
            definir_cor_led(efeito->r_duty, efeito->g_duty, efeito->b_duty);
            vTaskDelay(pdMS_TO_TICKS(efeito->tempo_on_ms));

            definir_cor_led(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(efeito->tempo_off_ms));
        }
    }
}


/**
 * @brief Inicializa os pinos GPIO e o driver LEDC (PWM).
 */
esp_err_t led_manager_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode = LEDC_MODE,
            .channel = led_config[i].canal,
            .timer_sel = LEDC_TIMER,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = led_config[i].gpio_num,
            .duty = 0, 
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }

    xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG_LED, "LED Manager inicializado. Pinagem: R:%d, G:%d, B:%d", 
             LED_RED_GPIO, LED_GREEN_GPIO, LED_BLUE_GPIO);
             
    led_manager_set_status(STATUS_AP_CONFIG);

    return ESP_OK;
}

/**
 * @brief Define o estado visual do LED com base no estado do sistema.
 */
void led_manager_set_status(led_status_t novo_status)
{
    if (novo_status >= sizeof(efeitos_status) / sizeof(led_efeito_t)) {
        ESP_LOGE(TAG_LED, "Status invalido: %d", novo_status);
        return;
    }
  
    status_atual = novo_status;
    ESP_LOGI(TAG_LED, "Novo Status de LED: %d", status_atual);
    
    if (efeitos_status[novo_status].tempo_on_ms == 0) {
        const led_efeito_t *efeito = &efeitos_status[novo_status];
        definir_cor_led(efeito->r_duty, efeito->g_duty, efeito->b_duty);
    }
}