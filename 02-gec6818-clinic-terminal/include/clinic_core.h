/*
 * 文件作用（答辩）：声明服务器业务核心的窄接口。
 * Core 只持有 ClinicStore 指针，clinic_core_handle() 接收结构化请求并产生结构化响应，
 * 因而可以脱离 TCP、JSON、SQLite 和 LVGL 单独测试业务规则。
 */
#ifndef CLINIC_CORE_H
#define CLINIC_CORE_H

#include "clinic_store.h"
#include "clinic_types.h"

typedef struct ClinicCore
{
    ClinicStore *store;
} ClinicCore;

/* 绑定 Store 依赖；不负责打开数据库。 */
int clinic_core_init(ClinicCore *core, ClinicStore *store);

/* 执行一次结构化业务请求；协议层必须在调用前完成 JSON 校验。 */
int clinic_core_handle(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response);

#endif
