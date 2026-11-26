/**
 * @file 0ui.c
 * @brief UI 通用组件实现
 * @details 提供颜色表、按钮样式池、按钮创建、文本域创建等通用 UI 功能
 */

#include <stdio.h>      // 标准输入输出
#include <stdint.h>     // 整数类型定义
#include <time.h>       // 时钟用时间函数
#include "0ui.h"        // UI 通用组件头文件
#include "lvgl/lvgl.h"  // LVGL 核心头文件

// 颜色值表，下标与 RgbNum 枚举对应，单位为 24 位十六进制颜色
int RGB[wu + 1] = {
    0x8FA3B7,  // 雾霾蓝
    0xC4A4A4,  // 灰玫瑰
    0xD4C4B0,  // 燕麦色
    0xA8B5A0,  // 灰豆绿
    0x9B9B9B,  // 烟灰色
    0xC9B8C8,  // 藕荷色
    0x9CAF88,  // 鼠尾草
    0xE8D5D0,  // 贝壳粉
    0xCB1B45,  // 绯红
    0xE60033,  // 赤丹
    0xF8B500,  // 山吹
    0x5DAC81,  // 若竹
    0x1E88A8,  // 琉璃
    0x8B7CB3,  // 藤紫
    0x2B2B2B,  // 墨色
    0xF5F5DC,  // 生成色
    0xF7F9FA,  // 冰川白
    0xB8C4CE,  // 雾灰
    0xD4C5B9,  // 砂岩
    0x7A8471,  // 苔藓绿
    0x2E4A62,  // 深海蓝
    0x8B6914,  // 焦糖棕
    0xF4A261,  // 珊瑚橙
    0xE6E6FA,  // 薰衣草
    0xFF006E,  // 霓虹粉
    0x00F5FF,  // 电光蓝
    0xB026FF,  // 激光紫
    0x39FF14,  // 荧光绿
    0xFF4500,  // 熔岩橙
    0x0A0A0F,  // 深空黑
    0x00D4AA,  // 全息青
    0xFFFF00,  // 脉冲黄
    0xC65D3B,  // 赤陶
    0xCC7722,  // 赭石
    0x6B8E23,  // 橄榄绿
    0x8B4513,  // 泥土棕
    0x708090,  // 石板灰
    0xC2B280,  // 沙漠金
    0x228B22,  // 森林绿
    0x483D8B,  // 暮光紫
    0xF0F0F0,  // 钛白
    0xC0C0C0,  // 银灰
    0x474747,  // 石墨
    0x1C1C1C,  // 炭黑
    0xE8E8E8,  // 珍珠灰
    0x666666,  // 铁灰
    0x7F7F7F,  // 铅灰
    0xDCDCDC,  // 霜灰
    0x0ABAB5,  // 蒂芙尼蓝
    0xFF6600,  // 爱马仕橙
    0x002FA7,  // 克莱因蓝
    0x800020,  // 勃艮第红
    0x1A1A1A,  // 香奈儿黑
    0xD4AF37,  // 卡地亚金
    0xE4002B,  // 潘通红
    0x006039,  // 劳力士绿
    0xFF7E5F,  // 日落橙
    0xFEB47B,  // 蜜桃粉
    0x2193B0,  // 深海渐变
    0x6DD5FA,  // 极光紫
    0xA8E6CF,  // 薄荷清新
    0xB76E79,  // 玫瑰金
    0x191970,  // 午夜蓝
    0xFFB7C5   // 樱花粉
};

// 字体缓存表，最多缓存 FONT_CACHE_SIZE 种不同字号
#define FONT_CACHE_SIZE 8

typedef struct {
    int         size;   // 字号
    lv_font_t*  font;   // 对应字号的中文字体
} font_cache_t;

static font_cache_t font_cache[FONT_CACHE_SIZE];  // 字体缓存数组
static int font_cache_cnt = 0;                    // 已缓存的字体数量

/**
 * @brief 按需获取指定字号的中文字体
 * @param size 字号
 * @return 对应字号的字体指针；加载失败或缓存满时返回 NULL
 * @details 同一字号只加载一次并缓存，避免重复加载 TTF 导致内存耗尽
 */
