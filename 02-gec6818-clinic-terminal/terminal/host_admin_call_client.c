/*
 * 文件作用（答辩）：Ubuntu 管理员叫号客户端，不属于普通用户板端权限。
 * 无参数运行时连接本机服务器，先获取真实科室列表并显示数字菜单；选择科室后发送
 * call_next，服务器把当天最早 WAITING 号单更新为 CALLED。支持 d 查数据、r 刷新、q 退出。
 *
 * 原四参数模式保留给自动化测试和排障。客户端只发送管理请求并展示结果，真正的叫号
 * 顺序、事务和 called_time 都由服务器 Core/Store/SQLite 决定。
 *
 * 交互模式流程：load_departments 从服务器取真实科室 -> print_department_menu 显示菜单
 * -> parse_menu_action 解析用户输入 -> perform_interactive_call 发送 call_next ->
 * print_call_result 展示叫到的号单。每次请求仍通过 TCP/JSON，不会直接打开数据库。
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clinic_net.h"
#include "clinic_protocol.h"
#include "clinic_types.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ADMIN_CALL_TIMEOUT_SECONDS 5
#define ADMIN_DEFAULT_HOST "127.0.0.1"
#define ADMIN_DEFAULT_PORT "9000"
#define ADMIN_INPUT_CAPACITY 64U
#define ADMIN_REQUEST_ID_MAX UINT64_C(9007199254740991)
#define ADMIN_DATA_PAGE_LIMIT CLINIC_ADMIN_PAGE_MAX_ITEMS

typedef enum AdminMenuAction
{
    ADMIN_MENU_INVALID = 0,
    ADMIN_MENU_CALL,
    ADMIN_MENU_DATA,
    ADMIN_MENU_REFRESH,
    ADMIN_MENU_QUIT
} AdminMenuAction;

static int parse_uint64_text(const char *text, uint64_t *value)
{
    const char *cursor;
    uint64_t parsed = 0U;

    if (text == NULL || value == NULL || text[0] == '\0')
    {
        return -1;
    }
    for (cursor = text; *cursor != '\0'; ++cursor)
    {
        uint64_t digit;

        if (*cursor < '0' || *cursor > '9')
        {
            return -1;
        }
        digit = (uint64_t)(unsigned char)(*cursor - '0');
        if (parsed > (UINT64_MAX - digit) / UINT64_C(10))
        {
            return -1;
        }
        parsed = parsed * UINT64_C(10) + digit;
    }
    *value = parsed;
    return 0;
}

static int parse_positive_int64_text(const char *text, int64_t *value)
{
    uint64_t parsed;

    if (value == NULL || parse_uint64_text(text, &parsed) != 0 ||
        parsed == 0U || parsed > (uint64_t)INT64_MAX)
    {
        return -1;
    }
    *value = (int64_t)parsed;
    return 0;
}

static int port_is_valid(const char *text)
{
    uint64_t port;

    return parse_uint64_text(text, &port) == 0 &&
        port > 0U && port <= UINT64_C(65535);
}

static char *create_request(
    uint64_t request_id,
    int64_t department_id,
    size_t *request_length)
{
    char request_id_text[32];
    char department_id_text[32];
    cJSON *root = NULL;
    char *json = NULL;
    char *request = NULL;
    size_t json_length;
    int written;

    if (request_length == NULL)
    {
        return NULL;
    }
    *request_length = 0U;
    written = snprintf(
        request_id_text,
        sizeof(request_id_text),
        "%" PRIu64,
        request_id);
    if (written < 0 || (size_t)written >= sizeof(request_id_text))
    {
        return NULL;
    }
    written = snprintf(
        department_id_text,
        sizeof(department_id_text),
        "%" PRId64,
        department_id);
    if (written < 0 || (size_t)written >= sizeof(department_id_text))
    {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", "call_next") == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddRawToObject(
            root,
            "department_id",
            department_id_text) == NULL)
    {
        cJSON_Delete(root);
        return NULL;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return NULL;
    }
    json_length = strlen(json);
    if (json_length <= CLINIC_MAX_FRAME_SIZE)
    {
        request = malloc(json_length + 2U);
    }
    if (request != NULL)
    {
        memcpy(request, json, json_length);
        request[json_length] = '\n';
        request[json_length + 1U] = '\0';
        *request_length = json_length + 1U;
    }
    cJSON_free(json);
    return request;
}

static char *create_department_request(
    uint64_t request_id,
    size_t *request_length)
{
    char request_id_text[32];
    cJSON *root = NULL;
    char *json = NULL;
    char *request = NULL;
    size_t json_length;
    int written;

    if (request_length == NULL)
    {
        return NULL;
    }
    *request_length = 0U;
    written = snprintf(
        request_id_text,
        sizeof(request_id_text),
        "%" PRIu64,
        request_id);
    if (written < 0 || (size_t)written >= sizeof(request_id_text))
    {
        return NULL;
    }
    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", "list_departments") == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL)
    {
        cJSON_Delete(root);
        return NULL;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return NULL;
    }
    json_length = strlen(json);
    if (json_length <= CLINIC_MAX_FRAME_SIZE)
    {
        request = malloc(json_length + 2U);
    }
    if (request != NULL)
    {
        memcpy(request, json, json_length);
        request[json_length] = '\n';
        request[json_length + 1U] = '\0';
        *request_length = json_length + 1U;
    }
    cJSON_free(json);
    return request;
}

static char *create_admin_page_request(
    const char *type,
    uint64_t request_id,
    int64_t after_id,
    size_t limit,
    size_t *request_length)
{
    char request_id_text[32];
    char after_id_text[32];
    char limit_text[32];
    cJSON *root = NULL;
    char *json = NULL;
    char *request = NULL;
    size_t json_length;
    int written;

    if (type == NULL || request_length == NULL || after_id < 0 ||
        limit == 0U || limit > CLINIC_ADMIN_PAGE_MAX_ITEMS)
    {
        return NULL;
    }
    *request_length = 0U;
    written = snprintf(
        request_id_text,
        sizeof(request_id_text),
        "%" PRIu64,
        request_id);
    if (written < 0 || (size_t)written >= sizeof(request_id_text))
    {
        return NULL;
    }
    written = snprintf(
        after_id_text,
        sizeof(after_id_text),
        "%" PRId64,
        after_id);
    if (written < 0 || (size_t)written >= sizeof(after_id_text))
    {
        return NULL;
    }
    written = snprintf(limit_text, sizeof(limit_text), "%zu", limit);
    if (written < 0 || (size_t)written >= sizeof(limit_text))
    {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", type) == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddRawToObject(root, "after_id", after_id_text) == NULL ||
        cJSON_AddRawToObject(root, "limit", limit_text) == NULL)
    {
        cJSON_Delete(root);
        return NULL;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return NULL;
    }
    json_length = strlen(json);
    if (json_length <= CLINIC_MAX_FRAME_SIZE)
    {
        request = malloc(json_length + 2U);
    }
    if (request != NULL)
    {
        memcpy(request, json, json_length);
        request[json_length] = '\n';
        request[json_length + 1U] = '\0';
        *request_length = json_length + 1U;
    }
    cJSON_free(json);
    return request;
}

static char *create_doctor_request(
    uint64_t request_id,
    int64_t department_id,
    size_t *request_length)
{
    char request_id_text[32];
    char department_id_text[32];
    cJSON *root = NULL;
    char *json = NULL;
    char *request = NULL;
    size_t json_length;
    int written;

    if (request_length == NULL || department_id <= 0)
    {
        return NULL;
    }
    *request_length = 0U;
    written = snprintf(
        request_id_text,
        sizeof(request_id_text),
        "%" PRIu64,
        request_id);
    if (written < 0 || (size_t)written >= sizeof(request_id_text))
    {
        return NULL;
    }
    written = snprintf(
        department_id_text,
        sizeof(department_id_text),
        "%" PRId64,
        department_id);
    if (written < 0 || (size_t)written >= sizeof(department_id_text))
    {
        return NULL;
    }
    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", "list_doctors") == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddRawToObject(
            root,
            "department_id",
            department_id_text) == NULL)
    {
        cJSON_Delete(root);
        return NULL;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return NULL;
    }
    json_length = strlen(json);
    if (json_length <= CLINIC_MAX_FRAME_SIZE)
    {
        request = malloc(json_length + 2U);
    }
    if (request != NULL)
    {
        memcpy(request, json, json_length);
        request[json_length] = '\n';
        request[json_length + 1U] = '\0';
        *request_length = json_length + 1U;
    }
    cJSON_free(json);
    return request;
}

static int receive_response(
    clinic_socket_t socket_fd,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    size_t received_total = 0U;
    struct timespec deadline;

    if (response == NULL || response_length == NULL ||
        response_capacity < CLINIC_MAX_FRAME_SIZE + 1U)
    {
        return -1;
    }
    *response_length = 0U;
    response[0] = '\0';
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
    {
        return -1;
    }
    deadline.tv_sec += ADMIN_CALL_TIMEOUT_SECONDS;

    while (received_total < CLINIC_MAX_FRAME_SIZE)
    {
        struct pollfd descriptor;
        int poll_status;

        descriptor.fd = socket_fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        for (;;)
        {
            struct timespec now;
            int64_t remaining_seconds;
            long remaining_nanoseconds;
            int64_t timeout_milliseconds;

            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            {
                response[0] = '\0';
                return -1;
            }
            remaining_seconds =
                (int64_t)deadline.tv_sec - (int64_t)now.tv_sec;
            remaining_nanoseconds = deadline.tv_nsec - now.tv_nsec;
            if (remaining_nanoseconds < 0L)
            {
                --remaining_seconds;
                remaining_nanoseconds += 1000000000L;
            }
            if (remaining_seconds < 0)
            {
                response[0] = '\0';
                return -1;
            }
            timeout_milliseconds =
                remaining_seconds * INT64_C(1000) +
                (remaining_nanoseconds + 999999L) / 1000000L;
            if (timeout_milliseconds <= 0)
            {
                response[0] = '\0';
                return -1;
            }
            if (timeout_milliseconds > INT_MAX)
            {
                timeout_milliseconds = INT_MAX;
            }
            poll_status = poll(&descriptor, 1U, (int)timeout_milliseconds);
            if (poll_status >= 0 || errno != EINTR)
            {
                break;
            }
        }
        if (poll_status <= 0 ||
            (descriptor.revents & (POLLIN | POLLHUP)) == 0)
        {
            response[0] = '\0';
            return -1;
        }

        {
            int received = recv(
                socket_fd,
                response + received_total,
                (int)(CLINIC_MAX_FRAME_SIZE - received_total),
                0);
            char *newline;

            if (received <= 0)
            {
                response[0] = '\0';
                return -1;
            }
            received_total += (size_t)received;
            response[received_total] = '\0';
            newline = memchr(response, '\n', received_total);
            if (newline != NULL)
            {
                *response_length = (size_t)(newline - response) + 1U;
                response[*response_length] = '\0';
                return 0;
            }
        }
    }
    response[0] = '\0';
    return -1;
}

/* 管理端一次同步 TCP 交换：连接、完整发送、poll 等待、按换行接收一帧并关闭连接。 */
static int exchange_request(
    const char *host,
    const char *port,
    const char *request,
    size_t request_length,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
    int status = -1;

    if (host == NULL || port == NULL || request == NULL ||
        response == NULL || response_length == NULL)
    {
        return -1;
    }
    if (clinic_net_connect(host, port, &socket_fd) == 0 &&
        clinic_net_send_all(socket_fd, request, request_length) == 0 &&
        receive_response(
            socket_fd,
            response,
            response_capacity,
            response_length) == 0)
    {
        status = 0;
    }
    if (socket_fd != CLINIC_SOCKET_INVALID)
    {
        clinic_socket_close(socket_fd);
    }
    return status;
}

