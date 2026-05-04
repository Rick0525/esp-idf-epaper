# 1.54 寸单色墨水屏（SSD1681 兼容）驱动组件
# log / freertos 是 IDF common requirements，自动加入，无需列出
idf_component_register(
    SRCS "epaper_154.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio
)
