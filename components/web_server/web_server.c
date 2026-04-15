#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include "cJSON.h" 
#include <sys/param.h> 
#include <stdbool.h>
#include "main.h"

static const char *TAG_WEB = "WEB_SERVER";
static httpd_handle_t server = NULL;

static const char *SUCCESS_HTML = 
"<!DOCTYPE html>"
"<html lang='pt-br'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Sucesso!</title>"
"<style>"
    "body { font-family: sans-serif; background-color: #f4f6f9; display: flex; justify-content: center; align-items: center; min-height: 100vh; }"
    ".container { background-color: #ffffff; padding: 30px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1); max-width: 350px; width: 90%; text-align: center; border-left: 5px solid #2ecc71; }"
    "h2 { color: #2ecc71; margin-bottom: 15px; } p { color: #34495e; }"
"</style>"
"</head>"
"<body><div class='container'><h2>✅ Conexão Bem-Sucedida!</h2><p>O dispositivo foi configurado e irá se conectar à rede principal.</p><p>Você pode fechar esta página.</p></div></body></html>";


static const char *FAIL_HTML = 
"<!DOCTYPE html>"
"<html lang='pt-br'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Falha!</title>"
"<style>"
    "body { font-family: sans-serif; background-color: #f4f6f9; display: flex; justify-content: center; align-items: center; min-height: 100vh; }"
    ".container { background-color: #ffffff; padding: 30px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1); max-width: 350px; width: 90%; text-align: center; border-left: 5px solid #e74c3c; }"
    "h2 { color: #e74c3c; margin-bottom: 15px; } p { color: #34495e; }"
    "button { padding: 10px 20px; background-color: #3498db; color: white; border: none; border-radius: 8px; cursor: pointer; margin-top: 15px; }"
"</style>"
"</head>"
"<body><div class='container'><h2>❌ Falha na Conexão</h2><p>Não foi possível conectar com as credenciais fornecidas. Tente novamente.</p><form method=\"GET\" action=\"/\"><button type=\"submit\">Voltar ao Formulário</button></form></div></body></html>";

static const char *CONFIG_FORM_HTML = 
"<!DOCTYPE html>"
"<html lang='pt-br'>"
"<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>SmartLock Config</title>"
    "<style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif; background-color: #f4f6f9; margin: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh; }"
        ".container { background-color: #ffffff; padding: 30px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1); max-width: 350px; width: 90%; text-align: center; }"
        "h2 { color: #34495e; margin-bottom: 25px; }"
        "input[type=\"text\"], input[type=\"password\"] { width: 100%; padding: 12px; margin: 10px 0; border: 1px solid #bdc3c7; border-radius: 8px; box-sizing: border-box; transition: border-color 0.3s; }"
        "input[type=\"text\"]:focus, input[type=\"password\"]:focus { border-color: #3498db; outline: none; }"
        "button { width: 100%; padding: 12px; background-color: #2ecc71; color: white; border: none; border-radius: 8px; cursor: pointer; font-size: 16px; margin-top: 20px; transition: background-color 0.3s; }"
        "button:hover { background-color: #27ae60; }"
        ".message { margin-top: 20px; padding: 10px; border-radius: 8px; background-color: #f0fdf4; color: #15803d; border: 1px solid #dcfce7; display: none; }"
    "</style>"
"</head>"
"<body>"
    "<div class=\"container\">"
        "<h2>Configurar Wi-Fi (SmartLock)</h2>"
        "<form method=\"POST\" action=\"/config_save\">"
            "<input type=\"text\" name=\"ssid\" placeholder=\"Nome da Rede Wi-Fi (SSID)\" required>"
            "<input type=\"password\" name=\"password\" placeholder=\"Senha da Rede Wi-Fi\" required>"
            "<button type=\"submit\">Salvar e Conectar</button>"
        "</form>"
        "<div id=\"statusMessage\" class=\"message\"></div>"
    "</div>"
"</body>"
"</html>";

// Exibe o Formulário
static esp_err_t captive_portal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG_WEB, "Servindo formulario de configuracao para URI: %s", req->uri);

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, CONFIG_FORM_HTML, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// Recebe e Processa as Credenciais
static esp_err_t config_post_handler(httpd_req_t *requisicao)
{
    char buffer_dados[256]; 
    int bytes_lidos;
    int tamanho_restante = requisicao->content_len;
    
    if (tamanho_restante >= sizeof(buffer_dados)) {
        ESP_LOGE(TAG_WEB, "Conteúdo POST muito grande (%d bytes). Limite: %d", 
                 tamanho_restante, (int)sizeof(buffer_dados));
        httpd_resp_send_500(requisicao);
        return ESP_FAIL;
    }
    
    bytes_lidos = httpd_req_recv(requisicao, buffer_dados, tamanho_restante);
    
    if (bytes_lidos <= 0) {
        if (bytes_lidos == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(requisicao); 
        }
        return ESP_FAIL;
    }

    buffer_dados[bytes_lidos] = '\0'; 
    ESP_LOGI(TAG_WEB, "Dados recebidos via POST: %s", buffer_dados);

    char ssid_string[32] = {0};
    char senha_string[64] = {0};
    

    if (httpd_query_key_value(buffer_dados, "ssid", ssid_string, sizeof(ssid_string)) != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Falha ao extrair SSID.");
    }

    if (httpd_query_key_value(buffer_dados, "password", senha_string, sizeof(senha_string)) != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Falha ao extrair Senha.");
    }

    
    ESP_LOGI(TAG_WEB, "Chamando lógica de salvamento e reset para SSID: %s", ssid_string);
    config_save_and_restart(ssid_string, senha_string);

    httpd_resp_set_status(requisicao, "200 OK");
    httpd_resp_set_type(requisicao, "text/html");
    httpd_resp_send(requisicao, SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

// Registro dos Handlers
static const httpd_uri_t config_post_uri = {
    .uri       = "/config_save", 
    .method    = HTTP_POST,
    .handler   = config_post_handler,
    .user_ctx  = NULL
};

// Handler para a URI Raiz e de Detecção (
static const httpd_uri_t root_uri = {
    .uri       = "/", 
    .method    = HTTP_GET,
    .handler   = captive_portal_handler,
    .user_ctx  = NULL
};

// Handler para detecção do Android/Google 
static const httpd_uri_t android_uri = {
    .uri       = "/generate_204", 
    .method    = HTTP_GET,
    .handler   = captive_portal_handler,
    .user_ctx  = NULL
};

// Handler para detecção do iOS/Apple 
static const httpd_uri_t apple_uri = {
    .uri       = "/hotspot-detect.html", 
    .method    = HTTP_GET,
    .handler   = captive_portal_handler,
    .user_ctx  = NULL
};


esp_err_t start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8; 

    ESP_LOGI(TAG_WEB, "Iniciando servidor na porta: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &android_uri);
        httpd_register_uri_handler(server, &apple_uri);
        httpd_register_uri_handler(server, &config_post_uri); 

        ESP_LOGI(TAG_WEB, "Servidor Web pronto para servir o formulario de configuracao.");

        return ESP_OK;
    }

    ESP_LOGE(TAG_WEB, "Erro ao iniciar servidor Web!");
    return ESP_FAIL;
}

