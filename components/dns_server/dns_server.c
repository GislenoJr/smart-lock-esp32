#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/prot/dns.h"
#include "lwip/dns.h"

#include "dns_server.h"

static const char *TAG_DNS = "DNS_SERVER";
static char s_ap_ip[16];

#define DNS_TYPE_A_VAL      (1)  
#define DNS_CLASS_IN_VAL    (1)   

#define DNS_OPCODE_QUERY_VAL       (0)
#define DNS_FLAG_OPCODE_MASK_VAL   (0x7800) 

// Estrutura de cabeçalho DNS 
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
};

// Tarefa para o Servidor DNS
static void dns_server_task(void *pvParameters)
{
    int sock;
    int err;
    struct sockaddr_in serv_addr;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    // 1. Criação do Socket UDP
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG_DNS, "Falha ao criar socket DNS: %d", errno);
        vTaskDelete(NULL);
        return;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(DNS_PORT);

    err = bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (err < 0) {
        ESP_LOGE(TAG_DNS, "Falha no bind do socket: %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_DNS, "Servidor DNS pronto para escutar no IP %s", s_ap_ip);
    
    uint8_t rx_buffer[512]; 
 
    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
        
        if (len < 0) {
            ESP_LOGE(TAG_DNS, "Erro no recvfrom: %d", errno);
            break;
        }

        if (len > 0) {
            if (len < sizeof(struct dns_header) + 1) {
                continue; 
            }
            
            struct dns_header *header = (struct dns_header *)rx_buffer;

            if ((ntohs(header->flags) & DNS_FLAG_OPCODE_MASK_VAL) != DNS_OPCODE_QUERY_VAL || 
                ntohs(header->qd_count) != 1) 
            {
                continue;
            }

            header->flags = htons(0x8180); 
            header->an_count = htons(1);
            
            uint8_t *answer_ptr = rx_buffer + len; 
            
            *(uint16_t *)answer_ptr = htons(0xC00C); 
            answer_ptr += sizeof(uint16_t);
            
            *(uint16_t *)answer_ptr = htons(DNS_TYPE_A_VAL);
            answer_ptr += sizeof(uint16_t);
            
            // 4.3. Classe (IN - Internet)
            *(uint16_t *)answer_ptr = htons(DNS_CLASS_IN_VAL);
            answer_ptr += sizeof(uint16_t);
            
            *(uint32_t *)answer_ptr = htonl(1); 
            answer_ptr += sizeof(uint32_t);
            
            *(uint16_t *)answer_ptr = htons(4); 
            answer_ptr += sizeof(uint16_t);
            
            ip_addr_t ip_addr;
            ipaddr_aton(s_ap_ip, &ip_addr); 
            *(uint32_t *)answer_ptr = ip_addr.u_addr.ip4.addr; 
            answer_ptr += sizeof(uint32_t);
            
            size_t total_len = answer_ptr - rx_buffer;
            int sent = sendto(sock, rx_buffer, total_len, 0, (struct sockaddr *)&client_addr, addr_len);
            if (sent < 0) {
                ESP_LOGE(TAG_DNS, "Erro no sendto: %d", errno);
            } else {
                ESP_LOGI(TAG_DNS, "DNS Redirecionado para %s", s_ap_ip);
            }
        }
    }
    
    if (sock != -1) {
        close(sock);
    }
    vTaskDelete(NULL);
}

//cria a tarefa FreeRTOS
esp_err_t start_dns_server(const char *ap_ip)
{
    if (strlen(ap_ip) > sizeof(s_ap_ip) - 1) {
        ESP_LOGE(TAG_DNS, "IP muito longo");
        return ESP_FAIL;
    }
    strncpy(s_ap_ip, ap_ip, sizeof(s_ap_ip) - 1);
    
    BaseType_t result = xTaskCreate(dns_server_task, 
                                    "dns_server", 
                                    4096, 
                                    NULL, 
                                    5,    
                                    NULL);

    if (result != pdPASS) {
        ESP_LOGE(TAG_DNS, "Falha ao criar tarefa DNS");
        return ESP_FAIL;
    }
    return ESP_OK;
}