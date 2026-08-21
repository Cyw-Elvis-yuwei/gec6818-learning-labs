# 嵌入式软件学习合集

这里集中保存非 STM32 的课程实验和完整项目。每个项目使用独立编号目录，不需要为每次实验新建 GitHub 仓库。

## 项目目录

| 编号 | 项目 | 技术方向 | 当前归档证据 |
| --- | --- | --- | --- |
| 01 | [LVGL 景区门票自助售票终端](01-lvgl-ticket-terminal/) | 嵌入式 Linux、LVGL、framebuffer、evdev、FreeType | 源码与静态资料已归档；当前版本构建和真机验证待补充 |
| 02 | [医路通 GEC6818 联网智慧医疗自助终端](02-gec6818-clinic-terminal/) | 嵌入式 Linux、LVGL、TCP/JSON、epoll、SQLite | 保留历史构建、测试和用户实机确认；部分原始日志不完整 |
| 03 | [ESP32 按键中断状态机](03-esp32-button-interrupt-state-machine/) | ESP32-S3 / GPIO Interrupt / FreeRTOS Queue / Task / Button State Machine | 用户实测通过：build、烧录、单击、双击、长按、LED 反馈 |
| 04 | [ESP32-S3 PCA9557 I2C IO 扩展](04-esp32-pca9557-i2c-io-expander/) | ESP32-S3 / 新版 I2C Master API / PCA9557 / GPIO 扩展 / 输入输出回环验证 | 用户实测通过：build、烧录、PCA9557 初始化、测试 A（IO5/IO6 回环）、测试 B（IO7 回环） |
| 05 | [ESP32-S3 QMI8658 新版 I2C 迁移](05-esp32-qmi8658-new-i2c/) | ESP32-S3 / 新版 I2C Master API / 多设备共享 I2C bus / QMI8658 姿态检测 | 用户实测通过：build、烧录、QMI8658 初始化、XYZ 倾角持续读取与姿态变化 |
| 06 | [ESP32-S3 ST7796 + FT6336 滑动看图](06-esp32s3-st7796-ft6336-swipe-gallery/) | ESP32-S3 / ST7796 SPI LCD / FT6336 I2C 触摸 / 滑动手势 / 内置演示图 | 用户实测通过：build、115200 完整烧录、屏幕显示、左右滑动识别与循环切图 |

## 证据口径

- “源码存在”只表示文件已归档。
- “静态检查”不等于编译通过。
- 历史构建、运行或真机反馈会明确标注日期和证据完整性。
- 没有重新执行的项目不会写成“当前版本已验证”。

## 归档原则

- 原始工程只读，不覆盖、不删除。
- 排除对象文件、构建目录、重复备份和发布压缩包。
- 未确认许可的字体和第三方素材不进入合集。
- 必需的第三方源码或库只保留实际构建需要的最小集合，并保留其许可证文件。

最后整理：2026-08-21。
