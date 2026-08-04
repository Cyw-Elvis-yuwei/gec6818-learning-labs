/*
 * 文件作用（答辩）：服务器业务核心，统一入口是 clinic_core_handle()。
 * 它根据 ClinicRequest.type 分发注册、登录、科室、医生、取号、号单查询、叫号和管理台
 * 只读分页请求，做业务参数检查，调用 Store 接口，再把 Store 状态映射成统一响应。
 *
 * Core 管“能不能做、结果代表什么”，但不知道 TCP、JSON、cJSON 或具体 SQL；例如重复
 * 取号时，它把 Store 返回的 ACTIVE_TICKET_EXISTS 转成成功携带原号单的业务响应。
 *
 * 答辩阅读地图：每个 handle_* 对应一种业务；它们先校验业务参数，再调用 clinic_store_*
 * 接口，最后把 StoreStatus 翻译成稳定的业务响应。clinic_core_handle() 只负责统一初始化
 * response 和按 request->type 分发，不包含任何 socket、JSON 或 SQL 代码。
 */
#include "clinic_core.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void set_response_text(
    char *destination,
    size_t destination_size,
    const char *text)
{
    if (destination != NULL && destination_size > 0U)
    {
        (void)snprintf(destination, destination_size, "%s", text);
    }
}

/* 所有失败响应从同一处清空旧数据并设置错误码，避免失败时夹带上次成功结果。 */
static void set_error(
    ClinicResponse *response,
    const char *error_code,
    const char *message)
{
    response->ok = 0;
    response->kind = CLINIC_RESPONSE_NONE;
    response->user_id = 0;
    response->department_count = 0U;
    response->doctor_count = 0U;
    response->admin_user_count = 0U;
    response->admin_ticket_count = 0U;
    response->has_more = 0;
    memset(&response->ticket, 0, sizeof(response->ticket));
    set_response_text(
        response->error_code,
        sizeof(response->error_code),
        error_code);
    set_response_text(
        response->message,
        sizeof(response->message),
        message);
}

static int credential_is_valid(const char *value, size_t capacity)
{
    const char *terminator = memchr(value, '\0', capacity);

    return terminator != NULL && terminator != value;
}

static int request_credentials_are_valid(const ClinicRequest *request)
{
    return credential_is_valid(
               request->username,
               sizeof(request->username)) &&
        credential_is_valid(
               request->password,
               sizeof(request->password));
}

/* 注册：检查账号密码 -> Store 新增用户 -> 区分成功、用户名重复和数据库错误。 */
static void handle_register(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    int64_t user_id = 0;
    ClinicStoreStatus status;

    if (!request_credentials_are_valid(request))
    {
        set_error(response, "INVALID_ARGUMENT", "username and password are required");
        return;
    }

    status = clinic_store_create_user(
        core->store,
        request->username,
        request->password,
        &user_id);

    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_AUTH;
        response->user_id = user_id;
        set_response_text(
            response->message,
            sizeof(response->message),
            "registration successful");
        return;
    }
    if (status == CLINIC_STORE_DUPLICATE)
    {
        set_error(response, "USERNAME_EXISTS", "username already exists");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access user storage");
}

/*
 * 登录：按用户名查询用户，再比较密码并返回 user_id。
 * 当前明文密码只适合教学演示；真实系统应在服务器端保存带盐哈希而不是明文。
 */
static void handle_login(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoredUser user;
    ClinicStoreStatus status;

    if (!request_credentials_are_valid(request))
    {
        set_error(response, "INVALID_ARGUMENT", "username and password are required");
        return;
    }

    memset(&user, 0, sizeof(user));
    status = clinic_store_find_user_by_username(
        core->store,
        request->username,
        &user);
    if (status == CLINIC_STORE_NOT_FOUND)
    {
        set_error(response, "USER_NOT_FOUND", "username or password is incorrect");
        return;
    }
    if (status != CLINIC_STORE_OK)
    {
        set_error(response, "DATABASE_ERROR", "could not access user storage");
        return;
    }

    /* Plaintext comparison: teaching demo only; never suitable for production. */
    if (strcmp(user.password, request->password) != 0)
    {
        set_error(response, "INVALID_PASSWORD", "username or password is incorrect");
        return;
    }

    response->ok = 1;
    response->kind = CLINIC_RESPONSE_AUTH;
    response->user_id = user.id;
    set_response_text(
        response->message,
        sizeof(response->message),
        "login successful");
}

