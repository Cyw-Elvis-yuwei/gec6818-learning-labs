/*
 * 文件作用（答辩）：Store 抽象接口的安全转发层，用来隔离业务逻辑和具体数据库实现。
 * ClinicStore 内保存 operations 函数表和 context；Core 调用统一的 clinic_store_* 接口，
 * 本文件检查参数和函数指针后，再转发给 SQLite 实现。
 *
 * 这样 Core 可以独立测试，也可以替换存储实现而不改业务代码。本文件不写 SQL、不解析
 * JSON、不处理 socket 或 LVGL；具体数据查询和事务位于 clinic_store_sqlite.c。
 *
 * 初学者理解：ClinicStoreOperations 类似一张“函数菜单”，context 是具体实现自己的数据。
 * SQLite 打开时把菜单填成 sqlite_* 函数并把数据库连接放入 context；Core 调用的始终是
 * 本文件 clinic_store_* 包装函数，所以以后换成别的数据库也不必重写 Core。
 */
#include "clinic_store.h"

#include <stddef.h>
#include <string.h>

/* 未绑定实现前先把函数表和上下文清空，防止误调用野指针。 */
void clinic_store_init(ClinicStore *store)
{
    if (store != NULL)
    {
        store->operations = NULL;
        store->context = NULL;
    }
}

