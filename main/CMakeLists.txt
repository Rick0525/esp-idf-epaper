idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES epaper_154
    PRIV_REQUIRES esp_timer
)
