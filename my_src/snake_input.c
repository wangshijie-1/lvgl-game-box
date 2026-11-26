/**
 * @file snake_input.c
 * @brief 贪吃蛇键盘输入（LVGL 移植版）：键盘按键 → Function → 交给引擎
 * @details 对应原始 core/KeyInput.cpp，两条输入路径都保留（板子上两种用法都可能出现）：
 *
 *          1) 终端 ANSI 转义：串口 / ssh / 本地终端里直接按键
 *             （WASD、方向键、回车、ESC、R/P/M/O/Q/1/2、/）。
 *             做法与 KeyInput::pollKeys 一致：stdin 置 raw + 非阻塞读。
 *             不做这一步的话，方向键会被终端自己回显成 " ^[[A " 这种转义串，
 *             程序永远收不到（现象就是"按了没反应"）。
 *
 *          2) evdev 自动检索：板子接 USB / GPIO 键盘时，扫描 /dev/input/event*，
 *             挑出具备字母键能力（EV_KEY 且支持 KEY_A）的设备读 input_event，
 *             与 KeyInput::pollKeysEvdevAuto 一致，支持设备热插拔后自动重连。
 *
 *          两条路径都只处理"按下"事件，统一翻译成 Function 后调用 engine_handle_key()。
 *          用 LVGL 定时器轮询而不是另开线程：引擎节拍本来就在 lv_timer 里跑，
 *          单线程分发天然不存在与引擎状态竞争的问题。
 */

#include "snake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define SNAKE_KBD_POLL_MS    20     // 轮询周期：人手按键间隔通常 >50ms，20ms 足够不漏键
#define SNAKE_KBD_RESCAN_MS  1000   // 没找到键盘时的重扫间隔，避免每 20ms 就遍历一次 /dev/input
#define SNAKE_ESC_TIMEOUT_MS 50     // ESC 序列续传超时：超时即认定为"用户单独按了 ESC"

/* ===================== 全局状态 ===================== */
static int      g_evFd     = -1;    // 键盘 evdev 句柄，-1 表示当前没有可用键盘
static uint32_t g_nextScan = 0;     // 下一次允许重扫设备的时间戳（限流用）
static int      g_escStep  = 0;     // ANSI 转义解析状态：0 空闲 / 1 已收 ESC / 2 已收 "ESC ["
static uint32_t g_escMs    = 0;     // 进入转义状态的时间戳，用于超时判定（见 SNAKE_ESC_TIMEOUT_MS）
static struct termios g_oldTermios; // 终端原始配置，退出时还原
static int      g_termRaw  = 0;     // stdin 是否已切到 raw 模式

/* ===================== 终端 raw 模式 ===================== */
// 还原终端设置。程序被 Ctrl+C 杀掉后如果还是 raw 模式，串口终端会没有回显、
// 回车也不换行，用户得手动敲 reset 才能恢复，所以退出/信号两条路都要还原。
static void term_restore(void)
{
    if (g_termRaw) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_oldTermios);
        g_termRaw = 0;
    }
}

// 收到退出信号：先还原终端，再按默认行为退出（保证退出码/信号语义不变）
static void term_signal_cb(int sig)
{
    term_restore();
    signal(sig, SIG_DFL);
    raise(sig);
}

// 把 stdin 切成 raw + 非阻塞。
// 这里【必须】两步一起做，缺一不可：
//   1) termios 关掉行缓冲(ICANON)与回显(ECHO)、VMIN/VTIME=0 —— 这是终端语义上的非阻塞，
//      但只对"真的是终端"的 stdin 有效；
//   2) fcntl 加 O_NONBLOCK —— 这是 fd 层面的非阻塞，stdin 是管道/重定向/不是 tty
//      （tcgetattr 会失败）时也保证 read() 不阻塞。
// 只做第 1 步的话，一旦 stdin 不是终端，read() 会一直挂着，把 LVGL 主循环连同引擎节拍
// 全卡死 → 表现就是移动忽快忽慢（卡一下、补一下）。凡是定时器回调里的 IO 都不能阻塞。
// 时间复杂度 O(1)，空间复杂度 O(1)
static void term_raw_enter(void)
{
    if (g_termRaw) return;

    // 先把 fd 设成非阻塞（与是否为终端无关，失败也无所谓，只是少一层保险）
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (fl >= 0) fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

    // 不是终端（被重定向 / 管道 / 无控制台）时 tcgetattr 失败，终端语义这颗跳过即可
    if (tcgetattr(STDIN_FILENO, &g_oldTermios) != 0) return;

    struct termios raw = g_oldTermios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) return;

    g_termRaw = 1;
    atexit(term_restore);
    signal(SIGINT, term_signal_cb);
    signal(SIGTERM, term_signal_cb);
}

