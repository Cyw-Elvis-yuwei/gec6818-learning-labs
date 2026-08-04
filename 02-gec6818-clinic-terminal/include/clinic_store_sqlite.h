/*
 * 文件作用（答辩）：声明 SQLite Store 的创建入口。
 * clinic_store_sqlite_open() 打开数据库、初始化表和基础数据，并把 SQLite operations/context
 * 安装到 ClinicStore；之后 Core 仍只通过 clinic_store.h 的抽象接口访问数据。
 */
#ifndef CLINIC_STORE_SQLITE_H
#define CLINIC_STORE_SQLITE_H

#include "clinic_store.h"

/* 成功后 store->operations/context 已可供 Core 使用；失败时不会留下半初始化 Store。 */
ClinicStoreStatus clinic_store_sqlite_open(
    ClinicStore *store,
    const char *database_path);

#endif
