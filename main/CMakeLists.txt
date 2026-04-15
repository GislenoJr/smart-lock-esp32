idf_component_register(
    SRCS 
        "main.c"

    INCLUDE_DIRS 
        "include"
        "."

    REQUIRES
        nvs_flash
        wifi_ap
        dns_server
        web_server
        esp_event
        esp_netif
        esp_wifi
        freertos
        nvs_manager
        reset_manager
        led_manager
        esp_timer
        rfid_reader
        driver
        mqtt_manager

    PRIV_REQUIRES
        esp_http_client
        esp-tls
)