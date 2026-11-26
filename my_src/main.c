/**
 * @file main.c
 * @brief 程序入口：初始化 LVGL、帧缓冲显示和触摸输入，进入主循环
 */

#include "lvgl/lvgl.h"
// #include "demos/lv_demos.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "layout.h"

/**
 * @brief 程序入口
 * @details 完成 LVGL 初始化、Linux 帧缓冲设备与 evdev 输入设备绑定，
 *          创建 UI 后进入周期性任务处理循环。
 */
int main(void)
{
    // 用当前时间初始化随机数种子，保证 2048 游戏随机性
    lv_rand_set_seed((uint32_t)time(NULL));
    lv_init();

    // 创建 Linux 帧缓冲显示设备并指定 framebuffer 节点
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp,"/dev/fb0");

    // 创建指针类输入设备（触摸屏），绑定 event 节点
    lv_indev_t* indev = lv_evdev_create(LV_INDEV_TYPE_POINTER,"/dev/input/event6");

    // 创建所有界面
    creat_my_ui();

    // LVGL 主循环：每 5ms 处理一次定时器和刷新任务
    while (1)
    {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}