lv_font_t* ui_font_get(int size) {
    if (size <= 0) {
        printf("[错误] ui_font_get: 字号非法 size=%d\n", size);
        return NULL;
    }

    // 先查缓存，命中直接返回
    for (int i = 0; i < font_cache_cnt; i++) {
        if (font_cache[i].size == size) {
            return font_cache[i].font;
        }
    }

    // 缓存已满，无法再加载新字号
    if (font_cache_cnt >= FONT_CACHE_SIZE) {
        printf("[错误] ui_font_get: 字体缓存已满(容量 %d)，无法加载字号 %d\n", FONT_CACHE_SIZE, size);
        return NULL;
    }

    // 按需加载该字号的中文字体
    lv_font_t* f = lv_tiny_ttf_create_file(FONT_FILE_PATH, size);
    if (f == NULL) {
        printf("[错误] ui_font_get: 加载字体 %s 字号 %d 失败\n", FONT_FILE_PATH, size);
        return NULL;
    }

    font_cache[font_cache_cnt].size = size;
    font_cache[font_cache_cnt].font = f;
    font_cache_cnt++;
    return f;
}

// 按钮样式池，最多同时存在 STYLE_BTN_SIZE 个不同样式
style_btn Btn[STYLE_BTN_SIZE];

/**
 * @brief 初始化按钮样式池
 * @details 将所有样式槽的 inited 标记置 0，表示尚未使用
 */
void style_btn_init0(void) {
    for (int i = 0; i < STYLE_BTN_SIZE; i++) {
        Btn[i].inited = 0;
    }
}

/**
 * @brief 查找或创建按钮样式
 * @param num 期望使用的起始样式槽索引
 * @param bg_color 按钮背景颜色（十六进制）
 * @param text_color 按钮文字颜色（十六进制）
 * @param font_size 按钮文字字号
 * @return 实际分配到的样式槽索引；参数非法或池满时返回 -1
 * @details 若已有完全相同的背景色、文字色和字号样式，则直接复用，否则在空闲槽新建
 */
int style_btn_init(int num, int bg_color, int text_color, int font_size) {
    if (num < 0 || num >= STYLE_BTN_SIZE) {
        printf("[错误] style_btn_init: 样式槽索引 num=%d 越界(合法范围 0~%d)\n", num, STYLE_BTN_SIZE - 1);
        return -1;
    }


    // 先遍历查找已存在的相同样式
    for (int j = 0; j < STYLE_BTN_SIZE; j++) {
        if (Btn[j].inited) {
            if (Btn[j].u32btn == bg_color &&
                Btn[j].u32lab == text_color &&
                Btn[j].font_size == font_size) {
                return j;
            }
        }
    }

    // 从 num 开始向后查找第一个空闲样式槽
    int i = num;
    while (i < STYLE_BTN_SIZE && Btn[i].inited) i++;
    if (i >= STYLE_BTN_SIZE) {
        printf("[错误] style_btn_init: 按钮样式池已满，无法创建新样式(容量 %d)\n", STYLE_BTN_SIZE);
        return -1;
    }

    // 初始化按钮背景样式
    lv_style_init(&Btn[i].btn);
    lv_style_set_bg_color(&Btn[i].btn, lv_color_hex(bg_color));
    Btn[i].u32btn = bg_color;

    // 初始化按钮文字样式
    lv_style_init(&Btn[i].lab);
    lv_style_set_text_color(&Btn[i].lab, lv_color_hex(text_color));
    lv_style_set_text_align(&Btn[i].lab, LV_TEXT_ALIGN_CENTER);
    // 按字号加载中文字体
    lv_font_t* font = ui_font_get(font_size);
    if (font != NULL) {
        lv_style_set_text_font(&Btn[i].lab, font);
    }
    Btn[i].u32lab = text_color;
    Btn[i].font_size = font_size;

    // 标记该槽位已初始化
    Btn[i].inited = 1;
    return i;
}


/**
 * @brief 创建一个带文字的按钮
 * @param style_num 样式池中的样式索引
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param w 按钮宽度
 * @param h 按钮高度
 * @param text 按钮文字内容
 * @param cb 按钮事件回调函数
 * @param user_data 传给回调函数的用户数据
 * @return 创建的按钮对象指针；参数非法时返回 NULL
 */
