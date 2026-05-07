idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES epaper_il0373_154t8
    PRIV_REQUIRES esp_timer
)
