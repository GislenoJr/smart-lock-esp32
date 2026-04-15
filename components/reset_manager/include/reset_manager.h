#ifndef RESET_MANAGER_H
#define RESET_MANAGER_H

#include "esp_err.h"

/**
 * @brief Inicia a tarefa de monitoramento do botão de reset de fábrica.
 * @note Esta função executa a lógica de long-press e chama o restart global.
 */
void reset_manager_start_monitor(void);

#endif // RESET_MANAGER_H