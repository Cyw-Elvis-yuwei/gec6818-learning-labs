/*
 * 文件作用：业务 JSON 与 ClinicRequest/ClinicResponse 结构体之间的严格转换层。
 * 解码支持注册、登录、科室、医生、取号、号单查询、叫号和管理台只读分页；编码根据
 * ResponseKind 输出 user_id、departments、doctors、ticket、queue_summary 或管理数据。
 *
 * 严格校验会拒绝缺失字段、重复字段、错误类型、负数、整数溢出和额外字段，并保留精确
 * request_id。这里仅做协议转换，不查询 SQLite，也不决定重复取号或叫号顺序。
 *
 * 为什么不能只调用一次 cJSON_GetObjectItem：cJSON 数字内部使用 double，超大整数可能
 * 丢失精度，而且普通查找不能拒绝重复字段。这里同时检查原始文本跨度，保证 request_id
 * 和各业务 ID 是无小数、无指数、范围正确的十进制整数，并拒绝缺失、重复和额外字段。
 */
#include "clinic_json.h"

#include <cjson/cJSON.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_EXACT_INTEGER_MAX UINT64_C(9007199254740991)

/* 以下 skip/find 辅助函数在原始 JSON 文本上定位值，供“精确整数”和重复字段检查使用。 */
static const char *skip_json_space(const char *cursor, const char *end)
{
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\n' || *cursor == '\r'))
    {
        ++cursor;
    }
    return cursor;
}

static const char *skip_validated_string(
    const char *cursor,
    const char *end)
{
    if (cursor >= end || *cursor != '"')
    {
        return NULL;
    }

    ++cursor;
    while (cursor < end)
    {
        if (*cursor == '\\')
        {
            cursor += 2;
            continue;
        }
        if (*cursor == '"')
        {
            return cursor + 1;
        }
        ++cursor;
    }
    return NULL;
}

static const char *skip_validated_value(
    const char *cursor,
    const char *end)
{
    unsigned int depth = 0U;

    cursor = skip_json_space(cursor, end);
    if (cursor >= end)
    {
        return NULL;
    }
    if (*cursor == '"')
    {
        return skip_validated_string(cursor, end);
    }
    if (*cursor != '{' && *cursor != '[')
    {
        while (cursor < end && *cursor != ',' && *cursor != '}' &&
               !isspace((unsigned char)*cursor))
        {
            ++cursor;
        }
        return cursor;
    }

    while (cursor < end)
    {
        if (*cursor == '"')
        {
            cursor = skip_validated_string(cursor, end);
            if (cursor == NULL)
            {
                return NULL;
            }
            continue;
        }
        if (*cursor == '{' || *cursor == '[')
        {
            ++depth;
        }
        else if (*cursor == '}' || *cursor == ']')
        {
            if (depth == 0U)
            {
                return NULL;
            }
            --depth;
            if (depth == 0U)
            {
                return cursor + 1;
            }
        }
        ++cursor;
    }
    return NULL;
}

