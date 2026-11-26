/**
 * @file snake_core.c
 * @brief 贪吃蛇(LVGL 版) 核心：World_Unit / GameSpec / Board / Food / Snake / 渲染后端
 * @details 逻辑与 D:\Share\2-project_GreedySnake 的 core/ 完全一致，
 *          仅把"帧缓冲像素渲染"替换为 LVGL 对象渲染（单元网格 + 标签 + 遮罩）。
 */

#include "snake.h"
#include <stdio.h>      // snprintf：侧栏分数/体力/配置文字格式化
#include <stdlib.h>
#include <string.h>
#include <time.h>       // clock_gettime：单调时钟 snake_now_ms
// 核心层是纯逻辑：不包含 unistd/pthread —— 整个游戏不再有 sleep、线程或自建循环，
// 节拍只由 LVGL 定时器驱动（见 snake_app.c 的 engine_tick / two_advance）。

/* ===================== 颜色 / 配置档位 ===================== */
// 与原始 Color[] 下标一一对应：EMPTY, SNAKE_BODY, SNAKE_HEAD, FOOD, WALL
int g_cellColor[CELL_WU] = {
    0x00FF00,   // EMPTY：草地（原始 0xFF00，LVGL 按 0xRRGGBB 解析）
    0x00D4AA,   // SNAKE_BODY：全息青
    0xFF0000,   // SNAKE_HEAD：火山红
    0xFFFF00,   // FOOD：脉冲黄
    0x0000FF    // WALL：纯蓝
};

const int CELL_SIZES[3]   = { 19, 29, 49 };
const int START_SPEEDS[3]  = { 20, 25, 35 };
const int BOOST_SPEEDS[3]  = { 35, 50, 70 };
int g_cellSizeIdx  = 0;
int g_startSpeedIdx = 0;
int g_boostSpeedIdx = 0;

GameSpec g_spec = { 19, 1, 20, 35, 0, 0 };

// 真实屏幕尺寸（创建界面时由父屏幕对象测得，替代原始 Screen::getWidth/Height）
int g_screenW = 1024;
int g_screenH = 600;

// 底部控制条已取消：SNAKE_CTRL_H 在 snake.h 中定义为 0，触控键全部移入右侧栏

/* 依据真实屏幕尺寸重算 gridW / gridH（与原始 getGridW / getGridH 一致） */
void spec_recalc(int screenW, int screenH)
{
    g_spec.cellSize   = CELL_SIZES[g_cellSizeIdx];
    g_spec.gap        = 1;
    g_spec.startSpeed = START_SPEEDS[g_startSpeedIdx];
    g_spec.boostSpeed = BOOST_SPEEDS[g_boostSpeedIdx];

    int availW = (screenW > SNAKE_PANEL_W) ? (screenW - SNAKE_PANEL_W) : (screenW);
    int step   = g_spec.cellSize + g_spec.gap;
    g_spec.gridW = availW / step;

    // 高度方向：SNAKE_CTRL_H 已置 0（触控键移入右侧栏，不再占底部），
    // 棋盘因此占满整屏高度，与原始 C++ 版的 getGridH(screenH) = screenH / cellStep 完全一致
    int availH = (screenH > SNAKE_CTRL_H) ? (screenH - SNAKE_CTRL_H) : screenH;
    g_spec.gridH = availH / step;
    if (g_spec.gridH < 1) g_spec.gridH = 1;
    if (g_spec.gridW < 1) g_spec.gridW = 1;
}

/* ===================== rand_helper ===================== */
// [min,max] 闭区间；min>max 时交换（与原始 rand_int 一致）
int rand_int(int min, int max)
{
    if (min > max) { int t = min; min = max; max = t; }
    return (int)lv_rand(min, max);
}

/* ===================== 单调时钟 ===================== */
// 游戏节拍与计时的统一时间基准（CLOCK_MONOTONIC）。为什么不用 lv_tick_get()：
//   1) 本工程里 lv_tick_get() 走的是 fbdev 驱动注册的 tick_get_cb，底层 gettimeofday 是
//      【墙钟】：非单调，NTP 校时会让 now-lastTick 突然变大（连跳多步）或回绕；
//   2) 它的推进点只存在于 lv_timer_handler() 里，等于把游戏节拍寄生在 LVGL 主循环上，
//      LVGL 一轮渲染变慢，节拍判定就整体后移，表现为移动节奏忽快忽慢。
// CLOCK_MONOTONIC 单调递增且与 LVGL 循环无关。
// 时间复杂度 O(1)，空间复杂度 O(1)
uint32_t snake_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

