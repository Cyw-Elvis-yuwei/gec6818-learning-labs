# GEC6818 平台复用包

## 中文判断提示

- 当前状态：GEC6818 实机环境探针、最终交叉工具链确认、最小 LVGL framebuffer 显示、最小 LVGL 触摸输入、LVGL 中文字体最小验证、板端最小 TCP 网络和 LVGL 后台网络线程已完成
- 这是什么意思：板端与工具链事实已有实机证据；显示、触摸、中文字体、最小 TCP ping/pong 以及 LVGL 主线程与后台网络线程协作均已通过实机验证。
- 是否还需要继续讨论：不需要重复验证工具链、显示、触摸、中文字体、最小 TCP 或后台线程；下一阶段直接进入 LVGL 医疗登录页骨架。
- 建议下一步：只实现 LVGL 医疗登录页骨架，不提前接入真实注册、登录或其他医疗业务请求。
- 还缺什么：医疗 UI 和板端真实医疗业务联调；`/font/simkai.ttf` 的存在和本次板端验证不代表字体许可已经明确。

这个目录是跨项目复用的 GEC6818 平台信息包，不是 LVGL 应用，也不是旧 `six` 工程的副本。以后创建新项目时，应以 [gec6818_profile.json](./gec6818_profile.json) 为唯一机器可读事实源，不再扫描旧 `six` 工程。

本包不包含 LVGL 源码、FreeType 二进制、字体、图片、旧登录/商品/支付/管理员业务、旧对象文件或旧 `demo`。脚本不会下载依赖、运行 `make`、安装软件、连接开发板、执行旧程序，也不会访问 framebuffer 或触摸事件流进行读写测试。

## 信息状态

| 状态 | 含义 | 能否作为新项目硬件配置 |
|---|---|---|
| `confirmed-runtime` | 已在 `scope` 指定的环境中由命令直接确认 | 只有字段作用域匹配时可用；板级字段要求 `scope=board`，工具链字段要求 `scope=toolchain` |
| `confirmed-source` | 旧工程源码直接写明 | 只能作为旧默认值或设计证据，不能冒充硬件实测值 |
| `inferred` | 由证据推导出的建议或判断 | 需要复核，不能替代关键运行参数 |
| `unknown` | 当前无法确认 | 不得使用 |

旧源码能够确认 LVGL 8.3.0、逻辑 800×480、`/dev/fb0`、LVGL 32 位色、单个全屏 draw buffer、`/dev/input/event0` 和 0～1024/0～600 触摸范围。除 LVGL 源码/API 代际外，这些都是 `confirmed-source`，不是当前开发板运行事实。

`scope=host-backend` 仅保存参考医疗后端的主机验证证据，永远不能满足 GEC6818 板级或工具链门禁。当前板级运行事实和最终工具链已经由独立的实机与工具链证据确认，最小 LVGL framebuffer 显示、最小 LVGL 触摸输入、LVGL 中文字体最小验证、板端最小 TCP 网络和 LVGL 后台网络线程也已有独立实机证据；这些证据不自动升级尚未验证的医疗 UI 或真实医疗业务请求字段。

## 当前已确认的实机与工具链

### GEC6818 实机环境

| 项目 | 已确认值 |
|---|---|
| 架构 | `armv7l` |
| SoC | `s5p6818` |
| 内核 | `Linux 3.4.39-gec` |
| glibc | `2.23` |
| 动态加载器 | `/lib/ld-linux.so.3` |
| framebuffer | `/dev/fb0` |
| framebuffer 位深 | 32 bpp |
| framebuffer virtual size | `800,1440` |
| 实际显示目标 | 800×480 |
| 触摸屏 | `gslX680` |
| 输入节点 | `/dev/input/event0` |
| 板端字体路径 | `/font/simkai.ttf` |
| 开发板 IP | `192.168.10.42` |

`/font/simkai.ttf` 已确认在板端使用路径中存在，但许可状态仍未知，不得从参考环境复制到其他交付物。触摸控制器和事件节点已经确认；后续最小触摸实机验证进一步确认原始 X 范围 0～1024、原始 Y 范围 0～600 映射到 800×480 后点击位置正确且无明显偏移。

### 最终交叉编译器

```text
/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc
```