/* 同名字段必须恰好出现一次；出现 0 次或多次都返回 NULL。 */
static cJSON *get_unique_object_item(
    const cJSON *object,
    const char *name)
{
    cJSON *child;
    cJSON *match = NULL;
    unsigned int matches = 0U;

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

/* 严格白名单：call_next 只能带 type、request_id、department_id，额外字段也拒绝。 */
static int call_next_has_only_expected_fields(const cJSON *object)
{
    const cJSON *child;
    size_t field_count = 0U;

    if (!cJSON_IsObject(object))
    {
        return 0;
    }
    for (child = object->child; child != NULL; child = child->next)
    {
        if (child->string == NULL ||
            (strcmp(child->string, "type") != 0 &&
             strcmp(child->string, "request_id") != 0 &&
             strcmp(child->string, "department_id") != 0))
        {
            return 0;
        }
        ++field_count;
    }
    return field_count == 3U;
}

static int get_current_ticket_has_only_expected_fields(const cJSON *object)
{
    const cJSON *child;
    size_t field_count = 0U;

    if (!cJSON_IsObject(object))
    {
        return 0;
    }
    for (child = object->child; child != NULL; child = child->next)
    {
        if (child->string == NULL ||
            (strcmp(child->string, "type") != 0 &&
             strcmp(child->string, "request_id") != 0 &&
             strcmp(child->string, "user_id") != 0))
        {
            return 0;
        }
        ++field_count;
    }
    return field_count == 3U;
}

static int admin_page_has_only_expected_fields(const cJSON *object)
{
    const cJSON *child;
    size_t field_count = 0U;

    if (!cJSON_IsObject(object))
    {
        return 0;
    }
    for (child = object->child; child != NULL; child = child->next)
    {
        if (child->string == NULL ||
            (strcmp(child->string, "type") != 0 &&
             strcmp(child->string, "request_id") != 0 &&
             strcmp(child->string, "after_id") != 0 &&
             strcmp(child->string, "limit") != 0))
        {
            return 0;
        }
        ++field_count;
    }
    return field_count == 4U;
}

static int find_top_level_value_span(
    const cJSON *root,
    const char *json,
    size_t length,
    const char *name,
    const char **value_begin,
    const char **value_end)
{
    const cJSON *child = root->child;
    const char *cursor = skip_json_space(json, json + length);
    const char *end = json + length;
    unsigned int matches = 0U;

    if (cursor >= end || *cursor != '{')
    {
        return -1;
    }
    ++cursor;

    while (child != NULL)
    {
        const char *current_value_begin;
        const char *current_value_end;

        cursor = skip_json_space(cursor, end);
        cursor = skip_validated_string(cursor, end);
        if (cursor == NULL)
        {
            return -1;
        }
        cursor = skip_json_space(cursor, end);
        if (cursor >= end || *cursor != ':')
        {
            return -1;
        }
        current_value_begin = skip_json_space(cursor + 1, end);
        current_value_end = skip_validated_value(current_value_begin, end);
        if (current_value_end == NULL)
        {
            return -1;
        }

        if (child->string != NULL && strcmp(child->string, name) == 0)
        {
            *value_begin = current_value_begin;
            *value_end = current_value_end;
            ++matches;
        }

        cursor = skip_json_space(current_value_end, end);
        if (cursor < end && *cursor == ',')
        {
            ++cursor;
        }
        child = child->next;
    }

    return matches == 1U ? 0 : -1;
}

static int parse_uint64_literal(
    const char *begin,
    const char *end,
    uint64_t *value)
{
    const char *cursor;
    uint64_t parsed = 0U;

    if (begin == NULL || end == NULL || value == NULL || begin >= end)
    {
        return -1;
    }

    for (cursor = begin; cursor < end; ++cursor)
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

static int parse_positive_int64_literal(
    const char *begin,
    const char *end,
    int64_t *value)
{
    uint64_t parsed;

    if (value == NULL || parse_uint64_literal(begin, end, &parsed) != 0 ||
        parsed == 0U || parsed > (uint64_t)INT64_MAX)
    {
        return -1;
    }
    *value = (int64_t)parsed;
    return 0;
}

static int copy_json_string(
    const cJSON *item,
    char *destination,
    size_t maximum_length)
{
    size_t length;

    if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
        return -1;
    }

    length = strlen(item->valuestring);
    if (length > maximum_length)
    {
        return -1;
    }
    memcpy(destination, item->valuestring, length + 1U);
    return 0;
}

static const char *get_ticket_skip_json_space(
    const char *cursor,
    const char *end)
{
    while (cursor < end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\n' || *cursor == '\r'))
    {
        ++cursor;
    }
    return cursor;
}

static const char *get_ticket_skip_json_string(
    const char *cursor,
    const char *end)
{
    if (cursor >= end || *cursor != '"')
    {
        return NULL;
    }
    ++cursor;
    while (cursor < end)
    {
        if (*cursor == '\\')
        {
            if (cursor + 1 >= end)
            {
                return NULL;
            }
            cursor += 2;
            continue;
        }
        if (*cursor == '"')
        {
            return cursor + 1;
        }
        ++cursor;
    }
    return NULL;
}

static const char *get_ticket_skip_json_value(
    const char *cursor,
    const char *end)
{
    unsigned int depth = 0U;

    cursor = get_ticket_skip_json_space(cursor, end);
    if (cursor >= end)
    {
        return NULL;
    }
    if (*cursor == '"')
    {
        return get_ticket_skip_json_string(cursor, end);
    }
    if (*cursor != '{' && *cursor != '[')
    {
        while (cursor < end && *cursor != ',' && *cursor != '}' &&
               *cursor != ']' && *cursor != ' ' && *cursor != '\t' &&
               *cursor != '\n' && *cursor != '\r')
        {
            ++cursor;
        }
        return cursor;
    }

    while (cursor < end)
    {
        if (*cursor == '"')
        {
            cursor = get_ticket_skip_json_string(cursor, end);
            if (cursor == NULL)
            {
                return NULL;
            }
            continue;
        }
        if (*cursor == '{' || *cursor == '[')
        {
            ++depth;
        }
        else if (*cursor == '}' || *cursor == ']')
        {
            if (depth == 0U)
            {
                return NULL;
            }
            --depth;
            if (depth == 0U)
            {
                return cursor + 1;
            }
        }
        ++cursor;
    }
    return NULL;
}

static const cJSON *get_ticket_unique_object_item(
    const cJSON *object,
    const char *name)
{
    const cJSON *child;
    const cJSON *match = NULL;
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

static int get_ticket_find_object_value_span(
    const cJSON *object,
    const char *json,
    size_t length,
    const char *name,
    const char **value_begin,
    const char **value_end)
{
    const cJSON *child = object->child;
    const char *cursor = get_ticket_skip_json_space(json, json + length);
    const char *end = json + length;
    unsigned int matches = 0U;

    if (cursor >= end || *cursor != '{' || value_begin == NULL ||
        value_end == NULL)
    {
        return -1;
    }
    ++cursor;
    while (child != NULL)
    {
        const char *current_begin;
        const char *current_end;

        cursor = get_ticket_skip_json_space(cursor, end);
        cursor = get_ticket_skip_json_string(cursor, end);
        if (cursor == NULL)
        {
            return -1;
        }
        cursor = get_ticket_skip_json_space(cursor, end);
        if (cursor >= end || *cursor != ':')
        {
            return -1;
        }
        current_begin = get_ticket_skip_json_space(cursor + 1, end);
        current_end = get_ticket_skip_json_value(current_begin, end);
        if (current_end == NULL)
        {
            return -1;
        }
        if (child->string != NULL && strcmp(child->string, name) == 0)
        {
            *value_begin = current_begin;
            *value_end = current_end;
            ++matches;
        }
        cursor = get_ticket_skip_json_space(current_end, end);
        if (cursor < end && *cursor == ',')
        {
            ++cursor;
        }
        child = child->next;
    }
    return matches == 1U ? 0 : -1;
}

static int get_ticket_parse_uint64_span(
    const char *begin,
    const char *end,
    uint64_t *value)
{
    const char *cursor;
    uint64_t parsed = 0U;

    if (begin == NULL || end == NULL || value == NULL || begin >= end)
    {
        return -1;
    }
    for (cursor = begin; cursor < end; ++cursor)
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

static ClinicJsonStatus decode_get_ticket_request_if_present(
    const char *json,
    size_t length,
    ClinicRequest *request,
    int *handled)
{
    const char *parse_end = NULL;
    const char *request_id_begin = NULL;
    const char *request_id_end = NULL;
    const char *ticket_id_begin = NULL;
    const char *ticket_id_end = NULL;
    const cJSON *type;
    const cJSON *request_id;
    const cJSON *ticket_id;
    char *terminated_json = NULL;
    cJSON *root = NULL;
    uint64_t parsed_request_id;
    uint64_t parsed_ticket_id;
    ClinicJsonStatus status = CLINIC_JSON_OK;

    if (handled == NULL)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    *handled = 0;
    if (json == NULL || request == NULL || length == 0U)
    {
        return CLINIC_JSON_OK;
    }
    if (length == SIZE_MAX || memchr(json, '\0', length) != NULL)
    {
        return CLINIC_JSON_OK;
    }
    terminated_json = cJSON_malloc(length + 1U);
    if (terminated_json == NULL)
    {
        return CLINIC_JSON_OK;
    }
    memcpy(terminated_json, json, length);
    terminated_json[length] = '\0';
    root = cJSON_ParseWithOpts(terminated_json, &parse_end, 1);
    if (!cJSON_IsObject(root))
    {
        goto cleanup;
    }
    type = get_ticket_unique_object_item(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "get_ticket") != 0)
    {
        goto cleanup;
    }

    *handled = 1;
    memset(request, 0, sizeof(*request));
    if (get_ticket_skip_json_space(
            parse_end,
            terminated_json + length) != terminated_json + length)
    {
        status = CLINIC_JSON_INVALID_REQUEST;
        goto cleanup;
    }
    request_id = get_ticket_unique_object_item(root, "request_id");
    ticket_id = get_ticket_unique_object_item(root, "ticket_id");
    if (!cJSON_IsNumber(request_id) || !cJSON_IsNumber(ticket_id) ||
        get_ticket_find_object_value_span(
            root,
            json,
            length,
            "request_id",
            &request_id_begin,
            &request_id_end) != 0 ||
        get_ticket_find_object_value_span(
            root,
            json,
            length,
            "ticket_id",
            &ticket_id_begin,
            &ticket_id_end) != 0 ||
        get_ticket_parse_uint64_span(
            request_id_begin,
            request_id_end,
            &parsed_request_id) != 0)
    {
        status = CLINIC_JSON_INVALID_REQUEST;
        goto cleanup;
    }
    request->request_id = parsed_request_id;
    if (get_ticket_parse_uint64_span(
            ticket_id_begin,
            ticket_id_end,
            &parsed_ticket_id) != 0 ||
        parsed_ticket_id == 0U || parsed_ticket_id > (uint64_t)INT64_MAX)
    {
        status = CLINIC_JSON_INVALID_REQUEST;
        goto cleanup;
    }
    request->type = CLINIC_REQ_GET_TICKET;
    request->ticket_id = (int64_t)parsed_ticket_id;

cleanup:
    cJSON_Delete(root);
    cJSON_free(terminated_json);
    return status;
}

static ClinicJsonStatus clinic_json_decode_request_without_get_ticket(
    const char *json,
    size_t length,
    ClinicRequest *request);

/*
 * 请求解码统一入口。成功后输出不再是文本，而是字段已验证的 ClinicRequest。
 * get_ticket 因精确整数兼容逻辑单独处理，其余请求进入统一严格解码路径。
 */
ClinicJsonStatus clinic_json_decode_request(
    const char *json,
    size_t length,
    ClinicRequest *request)
{
    int handled = 0;
    ClinicJsonStatus status = decode_get_ticket_request_if_present(
        json,
        length,
        request,
        &handled);

    if (handled)
    {
        return status;
    }
    return clinic_json_decode_request_without_get_ticket(
        json,
        length,
        request);
}

/*
 * 通用严格解码：拒绝内嵌 NUL、尾随垃圾、非对象根节点；先取唯一公共字段，
 * 再按 type 校验该业务允许的字段集合和类型。cleanup 统一释放 cJSON 和临时字符串。
 */
static ClinicJsonStatus clinic_json_decode_request_without_get_ticket(
    const char *json,
    size_t length,
    ClinicRequest *request)
{
    char *terminated_json;
    const char *parse_end = NULL;
    const char *request_id_begin = NULL;
    const char *request_id_end = NULL;
    const char *user_id_begin = NULL;
    const char *user_id_end = NULL;
    const char *department_id_begin = NULL;
    const char *department_id_end = NULL;
    const char *after_id_begin = NULL;
    const char *after_id_end = NULL;
    const char *limit_begin = NULL;
    const char *limit_end = NULL;
    cJSON *root = NULL;
    cJSON *type_item;
    cJSON *request_id_item;
    cJSON *user_id_item;
    cJSON *department_id_item;
    cJSON *after_id_item;
    cJSON *limit_item;
    cJSON *username_item;
    cJSON *password_item;
    uint64_t parsed_after_id = 0U;
    uint64_t parsed_limit = 0U;
    ClinicJsonStatus status = CLINIC_JSON_INVALID_REQUEST;

    if (json == NULL || request == NULL)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    memset(request, 0, sizeof(*request));

    if (length == 0U || length == SIZE_MAX ||
        memchr(json, '\0', length) != NULL)
    {
        return CLINIC_JSON_INVALID_JSON;
    }

    terminated_json = malloc(length + 1U);
    if (terminated_json == NULL)
    {
        return CLINIC_JSON_NO_MEMORY;
    }
    memcpy(terminated_json, json, length);
    terminated_json[length] = '\0';

    root = cJSON_ParseWithOpts(terminated_json, &parse_end, 1);
    if (root == NULL || parse_end == NULL ||
        parse_end != terminated_json + length || !cJSON_IsObject(root))
    {
        status = CLINIC_JSON_INVALID_JSON;
        goto cleanup;
    }

    request_id_item = get_unique_object_item(root, "request_id");
    if (request_id_item == NULL || !cJSON_IsNumber(request_id_item) ||
        find_top_level_value_span(
            root,
            terminated_json,
            length,
            "request_id",
            &request_id_begin,
            &request_id_end) != 0 ||
        parse_uint64_literal(
            request_id_begin,
            request_id_end,
            &request->request_id) != 0)
    {
        goto cleanup;
    }

    type_item = get_unique_object_item(root, "type");
    if (!cJSON_IsString(type_item) || type_item->valuestring == NULL)
    {
        goto cleanup;
    }

    if (strcmp(type_item->valuestring, "admin_list_users") == 0 ||
        strcmp(type_item->valuestring, "admin_list_tickets") == 0)
    {
        after_id_item = get_unique_object_item(root, "after_id");
        limit_item = get_unique_object_item(root, "limit");
        if (!admin_page_has_only_expected_fields(root) ||
            request->request_id == 0U ||
            request->request_id > JSON_EXACT_INTEGER_MAX ||
            after_id_item == NULL || !cJSON_IsNumber(after_id_item) ||
            limit_item == NULL || !cJSON_IsNumber(limit_item) ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "after_id",
                &after_id_begin,
                &after_id_end) != 0 ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "limit",
                &limit_begin,
                &limit_end) != 0 ||
            parse_uint64_literal(
                after_id_begin,
                after_id_end,
                &parsed_after_id) != 0 ||
            parse_uint64_literal(
                limit_begin,
                limit_end,
                &parsed_limit) != 0 ||
            parsed_after_id > JSON_EXACT_INTEGER_MAX ||
            parsed_after_id > (uint64_t)INT64_MAX ||
            parsed_limit == 0U ||
            parsed_limit > CLINIC_ADMIN_PAGE_MAX_ITEMS)
        {
            goto cleanup;
        }
        request->after_id = (int64_t)parsed_after_id;
        request->limit = (size_t)parsed_limit;
        request->type =
            strcmp(type_item->valuestring, "admin_list_users") == 0
                ? CLINIC_REQ_ADMIN_LIST_USERS
                : CLINIC_REQ_ADMIN_LIST_TICKETS;
        status = CLINIC_JSON_OK;
    }
    else if (strcmp(type_item->valuestring, "get_current_ticket") == 0)
    {
        user_id_item = get_unique_object_item(root, "user_id");
        if (!get_current_ticket_has_only_expected_fields(root) ||
            request->request_id == 0U ||
            request->request_id > JSON_EXACT_INTEGER_MAX ||
            user_id_item == NULL || !cJSON_IsNumber(user_id_item) ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "user_id",
                &user_id_begin,
                &user_id_end) != 0 ||
            parse_positive_int64_literal(
                user_id_begin,
                user_id_end,
                &request->user_id) != 0 ||
            (uint64_t)request->user_id > JSON_EXACT_INTEGER_MAX)
        {
            goto cleanup;
        }
        request->type = CLINIC_REQ_GET_CURRENT_TICKET;
        status = CLINIC_JSON_OK;
    }
    else if (strcmp(type_item->valuestring, "call_next") == 0)
    {
        department_id_item = get_unique_object_item(root, "department_id");
        if (!call_next_has_only_expected_fields(root) ||
            department_id_item == NULL ||
            !cJSON_IsNumber(department_id_item) ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "department_id",
                &department_id_begin,
                &department_id_end) != 0 ||
            parse_positive_int64_literal(
                department_id_begin,
                department_id_end,
                &request->department_id) != 0)
        {
            goto cleanup;
        }
        request->type = CLINIC_REQ_CALL_NEXT;
        status = CLINIC_JSON_OK;
    }
    else if (strcmp(type_item->valuestring, "create_ticket") == 0)
    {
        user_id_item = get_unique_object_item(root, "user_id");
        department_id_item = get_unique_object_item(root, "department_id");
        if (user_id_item == NULL || !cJSON_IsNumber(user_id_item) ||
            department_id_item == NULL || !cJSON_IsNumber(department_id_item) ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "user_id",
                &user_id_begin,
                &user_id_end) != 0 ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "department_id",
                &department_id_begin,
                &department_id_end) != 0 ||
            parse_positive_int64_literal(
                user_id_begin,
                user_id_end,
                &request->user_id) != 0 ||
            parse_positive_int64_literal(
                department_id_begin,
                department_id_end,
                &request->department_id) != 0)
        {
            goto cleanup;
        }
        request->type = CLINIC_REQ_CREATE_TICKET;
        status = CLINIC_JSON_OK;
    }
    else if (strcmp(type_item->valuestring, "list_doctors") == 0)
    {
        department_id_item = get_unique_object_item(root, "department_id");
        if (department_id_item == NULL ||
            !cJSON_IsNumber(department_id_item) ||
            find_top_level_value_span(
                root,
                terminated_json,
                length,
                "department_id",
                &department_id_begin,
                &department_id_end) != 0 ||
            parse_positive_int64_literal(
                department_id_begin,
                department_id_end,
                &request->department_id) != 0)
        {
            goto cleanup;
        }
        request->type = CLINIC_REQ_LIST_DOCTORS;
        status = CLINIC_JSON_OK;
    }
    else if (strcmp(type_item->valuestring, "list_departments") == 0)
    {
        request->type = CLINIC_REQ_LIST_DEPARTMENTS;
        status = CLINIC_JSON_OK;
    }
    else if (strcmp(type_item->valuestring, "register") == 0 ||
             strcmp(type_item->valuestring, "login") == 0)
    {
        username_item = get_unique_object_item(root, "username");
        password_item = get_unique_object_item(root, "password");
        if (copy_json_string(
                username_item,
                request->username,
                CLINIC_USERNAME_MAX_LENGTH) != 0 ||
            copy_json_string(
                password_item,
                request->password,
                CLINIC_PASSWORD_MAX_LENGTH) != 0)
        {
            goto cleanup;
        }

        if (strcmp(type_item->valuestring, "register") == 0)
        {
            request->type = CLINIC_REQ_REGISTER;
        }
        else
        {
            request->type = CLINIC_REQ_LOGIN;
        }
        status = CLINIC_JSON_OK;
    }
    else
    {
        status = CLINIC_JSON_UNKNOWN_REQUEST;
    }

