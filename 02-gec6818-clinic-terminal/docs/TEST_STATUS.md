# 医路通测试状态

更新时间：2026-07-24 20:17（Asia/Hong_Kong）

## 中文判断提示

- 当前状态：项目完成边界审查与最终验收材料已整理，当前阶段完成，等待用户确认当前范围是否收口。
- 这是什么意思：既有用户构建和 GEC6818 功能确认已按证据边界归档；本轮只同步材料，不产生新的功能验证结果。
- 是否还需要用户提供信息：需要用户确认最终验收边界；若接受当前范围，不再进入新的代码阶段。
- 建议下一步：用户确认最终验收边界与项目收口。
- 还缺什么：无号单、`COMPLETED`、`CANCELLED`、重复取号和超一屏滚动仍无专项实机证据；部分用户确认的原始命令和逐项日志未保留。

## 证据规则

- 只有实际执行命令且取得退出码或明确输出的测试，才记录为 PASS。
- 测试源码、Makefile 目标或可执行文件存在，不等于测试已经通过。
- 当前状态依据用户提供的 Ubuntu 实际命令输出和阶段报告维护。

## A. 取号加入前的历史全量回归

| 项目 | 记录 |
| --- | --- |
| 命令 | `make -B test` |
| 结果 | **19/19 PASS** |
| 编译结果 | 0 errors，0 warnings |
| 执行时点 | 医生列表阶段结束后、取号模块加入前 |
| 可证明范围 | 当时的注册登录、科室、医生、协议、服务器和主机链路通过全量回归 |
| 不可证明范围 | 不能证明当前含取号代码的完整仓库仍然全量通过 |

明确结论：该次全量回归发生在取号模块加入前，是有效历史证据；全部主机后端功能完成后的最终聚合回归证据见 J 节。

### Windows → Ubuntu 应用层联调历史证据

- Ubuntu epoll 服务器监听 `0.0.0.0:9000`。
- Windows 客户端先后两次连接 Ubuntu 服务器，两次请求均返回 pong。
- 客户端断开后，Ubuntu 服务器仍继续运行。
- 本记录未保留完整命令和退出码，不补写未保存的执行细节。

## B. 取号窄范围测试

以下测试均由用户在 VMware Ubuntu 的 `/mnt/hgfs/codex` 终端实际构建并运行；退出码均为 0。对应构建使用 `-std=c11 -Wall -Wextra -Wpedantic -Werror`。

| 测试 | 结果 | 证明范围 |
| --- | --- | --- |
| `test_ticket_store` | PASS，exit 0 | 取号数据模型与 SQLite Store 的定向行为 |
| `test_doctor_store` | PASS，exit 0 | 新增 Store 接口后的医生 Store 回归 |
| `test_ticket_core` | PASS，exit 0 | 取号请求到 Store 和结构化响应的 Core 行为 |
| `test_doctor_core` | PASS，exit 0 | 取号 Core 接入后的医生 Core 回归 |
| `test_ticket_json` | PASS，exit 0 | `create_ticket` 解码和 ticket 响应编码 |
| `test_doctor_json` | PASS，exit 0 | 取号 JSON 接入后的医生 JSON 回归 |
| `test_ticket_handler` | PASS，exit 0 | Handler→JSON→Core→Store→JSON 的离线取号链路 |

这些结果证明取号各层的窄范围目标及列出的医生模块回归项通过。取号真实 TCP 与客户端闭环由下一节的独立实际运行证据证明；这些窄范围结果仍不代表完整全量回归或板端运行通过。

## C. 取号真实 TCP 与主机客户端

执行环境：VMware Ubuntu，工作目录 `/mnt/hgfs/codex`。

实际执行命令：

```bash
make -B \
  build/linux/clinic_ticket_client \
  build/test/test_tcp_ticket

./build/test/test_tcp_ticket
```

| 项目 | 实际结果 |
| --- | --- |
| 构建退出码 | `make_exit=0` |
| TCP 测试退出码 | `tcp_ticket_exit=0` |
| 测试输出 | `TCP ticket tests passed` |
| 编译错误 | 0 |
| 编译警告 | 0 |

本次实际运行证明：`clinic_ticket_client` 可以通过真实 TCP 连接 Linux epoll 服务器，完成注册测试用户、创建 ticket、重复取号幂等、`USER_NOT_FOUND` 错误和服务器存活检查，并清理测试服务器与临时数据库。

本次没有执行加入取号功能后的完整 `make test`，因此不得将本次定向通过或历史 `19/19` 解释为当前仓库的完整回归通过。

## D. 排队状态查询（get_ticket）定向验证

执行环境：VMware Ubuntu，工作目录 `/mnt/hgfs/codex`。

### Store

实际执行：

```bash
make -B build/test/test_ticket_store build/test/test_ticket_core
./build/test/test_ticket_store
./build/test/test_ticket_core
```

| 项目 | 实际结果 |
| --- | --- |
| 构建 | `make_exit=0` |
| Store 测试 | `ticket store tests passed`，`ticket_store_exit=0` |
| Core 测试 | `ticket core tests passed`，`ticket_core_exit=0` |

Store 测试覆盖按 ID 返回完整 ticket、`CLINIC_STORE_TICKET_NOT_FOUND`、NULL `called_time` 映射、失败清零和重开数据库后的持久化查询。

### Core 与医生 Core 回归

实际执行：

```bash
make -B build/test/test_ticket_core build/test/test_doctor_core
./build/test/test_ticket_core
./build/test/test_doctor_core
```

| 项目 | 实际结果 |
| --- | --- |
| 构建 | `make_exit=0` |
| get_ticket Core | `ticket core tests passed`，`ticket_core_exit=0` |
| 医生 Core 回归 | `doctor core tests passed`，`doctor_core_exit=0` |

### JSON 与医生 JSON 回归

构建前实际删除旧的 ticket JSON 测试产物，然后重新构建并运行：

```bash
rm -f build/test/test_ticket_json
make -B build/test/test_ticket_json build/test/test_doctor_json
./build/test/test_ticket_json
./build/test/test_doctor_json
```

| 项目 | 实际结果 |
| --- | --- |
| 构建 | `make_exit=0` |
| get_ticket JSON | `ticket JSON tests passed`，`ticket_json_exit=0` |
| 医生 JSON 回归 | `doctor JSON tests passed`，`doctor_json_exit=0` |

曾实际出现并已修复一项兼容性编译故障：当前 Ubuntu cJSON 没有声明 `cJSON_ParseWithLengthOpts()`，在 `-Werror` 下导致编译失败。实现随后改为长度受控临时缓冲区配合 `cJSON_ParseWithOpts()`；删除旧二进制后重新构建，上述 JSON 测试均通过。

### Handler 离线链路

实际执行：

```bash
make -B build/test/test_ticket_handler
./build/test/test_ticket_handler
```

- 构建成功，无警告、无错误。
- 测试输出：`ticket handler tests passed`，并正常返回 shell。
- 本次没有打印独立 exit code，因此不记录虚构的 `exit=0` 文本证据。
- 该结果证明 get_ticket 可通过现有通用 Handler 完成离线 JSON→Core→Store→JSON 查询；该条离线证据本身不证明真实 TCP，真实 TCP 证据见下一节。

### 真实 TCP 与主机查询客户端

第一次构建发现 `tests/test_tcp_ticket.c` 约第 716 行存在格式字符串错误：错误形式为 `"% PRId64`，正确形式为 `"%" PRId64`。第一次构建失败后运行到的是旧测试二进制，因此那次测试输出不作为 get_ticket 新 TCP 代码的验证证据。

正式验证前实际删除旧测试二进制：

```bash
rm -f build/test/test_tcp_ticket
```

随后实际构建：

```bash
make -B build/linux/clinic_ticket_status_client build/test/test_tcp_ticket
```

构建结果：`make_exit=0`。本次构建确认以下目标均编译成功：

- `clinic_ticket_status_client`
- `clinic_server`
- `clinic_ticket_client`
- `test_tcp_ticket`

正式测试：

```bash
./build/test/test_tcp_ticket
```

| 项目 | 实际结果 |
| --- | --- |
| 测试输出 | `TCP ticket tests passed` |
| 测试退出码 | `tcp_ticket_exit=0` |

本次定向测试证明 get_ticket 状态查询客户端可以通过真实 TCP、epoll server、Handler、Core 和 SQLite 取得并校验 ticket JSON response，且新的 TCP ticket 测试通过。本次没有执行加入 ticket/get_ticket 后的完整 `make test`，不能描述为当前全量回归通过。

## E. 管理员 call_next Store 验证

### 定向验证

实际构建：

```bash
make -B build/test/test_ticket_store build/test/test_doctor_store
```

实际运行及输出：

```bash
./build/test/test_ticket_store
ticket store tests passed

./build/test/test_doctor_store
doctor store tests passed
```

该定向验证证明管理员 `call_next` Store 行为及医生 Store 回归通过。

### 首次完整回归异常

第一次执行 `make -B test` 时，`test_ticket_core` 出现 24 个失败。后续检查确认：