| 项目 | 已确认值 |
|---|---|
| target | `arm-none-linux-gnueabi` |
| sysroot | `/usr/local/arm/5.4.0/usr/arm-none-linux-gnueabi/sysroot` |
| ELF | 32-bit ARM EABI5 |
| interpreter | `/lib/ld-linux.so.3` |
| 最低记录符号版本 | `GLIBC_2.4` |
| CPU/架构目标 | Cortex-A15 / ARMv7 |

### 实机 ABI 运行证据

```text
GEC6818_ABI_OK
pointer_bits=32
int_bits=32
long_bits=32
probe_exit=0
```

该结果证明由最终工具链生成的最小探针能够通过 `/lib/ld-linux.so.3` 在目标板运行，并符合 32 位 ARM 用户态 ABI。

### 已验证部署链路

Ubuntu 编译 → VMware 共享目录 → Windows PowerShell 使用 `scp -O` → `/IOT` → 开发板运行。

### 最小 LVGL framebuffer 显示

- 工程目录：`board/lvgl_smoke/`。
- LVGL 8.3.0 源码：`reference/six/six/lvgl`。
- framebuffer 驱动：`reference/six/six/lv_drivers/display/fbdev.c`。
- framebuffer：`/dev/fb0`。
- 显示参数：800×480、32 bpp，draw buffer 为 800×40 像素。
- 构建产物：`build/board/clinic_lvgl_smoke`。

首次构建时，Makefile 的 LVGL 源文件路径处理丢失目录信息，编译器只收到 `lv_disp.c` 等 basename，导致大量 `No such file or directory`。该故障仅修改 `board/lvgl_smoke/Makefile`；改为保留每个源文件的完整相对路径后构建成功。

构建产物经验证为 ELF 32-bit ARM EABI5，interpreter 为 `/lib/ld-linux.so.3`，GNU/Linux ABI 为 3.2.0。

部署继续使用 Ubuntu 交叉编译 → VMware 共享目录 → Windows PowerShell 使用 `scp -O` → GEC6818 `/IOT/clinic_lvgl_smoke`。实机 LCD 正确显示纯色背景和居中的 `GEC6818 LVGL OK`，未出现花屏、偏移、裁切或崩溃；Ctrl+C 正常停止，`run_exit=0`。

### 最小 LVGL 触摸输入

- `board/lvgl_smoke/` 已加入最小触摸验证。
- 触摸屏：`gslX680`。
- 输入设备：`/dev/input/event0`。
- 输入链路：evdev → LVGL pointer input → `LV_EVENT_CLICKED`。
- 原始 X 坐标范围：0～1024。
- 原始 Y 坐标范围：0～600。
- 坐标映射目标：800×480。
- 实机上 `TOUCH ME` 按钮显示正常，点击后同一标签变为 `TOUCH OK`。
- 点击位置正确且无明显偏移，framebuffer 与触摸输入同时工作，程序运行稳定。

### LVGL 中文字体最小验证

- `board/lvgl_smoke/` 已完成中文字体最小验证。
- 开发板字体：`/font/simkai.ttf`。
- FreeType 运行库：`/IOT/libfreetype.so.6`。
- 使用 LVGL 8.3 FreeType 支持。
- 实机运行环境包含：`LD_LIBRARY_PATH=/IOT:/lib:/usr/lib`。
- 实机显示内容：`医路通 中文显示正常`。
- 中文显示清晰，无方框、乱码或缺字。
- framebuffer 正常，`TOUCH ME` 按钮仍可点击并正常变为 `TOUCH OK`。
- 中文字体和触摸输入可以同时工作，程序运行稳定。

### 板端最小 TCP 网络验证

- 已创建 `board/net_probe/main.c` 和 `board/net_probe/Makefile`。
- 构建产物：`build/board/clinic_net_probe`。
- Ubuntu 服务器地址：`192.168.10.41`；GEC6818 地址：`192.168.10.42`；TCP 端口：`9000`。
- 双向 ping 已通过，丢包率为 0%。
- 实际链路：GEC6818 → TCP → Ubuntu epoll `clinic_server` → ping JSON → pong JSON。

开发板实机输出：

```text
{"ok":true,"type":"pong","request_id":1,"message":"clinic server is alive"}
BOARD_TCP_PONG_OK
probe_exit=0
```

