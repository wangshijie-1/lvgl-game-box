/**
 * @file mole.h
 * @brief 打地鼠游戏模块接口
 * @details 对外提供游戏屏幕对象、界面创建与进图开局函数
 */

#ifndef __MOLE_H__
#define __MOLE_H__

#include "lvgl/lvgl.h"

// 打地鼠游戏界面屏幕对象（由 layout.c 的 gamebox_goto 切换加载）
extern lv_obj_t* src_mole;

// 创建并初始化打地鼠游戏界面（由 creat_my_ui 调用一次）
void creat_my_mole(void);

// 进入打地鼠界面时开局（由 gamebox_goto 调用，重置计分并开始计时）
void mole_enter(void);

#endif
