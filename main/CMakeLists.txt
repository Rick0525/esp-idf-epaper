idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES epaper_uc8276_42bwr
    PRIV_REQUIRES esp_timer
)
