# S5P6818 平台概况

## 中文判断提示

- 当前状态：候选工具链运行证据已归档，最终选择和板端配置未完成
- 这是什么意思：本文是 `s5p6818_profile.json` 的人类可读摘要，不是独立配置源。
- 是否还需要继续讨论：不需要讨论旧业务复用；只需用板端证据匹配并选定工具链，再补齐字体和部署参数。
- 建议下一步：在真实开发板运行只读 probe，然后比较板端 rootfs 与三个候选工具链。
- 还缺什么：选定工具链、显示像素格式、触摸协议/校准、目标运行用户和许可字体。

机器可读事实以 [s5p6818_profile.json](./s5p6818_profile.json) 为唯一准则。本文的值若与 JSON 不一致，应先核对证据，再修正文档投影，不能反向用本文覆盖 JSON。

## 当前结论

| 范围 | 值 | 状态 | 证据 | 主要风险/验证 |
|---|---|---|---|---|
| 板卡名称 | S5P6818 | `confirmed-source` | 任务说明、当前 PRD | 名称不能证明 SoC/板卡修订；读 `/proc/cpuinfo` |
| CPU/SoC、位数、内核、libc | `null` | `unknown` | 尚无板端报告 | 运行 board probe |
| 旧 LVGL 源码 | 8.3.0、LVGL 8 API | `confirmed-source` | `lvgl/lvgl.h`、旧 `main.c` | `lv_conf.h` 注释仍写 8.2.0；禁止混入 v9 API |
| 新项目推荐 LVGL | 8.3.0 | `inferred` | 兼容旧 API 基线的移植决策 | 使用干净可追溯源码，升级另立任务 |

参考医疗后端“13 项主机测试通过”已移入 `evidence_sources`，作用域固定为 `host-backend`；它不属于 S5P6818 板级或工具链平台参数。当前板级 `confirmed-runtime` 数量为 0。

## 工具链

| 候选 | 工具链运行证据 | 默认架构/float ABI | 当前用途 |
|---|---|---|---|
| `/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc` | Buildroot 2016.11 GCC 5.4.0；`arm-none-linux-gnueabi`；独立 sysroot 存在 | Cortex-A15、AAPCS-Linux、soft；`scope=toolchain` | 等待与板端 rootfs 比较，未选定 |
| `/usr/bin/arm-linux-gnueabi-gcc` | Ubuntu GCC 9.4.0；`arm-linux-gnueabi`；sysroot 输出 `/` | ARMv5T、EABI5 对象、soft；`scope=toolchain` | 通用 Ubuntu 候选，未选定 |
| `arm-linux-gcc` | 已解析为 `/usr/bin/arm-linux-gnueabi-gcc-9` | 与上一候选相同；`scope=toolchain` | 仅为 PATH 别名，不应作为可复现选择 |

三条候选的 default、soft、softfp、hard 最小 `-c` 均通过，输出为 ELF32 ARM EABI5；这只证明编译阶段接受相应参数，不证明链接、libc、FreeType 或板端运行兼容。原始报告位于 `evidence/toolchain_probe_vmware_20260713T103421Z.log`。

旧 Makefile 使用 `-mfloat-abi=softfp` 和 `-static`，没有显式 sysroot；这只能说明旧构建意图。旧静态 FreeType 与旧共享 FreeType 来自不同的编译器/CPU 基线，任何候选与它们的兼容性仍保持 `unknown`。新项目不链接这些旧库；FreeType 使用最终选定工具链重建，或取自匹配 sysroot。

严格 bootstrap 的6个工具链门禁是 selected compiler、dumpmachine、sysroot、default architecture、ABI 与 float ABI。候选信息虽已达到 `scope=toolchain` 的 `confirmed-runtime`，但尚未选定，因此这6项继续为 `unknown`；总计22项严格门禁没有减少。

## 显示