lv_obj_t* create_button(int style_num, lv_obj_t* parent,
    lv_align_t align, int x, int y, int w, int h,
    const char* text, lv_event_cb_t cb, void* user_data)
{
    // 校验样式索引是否合法且已初始化
    if (style_num < 0 || style_num >= STYLE_BTN_SIZE) {
        printf("[错误] create_button: 样式索引 style_num=%d 越界(合法范围 0~%d)\n", style_num, STYLE_BTN_SIZE - 1);
        return NULL;
    }
    if (!Btn[style_num].inited) {
        printf("[错误] create_button: 样式槽 %d 尚未初始化，请先调用 style_btn_init\n", style_num);
        return NULL;
    }

    // 创建按钮并设置位置、大小和背景样式
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_align(btn, align, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &Btn[style_num].btn, LV_STATE_DEFAULT);

    // 创建标签并设置文字内容与样式
    lv_obj_t* lab = lv_label_create(btn);
    lv_obj_set_align(lab, LV_ALIGN_CENTER);
    lv_obj_add_style(lab, &Btn[style_num].lab, LV_STATE_DEFAULT);
    lv_label_set_text(lab, text);

    // 注册按下、释放、点击三种事件回调
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED, user_data);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_RELEASED, user_data);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    // 所有按钮按下时背景不透明度统一变为 50%
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_PRESSED);

    return btn;
}

/**
 * @brief 文本域事件回调
 * @param e LVGL 事件对象
 * @details 处理 FOCUSED（显示键盘并绑定文本域）、DEFOCUSED（隐藏键盘）、
 *          READY（点击确认键，隐藏键盘并移除焦点）三种事件
 */
