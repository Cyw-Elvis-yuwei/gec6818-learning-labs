# ESP32-S3 ST7796 + FT6336 滑动看图实验

## 项目目标

在 ESP32-S3 上驱动 3.5 英寸 ST7796 SPI 液晶屏和 FT6336 I2C 电容触摸屏，通过按下点与松开前最后一点计算滑动方向，并用左右滑动循环浏览 3 张程序内置演示图。图片由代码实时生成，不需要 TF 卡。

## 技术与硬件

- ESP-IDF 5.5.3，目标芯片 `esp32s3`
- ST7796，SPI Mode 0，RGB565，横屏 480×320
- FT6336，I2C 地址 `0x38`
- PCA9557，I2C 地址 `0x19`，IO0 控制 LCD CS
- QMI8658，I2C 地址 `0x6A`；本阶段仅初始化，暂停周期读取以避免影响触摸采样

关键引脚以 `main/main.c` 为准：I2C SDA=GPIO1、SCL=GPIO2，LCD SCK=GPIO41、MOSI=GPIO40、DC=GPIO39、背光=GPIO42。

## 功能说明

1. 初始化共享 I2C 总线、PCA9557、ST7796、FT6336 和 QMI8658。
2. 使用厂商初始化序列启动 ST7796，SPI 模式固定为 Mode 0。
3. 将 FT6336 原始坐标转换为横屏坐标。
4. 每 20 ms 读取一次触摸；水平位移不少于 80 像素且大于垂直位移时判定为滑动。
5. 左滑进入下一张，右滑返回上一张，3 张内置图循环显示。

## 目录说明

- `main/main.c`：LCD 初始化、内置图片生成、坐标映射和滑动状态机
- `main/ft6336.*`：FT6336 触摸驱动
- `main/pca9557.*`：PCA9557 IO 扩展驱动
- `main/qmi8658.*`：QMI8658 驱动
- `components/esp_lcd_st7796/`：项目实际使用的 ST7796 组件最小源码及 Apache-2.0 许可证
- `CMakeLists.txt`、`main/CMakeLists.txt`、`main/idf_component.yml`、`dependencies.lock`：ESP-IDF 构建与依赖声明

## 构建与烧录

在 ESP-IDF 终端执行：

```powershell
cd <本项目目录>
idf.py set-target esp32s3
idf.py build
idf.py -p COM18 -b 115200 flash
idf.py -p COM18 monitor
```

端口号按本机实际情况替换。当前板卡在 Windows 下使用 460800 烧录时曾发生 USB 串口重新枚举后的 pySerial 配置失败；115200 已完整烧录成功。

## 验证证据

| 层级 | 结果 | 证据 |
| --- | --- | --- |
| 源码存在 | 已确认 | 主程序、4 组设备驱动、构建清单和 ST7796 组件已归档 |
| 静态检查 | 已确认 | 归档前检查入口、依赖、引脚、SPI Mode 0、坐标映射和滑动阈值 |
| 构建验证 | 用户当前验证通过 | 2026-08-21，ESP-IDF 5.5.3 生成 `led_blink.bin`，大小 `0x391b0` |
| 烧录验证 | 用户当前验证通过 | 2026-08-21，115200 写入 bootloader、应用和分区表，三段均 `Hash of data verified`，最后 `Done` |
| 真机行为 | 用户当前验证通过 | 屏幕正常显示 3 张演示图；FT6336 输出左右滑动日志；左右滑动切图符合预期 |

## 归档取舍

未归档 `build`、旧构建、缓存、临时目录、VS Code 配置、`dsh-usage` 和生成的 `sdkconfig`。`managed_components` 未内置，ESP-IDF Component Manager 会根据清单恢复依赖。课程 PDF、DOCX、原理图和屏幕资料未复制到软件合集，避免重复文件和不明确的再分发范围。

原始工程 `E:\ESP32\esp32_lab` 在归档过程中保持不变。