/* ===================== Board ===================== */
void board_init(Board* b, int w, int h)
{
    b->w = w; b->h = h;
    b->grid = (CellType*)calloc((size_t)w * h, sizeof(CellType));
}
void board_free(Board* b)
{
    if (b->grid) { free(b->grid); b->grid = NULL; }
    b->w = b->h = 0;
}
int board_valid(const Board* b, const Position* p)
{
    return p->x >= 0 && p->x < b->w && p->y >= 0 && p->y < b->h;
}
CellType board_get(const Board* b, const Position* p)
{
    if (!board_valid(b, p)) return CELL_WALL; // 越界按墙（与原始一致）
    return b->grid[p->y * b->w + p->x];
}
void board_set(Board* b, const Position* p, CellType t)
{
    if (board_valid(b, p)) b->grid[p->y * b->w + p->x] = t;
}
void board_clear(Board* b)
{
    if (b->grid) memset(b->grid, 0, (size_t)b->w * b->h * sizeof(CellType));
}
void board_build_walls(Board* b)
{
    for (int i = 0; i < b->w; i++) {
        Position p; p.x = i; p.y = 0;                 board_set(b, &p, CELL_WALL);
        p.y = b->h - 1;                              board_set(b, &p, CELL_WALL);
    }
    for (int i = 0; i < b->h; i++) {
        Position p; p.x = 0; p.y = i;                 board_set(b, &p, CELL_WALL);
        p.x = b->w - 1;                              board_set(b, &p, CELL_WALL);
    }
}

/* ===================== Food ===================== */
void food_init(Food* f, int color)
{
    f->pos.x = 0; f->pos.y = 0; f->exist = 0; f->color = color;
}
void food_update(Food* f, int w, int h)
{
    int mx = (w - 2 > 1) ? (w - 2) : 1;
    int my = (h - 2 > 1) ? (h - 2) : 1;
    f->pos.x = rand_int(1, mx);
    f->pos.y = rand_int(1, my);
}
void food_spawn(Food* f, Board* b)
{
    if (f->exist) return;
    int max = 1000;
    while (max--) {
        food_update(f, b->w, b->h);
        if (board_valid(b, &f->pos) && board_get(b, &f->pos) == CELL_EMPTY) {
            board_set(b, &f->pos, CELL_FOOD);
            f->exist = 1;
            return;
        }
    }
    // 兜底：顺序找一个空格
    for (int x = 1; x < b->w - 1; x++) {
        for (int y = 1; y < b->h - 1; y++) {
            Position p = { x, y };
            if (board_get(b, &p) == CELL_EMPTY) {
                f->pos = p;
                board_set(b, &p, CELL_FOOD);
                f->exist = 1;
                return;
            }
        }
    }
}

/* ===================== Snake ===================== */
// 是否为相反方向（基于枚举值异或配对，与原始 isOppositeDirection 一致）
static int is_opposite(Direction cur, Direction next)
{
    return cur != DIR_NONE && next != DIR_NONE &&
           ((int)cur ^ 1) == (int)next;
}

static void snake_ensure_cap(Snake* s, int need)
{
    if (s->cap >= need) return;
    int nc = s->cap ? s->cap * 2 : 8;
    while (nc < need) nc *= 2;
    s->body = (Position*)realloc(s->body, (size_t)nc * sizeof(Position));
    s->cap = nc;
}

void snake_init_empty(Snake* s)
{
    memset(s, 0, sizeof(Snake));
    s->cap = 8;
    s->body = (Position*)malloc((size_t)s->cap * sizeof(Position));
    s->len = 0;
    s->curDir = s->nextDir = DIR_LEFT;
    s->speed = g_spec.startSpeed;
    s->stamina = SNAKE_MAX_STAMINA;
    s->canBoost = 1;
    s->headColor = 0xFF0000;
    s->bodyColor = 0x00D4AA;
    s->newHead = (Position){0,0};
    s->oldTail = (Position){0,0};
}
void snake_free(Snake* s)
{
    if (s->body) { free(s->body); s->body = NULL; }
    s->cap = s->len = 0;
}