cleanup:
    cJSON_Delete(root);
    free(terminated_json);
    return status;
}

static int fixed_string_is_terminated(const char *value, size_t capacity)
{
    return value != NULL && memchr(value, '\0', capacity) != NULL;
}

static int add_departments_to_object(
    cJSON *root,
    const ClinicResponse *response)
{
    cJSON *departments;
    size_t index;

    departments = cJSON_AddArrayToObject(root, "departments");
    if (departments == NULL)
    {
        return -1;
    }

    for (index = 0U; index < response->department_count; ++index)
    {
        char id_text[32];
        cJSON *department;

        if (!fixed_string_is_terminated(
                response->departments[index].name,
                sizeof(response->departments[index].name)) ||
            snprintf(
                id_text,
                sizeof(id_text),
                "%" PRId64,
                response->departments[index].id) < 0)
        {
            return -1;
        }

        department = cJSON_CreateObject();
        if (department == NULL ||
            cJSON_AddRawToObject(department, "id", id_text) == NULL ||
            cJSON_AddStringToObject(
                department,
                "name",
                response->departments[index].name) == NULL)
        {
            cJSON_Delete(department);
            return -1;
        }
        cJSON_AddItemToArray(departments, department);
    }
    return 0;
}

static int add_doctors_to_object(
    cJSON *root,
    const ClinicResponse *response)
{
    cJSON *doctors;
    size_t index;

    doctors = cJSON_AddArrayToObject(root, "doctors");
    if (doctors == NULL)
    {
        return -1;
    }

    for (index = 0U; index < response->doctor_count; ++index)
    {
        char id_text[32];
        char department_id_text[32];
        cJSON *doctor;

        if (!fixed_string_is_terminated(
                response->doctors[index].name,
                sizeof(response->doctors[index].name)) ||
            !fixed_string_is_terminated(
                response->doctors[index].title,
                sizeof(response->doctors[index].title)) ||
            !fixed_string_is_terminated(
                response->doctors[index].specialty,
                sizeof(response->doctors[index].specialty)) ||
            snprintf(
                id_text,
                sizeof(id_text),
                "%" PRId64,
                response->doctors[index].id) < 0 ||
            snprintf(
                department_id_text,
                sizeof(department_id_text),
                "%" PRId64,
                response->doctors[index].department_id) < 0)
        {
            return -1;
        }

        doctor = cJSON_CreateObject();
        if (doctor == NULL ||
            cJSON_AddRawToObject(doctor, "id", id_text) == NULL ||
            cJSON_AddRawToObject(
                doctor,
                "department_id",
                department_id_text) == NULL ||
            cJSON_AddStringToObject(
                doctor,
                "name",
                response->doctors[index].name) == NULL ||
            cJSON_AddStringToObject(
                doctor,
                "title",
                response->doctors[index].title) == NULL ||
            cJSON_AddStringToObject(
                doctor,
                "specialty",
                response->doctors[index].specialty) == NULL)
        {
            cJSON_Delete(doctor);
            return -1;
        }
        cJSON_AddItemToArray(doctors, doctor);
    }
    return 0;
}