void textarea_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* ta = lv_event_get_target(e);
    lv_obj_t* kb = lv_event_get_user_data(e);

    // printf("event: %d\n", code);  // 调试用

    if (code == LV_EVENT_FOCUSED) {
        // printf("focused, kb=%p\n", kb);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_DEFOCUSED) {
        // printf("defocused\n");
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY) {
        // 点击键盘确认键后隐藏键盘并移除文本域焦点
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

/**
 * @brief 创建文本输入域
 * @param parent 父对象
 * @param kb 关联的键盘对象
 * @param x0 左上角 x 坐标
 * @param y0 左上角 y 坐标
 * @param w 宽度
 * @param h 高度
 */
/**
 * @brief 创建一个文本输入框
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param placeholder 占位提示文字（为空时不设置）
 * @param color 文字颜色（十六进制，可用 RGB[] 取色）
 * @param font_size 文字字号
 * @param kb 关联的键盘对象（传 NULL 则不绑定键盘）
 * @param is_password 是否密码模式（1=输入显示为圆点，0=明文）
 * @return 创建的输入框对象指针；参数非法时返回 NULL
 */
lv_obj_t* create_textbox(lv_obj_t* parent, lv_align_t align, int x, int y, int w, int h,
    const char* placeholder, int color, int font_size, lv_obj_t* kb, int is_password)
{
    if (parent == NULL) {
        printf("[错误] create_textbox: 父对象为空\n");
        return NULL;
    }
    if (w <= 0 || h <= 0) {
        printf("[错误] create_textbox: 宽高非法 w=%d h=%d\n", w, h);
        return NULL;
    }

    // 创建输入框并设置位置、大小、文字样式
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_obj_align(ta, align, x, y);
    lv_obj_set_size(ta, w, h);
    lv_obj_add_flag(ta, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_text_color(ta, lv_color_hex(color), 0);
    // 按字号加载中文字体
    lv_font_t* font = ui_font_get(font_size);
    if (font != NULL) {
        lv_obj_set_style_text_font(ta, font, 0);
    }

    // 设置占位提示文字
    if (placeholder != NULL && placeholder[0] != '\0') {
        lv_textarea_set_placeholder_text(ta, placeholder);
    }

    // 密码模式时输入内容显示为圆点
    if (is_password) {
        lv_textarea_set_password_mode(ta, true);
    }

    // 绑定键盘，聚焦弹键盘、确认键收起
    if (kb != NULL) {
        lv_obj_add_event_cb(ta, textarea_cb, LV_EVENT_FOCUSED, kb);
        lv_obj_add_event_cb(ta, textarea_cb, LV_EVENT_DEFOCUSED, kb);
        lv_obj_add_event_cb(ta, textarea_cb, LV_EVENT_READY, kb);
    }

    return ta;
}

/**
 * @brief 创建一个文本标签
 * @param parent 父对象
 * @param color 文字颜色（十六进制，可用 RGB[] 取色）
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param text 文本内容
 * @param font_size 文字字号
 * @return 创建的标签对象指针；参数非法时返回 NULL
 * @details 自动应用对应字号的中文字体
 */
lv_obj_t* create_label(lv_obj_t* parent, int color, lv_align_t align,
    int x, int y, const char* text, int font_size)
{
    if (parent == NULL) {
        printf("[错误] create_label: 父对象为空\n");
        return NULL;
    }
    if (text == NULL) {
        printf("[错误] create_label: 文本内容为空\n");
        return NULL;
    }

    lv_obj_t* lab = lv_label_create(parent);
    lv_label_set_text(lab, text);
    lv_obj_align(lab, align, x, y);
    lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
    // 按字号加载中文字体
    lv_font_t* font = ui_font_get(font_size);
    if (font != NULL) {
        lv_obj_set_style_text_font(lab, font, 0);
    }
    return lab;
}

/**
 * @brief 创建一个开关控件
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param on_color 开启时的颜色（十六进制）
 * @param off_color 关闭时的颜色（十六进制）
 * @param cb 值变化回调函数（LV_EVENT_VALUE_CHANGED）
 * @param user_data 传给回调函数的用户数据
 * @return 创建的开关对象指针；参数非法时返回 NULL
 * @details 默认关闭；如需初始开启，对返回值调用 lv_obj_add_state(obj, LV_STATE_CHECKED)
 */
lv_obj_t* create_switch(lv_obj_t* parent, lv_align_t align, int x, int y,
    int w, int h, int on_color, int off_color,
    lv_event_cb_t cb, void* user_data)
{
    if (parent == NULL) {
        printf("[错误] create_switch: 父对象为空\n");
        return NULL;
    }

    lv_obj_t* sw = lv_switch_create(parent);
    lv_obj_align(sw, align, x, y);
    lv_obj_set_size(sw, w, h);

    // 关闭状态主体颜色
    lv_obj_set_style_bg_color(sw, lv_color_hex(off_color), 0);
    // 开启状态（选中）主体颜色
    lv_obj_set_style_bg_color(sw, lv_color_hex(on_color),
        LV_PART_INDICATOR | LV_STATE_CHECKED);
    // 旋钮统一为白色
    lv_obj_set_style_bg_color(sw, lv_color_hex(RGB[bingchuanbai]), LV_PART_KNOB);

    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user_data);
    return sw;
}

/**
 * @brief 创建一个滑条控件
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param min 最小值
 * @param max 最大值
 * @param value 初始值
 * @param color 已滑动部分的颜色（十六进制）
 * @param cb 值变化回调函数（LV_EVENT_VALUE_CHANGED）
 * @param user_data 传给回调函数的用户数据
 * @return 创建的滑条对象指针；参数非法时返回 NULL
 */
lv_obj_t* create_slider(lv_obj_t* parent, lv_align_t align, int x, int y,
    int w, int h, int min, int max, int value, int color,
    lv_event_cb_t cb, void* user_data)
{
    if (parent == NULL) {
        printf("[错误] create_slider: 父对象为空\n");
        return NULL;
    }
    if (min >= max) {
        printf("[错误] create_slider: 范围非法 min=%d max=%d\n", min, max);
        return NULL;
    }

    lv_obj_t* sl = lv_slider_create(parent);
    lv_obj_align(sl, align, x, y);
    lv_obj_set_size(sl, w, h);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    // 已滑过的指示条颜色
    lv_obj_set_style_bg_color(sl, lv_color_hex(color), LV_PART_INDICATOR);
    // 旋钮统一为白色
    lv_obj_set_style_bg_color(sl, lv_color_hex(RGB[bingchuanbai]), LV_PART_KNOB);

    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, user_data);
    return sl;
}

/**
 * @brief 创建一个进度条控件
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param min 最小值
 * @param max 最大值
 * @param value 初始进度值
 * @param color 进度指示条颜色（十六进制）
 * @return 创建的进度条对象指针；参数非法时返回 NULL
 * @details 运行时用 lv_bar_set_value(bar, value, LV_ANIM_ON) 更新进度
 */
lv_obj_t* create_bar(lv_obj_t* parent, lv_align_t align, int x, int y,
    int w, int h, int min, int max, int value, int color)
{
    if (parent == NULL) {
        printf("[错误] create_bar: 父对象为空\n");
        return NULL;
    }
    if (min >= max) {
        printf("[错误] create_bar: 范围非法 min=%d max=%d\n", min, max);
        return NULL;
    }

    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_align(bar, align, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, min, max);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    // 进度指示条颜色
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    return bar;
}

