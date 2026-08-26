/*
 * 文件名：ui_text.h
 * 版本说明：中文注释。
 * 文件作用：中文文本宏集中管理文件。界面文字统一放在这里，便于修改和查阅。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_TEXT_H
#define UI_TEXT_H

/* UI text macros. Source files must be saved as UTF-8 and rendered with FreeType. */
#define TXT_SCREEN_TITLE         "景区门票自助售票"
#define TXT_PAY                  "付款"
#define TXT_BUY                  "购买"
#define TXT_CONFIRM              "确认"
#define TXT_CANCEL               "取消"
#define TXT_STOCK                "库存"
#define TXT_PRICE                "价格"
#define TXT_SUBTOTAL             "小计"
#define TXT_TOTAL                "合计"
#define TXT_YUAN                 "元"

#define TXT_TIP                  "提示"
#define TXT_PROMPT               "提示"
#define TXT_SUCCESS              "成功"

#define TXT_ADMIN                "管理员"
#define TXT_ADMIN_LOGIN          "管理员登录"
#define TXT_ACCOUNT              "账号"
#define TXT_PASSWORD             "密码"
#define TXT_ADMIN_LOGIN_FAILED   "账号或密码错误"
#define TXT_ADMIN_LOGIN_SUCCESS  "管理员登录成功"

#define TXT_ADMIN_MANAGE_TITLE   "管理员维护"
#define TXT_ADMIN_CURRENT_SCENIC "当前景区"
#define TXT_ADMIN_CURRENT_PRICE  "当前票价"
#define TXT_ADMIN_CURRENT_STOCK  "当前库存"
#define TXT_ADMIN_NEW_PRICE      "新票价"
#define TXT_ADMIN_NEW_STOCK      "新库存"
#define TXT_ADMIN_PREV_ITEM      "上一项"
#define TXT_ADMIN_NEXT_ITEM      "下一项"
#define TXT_ADMIN_SAVE           "保存"
#define TXT_ADMIN_CLOSE          "关闭"
#define TXT_ADMIN_SAVE_SUCCESS   "保存成功"
#define TXT_ADMIN_SAVE_FAILED    "保存失败"
#define TXT_ADMIN_PRICE_FORMAT_ERR "票价格式错误"
#define TXT_ADMIN_STOCK_FORMAT_ERR "库存格式错误"
#define TXT_ADMIN_PRICE_NEGATIVE "票价不能小于0"
#define TXT_ADMIN_STOCK_NEGATIVE "库存不能小于0"
#define TXT_ADMIN_NO_GOODS_DATA  "请先加载商品数据"

#define TXT_WECHAT               "微信"
#define TXT_ALIPAY               "支付宝"
#define TXT_WECHAT_QR            "微信二维码"
#define TXT_ALIPAY_QR            "支付宝二维码"

#define TXT_INPUT_NUM            "请输入购买数量"
#define TXT_INPUT_MONEY          "请输入实付金额"
#define TXT_MONEY_ERR            "请输入有效金额"
#define TXT_MONEY_SHORT          "付款金额不足"
#define TXT_PAY_INSUFFICIENT     "付款金额不足"
#define TXT_PAY_SUCCESS          "付款成功"

#define TXT_NO_GOODS             "购物车为空"
#define TXT_CART_EMPTY           "购物车为空，请先购买"
#define TXT_NUM_ERR              "请选择有效数量"
#define TXT_SELECT_VALID_QTY     "请选择有效数量"
#define TXT_STOCK_SHORT          "库存不足"
#define TXT_THIS_STOCK_SHORT     "该商品库存不足"
#define TXT_CANNOT_OVER_STOCK    "不能超过库存"
#define TXT_CART_FULL            "购物车已满"
#define TXT_ADD_CART_OK          "已加入购物车"
#define TXT_GOODS_ERROR          "商品数据异常"

#define TXT_REMAINING            "剩余"
#define TXT_PAGE_PREFIX          "第"
#define TXT_PAGE_SUFFIX          "页"
#define TXT_PRE_PAGE             "< 上一页"
#define TXT_NEXT_PAGE            "下一页 >"

/* Compatibility aliases used by the current ui_main.c version. */
#define TXT_PAGE                         TXT_PAGE_PREFIX
#define TXT_PAGE_END                     TXT_PAGE_SUFFIX
#define TXT_GOODS_DATA_ERROR             TXT_GOODS_ERROR
#define TXT_PLEASE_SELECT_VALID_QUANTITY TXT_SELECT_VALID_QTY
#define TXT_INSUFFICIENT_STOCK           TXT_STOCK_SHORT
#define TXT_ADDED_TO_CART                TXT_ADD_CART_OK


/* Admin product CRUD page text. */
#define TXT_ADMIN_GOODS_TITLE            "商品管理"
#define TXT_ADMIN_BTN_ADD                "新增商品"
#define TXT_ADMIN_BTN_DELETE             "删除商品"
#define TXT_ADMIN_BTN_MODIFY             "修改商品"
#define TXT_ADMIN_BTN_QUERY              "查询商品"
#define TXT_ADMIN_GOODS_LIST             "商品列表"
#define TXT_GOODS_NAME                   "商品名称"
#define TXT_GOODS_PRICE                  "售价"
#define TXT_GOODS_LEFT_STOCK             "剩余库存"
#define TXT_IMAGE_ID                     "图片ID"
#define TXT_ADD_GOODS_TITLE              "新增商品"
#define TXT_DELETE_GOODS_TITLE           "删除商品"
#define TXT_MODIFY_GOODS_TITLE           "修改商品"
#define TXT_QUERY_GOODS_TITLE            "查询商品"
#define TXT_SAVE                         "保存"
#define TXT_CLOSE                        "关闭"
#define TXT_DELETE                       "删除"
#define TXT_QUERY                        "查询"
#define TXT_GOODS_ADD_SUCCESS            "新增成功"
#define TXT_GOODS_DELETE_SUCCESS         "删除成功"
#define TXT_GOODS_MODIFY_SUCCESS         "修改成功"
#define TXT_GOODS_NOT_FOUND              "商品不存在"
#define TXT_GOODS_ALREADY_EXISTS         "商品已存在"
#define TXT_GOODS_NAME_EMPTY             "请输入商品名称"
#define TXT_GOODS_PRICE_ERR              "售价格式错误"
#define TXT_GOODS_STOCK_ERR              "库存格式错误"
#define TXT_GOODS_SAVE_FAILED            "商品保存失败"
#define TXT_GOODS_FULL                   "商品数量已满"
#define TXT_QUERY_RESULT                 "查询结果"
#define TXT_GOODS_BAD_CHAR               "商品名称或图片ID不能包含竖线"
#define TXT_REQUIRED_INFO                "请填写完整商品信息"
#define TXT_DEFAULT_IMAGE_HINT           "img_product_01~08"

#endif /* UI_TEXT_H */