/* 科室查询没有筛选参数，Store 返回数组，Core 负责检查容量类错误并设置响应种类。 */
static void handle_list_departments(
    ClinicCore *core,
    ClinicResponse *response)
{
    size_t department_count = 0U;
    ClinicStoreStatus status;

    status = clinic_store_list_departments(
        core->store,
        response->departments,
        CLINIC_MAX_DEPARTMENTS,
        &department_count);
    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_DEPARTMENTS;
        response->department_count = department_count;
        set_response_text(
            response->message,
            sizeof(response->message),
            "departments retrieved");
        return;
    }
    if (status == CLINIC_STORE_CAPACITY_EXCEEDED ||
        status == CLINIC_STORE_INVALID_ARGUMENT)
    {
        set_error(response, "INTERNAL_ERROR", "could not prepare department response");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access department storage");
}

/* 医生查询先要求 department_id 为正数，再让 Store 按科室过滤医生。 */
static void handle_list_doctors(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    size_t doctor_count = 0U;
    ClinicStoreStatus status;

    if (request->department_id <= 0)
    {
        set_error(response, "INVALID_ARGUMENT", "department_id must be positive");
        return;
    }

    status = clinic_store_list_doctors(
        core->store,
        request->department_id,
        response->doctors,
        CLINIC_MAX_DOCTORS,
        &doctor_count);
    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_DOCTORS;
        response->doctor_count = doctor_count;
        set_response_text(
            response->message,
            sizeof(response->message),
            "doctors retrieved");
        return;
    }
    if (status == CLINIC_STORE_CAPACITY_EXCEEDED ||
        status == CLINIC_STORE_INVALID_ARGUMENT)
    {
        set_error(response, "INTERNAL_ERROR", "could not prepare doctor response");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access doctor storage");
}

/*
 * 按科室取号，而不是预约指定医生。Store 若发现当天同用户同科室已有有效号单，
 * 返回 ACTIVE_TICKET_EXISTS 和原号单；Core 仍返回 ok=true，但 message 说明复用旧号单。
 */
static void handle_create_ticket(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoreStatus status;

    if (request->user_id <= 0 || request->department_id <= 0)
    {
        set_error(response, "INVALID_ARGUMENT", "user_id and department_id must be positive");
        return;
    }
    status = clinic_store_create_ticket(
        core->store,
        request->user_id,
        request->department_id,
        &response->ticket);
    if (status == CLINIC_STORE_OK ||
        status == CLINIC_STORE_ACTIVE_TICKET_EXISTS)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_TICKET;
        set_response_text(
            response->message,
            sizeof(response->message),
            status == CLINIC_STORE_OK
                ? "ticket created"
                : "active ticket retrieved");
        return;
    }
    if (status == CLINIC_STORE_INVALID_ARGUMENT)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid ticket request");
        return;
    }
    if (status == CLINIC_STORE_USER_NOT_FOUND)
    {
        set_error(response, "USER_NOT_FOUND", "user does not exist");
        return;
    }
    if (status == CLINIC_STORE_DEPARTMENT_NOT_FOUND)
    {
        set_error(response, "DEPARTMENT_NOT_FOUND", "department does not exist");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access ticket storage");
}

/* 管理端叫号：Core 只校验科室并解释结果；“下一张如何排序”由 SQLite Store 决定。 */
static void handle_call_next(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoreStatus status;

    if (request->department_id <= 0)
    {
        set_error(response, "INVALID_ARGUMENT", "department_id must be positive");
        return;
    }
    status = clinic_store_call_next(
        core->store,
        request->department_id,
        &response->ticket);
    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_TICKET;
        set_response_text(
            response->message,
            sizeof(response->message),
            "next ticket called");
        return;
    }
    if (status == CLINIC_STORE_DEPARTMENT_NOT_FOUND)
    {
        set_error(response, "DEPARTMENT_NOT_FOUND", "department does not exist");
        return;
    }
    if (status == CLINIC_STORE_NO_WAITING_TICKET)
    {
        set_error(response, "NO_WAITING_TICKET", "no waiting ticket");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access ticket storage");
}

/* 依赖注入：把已打开的 Store 交给 Core，之后 Core 只依赖抽象接口。 */
int clinic_core_init(ClinicCore *core, ClinicStore *store)
{
    if (core == NULL || store == NULL)
    {
        return -1;
    }

    core->store = store;
    return 0;
}

static int handle_get_ticket_request(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoreStatus store_status;

    memset(&response->ticket, 0, sizeof(response->ticket));
    response->kind = CLINIC_RESPONSE_NONE;
    if (request->ticket_id <= 0)
    {
        response->ok = 0;
        strcpy(response->error_code, "INVALID_ARGUMENT");
        strcpy(response->message, "ticket_id must be positive");
        return 0;
    }

    store_status = clinic_store_get_ticket(
        core->store,
        request->ticket_id,
        &response->ticket);
    if (store_status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_TICKET;
        strcpy(response->message, "ticket retrieved");
        return 0;
    }

    response->ok = 0;
    response->kind = CLINIC_RESPONSE_NONE;
    memset(&response->ticket, 0, sizeof(response->ticket));
    if (store_status == CLINIC_STORE_TICKET_NOT_FOUND)
    {
        strcpy(response->error_code, "TICKET_NOT_FOUND");
        strcpy(response->message, "ticket not found");
    }
    else
    {
        strcpy(response->error_code, "DATABASE_ERROR");
        strcpy(response->message, "database operation failed");
    }
    return 0;
}

