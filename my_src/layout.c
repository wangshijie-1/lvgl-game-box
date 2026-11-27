/**
 * @file layout.c
 * @brief 应用主界面布局实现
 * @details 负责创建登录界面、游戏盒子选择界面，并协调 2048 游戏界面的初始化
 */

#include "lvgl/lvgl.h"    // LVGL 核心头文件
#include <stdio.h>        // 标准输入输出
#include <stdint.h>       // 整数类型定义
#include <time.h>         // 时间函数
#include "2048.h"         // 2048 游戏模块接口
#include "game15.h"       // 数字华容道游戏模块接口
#include "flipcard.h"     // 记忆翻牌游戏模块接口
#include "mole.h"         // 打地鼠游戏模块接口
#include "0ui.h"          // UI 通用组件与全局资源

// 登录主界面屏幕对象
lv_obj_t* src0 = NULL;
lv_obj_t* src1 = NULL;

// 游戏盒子水平滚动选择界面屏幕对象
lv_obj_t* gamebox = NULL;

// RPG 游戏界面屏幕对象（预留）
lv_obj_t* srcrpg = NULL;

/**
 * @brief 创建游戏盒子水平滚动界面
 * @details 设置界面宽度为屏幕宽度的 3 倍，允许水平方向拖拽浏览游戏图标
 */
void create_gamebox_scroll(void) {
    gamebox = lv_obj_create(NULL);
    lv_obj_set_content_width(gamebox, 1024 * 3);
    lv_obj_set_scroll_dir(gamebox, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(gamebox, LV_SCROLL_SNAP_NONE);
}

/**
 * @brief 游戏盒子按钮事件回调
 * @param e LVGL 事件对象
 * @details 根据 user_data 切换回登录界面或进入 2048 游戏界面
 */
void gamebox_goto(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target(e);
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);

    // 按下时仅做视觉反馈；抬手时恢复并执行屏幕切换，支持长按不触发
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_transform_width(btn, 10, 0);
        lv_obj_set_style_transform_height(btn, 10, 0);
        // 若按钮内第一个子对象是图片，按下时同步降低图片不透明度到 50%
        lv_obj_t* img = lv_obj_get_child(btn, 0);
        if (img != NULL && lv_obj_check_type(img, &lv_image_class)) {
            lv_obj_set_style_image_opa(img, LV_OPA_50, LV_STATE_DEFAULT);
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_transform_width(btn, 0, 0);
        lv_obj_set_style_transform_height(btn, 0, 0);
        // 释放时恢复图片不透明度到 70%
        lv_obj_t* img = lv_obj_get_child(btn, 0);
        if (img != NULL && lv_obj_check_type(img, &lv_image_class)) {
            lv_obj_set_style_image_opa(img, LV_OPA_70, LV_STATE_DEFAULT);
        }
        // 抬手才执行屏幕切换
        switch (dir)
        {
        case 0:
            lv_screen_load(src0);
            break;
        case 1:
            lv_screen_load(src2048);
            break;
        case 2:
            lv_screen_load(src_game15);
            break;
        case 3:
            lv_screen_load(src_flipcard);
            break;
        case 4:
            lv_screen_load(src_mole);
            mole_enter();      // 进图即开局（重置计分并开始 30 秒回合）
            break;
        case 5:
            break;
        default:
            break;
        }
    }
}

/**
 * @brief 创建并加载整个应用的主 UI
 * @details 加载中文字体、创建登录界面、游戏盒子界面和 2048 游戏界面，
 *          最后默认显示登录界面
 */
