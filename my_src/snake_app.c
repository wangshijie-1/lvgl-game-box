/**
 * @file snake_app.c
 * @brief 贪吃蛇(LVGL 版) 应用层：菜单/单人/双人三模式 + 引擎状态机 + 触控控制
 * @details 严格保持与 D:\Share\2-project_GreedySnake 的 modes/ 与 GameEngine 一致的设计结构：
 *          - 菜单：上下导航 + 确认；含配置面板（格子大小/起步速度/加速速度）与修改确认弹窗
 *          - 单人：tick 驱动移动、碰撞/食物/分数/体力/胜利；支持存档(内存)与继续
 *          - 双人：双线程各驱一蛇（P1=WASD 火山红，P2=方向键 阳光黄），撞墙/自撞/撞对方/头碰头判死
 *          - 引擎：MENU/SINGLE/TWO/PAUSED/OVER 状态机 + 节拍驱动 + 暂停遮罩
 *          仅把"键盘输入"替换为触控按钮（仍映射为同一 Function 枚举），渲染走 LVGL 对象。
 */

#include "snake.h"
#include <stdio.h>      // snprintf：暂停/结算遮罩文字格式化
#include <stdlib.h>
#include <string.h>

/* ===================== 全局 ===================== */
lv_obj_t* src_snake = NULL;
extern lv_obj_t* gamebox;     // 游戏盒子屏幕（layout.c 中创建）
static int g_highScore = 0;   // 跨局最高分（与原始 g_highScore 一致）

// 引擎实例 g_engine 的 extern 声明已统一放在 snake.h（键盘输入模块也要用）
// 双人线程停止（定义于下方，返回盒子按钮需提前调用）
// 注意：此处位于 typedef 之前，只能用 struct 标签前置声明，不能写 TwoPlayerMode
static void two_stop(struct TwoPlayerMode* m);

typedef struct MenuMode {
    SnakeScreen* ui;
    MenuItem selected;
    int hasSave;
    int configOpen;
    int draftCell, draftStart, draftBoost;
    int confirmDialog;
} MenuMode;

typedef struct SingleMode {
    SnakeScreen* ui;
    Board board;
    Snake snake;
    Food food;
    Position lastFoodPos;
    uint32_t foodSpawnMs;
    int gameOver, won;
    int freeCells;
    int hasSave;
    // 存档（内存，未写文件，与原始一致）
    int saveValid;
    Position* saveBody; int saveBodyLen;
    Direction saveDir; int saveGrowing, saveDead;
    Position saveNewHead, saveOldTail; int saveSpeed, saveStamina;
    Position saveFoodPos; int saveFoodExist;
    int saveWin;
    CellType* saveGrid; int saveGW, saveGH;
} SingleMode;

typedef struct TwoPlayerMode {
    SnakeScreen* ui;
    Board board;
    Snake s1, s2;
    Food food;
    Position lastFoodPos;
    int running, paused, started;
    int dead1, dead2, gameOver, winner;
    // 节拍状态：两条蛇各自累计时间片（速度不同 → interval 不同），
    // 由引擎的 LVGL 定时器统一驱动。
    // 不再使用 pthread：原来两条蛇各跑一条 while + usleep(10ms) 线程，
    // 那是"自己搞循环"，既和 LVGL 主循环抢 CPU、抢锁，又把步长交给调度器抖动决定。
    uint32_t lastMs;
    int acc1, acc2;
} TwoPlayerMode;

/* ===================== 控制按钮（全部挂在右侧栏 panel 上） ===================== */
typedef struct {
    lv_obj_t* m_up, * m_down, * m_ok;
    lv_obj_t* s_up, * s_down, * s_left, * s_right, * s_boost, * s_pause, * s_back;
    lv_obj_t* t_p1u, * t_p1d, * t_p1l, * t_p1r, * t_p2u, * t_p2d, * t_p2l, * t_p2r;
    lv_obj_t* t_boost1, * t_boost2, * t_pause, * t_back;
    lv_obj_t* c_left, * c_right, * c_ok, * c_back;
    lv_obj_t* p_resume, * p_menu;
} Control;
static Control g_ctrl;

static void ctrl_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    Function fn = (Function)(intptr_t)lv_event_get_user_data(e);
    engine_handle_key(&g_engine, fn);
}

// 返回游戏盒子（独立于引擎内 M 菜单，给用户一个明确的出口）
static void snake_back_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // 先经引擎回到主菜单（单人会存档、双人会停止线程），再切回游戏盒子
    engine_handle_key(&g_engine, FN_MENU);
    lv_screen_load(gamebox);
}

static lv_obj_t* make_btn(int style, lv_obj_t* parent, int x, int y, int w, int h,
                          const char* text, Function fn)
{
    lv_obj_t* b = create_button(style, parent, LV_ALIGN_TOP_LEFT, x, y, w, h, text, ctrl_cb, (void*)(intptr_t)fn);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_DEFAULT);
    return b;
}

