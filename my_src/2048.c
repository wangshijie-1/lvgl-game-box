/**
 * @file 2048.c
 * @brief 2048 游戏逻辑与界面实现
 * @details 包含 4x4 棋盘数据、合并算法、随机数生成、UI 刷新及游戏界面创建
 */

#include "lvgl/lvgl.h"  // LVGL 核心头文件
#include <stdio.h>      // 标准输入输出
#include <stdint.h>     // 整数类型定义
#include <time.h>       // 时间函数
#include "2048.h"       // 2048 模块头文件
#include "0ui.h"        // UI 通用组件

// 2048 游戏界面屏幕对象
lv_obj_t* src2048 = NULL;

// 4x4 棋盘图像控件数组，每个格子对应一张 BMP 图片
lv_obj_t* img2048[SIZE_2048][SIZE_2048];

// 4x4 棋盘数据数组，记录每个格子的数字和合并状态
GeZi gezi[SIZE_2048][SIZE_2048];

// 数字 0、2、4、8...2048 对应的 BMP 图片路径表，索引 0~11
const char* bmp2048[12] = {
    "A:tu/game2048/0.bmp","A:tu/game2048/2.bmp","A:tu/game2048/4.bmp","A:tu/game2048/8.bmp",
    "A:tu/game2048/16.bmp","A:tu/game2048/32.bmp","A:tu/game2048/64.bmp","A:tu/game2048/128.bmp",
    "A:tu/game2048/256.bmp","A:tu/game2048/512.bmp","A:tu/game2048/1024.bmp","A:tu/game2048/2048.bmp",
};

// ---------------------------------------------------------
// 2048 核心逻辑部分
// ---------------------------------------------------------

/**
 * @brief 在随机空格子中生成一个新数字
 * @param gezi 棋盘数据数组
 * @details 先随机尝试 30 次寻找空位；若都失败则遍历棋盘强行放置。
 *          90% 概率生成 2，10% 概率生成 4。
 */
void suiji(GeZi gezi[SIZE_2048][SIZE_2048]) {
    int a = 0, b = 0;
    while ((a < 30) && (!b)) {
        int suijiy = lv_rand(0, SIZE_2048 - 1);
        int suijix = lv_rand(0, SIZE_2048 - 1);
        if (!gezi[suijiy][suijix].num) {
            // 90%概率生成2，10%概率生成4
            gezi[suijiy][suijix].num = (lv_rand(0, 9) < 9) ? 2 : 4;
            b = 1;
        }
        a++;
    }
    // 如果随机30次都没找到空位，就遍历强行塞一个
    if (!b) {
        for (int y = 0; y < SIZE_2048; y++) {
            for (int x = 0; x < SIZE_2048; x++) {
                if (!gezi[y][x].num) {
                    gezi[y][x].num = (lv_rand(0, 9) < 9) ? 2 : 4;
                    return;
                }
            }
        }
    }
}

/**
 * @brief 清除所有格子的合并标记
 * @param gezi 棋盘数据数组
 * @details 每次滑动操作前调用，防止同一回合内某个格子被多次合并
 */
void hebinghou(GeZi gezi[SIZE_2048][SIZE_2048]) {
    for (int y = 0; y < SIZE_2048; y++) {
        for (int x = 0; x < SIZE_2048; x++) {
            gezi[y][x].jisuan = 0;
        }
    }
}

/**
 * @brief 核心合并算法
 * @param gezi 棋盘数据数组
 * @param fangxiang 滑动方向：1=上，2=下，3=左，4=右
 * @details 按指定方向移动并合并相邻且数字相同的格子，已合并的格子本回合不再合并
 */