static int add_admin_users_to_object(
    cJSON *root,
    const ClinicResponse *response)
{
    cJSON *users = cJSON_AddArrayToObject(root, "users");
    size_t index;

    if (users == NULL)
    {
        return -1;
    }
    for (index = 0U; index < response->admin_user_count; ++index)
    {
        const ClinicUserSummary *user = &response->admin_users[index];
        char id_text[32];
        cJSON *item;

        if (user->id <= 0 || user->username[0] == '\0' ||
            !fixed_string_is_terminated(user->username, sizeof(user->username)) ||
            snprintf(id_text, sizeof(id_text), "%" PRId64, user->id) < 0)
        {
            return -1;
        }
        item = cJSON_CreateObject();
        if (item == NULL ||
            cJSON_AddRawToObject(item, "id", id_text) == NULL ||
            cJSON_AddStringToObject(item, "username", user->username) == NULL)
        {
            cJSON_Delete(item);
            return -1;
        }
        cJSON_AddItemToArray(users, item);
    }
    return 0;
}

static const char *ticket_status_name(ClinicTicketStatus status)
{
    switch (status)
    {
        case CLINIC_TICKET_WAITING:
            return "WAITING";
        case CLINIC_TICKET_CALLED:
            return "CALLED";
        case CLINIC_TICKET_COMPLETED:
            return "COMPLETED";
        case CLINIC_TICKET_CANCELLED:
            return "CANCELLED";
        default:
            return NULL;
    }
}