static cJSON *get_unique_object_item(const cJSON *object, const char *name)
{
    cJSON *child;
    cJSON *match = NULL;
    unsigned int matches = 0U;

    if (!cJSON_IsObject(object) || name == NULL)
    {
        return NULL;
    }
    for (child = object->child; child != NULL; child = child->next)
    {
        if (child->string != NULL && strcmp(child->string, name) == 0)
        {
            match = child;
            ++matches;
        }
    }
    return matches == 1U ? match : NULL;
}

static size_t object_member_count(const cJSON *object)
{
    const cJSON *child;
    size_t count = 0U;

    if (!cJSON_IsObject(object))
    {
        return 0U;
    }
    for (child = object->child; child != NULL; child = child->next)
    {
        ++count;
    }
    return count;
}

static int is_positive_int64_number(const cJSON *item)
{
    int64_t parsed;

    if (!cJSON_IsNumber(item) || item->valuedouble <= 0.0 ||
        item->valuedouble >= 9223372036854775808.0)
    {
        return 0;
    }
    parsed = (int64_t)item->valuedouble;
    return (double)parsed == item->valuedouble;
}

static int called_ticket_is_valid(const cJSON *ticket)
{
    cJSON *id;
    cJSON *user_id;
    cJSON *department_id;
    cJSON *queue_number;
    cJSON *status;
    cJSON *service_date;
    cJSON *created_time;
    cJSON *called_time;

    if (!cJSON_IsObject(ticket))
    {
        return 0;
    }
    id = get_unique_object_item(ticket, "id");
    user_id = get_unique_object_item(ticket, "user_id");
    department_id = get_unique_object_item(ticket, "department_id");
    queue_number = get_unique_object_item(ticket, "queue_number");
    status = get_unique_object_item(ticket, "status");
    service_date = get_unique_object_item(ticket, "service_date");
    created_time = get_unique_object_item(ticket, "created_time");
    called_time = get_unique_object_item(ticket, "called_time");

    return is_positive_int64_number(id) &&
        is_positive_int64_number(user_id) &&
        is_positive_int64_number(department_id) &&
        is_positive_int64_number(queue_number) &&
        cJSON_IsString(status) && status->valuestring != NULL &&
        strcmp(status->valuestring, "CALLED") == 0 &&
        cJSON_IsString(service_date) && service_date->valuestring != NULL &&
        service_date->valuestring[0] != '\0' &&
        is_positive_int64_number(created_time) &&
        is_positive_int64_number(called_time);
}

