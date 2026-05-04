# 变更日志

按节点记录每次原子化提交的功能与验证结果。

## 节点 0 — 项目骨架

- 创建目录结构：`main/`、`components/epaper_154/`（含 `include/`）、`docs/`
- `CMakeLists.txt`（顶层）、`main/CMakeLists.txt`、`components/epaper_154/CMakeLists.txt`
- `sdkconfig.defaults`：`CONFIG_IDF_TARGET="esp32"`、`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y`、`CONFIG_LOG_DEFAULT_LEVEL_INFO=y`
- `.gitignore`：`build/`、`sdkconfig`、`sdkconfig.old`、`managed_components/`、`dependencies.lock`、`.DS_Store` 等
- `main/main.c`：仅 `ESP_LOGI(TAG, "boot ok")`
- `components/epaper_154/`：占位 `epaper_154.h` / `epaper_154.c`，后续节点填充
- `docs/PLAN.md` 落盘
- 验证：`idf.py set-target esp32` → `idf.py build` 通过 → `idf.py -p /dev/cu.usbserial-10 flash monitor` 看到 "boot ok"