// 开局/重置：用头尾两点构造（单人 initGame 用）
void snake_reset(Snake* s, Position head, Position tail)
{
    snake_ensure_cap(s, 2);
    s->len = 2;
    s->body[0] = head;
    s->body[1] = tail;
    s->curDir = s->nextDir = DIR_LEFT;
    s->growing = 0; s->dead = 0;
    s->speed = g_spec.startSpeed;
    s->stamina = SNAKE_MAX_STAMINA;
    s->canBoost = 1;
    s->newHead = head;
    s->oldTail = tail;
}

// 在指定位置/朝向/长度初始化蛇身（双人模式对角出生用）
void snake_init_at(Snake* s, Position head, Direction dir, int len)
{
    snake_ensure_cap(s, len > 0 ? len : 1);
    int dx = 0, dy = 0;
    switch (dir) {
        case DIR_UP:    dy = 1;  break;
        case DIR_DOWN:  dy = -1; break;
        case DIR_LEFT:  dx = 1;  break;
        case DIR_RIGHT: dx = -1; break;
        default: break;
    }
    s->len = len;
    for (int i = 0; i < len; i++)
        s->body[i] = (Position){ head.x + dx * i, head.y + dy * i };
    s->curDir = s->nextDir = dir;
    s->growing = 0; s->dead = 0;
    s->speed = g_spec.startSpeed;
    s->stamina = SNAKE_MAX_STAMINA;
    s->canBoost = 1;
    s->newHead = s->body[0];
    s->oldTail = s->body[s->len - 1];
}

void snake_move(Snake* s)
{
    if (!is_opposite(s->curDir, s->nextDir))
        s->curDir = s->nextDir;

    Position nh = s->body[0];
    switch (s->curDir) {
        case DIR_UP:    nh.y--; break;
        case DIR_DOWN:  nh.y++; break;
        case DIR_LEFT:  nh.x--; break;
        case DIR_RIGHT: nh.x++; break;
        default: break;
    }
    // 头插：ensure_cap 保证容量 >= len+1，移位后数组有效元素为 body[0..len]（共 len+1 节）
    snake_ensure_cap(s, s->len + 1);
    memmove(&s->body[1], &s->body[0], (size_t)s->len * sizeof(Position));
    s->body[0] = nh;
    s->newHead = nh;

    // 长度记账必须与原始 deque 版严格对齐（这是"开局就越界"的根因）：
    //   增长(growing)：push_front 且不 pop_back → 长度 +1，旧尾仍是 body[len]
    //   非增长      ：push_front + pop_back → 长度不变，被丢弃的旧尾就是移位后的最后一节 body[len]
    // 反例（移植版曾写成 len-- / 漏 len++）：每走一步长度 -1，两步后 len 归零，
    // 第三步读 body[len-1] 即 body[-2] 属数组越界读；第四步 memmove 长度为负，
    // 转 size_t 后变成天量拷贝，直接越界写内存，表现为开局几步就崩/蛇身错乱。
    // 时间复杂度 O(1)，空间复杂度 O(1)。
    if (s->growing) {
        s->len++;                       // 增长：保留旧尾，长度 +1（容量已由上面 ensure_cap 保障）
        s->growing = 0;
    } else {
        s->oldTail = s->body[s->len];   // 旧尾 = 移位后的最后一节（索引 len，非 len-1）
    }

    // 体力逻辑（与原始 move() 一致）：加速每步 -1，正常每步 +1；耗尽禁止加速，>10 恢复
    if (s->speed == g_spec.boostSpeed) {
        s->stamina = (s->stamina > 0) ? (s->stamina - 1) : 0;
        if (s->stamina == 0) {
            s->canBoost = 0;
            s->speed = g_spec.startSpeed;
        }
    } else {
        s->stamina = (s->stamina < SNAKE_MAX_STAMINA) ? (s->stamina + 1) : SNAKE_MAX_STAMINA;
        if (s->stamina > 10) s->canBoost = 1;
    }
}

void snake_change_dir(Snake* s, Direction d) { s->nextDir = d; }
void snake_set_dir(Snake* s, Direction d)   { s->curDir = s->nextDir = d; }
void snake_set_speed(Snake* s, int sp)        { s->speed = sp; }

