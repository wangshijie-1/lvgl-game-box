/**
 * @file snake.h
 * @brief 贪吃蛇(LVGL 版) 对外结构与接口声明
 * @details 本文件是 D:\Share\2-project_GreedySnake 的 LVGL 移植版本，
 *          逻辑与设计并构严格保持与原始 C++ 工程一致：
 *          - 核心：Position / Direction / CellType / GameSpec / Board / Food / Snake
 *          - 模式：MenuMode / SingleMode / TwoPlayerMode（双线程）
 *          - 引擎：GameEngine 状态机 + 节拍驱动
 *          仅把"底层帧缓冲渲染"替换为 LVGL 对象渲染（单元网格 + 侧栏标签 + 遮罩），
 *          把"终端/evdev 键盘"替换为触控按钮（仍映射为同一套 Function 枚举）。
 */

#ifndef __SNAKE_H__
#define __SNAKE_H__

#include "lvgl/lvgl.h"
#include "0ui.h"
#include <stdint.h>

/* ===================== World_Unit ===================== */
typedef struct { int x; int y; } Position;

// 方向（值沿用原始枚举的异或配对关系：UP=0,DOWN=1 互为相反；LEFT=2,RIGHT=3 互为相反）
typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_NONE } Direction;

// 细胞类型（下标与颜色表 g_cellColor 一一对应）
typedef enum {
    CELL_EMPTY = 0,
    CELL_SNAKE_BODY,
    CELL_SNAKE_HEAD,
    CELL_FOOD,
    CELL_WALL,
    CELL_WU
} CellType;

extern int g_cellColor[CELL_WU];

/* GameSpec：当前生效配置（与原始 GameSpec 一一对应，集中管理布局/速度） */
typedef struct {
    int cellSize;   // 格子像素大小
    int gap;        // 格子间隙（固定 1）
    int startSpeed; // 起步速度
    int boostSpeed; // 加速速度
    int gridW;      // 棋盘列数（依据真实屏幕计算）
    int gridH;      // 棋盘行数
} GameSpec;
extern GameSpec g_spec;
extern int g_screenW;   // 真实屏幕宽（创建蛇界面时测得，替代原始 Screen::getWidth）
extern int g_screenH;   // 真实屏幕高（创建蛇界面时测得，替代原始 Screen::getHeight）

/* 配置档位（与原始一致）：格子大小 / 起步速度 / 加速速度 各三档 */
extern const int CELL_SIZES[3];
extern const int START_SPEEDS[3];
extern const int BOOST_SPEEDS[3];
extern int g_cellSizeIdx;
extern int g_startSpeedIdx;
extern int g_boostSpeedIdx;

// 常量：tickInterval = GAME_SPEED / snakeSpeed（与原始一致）
#define SNAKE_GAME_SPEED 7000
#define SNAKE_PANEL_W     124   // 右侧"计分 + 按键"栏宽度（从棋盘可用宽度中扣除）
// LVGL 移植：所有触控按键放进右侧栏，底部不再预留控制条高度。
// 置 0 后棋盘直接占满整屏高度，尺寸与原始 C++ 版完全一致（gridH = screenH / cellStep）。
#define SNAKE_CTRL_H      0
#define SNAKE_BOARD_X0    0
#define SNAKE_BOARD_Y0    0

// 依据真实屏幕尺寸重算 gridW / gridH（与原始 getGridW/getGridH 一致）
void spec_recalc(int screenW, int screenH);

// 单调递增毫秒时钟（CLOCK_MONOTONIC）：游戏节拍与食物计时统一用它。
// 不用 lv_tick_get()：本工程里它由 fbdev 驱动的 tick_get_cb 提供，底层是 gettimeofday
// 墙钟（不单调、可被 NTP 调整），而且只在 lv_timer_handler 里被读到，
// 等于把游戏节拍挂在了 LVGL 主循环上。
uint32_t snake_now_ms(void);

/* ===================== rand_helper ===================== */
// [min,max] 闭区间整数随机；min>max 时交换，保证总有合法值（与原始 rand_int 一致）
int rand_int(int min, int max);

/* ===================== Board ===================== */
typedef struct {
    int w, h;
    CellType* grid; // h*w 一维数组
} Board;

void board_init(Board* b, int w, int h);
void board_free(Board* b);
int  board_valid(const Board* b, const Position* p);
CellType board_get(const Board* b, const Position* p);
void board_set(Board* b, const Position* p, CellType t);
void board_clear(Board* b);
// 重建四周墙壁
void board_build_walls(Board* b);