static int add_admin_tickets_to_object(
    cJSON *root,
    const ClinicResponse *response)
{
    cJSON *tickets = cJSON_AddArrayToObject(root, "tickets");
    size_t index;

    if (tickets == NULL)
    {
        return -1;
    }
    for (index = 0U; index < response->admin_ticket_count; ++index)
    {
        const ClinicAdminTicketRecord *record =
            &response->admin_tickets[index];
        const ClinicTicket *ticket = &record->ticket;
        const char *status_name = ticket_status_name(ticket->status);
        char id_text[32];
        char user_id_text[32];
        char department_id_text[32];
        char queue_number_text[32];
        char created_time_text[32];
        char called_time_text[32];
        cJSON *item;

        if (status_name == NULL || ticket->id <= 0 || ticket->user_id <= 0 ||
            ticket->department_id <= 0 || ticket->queue_number <= 0 ||
            ticket->created_time <= 0 || ticket->called_time < 0 ||
            record->username[0] == '\0' || record->department_name[0] == '\0' ||
            !fixed_string_is_terminated(
                ticket->service_date,
                sizeof(ticket->service_date)) ||
            !fixed_string_is_terminated(
                record->username,
                sizeof(record->username)) ||
            !fixed_string_is_terminated(
                record->department_name,
                sizeof(record->department_name)) ||
            snprintf(id_text, sizeof(id_text), "%" PRId64, ticket->id) < 0 ||
            snprintf(
                user_id_text,
                sizeof(user_id_text),
                "%" PRId64,
                ticket->user_id) < 0 ||
            snprintf(
                department_id_text,
                sizeof(department_id_text),
                "%" PRId64,
                ticket->department_id) < 0 ||
            snprintf(
                queue_number_text,
                sizeof(queue_number_text),
                "%" PRId64,
                ticket->queue_number) < 0 ||
            snprintf(
                created_time_text,
                sizeof(created_time_text),
                "%" PRId64,
                ticket->created_time) < 0 ||
            (ticket->called_time != 0 &&
             snprintf(
                 called_time_text,
                 sizeof(called_time_text),
                 "%" PRId64,
                 ticket->called_time) < 0))
        {
            return -1;
        }

        item = cJSON_CreateObject();
        if (item == NULL ||
            cJSON_AddRawToObject(item, "id", id_text) == NULL ||
            cJSON_AddRawToObject(item, "user_id", user_id_text) == NULL ||
            cJSON_AddStringToObject(item, "username", record->username) == NULL ||
            cJSON_AddRawToObject(
                item,
                "department_id",
                department_id_text) == NULL ||
            cJSON_AddStringToObject(
                item,
                "department",
                record->department_name) == NULL ||
            cJSON_AddRawToObject(
                item,
                "queue_number",
                queue_number_text) == NULL ||
            cJSON_AddStringToObject(item, "status", status_name) == NULL ||
            cJSON_AddStringToObject(
                item,
                "service_date",
                ticket->service_date) == NULL ||
            cJSON_AddRawToObject(
                item,
                "created_time",
                created_time_text) == NULL ||
            (ticket->called_time == 0
                 ? cJSON_AddNullToObject(item, "called_time") == NULL
                 : cJSON_AddRawToObject(
                       item,
                       "called_time",
                       called_time_text) == NULL))
        {
            cJSON_Delete(item);
            return -1;
        }
        cJSON_AddItemToArray(tickets, item);
    }
    return 0;
}

