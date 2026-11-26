/**
 * @file game15.c
 * @brief 数字华容道游戏逻辑与界面实现
 * @details 4x4 滑块拼图：点击空格相邻的数字块使其滑入空格，目标是把
 *          1~15 复原成顺序排列。
 *
 * 开局生成方式（关键设计）：从"已复原"状态出发倒着随机走若干合法步。
 * 这样生成的局面一定可解——数字华容道随机排列中恰好一半在数学上无解
 * （判定依据是逆序数的奇偶性，4x4 还要再叠加空格所在行数），直接乱放
 * 数字有 50% 概率得到一个永远拼不回来的死局。
 */

#include "lvgl/lvgl.h"    // LVGL 核心头文件
#include <stdio.h>        // 标准输入输出
#include <stdint.h>       // intptr_t 等整数类型
#include "game15.h"       // 本模块头文件
#include "0ui.h"          // UI 通用组件

// 游戏界面屏幕对象
lv_obj_t* src_game15 = NULL;

// 4x4 数字块按钮与其文字标签（标签为按钮的第 0 个子对象）
static lv_obj_t* tiles[16];
static lv_obj_t* tile_labs[16];

// 棋盘数据：board[i] 表示 i 号位置上的数字，0 表示空格
static int board[16];

// 已走步数
static int moves = 0;

// 状态标签（显示步数 / 胜利提示）
static lv_obj_t* status_lab = NULL;

// 棋盘网格布局参数：左上角起点与格间距（格子 100x100，间距 10）
#define G15_X0     297
#define G15_Y0     140
#define G15_STEP   110

// 按钮样式槽索引（creat_my_game15 中初始化后记录）
static int tile_style = 0;

/**
 * @brief 判断当前棋盘是否已复原
 * @return 已复原返回 1，否则返回 0
 */
static int is_win(void) {
    for (int i = 0; i < 15; i++) {
        if (board[i] != i + 1) return 0;
    }
    return board[15] == 0;
}

/**
 * @brief 从复原态出发随机走 walk 步合法移动
 * @details 每一步把空格与随机一个相邻块交换；只走合法步保证可解
 */
static void shuffle_walk(int walk) {
    for (int i = 0; i < 16; i++) board[i] = (i == 15) ? 0 : i + 1;

    int z = 15;   // 空格位置
    while (walk-- > 0) {
        int col = z % 4, row = z / 4;
        int cand[4], n = 0;
        if (col > 0) cand[n++] = z - 1;
        if (col < 3) cand[n++] = z + 1;
        if (row > 0) cand[n++] = z - 4;
        if (row < 3) cand[n++] = z + 4;

        int pick = cand[lv_rand(0, n - 1)];
        board[z] = board[pick];
        board[pick] = 0;
        z = pick;
    }
}

/**
 * @brief 生成新开局（保证可解，且不等于复原态）
 */
static void game15_new_board(void) {
    do {
        shuffle_walk(300);
    } while (is_win());
    moves = 0;
}

/**
 * @brief 把棋盘数据刷新到按钮文字上（空格显示空白）
 */
static void render_board(void) {
    char buf[8];
    for (int i = 0; i < 16; i++) {
        if (board[i] == 0) {
            lv_label_set_text(tile_labs[i], "");
        }
        else {
            snprintf(buf, sizeof(buf), "%d", board[i]);
            lv_label_set_text(tile_labs[i], buf);
        }
    }
}

/**
 * @brief 刷新状态标签文字
 */
static void update_status(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "步数：%d", moves);
    lv_label_set_text(status_lab, buf);
}

/**
 * @brief 重开一局（重开按钮 / 胜利弹窗确定共用）
 * @details 对弹窗回调直接复用：重开是幂等操作，多次调用无副作用
 */
static void game15_restart(void) {
    game15_new_board();
    render_board();
    update_status();
}

/**
 * @brief 重开按钮回调（仅响应 CLICKED，过滤按下/抬起事件）
 */
static void game15_restart_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    game15_restart();
}

/**
 * @brief 胜利弹窗"确定"回调（再来一局）
 */
static void game15_dialog_ok(lv_event_t* e) {
    game15_restart();
}

/**
 * @brief 数字块点击回调
 * @details user_data 为被点击块的位置编号；若与空格相邻则交换并刷新。
 *          仅响应 CLICKED，过滤按下/抬起事件
 */
static void tile_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    // 找到空格位置
    int z = -1;
    for (int i = 0; i < 16; i++) {
        if (board[i] == 0) { z = i; break; }
    }
    if (z < 0) return;

    // 判断被点块与空格是否相邻（同行相邻列，或同列相邻行）
    int dr = idx / 4 - z / 4;
    int dc = idx % 4 - z % 4;
    if ((dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1))) {
        board[z] = board[idx];
        board[idx] = 0;
        moves++;
        render_board();
        update_status();

        if (is_win()) {
            char msg[96];
            snprintf(msg, sizeof(msg), "复原成功，共 %d 步！再来一局？", moves);
            lv_label_set_text(status_lab, "复原成功！");
            create_confirm_dialog(msg, 24, game15_dialog_ok, NULL);
        }
    }
}

/**
 * @brief 创建数字华容道游戏界面
 * @details 创建屏幕、标题、状态栏、4x4 数字块网格与控制按钮
 */
void creat_my_game15(void) {
    src_game15 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(src_game15, lv_color_hex(0x2B3856), LV_STATE_DEFAULT);

    // 标题与状态栏
    create_label(src_game15, 0xF7F9FA, LV_ALIGN_TOP_MID, 0, 12, "数字华容道：点击滑块复原 1~15", 28);
    status_lab = create_label(src_game15, 0x88E0A0, LV_ALIGN_TOP_MID, 0, 62, "步数：0", 24);

    // 数字块样式：燕麦色底、深灰数字
    tile_style = style_btn_init(4, 0xE8DCC0, 0x3B3B3B, 28);

    // 4x4 数字块网格
    for (int i = 0; i < 16; i++) {
        tiles[i] = create_button(tile_style, src_game15, LV_ALIGN_TOP_LEFT,
            G15_X0 + (i % 4) * G15_STEP, G15_Y0 + (i / 4) * G15_STEP,
            100, 100, "", tile_cb, (void*)(intptr_t)i);
        // 文字标签是按钮的第 0 个子对象，缓存下来供刷新用
        tile_labs[i] = lv_obj_get_child(tiles[i], 0);
    }

    // 返回游戏盒子按钮（深色底白字，与 2048 界面一致）
    int fhj = style_btn_init(0, 0x2B2B2B, 0xF7F9FA, 28);
    lv_obj_t* btn_back = create_button(fhj, src_game15, LV_ALIGN_TOP_RIGHT, 0, 0, 100, 50,
        "返回", go_to_sreen_cb, (void*)1);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_70, LV_STATE_DEFAULT);

    // 重开按钮（左上角，避开底部棋盘区）
    int chongkai = style_btn_init(1, 0x4A6FA5, 0xF7F9FA, 24);
    lv_obj_t* btn_restart = create_button(chongkai, src_game15, LV_ALIGN_TOP_LEFT, 0, 0, 120, 60,
        "重开", game15_restart_cb, NULL);
    lv_obj_set_style_bg_opa(btn_restart, LV_OPA_80, LV_STATE_DEFAULT);

    // 生成开局并渲染（creat_my_ui 启动时只创建界面，进入即有牌面）
    game15_new_board();
    render_board();
    update_status();
}