/* ===================== Food ===================== */
typedef struct {
    Position pos;
    int      exist;
    int      color;
} Food;

void food_init(Food* f, int color);
// 更新食物位置到 [1, w-2]x[1, h-2] 界内（避开墙，与原始 Food::update 一致）
void food_update(Food* f, int w, int h);
// 在棋盘空格生成食物（带 1000 次随机 + 顺序兜底，与原始 spawnFood 一致）
void food_spawn(Food* f, Board* b);

/* ===================== Snake ===================== */
typedef struct {
    Position* body;  // 动态数组，[0]=头，[len-1]=尾
    int       cap;   // 容量
    int       len;   // 当前长度
    Direction curDir;
    Direction nextDir;
    int       growing;
    int       dead;
    Position  newHead;  // 当前/上次移动后的新头（首帧前为真实头）
    Position  oldTail;  // 当前/上次移动后的旧尾
    int       speed;
    int       stamina;
    int       canBoost; // 体力耗尽后禁止加速，恢复>10点重新允许
    int       headColor;
    int       bodyColor;
} Snake;

#define SNAKE_MAX_STAMINA 100

void snake_init_empty(Snake* s);
void snake_free(Snake* s);
// 在指定位置/朝向/长度初始化蛇身（双人模式对角出生用，纯逻辑、不碰 Board）
void snake_init_at(Snake* s, Position head, Direction dir, int len);
// 开局/重置：用头尾两点构造（单人 initGame 用）
void snake_reset(Snake* s, Position head, Position tail);
void snake_move(Snake* s);
void snake_change_dir(Snake* s, Direction d);
void snake_set_dir(Snake* s, Direction d);
void snake_set_speed(Snake* s, int sp);
void snake_toggle_speed(Snake* s);
void snake_grow(Snake* s);
void snake_setdead(Snake* s);
int  snake_is_growing(const Snake* s);
int  snake_is_dead(const Snake* s);
Position snake_new_head(const Snake* s);
Position snake_old_tail(const Snake* s);
int  snake_len(const Snake* s);
int  snake_speed(const Snake* s);
int  snake_stamina(const Snake* s);
int  snake_is_boosting(const Snake* s);
int  snake_can_boost(const Snake* s);
int  snake_is_body(const Snake* s, const Position* p); // 跳过头部
const Position* snake_body_at(const Snake* s, int i);
int  snake_body_count(const Snake* s);
void snake_set_colors(Snake* s, int head, int body);
int  snake_head_color(const Snake* s);
int  snake_body_color(const Snake* s);
// 从存档恢复（参数 bodyArr 为头在前的数组）
void snake_restore(Snake* s, const Position* bodyArr, int bodyLen,
                   Direction dir, int growing, int dead,
                   Position newHead, Position oldTail,
                   int speed, int stamina);

/* ===================== SnakeScreen（LVGL 渲染后端） =====================
 * 替代原始 Screen(帧缓冲) + Font(点阵字模) + ScreenRender(像素绘制)。
 * 棋盘用单元矩形网格（cell），侧栏/菜单/遮罩用 LVGL 标签与容器对象。 */
typedef struct {
    lv_obj_t* cont;          // 棋盘容器
    lv_obj_t** cells;        // h*w 单元矩形对象（一维）
    int w, h;                // 格子列/行
    int cellSize, cellStep;

    lv_obj_t* panel;         // 右侧栏容器
    lv_obj_t* lblScore;      // 当前分（=蛇长）
    lv_obj_t* lblHigh;       // 最高分
    lv_obj_t* lblStaTitle;   // 体力标签 "STA"
    lv_obj_t* lblSta;        // 体力 "当前/上限"

    lv_obj_t* lblP1;         // 双人 P1 标签
    lv_obj_t* lblLen1;       // P1 长度
    lv_obj_t* lblSta1;       // P1 体力
    lv_obj_t* lblP2;         // 双人 P2 标签
    lv_obj_t* lblLen2;       // P2 长度
    lv_obj_t* lblSta2;       // P2 体力

    lv_obj_t* menu;          // 菜单容器
    lv_obj_t* lblTitle;      // "贪吃蛇" 标题
    lv_obj_t* lblItems[4];   // 四个菜单项
    lv_obj_t* config;        // 配置信息块（位于右侧栏内，与分数块互斥显示）
    lv_obj_t* lblCfg[3];     // 三行配置：项目名（格子大小/起步速度/加速速度）
    lv_obj_t* lblCfgVal[3];  // 三行配置：当前草稿值

    lv_obj_t* overlay;       // 暂停/结算/准备 遮罩
    lv_obj_t* lblOverlay;    // 遮罩文字
} SnakeScreen;