| 参数 | 旧源码值 | 旧值状态 | 真实运行值 | 运行值状态 |
|---|---:|---|---:|---|
| 逻辑宽度 | 800 | `confirmed-source` | `null` | `unknown` |
| 逻辑高度 | 480 | `confirmed-source` | `null` | `unknown` |
| framebuffer 节点 | `/dev/fb0` | `confirmed-source` | `null` | `unknown` |
| LVGL 色深 / 实际 bpp | 32 | `confirmed-source` | `null` | `unknown` |
| draw buffer | 单个、全屏、384000 像素 | `confirmed-source` | 是否合适 `null` | `unknown` |
| stride/line length | 旧 flush 在 32/16/8 位路径逐行使用 | `confirmed-source` | `null` | `unknown` |
| channel layout | 未校验 | `confirmed-source` 风险 | `null` | `unknown` |
| 旋转 | 旧应用未配置 | `confirmed-source` | `null` | `unknown` |

必须实测 bpp、RGB/BGR/alpha 位域、stride、x/y offset、`smem_len`、`FBIOBLANK` 支持和物理方向。旧 fbdev 的 24bpp 分支按四字节寻址，且没有可靠的 `MAP_FAILED` 状态传播和 `munmap()`；只能参考初始化/逐行 flush 思路，不能原样复制。

## 触摸

| 参数 | 旧源码值 | 旧值状态 | 真实运行值 | 运行值状态 |
|---|---:|---|---:|---|
| 设备节点 | `/dev/input/event0` | `confirmed-source` | `null` | `unknown` |
| X 范围 | 0～1024 | `confirmed-source` | `null` | `unknown` |
| Y 范围 | 0～600 | `confirmed-source` | `null` | `unknown` |
| 交换 X/Y | 否 | `confirmed-source` | `null` | `unknown` |
| X/Y 翻转 | 无独立旧设置 | `confirmed-source` | `null` | `unknown` |
| 自动发现 | 否 | `confirmed-source` | 新实现策略待定 | `unknown` |
| 输入协议 | 旧驱动识别部分 REL、ABS、MT、BTN 事件 | `confirmed-source` | `null` | `unknown` |

必须确认实际 event 节点、ABS_X/Y、ABS_MT_POSITION_X/Y、ABS_MT_SLOT、BTN_TOUCH、tracking ID 语义、轴范围和四角校准。board probe 不读事件流，所以这些不会被自动“确认”。旧 tracking ID 判断仅把 0 当按下，必须重写。

## 字体与资源

- FreeType 在旧 `lv_conf.h` 中启用：`confirmed-source`。
- 旧字体路径 `/font/simkai.ttf`：`confirmed-source`；许可证：`unknown`。
- 旧字体、FreeType `.a/.so`、图片、对象和业务资源均不进入本包。
- 新 CJK 字体、许可、校验和与可配置部署路径：`unknown`，必须在具体项目中确认。
- FreeType 构建策略“最终工具链重建或匹配 sysroot”：`inferred` 的安全决策。

## 通用网络基线

以下来自当前 PRD、头文件、实现与已完成回归，可作为平台架构约束，而不是医疗业务模板：

- UI 回调不阻塞 socket；独立网络工作线程；有界请求/结果队列。
- UTF-8 单行 JSON，以真实 `\n` 字节分帧。
- 单帧最大 4096 字节，每连接接收缓冲区 8192 字节。
- 完整发送与增量分帧接收，处理半包、连续帧和断开恢复。
- 不把 `clinic_*` 动作、科室、医生或排队接口写入通用模板。

## 部署

运行用户、应用目录、持久化目录、字体目录、启动机制和设备权限全部为 `unknown`。旧 `/root/ticket_terminal`、`/user.txt`、`/save_pwd.txt` 与 `/font/simkai.ttf` 只是危险旧路径证据，不是候选部署默认值。

完整字段、逐项风险和验证命令见 JSON；关键未知项及升级流程见 [S5P6818_KNOWN_UNKNOWNS.md](./S5P6818_KNOWN_UNKNOWNS.md)。
