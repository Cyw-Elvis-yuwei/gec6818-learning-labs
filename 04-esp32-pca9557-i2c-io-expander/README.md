# ESP32-S3 PCA9557 I2C IO 扩展实验（新版 I2C Master API）

## 实验名称

ESP32-S3 + 新版 I2C Master API + PCA9557 IO 扩展实验

## 实验目标

使用 ESP-IDF 新版 I2C Master API（`driver/i2c_master.h`）驱动板载 PCA9557 IO 扩展芯片：

- IO0~IO4 配置为输出
- IO5~IO7 配置为输入
- 设置输出电平
- 读取输入电平

## 硬件

- MCU：ESP32-S3
- PCA9557 I2C 地址：0x19（板载 I2C 总线）
- SDA：GPIO1
- SCL：GPIO2
- I2C 频率：100 kHz

| PCA9557 IO | 板级信号 | 方向 |
| --- | --- | --- |
| IO0 | LCD_CS | 输出（初始 1） |
| IO1 | PA_EN | 输出（初始 0） |
| IO2 | DVP_PWDN | 输出（初始 1） |
| IO3 | EXT-IO1 | 输出（实验翻转） |
| IO4 | EXT-IO2 | 输出（实验翻转） |
| IO5 | EXT-IO3 | 输入（杜邦线回环） |
| IO6 | EXT-IO4 | 输入（杜邦线回环） |
| IO7 | EXT-IO5 | 输入（杜邦线回环） |

## 软件

- ESP-IDF：v5.5.3
- 使用**新版 I2C Master API**：`driver/i2c_master.h`
- 不使用旧版 `driver/i2c.h` / `i2c_param_config()` / `i2c_driver_install()` / `i2c_cmd_link_create()`

## 代码结构

```text
main/main.c
    应用流程：创建新版 I2C master bus → 初始化 PCA9557 → IO3/IO4 周期翻转 + IO5/6/7 读取打印

main/pca9557.c / main/pca9557.h
    PCA9557 最小驱动：pca9557_init / pca9557_set_level / pca9557_get_level
```

设计要点：

- I2C master bus 由 `main.c` 创建（GPIO1=SDA，GPIO2=SCL，100 kHz）。
- PCA9557 驱动接收 bus handle，内部通过 `i2c_master_bus_add_device()` 添加 0x19 设备。
- 寄存器写用 `i2c_master_transmit()`，寄存器读用 `i2c_master_transmit_receive()`（先写寄存器地址再读数据）。
- 驱动内部维护 Output Port 影子值：`set_level()` 只修改目标位后整字节回写，保证 IO0=1 / IO1=0 / IO2=1 不会被其他 IO 的写操作意外覆盖。
- IO5/IO6/IO7 任一读取失败时打印明确错误并跳过本轮数据行，不输出看似有效的电平。

## PCA9557 配置

| 寄存器 | 值 | 含义 |
| --- | --- | --- |
| Output Port（0x01） | 0x05 | IO0=1，IO1=0，IO2=1；IO3/IO4 初始 0 |
| Polarity Inversion（0x02） | 0x00 | 不反转，IO5~IO7 读取与实际物理电平同极性 |
| Configuration（0x03） | 0xE0 | IO0~IO4=输出（0），IO5~IO7=输入（1） |

初始化顺序（避免先切输出再出现不受控电平）：

1. `i2c_master_bus_add_device`（地址 0x19，100 kHz）
2. 写 Output Port = 0x05（此刻 IO 仍为输入，不产生输出电平）
3. 写 Polarity = 0x00
4. 最后写 Configuration = 0xE0

## 构建与运行

目标芯片为 ESP32-S3，ESP-IDF v5.5.3：

```bash
idf.py set-target esp32s3   # 如尚未设置
idf.py build
idf.py -p PORT flash monitor
```

依赖组件（`espressif/led_strip`，来自工程模板，本实验未使用）由组件管理器按 `main/idf_component.yml` 与 `dependencies.lock` 自动拉取。

## 验证方法

用户通过两轮杜邦线回环测试验证：板子断电后接线，运行同一固件，串口观察至少两个完整 A/B 周期（每个状态保持 1 秒，每周期 2 秒）。

### 测试 A：验证 IO5、IO6

接线：

```text
H1-3 EXT-IO1 / IO3 → H1-5 EXT-IO3 / IO5
H1-4 EXT-IO2 / IO4 → H1-6 EXT-IO4 / IO6
```

验收：

```text
IO5 == IO3
IO6 == IO4
```

结果：**PASS**（用户真实验证）

### 测试 B：验证 IO7

移除测试 A 接线，改接：

```text
H1-3 EXT-IO1 / IO3 → H1-7 EXT-IO5 / IO7
```

验收：

```text
IO7 == IO3
```

结果：**PASS**（用户真实验证）

串口输出示例：

```text
PCA9557 initialized
IO0~IO4: OUTPUT
IO5~IO7: INPUT
OUT: IO3=0 IO4=1 | IN: IO5=0 IO6=1 IO7=1
OUT: IO3=1 IO4=0 | IN: IO5=1 IO6=0 IO7=1
```

## 已验证状态

以下结果全部来自用户真实编译、烧录与串口观察（非 DeepSeek Harness 自动验证）：

- ESP-IDF build：用户真实验证 PASS
- 烧录 / 启动：用户真实验证 PASS
- PCA9557 初始化（0x19 应答，无 NACK / timeout / reset loop）：用户真实验证 PASS
- 测试 A（IO5 跟随 IO3、IO6 跟随 IO4）：用户真实验证 PASS
- 测试 B（IO7 跟随 IO3）：用户真实验证 PASS

## 后续学习 / 扩展

- 将 QMI8658A 陀螺仪代码迁移到新版 I2C 驱动（**尚未实现**，本实验不包含该功能）。