/*
 * 查询当前号单时一次取得 Ticket 和 QueueSummary，避免板端自己推算当前叫号和前方人数。
 * 只有 Store 两部分都成功时 queue_summary_valid 才置 1。
 */
static void handle_get_current_ticket(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoreStatus status;

    if (request->user_id <= 0)
    {
        set_error(response, "INVALID_ARGUMENT", "user_id must be positive");
        return;
    }

    status = clinic_store_get_current_ticket(
        core->store,
        request->user_id,
        &response->ticket,
        &response->queue_summary);
    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_TICKET;
        response->queue_summary_valid = 1;
        set_response_text(
            response->message,
            sizeof(response->message),
            "current ticket retrieved");
        return;
    }
    if (status == CLINIC_STORE_CURRENT_TICKET_NOT_FOUND)
    {
        set_error(
            response,
            "CURRENT_TICKET_NOT_FOUND",
            "current ticket not found");
        return;
    }
    if (status == CLINIC_STORE_INVALID_ARGUMENT)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid current ticket request");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access ticket storage");
}

/* 管理台只读分页查询用户；Store 输出中不包含密码。 */
static void handle_admin_list_users(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoreStatus status;

    if (request->after_id < 0 || request->limit == 0U ||
        request->limit > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid admin user page");
        return;
    }
    status = clinic_store_list_users(
        core->store,
        request->after_id,
        response->admin_users,
        request->limit,
        &response->admin_user_count,
        &response->has_more);
    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_ADMIN_USERS;
        set_response_text(
            response->message,
            sizeof(response->message),
            "admin users retrieved");
        return;
    }
    if (status == CLINIC_STORE_INVALID_ARGUMENT)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid admin user page");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access user storage");
}

/* 管理台只读分页查询号单及关联的用户名、科室名。 */
static void handle_admin_list_tickets(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    ClinicStoreStatus status;

    if (request->after_id < 0 || request->limit == 0U ||
        request->limit > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid admin ticket page");
        return;
    }
    status = clinic_store_list_tickets(
        core->store,
        request->after_id,
        response->admin_tickets,
        request->limit,
        &response->admin_ticket_count,
        &response->has_more);
    if (status == CLINIC_STORE_OK)
    {
        response->ok = 1;
        response->kind = CLINIC_RESPONSE_ADMIN_TICKETS;
        set_response_text(
            response->message,
            sizeof(response->message),
            "admin tickets retrieved");
        return;
    }
    if (status == CLINIC_STORE_INVALID_ARGUMENT)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid admin ticket page");
        return;
    }
    set_error(response, "DATABASE_ERROR", "could not access ticket storage");
}

/*
 * Core 的唯一公开业务入口。先把 response 全部清零并复制 request_id，确保每次请求独立；
 * 再按枚举分发到对应 handler。未知 type 返回业务错误，不让服务器进程崩溃。
 */
int clinic_core_handle(
    ClinicCore *core,
    const ClinicRequest *request,
    ClinicResponse *response)
{
    if (response == NULL)
    {
        return -1;
    }

    memset(response, 0, sizeof(*response));
    if (request != NULL)
    {
        response->request_id = request->request_id;
    }

    if (core == NULL || core->store == NULL || request == NULL)
    {
        set_error(response, "INVALID_ARGUMENT", "invalid core request");
        return -1;
    }

    if (request->type == CLINIC_REQ_REGISTER)
    {
        handle_register(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_LOGIN)
    {
        handle_login(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_LIST_DEPARTMENTS)
    {
        handle_list_departments(core, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_LIST_DOCTORS)
    {
        handle_list_doctors(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_GET_TICKET)
    {
        return handle_get_ticket_request(core, request, response);
    }
    if (request->type == CLINIC_REQ_GET_CURRENT_TICKET)
    {
        handle_get_current_ticket(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_CREATE_TICKET)
    {
        handle_create_ticket(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_CALL_NEXT)
    {
        handle_call_next(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_ADMIN_LIST_USERS)
    {
        handle_admin_list_users(core, request, response);
        return 0;
    }
    if (request->type == CLINIC_REQ_ADMIN_LIST_TICKETS)
    {
        handle_admin_list_tickets(core, request, response);
        return 0;
    }

    set_error(response, "UNKNOWN_REQUEST", "unknown request type");
    return 0;
}
