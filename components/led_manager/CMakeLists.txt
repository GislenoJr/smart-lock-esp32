idf_component_register(
    SRCS "led_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES 
        driver      
        freertos    
)