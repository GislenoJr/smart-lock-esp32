# Tente esta versao se a primeira falhar, usando apenas uma lista:

idf_component_register(
    SRCS 
        "rfid_reader.c"
        "mfrc522.c"     
    
    PRIV_INCLUDE_DIRS 
        "../../main/include" 
        
    INCLUDE_DIRS "include"
    
    REQUIRES 
        freertos
        log
        esp_driver_spi
        esp_event
        driver
)