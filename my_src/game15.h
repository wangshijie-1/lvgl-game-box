/**
 * @file game15.h
 * @brief 数字华容道游戏模块接口
 * @details 对外提供游戏屏幕对象与界面创建函数，由 layout.c 统一切换加载
 */

#ifndef __GAME15_H__
#define __GAME15_H__

#include "lvgl/lvgl.h"

// 数字华容道游戏界面屏幕对象（由 layout.c 的 gamebox_goto 切换加载）
extern lv_obj_t* src_game15;

// 创建并初始化数字华容道游戏界面（由 creat_my_ui 调用一次）
void creat_my_game15(void);

#endif
