# 4.2 寸黑白红墨水屏（UC8176/UC8276/IL0398 系列）驱动组件
# log / freertos 是 IDF common requirements，自动加入，无需列出
# fonts 暴露给上层 #include "FreeSansBold9pt7b.h" 等 GFX 字体头
idf_component_register(
    SRCS "epaper_42bwr.c"
    INCLUDE_DIRS "include" "fonts"
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio
)
