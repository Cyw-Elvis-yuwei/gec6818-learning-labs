# 项目状态

更新时间：2026-07-23 10:20

## 当前摘要

- 当前阶段：当前阶段完成（已完成本次接管文档初始化）。
- 当前完成层级：needs-info。
- 最后一个由用户确认完成的阶段：needs-info；当前记录没有用户原始构建/运行/真机结果。
- 当前实现完成但未验证：源码中可见登录、售票主页面、购买/付款界面、管理员商品 CRUD 和 `file_goods` 缓冲区修复相关实现；这些只能证明文件内容存在，不能证明当前版本功能通过。
- 当前阻塞或 `needs-info`：缺少当前源码对应的用户构建证据、目标板信息和真机行为；商品读写路径、Make/CMake 权威入口仍需确认。
- 下一唯一阶段：用户在与当前源码对应的 Ubuntu/交叉编译环境执行 V-01 构建检查并回传摘要；收到结果前不进入代码修改或下一功能阶段。

只使用：`未开始`、`实现中`、`实现完成未验证`、`局部验证`、`当前阶段完成`、`needs-info`、`阻塞`。

## 模块状态

| 模块或范围 | 状态 | 证据 | 备注 |
| --- | --- | --- | --- |
| `src/core` | 实现完成未验证 | `src/core/main.c`、`CMakeLists.txt`、`Makefile` | 主入口初始化 LVGL、fbdev、evdev 并进入 `lv_timer_handler()` 循环；未在目标环境运行 |
| `src/login` | 实现完成未验证 | `ui_login.c/.h`、`file_user.c/.h` | 登录、注册和记住密码接口存在；用户文件权限和行为未验收 |
| `src/main_ui` | 实现完成未验证 | `ui_main.c`、`ui_goods_view.c`、`ui_main_state.c` | 商品展示、翻页、购买/付款入口存在；未做人工流程验证 |
| `src/admin` | 实现完成未验证 | `ui_admin_page.c`、`ui_admin_*_dialog.c`、`ui_admin_goods_store.c` | CRUD 接口存在；商品路径冲突和持久化行为未验收 |
| `src/common` / `image` | 实现完成未验证 | `ui_font.c`、`ui_keyboard.c`、`image/img_*.c` | 字体、键盘和图片资源存在；目标机字体加载未验收 |
| `lvgl` / `lv_drivers` / FreeType | needs-info | 目录、配置和 Makefile | 依赖文件存在，但未用当前源码完成链接验证 |
| `release/` | needs-info | 发布说明、`demo`、`install.sh`、`run.sh` | 只作为发布参考；旧 `demo` 的存在和时间不能证明当前源码功能正确 |

## 当前小阶段

- 唯一目标：建立人和 Codex 共用的项目接管文档。
- 连贯修改集合：创建 `docs/PROJECT_BRIEF.md`、`docs/PROJECT_STATUS.md`、`docs/TEST_STATUS.md`、`docs/ARCHITECTURE.md`。
- 用户验证路径：不适用，本阶段不改源码、不构建、不运行；后续唯一下一阶段使用 `TEST_STATUS.md` 的 V-01。
- 停止条件：文档创建并复核完成后停止，不自动开始编译、代码修改或设备验收。

## 已实现但尚未验证

- `src/core/main.c` 的 LVGL、800×480、fbdev、evdev 和主循环初始化路径已由源码核对，但尚未由用户在目标环境执行。
- 登录成功后清理当前屏幕并调用 `ui_main_create()` 的代码路径存在，但尚未由用户进行登录和页面切换验收。
- 管理员商品新增、删除、修改、查询和写回代码存在；`.scratch/verify-after-board/verify-file-goods-buffer-overflow.md` 明确要求回到 Ubuntu/开发板验证新增商品场景。
- 注册弹窗异步销毁和共享键盘头文件修复有 `.scratch/keyboard-register-dialog-fix.md` 记录，但当前没有可复核的环境、命令、退出码或设备证据。

## 当前阻塞与缺失信息

- 用户当前没有提供与本源码版本对应的 V-01 构建原始结果。
- 未确认开发板型号、CPU/SoC、目标 Linux、工具链完整版本、`/dev/fb0` 实际格式和 `/dev/input/event0` 实际设备。
- `file_goods.c` 使用 `/goods.txt`，管理员存储模块默认使用相对 `goods_utf8.txt` 并回退 `/goods_utf8.txt`；这会影响持久化验收。
- Makefile 使用 `arm-linux-gcc`、`-lfreetype`、`-lm`、`-static`，而 CMake 没有声明同一组链接参数；不能未经用户确认选择其一作为唯一入口。
- Git 命令在当前 workspace 返回“not a git repository”，暂无分支、commit 或差异追溯证据。

## 后续模块

- 用户构建和产物类型确认。
- 目标板部署、启动、登录、商品展示和触摸输入。
- 管理员 CRUD、商品持久化和 `file_goods` 缓冲区修复验证。
- 注册弹窗/软键盘稳定性与至少一轮循环稳定性验证。

这里只保留一个“下一唯一阶段”：先完成 V-01 构建检查并回传原始摘要；其他内容不是当前下发任务。

## 范围边界

- 当前项目范围：维护现有 LVGL 景区门票自助售票终端的源码事实、构建入口、运行依赖、阶段状态和用户验证证据。
- 明确不在范围内：本阶段不改源码、测试、Makefile、CMake、发布包、项目规则或设备配置；不连接远程服务；不替用户编译、运行、手动操作或真机验收。
- 必须保留的现有行为：800×480 LVGL 页面、Linux framebuffer/evdev 入口、登录后进入售票主页面、管理员入口和商品 `名称|价格|库存|图片编号` 数据格式；任何改变前必须另开 `work` 阶段并通过五项门禁。