- 测试开始前和结束后均删除测试数据库。
- 没有残留 `ticket_core` 数据库。
- `test_ticket_core` 单独连续执行两次均通过。
- ASan/UBSan 构建成功。
- ASan/UBSan 运行通过。
- 未发现内存错误。
- 排查过程中未修改源码。
- 故障当前无法复现，不记录虚构根因。

### 正式完整复跑

清理 `ticket_core` 测试数据库后实际执行：

```bash
make test
```

最终结果：`full_test_exit=0`。

准确结论：

- 当前 Makefile 定义的 `make test` 聚合测试通过。
- `test_ticket_handler` 和 `test_tcp_ticket` 不在本次聚合输出中。
- `test_ticket_handler` 和 `test_tcp_ticket` 此前已有独立定向通过证据。

该条记录保留为历史回归证据；其旧阶段边界已由 J 节的最终主机回归结果替代。

## F. 管理员 call_next Core 定向验证

实际构建：

```bash
make -B build/test/test_ticket_core build/test/test_doctor_core
```

两个目标均成功编译，无错误、无警告。

实际运行及输出：

```bash
./build/test/test_ticket_core
ticket core tests passed

./build/test/test_doctor_core
doctor core tests passed
```

准确边界：

- 本节证明 `call_next` Core 修改后的 ticket Core 和 doctor Core 定向回归通过。
- 本节原有的完整回归时间边界已由 J 节最终主机回归结果替代。

## G. 管理员 call_next JSON 定向验证

实际构建：

```bash
make -B build/test/test_ticket_json build/test/test_doctor_json
```

两个目标均强制重新编译成功，无错误、无警告。

实际运行及输出：

```bash
./build/test/test_ticket_json
ticket JSON tests passed

./build/test/test_doctor_json
doctor JSON tests passed
```

准确边界：

- 本节证明 `call_next` JSON 修改后的 ticket JSON 与 doctor JSON 定向验证通过。
- 本节原有的完整回归时间边界已由 J 节最终主机回归结果替代。

## H. 管理员 call_next Handler 离线链路定向验证

ticket Handler 实际构建：

```bash
make -B build/test/test_ticket_handler
```

强制重新编译成功，无错误、无警告。

实际运行及输出：

```bash
./build/test/test_ticket_handler
ticket handler tests passed
```

本次未单独打印退出码，但程序正常返回 shell。

doctor Handler 实际构建：

```bash
make -B build/test/test_doctor_handler
```

强制重新编译成功，无错误、无警告。

实际运行及输出：

```bash
./build/test/test_doctor_handler
doctor handler tests passed
```

退出状态：`doctor_handler_exit=0`。

准确边界：

- 本节证明 `call_next` Handler 修改后的 ticket Handler 与 doctor Handler 定向验证通过。
- 本节原有的完整回归时间边界已由 J 节最终主机回归结果替代。

## I. 管理员 call_next 真实 TCP 与 CLI 定向验证

实际构建：

```bash
make -B build/linux/clinic_admin_call_client build/test/test_tcp_ticket
```

本次成功编译：

- `clinic_admin_call_client`
- `clinic_server`
- `clinic_ticket_client`
- `clinic_ticket_status_client`
- `test_tcp_ticket`

实际运行及输出：

```bash
./build/test/test_tcp_ticket
TCP ticket tests passed
```

### 无关终端输入错误

- 曾误输入 `./build/test/test_tcp_ticketcd`。
- Bash 返回 `No such file or directory`。
- 这是命令粘贴错误，不是源码或测试失败。
- 随后重新构建并正确执行测试通过。

准确边界：

- 本节证明 `call_next` TCP 和管理员 CLI 定向验证通过。
- 本节原有的完整回归时间边界已由 J 节最终主机回归结果替代。

## J. 最终主机回归

### 当前 Makefile 聚合回归

实际执行：

```bash
make -B test
```

结果：

- 所有 Makefile 聚合目标重新编译成功。
- protocol、JSON、Store、Core、Handler、TCP auth、TCP departments、TCP doctors 全部通过。
- `full_test_exit=0`。

### Ticket 补充定向测试

实际执行及结果：

```bash
./build/test/test_ticket_handler
ticket handler tests passed
```

退出状态：`ticket_handler_exit=0`。

实际执行及结果：

```bash
./build/test/test_tcp_ticket
TCP ticket tests passed
```

退出状态：`tcp_ticket_exit=0`。

### 命令粘贴问题

- 原计划执行的 `make -B build/test/test_ticket_handler build/test/test_tcp_ticket` 被误粘贴到 `echo` 命令后。
- 该次 `make` 实际没有执行。
- `ticket_build_exit=0` 是 `echo` 的退出码，不作为构建证据。
- 两个测试程序此前均已在各自功能完成后强制重新构建并通过。
- 此后没有源码修改，因此本次再次运行结果属于有效累计回归证据。

### 最终边界

- 旧的“完整 `make test` 发生在 call_next 修改之前”边界已被本次结果替代。
- 当前 Makefile 定义的完整聚合测试已经在全部主机后端功能完成后通过。
- `test_ticket_handler` 和 `test_tcp_ticket` 不在当前聚合入口中，其补充运行结果与此前强制重建证据共同构成有效累计回归证据。

## K. GEC6818 实机与工具链验证

### 板端环境证据

- 架构：`armv7l`。
- SoC：`s5p6818`。
- 内核：`Linux 3.4.39-gec`。
- C 运行库：`glibc 2.23`。
- 动态加载器：`/lib/ld-linux.so.3`。
- framebuffer：`/dev/fb0`，32 bpp，`virtual_size=800,1440`，实际显示目标按 800×480。
- 触摸屏：`gslX680`，输入节点 `/dev/input/event0`。
- 字体路径：`/font/simkai.ttf`。
- 开发板 IP：`192.168.10.42`。

### 工具链证据

最终选定编译器：

```text
/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc
```

- target：`arm-none-linux-gnueabi`。
- sysroot：`/usr/local/arm/5.4.0/usr/arm-none-linux-gnueabi/sysroot`。
- 产物格式：ELF 32-bit ARM EABI5。
- interpreter：`/lib/ld-linux.so.3`。
- 最低记录符号版本：`GLIBC_2.4`。
- 目标 CPU/架构：Cortex-A15 / ARMv7。

### 实机运行结果

```text
GEC6818_ABI_OK
pointer_bits=32
int_bits=32
long_bits=32
```

退出状态：`probe_exit=0`。

### 部署链路

Ubuntu 编译 → VMware 共享目录 → Windows PowerShell 使用 `scp -O` → `/IOT` → 开发板运行。

### 验证边界

- 主机端业务闭环：完成。
- GEC6818 实机探针：完成。
- 最终工具链确认：完成。
- LVGL 最小显示：完成。
- LVGL 最小触摸输入：完成。
- LVGL 中文字体最小验证：完成。
- 板端最小 TCP 网络：完成。
- LVGL 后台网络线程：完成。

## L. GEC6818 最小 LVGL framebuffer 显示验证

### 工程与显示配置

- 最小工程：`tools/board/lvgl_smoke/`。
- LVGL 8.3.0 源码：`reference/six/six/lvgl`。
- framebuffer 驱动：`reference/six/six/lv_drivers/display/fbdev.c`。
- framebuffer：`/dev/fb0`。
- 显示参数：800×480、32 bpp。
- draw buffer：800×40 像素。

### 首次构建故障与修复

- 首次构建时，Makefile 的 LVGL 源文件路径处理丢失目录信息，编译器只收到 `lv_disp.c` 等 basename。
- 该问题导致大量 LVGL `.c` 文件报 `No such file or directory`。
- 修复只涉及 `tools/board/lvgl_smoke/Makefile`，没有修改 `main.c`、LVGL 配置、驱动配置、LVGL 源码或 fbdev 源码。
- Makefile 保留完整源文件路径后，交叉构建成功。

### 构建产物与 ELF 证据

- 构建产物：`build/board/clinic_lvgl_smoke`。
- 产物格式：ELF 32-bit ARM EABI5。
- interpreter：`/lib/ld-linux.so.3`。
- GNU/Linux ABI：3.2.0。

### 部署与实机运行

- 部署链路：Ubuntu 交叉编译 → VMware 共享目录 → Windows PowerShell 使用 `scp -O` → GEC6818 `/IOT/clinic_lvgl_smoke`。
- LCD 正确显示纯色背景和居中的 `GEC6818 LVGL OK`。
- 未出现花屏、偏移、裁切或崩溃。
- Ctrl+C 正常停止。
- 退出状态：`run_exit=0`。

### 验证边界

- 主机端完整业务闭环：完成。
- GEC6818 工具链：完成。
- GEC6818 最小 LVGL framebuffer 显示：完成。
- GEC6818 最小 LVGL 触摸输入：完成。
- GEC6818 LVGL 中文字体最小验证：完成。
- GEC6818 板端最小 TCP 网络：完成。
- GEC6818 LVGL 后台网络线程：完成。
- LVGL 医疗登录页骨架：完成。
- 登录页之外的医疗业务页面：未开始。
- 下一唯一阶段：板端真实 login TCP 请求。

## M. GEC6818 最小 LVGL 触摸验证

### 工程与输入链路

