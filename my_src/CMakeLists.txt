# 1. 源文件清单：显式列出，不再用 file(GLOB)。
# 原因：GLOB 在"新增 .c 文件"时无法可靠刷新目标源列表。CONFIGURE_DEPENDS 在 Makefile
# 生成器下只会打印 "GLOB mismatch!"，本次构建仍按旧列表编译（snake_input.c 没被编译，
# 链接报 undefined reference），必须再手动重配一次才生效。显式清单行为可预期，
# 新增源文件时在下面补一行即可。
set(MAIN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/0ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/2048.c
    ${CMAKE_CURRENT_SOURCE_DIR}/layout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mouse_cursor_icon.c
)

# 2. 将上述源文件添加到可执行文件中
add_executable(main ${MAIN_SRCS})


# 确保在链接前 lv_conf.h 已被复制到正确位置
add_dependencies(main lvgl)

# 链接库
# lvgl: LVGL 核心库（examples/demos/thorvg 已在根 CMakeLists 中禁用，不再单独生成 .so）
# m: 数学库
# pthread: POSIX 线程库
target_link_libraries(main
    lvgl
    m
    pthread
)

# 添加自定义目标以便一键运行：编译完成后执行 main
add_custom_target(run 
    COMMAND ${EXECUTABLE_OUTPUT_PATH}/main 
    DEPENDS main
)