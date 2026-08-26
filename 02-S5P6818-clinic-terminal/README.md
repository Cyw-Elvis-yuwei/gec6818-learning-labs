?# 医路通——S5P6818 联网智慧医疗自助服务终端

## 中文判断提示

- 当前状态：统一构建入口、Ubuntu/ARM 回归和最新产物演示流程检查已完成。
- 这是什么意思：用户确认最新板端产物已部署启动，主要业务演示流程检查通过。
- 是否还需要继续讨论：不需要；下一步是整理项目完成边界和最终材料。
- 建议下一步：明确已验证功能、未专项验证边界和未实现范围。
- 还缺什么：无号单、`COMPLETED`、`CANCELLED` 等独立边界证据，以及最终材料收口。

## 先看什么

1. `docs/PROJECT_STATUS.md`：当前进度和验证边界。
2. `docs/ARCHITECTURE.md`：稳定架构、数据流和线程边界。
3. `docs/TEST_STATUS.md`：真实测试记录和待验证边界。
4. `docs/FINAL_DEMO_GUIDE.md`：服务器、开发板和管理员演示命令。

## 目录地图

| 目录 | 用途 |
| --- | --- |
| `board/clinic_terminal` | S5P6818 正式 LVGL 医疗终端 |
| `common` | TCP、分帧、协议和 JSON 公共实现 |
| `core` | `clinic_core_handle()` 核心业务 |
| `store` | Store 接口和 SQLite 实现 |
| `server` | Ubuntu epoll 服务器 |
| `terminal` | Ubuntu 主机客户端和管理员命令 |
| `include` | 公共头文件和数据类型 |
| `tests` | 主机业务、协议、Handler 和 TCP 测试 |
| `tools/board` | LVGL 显示探针和开发板网络探针 |
| `third_party` | 当前项目直接使用的第三方源码 |
| `reference` | LVGL、驱动、FreeType 和平台参考资料；不要直接删除 |
| `docs` | 架构、测试、验收和演示文档 |
| `.scratch/智慧医疗终端` | PRD、issue 和历史计划资料 |
| `.scratch/operations` | 历史 Codex 工作指令，不参与编译 |
| `build` | 编译产物、测试程序、数据库和诊断文件 |

## 常用构建入口

根 `Makefile` 是唯一构建目标入口；在 Ubuntu 共享目录执行：

```bash
cd /mnt/hgfs/codex
make -B test                 # 主机回归
make host                    # 主机程序与客户端
make board                   # 正式终端 + 两个板端探针
make build-all               # 主机目标 + 全部板端目标
make board-clean             # 清理三个板端产物
```

也可以分别构建板端目标：

```bash
make board-terminal
make board-lvgl-smoke
make board-net-probe
```

PowerShell 的 `build.ps1` 只负责调度同一组根 Makefile 目标，不再维护重复的 C 源文件列表：

```powershell
.\build.ps1 -Target host
.\build.ps1 -Target test
.\build.ps1 -Target board
.\build.ps1 -Target build-all
```

如果当前 PowerShell 没有 GNU Make，请直接使用上面的 Ubuntu 命令；不会再走一套与根 Makefile 不一致的隐式编译逻辑。

主要产物：

```text
build/linux/clinic_server
build/test/*
build/board/clinic_terminal
build/board/clinic_lvgl_smoke
build/board/clinic_net_probe
build/data/clinic.db
build/diagnostics/*.plist
```

## 运行和部署

服务器默认使用 `build/data/clinic.db`，开发板程序仍部署到 `/IOT/clinic_terminal`。目录整理没有改变 TCP 端口、数据库路径、动态库路径或开发板运行命令。

```bash
./build/linux/clinic_server
```

```sh
cd /IOT
chmod +x clinic_terminal
export LD_LIBRARY_PATH=/IOT:/lib:/usr/lib
./clinic_terminal
```

## 设备演示录像

- [医路通 S5P6818 设备演示（2026-07-21）](evidence/clinic-terminal-device-demo-2026-07-21.mp4)

该录像是历史设备演示证据，展示开发板上的医路通终端界面与业务操作过程；它不等同于本次归档会话重新执行的完整验收。

## 当前整理边界

本轮只整理辅助目录和工程入口说明，没有移动 `common`、`core`、`store`、`server`、`terminal` 或正式板端源码，也没有修改协议、数据库结构和业务语义。`reference` 中的实际 LVGL 运行依赖将在后续确认后再单独提取。