void hebing(GeZi gezi[SIZE_2048][SIZE_2048],int fangxiang) {
    switch (fangxiang) {
    case 1: // 上移
        for (int x = 0; x < SIZE_2048; x++) {
            for (int y = 1; y < SIZE_2048; y++) {
                if (!gezi[y][x].num) continue;
                for (int y1 = y - 1; y1 >= 0; y1--) {
                    if (!gezi[y1][x].num) {
                        gezi[y1][x].num = gezi[y1 + 1][x].num;
                        gezi[y1 + 1][x].num = 0;
                    }
                    else if (gezi[y1][x].num == gezi[y1 + 1][x].num && !gezi[y1][x].jisuan) {
                        gezi[y1][x].num *= 2;
                        gezi[y1 + 1][x].num = 0;
                        gezi[y1][x].jisuan = 1;
                        break;
                    }
                    else {
                        break;
                    }
                }
            }
        }
        break;

    case 2: // 下移 (修复了原先的致命越界 gezi[y1+1] -> gezi[y1-1])
        for (int x = 0; x < SIZE_2048; x++) {
            for (int y = SIZE_2048 - 2; y >= 0; y--) {
                if (!gezi[y][x].num) continue;
                for (int y1 = y + 1; y1 < SIZE_2048; y1++) {
                    if (!gezi[y1][x].num) {
                        gezi[y1][x].num = gezi[y1 - 1][x].num;
                        gezi[y1 - 1][x].num = 0;
                    }
                    else if (gezi[y1][x].num == gezi[y1 - 1][x].num && !gezi[y1][x].jisuan) {
                        gezi[y1][x].num *= 2;
                        gezi[y1 - 1][x].num = 0;
                        gezi[y1][x].jisuan = 1;
                        break;
                    }
                    else {
                        break;
                    }
                }
            }
        }
        break;

    case 3: // 左移
        for (int y = 0; y < SIZE_2048; y++) {
            for (int x = 1; x < SIZE_2048; x++) {
                if (!gezi[y][x].num) continue;
                for (int x1 = x - 1; x1 >= 0; x1--) {
                    if (!gezi[y][x1].num) {
                        gezi[y][x1].num = gezi[y][x1 + 1].num;
                        gezi[y][x1 + 1].num = 0;
                    }
                    else if (gezi[y][x1].num == gezi[y][x1 + 1].num && !gezi[y][x1].jisuan) {
                        gezi[y][x1].num *= 2;
                        gezi[y][x1 + 1].num = 0;
                        gezi[y][x1].jisuan = 1;
                        break;
                    }
                    else {
                        break;
                    }
                }
            }
        }
        break;

    case 4: // 右移 (修复了原先的致命越界)
        for (int y = 0; y < SIZE_2048; y++) {
            for (int x = SIZE_2048 - 2; x >= 0; x--) {
                if (!gezi[y][x].num) continue;
                for (int x1 = x + 1; x1 < SIZE_2048; x1++) {
                    if (!gezi[y][x1].num) {
                        gezi[y][x1].num = gezi[y][x1 - 1].num;
                        gezi[y][x1 - 1].num = 0;
                    }
                    else if (gezi[y][x1].num == gezi[y][x1 - 1].num && !gezi[y][x1].jisuan) {
                        gezi[y][x1].num *= 2;
                        gezi[y][x1 - 1].num = 0;
                        gezi[y][x1].jisuan = 1;
                        break;
                    }
                    else {
                        break;
                    }
                }
            }
        }
        break;

    default:
        break;
    }
}

/**
 * @brief 根据棋盘数据刷新 4x4 图像网格显示
 * @param gezi 棋盘数据数组
 * @details 将每个格子的数字映射到对应 BMP 图片索引，0 显示空白图片
 */
void show_2048(GeZi gezi[SIZE_2048][SIZE_2048]) {
    for (int y = 0; y < SIZE_2048; y++) {
        for (int x = 0; x < SIZE_2048; x++) {
            int num = gezi[y][x].num;
            int index = 0; // 默认显示 0.bmp

            // 用除法计算索引：2->1, 4->2, 8->3...
            if (num > 0 && num <= 2048) {
                index = 1;
                int tmp = num;
                while (tmp > 2) {
                    tmp /= 2;
                    index++;
                }
            }

            if (index >= 0 && index < 12) {
                lv_image_set_src(img2048[y][x], bmp2048[index]);
            }
        }
    }
}

/**
 * @brief 创建 4x4 图像网格
 * @param parent 父对象
 * @param x0 网格左上角 x 坐标
 * @param y0 网格左上角 y 坐标
 * @details 每个格子大小为 60x60 像素，初始显示空白图片
 */
