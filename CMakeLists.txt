cmake_minimum_required(VERSION 3.10)

# 设置交叉编译
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER   /usr/local/arm-linux/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER  /usr/local/arm-linux/bin/aarch64-linux-gnu-g++)

project(lvgl_project)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 导出 compile_commands.json，供 VS Code C/C++ 扩展提供精确 IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)



# 设置可执行文件输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 关键修改：将 lvgl 编译为动态库，并设置动态库输出到 bin 目录
# 用 option() 提前创建缓存变量，子目录中的 option() 看到已存在就不会覆盖
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 关键修改：禁用 lvgl 的 examples/demos 独立库，避免生成多个 .so
# thorvg 不能禁用，因为 lvgl 内部调用了它的接口，需要把它静态合并进 liblvgl.so
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON)
set(LV_CONF_BUILD_DISABLE_DEMOS ON)

# 关键修改：设置 rpath 为 $ORIGIN，让板子上的可执行文件在同级目录自动找到动态库
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# 关键修改：创建自定义命令将lv_conf.h复制到lvgl目录
configure_file(
    ${PROJECT_SOURCE_DIR}/my_src/lv_conf.h
    ${PROJECT_SOURCE_DIR}/lvgl/lv_conf.h
    COPYONLY
)

# 添加头文件包含目录
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/lvgl
    ${PROJECT_SOURCE_DIR}/my_src
    
)

# 添加子目录
add_subdirectory(lvgl)
add_subdirectory(my_src)

# 关键修改：强制 lvgl 相关动态库输出到 bin 目录（覆盖 lvgl 内部默认的 CMAKE_BINARY_DIR/lib）
set(LVGL_SHARED_TARGETS lvgl)
if(TARGET lvgl_examples)
    list(APPEND LVGL_SHARED_TARGETS lvgl_examples)
endif()
if(TARGET lvgl_demos)
    list(APPEND LVGL_SHARED_TARGETS lvgl_demos)
endif()
if(TARGET lvgl_thorvg)
    list(APPEND LVGL_SHARED_TARGETS lvgl_thorvg)
endif()
set_target_properties(${LVGL_SHARED_TARGETS} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
)
