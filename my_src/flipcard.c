/**
 * @file flipcard.c
 * @brief 记忆翻牌配对游戏逻辑与界面实现
 * @details 4x4 共 16 张牌。每局从 15 种水果图片中随机抽取 8 种，
 *          每种配成一对。点击翻开一张；再翻第二张：
 *          两张相同 → 配对成功，变色固定；不同 → 600ms 后自动翻回。
 *          牌面为按钮上叠加的 lv_image，背面显示 "?" 并隐藏图片，
 *          翻开时隐藏文字并显示对应 BMP 图片。
 *
 * 并发要点：第二张翻错后设置 lock 标志并启动一次性 LVGL 定时器，
 * 定时器回调里翻回两张牌并解除 lock——避免玩家在等待翻回期间
 * 连点导致牌面状态错乱（LVGL 单线程事件模型下，标志位即足够，
 * 不需要加锁）。
 */

#include "lvgl/lvgl.h"    // LVGL 核心头文件
#include <stdio.h>        // 标准输入输出
#include <stdint.h>       // intptr_t 等整数类型
#include <string.h>       // strcmp
#include "flipcard.h"     // 本模块头文件
#include "0ui.h"          // UI 通用组件

// 游戏界面屏幕对象
lv_obj_t* src_flipcard = NULL;

// 15 种水果图片（88x88 BMP，路径按程序运行目录下的 tu/ 解析）
static const char* FRUIT_IMGS[15] = {
    "A:tu/flipcard/apple.bmp",      "A:tu/flipcard/banana.bmp",
    "A:tu/flipcard/cherry.bmp",     "A:tu/flipcard/grape.bmp",
    "A:tu/flipcard/blueberry.bmp",  "A:tu/flipcard/pear.bmp",
    "A:tu/flipcard/lemon.bmp",      "A:tu/flipcard/mango.bmp",
    "A:tu/flipcard/orange.bmp",     "A:tu/flipcard/mangosteen.bmp",
    "A:tu/flipcard/pineapple.bmp",  "A:tu/flipcard/peach.bmp",
    "A:tu/flipcard/watermelon.bmp", "A:tu/flipcard/strawberry.bmp",
    "A:tu/flipcard/kiwi.bmp",
};

#define FRUIT_COUNT 15
#define PAIR_COUNT  8

// 每张牌的水果图片路径（洗牌后填入）
static const char* values[16];

// 牌面按钮、文字标签（背面 "?"）与水果图片
static lv_obj_t* cards[16];
static lv_obj_t* card_labs[16];
static lv_obj_t* card_imgs[16];

// 状态数组：matched=已配对固定；revealed=当前正面朝上
static int matched[16];
static int revealed[16];

// 已翻开且等待配对的第一张牌（-1 表示还没有翻第一张）
static int first = -1;

// lock：两张不匹配的牌展示等待期间，屏蔽新的点击
static int lock = 0;

// 等待翻回的两张牌编号
static int flip_a = -1, flip_b = -1;

// 翻回延时定时器（一次性，回调内自删除）
static lv_timer_t* flip_timer = NULL;

// 已配对对数 / 总翻牌次数
static int pairs = 0;
static int flips = 0;

// 状态标签
static lv_obj_t* status_lab = NULL;

// 网格布局参数（与数字华容道同规格：100x100，间距 10）
#define FC_X0     297
#define FC_Y0     150
#define FC_STEP   110

/**
 * @brief 刷新状态标签文字
 */
static void update_status(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "已配对 %d/8    翻牌 %d 次", pairs, flips);
    lv_label_set_text(status_lab, buf);
}

/**
 * @brief 把一张牌显示为背面（"?"，隐藏水果图片）
 */
