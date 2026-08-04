#
# Makefile
#
CC = arm-linux-gcc

LVGL_DIR_NAME ?= lvgl
LVGL_DIR ?= ${shell pwd}

BIN = demo

prefix ?= /usr
bindir ?= $(prefix)/bin

# =========================
# 编译参数
# =========================
CFLAGS ?= -O3 -g0 \
-I$(LVGL_DIR)/ \
-I$(LVGL_DIR)/src/core \
-I$(LVGL_DIR)/src/login \
-I$(LVGL_DIR)/src/main_ui \
-I$(LVGL_DIR)/src/admin \
-I$(LVGL_DIR)/src/common \
-I$(LVGL_DIR)/src/data \
-I$(LVGL_DIR)/lvgl \
-I$(LVGL_DIR)/lvgl/src \
-I$(LVGL_DIR)/lv_drivers \
-I$(LVGL_DIR)/include \
-I$(LVGL_DIR)/freetypelib/include/freetype2 \
-Wall -Wshadow -Wundef -Wmissing-prototypes \
-Wno-discarded-qualifiers -Wextra -Wno-unused-function \
-Wno-error=strict-prototypes -Wpointer-arith -fno-strict-aliasing \
-Wno-error=cpp -Wuninitialized -Wmaybe-uninitialized \
-Wno-unused-parameter -Wno-missing-field-initializers \
-Wtype-limits -Wsizeof-pointer-memaccess -Wno-format-nonliteral \
-Wno-cast-qual -Wunreachable-code -Wno-switch-default \
-Wreturn-type -Wmultichar -Wformat-security \
-Wno-ignored-qualifiers -Wno-error=pedantic -Wno-sign-compare \
-Wno-error=missing-prototypes -Wdouble-promotion -Wclobbered \
-Wdeprecated -Wempty-body -Wstack-usage=2048 \
-Wno-unused-value -std=c99 \
-finput-charset=UTF-8 -fexec-charset=UTF-8 \
-mfloat-abi=softfp

# =========================
# 链接参数
# =========================
LDFLAGS ?= -lm -L$(LVGL_DIR)/freetypelib/lib -lfreetype -static

# =========================
# LVGL 源码
# =========================
include $(LVGL_DIR)/lvgl/lvgl.mk
include $(LVGL_DIR)/lv_drivers/lv_drivers.mk

# =========================
# 项目源码：程序入口
# =========================
MAINSRC = $(LVGL_DIR)/src/core/main.c

# =========================
# 项目源码：鼠标图标
# =========================
CSRCS += $(LVGL_DIR)/src/core/mouse_cursor_icon.c

# =========================
# 项目源码：登录注册
# =========================
CSRCS += $(LVGL_DIR)/src/login/file_user.c
CSRCS += $(LVGL_DIR)/src/login/ui_login.c

# =========================
# 项目源码：商品文件读写
# =========================
CSRCS += $(LVGL_DIR)/src/data/file_goods.c

# =========================
# 项目源码：主界面、商品展示、购买、付款
# =========================
CSRCS += $(LVGL_DIR)/src/main_ui/ui_main.c
CSRCS += $(LVGL_DIR)/src/main_ui/ui_main_state.c
CSRCS += $(LVGL_DIR)/src/main_ui/ui_goods_view.c
CSRCS += $(LVGL_DIR)/src/main_ui/ui_buy_dialog.c
CSRCS += $(LVGL_DIR)/src/main_ui/ui_pay_dialog.c
CSRCS += $(LVGL_DIR)/src/main_ui/ui_qr_display.c

# =========================
# 项目源码：通用模块
# =========================
CSRCS += $(LVGL_DIR)/src/common/ui_font.c
CSRCS += $(LVGL_DIR)/src/common/ui_msgbox.c
CSRCS += $(LVGL_DIR)/src/common/ui_keyboard.c

# =========================
# 项目源码：管理员后台
# =========================
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_dialog.c
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_page.c
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_goods_store.c
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_add_dialog.c
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_delete_dialog.c
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_modify_dialog.c
CSRCS += $(LVGL_DIR)/src/admin/ui_admin_query_dialog.c

# =========================
# 项目源码：图片资源
# =========================
CSRCS += $(LVGL_DIR)/image/img_login_bg_classic.c
CSRCS += $(LVGL_DIR)/image/img_login_bg_vintage.c
CSRCS += $(LVGL_DIR)/image/img_product_01.c
CSRCS += $(LVGL_DIR)/image/img_product_02.c
CSRCS += $(LVGL_DIR)/image/img_product_03.c
CSRCS += $(LVGL_DIR)/image/img_product_04.c
CSRCS += $(LVGL_DIR)/image/img_product_05.c
CSRCS += $(LVGL_DIR)/image/img_product_06.c
CSRCS += $(LVGL_DIR)/image/img_product_07.c
CSRCS += $(LVGL_DIR)/image/img_product_08.c
CSRCS += $(LVGL_DIR)/image/img_wechat_qr.c
CSRCS += $(LVGL_DIR)/image/img_alipay_qr.c

OBJEXT ?= .o

AOBJS = $(ASRCS:.S=$(OBJEXT))
COBJS = $(CSRCS:.c=$(OBJEXT))
MAINOBJ = $(MAINSRC:.c=$(OBJEXT))

SRCS = $(ASRCS) $(CSRCS) $(MAINSRC)
OBJS = $(AOBJS) $(COBJS) $(MAINOBJ)

all: default

%.o: %.c
	@$(CC) $(CFLAGS) -c $< -o $@
	@echo "CC $<"

default: $(AOBJS) $(COBJS) $(MAINOBJ)
	$(CC) -o $(BIN) $(MAINOBJ) $(AOBJS) $(COBJS) $(LDFLAGS)

clean:
	rm -f $(BIN) $(AOBJS) $(COBJS) $(MAINOBJ)

install:
	install -d $(DESTDIR)$(bindir)
	install $(BIN) $(DESTDIR)$(bindir)

uninstall:
	$(RM) -r $(addprefix $(DESTDIR)$(bindir)/,$(BIN))