/* 严格验证 call_next 响应及 ticket，request_id 必须与刚才发出的请求一致。 */
static int validate_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int *business_ok)
{
    const char *parse_end = NULL;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *ticket;

    if (response == NULL || business_ok == NULL || response_length == 0U ||
        response[response_length - 1U] != '\n' ||
        memchr(response, '\n', response_length - 1U) != NULL)
    {
        return -1;
    }
    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    if (!cJSON_IsObject(root) || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return -1;
    }
    ok = get_unique_object_item(root, "ok");
    request_id = get_unique_object_item(root, "request_id");
    ticket = get_unique_object_item(root, "ticket");
    if ((!cJSON_IsTrue(ok) && !cJSON_IsFalse(ok)) ||
        !cJSON_IsNumber(request_id) ||
        request_id->valuedouble != (double)expected_request_id ||
        cJSON_GetObjectItemCaseSensitive(root, "password") != NULL)
    {
        cJSON_Delete(root);
        return -1;
    }

    *business_ok = cJSON_IsTrue(ok);
    if (*business_ok)
    {
        if (!called_ticket_is_valid(ticket) ||
            cJSON_GetObjectItemCaseSensitive(root, "departments") != NULL ||
            cJSON_GetObjectItemCaseSensitive(root, "doctors") != NULL)
        {
            cJSON_Delete(root);
            return -1;
        }
    }
    else if (ticket != NULL ||
             !cJSON_IsString(get_unique_object_item(root, "error_code")))
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_Delete(root);
    return 0;
}

