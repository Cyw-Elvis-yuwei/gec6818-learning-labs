/**
 * @file ui_login.h
 * @brief 登录界面模块 - 头文件
 * 
 * 功能:
 *   - 800x480 全屏登录界面(背景图 + 输入框 + 软键盘 + 按钮)
 *   - 用户登录校验(对接 file_user 模块)
 *   - 独立注册弹窗(含账号密码输入框 + 确认/取消按钮)
 *   - 记住密码功能(复选框 + 缓存文件读写)
 *   - 复古/经典主题切换(复选框切换背景图)
 *   - 操作结果弹窗提示
 */

#ifndef UI_LOGIN_H
#define UI_LOGIN_H

#include "lvgl/lvgl.h"

/**
 * @brief 创建并显示登录界面(全屏)
 * @note  调用后会接管整个屏幕, 所有交互通过内部事件回调处理
 *        登录成功后自动销毁登录界面并跳转主界面(由回调函数指针实现)
 */
void ui_login_create(void);

#endif /* UI_LOGIN_H */