static void card_hide(int i) {
    lv_label_set_text(card_labs[i], "?");
    lv_obj_add_flag(card_imgs[i], LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 把一张牌显示为正面（水果图片，隐藏 "?"）
 */
static void card_show(int i) {
    lv_label_set_text(card_labs[i], "");
    lv_image_set_src(card_imgs[i], values[i]);
    lv_obj_remove_flag(card_imgs[i], LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief 洗牌：从 15 种水果里随机抽 8 种，各配一对，再打乱到 16 个位置
 * @details 两次 Fisher-Yates：先洗水果池取前 8 种，再把 16 张牌打乱；
 *          初始 first/lock/计时器全部复位
 */
static void flipcard_new_board(void) {
    const char* fruit_pool[FRUIT_COUNT];
    for (int i = 0; i < FRUIT_COUNT; i++) fruit_pool[i] = FRUIT_IMGS[i];
    for (int i = FRUIT_COUNT - 1; i > 0; i--) {
        int j = lv_rand(0, i);
        const char* t = fruit_pool[i]; fruit_pool[i] = fruit_pool[j]; fruit_pool[j] = t;
    }

    const char* pool[16];
    for (int i = 0; i < PAIR_COUNT; i++) {
        pool[i * 2] = fruit_pool[i];
        pool[i * 2 + 1] = fruit_pool[i];
    }
    for (int i = 15; i > 0; i--) {
        int j = lv_rand(0, i);
        const char* t = pool[i]; pool[i] = pool[j]; pool[j] = t;
    }

    for (int i = 0; i < 16; i++) {
        values[i] = pool[i];
        matched[i] = 0;
        revealed[i] = 0;
        card_hide(i);
        // 复位配对成功后的背景变色（本地样式覆盖会被清除）
        lv_obj_set_style_bg_color(cards[i], lv_color_hex(0x6C5B7B), LV_STATE_DEFAULT);
    }
    first = -1;
    lock = 0;
    pairs = 0;
    flips = 0;
    if (flip_timer) {
        lv_timer_delete(flip_timer);
        flip_timer = NULL;
    }
}

/**
 * @brief 翻回定时器回调（600ms 后把不匹配的两张牌翻回背面）
 * @details 在回调内部删除自身定时器；若 lock 已被复位则无事可做
 */
static void flip_back_cb(lv_timer_t* t) {
    if (flip_timer == t) flip_timer = NULL;
    lv_timer_delete(t);

    if (!lock) return;

    revealed[flip_a] = 0;
    revealed[flip_b] = 0;
    card_hide(flip_a);
    card_hide(flip_b);
    lock = 0;
}

/**
 * @brief 重开一局（重开按钮 / 胜利弹窗确定共用，幂等）
 */
static void flipcard_restart(void) {
    flipcard_new_board();
    update_status();
}

/**
 * @brief 重开按钮回调（仅响应 CLICKED）
 */
static void flipcard_restart_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    flipcard_restart();
}

/**
 * @brief 胜利弹窗"确定"回调（再来一局）
 */
static void flipcard_dialog_ok(lv_event_t* e) {
    flipcard_restart();
}

/**
 * @brief 牌面点击回调
 * @details user_data 为牌编号；状态机：翻第一张 → 翻第二张 →
 *          判定配对 / 启动翻回定时器。配对判定用图片路径比较。
 */
static void card_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    // 等待翻回期间、已配对、已翻开的牌都不响应
    if (lock || matched[idx] || revealed[idx]) return;

    // 翻开这张牌
    revealed[idx] = 1;
    card_show(idx);
    flips++;

    if (first < 0) {
        // 第一张：记录并等待第二张
        first = idx;
        update_status();
        return;
    }

    // 第二张：判定是否配对（同种水果的图片路径相同）
    int second = idx;
    if (strcmp(values[first], values[second]) == 0) {
        // 配对成功：变色固定，不再响应点击
        matched[first] = matched[second] = 1;
        lv_obj_set_style_bg_color(cards[first], lv_color_hex(0x7FA88C), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(cards[second], lv_color_hex(0x7FA88C), LV_STATE_DEFAULT);
        pairs++;
        first = -1;
        update_status();

        if (pairs == 8) {
            char msg[64];
            snprintf(msg, sizeof(msg), "全部配对完成，共翻 %d 次！再来一局？", flips);
            lv_label_set_text(status_lab, "通关！");
            create_confirm_dialog(msg, 24, flipcard_dialog_ok, NULL);
        }
    }
    else {
        // 不匹配：锁定并启动 600ms 翻回定时器
        lock = 1;
        flip_a = first;
        flip_b = second;
        first = -1;
        flip_timer = lv_timer_create(flip_back_cb, 600, NULL);
        update_status();
    }
}

/**
 * @brief 创建记忆翻牌游戏界面
 * @details 创建屏幕、标题、状态栏、4x4 牌面网格与控制按钮。
 *          每个牌面按钮内叠加一个居中 lv_image 用于显示水果图片，
 *          牌背面时隐藏图片、由标签显示 "?"。
 */
void creat_my_flipcard(void) {
    src_flipcard = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(src_flipcard, lv_color_hex(0x38425D), LV_STATE_DEFAULT);

    // 标题与状态栏
    create_label(src_flipcard, 0xF7F9FA, LV_ALIGN_TOP_MID, 0, 12, "记忆翻牌：找出全部 8 对水果", 28);
    status_lab = create_label(src_flipcard, 0x88E0A0, LV_ALIGN_TOP_MID, 0, 62, "已配对 0/8    翻牌 0 次", 24);

    // 牌面样式：藕荷色底、白字（用于背面 "?"）
    int card_style = style_btn_init(5, 0x6C5B7B, 0xF7F9FA, 28);

    // 4x4 牌面网格
    for (int i = 0; i < 16; i++) {
        cards[i] = create_button(card_style, src_flipcard, LV_ALIGN_TOP_LEFT,
            FC_X0 + (i % 4) * FC_STEP, FC_Y0 + (i / 4) * FC_STEP,
            100, 100, "?", card_cb, (void*)(intptr_t)i);
        card_labs[i] = lv_obj_get_child(cards[i], 0);

        // 水果图片：88x88 居中，牌 100x100 四周留 6px 露出按钮背景
        card_imgs[i] = lv_image_create(cards[i]);
        lv_obj_set_align(card_imgs[i], LV_ALIGN_CENTER);
        lv_obj_add_flag(card_imgs[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 返回游戏盒子按钮
    int fhj = style_btn_init(0, 0x2B2B2B, 0xF7F9FA, 28);
    lv_obj_t* btn_back = create_button(fhj, src_flipcard, LV_ALIGN_TOP_RIGHT, 0, 0, 100, 50,
        "返回", go_to_sreen_cb, (void*)1);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_70, LV_STATE_DEFAULT);

    // 重开按钮（左上角，避开底部牌面区）
    int chongkai = style_btn_init(1, 0x4A6FA5, 0xF7F9FA, 24);
    lv_obj_t* btn_restart = create_button(chongkai, src_flipcard, LV_ALIGN_TOP_LEFT, 0, 0, 120, 60,
        "重开", flipcard_restart_cb, NULL);
    lv_obj_set_style_bg_opa(btn_restart, LV_OPA_80, LV_STATE_DEFAULT);

    // 生成开局
    flipcard_new_board();
    update_status();
}