- ARM 网络客户端能够连接 Ubuntu 服务器，换行 JSON 发送和接收正常。
- 单一 5 秒截止时间机制正常，板端与主机端协议兼容。
- 开发板没有 `ss` 命令。
- 板端输出的 `server stopped` 是 `||` 分支结果，不能作为 Ubuntu 服务停止证据；服务监听状态必须在 Ubuntu 检查。

### LVGL 后台网络线程验证

- `board/lvgl_smoke/` 已完成 LVGL 后台网络线程验证。
- LVGL 点击回调只创建 pthread，不执行阻塞网络；后台线程执行 TCP ping/pong，且不调用任何 `lv_*` API。
- `pthread_mutex_t` 保护共享状态，LVGL 主线程更新 UI 并通过 `pthread_join()` 回收线程。
- UI 状态为 `等待检测`、`连接中...`、`服务器在线`、`连接失败`。
- 服务器在线时检测成功，同一进程能够连续多次成功检测。
- 服务器关闭时显示 `连接失败`，失败后仍可重新检测。
- 网络请求期间触摸和界面不阻塞，Ctrl+C 正常退出。
- 调试最初误判为线程无法复用；日志证明线程创建、运行、回收和标志清理均正常，实际问题是网络返回过快导致 `RUNNING` 未被主循环显示。
- 最终在 LVGL 点击回调中立即显示 `连接中...`，后台线程仍不操作 LVGL。
- 临时 `[CLICK]`、`[WORKER]`、`[MAIN]`、`[UI]` 日志已删除。
- 日志清理后重新交叉构建成功：`build_exit=0`；产物为 ELF 32-bit ARM EABI5，interpreter 为 `/lib/ld-linux.so.3`。

### 当前边界

- 主机端完整业务闭环：完成。
- GEC6818 实机探针：完成。
- 最终工具链确认：完成。
- LVGL 最小显示：完成。
- LVGL 最小触摸输入：完成。
- LVGL 中文字体最小验证：完成。
- 板端最小 TCP 网络：完成。
- LVGL 后台网络线程：完成。
- 医疗 UI：未开始。
- 板端真实医疗业务请求：未开始。
- 下一唯一阶段：LVGL 医疗登录页骨架。

## 标准使用流程

在工具链所在的 Linux/VMware 环境中运行：

```sh
sh scripts/probe_toolchain.sh
```

在真实 GEC6818 开发板上，由操作者主动运行只读探针并保存报告：

```sh
sh scripts/probe_gec6818_board.sh --output gec6818_runtime_report.txt
```

随后人工或在后续 Codex 阶段逐字段核对原始输出，将 `gec6818_profile.json` 中被命令直接证明的值更新为 `confirmed-runtime`，并填写匹配的 `scope`。不要整组批量升级，也不要用文件名、板卡商品名、旧二进制属性或主机后端测试代替板级/工具链证据。

校验 profile：

```sh
sh scripts/validate_profile.sh
```

关键字段全部通过后创建独立项目骨架：

```sh
sh scripts/bootstrap_gec6818_lvgl8_project.sh /path/to/new_project
```

本 README 已记录当前项目的实机和工具链确认结果，但脚本仍以 `gec6818_profile.json` 为机器可读输入。若 profile 中仍有与本阶段证据尚未同步的关键字段，校验和 bootstrap 仍会默认停止；不得通过 `--allow-unverified-defaults` 把未同步字段误写成已验证事实。仅在明确接受“旧源码默认值不是硬件事实”的研究场景下，才可生成占位骨架：

```sh
sh scripts/validate_profile.sh --allow-unverified-defaults
sh scripts/bootstrap_gec6818_lvgl8_project.sh --allow-unverified-defaults /path/to/new_project
```

这个参数只放宽“生成文件”，不会升级 canonical profile 的状态，也不会让项目具备可编译、可运行或可部署的含义。生成的 README、头文件和 Make 示例会保留醒目的 `UNVERIFIED` 标记；编译器不会被静默选中，无法从旧源码确认的 stride、像素格式和旋转仍使用显式无效占位。

## 探针边界

`probe_gec6818_board.sh` 只读取系统信息、设备节点元数据、fb0 sysfs、可选的 `fbset -i` 输出和 `/proc/bus/input/devices`。它不运行 `evtest`，不打开 `/dev/input/event*` 读取事件，不写 `/dev/fb*`，因此该探针本身不能证明触摸交互。当前项目的触摸结论来自后续独立的最小 LVGL 实机点击验证：0～1024、0～600 映射到 800×480 后点击位置正确且无明显偏移。

