#ifndef MAIN_H
#define MAIN_H

#include "esp_err.h"

void forcar_reiniciar_tarefa(void *argumento);
void config_save_and_restart(const char *ssid, const char *password);
void tarefa_liberar_trava(void *pvParameters);

#endif 