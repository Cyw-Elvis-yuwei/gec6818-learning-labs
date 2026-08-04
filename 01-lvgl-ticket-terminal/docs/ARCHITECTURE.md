# 项目架构

更新时间：2026-07-23 10:20

> 本文件记录当前源码和配置能够证明的稳定模块、数据流、依赖和运行环境职责。普通功能进度放在 `PROJECT_STATUS.md`，真实验证放在 `TEST_STATUS.md`。

## 系统目标与边界

- 系统目标：在 Linux framebuffer 终端上运行 LVGL 景区门票自助售票界面。
- 输入：用户登录/注册、触摸或 evdev 输入、商品文件和管理员商品操作。
- 输出：800×480 LVGL 页面、商品/付款界面、消息框和本地数据文件变化。
- 范围外：当前没有确认网络服务、远程 API、服务器、云端数据或具体开发板型号。

## 运行环境与职责

| 环境 | 运行内容 | 职责 | 不承担的职责 |
| --- | --- | --- | --- |
| Windows workspace | 文件读取、文档维护和源码协作 | 保存项目文件和接管事实 | 不替代 ARM 编译、目标板运行或真机验收 |
| Ubuntu/交叉编译环境 | `make`/`arm-linux-gcc` 构建 | 生成目标 `demo` | 不证明目标板显示、输入或功能行为 |
| 目标 Linux 开发板 | `/root/ticket_terminal/demo`、fbdev、evdev、字体和数据文件 | 手动运行、硬件观察和最终功能确认 | 不由 Codex 在本阶段代为验收 |

## 模块地图

| 模块 | 职责 | 对外入口 | 允许依赖 | 禁止依赖 |
| --- | --- | --- | --- | --- |
| `src/core` | 初始化 LVGL、显示、输入和主循环 | `main()` | LVGL、`fbdev`、`evdev`、登录模块 | 未确认的网络/服务器 |
| `src/login` | 普通用户登录、注册、记住密码 | `ui_login_create()`、`file_user_*()` | LVGL、公共字体/键盘、文件 I/O | 直接承担商品 CRUD |
| `src/data` | 商品结构、读取、查找、写回、库存更新 | `file_goods_*()` | C 标准库、商品数据模型 | UI 生命周期逻辑 |
| `src/main_ui` | 商品列表、翻页、购物车、购买/付款入口 | `ui_main_create()`、`refresh_page()` | `src/data`、common、LVGL、图片资源 | 直接管理 Linux 设备初始化 |
| `src/admin` | 管理员登录、商品管理页面和 CRUD | `ui_admin_page_*()`、`ui_admin_goods_*()` | main state、common、LVGL、文件 I/O | 改变登录/设备驱动协议 |
| `src/common` | 字体、键盘、消息框和文本常量 | `set_ft()`、键盘/消息框接口 | LVGL、FreeType、输入法资源 | 具体业务数据持久化 |
| `image` | 背景、商品、二维码的生成 C 资源 | `LV_IMG_DECLARE` 对应符号 | LVGL 资源格式 | 运行时文件 I/O |
| `lvgl` / `lv_drivers` | 第三方图形框架和平台驱动 | LVGL 8 API、fbdev/evdev | Linux ABI、项目配置 | 本项目业务规则 |

## 主数据流

```text
/dev/fb0 + /dev/input/event0
        ↓
src/core/main.c: lv_init → fbdev_init/evdev_init → lv_timer_handler
        ↓
ui_login_create → file_user (/user.txt, /save_pwd.txt)
        ↓ 登录成功
ui_main_create → file_goods (/goods.txt) → 商品展示/购买/付款
        ↓ 管理员入口
管理员登录 → ui_admin_page → ui_admin_goods_store
        ↓
goods_utf8.txt 或 /goods_utf8.txt（与 file_goods.c 的 /goods.txt 存在路径冲突）
```

