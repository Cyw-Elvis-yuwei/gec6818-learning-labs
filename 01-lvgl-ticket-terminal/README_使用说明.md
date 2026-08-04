# LVGL 登录背景图 800x480 转换工具

这个工具用于把任意图片处理成开发板屏幕尺寸：

```text
800 x 480
```

适合你的项目登录背景：

```c
LV_IMG_DECLARE(img_login_bg_classic);
LV_IMG_DECLARE(img_login_bg_vintage);
```

---

## 1. 安装依赖

Ubuntu 里执行：

```bash
sudo apt install python3-pil
```

如果没有 sudo 权限，可以试：

```bash
pip3 install pillow
```

---

## 2. 单张图片转换

```bash
python3 resize_to_800x480.py 原图.jpg 输出图.png
```

示例：

```bash
python3 resize_to_800x480.py bg.jpg bg_800x480.png
```

---

## 3. 两张登录背景一键转换

假设你有两张图：

```text
bg1.jpg
bg2.jpg
```

执行：

```bash
bash 一键生成800x480背景.sh bg1.jpg bg2.jpg
```

会生成：

```text
img_login_bg_classic_800x480.png
img_login_bg_vintage_800x480.png
```

---

## 4. 再转成 LVGL C 文件

把生成的 PNG 拿去 LVGL Image Converter 转换成 C array。

建议参数：

```text
Color format: True color
Output format: C array
```

变量名填写：

```text
img_login_bg_classic
img_login_bg_vintage
```

生成后分别保存为：

```text
image/img_login_bg_classic.c
image/img_login_bg_vintage.c
```

然后重新编译：

```bash
make clean
make -j$(nproc)
```

---

## 5. 推荐做法

为了少改代码，保持变量名不变：

```text
img_login_bg_classic
img_login_bg_vintage
```

这样 `ui_login.c` 里面不用改。
