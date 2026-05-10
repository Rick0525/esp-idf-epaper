# 切屏改这两行：
#   - SRCS  选 ../demos/demo_<screen>.c
#   - REQUIRES 选对应驱动组件
# main.c 是 3 行调度器，不要改。
idf_component_register(
    SRCS "main.c" "../demos/demo_42bwr.c"
    INCLUDE_DIRS "."
    REQUIRES epaper_uc8276_42bwr
    PRIV_REQUIRES esp_timer
)
