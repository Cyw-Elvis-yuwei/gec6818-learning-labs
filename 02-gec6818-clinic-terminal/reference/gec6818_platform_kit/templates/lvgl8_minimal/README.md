# LVGL 8 最小平台模板

## 中文判断提示

- 当前状态：安全占位模板
- 这是什么意思：它不含 LVGL、驱动实现、FreeType、字体或业务代码，且默认不能作为已验证配置编译。
- 是否还需要继续讨论：不需要先讨论 UI；先用作用域匹配的 `confirmed-runtime` profile 填充平台字段。
- 建议下一步：从平台包运行严格校验和 bootstrap，再按生成 README 的命令验证。
- 还缺什么：已确认的编译器、framebuffer、像素格式、触摸与部署参数。

这个目录只定义新 GEC6818 + LVGL 8 项目的最小配置表面：

- `platform_config.example.h`：硬件参数占位和 LVGL 8 代际保护；
- `toolchain.example.mk`：不选择编译器、不猜 sysroot/ABI、不链接旧库的 Make 片段。

示例中的 0、空字符串、`UNKNOWN` 和 `#error` 是故意的安全阻断，不是默认硬件值。显示和输入只能由 `scope=board` 的 `confirmed-runtime` 证据解除阻断；工具链字段只能使用 `scope=toolchain`。`scope=host-backend` 永远不能解除平台阻断。使用 bootstrap 的 `--allow-unverified-defaults` 时，生成物必须继续带 `GEC6818_UNVERIFIED_DEFAULTS=1`，不得部署到开发板。

模板不复制 LVGL 8.3.0 源码；具体项目应单独取得可追溯版本。FreeType 与许可明确的 CJK 字体也应由具体项目独立配置。