void snake_toggle_speed(Snake* s)
{
    if (!s->canBoost) return;
    s->speed = (s->speed == g_spec.startSpeed) ? g_spec.boostSpeed : g_spec.startSpeed;
}
void snake_grow(Snake* s)    { s->growing = 1; }
void snake_setdead(Snake* s) { s->dead = 1; }
int  snake_is_growing(const Snake* s) { return s->growing; }
int  snake_is_dead(const Snake* s)    { return s->dead; }
Position snake_new_head(const Snake* s) { return s->newHead; }
Position snake_old_tail(const Snake* s) { return s->oldTail; }
int  snake_len(const Snake* s)    { return s->len; }
int  snake_speed(const Snake* s)   { return s->speed; }
int  snake_stamina(const Snake* s) { return s->stamina; }
int  snake_is_boosting(const Snake* s) { return s->speed == g_spec.boostSpeed; }
int  snake_can_boost(const Snake* s)   { return s->canBoost; }
int  snake_is_body(const Snake* s, const Position* p)
{
    for (int i = 1; i < s->len; i++)
        if (s->body[i].x == p->x && s->body[i].y == p->y) return 1;
    return 0;
}
const Position* snake_body_at(const Snake* s, int i) { return &s->body[i]; }
int  snake_body_count(const Snake* s) { return s->len; }
void snake_set_colors(Snake* s, int head, int body) { s->headColor = head; s->bodyColor = body; }
int  snake_head_color(const Snake* s) { return s->headColor; }
int  snake_body_color(const Snake* s) { return s->bodyColor; }

void snake_restore(Snake* s, const Position* bodyArr, int bodyLen,
                   Direction dir, int growing, int dead,
                   Position newHead, Position oldTail,
                   int speed, int stamina)
{
    snake_ensure_cap(s, bodyLen > 0 ? bodyLen : 1);
    s->len = bodyLen;
    for (int i = 0; i < bodyLen; i++) s->body[i] = bodyArr[i];
    s->curDir = s->nextDir = dir;
    s->growing = growing;
    s->dead = dead;
    s->newHead = newHead;
    s->oldTail = oldTail;
    s->speed = speed;
    s->stamina = stamina;
    s->canBoost = (stamina > 10);
    if (!s->canBoost && s->speed == g_spec.boostSpeed)
        s->speed = g_spec.startSpeed;
}

/* ===================== SnakeScreen（LVGL 渲染后端） ===================== */
static lv_obj_t* g_parent = NULL; // src_snake 屏幕