- `tools/board/lvgl_smoke/` 已加入最小触摸验证。
- 触摸屏：`gslX680`。
- 输入设备：`/dev/input/event0`。
- 输入链路：evdev → LVGL pointer input → `LV_EVENT_CLICKED`。
- 原始 X 坐标范围为 0～1024，原始 Y 坐标范围为 0～600，映射到 800×480。

### 实机交互结果

- `TOUCH ME` 按钮显示正常。
- 点击按钮后文字变为 `TOUCH OK`。
- 点击位置正确，无明显偏移。
- framebuffer 与触摸输入同时工作。
- 程序运行稳定。

### 验证边界

- GEC6818 最小 LVGL framebuffer 显示：完成。
- GEC6818 最小 LVGL 触摸输入：完成。
- GEC6818 LVGL 中文字体最小验证：完成。
- GEC6818 板端最小 TCP 网络：完成。
- GEC6818 LVGL 后台网络线程：完成。
- LVGL 医疗登录页骨架：完成。
- 登录页之外的医疗业务页面：未开始。
- 下一唯一阶段：板端真实 login TCP 请求。

## N. GEC6818 LVGL 中文字体最小验证

### 字体与运行配置

- `tools/board/lvgl_smoke/` 已完成中文字体最小验证。
- 开发板字体：`/font/simkai.ttf`。
- FreeType 运行库：`/IOT/libfreetype.so.6`。
- 字体支持：LVGL 8.3 FreeType。
- 实机运行环境包含：`LD_LIBRARY_PATH=/IOT:/lib:/usr/lib`。
- 实机显示内容：`医路通 中文显示正常`。

### 实机显示与交互结果

- 中文显示清晰，无方框、乱码或缺字。
- framebuffer 显示正常。
- `TOUCH ME` 按钮仍可点击，点击后正常变为 `TOUCH OK`。
- 中文字体和触摸输入可以同时工作。
- 程序运行稳定。

### 验证边界

- GEC6818 最小 framebuffer 显示：完成。
- GEC6818 最小触摸输入：完成。
- GEC6818 LVGL 中文字体最小验证：完成。
- GEC6818 板端最小 TCP 网络：完成。
- GEC6818 LVGL 后台网络线程：完成。
- LVGL 医疗登录页骨架：完成。
- 登录页之外的医疗业务页面：未开始。
- 完整板端业务联调：未开始。
- 下一唯一阶段：板端真实 login TCP 请求。

## O. GEC6818 板端最小 TCP 网络验证

### 探针与网络环境

- 已创建 `tools/board/net_probe/main.c` 和 `tools/board/net_probe/Makefile`。
- 构建产物：`build/board/clinic_net_probe`。
- Ubuntu 服务器地址：`192.168.10.41`。
- GEC6818 地址：`192.168.10.42`。
- TCP 端口：`9000`。
- 双向 ping 已通过，丢包率为 0%。

### 实际链路与板端输出

GEC6818 → TCP → Ubuntu epoll `clinic_server` → ping JSON → pong JSON。

```text
{"ok":true,"type":"pong","request_id":1,"message":"clinic server is alive"}
BOARD_TCP_PONG_OK
probe_exit=0
```

### 验证结论

- ARM 网络客户端能够连接 Ubuntu 服务器。
- 换行 JSON 的发送和接收正常。
- 单一 5 秒截止时间机制正常。
- 板端与主机端协议兼容。

### 命令证据边界

- 开发板没有 `ss` 命令。
- 板端输出的 `server stopped` 是 `||` 分支结果，不能作为 Ubuntu 服务停止证据。
- 服务监听状态必须在 Ubuntu 检查。

### 当前边界

- LVGL framebuffer：完成。
- LVGL 触摸：完成。
- 中文字体：完成。
- 板端最小 TCP：完成。
- LVGL 后台网络线程：完成。
- LVGL 医疗登录页骨架：完成。
- 登录页之外的医疗业务页面：未开始。
- 板端真实医疗业务请求：未开始。
- 下一唯一阶段：板端真实 login TCP 请求。

## P. GEC6818 LVGL 后台网络线程验证

### 已验证线程模型

- `tools/board/lvgl_smoke/` 已完成 LVGL 后台网络线程验证。
- LVGL 点击回调只创建 pthread，不执行阻塞网络操作。
- 后台线程执行 TCP ping/pong，不调用任何 `lv_*` API。
- `pthread_mutex_t` 保护共享状态。
- LVGL 主线程更新 UI，并通过 `pthread_join()` 安全回收线程。

### UI 与实机结果

- UI 状态为 `等待检测`、`连接中...`、`服务器在线`、`连接失败`。
- 服务器在线时检测成功，同一进程能够连续多次成功检测。
- 服务器关闭时显示 `连接失败`，失败后仍可重新检测。
- 网络请求期间触摸和界面不阻塞。
- Ctrl+C 正常退出。

### 问题定位与修复验证

- 最初误判为线程无法重复使用。
- 运行日志证明 `pthread_create`、worker、`pthread_join` 和线程标志清理均正常。
- 实际问题是网络返回过快，`RUNNING` 状态未被主循环显示。
- 最终在 LVGL 点击回调中立即显示 `连接中...`，后台线程仍不操作 LVGL。
- 临时 `[CLICK]`、`[WORKER]`、`[MAIN]`、`[UI]` 日志已删除。

### 日志清理后构建证据

- 重新交叉构建成功：`build_exit=0`。
- 产物格式：ELF 32-bit ARM EABI5。
- interpreter：`/lib/ld-linux.so.3`。

### 当前边界

- framebuffer：完成。
- 触摸：完成。
- 中文字体：完成。
- 板端 TCP：完成。
- LVGL 后台网络线程：完成。
- LVGL 医疗登录页骨架：完成。
- 登录页之外的医疗业务页面：未开始。
- 板端真实医疗业务请求：未开始。
- 下一唯一阶段：板端真实 login TCP 请求。

## Q. GEC6818 LVGL 医疗登录页骨架实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 |
| 对应代码阶段 | LVGL 医疗登录页骨架 |
| 结果 | 用户确认按既定验收标准通过 |
| 原始输出 | 完整原始输出未在当前记录中保留 |
| 退出码 | 当前消息未保留准确的构建退出码或运行退出码，不记录虚构的 `build_exit=0` 或 `run_exit=0` |

### 用户确认的验收结果

- 用户名 K26 拼音键盘可见、可用，中文候选栏可用。
- 密码英文键盘可见、可用，密码字符保持隐藏。
- 两种键盘切换正常，点击页面空白可以收起输入区域。
- 登录按钮清晰可辨并具有按下反馈。
- 页面没有常驻的“请输入用户名和密码”校验文本。
- 用户名和密码的四种本地非空校验结果均通过模态消息框显示。
- 消息框可以关闭，关闭后已输入内容保留。
- Ctrl+C 可以正常退出。

### 证据边界

- 能够证明：登录页输入、键盘切换、模态校验提示和退出流程在 GEC6818 上可用。
- 不能证明：真实服务器认证、TCP 登录请求、SQLite 用户校验或登录成功页面跳转。
- 当前阶段只完成本地登录页骨架，不将“输入检查通过”解释为真实登录成功。

### 当前边界

- LVGL 医疗登录页骨架：当前阶段完成。
- 板端真实 login TCP 请求：当前阶段完成，已由用户在 GEC6818 实机验证。
- 下一唯一阶段：登录成功后的医疗服务主页骨架。

## R. 板端真实 login TCP 请求实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Ubuntu 服务器 / GEC6818 开发板 |
| 对应阶段 | 板端真实 login TCP 请求 |
| 保留的真实命令与响应 | Ubuntu 服务器注册：`clinicdemo / Pass1234`，返回 `{"ok":true,"request_id":1001,"user_id":1,"message":"registration successful"}`，`exit=0`；Ubuntu 服务器登录：`clinicdemo / Pass1234`，返回 `{"ok":true,"request_id":1002,"user_id":1,"message":"login successful"}`，`exit=0` |
| 板端实机确认 | 错误密码请求真实到达服务器并被拒绝；板端显示中文 `用户名或密码错误`；正确密码登录显示 `登录成功`；服务器离线时 UI 不冻结；服务器恢复后同一板端进程可重新登录成功；键盘和模态消息框没有回归；程序可以正常退出。完整板端原始输出和准确退出码未在当前记录中保留 |
| 板端 `user_id` 状态 | 登录成功后代码保存服务器返回的 `user_id`；主机真实响应确认 `user_id=1`；后续主页实机验证已确认该值传递并显示为 `用户 ID：1`，见 S 节 |
| 能够证明 | 主机端注册与登录真实通过；SQLite 返回真实 `user_id=1`；板端错误密码被真实拒绝；板端正确密码真实登录成功；网络离线不会冻结 LVGL；服务器恢复后可以重试 |
| 本节当时不能证明 | 登录后页面跳转当时尚未验证，后续证据见 S 节；本节仍不能证明科室、医生、取号业务、完整项目回归、精确并发线程数量或未保留的准确运行退出码 |

### 板端 cJSON 与构建依赖