static int validate_department_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    ClinicDepartment *departments,
    size_t capacity,
    size_t *department_count)
{
    const char *parse_end = NULL;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *items;
    cJSON *message;
    cJSON *item;
    size_t count = 0U;

    if (response == NULL || departments == NULL || department_count == NULL ||
        capacity == 0U || response_length == 0U ||
        response[response_length - 1U] != '\n' ||
        memchr(response, '\n', response_length - 1U) != NULL)
    {
        return -1;
    }
    *department_count = 0U;
    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    if (!cJSON_IsObject(root) || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return -1;
    }
    ok = get_unique_object_item(root, "ok");
    request_id = get_unique_object_item(root, "request_id");
    items = get_unique_object_item(root, "departments");
    message = get_unique_object_item(root, "message");
    if (!cJSON_IsTrue(ok) || !cJSON_IsNumber(request_id) ||
        request_id->valuedouble != (double)expected_request_id ||
        !cJSON_IsArray(items) || !cJSON_IsString(message) ||
        object_member_count(root) != 4U ||
        (size_t)cJSON_GetArraySize(items) > capacity)
    {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_ArrayForEach(item, items)
    {
        cJSON *id = get_unique_object_item(item, "id");
        cJSON *name = get_unique_object_item(item, "name");
        size_t name_length;
        size_t existing_index;

        if (!cJSON_IsObject(item) || object_member_count(item) != 2U ||
            !is_positive_int64_number(id) || !cJSON_IsString(name) ||
            name->valuestring == NULL)
        {
            cJSON_Delete(root);
            return -1;
        }
        name_length = strlen(name->valuestring);
        if (name_length == 0U ||
            name_length > CLINIC_DEPARTMENT_NAME_MAX_LENGTH)
        {
            cJSON_Delete(root);
            return -1;
        }
        for (existing_index = 0U;
             existing_index < count;
             ++existing_index)
        {
            if (departments[existing_index].id == (int64_t)id->valuedouble)
            {
                cJSON_Delete(root);
                return -1;
            }
        }
        departments[count].id = (int64_t)id->valuedouble;
        memcpy(departments[count].name, name->valuestring, name_length + 1U);
        ++count;
    }
    cJSON_Delete(root);
    if (count == 0U)
    {
        return -1;
    }
    *department_count = count;
    return 0;
}

static int parse_admin_page_envelope(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    const char *array_name,
    cJSON **root_out,
    cJSON **items_out,
    int *has_more)
{
    const char *parse_end = NULL;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *items;
    cJSON *more;
    cJSON *message;

    if (response == NULL || array_name == NULL || root_out == NULL ||
        items_out == NULL || has_more == NULL || response_length == 0U ||
        response[response_length - 1U] != '\n' ||
        memchr(response, '\n', response_length - 1U) != NULL)
    {
        return -1;
    }
    *root_out = NULL;
    *items_out = NULL;
    *has_more = 0;
    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    if (!cJSON_IsObject(root) || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return -1;
    }
    ok = get_unique_object_item(root, "ok");
    request_id = get_unique_object_item(root, "request_id");
    items = get_unique_object_item(root, array_name);
    more = get_unique_object_item(root, "has_more");
    message = get_unique_object_item(root, "message");
    if (!cJSON_IsTrue(ok) || !cJSON_IsNumber(request_id) ||
        request_id->valuedouble != (double)expected_request_id ||
        !cJSON_IsArray(items) ||
        (!cJSON_IsTrue(more) && !cJSON_IsFalse(more)) ||
        !cJSON_IsString(message) || object_member_count(root) != 5U ||
        cJSON_GetArraySize(items) < 0 ||
        (size_t)cJSON_GetArraySize(items) > CLINIC_ADMIN_PAGE_MAX_ITEMS ||
        cJSON_GetObjectItemCaseSensitive(root, "password") != NULL)
    {
        cJSON_Delete(root);
        return -1;
    }
    *root_out = root;
    *items_out = items;
    *has_more = cJSON_IsTrue(more);
    return 0;
}

static int print_admin_user_page(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int64_t after_id,
    int64_t *last_id,
    int *has_more)
{
    cJSON *root = NULL;
    cJSON *items = NULL;
    cJSON *item;
    int64_t current_last_id = after_id;
    size_t count = 0U;

    if (last_id == NULL || has_more == NULL ||
        parse_admin_page_envelope(
            response,
            response_length,
            expected_request_id,
            "users",
            &root,
            &items,
            has_more) != 0)
    {
        return -1;
    }
    cJSON_ArrayForEach(item, items)
    {
        cJSON *id = get_unique_object_item(item, "id");
        cJSON *username = get_unique_object_item(item, "username");
        size_t username_length;
        int64_t parsed_id;

        if (!cJSON_IsObject(item) || object_member_count(item) != 2U ||
            !is_positive_int64_number(id) || !cJSON_IsString(username) ||
            username->valuestring == NULL)
        {
            cJSON_Delete(root);
            return -1;
        }
        username_length = strlen(username->valuestring);
        parsed_id = (int64_t)id->valuedouble;
        if (parsed_id <= current_last_id || username_length == 0U ||
            username_length > CLINIC_USERNAME_MAX_LENGTH ||
            cJSON_GetObjectItemCaseSensitive(item, "password") != NULL)
        {
            cJSON_Delete(root);
            return -1;
        }
        printf("%-8" PRId64 " %s\n", parsed_id, username->valuestring);
        current_last_id = parsed_id;
        ++count;
    }
    cJSON_Delete(root);
    if (*has_more && count == 0U)
    {
        return -1;
    }
    *last_id = current_last_id;
    return 0;
}

static int ticket_status_is_valid(const cJSON *status)
{
    if (!cJSON_IsString(status) || status->valuestring == NULL)
    {
        return 0;
    }
    return strcmp(status->valuestring, "WAITING") == 0 ||
        strcmp(status->valuestring, "CALLED") == 0 ||
        strcmp(status->valuestring, "COMPLETED") == 0 ||
        strcmp(status->valuestring, "CANCELLED") == 0;
}

static int print_admin_ticket_page(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int64_t after_id,
    int64_t *last_id,
    int *has_more)
{
    cJSON *root = NULL;
    cJSON *items = NULL;
    cJSON *item;
    int64_t current_last_id = after_id;
    size_t count = 0U;

    if (last_id == NULL || has_more == NULL ||
        parse_admin_page_envelope(
            response,
            response_length,
            expected_request_id,
            "tickets",
            &root,
            &items,
            has_more) != 0)
    {
        return -1;
    }
    cJSON_ArrayForEach(item, items)
    {
        cJSON *id = get_unique_object_item(item, "id");
        cJSON *user_id = get_unique_object_item(item, "user_id");
        cJSON *username = get_unique_object_item(item, "username");
        cJSON *department_id =
            get_unique_object_item(item, "department_id");
        cJSON *department = get_unique_object_item(item, "department");
        cJSON *queue_number = get_unique_object_item(item, "queue_number");
        cJSON *status = get_unique_object_item(item, "status");
        cJSON *service_date = get_unique_object_item(item, "service_date");
        cJSON *created_time = get_unique_object_item(item, "created_time");
        cJSON *called_time = get_unique_object_item(item, "called_time");
        int64_t parsed_id;
        const char *called_text = "-";
        char called_buffer[32];

        if (!cJSON_IsObject(item) || object_member_count(item) != 10U ||
            !is_positive_int64_number(id) ||
            !is_positive_int64_number(user_id) ||
            !is_positive_int64_number(department_id) ||
            !is_positive_int64_number(queue_number) ||
            !is_positive_int64_number(created_time) ||
            !cJSON_IsString(username) || username->valuestring == NULL ||
            username->valuestring[0] == '\0' ||
            strlen(username->valuestring) > CLINIC_USERNAME_MAX_LENGTH ||
            !cJSON_IsString(department) || department->valuestring == NULL ||
            department->valuestring[0] == '\0' ||
            strlen(department->valuestring) >
                CLINIC_DEPARTMENT_NAME_MAX_LENGTH ||
            !ticket_status_is_valid(status) ||
            !cJSON_IsString(service_date) || service_date->valuestring == NULL ||
            strlen(service_date->valuestring) != CLINIC_SERVICE_DATE_LENGTH ||
            (!cJSON_IsNull(called_time) &&
             !is_positive_int64_number(called_time)))
        {
            cJSON_Delete(root);
            return -1;
        }
        parsed_id = (int64_t)id->valuedouble;
        if (parsed_id <= current_last_id)
        {
            cJSON_Delete(root);
            return -1;
        }
        if (!cJSON_IsNull(called_time))
        {
            (void)snprintf(
                called_buffer,
                sizeof(called_buffer),
                "%" PRId64,
                (int64_t)called_time->valuedouble);
            called_text = called_buffer;
        }
        printf(
            "%-6" PRId64 " %-12s %-10s %-6" PRId64
            " %-10s %-10s %s\n",
            parsed_id,
            username->valuestring,
            department->valuestring,
            (int64_t)queue_number->valuedouble,
            status->valuestring,
            service_date->valuestring,
            called_text);
        current_last_id = parsed_id;
        ++count;
    }
    cJSON_Delete(root);
    if (*has_more && count == 0U)
    {
        return -1;
    }
    *last_id = current_last_id;
    return 0;
}

static int print_doctor_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int64_t expected_department_id)
{
    const char *parse_end = NULL;
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *items;
    cJSON *message;
    cJSON *item;

    if (response == NULL || response_length == 0U ||
        response[response_length - 1U] != '\n' ||
        memchr(response, '\n', response_length - 1U) != NULL)
    {
        return -1;
    }
    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    if (!cJSON_IsObject(root) || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return -1;
    }
    ok = get_unique_object_item(root, "ok");
    request_id = get_unique_object_item(root, "request_id");
    items = get_unique_object_item(root, "doctors");
    message = get_unique_object_item(root, "message");
    if (!cJSON_IsTrue(ok) || !cJSON_IsNumber(request_id) ||
        request_id->valuedouble != (double)expected_request_id ||
        !cJSON_IsArray(items) || !cJSON_IsString(message) ||
        object_member_count(root) != 4U ||
        cJSON_GetArraySize(items) < 0 ||
        (size_t)cJSON_GetArraySize(items) > CLINIC_MAX_DOCTORS)
    {
        cJSON_Delete(root);
        return -1;
    }
    cJSON_ArrayForEach(item, items)
    {
        cJSON *id = get_unique_object_item(item, "id");
        cJSON *department_id =
            get_unique_object_item(item, "department_id");
        cJSON *name = get_unique_object_item(item, "name");
        cJSON *title = get_unique_object_item(item, "title");
        cJSON *specialty = get_unique_object_item(item, "specialty");

        if (!cJSON_IsObject(item) || object_member_count(item) != 5U ||
            !is_positive_int64_number(id) ||
            !is_positive_int64_number(department_id) ||
            (int64_t)department_id->valuedouble != expected_department_id ||
            !cJSON_IsString(name) || name->valuestring == NULL ||
            !cJSON_IsString(title) || title->valuestring == NULL ||
            !cJSON_IsString(specialty) || specialty->valuestring == NULL ||
            name->valuestring[0] == '\0' ||
            strlen(name->valuestring) > CLINIC_DOCTOR_NAME_MAX_LENGTH ||
            title->valuestring[0] == '\0' ||
            strlen(title->valuestring) > CLINIC_DOCTOR_TITLE_MAX_LENGTH ||
            specialty->valuestring[0] == '\0' ||
            strlen(specialty->valuestring) >
                CLINIC_DOCTOR_SPECIALTY_MAX_LENGTH)
        {
            cJSON_Delete(root);
            return -1;
        }
        printf(
            "%-6" PRId64 " %-12s %-14s %s\n",
            (int64_t)id->valuedouble,
            name->valuestring,
            title->valuestring,
            specialty->valuestring);
    }
    cJSON_Delete(root);
    return 0;
}

