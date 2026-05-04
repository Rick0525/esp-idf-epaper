# 顶层 CMakeLists：ESP-IDF 项目入口
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 仅构建 main 及其依赖，缩短编译时间
idf_build_set_property(MINIMAL_BUILD ON)

project(eink_screen)