- `third_party/cjson` 使用 cJSON 1.7.19。
- `clinic_terminal` 直接编译 `cJSON.c`，不依赖外部 `libcjson`。
- 构建产物为 32-bit ARM EABI5，解释器为 `/lib/ld-linux.so.3`，`RPATH=/IOT`。
- `NEEDED` 包含 `libpthread.so.0`，不包含 `libcjson`。

## S. GEC6818 LVGL 医疗服务主页骨架实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu 真实登录服务器 |
| 对应阶段 | 登录成功后的医疗服务主页骨架 |
| 结果 | 用户确认按本阶段验收范围通过 |
| 原始输出与退出码 | 当前记录未保留完整命令、终端输出或准确退出码，不补写未保存的执行细节 |

### 用户确认的实机结果

- 正确密码登录成功后，确认“登录成功”消息框会进入医疗服务主页。
- 主页显示服务器真实登录响应传递的 `用户 ID：1`。
- 当时主页显示“医路通”“欢迎使用医疗服务”“科室查询”“医生查询”“预约取号”“排队查询”；“预约取号”后来已更名为“门诊取号”。
- 点击任一入口只显示“功能开发中”。
- 登录页键盘、拼音候选栏、输入框及其他登录页对象无残留。
- 错误密码仍停留在登录页，不发生跳转。
- UI 无冻结，Ctrl+C 正常退出。
- 本阶段没有请求科室、医生、取号或排队数据。

### 证据边界

- 能够证明：登录成功消息框确认后的页面切换、真实 `user_id` 从登录结果传递到主页并显示、四个入口当时的占位交互、登录页对象清理、UI 响应和退出流程在 GEC6818 上正常。
- 本节当时不能证明科室、医生、取号或排队的真实请求；后续科室列表验证证据见 T 节。
- 当前阶段：登录成功后的医疗服务主页骨架，当前阶段完成。
- 后续科室列表真实请求与展示阶段也已完成，见 T 节。

## T. GEC6818 科室列表真实请求与展示实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu `clinic_server` / SQLite |
| 对应阶段 | 科室列表真实请求与展示 |
| 结果 | 用户确认按本阶段验收范围通过 |
| 原始输出与退出码 | 当前记录未提供完整命令、终端输出或准确退出码，不补写未保存的执行细节 |

### 用户确认的实机结果

- 登录成功进入主页后，点击“科室查询”，后台线程发送真实 `list_departments` 请求。
- Ubuntu `clinic_server` 从 SQLite 返回真实科室数据，板端成功显示科室名称和科室 ID。
- 点击具体科室只显示“请选择医生”，没有发起医生列表请求。
- 返回医疗服务主页不需要重新登录，并保留 `authenticated_user_id`。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程可以重新请求成功。
- 请求和页面交互期间 UI 无冻结，其他三个主页入口无回归，程序可以正常退出。
- 本次 SQLite 实际返回 5 个科室，内容未超出屏幕。

### 能够证明

- 科室真实 TCP 请求从板端到达 `clinic_server` 并获得成功响应。
- SQLite 返回的真实科室名称和 ID 已在 GEC6818 科室页面成功展示。
- 服务器离线不会冻结 LVGL，恢复后同一进程可以重新请求成功。
- 科室页面返回主页时不重新登录，登录状态和 `authenticated_user_id` 保留正常。
- 科室点击占位提示、其他三个主页入口及正常退出没有发生回归。

### 不能证明

- 科室数量超出一屏时的实际滚动手感；当前只能确认可滚动容器实现存在，5 条真实数据没有触发溢出或滚动。
- 医生列表真实请求与展示；本节在当时不能证明，后续用户实机证据见 U 节。
- 科室选择后的真实业务页面跳转。
- 取号和排队业务。

### 阶段结论

- 当前阶段：科室列表真实请求与展示，当前阶段完成。
- 本节验收时点的下一唯一阶段是医生列表真实请求与展示；该后续阶段现已完成，见 U 节。

## U. GEC6818 医生列表真实请求与展示实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu `clinic_server` / SQLite |
| 对应阶段 | 医生列表真实请求与展示 |
| 结果 | 用户确认按本阶段验收范围通过 |
| 原始输出与退出码 | 当前记录未提供完整命令、终端输出或准确退出码，不补写未保存的执行细节 |

### 用户确认的实机结果

- 用户在科室页面选择真实科室后，板端使用真实 `department_id` 请求该科室医生列表。
- Ubuntu `clinic_server` 从 SQLite 返回该科室真实医生数据。
- 医生页面正确显示当前科室名称、医生 ID、医生姓名、职称和专长。
- 请求期间 UI 无冻结。
- 在本节验收时点，点击医生只显示“暂未开放取号”，当时没有发起取号请求；后续科室真实取号证据见 V 节。
- 从医生页返回后回到原科室列表，不重新登录、不重新请求科室，并保留 `authenticated_user_id` 和科室缓存。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程可以重新请求医生成功。
- Ctrl+C 可以正常退出。

### 能够证明

- 科室页面选择结果中的真实 `department_id` 已正确传递到医生请求链路。
- 医生真实 TCP 请求从板端到达 `clinic_server` 并获得成功响应。
- SQLite 返回的该科室真实医生数据已在 GEC6818 医生页面正确展示。
- 服务器离线不会冻结 LVGL，恢复后同一进程可以重新请求医生成功。
- 医生页返回原科室列表时不重新登录、不重新请求科室，`authenticated_user_id` 和科室缓存保留正常。
- 医生点击占位提示和正常退出没有发生回归。

### 不能证明

- 真实取号请求；本节在当时不能证明，后续用户实机证据见 V 节。
- 新建号单后的排队状态。
- 重复取号限制。
- 叫号后的界面刷新。

### 阶段结论

- 当前阶段：医生列表真实请求与展示，当前阶段完成。
- 本节验收时点的下一唯一阶段是医生选择后的科室真实取号；该后续阶段现已完成，见 V 节。

## V. GEC6818 医生选择后的科室真实取号实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu `clinic_server` / SQLite |
| 对应阶段 | 医生选择后的科室真实取号 |
| 结果 | 用户确认按本阶段验收范围通过 |
| 原始输出与退出码 | 当前记录未提供完整命令、终端输出或准确退出码，不补写未保存的执行细节 |

### 协议与业务语义

- `create_ticket` 请求使用真实 `authenticated_user_id` 对应的 `user_id` 和所选医生所属科室的 `department_id`。
- 现有协议不包含 `doctor_id`，因此本轮证明的是从医生列表进入科室取号流程，不是指定医生预约。
- 医生页面显示“点击医生后，将为其所属科室取号”。
- 号单页面不显示医生姓名或医生 ID，不暗示号单绑定指定医生。

### 用户确认的实机结果

- 用户从医生列表选择医生后，板端发送真实 `create_ticket` TCP 请求。
- Ubuntu `clinic_server` 成功向 SQLite 写入真实号单并返回响应。
- 板端成功显示号单 ID、科室名称和 ID、用户 ID、排队序号、状态和服务日期。
- 请求期间 UI 无冻结。
- 返回主页时不重新登录，保留 `authenticated_user_id`，且不自动重新请求科室或医生。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程可以再次操作。
- Ctrl+C 可以正常退出。

### 能够证明

- 板端真实 `create_ticket` TCP 请求成功到达 `clinic_server`。
- SQLite 成功创建真实号单，返回的真实号单数据已在 GEC6818 正确展示。
- 离线恢复和号单页返回主页正常，认证用户身份继续保留。
- 当前取号业务语义是按真实 `department_id` 进行科室取号，不是绑定指定医生。

### 不能证明

- 重复取号限制。
- 具体服务器业务拒绝条件。
- 排队状态自动刷新。
- 叫号后的界面更新。

### 业务错误边界

- `SERVER_ERROR` 映射“取号失败”的实现存在。
- 本轮没有触发服务器具体业务拒绝条件，不能据此断言对应运行分支已通过实机验证。
- 本轮没有执行或观察重复取号限制验证；既有主机端历史测试证据不能替代本轮板端实机证据。

### 阶段结论

- 当前阶段：医生选择后的科室真实取号，当前阶段完成。
- 本节验收时点的下一阶段是当前号单查询与排队状态展示；其中 `get_current_ticket` 后端闭环现已完成，板端页面仍未开始。

## W. get_current_ticket 后端协议与主机 TCP 闭环验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 对应阶段 | `get_current_ticket` 后端协议与主机 TCP 闭环 |
| 构建 | `build_exit=0` |
| 全量测试 | `full_test_exit=0` |
| TCP 地址 | `127.0.0.1:9000` |
| 命令边界 | 已保留调用参数、原始响应和退出码；完整 shell 命令文本未提供，不自行补写 |

### 成功真实 TCP

调用参数：

```text
127.0.0.1 9000 3001 1
```

原始单行响应：

```json
{"ok":true,"request_id":3001,"ticket":{"id":1,"user_id":1,"department_id":1,"queue_number":1,"status":"WAITING","service_date":"2026-07-15","created_time":1784084376,"called_time":null},"message":"current ticket retrieved"}
```

退出码：`0`。

### 无号单真实 TCP

调用参数：

```text
127.0.0.1 9000 3002 999999
```