// 在 parent 屏幕下创建全部 LVGL 对象；依据当前 g_spec 建立单元网格
SnakeScreen* snake_screen_create(lv_obj_t* parent);
void snake_screen_free(SnakeScreen* s);
// 依据当前 g_spec 重建单元网格（格子大小/屏幕尺寸变化时调用）
void snake_screen_build_cells(SnakeScreen* s);

// 设置某个格子颜色（替代原始 draw_retangle 在棋盘上的像素写入）
void snake_screen_set_cell(SnakeScreen* s, int gx, int gy, int color);
// 整盘置色（清屏/重绘棋盘用）
void snake_screen_clear_board(SnakeScreen* s, int color);

// 侧栏：分数 / 最高分
void snake_screen_set_score(SnakeScreen* s, int score, int high);
// 侧栏：体力（单人复用 lblSta；双人复用 lblSta1/lblSta2）
void snake_screen_set_stamina(SnakeScreen* s, int maxS, int curS);
void snake_screen_set_stamina2(SnakeScreen* s, int maxS, int curS1, int curS2);
// 双人侧栏：长度
void snake_screen_set_len(SnakeScreen* s, int l1, int l2);

// 菜单项高亮/置灰刷新（替代原始像素绘制菜单）
void snake_screen_menu_show(SnakeScreen* s, int show);
void snake_screen_menu_render(SnakeScreen* s, int selected, int hasSave);
void snake_screen_config_show(SnakeScreen* s, int show);
void snake_screen_config_render(SnakeScreen* s, int selRow,
                                int draftCell, int draftStart, int draftBoost);
// 遮罩
void snake_screen_overlay_show(SnakeScreen* s, int show, const char* text);

/* ===================== Function（输入抽象，与原始 KeyInput 的 Function 对应） ===================== */
typedef enum {
    FN_MENU, FN_SINGLE, FN_TWO, FN_GAMEOVER, FN_PAUSE, FN_RESUME,
    FN_RESTART, FN_QUIT,
    FN_UP, FN_DOWN, FN_LEFT, FN_RIGHT,
    FN_UP2, FN_DOWN2, FN_LEFT2, FN_RIGHT2,
    FN_CONFIRM, FN_BACK, FN_BOOST1, FN_BOOST2,
    FN_NONE
} Function;

/* ===================== 模式前向声明（仅前向，具体定义见 snake_app.c） ===================== */
typedef enum { MI_SINGLE, MI_TWO, MI_CONTINUE, MI_CONFIG, MI_COUNT } MenuItem;

struct MenuMode;
struct SingleMode;
struct TwoPlayerMode;

/* ===================== GameEngine ===================== */
typedef enum { ST_MENU, ST_SINGLE, ST_TWO, ST_PAUSED, ST_OVER } GameState;

typedef struct {
    GameState state;
    GameState prev;
    SnakeScreen* ui;
    struct MenuMode*  menu;
    struct SingleMode* single;
    struct TwoPlayerMode* two;
    int        gameOver;     // 全局退出标志（本移植不使用，保留结构）
    int        tickInterval; // = GAME_SPEED / speed
    uint32_t   lastTick;     // 节拍计时（snake_now_ms 毫秒，单调时钟）
} Engine;

// 贪吃蛇游戏屏幕对象（供游戏盒子 layout.c 切换）
extern lv_obj_t* src_snake;

// 引擎实例（定义在 snake_app.c）：触控回调与键盘输入模块都往它分发按键
extern Engine g_engine;

// 创建并初始化贪吃蛇界面（由 layout.c 的 creat_my_ui 调用）
void creat_my_snake(void);

// 初始化键盘输入（终端 ANSI 转义 + evdev 自动检索），由 creat_my_snake 内部调用一次
void snake_input_init(void);

// 引擎：处理一个按键（触控按钮 / 键盘共用，等价于原始 processInput 对单键的分发）
void engine_handle_key(Engine* e, Function key);

#endif