static uint64_t create_interactive_request_id(void)
{
    struct timespec now;
    uint64_t request_id;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0 || now.tv_sec < 0)
    {
        return UINT64_C(1);
    }
    request_id =
        ((uint64_t)now.tv_sec % UINT64_C(9007199254740)) * UINT64_C(1000) +
        (uint64_t)(now.tv_nsec / 1000000L);
    return request_id == 0U ? UINT64_C(1) : request_id;
}

static uint64_t advance_request_id(uint64_t request_id)
{
    return request_id >= ADMIN_REQUEST_ID_MAX ? UINT64_C(1) : request_id + 1U;
}

/* 菜单数据来自服务器而不是写死，确保显示编号对应真实 department_id。 */
static int load_departments(
    const char *host,
    const char *port,
    uint64_t request_id,
    ClinicDepartment *departments,
    size_t capacity,
    size_t *department_count)
{
    ClinicDepartment parsed_departments[CLINIC_MAX_DEPARTMENTS];
    char *request;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t request_length = 0U;
    size_t response_length = 0U;
    size_t parsed_count = 0U;
    int status = -1;

    if (departments == NULL || department_count == NULL || capacity == 0U ||
        capacity > CLINIC_MAX_DEPARTMENTS)
    {
        return -1;
    }
    request = create_department_request(request_id, &request_length);
    if (request != NULL &&
        exchange_request(
            host,
            port,
            request,
            request_length,
            response,
            sizeof(response),
            &response_length) == 0 &&
        validate_department_response(
            response,
            response_length,
            request_id,
            parsed_departments,
            capacity,
            &parsed_count) == 0)
    {
        memcpy(
            departments,
            parsed_departments,
            parsed_count * sizeof(parsed_departments[0]));
        *department_count = parsed_count;
        status = 0;
    }
    free(request);
    return status;
}

