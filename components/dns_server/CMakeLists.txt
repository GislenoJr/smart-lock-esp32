idf_component_register(
    SRCS "dns_server.c"
    INCLUDE_DIRS "include"
    # O servidor DNS simples utiliza a pilha LwIP (Lightweight IP)
    PRIV_REQUIRES 
        freertos
        lwip
        log
)