// 清零容器的默认样式。默认主题会给每个 lv_obj 套上 styles.card：
// 约 20px 内边距(PAD_DEF) + 2px 边框 + 8px 圆角。子对象坐标是相对"内容区"的，
// 内边距不清零会让整块内容右移下移、右下角还被容器裁掉
// （现象就是"草地格子整体偏移、最右一列/最下一行墙壁不见、按钮被切一半"）。
// 凡是要精确摆放子对象的容器都必须走一遍这里。
// 时间复杂度 O(1)，空间复杂度 O(1)
static void container_reset(lv_obj_t* obj)
{
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

SnakeScreen* snake_screen_create(lv_obj_t* parent)
{
    g_parent = parent;
    int sw = lv_obj_get_width(parent);
    int sh = lv_obj_get_height(parent);
    if (sw <= 0) sw = 1024;   // 兜底：防止屏幕对象尚未完成尺寸初始化时网格计算为 0
    if (sh <= 0) sh = 600;
    g_screenW = sw; g_screenH = sh;
    spec_recalc(sw, sh);

    const int step   = g_spec.cellSize + g_spec.gap;
    const int boardW = g_spec.gridW * step;
    const int boardH = g_spec.gridH * step;
    const int menuW  = sw - SNAKE_PANEL_W;   // 棋盘/菜单只占左侧，右侧栏常驻显示

    SnakeScreen* s = (SnakeScreen*)calloc(1, sizeof(SnakeScreen));

    // 1) 棋盘容器：它自身的底色就是"草地"（整块只绘一次）。
    //    格子对象默认透明、只在非空时才上色，避免 900+ 个绿格子互相重叠。
    s->cont = lv_obj_create(parent);
    lv_obj_set_pos(s->cont, SNAKE_BOARD_X0, SNAKE_BOARD_Y0);
    lv_obj_set_size(s->cont, boardW, boardH);
    lv_obj_set_style_bg_color(s->cont, lv_color_hex(g_cellColor[CELL_EMPTY]), 0);
    lv_obj_set_style_bg_opa(s->cont, LV_OPA_COVER, 0);
    container_reset(s->cont);

    // 2) 主菜单容器（盖住棋盘，只占左侧）
    s->menu = lv_obj_create(parent);
    lv_obj_set_pos(s->menu, 0, 0);
    lv_obj_set_size(s->menu, menuW, sh);
    lv_obj_set_style_bg_color(s->menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s->menu, LV_OPA_COVER, 0);
    container_reset(s->menu);

    s->lblTitle = create_label(s->menu, g_cellColor[CELL_SNAKE_BODY], LV_ALIGN_TOP_MID, 0, 40, "贪吃蛇", 48);
    const char* items[4] = { "单人模式", "双人模式", "继续游戏", "配置" };
    for (int i = 0; i < 4; i++) {
        s->lblItems[i] = create_label(s->menu, 0xAAAAAA, LV_ALIGN_TOP_MID, 0,
                                      160 + i * 70, items[i], 32);
    }

    // 3) 遮罩（暂停/结算/配置确认）。必须建在右侧栏之前：
    //    右侧栏与其按键要始终压在最上层，暂停遮罩才不会盖住按键导致点不动。
    s->overlay = lv_obj_create(parent);
    lv_obj_set_pos(s->overlay, 0, 0);
    lv_obj_set_size(s->overlay, sw, sh);
    lv_obj_set_style_bg_color(s->overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s->overlay, LV_OPA_60, 0);
    container_reset(s->overlay);
    // 文字按左侧棋盘区域居中（避开 124px 右侧栏）
    s->lblOverlay = create_label(s->overlay, 0xFFFFFF, LV_ALIGN_CENTER, -SNAKE_PANEL_W / 2, 0, "", 40);

    // 4) 右侧栏：计分 + 配置 + 全部触控按键，所有状态下常驻
    s->panel = lv_obj_create(parent);
    lv_obj_set_pos(s->panel, sw - SNAKE_PANEL_W, 0);
    lv_obj_set_size(s->panel, SNAKE_PANEL_W, sh);
    lv_obj_set_style_bg_color(s->panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s->panel, LV_OPA_COVER, 0);
    container_reset(s->panel);

    // 4.1 单人分数块（菜单/单人/暂停/结算复用）
    s->lblScore = create_label(s->panel, 0xFFFFFF, LV_ALIGN_TOP_MID, 0, 6, "0", 28);
    s->lblHigh  = create_label(s->panel, 0xFFFFFF, LV_ALIGN_TOP_MID, 0, 52, "0", 28);
    s->lblStaTitle = create_label(s->panel, g_cellColor[CELL_SNAKE_BODY], LV_ALIGN_TOP_MID, 0, 104, "STA", 18);
    s->lblSta   = create_label(s->panel, 0x00FF00, LV_ALIGN_TOP_MID, 0, 128, "100/100", 18);

    // 4.2 双人信息块
    s->lblP1 = create_label(s->panel, g_cellColor[CELL_SNAKE_BODY], LV_ALIGN_TOP_LEFT, 6, 6, "P1", 24);
    s->lblLen1 = create_label(s->panel, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 6, 36, "LEN:3", 18);
    s->lblSta1 = create_label(s->panel, 0x00FF00, LV_ALIGN_TOP_LEFT, 6, 60, "100/100", 18);
    s->lblP2 = create_label(s->panel, 0xFFC400, LV_ALIGN_TOP_LEFT, 6, 92, "P2", 24);
    s->lblLen2 = create_label(s->panel, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 6, 122, "LEN:3", 18);
    s->lblSta2 = create_label(s->panel, 0x00FF00, LV_ALIGN_TOP_LEFT, 6, 146, "100/100", 18);

    // 4.3 配置块：每行"项目名 + 当前草稿值"上下两行排布，
    //     124px 宽度放得下，与分数块互斥显示（都不与 y>=170 的按键区重叠）
    s->config = lv_obj_create(s->panel);
    lv_obj_set_pos(s->config, 0, 0);
    lv_obj_set_size(s->config, SNAKE_PANEL_W, 164);
    lv_obj_set_style_bg_color(s->config, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(s->config, LV_OPA_COVER, 0);
    container_reset(s->config);
    const char* cfgNames[3] = { "格子大小", "起步速度", "加速速度" };
    for (int i = 0; i < 3; i++) {
        int ry = 6 + i * 52;
        s->lblCfg[i]    = create_label(s->config, 0xAAAAAA, LV_ALIGN_TOP_LEFT, 6,  ry,      cfgNames[i], 18);
        s->lblCfgVal[i] = create_label(s->config, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 14, ry + 22, "-",          24);
    }

    // 初始隐藏，由 apply_ui_state 按状态打开
    lv_obj_add_flag(s->menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->config, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s->panel, LV_OBJ_FLAG_HIDDEN);

    snake_screen_build_cells(s);
    return s;
}

void snake_screen_free(SnakeScreen* s)
{
    if (!s) return;
    if (s->cells) free(s->cells);
    // LVGL 对象由父屏幕销毁时统一释放，这里只释放数组
    free(s);
}

// 依据当前 g_spec 重建单元网格。
// 格子默认全透明：草地由 cont 一整块绘制，只有墙/食物/蛇这些"非空"格才上色，
// 绘制层级与重绘量都降到最小，也不会出现格子之间的缝隙色差。
// 时间复杂度 O(w*h)，空间复杂度 O(w*h)
void snake_screen_build_cells(SnakeScreen* s)
{
    if (s->cells) {
        int total = s->w * s->h;
        for (int i = 0; i < total; i++)
            if (s->cells[i]) lv_obj_del(s->cells[i]);
        free(s->cells);
        s->cells = NULL;
    }
    s->w = g_spec.gridW; s->h = g_spec.gridH;
    s->cellSize = g_spec.cellSize;
    s->cellStep = g_spec.cellSize + g_spec.gap;

    int total = s->w * s->h;
    s->cells = (lv_obj_t**)calloc((size_t)total, sizeof(lv_obj_t*));
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            lv_obj_t* c = lv_obj_create(s->cont);
            lv_obj_set_pos(c, x * s->cellStep, y * s->cellStep);
            lv_obj_set_size(c, s->cellSize, s->cellSize);
            lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);   // 草地透传，不再逐格铺绿
            lv_obj_set_style_border_width(c, 0, 0);
            lv_obj_set_style_radius(c, 0, 0);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
            s->cells[y * s->w + x] = c;
        }
    }
    // 棋盘占满左侧区域；右侧栏固定 124px 宽、整屏高
    lv_obj_set_size(s->cont, s->w * s->cellStep, s->h * s->cellStep);
    lv_obj_set_size(s->panel, SNAKE_PANEL_W, g_screenH);
    lv_obj_set_pos(s->panel, g_screenW - SNAKE_PANEL_W, 0);
}

