/*
 * 文件作用：定义 Core 可见的统一数据访问接口。
 * ClinicStoreOperations 是函数表，ClinicStore 同时保存函数表和实现上下文；SQLite 打开后
 * 填入这些函数指针，Core 始终调用 clinic_store_*，不直接依赖 sqlite3。
 * StoreStatus 用于区分重复、未找到、无等待号单、数据库错误等数据层结果。
 */
#ifndef CLINIC_STORE_H
#define CLINIC_STORE_H

#include "clinic_types.h"

#include <stddef.h>
#include <stdint.h>

/*
 * StoreStatus 是数据层语言，不直接等同 JSON 错误码；Core 会把这些状态翻译成业务响应。
 * 正值通常表示可预期业务结果，负值表示参数、容量或数据库类失败。
 */
typedef enum ClinicStoreStatus
{
    CLINIC_STORE_OK = 0,
    CLINIC_STORE_NOT_FOUND = 1,
    CLINIC_STORE_DUPLICATE = 2,
    CLINIC_STORE_ACTIVE_TICKET_EXISTS = 3,
    CLINIC_STORE_DATABASE_ERROR = -1,
    CLINIC_STORE_INVALID_ARGUMENT = -2,
    CLINIC_STORE_CAPACITY_EXCEEDED = -3,
    CLINIC_STORE_USER_NOT_FOUND = -4,
    CLINIC_STORE_DEPARTMENT_NOT_FOUND = -5,
    CLINIC_STORE_TICKET_NOT_FOUND = -6,
    CLINIC_STORE_NO_WAITING_TICKET = -7,
    CLINIC_STORE_CURRENT_TICKET_NOT_FOUND = -8
} ClinicStoreStatus;

typedef struct ClinicStoredUser
{
    int64_t id;
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
    char password[CLINIC_PASSWORD_MAX_LENGTH + 1U];
} ClinicStoredUser;

typedef struct ClinicStore ClinicStore;

/*
 * 数据访问函数表：每个函数第一个参数都是具体实现 context。
 * SQLite 实现提供这些函数，也可在 Core 单元测试中换成内存假 Store。
 */
typedef struct ClinicStoreOperations
{
    ClinicStoreStatus (*create_user)(
        void *context,
        const char *username,
        const char *password,
        int64_t *user_id);
    ClinicStoreStatus (*find_user_by_username)(
        void *context,
        const char *username,
        ClinicStoredUser *user);
    ClinicStoreStatus (*list_departments)(
        void *context,
        ClinicDepartment *departments,
        size_t capacity,
        size_t *count);
    ClinicStoreStatus (*list_doctors)(
        void *context,
        int64_t department_id,
        ClinicDoctor *doctors,
        size_t capacity,
        size_t *count);
    ClinicStoreStatus (*close)(void *context);
    ClinicStoreStatus (*create_ticket)(
        void *context,
        int64_t user_id,
        int64_t department_id,
        ClinicTicket *ticket);
    ClinicStoreStatus (*get_ticket)(
        void *context,
        int64_t ticket_id,
        ClinicTicket *ticket);
    ClinicStoreStatus (*get_current_ticket)(
        void *context,
        int64_t user_id,
        ClinicTicket *ticket,
        ClinicQueueSummary *summary);
    ClinicStoreStatus (*call_next)(
        void *context,
        int64_t department_id,
        ClinicTicket *ticket);
    ClinicStoreStatus (*list_users)(
        void *context,
        int64_t after_id,
        ClinicUserSummary *users,
        size_t capacity,
        size_t *count,
        int *has_more);
    ClinicStoreStatus (*list_tickets)(
        void *context,
        int64_t after_id,
        ClinicAdminTicketRecord *tickets,
        size_t capacity,
        size_t *count,
        int *has_more);
} ClinicStoreOperations;

/* operations 决定“调用哪个实现”，context 保存“该实现的私有状态”。 */
struct ClinicStore
{
    const ClinicStoreOperations *operations;
    void *context;
};

void clinic_store_init(ClinicStore *store);

ClinicStoreStatus clinic_store_create_user(
    ClinicStore *store,
    const char *username,
    const char *password,
    int64_t *user_id);

ClinicStoreStatus clinic_store_find_user_by_username(
    ClinicStore *store,
    const char *username,
    ClinicStoredUser *user);

ClinicStoreStatus clinic_store_list_departments(
    ClinicStore *store,
    ClinicDepartment *departments,
    size_t capacity,
    size_t *count);

ClinicStoreStatus clinic_store_list_doctors(
    ClinicStore *store,
    int64_t department_id,
    ClinicDoctor *doctors,
    size_t capacity,
    size_t *count);

ClinicStoreStatus clinic_store_close(ClinicStore *store);

ClinicStoreStatus clinic_store_create_ticket(
    ClinicStore *store,
    int64_t user_id,
    int64_t department_id,
    ClinicTicket *ticket);

ClinicStoreStatus clinic_store_get_ticket(
    ClinicStore *store,
    int64_t ticket_id,
    ClinicTicket *ticket);

ClinicStoreStatus clinic_store_get_current_ticket(
    ClinicStore *store,
    int64_t user_id,
    ClinicTicket *ticket,
    ClinicQueueSummary *summary);

ClinicStoreStatus clinic_store_call_next(
    ClinicStore *store,
    int64_t department_id,
    ClinicTicket *ticket);

ClinicStoreStatus clinic_store_list_users(
    ClinicStore *store,
    int64_t after_id,
    ClinicUserSummary *users,
    size_t capacity,
    size_t *count,
    int *has_more);

ClinicStoreStatus clinic_store_list_tickets(
    ClinicStore *store,
    int64_t after_id,
    ClinicAdminTicketRecord *tickets,
    size_t capacity,
    size_t *count,
    int *has_more);

#endif