`probe_toolchain.sh` 只检查以下三个候选：

- `/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc`
- `/usr/bin/arm-linux-gnueabi-gcc`
- `arm-linux-gcc`

它会报告解析路径、版本、目标 triplet、sysroot、include 搜索、libgcc 与 float ABI 信息，并在系统临时目录执行最小 C 文件的“仅编译 `-c`”检查。它不链接，不使用旧 LVGL 或旧 FreeType，并在退出时删除临时文件。空的 `-print-sysroot` 输出只是一次真实观测，不证明目标 libc 或链接环境完整。

2026-07-13 的 VMware 原始探针结果保存在 `evidence/toolchain_probe_vmware_20260713T103421Z.log`，其 SHA-256 与候选字段已记录在 profile。该记录属于候选阶段的历史证据；最终已选定 `/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc`，并由板端 ABI 探针完成运行确认。

## 固定的平台原则

- 新项目保持 LVGL 8 API；推荐从干净、可追溯的 LVGL 8.3.0 源码开始，不混入 LVGL 9 API。
- fbdev 只能在实际 bpp、channel offsets、stride、offset、映射长度和旋转确认后实现。旧驱动存在 24bpp 按四字节复制、颜色通道未校验、`MAP_FAILED` 处理不足、无 `munmap()` 和初始化失败不可传播等风险。
- evdev 节点不得写死。必须确认 ABS/MT 能力、BTN_TOUCH、slot/tracking ID、轴范围和方向；旧驱动把 tracking ID 仅等于 0 当作按下，不能照搬。
- FreeType 必须使用最终工具链/sysroot 重建，或来自与其匹配的 sysroot。旧 `/font/simkai.ttf` 许可未知，禁止复制；新项目应选择许可明确、路径可配置的 CJK 字体。
- LVGL UI 回调不得执行阻塞 socket。通用终端层采用网络工作线程及有界请求/结果队列；LVGL 对象只在 UI 线程更新。
- 参考协议是 UTF-8 单行 JSON，以真实换行字节 `\n` 结束，单帧最大 4096 字节、每连接接收缓冲区 8192 字节，并要求完整发送与分帧接收。这些是参考实现约束，不是 GEC6818 硬件极限。
- 通用模板不得硬编码医疗业务接口。

## 目录内容

```text
gec6818_platform_kit/
├── README.md
├── GEC6818_PLATFORM_PROFILE.md
├── GEC6818_KNOWN_UNKNOWNS.md
├── gec6818_profile.json
├── config/
├── evidence/
├── scripts/
└── templates/lvgl8_minimal/
```

`config/` 中的旧源码值都使用 `SOURCE_` 或明确状态名，不会成为 active runtime 值。`templates/` 是不可直接运行的最小占位模板；bootstrap 只把模板和 profile 投影到新的空目录。
`evidence/` 保存不可改写的原始探针报告；报告中的 `scope` 决定它能证明哪一类字段。

## 更新为 confirmed-runtime

每次更新一个字段时：

1. 保存原始 probe 报告，并记录板卡、rootfs、内核或编译器身份与观测时间。
2. 确认命令输出直接证明该字段，而且证据作用域匹配：显示/输入使用 `board`，编译器使用 `toolchain`；间接推断仍保持 `inferred`。
3. 更新 `value`、`status`、`scope`、`source`、`risk` 和 `verification_command`，并在 `evidence_sources` 增加报告来源。
4. 重新运行 `validate_profile.sh`。
5. 更换板卡、LCD/触摸模组、内核、rootfs 或工具链后，重新验证受影响字段。

## 未来给 Codex 的标准提示词

> 读取 `gec6818_platform_kit/gec6818_profile.json` 和 `README.md`，仅使用与字段匹配的 `scope=board` 或 `scope=toolchain` 的 `confirmed-runtime` 平台关键值创建 GEC6818 项目。`scope=host-backend` 只属于参考项目证据。不得重新扫描旧 six 工程，不得使用 `unknown` 参数，不得复制旧业务源码、旧对象、旧库、旧字体或旧图片。若完成目标所需的关键字段尚未达到作用域匹配的 `confirmed-runtime`，请停止并列出所需验证，不要自行猜测。