原始单行响应：

```json
{"ok":false,"request_id":3002,"error_code":"CURRENT_TICKET_NOT_FOUND","message":"current ticket not found"}
```

退出码：`2`。

### 能够证明

- 新增独立协议 `get_current_ticket(user_id)` 的构建和全量测试通过。
- 真实 TCP 成功分支返回当天号单的现有 Ticket 响应结构，且客户端以退出码 `0` 结束。
- 无号单分支返回独立错误 `CURRENT_TICKET_NOT_FOUND`，且客户端以退出码 `2` 结束。
- 查询语义是按 `user_id` 查询当天最新号单；原 `get_ticket(ticket_id)` 和 `TICKET_NOT_FOUND` 没有被改义。

### 不能证明

- 本节当时不能证明 GEC6818 板端当前号单查询与排队页面；后续用户实机证据见 X 节。
- 排队状态自动轮询。
- 板端对 `CALLED`、`COMPLETED` 或 `CANCELLED` 状态的展示。
- 服务器推送。

### 阶段结论

- 当前完成阶段：`get_current_ticket` 后端协议与主机 TCP 闭环。
- 本节验收时点的下一唯一阶段是板端当前号单查询与排队状态展示；该后续阶段现已完成，见 X 节。

## X. GEC6818 板端当前号单查询与排队状态展示实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu `clinic_server` / SQLite |
| 对应阶段 | 板端当前号单查询与排队状态展示 |
| 结果 | 用户确认按本阶段验收范围通过 |
| 原始输出与退出码 | 当前记录未提供完整命令、终端输出或准确退出码，不补写未保存的执行细节 |

### 用户确认的实机结果

- 主页“排队查询”使用真实 `authenticated_user_id` 发起 `get_current_ticket` TCP 请求。
- 请求在 pthread 后台线程执行，LVGL 主线程不阻塞，UI 无冻结。
- 排队状态页面显示真实号单 ID、用户 ID、科室 ID、排队序号、当前状态、服务日期、创建时间和叫号时间。
- 实机号单的 `WAITING` 状态正确显示为“等待叫号”，`called_time=null` 正确显示为“尚未叫号”。
- 返回主页不重新登录，`authenticated_user_id` 继续保留。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程能够重新查询成功。
- 科室、医生和科室真实取号流程无回归，Ctrl+C 正常退出。

### 能够证明

- GEC6818 已完成真实 `get_current_ticket` TCP 请求、`clinic_server` / SQLite 查询和 LVGL 排队状态展示闭环。
- `WAITING` 中文映射及 `called_time=null` 的“尚未叫号”展示符合本轮真实响应。
- 网络离线不会冻结 UI，服务器恢复后同一进程可以重试成功。
- 排队页面返回主页、认证身份保留以及既有科室、医生和取号流程回归正常。

### 不能证明

- 板端 `CURRENT_TICKET_NOT_FOUND` 无号单分支；该分支只有主机端历史证据，本轮未在 GEC6818 触发。
- 在本节验收时，`CALLED`、`COMPLETED` 或 `CANCELLED` 的板端实机显示尚未覆盖；后续 Y 节已补充 `CALLED` 手动刷新证据，`COMPLETED` 和 `CANCELLED` 仍未覆盖。
- 在本节验收时页面内手动刷新尚未覆盖；后续 Y 节已完成该验证，自动轮询仍未实现。
- 服务器推送更新。

### 阶段结论

- 当前完成阶段：板端当前号单查询与排队状态展示。
- 本节验收时点的下一唯一阶段是排队状态手动刷新与叫号结果展示；该后续阶段现已完成，见 Y 节。

## Y. GEC6818 排队状态手动刷新与叫号结果展示实机验证

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu `clinic_server` / SQLite |
| 对应阶段 | 排队状态手动刷新与叫号结果展示 |
| 结果 | 用户确认全部阶段检查通过 |
| 历史验证准确时间 | 未在当前记录中保留 |
| 原始输出与退出码 | 当前记录未保留完整命令、终端输出或准确退出码，不补写未保存的执行细节 |

### 用户确认的实机结果

- 排队页面点击“刷新状态”后显示“正在刷新...”；请求期间刷新和返回按钮不可用，UI 无冻结。
- 未叫号时刷新后仍显示“等待叫号”和“尚未叫号”。
- Ubuntu 管理端使用 `build/linux/clinic_admin_call_client <server_ip> <port> <request_id> <department_id>`，通过真实 `department_id` 执行 `call_next`。
- 管理端成功将对应号单更新为 `CALLED`；板端再次手动刷新后显示“已叫号”和真实非空 `called_time`。
- 刷新前后号单 ID、用户 ID、科室 ID 和排队序号保持一致；板端不显示英文 `CALLED`，也没有新增叫号按钮。
- 服务器离线刷新时显示“无法连接服务器”，保留刷新前的号单详情，按钮恢复且 UI 无冻结。
- 服务器恢复后，同一板端进程可以再次刷新成功。
- 返回主页无需重新登录，Ctrl+C 正常退出。

### 能够证明

- `WAITING` 状态下的页面内手动刷新正常，仍显示“等待叫号”和“尚未叫号”。
- 管理端 `call_next(department_id)` 后，板端再次手动刷新能够显示中文 `CALLED` 结果和真实更新的 `called_time`。
- 网络离线时保留上一次有效号单详情，按钮和 UI 状态可以恢复。
- 服务器恢复后同一板端进程能够再次刷新成功。
- 排队页面返回主页、认证状态保留和 Ctrl+C 退出正常。

### 不能证明

- 板端 `CURRENT_TICKET_NOT_FOUND` 无号单分支。
- `COMPLETED` 和 `CANCELLED` 的板端实机显示。
- 自动轮询。
- 服务器推送。
- 完整项目回归。

### 阶段结论

- 当前完成阶段：排队状态手动刷新与叫号结果展示。
- 本节验收时点的下一阶段是 `call_next` WAITING 号单选择修复；该后续阶段现已完成，见 Z 节。

## Z. call_next 跳过旧 CALLED 号单真实 Bug 回归

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Ubuntu `clinic_server` / SQLite / GEC6818 开发板 |
| 对应阶段 | `call_next` WAITING 号单选择修复 |
| 当前源码构建 | `build_exit=0` |
| 当前源码全量测试 | `full_test_exit=0` |
| 本次同步边界 | Codex 只记录用户真实结果，未重新执行构建、测试、程序运行或验收 |

### 修复前真实失败证据

- `ticket.id=1` 已为 `CALLED`，`ticket.id=4` 仍为 `WAITING`。
- 修复前再次执行 `call_next(department_id=1)`，错误地重复返回 `ticket.id=1`。
- 随后用户 2 查询仍返回 `ticket.id=4`、`WAITING`，证明后续等待号单没有被选择或更新。

### 首次调用的环境失败

- 在服务器尚未监听时，首次真实调用返回 `could not connect to 127.0.0.1:9000`，退出码为 `call_next_exit=1`。
- 该结果由服务器未启动造成，只证明当时无法建立连接，不作为修复回归失败证据。

### 修复后真实 TCP 回归

实际调用：

```text
clinic_admin_call_client 127.0.0.1 9000 5006 1
```

实际响应：

```json
{"ok":true,"request_id":5006,"ticket":{"id":4,"user_id":2,"department_id":1,"queue_number":2,"status":"CALLED","service_date":"2026-07-15","created_time":1784102083,"called_time":1784105823},"message":"next ticket called"}
```

退出码：`call_next_exit=0`。

随后执行：

```text
host_current_ticket_client 127.0.0.1 9000 5007 2
```

实际响应：

```json
{"ok":true,"request_id":5007,"ticket":{"id":4,"user_id":2,"department_id":1,"queue_number":2,"status":"CALLED","service_date":"2026-07-15","created_time":1784102083,"called_time":1784105823},"message":"current ticket retrieved"}
```

退出码：`user2_ticket_exit=0`。

### 用户确认的开发板实机结果

- 手动刷新后当前状态显示“已叫号”。
- 叫号时间显示服务器返回的真实非空值。
- 号单 ID 仍为 4，用户 ID 仍为 2，科室 ID 仍为 1。

### 能够证明

- 修复后的当前源码构建通过，且当前源码全量测试通过。
- `call_next(department_id=1)` 正确跳过旧的 `CALLED` 号单 `ticket.id=1`。
- 下一张 `WAITING` 号单 `ticket.id=4` 被更新为 `CALLED`，并写入真实非空 `called_time`。
- `get_current_ticket(user_id=2)` 返回与 `call_next` 一致的 Ticket。
- GEC6818 开发板手动刷新显示与主机真实响应一致。

### 不能证明

- `COMPLETED` 实机路径。
- `CANCELLED` 实机路径。
- 自动轮询。
- 服务器推送。
- 所有未在本节列出的最终演示场景。

### 阶段结论

- 当前完成阶段：`call_next` WAITING 号单选择修复。
- 下一唯一阶段：最终演示验收与项目完成边界审查。

## AA. 尚未验证

