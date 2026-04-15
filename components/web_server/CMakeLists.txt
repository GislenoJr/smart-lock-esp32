idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"

    PRIV_INCLUDE_DIRS 
        "../../main/include" 
        
    PRIV_REQUIRES 
        freertos
        esp_http_server
        esp_event
        log
        json
)