// 给单格上色：颜色等于草地色时置为透明（露出容器底色），否则实心填充。
// 时间复杂度 O(1)，空间复杂度 O(1)
void snake_screen_set_cell(SnakeScreen* s, int gx, int gy, int color)
{
    if (!s || gx < 0 || gx >= s->w || gy < 0 || gy >= s->h) return;
    lv_obj_t* c = s->cells[gy * s->w + gx];
    if (color == g_cellColor[CELL_EMPTY]) {
        lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    }
}

// 整盘置色：传入草地色等价于"整盘清空"（全部回到透传状态）。
// 时间复杂度 O(w*h)，空间复杂度 O(1)
void snake_screen_clear_board(SnakeScreen* s, int color)
{
    if (!s) return;
    int total = s->w * s->h;
    int isGrass = (color == g_cellColor[CELL_EMPTY]);
    for (int i = 0; i < total; i++) {
        if (isGrass) {
            lv_obj_set_style_bg_opa(s->cells[i], LV_OPA_TRANSP, 0);
        } else {
            lv_obj_set_style_bg_color(s->cells[i], lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(s->cells[i], LV_OPA_COVER, 0);
        }
    }
}

void snake_screen_set_score(SnakeScreen* s, int score, int high)
{
    if (!s) return;
    char buf[32];
    lv_label_set_text(s->lblScore, (snprintf(buf, sizeof(buf), "%d", score), buf));
    lv_label_set_text(s->lblHigh,  (snprintf(buf, sizeof(buf), "%d", high),  buf));
}
static int stamina_color(int cur, int max)
{
    if (max <= 0) return 0xFFFFFF;
    int pct = cur * 100 / max;
    if (pct >= 60) return 0x00FF00;
    if (pct >= 30) return 0xFFFF00;
    return 0xFF3030;
}
void snake_screen_set_stamina(SnakeScreen* s, int maxS, int curS)
{
    if (!s) return;
    char buf[32];
    lv_label_set_text(s->lblSta, (snprintf(buf, sizeof(buf), "%d/%d", curS, maxS), buf));
    lv_obj_set_style_text_color(s->lblSta, lv_color_hex(stamina_color(curS, maxS)), 0);
}
void snake_screen_set_stamina2(SnakeScreen* s, int maxS, int curS1, int curS2)
{
    if (!s) return;
    char buf[32];
    lv_label_set_text(s->lblSta1, (snprintf(buf, sizeof(buf), "%d/%d", curS1, maxS), buf));
    lv_label_set_text(s->lblSta2, (snprintf(buf, sizeof(buf), "%d/%d", curS2, maxS), buf));
    lv_obj_set_style_text_color(s->lblSta1, lv_color_hex(stamina_color(curS1, maxS)), 0);
    lv_obj_set_style_text_color(s->lblSta2, lv_color_hex(stamina_color(curS2, maxS)), 0);
}
void snake_screen_set_len(SnakeScreen* s, int l1, int l2)
{
    if (!s) return;
    char buf[32];
    lv_label_set_text(s->lblLen1, (snprintf(buf, sizeof(buf), "LEN:%d", l1), buf));
    lv_label_set_text(s->lblLen2, (snprintf(buf, sizeof(buf), "LEN:%d", l2), buf));
}

void snake_screen_menu_show(SnakeScreen* s, int show)
{
    if (!s) return;
    if (show) lv_obj_clear_flag(s->menu, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(s->menu, LV_OBJ_FLAG_HIDDEN);
}
void snake_screen_menu_render(SnakeScreen* s, int selected, int hasSave)
{
    if (!s) return;
    const char* items[4] = { "单人模式", "双人模式", "继续游戏", "配置" };
    for (int i = 0; i < 4; i++) {
        int disabled = (i == 2 && !hasSave);
        int sel = (i == selected);
        int color = disabled ? 0x666666 : (sel ? 0xFFFFFF : 0xAAAAAA);
        lv_obj_set_style_text_color(s->lblItems[i], lv_color_hex(color), 0);
    }
    (void)items;
}
void snake_screen_config_show(SnakeScreen* s, int show)
{
    if (!s) return;
    if (show) lv_obj_clear_flag(s->config, LV_OBJ_FLAG_HIDDEN);
    else     lv_obj_add_flag(s->config, LV_OBJ_FLAG_HIDDEN);
}
// 配置块渲染：项目名固定在 lblCfg[i]（不随数值变化），当前草稿值写入 lblCfgVal[i]。
// 选中行用"青色项目名 + 黄色数值"突出，其余灰色。124px 宽放得下，无需再缩字号。
// 时间复杂度 O(1)，空间复杂度 O(1)
void snake_screen_config_render(SnakeScreen* s, int selRow,
                                int draftCell, int draftStart, int draftBoost)
{
    if (!s) return;
    const char* cellLabels[3] = { "19+1小", "29+1中", "49+1大" };
    char b0[32], b1[32], b2[32];
    snprintf(b0, sizeof(b0), "%s", cellLabels[draftCell]);
    snprintf(b1, sizeof(b1), "%d", START_SPEEDS[draftStart]);
    snprintf(b2, sizeof(b2), "%d", BOOST_SPEEDS[draftBoost]);
    lv_label_set_text(s->lblCfgVal[0], b0);
    lv_label_set_text(s->lblCfgVal[1], b1);
    lv_label_set_text(s->lblCfgVal[2], b2);
    for (int i = 0; i < 3; i++) {
        int sel = (i == selRow);
        lv_obj_set_style_text_color(s->lblCfg[i],
            lv_color_hex(sel ? g_cellColor[CELL_SNAKE_BODY] : 0xAAAAAA), 0);
        lv_obj_set_style_text_color(s->lblCfgVal[i],
            lv_color_hex(sel ? 0xFFFF00 : 0xFFFFFF), 0);
    }
}
void snake_screen_overlay_show(SnakeScreen* s, int show, const char* text)
{
    if (!s) return;
    if (show) {
        lv_obj_clear_flag(s->overlay, LV_OBJ_FLAG_HIDDEN);
        if (text) lv_label_set_text(s->lblOverlay, text);
    } else {
        lv_obj_add_flag(s->overlay, LV_OBJ_FLAG_HIDDEN);
    }
}