static int show_admin_users(
    const char *host,
    const char *port,
    uint64_t *request_id)
{
    int64_t after_id = 0;
    int has_more = 0;

    puts("\n[用户 users]");
    puts("ID       用户名");
    do
    {
        char *request;
        char response[CLINIC_MAX_FRAME_SIZE + 1U];
        size_t request_length = 0U;
        size_t response_length = 0U;
        int64_t last_id = after_id;

        request = create_admin_page_request(
            "admin_list_users",
            *request_id,
            after_id,
            ADMIN_DATA_PAGE_LIMIT,
            &request_length);
        if (request == NULL ||
            exchange_request(
                host,
                port,
                request,
                request_length,
                response,
                sizeof(response),
                &response_length) != 0 ||
            print_admin_user_page(
                response,
                response_length,
                *request_id,
                after_id,
                &last_id,
                &has_more) != 0)
        {
            free(request);
            return -1;
        }
        free(request);
        *request_id = advance_request_id(*request_id);
        after_id = last_id;
    } while (has_more);
    return 0;
}

static int show_admin_doctors(
    const char *host,
    const char *port,
    uint64_t *request_id,
    const ClinicDepartment *departments,
    size_t department_count)
{
    size_t index;

    puts("\n[医生 doctors]");
    puts("ID     姓名         职称           擅长方向");
    for (index = 0U; index < department_count; ++index)
    {
        char *request;
        char response[CLINIC_MAX_FRAME_SIZE + 1U];
        size_t request_length = 0U;
        size_t response_length = 0U;

        printf(
            "-- %s（科室 ID：%" PRId64 "）--\n",
            departments[index].name,
            departments[index].id);
        request = create_doctor_request(
            *request_id,
            departments[index].id,
            &request_length);
        if (request == NULL ||
            exchange_request(
                host,
                port,
                request,
                request_length,
                response,
                sizeof(response),
                &response_length) != 0 ||
            print_doctor_response(
                response,
                response_length,
                *request_id,
                departments[index].id) != 0)
        {
            free(request);
            return -1;
        }
        free(request);
        *request_id = advance_request_id(*request_id);
    }
    return 0;
}

static int show_admin_tickets(
    const char *host,
    const char *port,
    uint64_t *request_id)
{
    int64_t after_id = 0;
    int has_more = 0;

    puts("\n[号单 tickets]");
    puts("ID     用户名       科室       号码   状态       日期       叫号时间");
    do
    {
        char *request;
        char response[CLINIC_MAX_FRAME_SIZE + 1U];
        size_t request_length = 0U;
        size_t response_length = 0U;
        int64_t last_id = after_id;

        request = create_admin_page_request(
            "admin_list_tickets",
            *request_id,
            after_id,
            ADMIN_DATA_PAGE_LIMIT,
            &request_length);
        if (request == NULL ||
            exchange_request(
                host,
                port,
                request,
                request_length,
                response,
                sizeof(response),
                &response_length) != 0 ||
            print_admin_ticket_page(
                response,
                response_length,
                *request_id,
                after_id,
                &last_id,
                &has_more) != 0)
        {
            free(request);
            return -1;
        }
        free(request);
        *request_id = advance_request_id(*request_id);
        after_id = last_id;
    } while (has_more);
    return 0;
}

