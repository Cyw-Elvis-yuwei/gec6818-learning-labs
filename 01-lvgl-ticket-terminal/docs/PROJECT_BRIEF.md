# 项目简报

更新时间：2026-07-23 10:20

> 这是人和 Codex 共用的一页项目入口。本文件只记录从当前项目文件核对出的稳定事实；阶段进度见 `PROJECT_STATUS.md`，验证事实见 `TEST_STATUS.md`，稳定架构见 `ARCHITECTURE.md`。

## 30 秒摘要

- 项目目标：当前项目文档将其定义为“景区门票自助售票终端系统”，提供登录、商品展示、购票/付款界面、管理员登录和商品增删改查。
- 主要使用者：售票终端普通用户和管理员；这是项目文档定义，尚未由当前任务中的用户真机验收确认。
- 当前交付范围：LVGL 8.3.0 的 Linux framebuffer/evdev 图形终端，目标显示逻辑分辨率为 800×480，并包含 FreeType 中文字体和本地文件数据。
- 明确不在范围内：当前没有确认云服务、远程 API、网络协议、服务器或目标开发板具体型号。
- 当前状态入口：见 `PROJECT_STATUS.md`。

## 快速入口

| 项目项 | 已核对内容 |
| --- | --- |
| 项目根目录 | `E:\test\GZ2617\VMware_shared_folder\six` |
| 主入口 | `src/core/main.c` 的 `main()` |
| 顶层构建入口 | `Makefile`：`arm-linux-gcc`，产物名 `demo`；`CMakeLists.txt` 是另一套入口，不能默认视为与 Makefile 等价 |
| 启动或运行入口 | `release/ticket_terminal_release/run.sh`，或在目标机 `/root/ticket_terminal` 执行 `./demo`；尚未由用户在目标机确认 |
| 自动化测试入口 | 当前未发现自动化测试命令；`docs/TEST_FLOW.md` 是人工终端/开发板测试流程 |
| 部署、上传或真机入口 | `release/ticket_terminal_release/install.sh`；需要用户在目标板执行和验收 |

`README_使用说明.md` 主要说明登录背景图转换工具，不是整套售票终端的运行入口；接管时以当前源码、Makefile、发布说明和目标机验证为准。

## 技术栈与运行环境

| 层次 | 技术或环境 | 用途 | 证据文件 |
| --- | --- | --- | --- |
| 语言 | C，Makefile 指定 `-std=c99` | 应用和 LVGL 驱动集成 | `Makefile`、`src/` |
| 图形框架 | LVGL 8.3.0 | 控件、事件、绘制和页面 | `lvgl/lvgl.h`、`src/core/main.c` |
| 显示/输入 | Linux framebuffer `/dev/fb0`、evdev `/dev/input/event0` | 目标板显示和触摸/输入 | `lv_drv_conf.h`、`lv_drivers/` |
| 字体与资源 | FreeType、生成的 LVGL C 图片资源 | 中文字体、背景、商品和二维码 | `src/common/ui_font.c`、`image/`、`freetypelib/` |
| 构建系统 | GNU Make 交叉编译；CMake 备用入口 | 生成 `demo` | `Makefile`、`CMakeLists.txt` |
| 目标平台 | Linux ARM 意图；具体开发板、位宽、工具链版本未确认 | 终端运行环境 | `Makefile`、`release/ticket_terminal_source/README_编译运行说明.txt` |

## 目录与模块地图

