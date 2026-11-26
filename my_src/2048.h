/**
 * @file 2048.h
 * @brief 2048 游戏模块对外接口与数据结构声明
 */

#ifndef __2048_H__
#define __2048_H__

// 单个格子数据结构
typedef struct {
    int num;        // 当前格子显示的数字，0 表示空
    int jisuan;     // 标记本回合是否已经合并过，防止连环合并
} GeZi;

// 棋盘边长，4x4
#define SIZE_2048 4

// 2048 游戏界面屏幕对象
extern lv_obj_t* src2048;

// 4x4 棋盘数据数组
extern GeZi gezi[SIZE_2048][SIZE_2048];

// 4x4 棋盘图像控件数组
extern lv_obj_t* img2048[SIZE_2048][SIZE_2048];

// 数字 0/2/4/8...2048 对应的 BMP 图片路径表，共 12 张
extern const char* bmp2048[12];

/**
 * @brief 在随机空格子中生成一个新数字
 * @param gezi 棋盘数据数组
 */
void suiji(GeZi gezi[SIZE_2048][SIZE_2048]);

/**
 * @brief 清除所有格子的合并标记
 * @param gezi 棋盘数据数组
 * @details 每次滑动前调用，防止同一回合内多次合并
 */
void hebinghou(GeZi gezi[SIZE_2048][SIZE_2048]);

/**
 * @brief 按指定方向合并并移动格子
 * @param gezi 棋盘数据数组
 * @param fangxiang 方向：1=上，2=下，3=左，4=右
 */
void hebing(GeZi gezi[SIZE_2048][SIZE_2048], int fangxiang);

/**
 * @brief 根据棋盘数据刷新 UI 显示
 * @param gezi 棋盘数据数组
 */
void show_2048(GeZi gezi[SIZE_2048][SIZE_2048]);

/**
 * @brief 创建 4x4 图像网格
 * @param parent 父对象
 * @param x0 网格左上角 x 坐标
 * @param y0 网格左上角 y 坐标
 */
void create_img(lv_obj_t* parent, int x0, int y0);

/**
 * @brief 创建 2048 游戏界面
 */
void creat_my_2048(void);

#endif