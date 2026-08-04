/*
 * 文件名：main.c
 * 文件作用：
 *   本文件是整个 LVGL 景区门票售票系统的程序入口。
 *
 * 零基础理解：
 *   1. 程序运行时，最先执行 main() 函数。
 *   2. main() 里先初始化 LVGL、屏幕、触摸，再创建登录界面。
 *   3. 最后进入 while(1) 死循环，让 LVGL 一直刷新界面和处理触摸事件。
 *
 * 答辩讲法：
 *   main.c 是程序入口文件，负责完成底层初始化和启动第一个界面。
 *   它不直接写业务逻辑，只负责把 LVGL、屏幕、触摸和登录界面启动起来。
 */

/* =========================
 * 头文件区域
 * 思路：
 *   使用某个函数之前，需要先包含对应头文件。
 *   例如要用 lv_init()，就要包含 lvgl/lvgl.h。
 * ========================= */

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "ui_login.h"


#define DISP_BUF_SIZE (800 * 480)

/*
 * main 函数：程序入口。
 *
 * 思路：
 *   程序启动后按顺序做 5 件事：
 *   1. 初始化 LVGL；
 *   2. 初始化屏幕显示；
 *   3. 注册显示驱动；
 *   4. 初始化触摸输入；
 *   5. 创建登录界面并进入循环刷新。
 */
int main(void)
{

    lv_init();
    fbdev_init();
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    lv_disp_drv_register(&disp_drv);
    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;
    indev_drv_1.read_cb = evdev_read;
    lv_indev_t *mouse_indev = lv_indev_drv_register(&indev_drv_1);

    /*
     * 7. 创建登录界面。
     *
     * 思路：
     *   程序启动后不直接进入主售票界面，而是先进入登录界面。
     *   用户登录成功后，再由登录模块跳转到主界面。
     *
     * 函数作用：
     *   ui_login_create()
     *   创建登录页面上的账号框、密码框、登录按钮、注册按钮等控件。
     */
    ui_login_create();

    /*
     * 8. LVGL 主循环。
     *
     * 思路：
     *   嵌入式界面程序不能执行一次就结束。
     *   它需要一直循环：
     *   1. 处理 LVGL 定时任务；
     *   2. 处理触摸事件；
     *   3. 刷新界面。
     */
    while (1) {
        /*
         * LVGL 定时任务处理函数。
         *
         * 函数作用：
         *   lv_timer_handler()
         *   处理 LVGL 内部任务，例如控件刷新、动画、输入事件、重绘屏幕等。
         */
        lv_timer_handler();

        /*
         * 休眠 5000 微秒，也就是 5 毫秒。
         *
         * 思路：
         *   如果 while 循环完全不休眠，会一直占用 CPU。
         *
         * 函数作用：
         *   usleep(5000)
         *   让程序短暂暂停，降低 CPU 占用。
         */
        usleep(5000);
    }

    /*
     * 正常情况下不会执行到这里。
     * 因为上面的 while(1) 是无限循环。
     */
    return 0;
}

/*
 * custom_tick_get：给 LVGL 提供系统运行时间。
 *
 * 思路：
 *   LVGL 需要知道“程序运行了多少毫秒”，用来处理动画、长按、定时器等功能。
 *   这个函数通过 Linux 的 gettimeofday() 获取当前系统时间，然后减去启动时间。
 *
 * 使用位置：
 *   在 lv_conf.h 中配置：
 *   LV_TICK_CUSTOM_SYS_TIME_EXPR
 *
 * 返回值：
 *   从程序启动到现在经过的毫秒数。
 */
uint32_t custom_tick_get(void)
{
    /*
     * start_ms 保存程序第一次调用该函数时的时间。
     *
     * static 说明：
     *   普通局部变量函数结束后会销毁；
     *   static 局部变量会一直保存值。
     */
    static uint64_t start_ms = 0;

    /*
     * 第一次调用时，记录起始时间。
     */
    if (start_ms == 0) {
        struct timeval tv_start;

        /*
         * 获取当前系统时间。
         *
         * 函数作用：
         *   gettimeofday(&tv_start, NULL)
         *   获取当前时间，包含秒 tv_sec 和微秒 tv_usec。
         */
        gettimeofday(&tv_start, NULL);

        /*
         * 把秒和微秒统一转换成毫秒。
         *
         * 计算说明：
         *   1 秒 = 1000000 微秒
         *   1 毫秒 = 1000 微秒
         */
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    /*
     * 每次调用时，再获取当前时间。
     */
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);

    /*
     * 当前时间也转换成毫秒。
     */
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    /*
     * 当前时间 - 起始时间 = 程序运行了多少毫秒。
     */
    uint32_t time_ms = now_ms - start_ms;

    /*
     * 返回给 LVGL 使用。
     */
    return time_ms;
}