/**
 * @brief 创建一个图片按钮
 * @param parent 父对象
 * @param src 图片源（指向 lv_image_dsc_t 的指针）
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param cb 点击回调函数（LV_EVENT_CLICKED）
 * @param user_data 传给回调函数的用户数据
 * @return 创建的图片按钮对象指针；参数非法时返回 NULL
 */
lv_obj_t* create_image_button(lv_obj_t* parent, const void* src, lv_align_t align,
    int x, int y, int w, int h, lv_event_cb_t cb, void* user_data)
{
    if (parent == NULL) {
        printf("[错误] create_image_button: 父对象为空\n");
        return NULL;
    }
    if (src == NULL) {
        printf("[错误] create_image_button: 图片源为空\n");
        return NULL;
    }

    lv_obj_t* img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_align(img, align, x, y);
    lv_obj_set_size(img, w, h);
    // 图片默认不可点击，需显式开启
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(img, cb, LV_EVENT_CLICKED, user_data);
    return img;
}

// 确认弹窗内部结构，挂在遮罩层的 user_data 上
typedef struct {
    lv_obj_t*     ok_btn;    // 确定按钮，用于区分取消/确定
    lv_event_cb_t ok_cb;     // 点击确定后要执行的回调
    void*         ok_data;   // 传给确认回调的用户数据
} confirm_dlg;

/**
 * @brief 确认弹窗按钮内部回调
 * @details 点击确定时先执行外部回调再关闭弹窗；点击取消直接关闭
 */
static void confirm_dlg_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    // 按钮 -> 对话框容器 -> 遮罩，弹窗数据挂在遮罩上
    lv_obj_t* box = lv_obj_get_parent(lv_event_get_target(e));
    lv_obj_t* overlay = lv_obj_get_parent(box);
    confirm_dlg* dlg = (confirm_dlg*)lv_obj_get_user_data(overlay);
    if (dlg == NULL) return;

    if (lv_event_get_target(e) == dlg->ok_btn && dlg->ok_cb != NULL) {
        dlg->ok_cb(e);
    }
    lv_obj_delete(overlay);
    lv_free(dlg);
}

/**
 * @brief 创建一个确认弹窗
 * @param msg 提示文字
 * @param font_size 弹窗文字字号
 * @param ok_cb 点击确定后的回调函数（LV_EVENT_CLICKED，user_data 为 ok_data）
 * @param ok_data 传给确认回调的用户数据
 * @details 弹窗含半透明遮罩、提示文字及"取消/确定"按钮；
 *          点击确定时先执行 ok_cb 再自动关闭，点击取消直接关闭
 */
void create_confirm_dialog(const char* msg, int font_size, lv_event_cb_t ok_cb, void* ok_data) {
    // 半透明遮罩层
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(overlay, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);

    // 对话框容器
    lv_obj_t* box = lv_obj_create(overlay);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, 300, 180);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(RGB[bingchuanbai]), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 10, 0);

    // 提示文字
    lv_obj_t* msg_lab = lv_label_create(box);
    lv_obj_set_width(msg_lab, 260);
    lv_obj_set_align(msg_lab, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(msg_lab, 0, 20);
    lv_label_set_text(msg_lab, msg);
    lv_obj_set_style_text_align(msg_lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(msg_lab, lv_color_hex(RGB[mose]), 0);
    // 按字号加载中文字体
    lv_font_t* font = ui_font_get(font_size);
    if (font != NULL) {
        lv_obj_set_style_text_font(msg_lab, font, 0);
    }

    // 取消 / 确定 按钮
    int st_cancel = style_btn_init(0, RGB[wuhui], RGB[bingchuanbai], font_size);
    int st_ok = style_btn_init(1, RGB[shanchui], RGB[bingchuanbai], font_size);
    create_button(st_cancel, box, LV_ALIGN_BOTTOM_MID, -80, -20, 110, 42,
                  "取消", confirm_dlg_btn_cb, ok_data);
    lv_obj_t* ok_btn = create_button(st_ok, box, LV_ALIGN_BOTTOM_MID, 80, -20, 110, 42,
                                     "确定", confirm_dlg_btn_cb, ok_data);

    // 保存回调信息，挂在遮罩层上供内部回调使用
    confirm_dlg* dlg = (confirm_dlg*)lv_malloc(sizeof(confirm_dlg));
    if (dlg == NULL) {
        printf("[错误] create_confirm_dialog: 内存分配失败\n");
        lv_obj_delete(overlay);
        return;
    }
    dlg->ok_btn = ok_btn;
    dlg->ok_cb = ok_cb;
    dlg->ok_data = ok_data;
    lv_obj_set_user_data(overlay, dlg);
}

/**
 * @brief 时钟定时刷新回调
 * @details 每秒用系统时间刷新标签内容，显示"年-月-日 时:分:秒"
 */
static void clock_timer_cb(lv_timer_t* t) {
    lv_obj_t* lab = (lv_obj_t*)lv_timer_get_user_data(t);
    if (lab == NULL) return;

    time_t now = time(NULL);
    struct tm* tnow = localtime(&now);
    if (tnow == NULL) return;

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tnow);
    lv_label_set_text(lab, buf);
}