// 创建右侧栏上的全部触控按键（底部控制条已取消）。
// 空间约束：栏宽只有 124px，所以：
//   - 方向键用 36x34 的小键，横排三键（4 / 44 / 84）刚好占满 124px；
//   - 确认/返回/加速用整行 116 宽；
//   - 暂停/菜单按要求做得更小（56x32），保证双人模式 12 个键也放得下。
// 按键全部挂在右侧栏 panel 上；panel 建在遮罩之后，因此任何状态下都在最上层、不会被遮住点不动。
// 文字统一 24 号（沿用 0ui 字体缓存里已有的字号，不额外占用缓存位）。
// 时间复杂度 O(1)，空间复杂度 O(1)
static void ctrl_create(Engine* e)
{
    lv_obj_t* panel = e->ui->panel;

    int st_white = style_btn_init(0, 0xF7F9FA, 0x2B2B2B, 24);
    int st_red   = style_btn_init(1, 0xFF0000, 0xFFFFFF, 24);
    int st_blue  = style_btn_init(2, 0x1E88A8, 0xFFFFFF, 24);

    // ---- 菜单：↑ / ↓ / 确认（↑↓ 已在 #2 里实测能正常显示，非 ◀▶ 那类缺字形符号）----
    g_ctrl.m_up   = make_btn(st_white, panel, 44, 170, 36, 34, "↑", FN_UP);
    g_ctrl.m_down = make_btn(st_white, panel, 44, 210, 36, 34, "↓", FN_DOWN);
    g_ctrl.m_ok   = make_btn(st_blue,  panel, 4,  256, 116, 36, "确认", FN_CONFIRM);

    // ---- 单人：方向盘 + 加速 + 暂停 + 菜单 ----
    g_ctrl.s_up    = make_btn(st_white, panel, 44, 170, 36, 34, "上", FN_UP);
    g_ctrl.s_left  = make_btn(st_white, panel, 4,  208, 36, 34, "左", FN_LEFT);
    g_ctrl.s_down  = make_btn(st_white, panel, 44, 208, 36, 34, "下", FN_DOWN);
    g_ctrl.s_right = make_btn(st_white, panel, 84, 208, 36, 34, "右", FN_RIGHT);
    g_ctrl.s_boost = make_btn(st_red,   panel, 4,  250, 116, 34, "加速", FN_BOOST1);
    g_ctrl.s_pause = make_btn(st_blue,  panel, 4,  292, 56, 32, "暂停", FN_PAUSE);
    g_ctrl.s_back  = make_btn(st_white, panel, 64, 292, 56, 32, "菜单", FN_MENU);

    // ---- 双人：P1(红) / P2(蓝) 各一组方向盘 + 独立加速 + 暂停/菜单 ----
    g_ctrl.t_p1u = make_btn(st_red, panel, 44, 170, 36, 30, "上", FN_UP);
    g_ctrl.t_p1l = make_btn(st_red, panel, 4,  202, 36, 30, "左", FN_LEFT);
    g_ctrl.t_p1d = make_btn(st_red, panel, 44, 202, 36, 30, "下", FN_DOWN);
    g_ctrl.t_p1r = make_btn(st_red, panel, 84, 202, 36, 30, "右", FN_RIGHT);
    g_ctrl.t_p2u = make_btn(st_blue, panel, 44, 240, 36, 30, "上", FN_UP2);
    g_ctrl.t_p2l = make_btn(st_blue, panel, 4,  272, 36, 30, "左", FN_LEFT2);
    g_ctrl.t_p2d = make_btn(st_blue, panel, 44, 272, 36, 30, "下", FN_DOWN2);
    g_ctrl.t_p2r = make_btn(st_blue, panel, 84, 272, 36, 30, "右", FN_RIGHT2);
    g_ctrl.t_boost1 = make_btn(st_red,  panel, 4, 310, 116, 30, "P1加速", FN_BOOST1);
    g_ctrl.t_boost2 = make_btn(st_blue, panel, 4, 344, 116, 30, "P2加速", FN_BOOST2);
    g_ctrl.t_pause  = make_btn(st_white, panel, 4, 382, 56, 30, "暂停", FN_PAUSE);
    g_ctrl.t_back   = make_btn(st_white, panel, 64, 382, 56, 30, "菜单", FN_MENU);

    // ---- 配置：左/右 调值 + 确认/返回 ----
    g_ctrl.c_left  = make_btn(st_white, panel, 4,  170, 56, 34, "左", FN_LEFT);
    g_ctrl.c_right = make_btn(st_white, panel, 64, 170, 56, 34, "右", FN_RIGHT);
    g_ctrl.c_ok    = make_btn(st_blue,  panel, 4,  212, 116, 36, "确认", FN_CONFIRM);
    g_ctrl.c_back  = make_btn(st_white, panel, 4,  254, 116, 36, "返回", FN_BACK);

    // ---- 暂停 / 结算 ----
    g_ctrl.p_resume = make_btn(st_blue,  panel, 4, 170, 116, 36, "继续", FN_RESUME);
    g_ctrl.p_menu   = make_btn(st_white, panel, 4, 212, 116, 36, "菜单", FN_MENU);
}

static void hide_all_ctrl(void)
{
    lv_obj_t** all[] = { &g_ctrl.m_up, &g_ctrl.m_down, &g_ctrl.m_ok,
        &g_ctrl.s_up, &g_ctrl.s_down, &g_ctrl.s_left, &g_ctrl.s_right,
        &g_ctrl.s_boost, &g_ctrl.s_pause, &g_ctrl.s_back,
        &g_ctrl.t_p1u, &g_ctrl.t_p1d, &g_ctrl.t_p1l, &g_ctrl.t_p1r,
        &g_ctrl.t_p2u, &g_ctrl.t_p2d, &g_ctrl.t_p2l, &g_ctrl.t_p2r,
        &g_ctrl.t_boost1, &g_ctrl.t_boost2, &g_ctrl.t_pause, &g_ctrl.t_back,
        &g_ctrl.c_left, &g_ctrl.c_right, &g_ctrl.c_ok, &g_ctrl.c_back,
        &g_ctrl.p_resume, &g_ctrl.p_menu };
    for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++)
        if (*all[i]) lv_obj_add_flag(*all[i], LV_OBJ_FLAG_HIDDEN);
}

