# SmartLock/components/wifi_ap/CMakeLists.txt

idf_component_register(
    SRCS "wifi_ap.c"
    INCLUDE_DIRS 
        "include"
    PRIV_REQUIRES 
        freertos 
        esp_event 
        esp_wifi 
        log
        esp_netif 
)