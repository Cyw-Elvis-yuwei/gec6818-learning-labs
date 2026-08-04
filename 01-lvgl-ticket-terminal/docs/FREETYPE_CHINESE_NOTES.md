# Direct Chinese + FreeType Notes

This version uses direct Chinese strings in `ui_text.h`.

Required conditions:

1. Source files must be saved as UTF-8, preferably UTF-8 without BOM.
2. The board must contain the FreeType font file used by `ui_font.c`:
   `/font/simkai.ttf`
3. If the font is placed next to `demo`, change `ui_font.c` to:
   `#define LV_FONT_KAI "./simkai.ttf"`
4. Every label/text-area that displays Chinese must call `set_ft(obj, size)`.
5. If the compiler or editor still causes encoding issues, add these flags to CFLAGS:
   `-finput-charset=UTF-8 -fexec-charset=UTF-8`

Quick checks:

```bash
file -bi ui_text.h
ls -l /font/simkai.ttf
make clean
make -j$(nproc)
```


---

> 答辩版说明：本包已补充中文注释和关键文件说明，便于零基础讲解。
