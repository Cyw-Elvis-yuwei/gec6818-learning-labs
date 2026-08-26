# 医路通项目接管简报

更新时间：2026-07-24 20:17（Asia/Hong_Kong）

本文件是人和 Codex 接手项目时的单页入口。它只记录当前稳定事实、常用入口和未完成边界；详细状态以项目状态、测试状态和架构文档为准。

## 30 秒摘要

- 项目名称：医路通——基于 S5P6818 和 LVGL 的联网智慧医疗自助服务终端。
- 项目目标：在 S5P6818 嵌入式 Linux 终端上完成联网医疗排队自助服务。
- 板端功能：注册、登录、记住密码、科室查询、医生查询、按科室取号、排队状态查询、手动/自动刷新、退出登录和网络异常提示。
- Ubuntu 端功能：epoll 网络服务器、业务处理、SQLite 数据保存，以及简易管理员叫号客户端。
- 当前闭环：用户登录后按科室取号，服务器保存号单，管理员调用下一位，板端刷新或自动刷新排队状态。
- 当前阶段：项目完成边界审查与最终验收材料已整理，等待用户确认当前范围是否收口。

明确不在当前范围内：HTTP 后台、服务端推送、复杂指定医生预约、诊断、处方、传感器和云服务。

## 项目位置与快速入口

| 项目项 | 入口 |
| --- | --- |
| Windows 项目根目录 | `E:\codex` |
| Ubuntu 共享目录 | `/mnt/hgfs/codex` |
| 主机测试 | `cd /mnt/hgfs/codex` 后执行 `make -B test` |
| 主机/板端完整构建 | `make build-all` |
| 板端直接构建 | `make -C board/clinic_terminal clean`；`make -C board/clinic_terminal -j$(nproc)` |
| 服务器运行 | `/mnt/hgfs/codex` 下执行 `./build/linux/clinic_server` |
| 管理员叫号 | `/mnt/hgfs/codex` 下执行 `./build/linux/clinic_admin_call_client` |
| 板端部署文件 | `build/board/clinic_terminal` |
| 开发板运行目录 | `/IOT` |
| 开发板运行环境 | `LD_LIBRARY_PATH=/IOT:/lib:/usr/lib` |
| 服务器数据库 | `build/data/clinic.db` |

开发板部署仍由用户在 Windows 执行 `scp -O`，运行和实机验收也由用户执行。接管时不能把构建产物存在误认为板端验证证据。

## 技术栈与线程边界

- 语言和系统：C、C11、Ubuntu 20.04、嵌入式 Linux、S5P6818。
- 板端：LVGL、LCD、触摸、中文字体、拼音输入法、pthread 网络工作线程。
- 服务端：Linux `epoll`、TCP、单行 UTF-8 JSON、SQLite。
- 公共协议：以换行符作为 JSON 消息边界；公共网络、分帧、协议和 JSON 代码位于 `common/` 与 `include/`。
- LVGL 只由板端主线程调用；网络工作线程只负责请求、收发和保存结果，不能直接操作 LVGL 控件。
- 网络线程结束后由主线程执行一次 `pthread_join`，再根据结果更新当前仍有效的页面。
- SQLite 只在服务器端通过 Store 访问，开发板不直接打开数据库。

## 稳定数据流

```text
S5P6818 LVGL 主线程
  → pthread 网络工作线程
  → TCP/单行 JSON + 换行分帧
  → Ubuntu epoll 服务器
  → Handler 解析与校验
  → clinic_core_handle() 处理业务规则
  → Store 访问 SQLite
  → JSON 响应原路返回
  → 板端主线程更新 LVGL 页面
```

职责定位：Handler 负责协议入口，Core 负责业务规则，Store 负责屏蔽数据库细节，SQLite 负责服务器端持久化。板端查询排队摘要仍以 `get_current_ticket` 为唯一入口。

## 目录地图

| 目录 | 用途 |
| --- | --- |
| `board/clinic_terminal/` | S5P6818 正式 LVGL 医疗终端 |
| `common/` | TCP、分帧、协议和 JSON 公共实现 |
| `include/` | 公共头文件、数据类型和接口 |
| `server/` | Ubuntu epoll 服务器与 Handler |
| `core/` | `clinic_core_handle()` 核心业务 |
| `store/` | Store 接口和 SQLite 实现 |
| `terminal/` | Ubuntu 主机客户端和管理员命令 |
| `tests/` | 主机业务、协议、Store、Handler 和 TCP 测试 |
| `tools/board/` | LVGL 显示探针和开发板网络探针 |
| `third_party/` | 项目直接使用的第三方源码，如 cJSON |
| `reference/` | LVGL、驱动、FreeType 和平台参考代码，不要删除 |
| `docs/` | 架构、状态、测试、验收和演示文档 |
| `.scratch/` | PRD、issue 和历史计划资料，不参与常规编译 |
| `build/` | 编译产物、测试程序、数据库和诊断文件；属于生成目录 |

## 关键阅读顺序

1. `docs/PROJECT_BRIEF.md`：本接管入口。
2. `docs/PROJECT_STATUS.md`：当前完成项、用户证据和唯一下一阶段。
3. `docs/TEST_STATUS.md`：实际测试记录与证据边界。
4. `docs/ARCHITECTURE.md`：稳定架构、线程边界、协议和生命周期。
5. `CONTEXT.md`：领域词汇、号单语义和稳定业务规则。
6. `docs/adr/`：Handler/Core/Store 分层、线程边界、TCP 分帧和取号语义决策。
7. `README.md` 与根目录 `Makefile`：构建、运行和验证入口。
8. `include/clinic_types.h`：业务对象和状态定义。
9. `common/`、`server/`、`core/`、`store/`：服务器请求处理主链。
10. `board/clinic_terminal/`：页面、网络客户端和 LVGL 生命周期。

## 当前已知边界

- 5 秒自动刷新和排队页定时器生命周期已由用户确认通过；当前记录保留“用户确认”结论，不能补写不存在的原始命令、退出码或完整实机日志。
- 最新统一产物的“科室查询/医生查询/门诊取号”入口语义和演示流程已有用户检查通过反馈，但状态仍保留证据不完整边界，不扩展为全量专项证据。
- 无号单、`COMPLETED`、`CANCELLED` 等边界场景仍应以 `PROJECT_STATUS.md` 和 `TEST_STATUS.md` 的记录为准，不得擅自标为已验收。
- `CONTEXT.md` 和 `docs/adr/` 已建立；它们只记录当前领域词汇和稳定架构决策，不替代项目状态与真实测试证据。
- 当前没有进行源码修改、测试执行、编译、部署或开发板操作；本次只建立接管文档。

## 接手原则

先读本文件、项目状态、测试状态和架构文档，再查看源码。任何新修改必须保持 Handler/Core/Store 边界、SQLite 服务器端唯一访问、LVGL 主线程约束和线程一次消费/join 约束。用户没有提供新的构建或实机输出前，只能写“实现完成未验证”或引用已有用户证据，不能扩大验证结论。
