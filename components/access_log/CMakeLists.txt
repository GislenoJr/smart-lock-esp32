idf_component_register(
    SRCS "src/access_log.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt_manager spiffs
)