/* ===================== 按键映射（与原始 KeyInput 完全一致） ===================== */
// evdev 键码 → Function
static Function map_evdev(int code)
{
    switch (code) {
    case KEY_W: return FN_UP;        case KEY_S: return FN_DOWN;
    case KEY_A: return FN_LEFT;      case KEY_D: return FN_RIGHT;
    case KEY_UP:    return FN_UP2;   case KEY_DOWN:  return FN_DOWN2;
    case KEY_LEFT:  return FN_LEFT2; case KEY_RIGHT: return FN_RIGHT2;
    case KEY_ENTER: return FN_CONFIRM;
    case KEY_ESC:   return FN_BACK;
    case KEY_M: return FN_MENU;      case KEY_P: return FN_PAUSE;
    case KEY_R: return FN_BOOST1;    case KEY_SLASH: return FN_BOOST2;
    case KEY_O: return FN_RESUME;    case KEY_Q: return FN_QUIT;
    case KEY_1: return FN_SINGLE;    case KEY_2: return FN_TWO;
    default: return FN_NONE;
    }
}

// 终端普通字符 → Function
static Function map_char(char c)
{
    switch (c) {
    case 'w': case 'W': return FN_UP;
    case 's': case 'S': return FN_DOWN;
    case 'a': case 'A': return FN_LEFT;
    case 'd': case 'D': return FN_RIGHT;
    case 'm': case 'M': return FN_MENU;
    case 'p': case 'P': return FN_PAUSE;
    case 'o': case 'O': return FN_RESUME;
    case 'r': case 'R': return FN_BOOST1;
    case 'q': case 'Q': return FN_QUIT;
    case '1': return FN_SINGLE;
    case '2': return FN_TWO;
    case '/': return FN_BOOST2;
    case '\r': case '\n': return FN_CONFIRM;
    default: return FN_NONE;
    }
}

/* ===================== 统一分发 ===================== */
// 只有贪吃蛇界面处于活动状态时才分发：否则在登录页/游戏盒子里敲键盘，
// 会莫名其妙地操作到后台的蛇菜单（引擎状态是全局的）。
// 时间复杂度 O(1)，空间复杂度 O(1)
static void dispatch_key(Function fn)
{
    if (fn == FN_NONE) return;
    if (src_snake == NULL) return;
    if (lv_screen_active() != src_snake) return;
    engine_handle_key(&g_engine, fn);
}