/* 所有数据都通过服务器只读接口获取，管理台不直接打开 clinic.db。 */
static int show_admin_data(
    const char *host,
    const char *port,
    uint64_t *request_id,
    const ClinicDepartment *departments,
    size_t department_count)
{
    size_t index;

    if (host == NULL || port == NULL || request_id == NULL ||
        departments == NULL || department_count == 0U)
    {
        return -1;
    }
    puts("\n========== 后台数据库只读核验 ==========");
    if (show_admin_users(host, port, request_id) != 0)
    {
        return -1;
    }

    puts("\n[科室 departments]");
    puts("ID       科室名称");
    for (index = 0U; index < department_count; ++index)
    {
        printf(
            "%-8" PRId64 " %s\n",
            departments[index].id,
            departments[index].name);
    }
    if (show_admin_doctors(
            host,
            port,
            request_id,
            departments,
            department_count) != 0 ||
        show_admin_tickets(host, port, request_id) != 0)
    {
        return -1;
    }
    puts("========== 数据核验结束 ==========\n");
    return 0;
}

/* 把用户选择的科室 ID 编成 call_next；客户端不参与“谁是下一号”的排序。 */
static int perform_interactive_call(
    const char *host,
    const char *port,
    uint64_t request_id,
    int64_t department_id,
    char *response,
    size_t response_capacity,
    size_t *response_length,
    int *business_ok)
{
    char *request;
    size_t request_length = 0U;
    int status = -1;

    request = create_request(request_id, department_id, &request_length);
    if (request != NULL &&
        exchange_request(
            host,
            port,
            request,
            request_length,
            response,
            response_capacity,
            response_length) == 0 &&
        validate_response(
            response,
            *response_length,
            request_id,
            business_ok) == 0)
    {
        status = 0;
    }
    free(request);
    return status;
}

static void print_call_result(
    const char *response,
    int business_ok,
    const ClinicDepartment *department)
{
    cJSON *root = cJSON_Parse(response);

    if (!cJSON_IsObject(root) || department == NULL)
    {
        cJSON_Delete(root);
        puts("叫号结果解析失败。\n");
        return;
    }
    if (business_ok)
    {
        cJSON *ticket = get_unique_object_item(root, "ticket");
        cJSON *id = get_unique_object_item(ticket, "id");
        cJSON *queue_number = get_unique_object_item(ticket, "queue_number");

        printf(
            "\n叫号成功\n科室：%s\n当前叫号：%" PRId64
            "\n号单 ID：%" PRId64 "\n\n",
            department->name,
            (int64_t)queue_number->valuedouble,
            (int64_t)id->valuedouble);
    }
    else
    {
        cJSON *error_code = get_unique_object_item(root, "error_code");
        const char *code = cJSON_IsString(error_code)
            ? error_code->valuestring
            : "UNKNOWN_ERROR";

        if (strcmp(code, "NO_WAITING_TICKET") == 0)
        {
            printf("\n%s暂无等待号单。\n\n", department->name);
        }
        else if (strcmp(code, "DEPARTMENT_NOT_FOUND") == 0)
        {
            puts("\n科室不存在，请刷新科室列表。\n");
        }
        else
        {
            printf("\n叫号失败：%s\n\n", code);
        }
    }
    cJSON_Delete(root);
}

static void print_department_menu(
    const ClinicDepartment *departments,
    size_t department_count)
{
    size_t index;

    puts("\n医路通叫号管理台\n");
    for (index = 0U; index < department_count; ++index)
    {
        printf(
            "%zu. %s（科室 ID：%" PRId64 "）\n",
            index + 1U,
            departments[index].name,
            departments[index].id);
    }
    puts("r. 刷新科室");
    puts("d. 查看后台数据（只读）");
    puts("q. 退出");
}

static AdminMenuAction parse_menu_action(
    char *input,
    size_t department_count,
    size_t *selected_index)
{
    uint64_t selection;

    if (input == NULL || selected_index == NULL)
    {
        return ADMIN_MENU_INVALID;
    }
    input[strcspn(input, "\r\n")] = '\0';
    if (strcmp(input, "q") == 0 || strcmp(input, "Q") == 0)
    {
        return ADMIN_MENU_QUIT;
    }
    if (strcmp(input, "r") == 0 || strcmp(input, "R") == 0)
    {
        return ADMIN_MENU_REFRESH;
    }
    if (strcmp(input, "d") == 0 || strcmp(input, "D") == 0)
    {
        return ADMIN_MENU_DATA;
    }
    if (parse_uint64_text(input, &selection) != 0 || selection == 0U ||
        selection > department_count)
    {
        return ADMIN_MENU_INVALID;
    }
    *selected_index = (size_t)(selection - 1U);
    return ADMIN_MENU_CALL;
}