| 项目 | 当前状态 | 需要的后续证据 |
| --- | --- | --- |
| Windows 当前构建 | 未验证 | Windows 工具链的实际构建输出；Ubuntu 结果不能替代 |
| 科室列表超出一屏时的滚动交互 | 未验证 | 多于一屏的真实科室数据以及实际拖动、滚动和边界表现 |
| 重复取号限制与具体服务器业务拒绝 | 未验证 | 板端触发相应服务器规则并保留真实响应的实机证据 |
| 板端当前号单无号单分支 | 未验证 | GEC6818 实机触发 `CURRENT_TICKET_NOT_FOUND` 的证据 |
| 板端其余号单状态 | 未验证 | GEC6818 实机分别展示 `COMPLETED` 和 `CANCELLED` 的证据；`CALLED` 已在 Y 节验证 |
| 排队状态自动轮询与服务器推送 | 未验证 | 自动轮询或服务器主动推送的实现及实机证据 |
| 最终演示验收与项目完成边界审查 | 未验证 | 完成最终演示主链，并审查项目完成声明所需证据与未覆盖边界 |

## AB. GEC6818 注册登录认证链路实机确认

### 用户确认范围

- 用户已确认 GEC6818 开发板上的注册、注册成功返回登录页、登录和进入医疗服务主页流程成功。
- 返回登录页时，用户名保持回填，密码字段保持清空。
- 用户已确认长输入、输入框切换和相关认证页面交互测试成功。
- 用户反馈本轮列出的五组认证测试均成功；本节不补写未保留的原始日志、退出码或完整主机回归结果。

### 证据边界

- 该记录证明认证链路具备用户实机确认，不代表本阶段排队信息增强已经实现或验证。
- 下一唯一阶段：排队信息增强主机回归与 GEC6818 实机验收。

## AC. 排队信息增强源码阶段与用户验证

### 当前状态

- 本节记录源码阶段和随后用户实机确认，不改变前述历史实机结论。
- `get_current_ticket` 已扩展 `queue_summary`；Store、Core、JSON、主机当前号单解析器、板端解析器和排队页展示已同步修改。
- `create_ticket`、`get_ticket`、`call_next` 响应结构、手动刷新模型、板端不发送 `call_next` 的边界保持不变。
- 当前结论：用户已确认本阶段检查全部通过。

### 本轮执行的验证命令

```bash
cd /mnt/hgfs/codex
make -B test
make -C board/clinic_terminal clean
make -C board/clinic_terminal -j$(nproc)
```

```powershell
cd E:\codex
scp -O .\build\board\clinic_terminal `
  root@192.168.10.42:/IOT/clinic_terminal
if ($LASTEXITCODE -ne 0) { throw "clinic_terminal transfer failed" }
```

```sh
cd /IOT
chmod +x clinic_terminal
export LD_LIBRARY_PATH=/IOT:/lib:/usr/lib
./clinic_terminal
```

### 证据边界

- 本阶段记录来自用户实际执行后的确认；未补写未保留的具体退出码和原始终端日志。
- 用户明确反馈：叫号并刷新后，前方人数每次减少 1；本轮列出的全部检查通过。

## AD. GEC6818 排队信息增强实机确认

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 / Ubuntu 服务器 / SQLite |
| 对应阶段 | 当前叫号与前方 `WAITING` 人数增强 |
| 结果 | 用户确认本轮全部检查通过 |
| 关键结果 | 叫号后手动刷新，前方人数每次减少 1 |
| 原始输出与退出码 | 当前记录未保留，不补写具体值 |

### 阶段结论

- 排队页当前叫号、前方人数和手动刷新增强已有用户实机确认。
- 本阶段不增加自动轮询、服务器推送或板端叫号按钮。
- 下一唯一阶段：最终演示验收与项目完成边界审查。

## AE. 主页独立医生查询入口源码阶段

### 当前状态

- 主页“医生查询”按钮已从“功能开发中”占位回调改为复用现有科室请求入口。
- 点击后先进入科室列表；选择科室后继续复用现有医生列表请求和页面展示。
- 科室查询和医生查询请求期间，两个主页入口都会禁用，避免重复提交同一个科室请求。
- 本节只记录源码实现，不记录新的构建通过或开发板实机通过。

### 待执行验证命令

```bash
cd /mnt/hgfs/codex
make -B test
make -C board/clinic_terminal clean
make -C board/clinic_terminal -j$(nproc)
```

### 实机验收边界

- 登录后点击主页“医生查询”，应进入科室列表而不是弹出“功能开发中”。
- 选择科室后，应进入现有医生列表；返回路径、重复点击和服务器离线提示应保持原有行为。
- 本节不增加预约绑定 `doctor_id`，不增加板端 `call_next` 或叫号按钮。
- 当前源码状态：实现完成未验证，等待用户执行。

## Makefile 边界说明

- 用户最新保留的聚合结果为 `full_test_exit=0`，证明当次实际执行的全量测试成功。
- 本文件不根据 Makefile 中的目标数量或测试源码存在性推定具体子测试已执行；只记录用户保留的聚合退出码。

## 追溯限制

当前状态依据实际命令输出和阶段报告维护，不具备 Git commit 级追溯能力。

## AE. 最新完整验证结果

### 用户确认

- 用户已确认主机完整测试通过。
- 用户已确认板端交叉编译通过。
- 用户已确认开发板程序传输成功并正常启动。
- 用户已确认既有注册、登录、科室查询、医生列表、取号、当前叫号、前方等待人数、手动刷新和异常恢复流程通过；主页独立医生查询入口的本次源码变更仍未有独立构建和实机点击证据。
- 用户已确认叫号后前方等待人数按预期减少。

### 结论

本项目当前已完成本阶段计划范围内的主机回归、板端构建、部署和 GEC6818 实机验收。未将自动轮询、服务器推送、复杂预约和未实际触发的边界状态写成已验证。

最终演示顺序和项目完成边界见 [`docs/FINAL_ACCEPTANCE.md`](FINAL_ACCEPTANCE.md)。

本次新增的主页入口语义修正尚未构建或实机验证：查询入口应先展示医生信息，当时的“预约取号”入口才允许创建号单；该入口后来已更名为“门诊取号”。

退出登录按钮的本次源码修改尚未构建或实机验证。
## AF. 主页入口与退出登录源码加固验证边界

### 静态检查

- 使用 Windows Clang 执行 `board/clinic_terminal/home_page.c` 的 C11、`-Wall -Wextra -Wpedantic` 语法检查，命令返回 0。
- 已检查主页回调签名：浏览入口传入非取号模式，预约入口传入取号模式；没有发现遗留 `service_entry_clicked_cb` 调用方。
- 已检查退出流程：退出请求后等待 department、doctor、ticket、current-ticket worker，再清理页面并创建登录页。

### 未完成验证

以下命令仍需在 Ubuntu 执行：

```bash
cd /mnt/hgfs/codex
make -C board/clinic_terminal clean
make -C board/clinic_terminal -j$(nproc)
```

以下场景仍需在 GEC6818 实机点击确认：

- 医生查询入口只展示信息，不创建号单。
- 科室查询入口只展示信息，不创建号单。
- 当时的预约取号入口（现“门诊取号”）仍可正常创建号单。
- 主页退出登录后回到登录页，并可再次登录。
- 退出时不存在重复页面、旧输入法或旧网络线程残留。

### 本阶段结论

实现完成未验证，等待用户执行。

## AG. 医生查询提示与退出后拼音键盘回归边界

### 用户报告

- 从查询入口选择科室后，医生页仍显示“点击医生后，将为其所属科室取号”，与查询模式不符。
- 退出登录后重新进入登录页，用户名输入框弹出的拼音键盘出现文本框但看不到字体。

### 已完成的源码检查

- `doctor_page.c` 使用 Windows Clang 执行 C11、`-Wall -Wextra -Wpedantic -Werror` 语法检查，返回 0。
- 已确认误导文案从医生页源码中删除。
- 已确认退出分支先执行 `lv_scr_load(login_screen)`，再调用 `create_login_page()`。

### 待执行验证

```bash
cd /mnt/hgfs/codex
make -C board/clinic_terminal clean
make -C board/clinic_terminal -j$(nproc)
```

开发板重点回归：

1. 登录后进入“科室查询”，选择科室，确认医生页不再显示取号说明。
2. 返回主页进入当时的“预约取号”（现“门诊取号”），确认选择医生仍可正常取号。
3. 点击“退出登录”，重新进入登录页。
4. 点击用户名输入框，确认键盘字母和拼音候选显示正常。
5. 再次退出并登录一次，确认第二次进入登录页仍正常。

### 本阶段结论

实现完成未验证，等待用户执行。

## AH. 拼音候选状态保护用户确认

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Ubuntu ARM 构建 / GEC6818 开发板 |
| 验证范围 | 拼音候选对应、无匹配候选隐藏、连续候选操作、长输入、输入框切换、注册页、退出后重登 |
| 用户结果 | 检查通过 |
| 原始退出码与完整日志 | 当前记录未保留，不补写具体值 |

### 结论

- 本轮拼音候选错配与 `Segmentation fault` 回归检查已有用户真实确认。
- 本结论只覆盖本轮给出的验证范围，不扩展为未执行场景或整个项目的最终验收。
- 当前阶段完成。

## AI. 登录页记住密码用户确认

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Ubuntu ARM 构建 / PowerShell 部署 / GEC6818开发板 |
| 构建 | 用户确认成功 |
| 部署与运行 | 用户确认成功 |
| 记住状态切换 | 用户确认通过 |
| 退出登录后回填 | 用户确认通过 |
| 程序重启后回填 | 用户确认通过 |
| 登录失败不覆盖正确密码 | 用户确认通过 |
| 取消记住后停止回填并删除凭据 | 用户确认通过 |
| 凭据文件权限 | 用户确认本轮检查通过；原始 `stat` 输出未在当前记录保留 |
| 原始退出码与完整日志 | 当前记录未保留，不补写具体值 |

### 结论

- 登录页记住密码功能已有用户真实构建、部署和GEC6818实机确认。
- 本结论只覆盖本轮规定的验证场景，不表示密码具备硬件级加密保护。
- 当前阶段完成。

## AJ. 三种服务入口语义拆分源码检查

### V-AJ-01：页面模块严格语法检查

| 项目 | 记录 |
| --- | --- |
| 执行环境 | Windows 11 / Clang |
| 工作目录 | `E:\codex` |
| 命令 | `clang -fsyntax-only -std=c11 -Wall -Wextra -Wpedantic -Werror -Iboard/clinic_terminal -Iinclude -Ireference/six/six -Ireference/six/six/lvgl -Ireference/six/six/lvgl/src board/clinic_terminal/home_page.c board/clinic_terminal/department_page.c board/clinic_terminal/doctor_page.c` |
| 原始退出码 | `0` |
| 错误标志 | `0` |
| 结论 | 三个直接修改的页面模块通过严格 C11 语法检查 |

### V-AJ-02：`main.c` 本机检查边界

| 项目 | 记录 |
| --- | --- |
| 执行环境 | Windows 11 / Clang |
| 结果 | 在 `#include <pthread.h>` 处停止 |
| 原始退出码 | `1` |
| 错误标志 | `1` |
| 含义 | Windows LLVM 环境没有 Linux `pthread.h`，未取得 `main.c` 完整语法证据；必须以 Ubuntu ARM 构建为准 |