void create_img(lv_obj_t* parent, int x0, int y0) {
    for (int y = 0; y < SIZE_2048; y++) {
        for (int x = 0; x < SIZE_2048; x++) {
            img2048[y][x] = lv_image_create(parent);
            lv_image_set_src(img2048[y][x], bmp2048[0]);
            lv_obj_set_pos(img2048[y][x], x * 60 + x0, y * 60 + y0);
        }
    }
}

// ---------------------------------------------------------
// 2048 UI 部分
// ---------------------------------------------------------

/**
 * @brief 2048 方向按钮事件回调
 * @param e LVGL 事件对象
 * @details 按下时仅播放缩放动画；抬手（RELEASED）时才执行合并、生成新数字并刷新界面，
 *          支持长按不触发
 */
void dir_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target(e);
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        // 按下时仅做视觉反馈，不执行移动或合并
        lv_obj_set_style_transform_width(btn, 10, 0);
        lv_obj_set_style_transform_height(btn, 10, 0);
    }
    else if (code == LV_EVENT_RELEASED) {
        // 抬手时恢复缩放并执行一次方向移动/合并
        lv_obj_set_style_transform_width(btn, 0, 0);
        lv_obj_set_style_transform_height(btn, 0, 0);
        hebing(gezi, dir);
        hebinghou(gezi);
        suiji(gezi);
        show_2048(gezi);
    }
}


/**
 * @brief 创建 2048 游戏界面
 * @details 初始化游戏屏幕、设置背景图、创建 4x4 棋盘网格和方向控制按钮
 */
void creat_my_2048(void) {
    // 创建 2048 游戏屏幕对象
    src2048 = lv_obj_create(NULL);

    suiji(gezi);
    suiji(gezi);

    // 设置 2048 游戏界面背景图（底板）
    lv_obj_t* diban_2048 = lv_image_create(src2048);
    lv_image_set_src(diban_2048, "A:tu/game2048/game20481.bmp");
    lv_obj_set_pos(diban_2048, 0, 0);

    // 设置 2048 游戏界面背景图（上层装饰）
    lv_obj_t* beijing_2048 = lv_image_create(src2048);
    lv_image_set_src(beijing_2048, "A:tu/game2048/game.bmp");
    lv_obj_set_pos(beijing_2048, 0, 0);

    // 创建 4x4 棋盘图像网格，起始位置 (180, 180)
    create_img(src2048, 180, 180);
    // 关键修复：网格创建后必须立即把 gezi 数据刷新到图片控件，否则初始随机格子不会显示
    show_2048(gezi);

    // 方向控制按钮样式：樱花粉背景、琉璃色文字
    int fxj = style_btn_init(0, 0xFFB7C5, 0x1B83AB, 28);
    // 返回按钮样式：深色背景、白色文字
    int fhj = style_btn_init(0, 0x2B2B2B, 0xF7F9FA, 28);

    // 创建上、下、左、右四个方向按钮，按键常态不透明度 70%，文字不透明
    lv_obj_t* btn_up    = create_button(fxj, src2048, LV_ALIGN_CENTER, 260, -50, 80, 120, "上", dir_btn_cb, (void*)1);
    lv_obj_t* btn_down  = create_button(fxj, src2048, LV_ALIGN_CENTER, 260, 150, 80, 120, "下", dir_btn_cb, (void*)2);
    lv_obj_t* btn_left  = create_button(fxj, src2048, LV_ALIGN_CENTER, 160, 50, 120, 80, "左", dir_btn_cb, (void*)3);
    lv_obj_t* btn_right = create_button(fxj, src2048, LV_ALIGN_CENTER, 360, 50, 120, 80, "右", dir_btn_cb, (void*)4);
    lv_obj_set_style_bg_opa(btn_up,    LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_down,  LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_left,  LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_right, LV_OPA_70, LV_STATE_DEFAULT);

    // 创建返回按钮，按键常态不透明度 70%，文字不透明
    lv_obj_t* btn_back_2048 = create_button(fhj, src2048, LV_ALIGN_TOP_RIGHT, 0, 0, 100, 50, "返回", go_to_sreen_cb, (void*)1);
    lv_obj_set_style_bg_opa(btn_back_2048, LV_OPA_70, LV_STATE_DEFAULT);
}