| 路径 | 类型 | 作用 | 关键入口 |
| --- | --- | --- | --- |
| `src/core/` | core | LVGL、显示、输入初始化和主循环 | `main.c` |
| `src/login/` | feature/data | 普通用户登录、注册、记住密码 | `ui_login.c`、`file_user.c` |
| `src/data/` | data | 商品文件读取、查找、写回和库存更新 | `file_goods.c` |
| `src/main_ui/` | feature/state | 商品展示、翻页、购物车、购买和付款界面 | `ui_main.c`、`ui_goods_view.c` |
| `src/admin/` | feature/data | 管理员登录、管理页和商品 CRUD | `ui_admin_page.c`、`ui_admin_goods_store.c` |
| `src/common/` | shared | FreeType 字体、软键盘、消息框和文本常量 | `ui_font.c`、`ui_keyboard.c`、`ui_text.h` |
| `image/` | generated/resource | 背景、商品图片和支付二维码的 C 数组 | `img_*.c` |
| `lvgl/`、`lv_drivers/` | third-party/dependency | LVGL 核心和 Linux/其他平台驱动 | 各自 `CMakeLists.txt`、`*.mk` |
| `freetypelib/`、`lib/` | dependency | FreeType 头文件和库文件 | `Makefile` 链接参数 |
| `release/` | release/reference | `demo`、字体、数据和部署/运行脚本 | `ticket_terminal_release/` |
| `.scratch/`、`docs/` | project docs | 问题记录、测试流程和接管文档 | `.scratch/`、本目录 |

## 核心流程

```text
main()
  → LVGL + fbdev + evdev 初始化
  → 登录页
  → 普通用户登录成功后清理当前屏幕并创建售票主页面
  → 商品展示 / 翻页 / 购买 / 付款 / 管理员入口
  → 管理员验证成功后显示管理页
  → 新增、删除、修改、查询商品并写回本地文件
```

## 配置与外部依赖

| 名称 | 作用 | 来源或位置 | 当前状态 |
| --- | --- | --- | --- |
| `lv_conf.h` | LVGL 色深、周期、文件系统、图片解码和 FreeType 开关 | 项目根目录 | 已核对 |
| `lv_drv_conf.h` | fbdev/evdev 开关、设备节点和触摸校准配置 | 项目根目录 | 已核对；目标机参数未实测 |
| `arm-linux-gcc` | Makefile 的交叉编译器名称 | `Makefile` | 已声明；版本和实际可用性未确认 |
| FreeType、libm | 字体和数学库 | `freetypelib/`、`Makefile` | 已声明；本次未链接验证 |
| `/font/simkai.ttf` | 运行期中文字体 | `ui_font.c`、发布脚本 | 路径已声明；目标机存在性未确认 |
| `/user.txt`、`/save_pwd.txt` | 用户和记住密码数据 | `src/login/file_user.h` | 路径已声明；目标机权限未确认 |
| 商品数据文件 | `file_goods.c` 读取/写入 `/goods.txt`；管理员模块默认写入 `goods_utf8.txt`，失败时回退 `/goods_utf8.txt` | `src/data/file_goods.c`、`src/admin/ui_admin_goods_store.c` | 存在路径冲突，后续功能阶段必须先确认 |

## 推荐阅读顺序

1. `AGENTS.md` 及 `docs/agents/` 下的项目协作规则。
2. 本文件。
3. `PROJECT_STATUS.md`。
4. `TEST_STATUS.md`。
5. `ARCHITECTURE.md`。
6. `Makefile`、`CMakeLists.txt`、`src/core/main.c`。
7. 与当前阶段直接相关的模块头文件和实现。
8. `docs/TEST_FLOW.md`、发布说明和用户最新原始验证结果。

## Codex 接管顺序

新任务先读取项目规则、本简报、项目状态、验证状态、架构文档和用户最新原始验证结果。恢复时只读，不递归扫描整个仓库，不自动编译、运行、测试或进入下一阶段。状态文档只提供事实索引，不能替代当前源码、配置或用户验收证据。每次对话只承担一个明确小阶段；用户编译、构建、手动运行、真机验收和最终确认由用户完成。

## 已知缺口

- 当前记录没有用户对当前源码执行的构建、运行或开发板验收结果。
- 开发板型号、SoC、ARM 位宽/架构、Linux 版本、工具链完整版本和 framebuffer 像素格式未确认。
- Makefile 与 CMake 的依赖/链接参数不同；当前未确认哪一个是后续唯一构建入口。
- `file_goods.c` 与管理员商品存储模块的商品路径不一致。
- 当前工作区的 `.git` 目录不能被 Git 命令识别为有效仓库，暂无 commit 级追溯。