static int add_ticket_to_object(
    cJSON *root,
    const ClinicResponse *response)
{
    char id_text[32];
    char user_id_text[32];
    char department_id_text[32];
    char queue_number_text[32];
    char created_time_text[32];
    char called_time_text[32];
    const char *status_name = ticket_status_name(response->ticket.status);
    cJSON *ticket = NULL;

    if (status_name == NULL || response->ticket.id <= 0 ||
        response->ticket.user_id <= 0 || response->ticket.department_id <= 0 ||
        response->ticket.queue_number <= 0 || response->ticket.created_time <= 0 ||
        response->ticket.called_time < 0 ||
        !fixed_string_is_terminated(
            response->ticket.service_date,
            sizeof(response->ticket.service_date)) ||
        snprintf(id_text, sizeof(id_text), "%" PRId64, response->ticket.id) < 0 ||
        snprintf(user_id_text, sizeof(user_id_text), "%" PRId64, response->ticket.user_id) < 0 ||
        snprintf(
            department_id_text,
            sizeof(department_id_text),
            "%" PRId64,
            response->ticket.department_id) < 0 ||
        snprintf(
            queue_number_text,
            sizeof(queue_number_text),
            "%" PRId64,
            response->ticket.queue_number) < 0 ||
        snprintf(
            created_time_text,
            sizeof(created_time_text),
            "%" PRId64,
            response->ticket.created_time) < 0 ||
        (response->ticket.called_time != 0 &&
         snprintf(
             called_time_text,
             sizeof(called_time_text),
             "%" PRId64,
             response->ticket.called_time) < 0))
    {
        return -1;
    }

    ticket = cJSON_CreateObject();
    if (ticket == NULL ||
        cJSON_AddRawToObject(ticket, "id", id_text) == NULL ||
        cJSON_AddRawToObject(ticket, "user_id", user_id_text) == NULL ||
        cJSON_AddRawToObject(
            ticket,
            "department_id",
            department_id_text) == NULL ||
        cJSON_AddRawToObject(
            ticket,
            "queue_number",
            queue_number_text) == NULL ||
        cJSON_AddStringToObject(ticket, "status", status_name) == NULL ||
        cJSON_AddStringToObject(
            ticket,
            "service_date",
            response->ticket.service_date) == NULL ||
        cJSON_AddRawToObject(
            ticket,
            "created_time",
            created_time_text) == NULL ||
        (response->ticket.called_time == 0
             ? cJSON_AddNullToObject(ticket, "called_time") == NULL
             : cJSON_AddRawToObject(
                   ticket,
                   "called_time",
                   called_time_text) == NULL))
    {
        cJSON_Delete(ticket);
        return -1;
    }
    cJSON_AddItemToObject(root, "ticket", ticket);
    return 0;
}