/* ===================== 路径 1：终端 ANSI 转义 ===================== */
// 解析 stdin：
//   普通字符  → map_char
//   ESC ESC[A/B/C/D → 上/下/右/左（P2 方向）
//   裸 ESC    → FN_BACK
// 方向键是 3 个字节，可能被拆到两次 read 里，所以用 g_escStep 状态机跨轮询接续，
// 而不是像原始版那样在同一帧里连续 read 三次（串口下会偶发误判成裸 ESC）。
// 时间复杂度 O(n)，n 为本次可读字节数；空间复杂度 O(1)
static void poll_stdin(void)
{
    // 超时兜底：方向键三字节理论上紧接着到达（串口也就几百微秒），
    // 超过 SNAKE_ESC_TIMEOUT_MS 还没续上，判定为"用户单独按了 ESC"。
    // 没有这一步的话，单独按 ESC 会一直停在等待状态，下一次按键会多带一个返回动作。
    if (g_escStep != 0 && snake_now_ms() - g_escMs > SNAKE_ESC_TIMEOUT_MS) {
        if (g_escStep == 1) dispatch_key(FN_BACK);   // 裸 ESC = 返回/取消
        g_escStep = 0;                               // 序列残缺（ESC [ 后没后续）则直接丢弃
    }

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (g_escStep == 1) {                  // 上一步收到 ESC，等 '['
            if (c == '[') { g_escStep = 2; g_escMs = snake_now_ms(); continue; }
            g_escStep = 0;
            dispatch_key(FN_BACK);             // 裸 ESC = 返回/取消
            // 落空不 continue：这个字符本身还要按普通键继续处理
        }
        if (g_escStep == 2) {                  // 上一步收到 "ESC ["，等方向字母
            g_escStep = 0;
            dispatch_key(c == 'A' ? FN_UP2 :
                         c == 'B' ? FN_DOWN2 :
                         c == 'C' ? FN_RIGHT2 :
                         c == 'D' ? FN_LEFT2 : FN_NONE);
            continue;
        }
        if (c == 0x1B) { g_escStep = 1; g_escMs = snake_now_ms(); continue; }
        dispatch_key(map_char(c));
    }
}

/* ===================== 路径 2：evdev 键盘 ===================== */
static void evdev_close(void)
{
    if (g_evFd >= 0) { close(g_evFd); g_evFd = -1; }
}

// 扫描 /dev/input/event*，返回第一个具备字母键能力（EV_KEY 且支持 KEY_A）的设备句柄。
// 判定条件与原始 findKeyboardDevice 一致：能过滤掉触摸屏 / 按键以外的输入设备。
// 失败返回 -1。时间复杂度 O(设备数)，空间复杂度 O(1)
static int evdev_find(void)
{
    DIR* dir = opendir("/dev/input");
    if (dir == NULL) return -1;

    struct dirent* entry;
    int fd = -1;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        char path[256];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        unsigned long key_bit[KEY_MAX / 8 + 1];
        memset(key_bit, 0, sizeof(key_bit));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bit)), key_bit) >= 0 &&
            (key_bit[KEY_A / 8] & (1UL << (KEY_A % 8)))) {
            break;                             // 含字母键 → 认定是键盘
        }
        close(fd);
        fd = -1;
    }
    closedir(dir);
    return fd;
}

// 读取并分发 evdev 事件；设备被拔出（read 报错）时关闭句柄，下次轮询重扫。
// 只处理 EV_KEY 且 value==1（按下），忽略松开与长按重复。
// 时间复杂度 O(n)，n 为本次可读事件数；空间复杂度 O(1)
static void poll_evdev(void)
{
    if (g_evFd < 0) {
        uint32_t now = snake_now_ms();
        if (now < g_nextScan) return;
        g_nextScan = now + SNAKE_KBD_RESCAN_MS;
        g_evFd = evdev_find();
        if (g_evFd < 0) return;
        printf("[键盘] 已连接 evdev 输入设备：WASD / 方向键 / 回车 / ESC / R / P / M 可用\n");
    }

    struct input_event ev;
    ssize_t n;
    while ((n = read(g_evFd, &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
        if (ev.type != EV_KEY || ev.value != 1) continue;
        dispatch_key(map_evdev((int)ev.code));
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        printf("[键盘] evdev 设备断开，等待重连\n");
        evdev_close();
    }
}

/* ===================== 对外入口 ===================== */
static void kbd_poll_cb(lv_timer_t* t)
{
    (void)t;
    poll_evdev();
    poll_stdin();
}

// 初始化键盘输入：切终端 raw + 建轮询定时器（由 creat_my_snake 调用一次）
// 时间复杂度 O(1)，空间复杂度 O(1)
void snake_input_init(void)
{
    term_raw_enter();
    lv_timer_create(kbd_poll_cb, SNAKE_KBD_POLL_MS, NULL);
}
