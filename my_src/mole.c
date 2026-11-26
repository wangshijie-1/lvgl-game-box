/**
 * @file mole.c
 * @brief 打地鼠游戏逻辑与界面实现
 * @details 3x3 共 9 个地洞，两个常驻 LVGL 定时器驱动整局游戏：
 *          地鼠定时器（750ms 随机换洞）与回合时钟（1 秒倒计时 30 秒）。
 *
 * 定时器管理要点：两个定时器在界面创建时建立、常驻不销毁，靠
 * running 标志位门控——不在界面时定时器空转直接返回，进入界面
 * 由 mole_enter() 复位开局。这比"进界面建定时器、退界面删定时器"
 * 简单可靠，不会出现定时器指针悬空的问题。
 *
 * 离开界面使用专用返回回调（先停局再切屏），避免回合在后台空跑。
 */

#include "lvgl/lvgl.h"    // LVGL 核心头文件
#include <stdio.h>        // 标准输入输出
#include <stdint.h>       // intptr_t 等整数类型
#include "mole.h"         // 本模块头文件
#include "0ui.h"          // UI 通用组件

// 游戏界面屏幕对象
lv_obj_t* src_mole = NULL;

// 9 个地洞按钮与其文字标签
static lv_obj_t* holes[9];
static lv_obj_t* hole_labs[9];

// 回合状态：是否进行中
static int running = 0;

// 得分 / 失误 / 剩余秒数
static int score = 0;
static int miss = 0;
static int time_left = 30;

// 当地鼠所在的地洞编号（-1 表示没有地鼠在场）
static int active = -1;

// 地鼠定时器与回合时钟
static lv_timer_t* mole_timer = NULL;
static lv_timer_t* clock_timer = NULL;

// 状态标签
static lv_obj_t* status_lab = NULL;

// 网格布局参数：3x3，洞 120x120，间距 20（步长 140）
#define MOLE_X0     312
#define MOLE_Y0     130
#define MOLE_STEP   140

// 回合时长（秒）
#define MOLE_ROUND  30

// 地洞常态 / 地鼠在场的颜色
#define HOLE_COLOR      0xD2B48C
#define MOLE_COLOR      0x8B5A2B

// 游戏盒子界面（layout.c 定义，离开本游戏时切回）
extern lv_obj_t* gamebox;

/**
 * @brief 刷新状态标签文字
 */
static void update_status(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "得分 %d    失误 %d    剩余 %d 秒", score, miss, time_left);
    lv_label_set_text(status_lab, buf);
}

/**
 * @brief 地鼠出现：点亮指定地洞
 */
static void hole_on(int i) {
    lv_label_set_text(hole_labs[i], "鼠");
    lv_obj_set_style_bg_color(holes[i], lv_color_hex(MOLE_COLOR), LV_STATE_DEFAULT);
}

/**
 * @brief 地鼠消失：恢复地洞常态
 */
static void hole_off(int i) {
    lv_label_set_text(hole_labs[i], "");
    lv_obj_set_style_bg_color(holes[i], lv_color_hex(HOLE_COLOR), LV_STATE_DEFAULT);
}

/**
 * @brief 地鼠定时器回调（750ms）：随机换一个洞出地鼠
 * @details running=0 时（不在本界面或回合已结束）直接返回空转
 */
static void mole_timer_cb(lv_timer_t* t) {
    if (!running) return;
    if (active >= 0) hole_off(active);
    active = lv_rand(0, 8);
    hole_on(active);
}

/**
 * @brief 回合时钟回调（1 秒）：倒计时，归零结束回合
 */
static void clock_timer_cb(lv_timer_t* t) {
    if (!running) return;
    time_left--;
    if (time_left <= 0) {
        running = 0;
        if (active >= 0) {
            hole_off(active);
            active = -1;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "时间到！最终得分 %d，失误 %d", score, miss);
        lv_label_set_text(status_lab, buf);
        return;
    }
    update_status();
}

/**
 * @brief 地洞点击回调：点中地鼠得分，点空计失误
 */
static void hole_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!running) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (idx == active) {
        score++;
        hole_off(idx);
        active = -1;
    }
    else {
        miss++;
    }
    update_status();
}

/**
 * @brief 开局：重置计分并开始计时（进入界面 / 重开 / 弹窗确定共用）
 */
static void mole_start(void) {
    if (active >= 0) {
        hole_off(active);
        active = -1;
    }
    score = 0;
    miss = 0;
    time_left = MOLE_ROUND;
    running = 1;
    update_status();
}

/**
 * @brief 进入打地鼠界面时开局（gamebox_goto 切屏后调用）
 */
void mole_enter(void) {
    mole_start();
}

/**
 * @brief 重开按钮回调（仅响应 CLICKED）
 */
static void mole_restart_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    mole_start();
}

/**
 * @brief 专用返回回调：先停局再切回游戏盒子，避免回合后台空跑
 */
static void mole_leave_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    running = 0;
    if (active >= 0) {
        hole_off(active);
        active = -1;
    }
    lv_screen_load(gamebox);
}

/**
 * @brief 创建打地鼠游戏界面
 * @details 创建屏幕、标题、状态栏、3x3 地洞网格、控制按钮与常驻定时器
 */
void creat_my_mole(void) {
    src_mole = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(src_mole, lv_color_hex(0x4A5240), LV_STATE_DEFAULT);

    // 标题
    create_label(src_mole, 0xF7F9FA, LV_ALIGN_TOP_MID, 0, 12, "打地鼠：点中地鼠得分，30 秒挑战", 28);

    // 地洞样式：浅棕底、深棕字
    int hole_style = style_btn_init(6, HOLE_COLOR, 0x3B2F2F, 32);

    // 3x3 地洞网格
    for (int i = 0; i < 9; i++) {
        holes[i] = create_button(hole_style, src_mole, LV_ALIGN_TOP_LEFT,
            MOLE_X0 + (i % 3) * MOLE_STEP, MOLE_Y0 + (i / 3) * MOLE_STEP,
            120, 120, "", hole_cb, (void*)(intptr_t)i);
        hole_labs[i] = lv_obj_get_child(holes[i], 0);
    }

    // 状态栏（放底部，避开网格）
    status_lab = create_label(src_mole, 0xFFD97A, LV_ALIGN_BOTTOM_MID, 0, -15, "得分 0    失误 0    剩余 30 秒", 24);

    // 返回游戏盒子按钮（专用回调：先停局再切屏）
    int fhj = style_btn_init(0, 0x2B2B2B, 0xF7F9FA, 28);
    lv_obj_t* btn_back = create_button(fhj, src_mole, LV_ALIGN_TOP_RIGHT, 0, 0, 100, 50,
        "返回", mole_leave_cb, NULL);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_70, LV_STATE_DEFAULT);

    // 重开按钮
    int chongkai = style_btn_init(1, 0x4A6FA5, 0xF7F9FA, 24);
    lv_obj_t* btn_restart = create_button(chongkai, src_mole, LV_ALIGN_TOP_LEFT, 0, 0, 120, 60,
        "重开", mole_restart_cb, NULL);
    lv_obj_set_style_bg_opa(btn_restart, LV_OPA_80, LV_STATE_DEFAULT);

    // 常驻定时器（创建一次，靠 running 标志门控，不销毁）
    if (mole_timer == NULL) {
        mole_timer = lv_timer_create(mole_timer_cb, 750, NULL);
    }
    if (clock_timer == NULL) {
        clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    }

    update_status();
}
