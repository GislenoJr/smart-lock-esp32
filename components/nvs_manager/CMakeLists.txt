idf_component_register(
    SRCS "nvs_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES 
        nvs_flash 
)