# GEC6818 已知未知项

## 中文判断提示

- 当前状态：严格 bootstrap 被关键未知项阻断
- 这是什么意思：候选工具链信息已确认，但最终选择和全部板端字段仍未确认；当前只能生成带警告的研究骨架。
- 是否还需要继续讨论：不需要猜测参数；需要收集命令输出和物理交互结果。
- 建议下一步：在真实开发板保存 board probe 原始报告，再用其 rootfs/CPU 信息选择工具链。
- 还缺什么：板端报告、最终工具链选择、触摸四角实测、许可字体和部署决策。

本文件解释 [gec6818_profile.json](./gec6818_profile.json) 中的 `unknown`。JSON 是唯一事实源；本文不提供可直接复制到代码中的替代默认值。

## 默认阻断 bootstrap 的字段

| 范围 | JSON 字段 | 为什么阻断 | 直接证据 |
|---|---|---|---|
| 工具链 | `toolchains.selected_compiler` | 三个候选已探测，但没有板端 rootfs 证据支持最终选择 | 比较候选实际路径、triplet、sysroot 与板端报告 |
| 工具链 | `selected_dumpmachine`、`selected_sysroot`、`selected_default_architecture`、`selected_abi`、`selected_float_abi` | 必须与被选中的同一候选绑定，不能混合三个候选的值 | 完成工具链选择后从该候选的 `scope=toolchain` 运行证据投影 |
| 显示 | `display.runtime_device` | `/dev/fb0` 只是旧默认 | 板端节点清单和 sysfs 对应关系 |
| 显示 | `runtime_width`、`runtime_height` | 800×480 只是旧逻辑值 | sysfs virtual_size 与 `fbset -i`，必要时 ioctl 结果 |
| 显示 | `runtime_bits_per_pixel` | 32bpp 未实测 | sysfs bits_per_pixel / `fbset -i` |
| 显示 | `runtime_line_length`、`runtime_pixel_format` | 错误 stride/位域会错行或错色 | sysfs stride、`fbset -i` 或只读 ioctl 报告 |
| 显示 | `runtime_rotation` | 显示与触摸必须同向 | 非对称画面和物理方向人工记录 |
| 输入 | `input.runtime_device` | event 编号不稳定 | `/proc/bus/input/devices` 与设备能力对应 |
| 输入 | `runtime_x/y_min/max` | 旧 0～1024/0～600 未实测 | 选定节点的 EVIOCGABS 能力 |
| 输入 | `runtime_swap_xy`、`runtime_invert_x/y` | 由安装方向决定 | 最终显示方向下的四角测试 |
| 输入 | `runtime_event_protocol` | ABS、MT A、MT B 状态机不同 | 能力位及短时受控触摸诊断 |

严格校验要求上述字段为作用域匹配的 `confirmed-runtime`：工具链字段必须为 `scope=toolchain`，显示和输入字段必须为 `scope=board`。`--allow-unverified-defaults` 仅允许脚本生成显式 `UNVERIFIED` 文件；不会把这些字段改为已确认，也不会自动选择候选编译器。22项门禁保持不变。

2026-07-13 已确认三个候选编译器均存在并能完成最小 `-c` 测试，但这只更新各候选自己的运行字段。由于板端尚未探测，`toolchains.selected_*` 继续为 `unknown`，候选值不得拼接成一个虚构的“已选工具链”。

## 板端 probe 后仍可能未知

board probe 故意不读取 `/dev/input/event*` 事件流，也不运行会持续阻塞的 `evtest`。因此以下通常还需要用户明确授权的、限时且可中止的交互验证：

- ABS_X/Y 与 ABS_MT_POSITION_X/Y 的实际最小/最大值；
- 是否使用 MT protocol A 或带 ABS_MT_SLOT 的 protocol B；
- BTN_TOUCH 与 tracking ID 的按下/释放语义；
- X/Y 交换、逐轴翻转、中心及四角准确性；
- 触摸方向与最终显示旋转的一致性。

sysfs 或 `fbset` 也可能不完整展示 channel offsets、`smem_len`、x/y offset 或 `FBIOBLANK` 行为。缺少直接输出时继续保持 `unknown`，不能用“常见配置”补齐。

## 非 bootstrap 配置但仍需确认

- CPU/SoC、内核版本、32/64 位、libc、BusyBox/Buildroot 身份。
- 运行用户、设备节点权限、挂载点、持久化目录、可用空间与启动机制。
- 全屏 draw buffer 的 RAM 和刷新性能是否合理。
- 许可明确的 CJK 字体、来源、版本、校验和及可配置部署路径。
- FreeType 的最终构建版本、配置、sysroot 和目标 ELF 属性。
- `FBIOBLANK` 是否支持，以及 adapted fbdev 的错误/清理路径。

这些字段可能不阻止“生成骨架”，但会阻止把骨架称为可部署工程。

## 状态升级规则

1. 保存原始报告，不覆盖旧报告；记录观测日期及板卡/rootfs/工具链身份。
2. 将报告作为 `evidence_sources` 新项加入 profile。
3. 一次只更新被输出直接证明的字段：`value` 使用正确 JSON 类型，`status` 改为 `confirmed-runtime`，`scope` 必须与字段匹配，`source` 指向报告和具体段落。主机后端测试只能使用 `scope=host-backend`，不得升级任何平台字段。
4. 仍需解释的字段用 `inferred`，缺证据继续用 `null` + `unknown`。
5. `risk` 保留适用限制，例如“只证明 compile-only，不证明链接/运行”。
6. 运行 `sh scripts/validate_profile.sh`，修复所有结构、作用域和关键未确认项。
7. 更换 LCD/触摸、内核、rootfs、编译器或 sysroot 后，将受影响的旧运行证据视为过期并重新探测。

特别说明：`-print-sysroot` 的空输出可以作为“该命令本次输出为空”的运行证据，但不证明编译器拥有完整、正确的隐式目标 libc。最小 `-c` 通过也只证明编译阶段，不能证明链接、FreeType 或板端运行兼容。

## 禁止用来填充 unknown 的内容

- 板卡名称、编译器文件名或目录名；
- 旧 `demo`、旧 `.o`、旧 FreeType `.a/.so` 的架构属性；
- 旧 `/dev/fb0`、32bpp、`event0` 和触摸范围；
- 网络上找到的“典型 GEC6818 参数”；
- 无许可来源的旧 `simkai.ttf`；
- 旧部署脚本中的 `/root` 或根目录数据文件。
