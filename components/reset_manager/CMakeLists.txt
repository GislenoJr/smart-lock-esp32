idf_component_register(
    SRCS "reset_manager.c"
    INCLUDE_DIRS "include"

    PRIV_INCLUDE_DIRS 
        "../../main/include"
        
    PRIV_REQUIRES 
        driver          
        freertos
        nvs_manager     
)