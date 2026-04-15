    #include <stdio.h>
    #include <string.h>
    #include <stdbool.h>
    #include <stdlib.h>

    #include "nvs_flash.h"
    #include "esp_log.h"
    #include "esp_event.h"
    #include "esp_timer.h"

    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/event_groups.h"
    #include "freertos/queue.h"

    #include "esp_wifi.h"
    #include "esp_netif.h"
    #include "driver/gpio.h"

    #include "esp_http_client.h"
    #include "esp_crt_bundle.h"  
    #include "main.h"
    #include "nvs_manager.h"
    #include "reset_manager.h"
    #include "led_manager.h"
    #include "wifi_ap.h"
    #include "dns_server.h"
    #include "web_server.h"
    #include "rfid_reader.h"
    #include "mqtt_manager.h"

    #define GPIO_TRAVA 15
    static const char *TAG_MAIN = "SMARTLOCK_MAIN";

    static EventGroupHandle_t grupo_eventos_wifi;
    static const int BIT_WIFI_CONECTADO = BIT0;
    static const int BIT_WIFI_FALHA     = BIT1;

    static esp_timer_handle_t timer_reconexao = NULL;
    #define TEMPO_RECONEXAO_PERIODICA_S 60

    // =======================
    // Google Apps Script URL
    // =======================
    #define SHEETS_URL "https://script.google.com/macros/s/AKfycbwVCsVWalTt9MY9HK7cM-h_VNzw27HN2DovlDa2K9xS_eRbT1yWtXPbR_lUOJ1iyrbf/exec"
    #define SHEETS_PARAM_NAME "uid"

    // =======================
    // CACHE EM MEMÓRIA RAM 
    // =======================
    #define MAX_UIDS_CACHE 100
    #define MAX_UID_LENGTH 32

    typedef struct {
        char uid[MAX_UID_LENGTH];
        char name[64];
    } uid_cache_entry_t;

    static uid_cache_entry_t uid_cache[MAX_UIDS_CACHE];
    static int uid_cache_count = 0;
    static bool cache_carregado = false;
    static bool modo_offline = false;

    // =======================
    // Buffer para sincronização inicial
    // =======================
    static char sync_buffer[4096];
    static int sync_buffer_len = 0;

    // =======================
    // Dedup no ESP (anti-dupla leitura)
    // =======================
    #define UID_DEDUP_MS 2500
    static char last_uid[32] = {0};
    static int64_t last_uid_ms = 0;

    static bool uid_should_process(const char *uid_hex) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (strcmp(uid_hex, last_uid) == 0 && (now_ms - last_uid_ms) < UID_DEDUP_MS) {
            return false;
        }
        strncpy(last_uid, uid_hex, sizeof(last_uid) - 1);
        last_uid[sizeof(last_uid) - 1] = 0;
        last_uid_ms = now_ms;
        return true;
    }

    // =======================
    // Helpers UID
    // =======================
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

    // =======================
    // Trava
    // =======================
    void tarefa_liberar_trava(void *pvParameters) {
        (void)pvParameters;
        ESP_LOGI(TAG_MAIN, "Porta Liberada!");
        gpio_set_level(GPIO_TRAVA, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(GPIO_TRAVA, 0);
        ESP_LOGI(TAG_MAIN, "Porta Travada novamente.");
        vTaskDelete(NULL);
    }

    static void inicializar_gpio_trava(void) {
        gpio_reset_pin(GPIO_TRAVA);
        gpio_set_direction(GPIO_TRAVA, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_TRAVA, 0);
    }

    // =======================
    // Timer de reconexão Wi-Fi
    // =======================
    static void reconectar_wifi_periodicamente(void *argumento) {
        (void)argumento;
        ESP_LOGI(TAG_MAIN, "Timer acionado. Tentando reconexao ao Wi-Fi...");
        esp_wifi_connect();
    }

    static void gerenciar_timer_reconexao(bool iniciar) {
        if (iniciar) {
            if (timer_reconexao == NULL) {
                const esp_timer_create_args_t timer_args = {
                    .callback = &reconectar_wifi_periodicamente,
                    .name = "wifi_retry_timer"
                };
                ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_reconexao));
            }
            ESP_LOGW(TAG_MAIN, "Modo resiliencia. Proxima reconexao em %d s.", TEMPO_RECONEXAO_PERIODICA_S);
            ESP_ERROR_CHECK(esp_timer_start_periodic(timer_reconexao, TEMPO_RECONEXAO_PERIODICA_S * 1000000ULL));
        } else {
            if (timer_reconexao != NULL && esp_timer_is_active(timer_reconexao)) {
                ESP_ERROR_CHECK(esp_timer_stop(timer_reconexao));
                ESP_LOGI(TAG_MAIN, "Timer de reconexao parado.");
            }
        }
    }

    // =======================
    // MQTT callback
    // =======================
    static void on_mqtt_conn(bool connected) {
        ESP_LOGI(TAG_MAIN, "MQTT status: %s", connected ? "CONECTADO" : "DESCONECTADO");
    }

    // Publica evento
    static void publish_access_event(const char *uid_hex, bool allowed, const char *name_opt, const char *reason_opt) {
        if (!mqtt_manager_is_connected()) {
            ESP_LOGW(TAG_MAIN, "MQTT nao conectado. Log descartado.");
            return;
        }

        char payload[256];
        snprintf(payload, sizeof(payload),
                "{\"uid\":\"%s\",\"authorized\":%s,\"person_name\":\"%s\",\"reason\":\"%s\"}",
                uid_hex,
                allowed ? "true" : "false",
                (allowed && name_opt) ? name_opt : "",
                reason_opt ? reason_opt : "");

        mqtt_manager_publish("smartlock/lock-001/events", payload, 1, false);
    }

    // =======================
    // Handler para sincronização inicial 
    // =======================
    static esp_err_t sync_http_event_handler(esp_http_client_event_t *evt) {
        switch (evt->event_id) {
            case HTTP_EVENT_ON_DATA:
                if (esp_http_client_get_status_code(evt->client) == 200) {
                    if (sync_buffer_len + evt->data_len < (int)sizeof(sync_buffer)) {
                        memcpy(sync_buffer + sync_buffer_len, evt->data, evt->data_len);
                        sync_buffer_len += evt->data_len;
                    }
                }
                break;
            case HTTP_EVENT_ON_FINISH:
                if (sync_buffer_len < (int)sizeof(sync_buffer)) {
                    sync_buffer[sync_buffer_len] = '\0';
                } else {
                    sync_buffer[sizeof(sync_buffer) - 1] = '\0';
                }
                break;
            default:
                break;
        }
        return ESP_OK;
    }

    // =======================
    // SINCRONIZAÇÃO INICIAL (com validação completa SSL)
    // =======================
    static bool sincronizar_planilha_uma_vez(void) {
        ESP_LOGI(TAG_MAIN, "Sincronizando planilha UMA VEZ no boot (com SSL)...");
        
        sync_buffer_len = 0;
        memset(sync_buffer, 0, sizeof(sync_buffer));
        
        // URL para pegar TODOS os UIDs
        char url[256];
        snprintf(url, sizeof(url), "%s?%s=all", SHEETS_URL, SHEETS_PARAM_NAME);
        
        // Configuração COMPLETA com validação SSL
        esp_http_client_config_t config = {
            .url = url,
            .event_handler = sync_http_event_handler,
            .crt_bundle_attach = esp_crt_bundle_attach,  // VALIDAÇÃO SSL COMPLETA
            .timeout_ms = 10000,  // Tempo suficiente para handshake SSL
            .user_agent = "esp32-smartlock-sync/1.0",
            .disable_auto_redirect = false,
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG_MAIN, "Falha ao criar cliente HTTP para sincronizacao");
            return false;
        }
        
        // IMPORTANTE para Apps Script
        esp_http_client_set_redirection(client);
        
        ESP_LOGI(TAG_MAIN, "Iniciando sincronizacao SSL...");
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        
        if (err != ESP_OK || status != 200) {
            ESP_LOGW(TAG_MAIN, "Sincronizacao falhou: err=%s status=%d", 
                    esp_err_to_name(err), status);
            return false;
        }
        
        ESP_LOGI(TAG_MAIN, "Sincronizacao SSL concluida! %d bytes recebidos", sync_buffer_len);
        return true;
    }

    // =======================
    // PROCESSAR RESPOSTA E CRIAR CACHE EM MEMÓRIA
    // =======================
    static void processar_e_criar_cache(void) {
        uid_cache_count = 0;
        
        if (sync_buffer_len == 0 || !sync_buffer[0]) {
            ESP_LOGW(TAG_MAIN, "Buffer de sincronizacao vazio");
            return;
        }
        
        ESP_LOGI(TAG_MAIN, "Processando resposta para criar cache...");
        
        // Processar cada linha (assumindo que cada UID está em uma linha)
        char *line = strtok(sync_buffer, "\n");
        int uids_processados = 0;
        
        while (line != NULL && uid_cache_count < MAX_UIDS_CACHE) {
            // Remover espaços e caracteres de controle
            char *trimmed = line;
            while (*trimmed == ' ' || *trimmed == '\r' || *trimmed == '\t') trimmed++;
            
            // Verificar se parece um UID (só números e letras A-F, tamanho mínimo)
            if (strlen(trimmed) >= 8) {  // Pelo menos 8 caracteres hex
                bool valido = true;
                for (int i = 0; trimmed[i] != '\0'; i++) {
                    char c = trimmed[i];
                    if (!((c >= '0' && c <= '9') || 
                        (c >= 'A' && c <= 'F') || 
                        (c >= 'a' && c <= 'f'))) {
                        valido = false;
                        break;
                    }
                }
                
                if (valido) {
                    // Converter para maiúsculas
                    char uid_upper[MAX_UID_LENGTH];
                    strncpy(uid_upper, trimmed, MAX_UID_LENGTH - 1);
                    uid_upper[MAX_UID_LENGTH - 1] = '\0';
                    
                    for (int i = 0; uid_upper[i] != '\0'; i++) {
                        if (uid_upper[i] >= 'a' && uid_upper[i] <= 'f') {
                            uid_upper[i] = uid_upper[i] - 'a' + 'A';
                        }
                    }
                    
                    // Armazenar no cache
                    strncpy(uid_cache[uid_cache_count].uid, uid_upper, MAX_UID_LENGTH - 1);
                    uid_cache[uid_cache_count].uid[MAX_UID_LENGTH - 1] = '\0';
                    
                    // Tentar extrair nome se houver (formato: UID|Nome)
                    char *pipe = strchr(trimmed, '|');
                    if (pipe) {
                        strncpy(uid_cache[uid_cache_count].name, pipe + 1, 63);
                        uid_cache[uid_cache_count].name[63] = '\0';
                    } else {
                        strcpy(uid_cache[uid_cache_count].name, "Autorizado");
                    }
                    
                    uid_cache_count++;
                    uids_processados++;
                    
                    if (uids_processados <= 5) {  // Log apenas os primeiros 5
                        ESP_LOGI(TAG_MAIN, "Cache: %s -> %s", 
                                uid_cache[uid_cache_count-1].uid,
                                uid_cache[uid_cache_count-1].name);
                    }
                }
            }
            
            line = strtok(NULL, "\n");
        }
        
        ESP_LOGI(TAG_MAIN, "Cache criado: %d UIDs carregados na RAM", uid_cache_count);
        cache_carregado = true;
        
        // Liberar buffer de sincronização (economizar RAM)
        sync_buffer_len = 0;
        sync_buffer[0] = '\0';
    }

    // =======================
    // CONSULTA NO CACHE (ULTRA-RÁPIDA - 1ms)
    // =======================
    static bool consultar_cache_uid(const char *uid_hex, char *out_name, size_t out_name_len) {
        if (!cache_carregado || uid_cache_count == 0) {
            return false;
        }
        
        // Converter para maiúsculas para busca
        char uid_upper[MAX_UID_LENGTH];
        strncpy(uid_upper, uid_hex, MAX_UID_LENGTH - 1);
        uid_upper[MAX_UID_LENGTH - 1] = '\0';
        
        for (int i = 0; uid_upper[i] != '\0'; i++) {
            if (uid_upper[i] >= 'a' && uid_upper[i] <= 'f') {
                uid_upper[i] = uid_upper[i] - 'a' + 'A';
            }
        }
        
        // Busca linear (rápida para até 100 UIDs)
        for (int i = 0; i < uid_cache_count; i++) {
            if (strcmp(uid_cache[i].uid, uid_upper) == 0) {
                if (out_name && out_name_len > 0) {
                    strncpy(out_name, uid_cache[i].name, out_name_len - 1);
                    out_name[out_name_len - 1] = '\0';
                }
                return true;
            }
        }
        
        return false;
    }

    // =======================
    // Worker via fila 
    // =======================
    typedef struct {
        char uid_hex[32];
    } auth_job_t;

    static QueueHandle_t auth_queue = NULL;

    static void auth_worker_task(void *pv) {
        (void)pv;
        auth_job_t job;

        while (1) {
            if (xQueueReceive(auth_queue, &job, portMAX_DELAY) != pdTRUE) continue;

            char name[96] = {0};
            bool allowed = false;
            char reason[96] = "uid_not_found";
            
            if (cache_carregado) {
                // CONSULTA ULTRA-RÁPIDA NO CACHE (~1ms)
                allowed = consultar_cache_uid(job.uid_hex, name, sizeof(name));
                if (allowed) {
                    strcpy(reason, "");
                }
            } else {
                ESP_LOGW(TAG_MAIN, "Cache nao carregado. Modo offline.");
                strcpy(reason, "cache_not_loaded");
            }

            if (allowed) {
                ESP_LOGI(TAG_MAIN, "UID=%s AUTORIZADO (%s) via cache. Abrindo.", job.uid_hex, name[0] ? name : "");
                publish_access_event(job.uid_hex, true, name[0] ? name : NULL, reason[0] ? reason : "");
                xTaskCreate(tarefa_liberar_trava, "liberar_trava", 2048, NULL, 5, NULL);
            } else {
                ESP_LOGW(TAG_MAIN, "UID=%s NEGADO (%s).", job.uid_hex, reason);
                publish_access_event(job.uid_hex, false, NULL, reason);
            }

            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // =======================
    // RFID callback
    // =======================
    static void on_rfid_uid(const uint8_t *uid, size_t uid_len) {
        char uid_hex[32] = {0};
        uid_to_hex(uid, uid_len, uid_hex, sizeof(uid_hex));
        if (!uid_hex[0]) return;

        // Ignorar os dois primeiros caracteres (ex: "92" do "92F6877390")
        char uid_sem_prefixo[32] = {0};
        if (strlen(uid_hex) > 2) {
            // Copiar a partir do 3º caractere (índice 2)
            strncpy(uid_sem_prefixo, uid_hex + 2, sizeof(uid_sem_prefixo) - 1);
            uid_sem_prefixo[sizeof(uid_sem_prefixo) - 1] = '\0';
            
            ESP_LOGI(TAG_MAIN, "UID original: %s, UID processado: %s", uid_hex, uid_sem_prefixo);
        } else {
            // Se o UID for muito curto, usar completo
            strncpy(uid_sem_prefixo, uid_hex, sizeof(uid_sem_prefixo) - 1);
            ESP_LOGW(TAG_MAIN, "UID muito curto, usando completo: %s", uid_hex);
        }

        if (!uid_should_process(uid_sem_prefixo)) {
            ESP_LOGW(TAG_MAIN, "Leitura duplicada ignorada UID=%s", uid_sem_prefixo);
            return;
        }

        auth_job_t job = {0};
        strncpy(job.uid_hex, uid_sem_prefixo, sizeof(job.uid_hex) - 1);

        if (auth_queue) {
            if (xQueueSend(auth_queue, &job, 0) != pdTRUE) {
                ESP_LOGW(TAG_MAIN, "Fila cheia. Descartando UID=%s", uid_sem_prefixo);
            }
        }
    }

    // =======================
    // Wi-Fi event handler
    // =======================
    static void wifi_event_handler(void* argumento, esp_event_base_t base_evento,
                                int32_t id_evento, void* dados_evento) {
        (void)argumento;

        if (base_evento == WIFI_EVENT && id_evento == WIFI_EVENT_STA_START) {
            led_manager_set_status(STATUS_STA_CONNECTING);
            esp_wifi_connect();

        } else if (base_evento == WIFI_EVENT && id_evento == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG_MAIN, "Desconectado do Wi-Fi.");
            xEventGroupClearBits(grupo_eventos_wifi, BIT_WIFI_CONECTADO);
            xEventGroupSetBits(grupo_eventos_wifi, BIT_WIFI_FALHA);

            led_manager_set_status(STATUS_OFFLINE_LOGIC);

            wifi_mode_t modo_wifi_atual;
            if (esp_wifi_get_mode(&modo_wifi_atual) == ESP_OK && modo_wifi_atual == WIFI_MODE_STA) {
                gerenciar_timer_reconexao(true);
            }

        } else if (base_evento == IP_EVENT && id_evento == IP_EVENT_STA_GOT_IP) {
            gerenciar_timer_reconexao(false);
            xEventGroupSetBits(grupo_eventos_wifi, BIT_WIFI_CONECTADO);
            led_manager_set_status(STATUS_STA_CONNECTED);

            mqtt_manager_start();
        }
    }

    // =======================
    // Portal cativo
    // =======================
    void start_captive_portal_mode(void) {
        led_manager_set_status(STATUS_AP_CONFIG);

        esp_wifi_stop();
        gerenciar_timer_reconexao(false);

        esp_netif_t *manipulador_netif_ap = start_wifi_soft_ap();
        if (manipulador_netif_ap == NULL) return;

        esp_netif_ip_info_t informacoes_ip;
        ESP_ERROR_CHECK(esp_netif_get_ip_info(manipulador_netif_ap, &informacoes_ip));
        char string_ip_ap[16];
        esp_ip4addr_ntoa(&informacoes_ip.ip, string_ip_ap, sizeof(string_ip_ap));

        start_dns_server(string_ip_ap);
        start_web_server();

        ESP_LOGI(TAG_MAIN, "Portal Cativo ATIVADO. IP: %s", string_ip_ap);
    }

    // =======================
    // Interface / reinício
    // =======================
    void forcar_reiniciar_tarefa(void *argumento) {
        (void)argumento;
        led_manager_set_status(STATUS_RESETTING);
        ESP_LOGI(TAG_MAIN, "Reiniciando em 5 segundos...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    void config_save_and_restart(const char *ssid, const char *password) {
        nvs_manager_save_credentials(ssid, password);
        xTaskCreate(forcar_reiniciar_tarefa, "reiniciar_tarefa", 2048, NULL, 5, NULL);
    }

    // =======================
    // app_main 
    // =======================
    void app_main(void) {
        led_manager_init();
        inicializar_gpio_trava();

        // NVS
        esp_err_t resultado_nvs = nvs_flash_init();
        if (resultado_nvs == ESP_ERR_NVS_NO_FREE_PAGES || resultado_nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGI(TAG_MAIN, "Particao NVS corrompida. Formatando...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            resultado_nvs = nvs_flash_init();
        }
        ESP_ERROR_CHECK(resultado_nvs);

        ESP_LOGI(TAG_MAIN, "=== SMARTLOCK COM CACHE EM RAM ===");
        ESP_LOGI(TAG_MAIN, "Sincronizacao inicial SSL...");

        // MQTT init
        mqtt_manager_config_t mqtt_cfg = {
            .broker_uri   = "mqtts://2038ddb629c74560a056ffd609920ceb.s1.eu.hivemq.cloud:8883",
            .username     = "hivemq.webclient.1768265273274",
            .password     = "0921ZQRYTDfljikn#*@!",
            .client_id    = NULL,
            .topic_events = "smartlock/lock-001/events",
            .qos          = 1,
            .retain       = false
        };
        (void)mqtt_manager_init(&mqtt_cfg, on_mqtt_conn);

        // Wi-Fi stack
        grupo_eventos_wifi = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        wifi_init_config_t configuracao_wifi_init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&configuracao_wifi_init));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

        // Worker de autenticação
        auth_queue = xQueueCreate(6, sizeof(auth_job_t));
        xTaskCreate(auth_worker_task, "auth_worker", 8192, NULL, 5, NULL);

        // RFID init
        ESP_ERROR_CHECK(rfid_reader_init(on_rfid_uid));

        // Carregar credenciais Wi-Fi
        char ssid_salvo[MAX_SSID_LEN] = {0};
        char senha_salva[MAX_PASS_LEN] = {0};

        if (nvs_manager_load_credentials(ssid_salvo, sizeof(ssid_salvo), senha_salva, sizeof(senha_salva))) {
            wifi_config_t configuracao_wifi = { .sta = {} };
            strncpy((char*)configuracao_wifi.sta.ssid, ssid_salvo, sizeof(configuracao_wifi.sta.ssid) - 1);
            strncpy((char*)configuracao_wifi.sta.password, senha_salva, sizeof(configuracao_wifi.sta.password) - 1);

            esp_netif_t *manipulador_netif_sta = esp_netif_create_default_wifi_sta();
            if (manipulador_netif_sta == NULL) {
                ESP_LOGE(TAG_MAIN, "Falha ao criar Netif STA. Entrando em modo AP.");
                start_captive_portal_mode();
                return;
            }

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &configuracao_wifi));
            ESP_ERROR_CHECK(esp_wifi_start());

            int tentativas_maximas = 5;
            bool conectado_com_sucesso = false;

            for (int i = 0; i < tentativas_maximas; i++) {
                ESP_LOGI(TAG_MAIN, "Tentativa de conexao #%d", i + 1);

                esp_wifi_disconnect();
                gerenciar_timer_reconexao(false);

                xEventGroupClearBits(grupo_eventos_wifi, BIT_WIFI_CONECTADO | BIT_WIFI_FALHA);
                esp_wifi_connect();

                EventBits_t bits_resultado = xEventGroupWaitBits(
                    grupo_eventos_wifi,
                    BIT_WIFI_CONECTADO | BIT_WIFI_FALHA,
                    pdTRUE,
                    pdFALSE,
                    pdMS_TO_TICKS(15000)
                );

                if (bits_resultado & BIT_WIFI_CONECTADO) {
                    conectado_com_sucesso = true;
                    break;
                }

                ESP_LOGW(TAG_MAIN, "Conexao falhou (timeout/erro).");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            if (conectado_com_sucesso) {
                // ========================================
                // SINCRONIZAR PLANILHA UMA VEZ (com SSL)
                // ========================================
                bool sincronizado = sincronizar_planilha_uma_vez();
                
                if (sincronizado) {
                    // Processar resposta e criar cache em RAM
                    processar_e_criar_cache();
                    ESP_LOGI(TAG_MAIN, "✅ Cache carregado! Velocidade: ~1ms por consulta!");
                    ESP_LOGI(TAG_MAIN, "🔧 Sistema pronto para uso OFFLINE!");
                } else {
                    ESP_LOGW(TAG_MAIN, "⚠️  Falha na sincronizacao inicial");
                    modo_offline = true;
                }
                
                reset_manager_start_monitor();
            } else {
                ESP_LOGW(TAG_MAIN, "Falha total na conexao STA inicial. Modo Offline.");
                modo_offline = true;
                reset_manager_start_monitor();
                led_manager_set_status(STATUS_OFFLINE_LOGIC);
            }

        } else {
            ESP_LOGI(TAG_MAIN, "Nenhuma credencial salva. Iniciando Portal Cativo.");
            start_captive_portal_mode();
        }
    }