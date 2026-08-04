cmake_minimum_required(VERSION 3.1)

project(lvgl_fb)

add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

set(PROJECT_SOURCES
    src/core/main.c
    src/core/mouse_cursor_icon.c
    src/login/file_user.c
    src/login/ui_login.c
    src/data/file_goods.c
    src/main_ui/ui_main.c
    src/main_ui/ui_main_state.c
    src/main_ui/ui_goods_view.c
    src/main_ui/ui_buy_dialog.c
    src/main_ui/ui_pay_dialog.c
    src/main_ui/ui_qr_display.c
    src/common/ui_font.c
    src/common/ui_msgbox.c
    src/common/ui_keyboard.c
    src/admin/ui_admin_dialog.c
    src/admin/ui_admin_page.c
    src/admin/ui_admin_goods_store.c
    src/admin/ui_admin_add_dialog.c
    src/admin/ui_admin_delete_dialog.c
    src/admin/ui_admin_modify_dialog.c
    src/admin/ui_admin_query_dialog.c
    image/img_login_bg_classic.c
    image/img_login_bg_vintage.c
    image/img_product_01.c
    image/img_product_02.c
    image/img_product_03.c
    image/img_product_04.c
    image/img_product_05.c
    image/img_product_06.c
    image/img_product_07.c
    image/img_product_08.c
    image/img_wechat_qr.c
    image/img_alipay_qr.c
)

add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    .
    src/core
    src/login
    src/main_ui
    src/admin
    src/common
    src/data
    lvgl
    lvgl/src
    lvgl/src/font
    lv_drivers
    include
    freetypelib/include/freetype2
)

target_link_libraries(${PROJECT_NAME} PRIVATE lvgl lvgl::examples lvgl::demos lvgl::drivers)