/* 循环菜单支持数字叫号、d 只读查数据、r 刷新科室、q 退出。 */
static int run_interactive_mode(void)
{
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    size_t department_count = 0U;
    uint64_t request_id = create_interactive_request_id();
    char input[ADMIN_INPUT_CAPACITY];
    int exit_status = EXIT_FAILURE;

    (void)signal(SIGPIPE, SIG_IGN);
    if (clinic_net_startup() != 0)
    {
        fputs("网络初始化失败。\n", stderr);
        return EXIT_FAILURE;
    }
    if (load_departments(
            ADMIN_DEFAULT_HOST,
            ADMIN_DEFAULT_PORT,
            request_id,
            departments,
            CLINIC_MAX_DEPARTMENTS,
            &department_count) != 0)
    {
        fprintf(
            stderr,
            "无法连接 %s:%s 或获取科室列表失败。\n",
            ADMIN_DEFAULT_HOST,
            ADMIN_DEFAULT_PORT);
        clinic_net_cleanup();
        return EXIT_FAILURE;
    }
    request_id = advance_request_id(request_id);

    for (;;)
    {
        size_t selected_index = 0U;
        AdminMenuAction action;

        print_department_menu(departments, department_count);
        fputs("\n请选择操作：", stdout);
        (void)fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            exit_status = feof(stdin) ? EXIT_SUCCESS : EXIT_FAILURE;
            break;
        }
        action = parse_menu_action(input, department_count, &selected_index);
        if (action == ADMIN_MENU_QUIT)
        {
            exit_status = EXIT_SUCCESS;
            break;
        }
        if (action == ADMIN_MENU_INVALID)
        {
            puts("输入无效，请输入菜单中的数字、d、r 或 q。");
            continue;
        }
        if (action == ADMIN_MENU_DATA)
        {
            if (show_admin_data(
                    ADMIN_DEFAULT_HOST,
                    ADMIN_DEFAULT_PORT,
                    &request_id,
                    departments,
                    department_count) != 0)
            {
                puts("后台数据查询失败，请确认服务器和协议版本一致。");
            }
            continue;
        }
        if (action == ADMIN_MENU_REFRESH)
        {
            if (load_departments(
                    ADMIN_DEFAULT_HOST,
                    ADMIN_DEFAULT_PORT,
                    request_id,
                    departments,
                    CLINIC_MAX_DEPARTMENTS,
                    &department_count) != 0)
            {
                puts("刷新科室失败，请确认服务器是否正常运行。");
            }
            request_id = advance_request_id(request_id);
            continue;
        }
        if (action == ADMIN_MENU_CALL)
        {
            char response[CLINIC_MAX_FRAME_SIZE + 1U];
            size_t response_length = 0U;
            int business_ok = 0;

            if (perform_interactive_call(
                    ADMIN_DEFAULT_HOST,
                    ADMIN_DEFAULT_PORT,
                    request_id,
                    departments[selected_index].id,
                    response,
                    sizeof(response),
                    &response_length,
                    &business_ok) != 0)
            {
                puts("叫号请求失败，请确认服务器是否正常运行。");
            }
            else
            {
                print_call_result(
                    response,
                    business_ok,
                    &departments[selected_index]);
            }
            request_id = advance_request_id(request_id);
        }
    }
    clinic_net_cleanup();
    return exit_status;
}

/* 无参数进入答辩演示菜单；四参数模式用于脚本化指定服务器、请求 ID 和科室 ID。 */
int main(int argc, char **argv)
{
    const char *host;
    const char *port;
    uint64_t request_id;
    int64_t department_id;
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
    char *request;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t request_length = 0U;
    size_t response_length = 0U;
    int business_ok = 0;
    int exit_status = EXIT_FAILURE;

    if (argc == 1)
    {
        return run_interactive_mode();
    }
    if (argc != 5)
    {
        fprintf(
            stderr,
            "usage:\n"
            "  %s\n"
            "  %s <server_ip> <port> <request_id> <department_id>\n",
            argv[0],
            argv[0]);
        return EXIT_FAILURE;
    }
    host = argv[1];
    port = argv[2];
    if (host[0] == '\0' || !port_is_valid(port) ||
        parse_uint64_text(argv[3], &request_id) != 0 ||
        parse_positive_int64_text(argv[4], &department_id) != 0)
    {
        fprintf(stderr, "invalid administrator call arguments\n");
        return EXIT_FAILURE;
    }
    request = create_request(request_id, department_id, &request_length);
    if (request == NULL)
    {
        fprintf(stderr, "could not encode administrator call request\n");
        return EXIT_FAILURE;
    }

    (void)signal(SIGPIPE, SIG_IGN);
    if (clinic_net_startup() != 0 ||
        clinic_net_connect(host, port, &socket_fd) != 0)
    {
        fprintf(stderr, "could not connect to %s:%s\n", host, port);
        clinic_net_cleanup();
        free(request);
        return EXIT_FAILURE;
    }
    if (clinic_net_send_all(socket_fd, request, request_length) != 0 ||
        receive_response(
            socket_fd,
            response,
            sizeof(response),
            &response_length) != 0 ||
        validate_response(
            response,
            response_length,
            request_id,
            &business_ok) != 0)
    {
        fprintf(stderr, "administrator call request failed\n");
    }
    else if (fwrite(response, 1U, response_length, stdout) != response_length)
    {
        fprintf(stderr, "could not write administrator call response\n");
    }
    else if (!business_ok)
    {
        fprintf(stderr, "administrator call request returned a business error\n");
    }
    else
    {
        exit_status = EXIT_SUCCESS;
    }

    clinic_socket_close(socket_fd);
    clinic_net_cleanup();
    free(request);
    return exit_status;
}