### 待用户执行

```bash
cd /mnt/hgfs/codex
make -C board/clinic_terminal clean
clean_exit=$?
make -C board/clinic_terminal -j$(nproc)
build_exit=$?
printf 'clean_exit=%s\nbuild_exit=%s\n' "$clean_exit" "$build_exit"
file build/board/clinic_terminal
```

### 当前结论

验证总错误标志：`1`（缺少目标环境构建和实机证据）。

实现完成未验证，等待用户执行。

## AK. 完全拆分与直接科室取号源码检查

### V-AK-01：页面模块严格语法检查

| 项目 | 记录 |
| --- | --- |
| 执行环境 | Windows 11 / Clang |
| 工作目录 | `E:\codex` |
| 检查文件 | `home_page.c`、`department_page.c`、`doctor_page.c`、`ticket_page.c` |
| 参数 | `-std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only` |
| 原始退出码 | `0` |
| 错误标志 | `0` |

### V-AK-02：`main.c` 接口语法检查

| 项目 | 记录 |
| --- | --- |
| 执行环境 | Windows 11 / Clang + 临时 POSIX 声明桩 |
| 检查目的 | 验证本轮回调类型、函数参数和结构体字段引用 |
| 原始退出码 | `0` |
| 错误标志 | `0` |
| 边界 | 使用 `-Wno-incompatible-pointer-types` 排除既有 FreeType 销毁参数警告；声明桩已删除，不构成 Linux/ARM 构建证据 |

### V-AK-03：源码业务契约检查

| 项目 | 记录 |
| --- | --- |
| 原始退出码 | `0` |
| 错误标志 | `0` |
| 检查结果 | `department_query_scope=department_only` |
| 检查结果 | `doctor_query_scope=doctor_details_only` |
| 检查结果 | `ticket_scope=department_direct` |
| 检查结果 | `legacy_doctor_ticket_fields=0` |

### 当前结论

本机检查错误标志：`0`。

用户验证总错误标志：`1`（尚缺 Ubuntu ARM 构建、部署和 GEC6818 实机证据）。

实现完成未验证，等待用户执行。

## AL. 辅助目录整理后的 Ubuntu/ARM 构建验证

更新时间：2026-07-17 16:49（Asia/Hong_Kong）

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | VMware Ubuntu 20.04，共享目录 `/mnt/hgfs/codex` |
| 主机回归 | `make -B test`，`test=0` |
| 正式终端 | `terminal_clean=0`，`terminal_build=0` |
| LVGL 显示探针 | `smoke_clean=0`，`smoke_build=0` |
| 网络探针 | `probe_clean=0`，`probe_build=0` |
| 产物 | `build/board/clinic_terminal`、`clinic_lvgl_smoke`、`clinic_net_probe` 均存在 |
| 文件类型 | 三个产物均为 `ELF 32-bit LSB executable, ARM, EABI5`，动态链接器 `/lib/ld-linux.so.3`，含 `debug_info` 且未 strip |

### 用户回传的关键输出

```text
test=0 terminal_clean=0 terminal_build=0 smoke_clean=0 smoke_build=0 probe_clean=0 probe_build=0
```

### 本阶段结论

- 本阶段用户验证错误标志：`0`。
- 已证明目录整理后的 Ubuntu/ARM 构建路径可用，且产物 ABI 正确。
- 未证明本次最新源码已经在 GEC6818 上启动、点击或完成断网恢复；不把构建证据扩展为实机通过。
- 文档阶段完成，不改变已有用户验证结论。

## AO. GEC6818 最新统一产物部署与最终演示流程确认

更新时间：2026-07-17 17:19（Asia/Hong_Kong）

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Windows PowerShell 部署、Ubuntu 服务器、GEC6818 开发板 |
| 部署范围 | 最新 `build/board/clinic_terminal` 上传并启动 |
| 演示范围 | 注册/登录、科室查询、医生查询、门诊取号、排队状态、管理员叫号、手动刷新、异常恢复、退出登录 |
| 用户结果 | 用户明确反馈“检查通过” |
| 原始退出码与完整日志 | 当前记录未保留，不补写具体值 |

### 本阶段结论

- 本阶段用户验证错误标志：`0`。
- 用户已确认本阶段演示流程检查通过。
- 本记录不扩展为无号单、`COMPLETED`、`CANCELLED` 等未专项触发场景通过。
- 文档阶段完成，不改变已有用户验证结论。

## AM. 统一构建入口实现阶段

更新时间：2026-07-17 17:04（Asia/Hong_Kong）

### V-AM-01：PowerShell 脚本语法检查

| 项目 | 记录 |
| --- | --- |
| 执行环境 | Windows 11 / PowerShell AST Parser |
| 检查文件 | `build.ps1` |
| 原始错误标志 | `0` |
| 结论 | 脚本语法有效，未执行外部构建 |

### V-AM-02：构建入口静态检查

| 项目 | 记录 |
| --- | --- |
| 检查文件 | `Makefile`、`build.ps1`、`README.md` |
| 检查范围 | 十个根目标、三个板端路径、六个递归板端调度、README 命令 |
| 原始错误标志 | `0` |
| 结论 | 统一入口名称、路径和文档命令一致 |

### 待用户执行

```bash
cd /mnt/hgfs/codex
make -B test
test_exit=$?
make board-clean
board_clean_exit=$?
make -B build-all
build_all_exit=$?
printf 'test=%s board_clean=%s build_all=%s\n' \
  "$test_exit" "$board_clean_exit" "$build_all_exit"
ls -l \
  build/linux/clinic_server \
  build/linux/clinic_ticket_client \
  build/linux/clinic_ticket_status_client \
  build/linux/clinic_admin_call_client \
  build/board/clinic_terminal \
  build/board/clinic_lvgl_smoke \
  build/board/clinic_net_probe
file \
  build/board/clinic_terminal \
  build/board/clinic_lvgl_smoke \
  build/board/clinic_net_probe
```

### 当前结论

- 本机静态检查错误标志：`0`。
- 本阶段用户验证错误标志：`1`（尚未执行 Ubuntu 统一入口回归）。
- 源码与构建配置修改完成，等待用户执行。

## AN. 统一构建入口 Ubuntu/ARM 回归确认

更新时间：2026-07-17 17:10（Asia/Hong_Kong）

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | VMware Ubuntu 20.04，共享目录 `/mnt/hgfs/codex` |
| 主机回归 | `test=0` |
| 板端清理 | `board_clean=0` |
| 统一构建 | `build_all=0` |
| 主机产物 | `clinic_server`、`clinic_ticket_client`、`clinic_ticket_status_client`、`clinic_admin_call_client` 均存在 |
| 板端产物 | `clinic_terminal` 448056 bytes；`clinic_lvgl_smoke` 299876 bytes；`clinic_net_probe` 10964 bytes |
| ABI 检查 | 三个板端产物均为 `ELF 32-bit LSB executable, ARM, EABI5`，动态链接器 `/lib/ld-linux.so.3`，含 `debug_info` 且未 strip |

