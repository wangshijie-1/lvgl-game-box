/**
 * @file 0ui.h
 * @brief UI 通用组件与全局资源头文件
 * @details 声明颜色表、全局字体、按钮样式池及通用控件创建函数
 */

#ifndef __0UI_H__
#define __0UI_H__

#include "lvgl/lvgl.h"

// 按钮样式池最大数量
#define STYLE_BTN_SIZE  20

// 颜色名称枚举，与 RGB[] 数组一一对应
typedef enum {
    wumailan,      // 雾霾蓝
    huimeigui,     // 灰玫瑰
    yanmaise,      // 燕麦色
    huidoulv,      // 灰豆绿
    yanhuise,      // 烟灰色
    ouhese,        // 藕荷色
    shuweicao,     // 鼠尾草
    beikefen,      // 贝壳粉
    feihong,       // 绯红
    chidan,        // 赤丹
    shanchui,      // 山吹
    ruozhu,        // 若竹
    liuli,         // 琉璃
    tengzi,        // 藤紫
    mose,          // 墨色
    shengchengse,  // 生成色
    bingchuanbai,  // 冰川白
    wuhui,         // 雾灰
    shayan,        // 砂岩
    taixianlv,     // 苔藓绿
    shenhailan,    // 深海蓝
    jiaotangzong,  // 焦糖棕
    shanhucheng,   // 珊瑚橙
    xunyicao,      // 薰衣草
    nihongfen,     // 霓虹粉
    dianguanglan,  // 电光蓝
    jiguangzi,     // 激光紫
    yingguanglv,   // 荧光绿
    rongyancheng,  // 熔岩橙
    shenkonghei,   // 深空黑
    quanxijing,    // 全息青
    maichonghuang, // 脉冲黄
    chitao,        // 赤陶
    zheshi,        // 赭石
    ganlanlv,      // 橄榄绿
    nituzong,      // 泥土棕
    shibanhui,     // 石板灰
    shamojin,      // 沙漠金
    senlinlv,      // 森林绿
    muguangzi,     // 暮光紫
    taibai,        // 钛白
    yinhui,        // 银灰
    shimo,         // 石墨
    tanhei,        // 炭黑
    zhenzhuhui,    // 珍珠灰
    tiehui,        // 铁灰
    qianhui,       // 铅灰
    shuanghui,     // 霜灰
    difunilan,     // 蒂芙尼蓝
    aimasicheng,   // 爱马仕橙
    kelanyinlan,   // 克莱因蓝
    bodigonghong,  // 勃艮第红
    xiangnaierhei, // 香奈儿黑
    kadiyajin,     // 卡地亚金
    pantonghong,   // 潘通红
    laolishilv,    // 劳力士绿
    riluocheng,    // 日落橙
    mitaofen,      // 蜜桃粉
    shenhaibian,   // 深海渐变
    jiguangzi2,    // 极光紫
    boheqingxin,   // 薄荷清新
    meiguijin,     // 玫瑰金
    wuyelan,       // 午夜蓝
    yinghuafen,    // 樱花粉
    wu,            // 白色
} RgbNum;

// 颜色值表，下标由 RgbNum 枚举指定
extern int RGB[wu + 1];

// 中文字体文件路径
#define FONT_FILE_PATH "A:tu/hei.TTF"

// 按需获取指定字号的中文字体（内部按字号缓存，避免重复加载耗尽内存）
lv_font_t* ui_font_get(int size);

// 按钮样式结构体，包含按钮背景样式与文字样式
typedef struct {
    lv_style_t btn;  // 按键背景样式
    lv_style_t lab;  // 按键文字样式
    int u32btn;      // 背景颜色值（用于去重比较）
    int u32lab;      // 文字颜色值（用于去重比较）
    int font_size;   // 文字字号（用于去重比较）
    int inited;      // 该样式槽是否已初始化
} style_btn;

extern style_btn Btn[STYLE_BTN_SIZE];

/**
 * @brief 创建一个带文字的按钮
 * @param style_num 样式池中已初始化的样式索引
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param w 按钮宽度
 * @param h 按钮高度
 * @param text 按钮文字
 * @param cb 按钮事件回调函数
 * @param user_data 传给回调函数的用户数据
 * @return 创建的按钮对象指针
 */
lv_obj_t* create_button(int style_num, lv_obj_t* parent,
    lv_align_t align, int x, int y, int w, int h,
    const char* text, lv_event_cb_t cb, void* user_data);

// 通用界面切换回调（登录 / 返回按钮共用）：user_data==1 进入游戏盒子
void go_to_sreen_cb(lv_event_t* e);

/**
 * @brief 初始化按钮样式池，将所有槽位标记为未使用
 */
void style_btn_init0(void);

/**
 * @brief 查找或创建一个按钮样式
 * @param num 期望使用的样式槽起始索引
 * @param bg_color 按钮背景颜色（十六进制）
 * @param text_color 按钮文字颜色（十六进制）
 * @param font_size 按钮文字字号
 * @return 实际分配的样式槽索引，失败返回 -1
 * @details 若相同样式已存在则直接复用，否则在空闲槽新建
 */
