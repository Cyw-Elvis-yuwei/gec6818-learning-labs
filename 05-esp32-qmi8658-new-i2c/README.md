# ESP32-S3 QMI8658 新版 I2C 迁移实验

## 实验名称

ESP32-S3 + 新版 I2C Master API + QMI8658 姿态传感器迁移

## 实验目标

将课件《11-I2C-姿态传感器》中 QMI8658 的旧版 I2C 通信迁移到 ESP-IDF 新版 I2C Master API：

- `i2c_master_bus_add_device()`
- `i2c_master_transmit()`
- `i2c_master_transmit_receive()`

同一 I2C bus（100 kHz，GPIO1=SDA，GPIO2=SCL）挂载两个设备：

```text
├─ PCA9557 @ 0x19（仅初始化）
└─ QMI8658 @ 0x6A（初始化 + 每秒 XYZ 倾角输出）
```

## 硬件

- MCU：ESP32-S3
- SDA：GPIO1
- SCL：GPIO2
- I2C：100 kHz
- PCA9557 @ 0x19（仅初始化）
- QMI8658 @ 0x6A（姿态传感器）

## 软件

- ESP-IDF：v5.5.3
- 使用**新版 I2C Master API**：`driver/i2c_master.h`
- 不使用旧版 `driver/i2c.h` / `i2c_param_config()` / `i2c_driver_install()` / `i2c_master_write_to_device()` / `i2c_master_write_read_device()`

## QMI8658 配置

WHO_AM_I 期望值：`0x05`

| 寄存器 | 值 |
| --- | --- |
| RESET (0x60) | 0xB0 |
| CTRL1 (0x02) | 0x40（地址自动递增） |
| CTRL2 (0x03) | 0x15（加速度计） |
| CTRL3 (0x04) | 0x55（陀螺仪） |
| CTRL7 (0x08) | 0x03（启用 Acc + Gyro） |

初始化流程：

1. WHO_AM_I 校验（最多 5 次，间隔 100 ms；仍非 0x05 返回错误）
2. RESET = 0xB0，等待 10 ms
3. CTRL1 → CTRL2 → CTRL3 → CTRL7
4. CTRL7 后等待约 200 ms

数据读取：

- STATUS0（0x2E）data-ready 有限等待（总等待约 100 ms）
- 从 AX_L（0x35）连续读取 12 字节（acc_x/y/z + gyr_x/y/z，各 int16，低字节在前）
- 根据加速度计算 XYZ 倾角（atan 公式，弧度 × 57.29578f 转角度）
- 每约 1 秒串口打印 `angle_x / angle_y / angle_z`

## Bug 修复记录

初版现象（用户真实验证）：

```text
PCA9557 initialized
QMI8658 OK
QMI8658 read failed: ESP_ERR_TIMEOUT（大量，偶尔成功一次）
```

修正内容：

```text
CTRL2 0x95 → 0x15
CTRL3 0xD5 → 0x55
配置顺序调整为 CTRL1 → CTRL2 → CTRL3 → CTRL7
CTRL7 后增加约 200 ms 启动稳定等待
```

修正后用户真实验证：

```text
连续倾角输出：PASS
开发板姿态变化 → XYZ 数值变化：PASS
```

> 验证结果来自用户真实 build / flash / monitor / 硬件操作，不是 DeepSeek Harness 自动验证。

## 构建与运行

目标芯片为 ESP32-S3，ESP-IDF v5.5.3：

```bash
idf.py set-target esp32s3   # 如尚未设置
idf.py build
idf.py -p PORT flash monitor
```

## 代码结构

```text
main/main.c        应用流程：I2C bus 创建、PCA9557/QMI8658 初始化、每秒倾角打印
main/pca9557.c/.h  PCA9557 最小驱动（仅初始化，IO0~IO2 安全初值）
main/qmi8658.c/.h  QMI8658 最小驱动（初始化 + 原始值读取 + 倾角计算）
```

## 已验证状态

以下结果全部来自用户真实编译、烧录与硬件操作（非自动化验证）：

- ESP-IDF build：用户真实验证 PASS
- 烧录 / 启动：用户真实验证 PASS
- PCA9557 @ 0x19 初始化：用户真实验证 PASS
- QMI8658 @ 0x6A 初始化（WHO_AM_I）：用户真实验证 PASS
- XYZ 倾角持续输出：用户真实验证 PASS
- 倾斜开发板 XYZ 数值明显变化：用户真实验证 PASS
- 原 ESP_ERR_TIMEOUT 问题已修复：用户真实验证 PASS
