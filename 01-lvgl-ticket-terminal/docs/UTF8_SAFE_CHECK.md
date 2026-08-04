# UTF-8 安全版说明

本版本保持项目前面已经验证可用的写法：所有 UI 中文文本都集中放在 `ui_text.h`，并且使用 `\x..` UTF-8 字节串宏定义。

管理员模块中不应直接出现中文源码字符串。可用命令检查：

```bash
grep -nP "[\x{4e00}-\x{9fff}]" *.c *.h
```

正常情况下，上面命令不应输出任何 `.c/.h` 中文行。

如果界面仍显示方框，优先检查 FreeType 字体路径：

```bash
ls -l /font/simkai.ttf
```

如果字体文件和程序在同一目录，也可以把 `ui_font.c` 中的：

```c
#define LV_FONT_KAI "/font/simkai.ttf"
```

改成：

```c
#define LV_FONT_KAI "./simkai.ttf"
```

并确保所有显示中文的 label/textarea 都调用了 `set_ft(...)`。