/* ===================== UI 状态切换（控制对象可见性） ===================== */
// 单个对象显隐（LVGL 9 用 HIDDEN 标志表达隐藏）
// 时间复杂度 O(1)，空间复杂度 O(1)
static void vis(lv_obj_t* o, int show)
{
    if (o == NULL) return;
    if (show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// 按状态刷新右侧栏信息块 + 键盘可见性。
// 与旧实现的区别：右侧栏 panel 现在是唯一的"计分 + 按键"区，所有状态下都常驻显示，
// 棋盘/菜单只占左侧 900px，不再有底部控制条。
// 时间复杂度 O(1)，空间复杂度 O(1)
static void apply_ui_state(Engine* e)
{
    SnakeScreen* ui = e->ui;
    hide_all_ctrl();

    // 右侧栏常驻；先关掉菜单/配置块/遮罩/棋盘与两组信息块，再按状态逐一打开
    vis(ui->panel, 1);
    vis(ui->menu, 0);
    vis(ui->config, 0);
    vis(ui->overlay, 0);
    vis(ui->cont, 0);
    vis(ui->lblScore, 0); vis(ui->lblHigh, 0); vis(ui->lblStaTitle, 0); vis(ui->lblSta, 0);
    vis(ui->lblP1, 0); vis(ui->lblP2, 0); vis(ui->lblLen1, 0); vis(ui->lblLen2, 0);
    vis(ui->lblSta1, 0); vis(ui->lblSta2, 0);

    // 暂停/结算态没有独立模式信息，沿用 e->prev（进入时记录的对局模式）
    int two = (e->prev == ST_TWO);

    switch (e->state) {
    case ST_MENU:
        vis(ui->menu, 1);
        if (e->menu->configOpen) {
            vis(ui->config, 1);
            vis(g_ctrl.c_left, 1); vis(g_ctrl.c_right, 1);
            vis(g_ctrl.c_ok, 1);   vis(g_ctrl.c_back, 1);
        } else {
            // 菜单下右侧栏显示分数块（历史最高分），配合 ↑ / ↓ / 确认
            vis(ui->lblScore, 1); vis(ui->lblHigh, 1);
            vis(g_ctrl.m_up, 1); vis(g_ctrl.m_down, 1); vis(g_ctrl.m_ok, 1);
        }
        break;
    case ST_SINGLE:
        vis(ui->cont, 1);
        vis(ui->lblScore, 1); vis(ui->lblHigh, 1); vis(ui->lblStaTitle, 1); vis(ui->lblSta, 1);
        vis(g_ctrl.s_up, 1); vis(g_ctrl.s_down, 1); vis(g_ctrl.s_left, 1); vis(g_ctrl.s_right, 1);
        vis(g_ctrl.s_boost, 1); vis(g_ctrl.s_pause, 1); vis(g_ctrl.s_back, 1);
        break;
    case ST_TWO:
        vis(ui->cont, 1);
        vis(ui->lblP1, 1); vis(ui->lblLen1, 1); vis(ui->lblSta1, 1);
        vis(ui->lblP2, 1); vis(ui->lblLen2, 1); vis(ui->lblSta2, 1);
        vis(g_ctrl.t_p1u, 1); vis(g_ctrl.t_p1d, 1); vis(g_ctrl.t_p1l, 1); vis(g_ctrl.t_p1r, 1);
        vis(g_ctrl.t_p2u, 1); vis(g_ctrl.t_p2d, 1); vis(g_ctrl.t_p2l, 1); vis(g_ctrl.t_p2r, 1);
        vis(g_ctrl.t_boost1, 1); vis(g_ctrl.t_boost2, 1);
        vis(g_ctrl.t_pause, 1); vis(g_ctrl.t_back, 1);
        break;
    case ST_PAUSED:
        vis(ui->cont, 1);
        vis(ui->overlay, 1);
        if (two) {
            vis(ui->lblP1, 1); vis(ui->lblLen1, 1); vis(ui->lblSta1, 1);
            vis(ui->lblP2, 1); vis(ui->lblLen2, 1); vis(ui->lblSta2, 1);
        } else {
            vis(ui->lblScore, 1); vis(ui->lblHigh, 1); vis(ui->lblStaTitle, 1); vis(ui->lblSta, 1);
        }
        vis(g_ctrl.p_resume, 1); vis(g_ctrl.p_menu, 1);
        break;
    case ST_OVER:
        vis(ui->cont, 1);
        vis(ui->overlay, 1);
        if (two) {
            vis(ui->lblP1, 1); vis(ui->lblLen1, 1); vis(ui->lblSta1, 1);
            vis(ui->lblP2, 1); vis(ui->lblLen2, 1); vis(ui->lblSta2, 1);
        } else {
            vis(ui->lblScore, 1); vis(ui->lblHigh, 1); vis(ui->lblStaTitle, 1); vis(ui->lblSta, 1);
        }
        vis(g_ctrl.p_menu, 1);
        break;
    }
}

/* ===================== MenuMode ===================== */
static MenuItem menu_handle_nav(MenuMode* m, Function key)
{
    switch (key) {
    case FN_UP: case FN_UP2:
        m->selected = (MenuItem)((m->selected - 1 + MI_COUNT) % MI_COUNT);
        if (m->selected == MI_CONTINUE && !m->hasSave)
            m->selected = (MenuItem)((m->selected - 1 + MI_COUNT) % MI_COUNT);
        break;
    case FN_DOWN: case FN_DOWN2:
        m->selected = (MenuItem)((m->selected + 1) % MI_COUNT);
        if (m->selected == MI_CONTINUE && !m->hasSave)
            m->selected = (MenuItem)((m->selected + 1) % MI_COUNT);
        break;
    case FN_CONFIRM:
        if (m->selected == MI_CONTINUE && !m->hasSave) return MI_COUNT;
        if (m->selected == MI_CONFIG) { // 进入配置面板（不返回动作）
            m->configOpen = 1;
            m->draftCell = g_cellSizeIdx;
            m->draftStart = g_startSpeedIdx;
            m->draftBoost = g_boostSpeedIdx;
            m->selected = MI_SINGLE;
            return MI_COUNT;
        }
        return m->selected;
    default:
        break;
    }
    return MI_COUNT;
}
static MenuItem menu_handle_config(MenuMode* m, Function key)
{
    int selRow = (int)m->selected; // 0..2
    switch (key) {
    case FN_UP: case FN_UP2:   m->selected = (MenuItem)((selRow - 1 + 3) % 3); break;
    case FN_DOWN: case FN_DOWN2: m->selected = (MenuItem)((selRow + 1) % 3); break;
    case FN_LEFT: case FN_LEFT2:
        if (selRow == 0) m->draftCell  = (m->draftCell - 1 + 3) % 3;
        if (selRow == 1) m->draftStart = (m->draftStart - 1 + 3) % 3;
        if (selRow == 2) m->draftBoost = (m->draftBoost - 1 + 3) % 3;
        break;
    case FN_RIGHT: case FN_RIGHT2:
        if (selRow == 0) m->draftCell  = (m->draftCell + 1) % 3;
        if (selRow == 1) m->draftStart = (m->draftStart + 1) % 3;
        if (selRow == 2) m->draftBoost = (m->draftBoost + 1) % 3;
        break;
    case FN_CONFIRM: {
        int changed = (m->draftCell != g_cellSizeIdx) ||
                     (m->draftStart != g_startSpeedIdx) ||
                     (m->draftBoost != g_boostSpeedIdx);
        if (changed) m->confirmDialog = 1;          // 弹出确认（用遮罩文本）
        else { m->configOpen = 0; m->selected = MI_CONFIG; }
        return MI_COUNT;
    }
    case FN_PAUSE: case FN_MENU: case FN_BACK:
        m->configOpen = 0; m->selected = MI_CONFIG; return MI_COUNT;
    default: break;
    }
    return MI_COUNT;
}
static MenuItem menu_handle_confirm(MenuMode* m, Function key)
{
    switch (key) {
    case FN_CONFIRM: // 应用修改：写回 GameSpec + 清档
        g_cellSizeIdx = m->draftCell;
        g_startSpeedIdx = m->draftStart;
        g_boostSpeedIdx = m->draftBoost;
        spec_recalc(g_screenW, g_screenH);
        snake_screen_build_cells(m->ui);   // 格子大小变化 → 重建单元网格以匹配新 gridW/H
        if (g_engine.single) g_engine.single->hasSave = 0; // 失去存档 → "继续"变灰
        m->hasSave = 0;
        m->confirmDialog = 0;
        m->configOpen = 0;
        m->selected = MI_CONFIG;
        return MI_COUNT;
    case FN_PAUSE: case FN_MENU: case FN_BACK:
        m->confirmDialog = 0;
        return MI_COUNT; // 仅关弹窗，仍停留配置面板
    default:
        return MI_COUNT;
    }
}
static MenuItem menu_handle_input(MenuMode* m, Function key)
{
    if (m->confirmDialog) return menu_handle_confirm(m, key);
    if (m->configOpen)    return menu_handle_config(m, key);
    return menu_handle_nav(m, key);
}
static void menu_render(MenuMode* m)
{
    snake_screen_menu_render(m->ui, (int)m->selected, m->hasSave);
    if (m->configOpen)
        snake_screen_config_render(m->ui, (int)m->selected, m->draftCell, m->draftStart, m->draftBoost);
    if (m->confirmDialog)
        snake_screen_overlay_show(m->ui, 1, "修改配置将失去存档\n回车确认 取消键返回");
    else
        snake_screen_overlay_show(m->ui, 0, NULL);
}

/* ===================== SingleMode ===================== */
static void single_init_game(SingleMode* m)
{
    int sw = lv_disp_get_hor_res(NULL), sh = lv_disp_get_ver_res(NULL);
    (void)sw; (void)sh;
    board_free(&m->board);
    board_init(&m->board, g_spec.gridW, g_spec.gridH);
    board_build_walls(&m->board);
    m->freeCells = (m->board.w - 2) * (m->board.h - 2);
    m->won = 0;

    int hx0 = 2, hx1 = m->board.w - 3; if (hx0 > hx1) hx1 = hx0;
    int hy0 = 2, hy1 = m->board.h - 3; if (hy0 > hy1) hy1 = hy0;
    Position head = { rand_int(hx0, hx1), rand_int(hy0, hy1) };

    Position offsets[4] = { {0,-1}, {0,1}, {-1,0}, {1,0} };
    int idx = rand_int(0, 3);
    Position tail = { head.x + offsets[idx].x, head.y + offsets[idx].y };
    if (!(tail.x >= 1 && tail.x <= m->board.w - 2 && tail.y >= 1 && tail.y <= m->board.h - 2)) {
        for (int k = 0; k < 4; k++) {
            Position t = { head.x + offsets[k].x, head.y + offsets[k].y };
            if (t.x >= 1 && t.x <= m->board.w - 2 && t.y >= 1 && t.y <= m->board.h - 2) {
                tail = t; idx = k; break;
            }
        }
    }
    snake_reset(&m->snake, head, tail);
    Direction initDir;
    switch (idx) {
        case 0: initDir = DIR_DOWN;  break;
        case 1: initDir = DIR_UP;    break;
        case 2: initDir = DIR_RIGHT; break;
        default: initDir = DIR_LEFT; break;
    }
    snake_set_dir(&m->snake, initDir);
    snake_set_speed(&m->snake, g_spec.startSpeed);

    m->food.exist = 0;
    food_spawn(&m->food, &m->board);
    for (int i = 0; i < m->snake.len; i++)
        board_set(&m->board, &m->snake.body[i], CELL_SNAKE_BODY);
    board_set(&m->board, &m->snake.newHead, CELL_SNAKE_HEAD);
    board_set(&m->board, &m->food.pos, CELL_FOOD);

    m->lastFoodPos = m->food.pos;
    m->gameOver = 0;
    m->won = 0;
    m->hasSave = 0;
    m->saveValid = 0;
    m->foodSpawnMs = snake_now_ms();
}

static void single_handle_input(SingleMode* m, Function key)
{
    Direction d = DIR_NONE;
    switch (key) {
    case FN_UP: case FN_UP2:    d = DIR_UP;    break;
    case FN_DOWN: case FN_DOWN2: d = DIR_DOWN;  break;
    case FN_LEFT: case FN_LEFT2: d = DIR_LEFT;  break;
    case FN_RIGHT: case FN_RIGHT2: d = DIR_RIGHT; break;
    default: break;
    }
    if (d != DIR_NONE) snake_change_dir(&m->snake, d);
}

// 吃食物后把"当前长度+1"记为得分（与原始一致：getLength()+1）
static void single_update(SingleMode* m)
{
    if (m->gameOver) return;

    if (m->food.exist) {
        uint32_t now = snake_now_ms();
        if (now - m->foodSpawnMs >= 20000) {
            board_set(&m->board, &m->food.pos, CELL_EMPTY);
            m->food.exist = 0;
            food_spawn(&m->food, &m->board);
            m->foodSpawnMs = now;
        }
    }

    snake_move(&m->snake);
    Position head = snake_new_head(&m->snake);
    if (head.x < 1 || head.x > m->board.w - 2 ||
        head.y < 1 || head.y > m->board.h - 2) {
        snake_setdead(&m->snake); m->gameOver = 1; return;
    }
    for (int i = 1; i < m->snake.len; i++)
        if (head.x == m->snake.body[i].x && head.y == m->snake.body[i].y) {
            snake_setdead(&m->snake); m->gameOver = 1; return;
        }

    food_spawn(&m->food, &m->board);

    if (head.x == m->food.pos.x && head.y == m->food.pos.y) {
        snake_grow(&m->snake);
        m->food.exist = 0;
        // 吃掉后立即重置 20 秒计时：新果子由下一次 food_spawn() 生成，计时起点必须是
        // "此刻"而不是上一颗果子的生成时间。原始 C++ 版是在 spawnFood() 里刷新
        // foodSpawnTime 的，移植时漏了这一步，新果子会继承旧果子的剩余寿命
        //（果子吃得越晚，新长出来的就越快到期，甚至刚出现就消失）。
        m->foodSpawnMs = snake_now_ms();
        int cur = m->snake.len + 1;
        if (cur > g_highScore) g_highScore = cur;
    }

    for (int i = 0; i < m->snake.len; i++)
        board_set(&m->board, &m->snake.body[i], CELL_SNAKE_BODY);
    board_set(&m->board, &m->snake.newHead, CELL_SNAKE_HEAD);
    board_set(&m->board, &m->snake.oldTail, CELL_EMPTY);
    if (m->food.exist) board_set(&m->board, &m->food.pos, CELL_FOOD);

    if (m->snake.len >= m->freeCells) {
        m->won = 1; m->gameOver = 1;
        if (m->snake.len > g_highScore) g_highScore = m->snake.len;
    }
}

static void single_render_full(SingleMode* m)
{
    SnakeScreen* ui = m->ui;
    for (int y = 0; y < m->board.h; y++)
        for (int x = 0; x < m->board.w; x++) {
            Position p = { x, y };
            snake_screen_set_cell(ui, x, y, g_cellColor[board_get(&m->board, &p)]);
        }
    snake_screen_set_score(ui, m->snake.len, g_highScore);
    snake_screen_set_stamina(ui, SNAKE_MAX_STAMINA, m->snake.stamina);
    m->lastFoodPos = m->food.pos;
}

// 增量渲染：严格对齐原始 ScreenRender::render_snake_food 的四步顺序
//   1) 擦上一帧食物   2) 擦旧蛇尾（非增长帧）   3) 画食物+新蛇头   4) 画蛇头后一节
// 时间复杂度 O(1)，空间复杂度 O(1)
static void single_render(SingleMode* m)
{
    SnakeScreen* ui = m->ui;

    // 1) 擦上一帧食物：吃到食物后该格不再标记 FOOD，不擦就会残留一块黄像素
    // （增量渲染只擦蛇尾，没人清它），看起来像"食物没吃到 / 还在原地"
    Position lf = m->lastFoodPos;
    if (lf.x >= 0 && lf.x < m->board.w && lf.y >= 0 && lf.y < m->board.h)
        snake_screen_set_cell(ui, lf.x, lf.y, g_cellColor[CELL_EMPTY]);

    // 2) 擦旧蛇尾：原版 render_snake_food 的第一步，移植时漏掉了。
    // 少了这一步，蛇身永远不会缩回去，走过的每格都被永久染成蛇身色，
    // 视觉上就是"蛇越走越长、留下一整条痕迹"，与逻辑上的蛇长完全不符。
    // 增长那一帧不擦（旧尾就是新长出的那一节），与原版一致。
    if (!m->snake.growing) {
        Position ot = m->snake.oldTail;
        snake_screen_set_cell(ui, ot.x, ot.y, g_cellColor[CELL_EMPTY]);
    }

    // 3) 画当前食物与蛇头
    if (m->food.exist)
        snake_screen_set_cell(ui, m->food.pos.x, m->food.pos.y, g_cellColor[CELL_FOOD]);
    Position h = snake_new_head(&m->snake);
    snake_screen_set_cell(ui, h.x, h.y, g_cellColor[CELL_SNAKE_HEAD]);

    // 4) 画蛇头后一节（即上一帧的蛇头，颜色由头色转为身色）
    if (m->snake.len > 1)
        snake_screen_set_cell(ui, m->snake.body[1].x, m->snake.body[1].y, g_cellColor[CELL_SNAKE_BODY]);

    m->lastFoodPos = m->food.pos;
    snake_screen_set_stamina(ui, SNAKE_MAX_STAMINA, m->snake.stamina);
}

static void single_save(SingleMode* m)
{
    m->saveValid = 1;
    m->hasSave = 1;
    // 蛇身
    if (m->saveBody) free(m->saveBody);
    m->saveBodyLen = m->snake.len;
    m->saveBody = (Position*)malloc((size_t)m->snake.len * sizeof(Position));
    for (int i = 0; i < m->snake.len; i++) m->saveBody[i] = m->snake.body[i];
    m->saveDir = m->snake.curDir;
    m->saveGrowing = m->snake.growing;
    m->saveDead = m->snake.dead;
    m->saveNewHead = m->snake.newHead;
    m->saveOldTail = m->snake.oldTail;
    m->saveSpeed = m->snake.speed;
    m->saveStamina = m->snake.stamina;
    m->saveFoodPos = m->food.pos;
    m->saveFoodExist = m->food.exist;
    m->saveWin = m->won;
    // 棋盘网格
    if (m->saveGrid) free(m->saveGrid);
    m->saveGW = m->board.w; m->saveGH = m->board.h;
    m->saveGrid = (CellType*)malloc((size_t)m->board.w * m->board.h * sizeof(CellType));
    memcpy(m->saveGrid, m->board.grid, (size_t)m->board.w * m->board.h * sizeof(CellType));
}
static void single_load(SingleMode* m)
{
    if (!m->saveValid) return;
    snake_restore(&m->snake, m->saveBody, m->saveBodyLen, m->saveDir,
                  m->saveGrowing, m->saveDead, m->saveNewHead, m->saveOldTail,
                  m->saveSpeed, m->saveStamina);
    m->food.pos = m->saveFoodPos; m->food.exist = m->saveFoodExist; m->foodSpawnMs = snake_now_ms();
    board_clear(&m->board);
    memcpy(m->board.grid, m->saveGrid, (size_t)m->board.w * m->board.h * sizeof(CellType));
    m->won = m->saveWin;
    m->freeCells = (m->board.w - 2) * (m->board.h - 2);
    m->gameOver = 0;
    m->hasSave = 1;
}

/* ===================== TwoPlayerMode ===================== */
static int two_check_collision(TwoPlayerMode* m, Snake* self, Snake* other)
{
    Position head = snake_new_head(self);
    if (head.x < 1 || head.x > m->board.w - 2 || head.y < 1 || head.y > m->board.h - 2)
        return 1;
    const Position* b = self->body;
    for (int i = 1; i + 1 < self->len; i++)
        if (head.x == b[i].x && head.y == b[i].y) return 1;
    const Position* ob = other->body;
    for (int i = 0; i < other->len; i++)
        if (head.x == ob[i].x && head.y == ob[i].y) return 1;
    return 0;
}
static void two_finalize(TwoPlayerMode* m)
{
    if (m->gameOver) return;
    if (m->dead1 && m->dead2) {
        int l1 = m->s1.len, l2 = m->s2.len;
        m->winner = (l1 > l2) ? 1 : (l2 > l1) ? 2 : 0;
    } else if (m->dead1) m->winner = 2;
    else if (m->dead2) m->winner = 1;
    m->gameOver = 1;
}
static void two_init_game(TwoPlayerMode* m)
{
    board_free(&m->board);
    board_init(&m->board, g_spec.gridW, g_spec.gridH);
    board_build_walls(&m->board);
    int w = m->board.w, h = m->board.h;

    Position head1 = { w / 4,     h - 3 };
    Position head2 = { 3 * w / 4, 2 };
    m->s1.headColor = 0xFF0000; m->s1.bodyColor = 0x00D4AA;
    m->s2.headColor = 0xFFC400; m->s2.bodyColor = 0xFF8000;
    snake_init_at(&m->s1, head1, DIR_RIGHT, 3);
    snake_init_at(&m->s2, head2, DIR_LEFT, 3);

    m->dead1 = m->dead2 = 0; m->gameOver = 0; m->winner = 0;
    m->started = 0; m->paused = 0; m->running = 0;

    m->food.exist = 0;
    food_spawn(&m->food, &m->board);
    for (int i = 0; i < m->s1.len; i++) board_set(&m->board, &m->s1.body[i], CELL_SNAKE_BODY);
    board_set(&m->board, &m->s1.body[0], CELL_SNAKE_HEAD);
    for (int i = 0; i < m->s2.len; i++) board_set(&m->board, &m->s2.body[i], CELL_SNAKE_BODY);
    board_set(&m->board, &m->s2.body[0], CELL_SNAKE_HEAD);
    if (m->food.exist) board_set(&m->board, &m->food.pos, CELL_FOOD);
}
static void two_handle_input(TwoPlayerMode* m, Function key)
{
    switch (key) {
    case FN_UP:    snake_change_dir(&m->s1, DIR_UP);    m->started = 1; break;
    case FN_DOWN:  snake_change_dir(&m->s1, DIR_DOWN);  m->started = 1; break;
    case FN_LEFT:  snake_change_dir(&m->s1, DIR_LEFT);  m->started = 1; break;
    case FN_RIGHT: snake_change_dir(&m->s1, DIR_RIGHT); m->started = 1; break;
    case FN_BOOST1: snake_toggle_speed(&m->s1); break;
    case FN_UP2:    snake_change_dir(&m->s2, DIR_UP);    m->started = 1; break;
    case FN_DOWN2:  snake_change_dir(&m->s2, DIR_DOWN);  m->started = 1; break;
    case FN_LEFT2:  snake_change_dir(&m->s2, DIR_LEFT);  m->started = 1; break;
    case FN_RIGHT2: snake_change_dir(&m->s2, DIR_RIGHT); m->started = 1; break;
    case FN_BOOST2: snake_toggle_speed(&m->s2); break;
    default: break;
    }
}
// 停止/结束双人对局。原来这里要 join 两条蛇线程；现在节拍整个跑在 LVGL 定时器里，
// 只需把对局标记为未运行（暂停/结算/切状态全部由状态机负责）。
static void two_stop(TwoPlayerMode* m)
{
    m->running = 0;
}
static void two_draw_matured(TwoPlayerMode* m, Snake* self, int headColor, int bodyColor)
{
    SnakeScreen* ui = m->ui;
    if (!self->growing)
        snake_screen_set_cell(ui, self->oldTail.x, self->oldTail.y, g_cellColor[CELL_EMPTY]);
    Position h = snake_new_head(self);
    snake_screen_set_cell(ui, h.x, h.y, headColor);
    if (self->len > 1)
        snake_screen_set_cell(ui, self->body[1].x, self->body[1].y, bodyColor);
}
// 单条蛇走一步：移动 → 碰撞 → 吃食物 → 维护棋盘标记。
// 与原始 snakeThread 中 "self->move() 之后" 的那一段逐句对应，只是不再有线程与锁。
// 时间复杂度 O(蛇长)，空间复杂度 O(1)
static void two_step_one(TwoPlayerMode* m, Snake* self, Snake* other,
                         int* selfDead, int* otherDead)
{
    if (*selfDead) return;                       // 已死的蛇不再动

    snake_move(self);
    if (two_check_collision(m, self, other)) {
        self->dead = 1; *selfDead = 1; two_finalize(m);
        return;
    }
    if (snake_new_head(self).x == snake_new_head(other).x &&
        snake_new_head(self).y == snake_new_head(other).y) {
        self->dead = 1; other->dead = 1;         // 两蛇头相撞 → 都死
        *selfDead = 1; *otherDead = 1; two_finalize(m);
        return;
    }
    if (snake_new_head(self).x == m->food.pos.x &&
        snake_new_head(self).y == m->food.pos.y) {
        snake_grow(self); m->food.exist = 0; food_spawn(&m->food, &m->board);
    }
    for (int i = 0; i < self->len; i++)
        board_set(&m->board, &self->body[i], CELL_SNAKE_BODY);
    board_set(&m->board, &self->newHead, CELL_SNAKE_HEAD);
    board_set(&m->board, &self->oldTail, CELL_EMPTY);
    if (m->food.exist) board_set(&m->board, &m->food.pos, CELL_FOOD);
}

// 双人节拍：由引擎的 LVGL 定时器统一调用，一次调用推进两条蛇各自的时间片。
// 计时用单调时钟量真实流逝的 dt（不假定调用间隔正好是多少），两条蛇速度不同 → interval 不同，
// 各自的 acc 累计到自己的 interval 才走一步，且 acc 保留余数（长期平均步长严格等于 interval）。
// 全程在 LVGL 线程内完成：无锁、无抢占，不存在"被渲染挡一下就走慢一步"。
// 时间复杂度 O(1)（单步内部代价另计），空间复杂度 O(1)
static void two_advance(TwoPlayerMode* m)
{
    uint32_t now = snake_now_ms();
    if (!m->running) { m->lastMs = now; return; }

    if (m->paused || !m->started) {   // 暂停/准备阶段：冻结累计，恢复后重新计时
        m->acc1 = m->acc2 = 0;
        m->lastMs = now;
        return;
    }

    uint32_t dt = now - m->lastMs;
    m->lastMs = now;
    if (dt > 100) dt = 100;           // 长时间没被调度（切屏/整屏重绘）不追帧，避免瞬移
    if (m->gameOver) { m->acc1 = m->acc2 = 0; return; }

    m->acc1 += (int)dt;
    m->acc2 += (int)dt;
    int iv1 = SNAKE_GAME_SPEED / m->s1.speed;
    int iv2 = SNAKE_GAME_SPEED / m->s2.speed;
    if (m->acc1 >= iv1) { m->acc1 -= iv1; two_step_one(m, &m->s1, &m->s2, &m->dead1, &m->dead2); }
    if (m->acc2 >= iv2) { m->acc2 -= iv2; two_step_one(m, &m->s2, &m->s1, &m->dead2, &m->dead1); }
}
static void two_reset(TwoPlayerMode* m)
{
    two_stop(m);
    two_init_game(m);
    m->paused = 0;
    m->running = 1;                          // 交由引擎的 LVGL 定时器驱动，不再创建线程
    m->lastMs = snake_now_ms();
    m->acc1 = m->acc2 = 0;
}
static void two_render_full(TwoPlayerMode* m)
{
    SnakeScreen* ui = m->ui;
    for (int y = 0; y < m->board.h; y++)
        for (int x = 0; x < m->board.w; x++) {
            Position p = { x, y };
            snake_screen_set_cell(ui, x, y, g_cellColor[board_get(&m->board, &p)]);
        }
    for (int i = 0; i < m->s1.len; i++)
        snake_screen_set_cell(ui, m->s1.body[i].x, m->s1.body[i].y,
            (i == 0) ? m->s1.headColor : m->s1.bodyColor);
    for (int i = 0; i < m->s2.len; i++)
        snake_screen_set_cell(ui, m->s2.body[i].x, m->s2.body[i].y,
            (i == 0) ? m->s2.headColor : m->s2.bodyColor);
    m->lastFoodPos = m->food.pos;
    snake_screen_set_len(ui, m->s1.len, m->s2.len);
    snake_screen_set_stamina2(ui, SNAKE_MAX_STAMINA, m->s1.stamina, m->s2.stamina);
}
static void two_render(TwoPlayerMode* m)
{
    SnakeScreen* ui = m->ui;
    Position lf = m->lastFoodPos;
    if (lf.x >= 0 && lf.x < m->board.w && lf.y >= 0 && lf.y < m->board.h)
        snake_screen_set_cell(ui, lf.x, lf.y, g_cellColor[CELL_EMPTY]);
    if (m->food.exist)
        snake_screen_set_cell(ui, m->food.pos.x, m->food.pos.y, g_cellColor[CELL_FOOD]);
    two_draw_matured(m, &m->s1, m->s1.headColor, m->s1.bodyColor);
    two_draw_matured(m, &m->s2, m->s2.headColor, m->s2.bodyColor);
    m->lastFoodPos = m->food.pos;

    snake_screen_set_len(ui, m->s1.len, m->s2.len);
    snake_screen_set_stamina2(ui, SNAKE_MAX_STAMINA, m->s1.stamina, m->s2.stamina);

    if (m->gameOver) {
        char buf[48];
        if (m->winner == 0) lv_label_set_text(ui->lblOverlay, "游戏结束\n平局");
        else { snprintf(buf, sizeof(buf), "游戏结束\n玩家%d 赢", m->winner); lv_label_set_text(ui->lblOverlay, buf); }
    }
}
static void two_set_paused(TwoPlayerMode* m, int p) { m->paused = p; }

/* ===================== Engine ===================== */
Engine g_engine;

static void refresh_tick(Engine* e, int useStart)
{
    int speed = useStart ? g_spec.startSpeed : g_engine.single->snake.speed;
    e->tickInterval = SNAKE_GAME_SPEED / speed;
}
static void engine_enter_menu(Engine* e)
{
    two_stop(e->two);
    e->state = ST_MENU;
    e->menu->hasSave = e->single->hasSave;
    menu_render(e->menu);
    apply_ui_state(e);
}
static void on_menu_single(Engine* e)
{
    two_stop(e->two);
    single_init_game(e->single);
    e->state = ST_SINGLE;
    refresh_tick(e, 1);
    e->lastTick = snake_now_ms();
    single_render_full(e->single);
    apply_ui_state(e);
}
static void on_menu_two(Engine* e)
{
    two_reset(e->two);
    e->state = ST_TWO;
    e->tickInterval = 10;
    e->lastTick = snake_now_ms();
    two_render_full(e->two);
    apply_ui_state(e);
}
static void on_menu_continue(Engine* e)
{
    single_load(e->single);
    e->state = ST_SINGLE;
    refresh_tick(e, 0);
    e->lastTick = snake_now_ms();
    single_render_full(e->single);
    apply_ui_state(e);
}
static void engine_do_menu(Engine* e, MenuItem act)
{
    switch (act) {
    case MI_SINGLE:  on_menu_single(e);  break;
    case MI_TWO:     on_menu_two(e);     break;
    case MI_CONTINUE: on_menu_continue(e); break;
    case MI_CONFIG:  engine_enter_menu(e); break;
    default: break;
    }
}
static void engine_enter_pause(Engine* e)
{
    if (e->state != ST_SINGLE && e->state != ST_TWO) return;
    e->prev = e->state;
    e->state = ST_PAUSED;
    if (e->prev == ST_TWO) two_set_paused(e->two, 1);
    char buf[64];
    if (e->prev == ST_SINGLE)
        snprintf(buf, sizeof(buf), "暂停\n得分: %d", e->single->snake.len);
    else
        snprintf(buf, sizeof(buf), "暂停\nP1: %d  P2: %d", e->two->s1.len, e->two->s2.len);
    snake_screen_overlay_show(e->ui, 1, buf);
    apply_ui_state(e);
}
static void engine_exit_pause(Engine* e)
{
    if (e->state != ST_PAUSED) return;
    snake_screen_overlay_show(e->ui, 0, NULL);
    e->state = e->prev;
    e->lastTick = snake_now_ms();
    if (e->prev == ST_TWO) two_set_paused(e->two, 0);
    apply_ui_state(e);
}
static void engine_handle_single(Engine* e, Function key)
{
    switch (key) {
    case FN_PAUSE: engine_enter_pause(e); break;
    case FN_MENU:  single_save(e->single); e->prev = ST_SINGLE; engine_enter_menu(e); break;
    case FN_QUIT:  engine_enter_menu(e); break;
    case FN_BOOST1: snake_toggle_speed(&e->single->snake); refresh_tick(e, 0); break;
    default: single_handle_input(e->single, key); break;
    }
}
static void engine_handle_two(Engine* e, Function key)
{
    switch (key) {
    case FN_PAUSE: engine_enter_pause(e); break;
    case FN_MENU:  two_stop(e->two); engine_enter_menu(e); break;
    case FN_QUIT:  engine_enter_menu(e); break;
    default: two_handle_input(e->two, key); break;
    }
}
static void engine_handle_paused(Engine* e, Function key)
{
    switch (key) {
    case FN_PAUSE: case FN_RESUME: engine_exit_pause(e); break;
    case FN_MENU: single_save(e->single); engine_exit_pause(e); engine_enter_menu(e); break;
    case FN_QUIT: engine_enter_menu(e); break;
    default: break;
    }
}
static void engine_handle_over(Engine* e, Function key)
{
    switch (key) {
    case FN_MENU: case FN_CONFIRM: case FN_RESUME: engine_enter_menu(e); break;
    case FN_QUIT: engine_enter_menu(e); break;
    default: break;
    }
}
static void draw_game_over(Engine* e)
{
    char buf[64];
    if (e->single->won)
        snprintf(buf, sizeof(buf), "胜利\n得分: %d", e->single->snake.len);
    else
        snprintf(buf, sizeof(buf), "游戏结束\n得分: %d", e->single->snake.len);
    snake_screen_overlay_show(e->ui, 1, buf);
}
void engine_handle_key(Engine* e, Function key)
{
    if (key == FN_NONE) return;
    switch (e->state) {
    case ST_MENU: {
        MenuItem act = menu_handle_input(e->menu, key);
        if (act != MI_COUNT) engine_do_menu(e, act);
        else { apply_ui_state(e); menu_render(e->menu); }
        break;
    }
    case ST_SINGLE: engine_handle_single(e, key); break;
    case ST_TWO:    engine_handle_two(e, key);    break;
    case ST_PAUSED: engine_handle_paused(e, key); break;
    case ST_OVER:   engine_handle_over(e, key);   break;
    }
}
// 节拍推进。两个要点（原来的写法会让移动节奏忽快忽慢）：
//   1) 时间基准用 snake_now_ms()（CLOCK_MONOTONIC），不再用 lv_tick_get()：
//      后者在本工程是 fbdev 驱动的墙钟回调，且推进点寄生在 LVGL 主循环里；
//   2) 推进按"应到时刻"累加（lastTick += tickInterval），保留本次多出来的余数。
//      旧写法 lastTick = now 会把余数丢掉，导致每步实际耗时 = tickInterval 向上取整到
//      轮询周期，而轮询周期随 LVGL 每轮的定时器+渲染负载漂移，
//      于是周期性地多等一个轮询周期 → 表现为"慢一步、快几步"地循环。
// 时间复杂度 O(1)，空间复杂度 O(1)
static void engine_tick(Engine* e)
{
    uint32_t now = snake_now_ms();
    uint32_t el = now - e->lastTick;
    if (e->state == ST_SINGLE) {
        if (el < (uint32_t)e->tickInterval) return;
        e->lastTick += (uint32_t)e->tickInterval;   // 保留余数，长期平均步长严格等于 tickInterval
        // 落后超过一整拍（例如刚从暂停/遮挡中恢复）就重新对齐，避免连续补步造成瞬移
        if (now - e->lastTick >= (uint32_t)e->tickInterval) e->lastTick = now;
        single_update(e->single);
        single_render(e->single);
        if (e->single->gameOver) {
            e->prev = ST_SINGLE;   // 记录对局模式，右侧栏据此决定显示哪块信息
            e->state = ST_OVER;
            draw_game_over(e);
            apply_ui_state(e);
        } else {
            refresh_tick(e, 0);
        }
    } else if (e->state == ST_TWO) {
        if (el < (uint32_t)e->tickInterval) return;
        e->lastTick += (uint32_t)e->tickInterval;
        if (now - e->lastTick >= (uint32_t)e->tickInterval) e->lastTick = now;
        two_advance(e->two);   // 两条蛇各自的时间片推进（不再有独立线程）
        two_render(e->two);
        if (e->two->gameOver) {
            e->prev = ST_TWO;      // 同上
            e->state = ST_OVER;
            apply_ui_state(e); // 结算文字已在 two_render 写入遮罩
        }
    }
}
static void engine_timer_cb(lv_timer_t* t)
{
    Engine* e = (Engine*)lv_timer_get_user_data(t);
    engine_tick(e);
}

/* ===================== 对外入口 ===================== */
void creat_my_snake(void)
{
    // 创建贪吃蛇游戏屏幕（与 2048 的 src2048 同级）
    src_snake = lv_obj_create(NULL);

    // 引擎对象初始化
    memset(&g_engine, 0, sizeof(Engine));
    g_engine.state = ST_MENU;
    g_engine.ui = snake_screen_create(src_snake);

    // 菜单 / 单人 / 双人 模式实例
    static MenuMode menu;
    static SingleMode single;
    static TwoPlayerMode two;
    memset(&menu, 0, sizeof(menu));
    memset(&single, 0, sizeof(single));
    memset(&two, 0, sizeof(two));
    menu.ui = g_engine.ui;
    menu.selected = MI_SINGLE;
    menu.hasSave = 0;
    single.ui = g_engine.ui;
    two.ui = g_engine.ui;

    g_engine.menu = &menu;
    g_engine.single = &single;
    g_engine.two = &two;

    // 初始化单人对局（仅建棋盘，不渲染；进入菜单态）
    single_init_game(&single);

    // 创建底部触控控制条与按钮
    ctrl_create(&g_engine);

    // "返回盒子"按钮（独立于引擎内 M 菜单，给用户一个明确出口）。
    // 放在右侧栏最下方，而不是左上角浮在棋盘上——棋盘现在占满左侧整块草地，浮起来会挡画面。
    int st_back = style_btn_init(1, 0x2B2B2B, 0xF7F9FA, 24);
    lv_obj_t* btn_box = create_button(st_back, g_engine.ui->panel, LV_ALIGN_TOP_LEFT, 4, 560, 116, 28, "返回盒子", snake_back_cb, NULL);
    lv_obj_set_style_bg_opa(btn_box, LV_OPA_70, LV_STATE_DEFAULT);

    // 键盘输入（终端 ANSI 转义 + evdev 自动检索）：与触控按钮共用 engine_handle_key 入口
    snake_input_init();

    // 默认进入主菜单
    engine_enter_menu(&g_engine);

    // 用 lv_timer 驱动节拍（替代原始独立线程的 usleep 主循环）。
    // 周期取 5ms 而不是 10ms：节拍判定只能落在轮询点上，轮询越细，
    // 每步实际耗时与 tickInterval 的偏差越小（配合 engine_tick 里的"保留余数"累加）。
    lv_timer_create(engine_timer_cb, 5, &g_engine);
}