int style_btn_init(int num, int bg_color, int text_color, int font_size);

/**
 * @brief 文本域焦点事件回调
 * @param e LVGL 事件对象
 * @details 获取焦点时显示键盘，失去焦点或点击确认时隐藏键盘
 */
void textarea_cb(lv_event_t* e);

/**
 * @brief 创建一个文本输入框
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param placeholder 占位提示文字（为空时不设置）
 * @param color 文字颜色（十六进制，可用 RGB[] 取色）
 * @param font_size 文字字号
 * @param kb 关联的键盘对象（传 NULL 则不绑定键盘）
 * @param is_password 是否密码模式（1=输入显示为圆点，0=明文）
 * @return 创建的输入框对象指针；参数非法时返回 NULL
 * @details 自动应用中文字体，绑定键盘后聚焦弹键盘、确认键收起
 */
lv_obj_t* create_textbox(lv_obj_t* parent, lv_align_t align, int x, int y, int w, int h,
    const char* placeholder, int color, int font_size, lv_obj_t* kb, int is_password);

/**
 * @brief 创建一个文本标签
 * @param parent 父对象
 * @param color 文字颜色（十六进制，可用 RGB[] 取色）
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param text 文本内容
 * @param font_size 文字字号
 * @return 创建的标签对象指针
 */
lv_obj_t* create_label(lv_obj_t* parent, int color, lv_align_t align,
    int x, int y, const char* text, int font_size);

/**
 * @brief 创建一个开关控件
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param on_color 开启时的颜色
 * @param off_color 关闭时的颜色
 * @param cb 值变化回调函数（LV_EVENT_VALUE_CHANGED）
 * @param user_data 传给回调函数的用户数据
 * @return 创建的开关对象指针
 */
lv_obj_t* create_switch(lv_obj_t* parent, lv_align_t align, int x, int y,
    int w, int h, int on_color, int off_color,
    lv_event_cb_t cb, void* user_data);

/**
 * @brief 创建一个滑条控件
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param min 最小值
 * @param max 最大值
 * @param value 初始值
 * @param color 已滑动部分的颜色
 * @param cb 值变化回调函数（LV_EVENT_VALUE_CHANGED）
 * @param user_data 传给回调函数的用户数据
 * @return 创建的滑条对象指针
 */
lv_obj_t* create_slider(lv_obj_t* parent, lv_align_t align, int x, int y,
    int w, int h, int min, int max, int value, int color,
    lv_event_cb_t cb, void* user_data);

/**
 * @brief 创建一个进度条控件
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param min 最小值
 * @param max 最大值
 * @param value 初始进度值
 * @param color 进度指示条颜色
 * @return 创建的进度条对象指针
 */
lv_obj_t* create_bar(lv_obj_t* parent, lv_align_t align, int x, int y,
    int w, int h, int min, int max, int value, int color);

/**
 * @brief 创建一个图片按钮
 * @param parent 父对象
 * @param src 图片源（指向 lv_image_dsc_t 的指针）
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param w 宽度
 * @param h 高度
 * @param cb 点击回调函数（LV_EVENT_CLICKED）
 * @param user_data 传给回调函数的用户数据
 * @return 创建的图片按钮对象指针
 */
lv_obj_t* create_image_button(lv_obj_t* parent, const void* src, lv_align_t align,
    int x, int y, int w, int h, lv_event_cb_t cb, void* user_data);

/**
 * @brief 创建一个确认弹窗
 * @param msg 提示文字
 * @param font_size 弹窗文字字号
 * @param ok_cb 点击确定后的回调函数（LV_EVENT_CLICKED，user_data 为 ok_data）
 * @param ok_data 传给确认回调的用户数据
 * @details 点击确定先执行 ok_cb 再自动关闭；点击取消直接关闭
 */
void create_confirm_dialog(const char* msg, int font_size, lv_event_cb_t ok_cb, void* ok_data);
/**
 * @brief 创建一个自动刷新的时钟标签
 * @param parent 父对象
 * @param align 对齐方式
 * @param x 相对于对齐锚点的 x 偏移
 * @param y 相对于对齐锚点的 y 偏移
 * @param color 文字颜色（十六进制）
 * @param font_size 文字字号
 * @return 创建的标签对象指针
 * @details 内部创建 1 秒定时器自动刷新，显示"年-月-日 时:分:秒"
 */
lv_obj_t* create_clock(lv_obj_t* parent, lv_align_t align, int x, int y, int color, int font_size);

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
 * @details 一个函数搞定屏幕创建，宽高/背景色/圆角/透明度/对齐位置按需传参，不用每次写一堆设置
 */
lv_obj_t* create_screen(lv_obj_t* parent, int w, int h, int bg_color, int radius, int opa,
    lv_align_t align, int x, int y);

#endif