/* 当前叫号 0 在协议中编码为 null；前方人数必须是非负整数。 */
static int add_queue_summary_to_object(
    cJSON *root,
    const ClinicResponse *response)
{
    char current_called_text[32];
    char waiting_ahead_text[32];
    cJSON *summary = NULL;

    if (root == NULL || response == NULL ||
        response->queue_summary.current_called_queue_number < 0 ||
        response->queue_summary.waiting_ahead_count < 0 ||
        snprintf(
            current_called_text,
            sizeof(current_called_text),
            "%" PRId64,
            response->queue_summary.current_called_queue_number) < 0 ||
        snprintf(
            waiting_ahead_text,
            sizeof(waiting_ahead_text),
            "%" PRId64,
            response->queue_summary.waiting_ahead_count) < 0)
    {
        return -1;
    }

    summary = cJSON_CreateObject();
    if (summary == NULL ||
        (response->queue_summary.current_called_queue_number == 0
             ? cJSON_AddNullToObject(summary, "current_called_queue_number") == NULL
             : cJSON_AddRawToObject(
                   summary,
                   "current_called_queue_number",
                   current_called_text) == NULL) ||
        cJSON_AddRawToObject(
            summary,
            "waiting_ahead_count",
            waiting_ahead_text) == NULL)
    {
        cJSON_Delete(summary);
        return -1;
    }

    cJSON_AddItemToObject(root, "queue_summary", summary);
    return 0;
}

/*
 * 响应编码统一入口：先验证 ClinicResponse 内部一致性，再按 kind 添加对应载荷；
 * 成功响应与错误响应字段不同，最后序列化到调用者固定缓冲区并检查容量。
 * 本函数只负责“结构体怎样变 JSON”，不决定业务成功还是失败。
 */
