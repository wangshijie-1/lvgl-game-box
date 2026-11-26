/**
 * @file flipcard.h
 * @brief 记忆翻牌配对游戏模块接口
 * @details 对外提供游戏屏幕对象与界面创建函数，由 layout.c 统一切换加载
 */

#ifndef __FLIPCARD_H__
#define __FLIPCARD_H__

#include "lvgl/lvgl.h"

// 记忆翻牌游戏界面屏幕对象（由 layout.c 的 gamebox_goto 切换加载）
extern lv_obj_t* src_flipcard;

// 创建并初始化记忆翻牌游戏界面（由 creat_my_ui 调用一次）
void creat_my_flipcard(void);

#endif
