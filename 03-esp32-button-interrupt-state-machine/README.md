# ESP32-S3 GPIO 中断 + FreeRTOS 按键状态机

## 实验名称

ESP32-S3 GPIO 中断 + FreeRTOS 按键状态机

## 硬件

- ESP32-S3
- 用户按键：GPIO11
- GPIO11 松开为高电平
- GPIO11 按下为低电平（内部上拉）
- 板载 LED：GPIO10（低电平点亮）

## 功能

支持三种按键事件：

- 单击 `SINGLE_CLICK`
- 双击 `DOUBLE_CLICK`
- 长按 `LONG_PRESS`

## 核心架构

```text
GPIO11 双边沿中断
        ↓
ISR
记录 level + tick
        ↓
FreeRTOS Key Queue
        ↓
Key Task
消抖 + 状态机
        ↓
SINGLE / DOUBLE / LONG
        ↓
LED Command Queue
        ↓
LED Task
        ↓
GPIO10
```

- GPIO ISR 只做最小工作：读电平、`xTaskGetTickCountFromISR()` 记录 tick、`xQueueSendFromISR()` 入队，立即退出；不打印、不判断、不操作 LED。
- 消抖在 Key Task 完成：事件到达后等待 20ms 重读电平确认，时间判断仍使用 ISR 记录的原始 tick。
- 双击窗口用 `xQueueReceive` 超时（300ms）实现，不把任务睡死。
- Key Task 与 LED Task 通过 LED Command Queue 解耦，Key Task 不直接操作 LED。

## 时间参数

| 参数 | 值 |
| --- | --- |
| 消抖确认延时 | 20 ms |
| 最小有效按压 | 30 ms（小于视为毛刺） |
| 双击窗口 | 300 ms（第一次松开后等待第二次按下） |
| 长按阈值 | 1500 ms |
| LED 快闪单阶段 | 100 ms（亮/灭各 100ms） |

所有时间常量均通过 `pdMS_TO_TICKS()` 换算，不假设 FreeRTOS tick 等于 1ms。

## 行为

单击：

```text
LED 翻转一次
```

双击：

```text
LED 快闪 2 次后恢复原状态
```

长按：

```text
LED 快闪 3 次后恢复原状态
```

同时通过串口打印事件日志，例如：

```text
[KEY] SINGLE_CLICK duration=xxx ms
[KEY] DOUBLE_CLICK interval=xxx ms
[KEY] LONG_PRESS duration=xxx ms
```

## 构建与运行

目标芯片为 ESP32-S3。在 ESP-IDF 环境下执行：

```bash
idf.py set-target esp32s3   # 如尚未设置
idf.py build
idf.py -p PORT flash monitor
```

依赖组件（`espressif/led_strip`，来自工程模板，本实验未使用）由组件管理器按 `main/idf_component.yml` 与 `dependencies.lock` 自动拉取。

## 已验证状态

- ESP-IDF build：用户已验证通过
- 烧录：用户已验证通过
- 单击：用户实测通过
- 双击：用户实测通过
- 长按：用户实测通过
- LED 反馈：用户目视通过
