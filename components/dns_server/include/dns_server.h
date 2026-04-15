#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

#define DNS_PORT 53

/**
 * @brief Inicia a tarefa de servidor DNS.
 * * @param ap_ip O IP do Ponto de Acesso (normalmente 192.168.4.1) para onde os dominios serao redirecionados.
 * @return ESP_OK se iniciado com sucesso.
 */
esp_err_t start_dns_server(const char *ap_ip);

#endif 