### 本阶段结论

- 本阶段用户验证错误标志：`0`。
- 统一入口已完成 Ubuntu/ARM 构建验证。
- 本记录不扩展为 GEC6818 实机运行通过；开发板部署和最终演示仍是下一阶段。
- 文档阶段完成，不改变已有用户验证结论。

## AO. 重复取号状态回归首次失败记录

更新时间：2026-07-17 20:26（Asia/Hong_Kong）

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | VMware Ubuntu 20.04，共享目录 `/mnt/hgfs/codex` |
| 验证命令 | `make -B test`；附件从编译输出开始，未保留命令提示符行 |
| 关键结果 | 协议、JSON、SQLite 基础、科室 Store 和医生 Store 测试先后通过；`test_ticket_store` 失败后停止 |
| 失败输出 | `FAIL: tests/test_ticket_store.c:850: ... == CLINIC_STORE_OK` |
| 原始退出码 | `full_test_exit=2` |
| 错误标志 | `1` |

### 当前结论

- 失败原因是 `CALLED` 活动号单的重复创建测试仍保留旧状态期望；实际返回的原号单内容断言没有失败。
- 源码已将该断言改为 `CLINIC_STORE_ACTIVE_TICKET_EXISTS`，但修改后尚未由用户重新执行测试。
- 本记录不得解释为回归通过、ARM 构建通过或 GEC6818 实机通过。

## AQ. Ubuntu 管理端交互式叫号菜单用户确认

更新时间：2026-07-17 21:13（Asia/Hong_Kong）

### V-AQ-01：重新构建与无参数菜单操作

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | VMware Ubuntu 20.04，共享目录 `/mnt/hgfs/codex` |
| 提供的构建命令 | `make -B build/linux/clinic_admin_call_client` |
| 提供的运行命令 | `./build/linux/clinic_admin_call_client` |
| 验证范围 | 无参数启动、动态科室菜单、数字叫号、`r` 刷新、`q` 退出 |
| 用户结果 | 用户明确反馈“验证通过” |
| 原始退出码与完整日志 | 当前记录未保留，不补写具体值 |
| 证据完整性错误标志 | `1`：缺少原始退出码、产物信息和逐条终端输出 |

### 本阶段结论

- 功能结论：用户确认 Ubuntu 管理端交互式叫号菜单验证通过。
- 证据完整性结论：缺少原始退出码和终端日志，不能补写 `build_exit=0` 或具体菜单输出。
- 本记录不扩展为 GEC6818 实机验证，也不改变原四参数 CLI 的既有验证结论。
- 文档阶段完成，不改变其他阶段的用户验证边界。

## AR. 板端公共 TCP 传输模块用户确认

更新时间：2026-07-18 14:33（Asia/Hong_Kong）

### V-AR-01：专项测试、完整回归、ARM 构建与板端业务回归

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | VMware Ubuntu 20.04、共享目录 `/mnt/hgfs/codex`、GEC6818 |
| 提供的专项范围 | 构建并运行 `test_board_transport` |
| 提供的完整回归范围 | 根 Makefile `test` 聚合目标 |
| 提供的板端构建范围 | 清理并重新构建 `clinic_terminal` ARM 产物 |
| 提供的板端操作范围 | 登录/注册、科室、医生、取号、重复取号提示、排队查询、手动刷新、断网与恢复 |
| 用户结果 | 用户明确反馈“检查通过” |
| 原始退出码、产物信息与完整日志 | 当前记录未保留，不补写具体值 |
| 证据完整性错误标志 | `1`：缺少原始退出码、专项测试原文、产物哈希和逐项板端日志 |

### 本阶段结论

- 功能结论：用户确认公共传输模块修改后的规定检查范围通过。
- 证据完整性结论：只能记录用户确认，不能补写 `transport_test_exit=0`、`full_test_exit=0`、`board_build_exit=0` 或具体产物信息。
- 本记录不扩展为未实际触发的 `COMPLETED`、`CANCELLED` 或无号单边界场景通过。
- 文档阶段完成，不改变其他阶段的用户验证边界。

## AS. GEC6818 排队页 5 秒自动刷新用户确认

更新时间：2026-07-21 11:39（Asia/Hong_Kong）

### V-AS-01：自动轮询实机确认

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 |
| 验证范围 | 排队页 5 秒自动刷新 |
| 用户结果 | 用户明确反馈“验证也通过了，自动轮询” |
| 原始命令、退出码与逐项操作日志 | 当前记录未保留，不补写具体值 |
| 证据完整性错误标志 | `1`：缺少原始命令、退出码和逐项板端日志 |

### 本阶段结论

- 功能结论：用户确认排队页自动轮询验证通过。
- 证据完整性结论：缺少本轮命令和原始输出，不能补写 ARM 构建、上传或运行的具体退出码。
- 本记录不扩展为服务器推送、无号单、`COMPLETED`、`CANCELLED`、断网恢复或重复进出页面等未单独说明场景通过。
- 文档阶段完成，不改变其他阶段的用户验证边界。

## AT. 排队页定时器生命周期封装 Ubuntu/ARM 构建验证

更新时间：2026-07-24 20:05（Asia/Hong_Kong）

### V-AT-01：Ubuntu/ARM 构建与产物存在性检查

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Ubuntu |
| 工作目录 | `/mnt/hgfs/codex` |
| 验证编号 | V-AT-01 |
| 命令或验证名称 | `make -C board/clinic_terminal clean`；`make -C board/clinic_terminal -j"$(nproc)"`；`test -f build/board/clinic_terminal` |
| 对应代码阶段 | 排队页定时器生命周期封装 |
| 用户回传结果 | `clean=0`、`board_build=0`、`artifact=0` |
| 原始退出码 | 清理 `0`；板端构建 `0`；产物存在性检查 `0` |
| 错误标志 | `0` |
| 文件或产物证据 | `build/board/clinic_terminal` 存在；未提供大小、`file` 输出或完整构建日志 |
| 结果 | Ubuntu/ARM 构建和产物存在性检查通过 |
| 能够证明 | 本次修改可以通过用户提供的板端构建入口，并生成目标产物 |
| 不能证明 | GEC6818 部署、启动、自动/手动刷新、反复进出页面、断网恢复或定时器销毁行为 |

### V-AT-02：GEC6818 实机生命周期回归用户确认

该项验证结果已在后续独立的 AU 阶段记录：用户确认反复进入/返回排队页、自动与手动刷新交互、刷新期间按钮状态、断网恢复和页面返回后的定时器销毁回归通过；当前记录未保留原始命令、部署记录、退出码和逐项日志。

## AU. 排队页定时器生命周期封装 GEC6818 实机回归确认

更新时间：2026-07-24 20:10（Asia/Hong_Kong）

### V-AU-01：GEC6818 实机生命周期回归

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 |
| 验证编号 | V-AU-01 |
| 对应代码阶段 | 排队页定时器生命周期封装 |
| 验证范围 | 反复进入/返回排队页、自动与手动刷新、刷新期间按钮状态、断网恢复、页面返回后的定时器销毁 |
| 用户结果 | 用户明确反馈“验证通过” |
| 原始命令、部署记录、退出码和逐项操作日志 | 未在当前记录中保留，不补写具体值 |
| 证据完整性错误标志 | `1`：缺少原始命令、部署记录、退出码和逐项日志 |
| 功能结论 | 用户确认本次 GEC6818 生命周期回归通过 |
| 不能证明 | 具体命令退出码、产物哈希、逐项操作原始日志，以及未列出的其他场景 |

### 阶段结论

- 本阶段状态：当前阶段完成。
- 功能结论：用户确认本次定时器生命周期回归通过。
- 证据完整性结论：证据完整性错误标志为 `1`，因为当前记录没有原始命令、退出码和逐项日志。
- 下一唯一阶段：项目完成边界审查与最终材料收口。

## AV. 项目完成边界审查与最终验收材料收口

更新时间：2026-07-24 20:17（Asia/Hong_Kong）

### 文档阶段记录

| 项目 | 记录 |
| --- | --- |
| 执行者 | Codex Desktop |
| 工作目录 | `E:\codex` |
| 对应代码阶段 | 项目完成边界审查与最终验收材料收口 |
| 更新文件 | `PROJECT_BRIEF.md`、`PROJECT_STATUS.md`、`TEST_STATUS.md`、`FINAL_ACCEPTANCE.md`、`FINAL_DEMO_GUIDE.md` |
| 功能构建、测试、程序运行和设备操作 | 本阶段未执行 |
| 文档处理结果 | 已同步 5 秒自动刷新、定时器生命周期用户确认、最终演示顺序、证据完整性标志和未覆盖边界 |
| 功能验证结论 | 不产生新的功能验证结论，不改变既有用户验证边界 |
| 文档阶段错误标志 | `0` |

### 阶段结论

- 本阶段状态：当前阶段完成。
- 用户验证与 Codex 文档处理分开记录；本阶段没有把文档同步写成构建、测试或实机通过。
- 项目整体仍等待用户确认最终验收边界，不宣称原始 PRD 全部完成。
- 下一唯一阶段：用户确认最终验收边界与项目收口。