/**
 * @brief 创建一个屏幕或子屏幕容器
 * @param parent 父对象；传 NULL 创建根屏幕，传已有对象创建子屏幕
 * @param w 宽度；<=0 表示不设置（根屏幕默认全屏，子屏幕用默认尺寸）
 * @param h 高度；<=0 表示不设置
 * @param bg_color 背景颜色（十六进制，可用 RGB[] 取色）；<0 表示不设置背景
 * @param radius 圆角半径；<0 表示不设置圆角
 * @param opa 背景不透明度（0~255，LV_OPA_*）；<0 表示不设置
 * @param align 对齐方式（如 LV_ALIGN_CENTER / LV_ALIGN_RIGHT_MID）；不设置传 LV_ALIGN_DEFAULT
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @return 创建的屏幕对象指针；失败返回 NULL
 */
lv_obj_t* create_screen(lv_obj_t* parent, int w, int h, int bg_color, int radius, int opa,
    lv_align_t align, int x, int y) {
    // 创建屏幕对象（parent 为 NULL 时是根屏幕，否则是子屏幕容器）
    lv_obj_t* scr = lv_obj_create(parent);
    if (scr == NULL) {
        printf("[错误] create_screen: 屏幕对象创建失败\n");
        return NULL;
    }

    // 按需设置宽高（<=0 不设置，根屏幕默认全屏）
    if (w > 0) lv_obj_set_width(scr, w);
    if (h > 0) lv_obj_set_height(scr, h);

    // 按需设置背景颜色（<0 使用默认背景）
    if (bg_color >= 0) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(bg_color), 0);
    }

    // 按需设置背景不透明度（<0 使用默认不透明度）
    if (opa >= 0) {
        lv_obj_set_style_bg_opa(scr, opa, 0);
    }

    // 按需设置圆角（<0 使用默认圆角）
    if (radius >= 0) {
        lv_obj_set_style_radius(scr, radius, 0);
    }

    // 按需设置对齐位置（居中/靠右等，含坐标偏移）
    lv_obj_align(scr, align, x, y);

    return scr;
}

/**
 * @brief 创建一个自动刷新的时钟标签
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对对齐锚点的 x 偏移
 * @param y 相对对齐锚点的 y 偏移
 * @param color 文字颜色（十六进制）
 * @param font_size 文字字号
 * @return 创建的标签对象指针；参数非法时返回 NULL
 * @details 内部创建 1 秒定时器自动刷新，无需手动更新
 */
lv_obj_t* create_clock(lv_obj_t* parent, lv_align_t align, int x, int y, int color, int font_size) {
    if (parent == NULL) {
        printf("[错误] create_clock: 父对象为空\n");
        return NULL;
    }

    lv_obj_t* lab = lv_label_create(parent);
    lv_obj_align(lab, align, x, y);
    lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
    // 按字号加载中文字体
    lv_font_t* font = ui_font_get(font_size);
    if (font != NULL) {
        lv_obj_set_style_text_font(lab, font, 0);
    }

    lv_timer_t* t = lv_timer_create(clock_timer_cb, 1000, lab);
    if (t == NULL) {
        printf("[错误] create_clock: 定时器创建失败\n");
        return NULL;
    }
    clock_timer_cb(t);   // 立即刷新一次，显示当前时间
    return lab;
}