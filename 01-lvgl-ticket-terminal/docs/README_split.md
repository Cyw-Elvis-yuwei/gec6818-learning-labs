# 拆分版主界面 + 管理员商品 CRUD 功能说明

本包按“不同页面/功能单独存放”的方式组织代码，保留已有售票主页面、购买加减、支付二维码、键盘输入等功能，并按最新诉求重做管理员后台。

## 管理员新流程

1. 主页面点击“管理员”按钮。
2. 弹出管理员专属登录窗口。
3. 输入管理员账号、密码。
4. 校验成功后进入“商品管理页面”。
5. 商品管理页面包含四个操作按钮：
   - 新增商品
   - 删除商品
   - 修改商品
   - 查询商品
6. 每个按钮都会打开一个独立小窗口完成对应操作。
7. 管理页面左侧实时展示全部商品名称、售价、库存。
8. 新增、删除、修改后会立即刷新主页面和管理页列表，并写回 `goods_utf8.txt`。

## 管理员默认账号

位置：`ui_admin_dialog.c`

```c
#define ADMIN_ACCOUNT  "admin"
#define ADMIN_PASSWORD "123456"
```

## 新增/替换的管理员模块

```text
ui_admin_dialog.c/.h         管理员登录弹窗
ui_admin_page.c/.h           商品管理主页面，含商品列表和四个操作入口
ui_admin_goods_store.c/.h    商品查找、新增、删除、修改、保存文件
ui_admin_add_dialog.c/.h     新增商品窗口
ui_admin_delete_dialog.c/.h  删除商品窗口
ui_admin_modify_dialog.c/.h  修改商品窗口
ui_admin_query_dialog.c/.h   查询商品窗口
```

## 旧模块处理

旧的 `ui_admin_manage.c/.h` 已经不再作为主要功能使用。本包里保留了空兼容文件，避免你的 Makefile 旧条目立即报错。

后续整理时可以从 Makefile 里删除：

```make
ui_admin_manage.c
```

## Makefile 源文件列表

如果你的 Makefile 不是自动编译全部 `.c`，建议主界面相关源文件保持如下，且每个文件只能出现一次：

```make
ui_main.c \
ui_main_state.c \
ui_font.c \
ui_msgbox.c \
ui_keyboard.c \
ui_qr_display.c \
ui_pay_dialog.c \
ui_buy_dialog.c \
ui_goods_view.c \
ui_admin_dialog.c \
ui_admin_page.c \
ui_admin_goods_store.c \
ui_admin_add_dialog.c \
ui_admin_delete_dialog.c \
ui_admin_modify_dialog.c \
ui_admin_query_dialog.c
```

可选删除旧项：

```make
ui_admin_manage.c
```

如果仍然保留也能编译，因为本包中的该文件是空兼容单元。

## 商品文件路径

商品保存接口默认写入：

```c
#define GOODS_FILE_PATH "goods_utf8.txt"
```

位置：`ui_admin_goods_store.c`

如果你的程序实际读取路径不是这个，需要和 `file_goods_read_all()` 使用的路径保持一致。

## 商品格式

`goods_utf8.txt` 每行格式仍然是：

```text
商品名称|售价|库存|图片ID
```

示例：

```text
黄山风景区|190.00|500|img_product_01
```

新增商品窗口的图片 ID 可填：

```text
img_product_01 ~ img_product_08
```

## 编译步骤

先备份：

```bash
cp ui_main.c ui_main.c.bak
```

复制本包文件到工程目录后，检查 Makefile 中不要重复加入同一个 `.c` 文件。然后执行：

```bash
make clean
make -j$(nproc)
```

如果出现 `multiple definition`，说明 Makefile 里某个 `.c` 被重复加入，删掉重复项即可。


---

> 答辩版说明：本包已补充中文注释和关键文件说明，便于零基础讲解。