/* 用户写入转发：先检查 Store、函数指针、上下文和所有输出参数，再调用具体实现。 */
ClinicStoreStatus clinic_store_create_user(
    ClinicStore *store,
    const char *username,
    const char *password,
    int64_t *user_id)
{
    if (store == NULL || store->operations == NULL ||
        store->operations->create_user == NULL || store->context == NULL ||
        username == NULL || password == NULL || user_id == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    return store->operations->create_user(
        store->context,
        username,
        password,
        user_id);
}

ClinicStoreStatus clinic_store_find_user_by_username(
    ClinicStore *store,
    const char *username,
    ClinicStoredUser *user)
{
    if (store == NULL || store->operations == NULL ||
        store->operations->find_user_by_username == NULL ||
        store->context == NULL || username == NULL || user == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    return store->operations->find_user_by_username(
        store->context,
        username,
        user);
}

/* 列表接口还要验证调用者提供的数组容量，具体 SELECT 由实现层完成。 */
ClinicStoreStatus clinic_store_list_departments(
    ClinicStore *store,
    ClinicDepartment *departments,
    size_t capacity,
    size_t *count)
{
    if (store == NULL || departments == NULL || count == NULL || capacity == 0U)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    if (store->operations == NULL ||
        store->operations->list_departments == NULL ||
        store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    return store->operations->list_departments(
        store->context,
        departments,
        capacity,
        count);
}

ClinicStoreStatus clinic_store_list_doctors(
    ClinicStore *store,
    int64_t department_id,
    ClinicDoctor *doctors,
    size_t capacity,
    size_t *count)
{
    if (store == NULL || department_id <= 0 || doctors == NULL ||
        count == NULL || capacity == 0U)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    *count = 0U;
    if (store->operations == NULL ||
        store->operations->list_doctors == NULL ||
        store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }

    return store->operations->list_doctors(
        store->context,
        department_id,
        doctors,
        capacity,
        count);
}

/* 只有具体 close 成功后才解绑 operations/context，避免失败后丢失数据库句柄。 */
ClinicStoreStatus clinic_store_close(ClinicStore *store)
{
    ClinicStoreStatus status;

    if (store == NULL || store->operations == NULL ||
        store->operations->close == NULL || store->context == NULL)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }

    status = store->operations->close(store->context);
    if (status == CLINIC_STORE_OK)
    {
        store->operations = NULL;
        store->context = NULL;
    }

    return status;
}

/* 号单输出先清零；失败时调用者不会误读到旧号单内容。 */
ClinicStoreStatus clinic_store_create_ticket(
    ClinicStore *store,
    int64_t user_id,
    int64_t department_id,
    ClinicTicket *ticket)
{
    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (ticket == NULL || store == NULL || user_id <= 0 || department_id <= 0)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    if (store->operations == NULL ||
        store->operations->create_ticket == NULL || store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    return store->operations->create_ticket(
        store->context,
        user_id,
        department_id,
        ticket);
}

ClinicStoreStatus clinic_store_get_ticket(
    ClinicStore *store,
    int64_t ticket_id,
    ClinicTicket *ticket)
{
    ClinicStoreStatus status;

    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (ticket == NULL || store == NULL || ticket_id <= 0)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    if (store->operations == NULL ||
        store->operations->get_ticket == NULL || store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    status = store->operations->get_ticket(
        store->context,
        ticket_id,
        ticket);
    if (status != CLINIC_STORE_OK)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    return status;
}

/* 当前号单与排队摘要属于同一次查询结果，任一失败时两者都清零。 */
ClinicStoreStatus clinic_store_get_current_ticket(
    ClinicStore *store,
    int64_t user_id,
    ClinicTicket *ticket,
    ClinicQueueSummary *summary)
{
    ClinicStoreStatus status;

    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (summary != NULL)
    {
        memset(summary, 0, sizeof(*summary));
    }
    if (ticket == NULL || summary == NULL || store == NULL || user_id <= 0)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    if (store->operations == NULL ||
        store->operations->get_current_ticket == NULL ||
        store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    status = store->operations->get_current_ticket(
        store->context,
        user_id,
        ticket,
        summary);
    if (status != CLINIC_STORE_OK)
    {
        memset(ticket, 0, sizeof(*ticket));
        memset(summary, 0, sizeof(*summary));
    }
    return status;
}

/* 叫号失败同样清空 ticket，具体事务和排序仍由 SQLite 实现负责。 */
ClinicStoreStatus clinic_store_call_next(
    ClinicStore *store,
    int64_t department_id,
    ClinicTicket *ticket)
{
    ClinicStoreStatus status;

    if (ticket != NULL)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    if (ticket == NULL || store == NULL || department_id <= 0)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    if (store->operations == NULL ||
        store->operations->call_next == NULL || store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    status = store->operations->call_next(
        store->context,
        department_id,
        ticket);
    if (status != CLINIC_STORE_OK)
    {
        memset(ticket, 0, sizeof(*ticket));
    }
    return status;
}

/* 管理台分页读取用户；只返回 id/username，不经过该接口暴露密码。 */
ClinicStoreStatus clinic_store_list_users(
    ClinicStore *store,
    int64_t after_id,
    ClinicUserSummary *users,
    size_t capacity,
    size_t *count,
    int *has_more)
{
    ClinicStoreStatus status;

    if (count != NULL)
    {
        *count = 0U;
    }
    if (has_more != NULL)
    {
        *has_more = 0;
    }
    if (store == NULL || after_id < 0 || users == NULL || count == NULL ||
        has_more == NULL || capacity == 0U ||
        capacity > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    memset(users, 0, capacity * sizeof(users[0]));
    if (store->operations == NULL || store->operations->list_users == NULL ||
        store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    status = store->operations->list_users(
        store->context,
        after_id,
        users,
        capacity,
        count,
        has_more);
    if (status != CLINIC_STORE_OK)
    {
        memset(users, 0, capacity * sizeof(users[0]));
        *count = 0U;
        *has_more = 0;
    }
    return status;
}

/* 管理台分页读取号单及 JOIN 后的用户名/科室名。 */
ClinicStoreStatus clinic_store_list_tickets(
    ClinicStore *store,
    int64_t after_id,
    ClinicAdminTicketRecord *tickets,
    size_t capacity,
    size_t *count,
    int *has_more)
{
    ClinicStoreStatus status;

    if (count != NULL)
    {
        *count = 0U;
    }
    if (has_more != NULL)
    {
        *has_more = 0;
    }
    if (store == NULL || after_id < 0 || tickets == NULL || count == NULL ||
        has_more == NULL || capacity == 0U ||
        capacity > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        return CLINIC_STORE_INVALID_ARGUMENT;
    }
    memset(tickets, 0, capacity * sizeof(tickets[0]));
    if (store->operations == NULL || store->operations->list_tickets == NULL ||
        store->context == NULL)
    {
        return CLINIC_STORE_DATABASE_ERROR;
    }
    status = store->operations->list_tickets(
        store->context,
        after_id,
        tickets,
        capacity,
        count,
        has_more);
    if (status != CLINIC_STORE_OK)
    {
        memset(tickets, 0, capacity * sizeof(tickets[0]));
        *count = 0U;
        *has_more = 0;
    }
    return status;
}
