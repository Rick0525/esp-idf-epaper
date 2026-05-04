# 项目开发约定

## 计划与文档
- 按照项目中的计划进行编程，如果计划有变实时更新到计划文件中。
- 计划文件与代码生成的注释使用中文填写。

## 版本管理
- 采用 git 进行版本管理，原子化管理：每完成一部分并测试成功后进行保存（commit），并将所做的内容写入对应文档中。

## 构建环境
- 运行 `source ~/.espressif/tools/activate_idf_v6.0.1.sh` 激活 ESP-IDF 环境。
- 运行 `idf.py build` 构建项目。

## 已知坑与解决方法（节点 0 总结）

### 1. `idf.py` 是 zsh alias，在 `&&` 链中不会展开
- **现象**：`source activate_idf_v6.0.1.sh && idf.py build` 报 `command not found: idf.py`。
- **原因**：activate 脚本不是把 `idf.py` 加到 `PATH`，而是设为 zsh alias；同一行命令链里 alias 不会展开。
- **解决**：自动化脚本里直接走完整路径，避免依赖 alias：
  ```
  source ~/.espressif/tools/activate_idf_v6.0.1.sh; \
  /Users/rick/.espressif/tools/python/v6.0.1/venv/bin/python \
  /Users/rick/.espressif/v6.0.1/esp-idf/tools/idf.py \
  -C /Users/rick/Documents/ESP32Projects/eink_screen <子命令>
  ```
  用 `-C <项目路径>` 指定项目目录，无需 `cd`。

### 2. ESP-IDF v6.0 组件名变化与隐式 common requirements
- **现象**：`CMakeLists.txt` 写 `REQUIRES esp_log freertos` 报 `Failed to resolve component 'esp_log': unknown name`。
- **原因**：v6.0 中 `esp_log` 组件已重命名为 `log`；同时 `log` / `freertos` / `esp_common` / `esp_hw_support` 等是 IDF **common requirements**，会被自动注入到所有组件，**根本不需要手动列出**。
- **解决**：参照官方示例 `examples/get-started/hello_world/main/CMakeLists.txt` 的最简风格：
  ```cmake
  # main 组件
  idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES <自定义组件>)
  # 自定义组件
  idf_component_register(SRCS "..." INCLUDE_DIRS "include"
                         PRIV_REQUIRES esp_driver_spi esp_driver_gpio)
  ```
  只列**真正需要**的组件（如 `esp_driver_spi`、`esp_driver_gpio`），日志和 FreeRTOS 直接 `#include` 即可。

### 3. macOS 自带 shell 没有 `timeout` 命令
- **现象**：用 `timeout 6 idf.py monitor` 报 `command not found: timeout`。
- **解决**：用 Python 一行读串口替代（已在自动化里使用），或 `brew install coreutils` 后改用 `gtimeout`。

### 4. 串口 monitor 自动化方式
- 由于 `idf.py monitor` 会阻塞，自动验证启动日志时改用 Python 直接读 `/dev/cu.usbserial-10`：
  - 通过 RTS 引脚触发硬件 reset
  - 用 `pyserial` 读 4~6 秒输出
  - grep 关键日志（如 "boot ok"）判断验证通过