void creat_my_ui(void) {
    // 中文字体统一由 0ui.c 的 ui_font_get() 按字号懒加载并缓存，
    // 这里不再单独加载全局字体（原 my_font 变量已移除），避免重复加载 TTF 耗尽内存
    // 创建登录主界面屏幕
    src0 = lv_obj_create(NULL);

    // 初始化按钮样式池
    style_btn_init0();

    // 设置登录界面背景图
    lv_obj_t* zhuyemian = lv_image_create(src0);
    lv_image_set_src(zhuyemian, "A:tu/zhujiemian.bmp");
    lv_obj_set_pos(zhuyemian, 0, 0);


    // 2026-11-27：注释登录主界面的两个调试用人物 GIF（ren1/gj），不再显示。
    // 原因：早期联调时用于观察刷新，现属冗余元素；恢复时取消注释即可
    // lv_obj_t* gif1 = lv_gif_create(src0);
    // lv_gif_set_src(gif1, "A:/home/KK/3-gamebox/tu/renwu/ren1.gif");
    // lv_obj_set_pos(gif1, 50, 50);

    // lv_obj_t* gif2 = lv_gif_create(src0);
    // lv_gif_set_src(gif2, "A:/home/KK/3-gamebox/tu/renwu/gj.gif");
    // lv_obj_set_pos(gif2, 0, 0);


    src1 = lv_obj_create(src0);
    lv_obj_set_size(src1, 500, 350);
    lv_obj_align(src1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(src1, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_bg_opa(src1, LV_OPA_50, 0);
    // 防止点击 src1 背景时抢走文本域焦点，导致键盘一闪而逝
    lv_obj_remove_flag(src1, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // 创建虚拟键盘，父对象为登录面板 src1，初始隐藏，聚焦文本域时显示
    lv_obj_t* kb = lv_keyboard_create(src0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(kb, 1024, 300);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_FLOATING);


    // 创建“登录”按钮：白底金字，按键不透明度 80%，文字不透明
    int denglu = style_btn_init(0, 0xF7F9FA, 0xD4AF27, 28);
    lv_obj_t* btn_login = create_button(denglu, src1, LV_ALIGN_CENTER, 0, 80, 120, 80, "登录", go_to_sreen_cb, (void*)1);
    lv_obj_set_style_bg_opa(btn_login, LV_OPA_80, LV_STATE_DEFAULT);

    lv_obj_t* zhanghao = lv_label_create(src1);
    lv_obj_align(zhanghao, LV_ALIGN_TOP_LEFT, 30, 56);
    lv_label_set_text(zhanghao, "账号：");
    lv_obj_add_style(zhanghao, &Btn[0].lab, LV_STATE_DEFAULT);

    lv_obj_t* mima = lv_label_create(src1);
    lv_obj_align(mima, LV_ALIGN_TOP_LEFT, 30, 106);
    lv_label_set_text(mima, "密码：");
    lv_obj_add_style(mima, &Btn[0].lab, LV_STATE_DEFAULT);

    create_textbox(src1, LV_ALIGN_TOP_LEFT, 120, 50, 300, 50, "请输入账号", 0xFFFFFF, 24, kb, 0);
    create_textbox(src1, LV_ALIGN_TOP_LEFT, 120, 100, 300, 50, "请输入密码", 0xFFFFFF, 24, kb, 1);

    

    // 创建游戏盒子水平滚动界面
    create_gamebox_scroll();


    lv_obj_t* box_beijing = lv_image_create(gamebox);
    lv_image_set_src(box_beijing, "A:tu/gamebox1.bmp");
    lv_obj_set_pos(box_beijing, 0, 0);
    // 创建游戏盒子中的“返回”按钮：黑底白字，按键不透明度 70%，文字不透明
    int gamebox_fhj = style_btn_init(1, 0x2B2B2B, 0xF7F9FA, 28);
    lv_obj_t* btn_back_box = create_button(gamebox_fhj, gamebox, LV_ALIGN_TOP_RIGHT, 0, 0, 100, 50, "返回", gamebox_goto, (void*)0);
    lv_obj_set_style_bg_opa(btn_back_box, LV_OPA_70, LV_STATE_DEFAULT);

    // 创建“2048”游戏入口按钮：白底，按键和图片不透明度均为 70%
    int gamebox_2048 = style_btn_init(2, 0xFFFFFF, 0xFFFFFF, 28);
    lv_obj_t* box_2048 = create_button(gamebox_2048, gamebox, LV_ALIGN_TOP_LEFT, 100, 100, 150, 150, "", gamebox_goto, (void*)1);
    lv_obj_set_style_bg_opa(box_2048, LV_OPA_70, LV_STATE_DEFAULT);

    // 在 2048 入口按钮上叠加游戏封面图，常态不透明度 70%
    lv_obj_t* box_2048_tu = lv_image_create(box_2048);
    lv_image_set_src(box_2048_tu, "A:tu/game2048/box_2048.bmp");
    lv_obj_set_pos(box_2048_tu, -20, -11);
    lv_obj_set_style_image_opa(box_2048_tu, LV_OPA_70, LV_STATE_DEFAULT);

    // 初始化 2048 游戏界面
    creat_my_2048();

    // 创建“华容道”游戏入口按钮（白底文字）
    int gamebox_15 = style_btn_init(3, 0xFFFFFF, 0x2B2B2B, 28);
    lv_obj_t* box_15 = create_button(gamebox_15, gamebox, LV_ALIGN_TOP_LEFT, 300, 100, 150, 150, "华容道", gamebox_goto, (void*)2);
    lv_obj_set_style_bg_opa(box_15, LV_OPA_70, LV_STATE_DEFAULT);

    // 创建“翻牌”游戏入口按钮
    int gamebox_fc = style_btn_init(3, 0xFFFFFF, 0x2B2B2B, 28);
    lv_obj_t* box_fc = create_button(gamebox_fc, gamebox, LV_ALIGN_TOP_LEFT, 500, 100, 150, 150, "翻牌", gamebox_goto, (void*)3);
    lv_obj_set_style_bg_opa(box_fc, LV_OPA_70, LV_STATE_DEFAULT);

    // 创建“打地鼠”游戏入口按钮
    int gamebox_ms = style_btn_init(3, 0xFFFFFF, 0x2B2B2B, 28);
    lv_obj_t* box_ms = create_button(gamebox_ms, gamebox, LV_ALIGN_TOP_LEFT, 700, 100, 150, 150, "打地鼠", gamebox_goto, (void*)4);
    lv_obj_set_style_bg_opa(box_ms, LV_OPA_70, LV_STATE_DEFAULT);

    // 初始化三个游戏界面
    creat_my_game15();
    creat_my_flipcard();
    creat_my_mole();

    // 默认加载登录界面
    lv_screen_load(src0);
}

/**
 * @brief 通用界面切换回调（登录按钮 / 2048 返回按钮共用）
 * @param e LVGL 事件对象
 * @details user_data == 1 时进入游戏盒子界面
 */
void go_to_sreen_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    intptr_t d = (intptr_t)lv_event_get_user_data(e);
    if (d == 1) lv_screen_load(gamebox);
}