字体由 `src/common/ui_font.c` 从 `/font/simkai.ttf` 加载；图片由 `image/img_*.c` 静态链接到程序。

## 线程、进程与对象生命周期

- 线程或进程模型：`main.c` 包含 `pthread.h`，但当前针对源码的符号核对未发现 `pthread_create`；已确认的调度模型是单一主循环调用 `lv_timer_handler()`。
- UI 所有权：当前 UI 创建和事件回调均在 LVGL 主循环上下文中；`lv_scr_act()` 是页面对象的根上下文。
- 共享状态与同步方式：`ui_main_state.c` 定义商品列表、购物车、页码和页面控件指针等全局状态；未发现独立线程同步协议。
- 创建、切换和销毁规则：登录成功路径清理当前屏幕后创建主页面；管理员页面通过隐藏/显示复用；注册弹窗关闭路径使用异步删除；完整长期运行和重复切换仍需 V-04/V-05 真机验证。

## 网络与协议

不适用（当前关键文件和构建配置未发现网络连接、服务端接口或自定义协议；付款界面使用静态二维码资源，不能据此推断在线支付能力）。

## 存储与数据模型

- 存储位置：用户账号 `/user.txt`，记住密码 `/save_pwd.txt`；商品读取/写入模块使用 `/goods.txt`；管理员保存模块默认使用相对 `goods_utf8.txt`，失败时尝试 `/goods_utf8.txt`。
- 核心实体或表：`goods_item_t { scenic, price, stock, img_name }`；`goods_list_t` 最多 20 项；文本格式为 `名称|价格|库存|图片编号`。
- 迁移与兼容规则：当前未确认；路径冲突必须在后续代码阶段先由用户确认目标运行目录和持久化策略。

## 关键入口

| 类型 | 路径或标识符 | 作用 |
| --- | --- | --- |
| 主入口 | `src/core/main.c:main` | 初始化设备与 LVGL，创建登录页，进入主循环 |
| 构建入口 | `Makefile` | 使用 `arm-linux-gcc` 生成 `demo` |
| 备用构建入口 | `CMakeLists.txt` | CMake 组织 LVGL、driver 和应用源文件；链接参数与 Makefile 不完全一致 |
| 用户运行入口 | `release/ticket_terminal_release/run.sh` | 进入 `/root/ticket_terminal` 并执行 `demo` |
| 管理员入口 | `src/main_ui/ui_goods_view.c` → `ui_admin_dialog.c` | 从售票页面进入管理员登录和管理页 |
| 人工验证入口 | `docs/TEST_FLOW.md` | 管理员 CRUD、购买、持久化和循环稳定性流程 |

## 重要设计决定

- 应用逻辑分辨率固定为 800×480，显示使用 LVGL 8 的 `lv_disp_drv_t`/`lv_indev_drv_t` 旧 API。
- `lv_conf.h` 启用 32 位颜色、30 ms 刷新/输入周期、FreeType 和文件系统；具体 framebuffer 位域仍需目标机实测。
- Makefile 采用 `-lm`、FreeType 路径和 `-static`；这只是当前构建配置，不是已验证的链接结果。
- 设备节点固定为 `/dev/fb0` 和 `/dev/input/event0`；目标板编号变化时需要新的配置/功能阶段处理。

## 架构缺口

- 目标板型号、CPU/SoC、位宽、Linux 版本、工具链完整版本、实际 framebuffer 像素格式和触摸原始范围未确认。
- `file_goods.c` 与管理员商品存储模块使用不同商品路径，读写持久化是否一致未确认。
- Makefile 与 CMake 的链接/依赖参数不同，当前未确认后续应保留哪条构建路径。
- 根目录存在旧 `demo` 和发布目录产物；其文件存在、大小和时间不能作为当前源码功能证据。
- 当前 workspace 的 `.git` 不能被 Git 命令识别为有效仓库，无法记录 commit 级追溯。