ClinicJsonStatus clinic_json_encode_response(
    const ClinicResponse *response,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    char request_id_text[32];
    char user_id_text[32];
    cJSON *root = NULL;
    char *serialized = NULL;
    size_t serialized_length;
    ClinicJsonStatus status = CLINIC_JSON_NO_MEMORY;

    if (output_length != NULL)
    {
        *output_length = 0U;
    }
    if (output != NULL && output_capacity > 0U)
    {
        output[0] = '\0';
    }

    if (response == NULL || output == NULL || output_capacity == 0U ||
        output_length == NULL ||
        !fixed_string_is_terminated(
            response->message,
            sizeof(response->message)) ||
        (!response->ok &&
         !fixed_string_is_terminated(
             response->error_code,
             sizeof(response->error_code))))
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    if (response->ok &&
        response->kind != CLINIC_RESPONSE_AUTH &&
        response->kind != CLINIC_RESPONSE_DEPARTMENTS &&
        response->kind != CLINIC_RESPONSE_DOCTORS &&
        response->kind != CLINIC_RESPONSE_TICKET &&
        response->kind != CLINIC_RESPONSE_ADMIN_USERS &&
        response->kind != CLINIC_RESPONSE_ADMIN_TICKETS)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    if (response->queue_summary_valid < 0 ||
        response->queue_summary_valid > 1 ||
        (response->queue_summary_valid != 0 &&
         (!response->ok || response->kind != CLINIC_RESPONSE_TICKET ||
          response->queue_summary.current_called_queue_number < 0 ||
          response->queue_summary.waiting_ahead_count < 0)))
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    if (response->ok &&
        response->kind == CLINIC_RESPONSE_DEPARTMENTS &&
        response->department_count > CLINIC_MAX_DEPARTMENTS)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    if (response->ok &&
        response->kind == CLINIC_RESPONSE_DOCTORS &&
        response->doctor_count > CLINIC_MAX_DOCTORS)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    if (response->has_more < 0 || response->has_more > 1 ||
        (response->ok &&
         response->kind == CLINIC_RESPONSE_ADMIN_USERS &&
         response->admin_user_count > CLINIC_ADMIN_PAGE_MAX_ITEMS) ||
        (response->ok &&
         response->kind == CLINIC_RESPONSE_ADMIN_TICKETS &&
         response->admin_ticket_count > CLINIC_ADMIN_PAGE_MAX_ITEMS) ||
        (response->has_more != 0 &&
         response->kind != CLINIC_RESPONSE_ADMIN_USERS &&
         response->kind != CLINIC_RESPONSE_ADMIN_TICKETS))
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }
    if (response->ok && response->kind == CLINIC_RESPONSE_TICKET &&
        ticket_status_name(response->ticket.status) == NULL)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }

    if (snprintf(
            request_id_text,
            sizeof(request_id_text),
            "%" PRIu64,
            response->request_id) < 0)
    {
        return CLINIC_JSON_INVALID_ARGUMENT;
    }

    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddBoolToObject(root, "ok", response->ok != 0) == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL)
    {
        goto cleanup;
    }

    if (response->ok &&
        response->kind == CLINIC_RESPONSE_DEPARTMENTS)
    {
        if (add_departments_to_object(root, response) != 0 ||
            cJSON_AddStringToObject(
                root,
                "message",
                response->message) == NULL)
        {
            goto cleanup;
        }
    }
    else if (response->ok &&
             response->kind == CLINIC_RESPONSE_DOCTORS)
    {
        if (add_doctors_to_object(root, response) != 0 ||
            cJSON_AddStringToObject(
                root,
                "message",
                response->message) == NULL)
        {
            goto cleanup;
        }
    }
    else if (response->ok &&
             response->kind == CLINIC_RESPONSE_ADMIN_USERS)
    {
        if (add_admin_users_to_object(root, response) != 0 ||
            cJSON_AddBoolToObject(
                root,
                "has_more",
                response->has_more != 0) == NULL ||
            cJSON_AddStringToObject(
                root,
                "message",
                response->message) == NULL)
        {
            goto cleanup;
        }
    }
    else if (response->ok &&
             response->kind == CLINIC_RESPONSE_ADMIN_TICKETS)
    {
        if (add_admin_tickets_to_object(root, response) != 0 ||
            cJSON_AddBoolToObject(
                root,
                "has_more",
                response->has_more != 0) == NULL ||
            cJSON_AddStringToObject(
                root,
                "message",
                response->message) == NULL)
        {
            goto cleanup;
        }
    }
    else if (response->ok &&
             response->kind == CLINIC_RESPONSE_AUTH)
    {
        if (snprintf(
                user_id_text,
                sizeof(user_id_text),
                "%" PRId64,
                response->user_id) < 0 ||
            cJSON_AddRawToObject(root, "user_id", user_id_text) == NULL ||
            cJSON_AddStringToObject(
                root,
                "message",
                response->message) == NULL)
        {
            goto cleanup;
        }
    }
    else if (response->ok &&
             response->kind == CLINIC_RESPONSE_TICKET)
    {
        if (add_ticket_to_object(root, response) != 0 ||
            (response->queue_summary_valid != 0 &&
             add_queue_summary_to_object(root, response) != 0) ||
            cJSON_AddStringToObject(
                root,
                "message",
                response->message) == NULL)
        {
            goto cleanup;
        }
    }
    else if (cJSON_AddStringToObject(
                 root,
                 "error_code",
                 response->error_code) == NULL ||
             cJSON_AddStringToObject(
                 root,
                 "message",
                 response->message) == NULL)
    {
        goto cleanup;
    }

    serialized = cJSON_PrintUnformatted(root);
    if (serialized == NULL)
    {
        goto cleanup;
    }
    serialized_length = strlen(serialized);
    if (serialized_length > SIZE_MAX - 2U ||
        output_capacity < serialized_length + 2U)
    {
        status = CLINIC_JSON_OUTPUT_TOO_SMALL;
        goto cleanup;
    }

    memcpy(output, serialized, serialized_length);
    output[serialized_length] = '\n';
    output[serialized_length + 1U] = '\0';
    *output_length = serialized_length + 1U;
    status = CLINIC_JSON_OK;

cleanup:
    cJSON_free(serialized);
    cJSON_Delete(root);
    return status;
}
