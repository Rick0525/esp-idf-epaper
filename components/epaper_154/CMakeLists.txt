# 1.54 寸单色墨水屏（IL0373 / GDEW0154T8）驱动组件
# log / freertos 是 IDF common requirements，自动加入，无需列出
# fonts 暴露给上层 #include "FreeSansBold9pt7b.h" 等 GFX 字体头
idf_component_register(
    SRCS "epaper_154.c"
    INCLUDE_DIRS "include" "fonts"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio
)
