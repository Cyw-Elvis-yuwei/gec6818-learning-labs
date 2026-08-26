/*
 * 文件作用：Ubuntu 主机端医生查询客户端。
 * 它携带 department_id 请求该科室医生并检查响应，供真实 TCP 联调和回归测试使用；
 * 医生查询只浏览资料，取号仍按科室完成。
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clinic_net.h"
#include "clinic_protocol.h"

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

#define DOCTOR_RESPONSE_TIMEOUT_SECONDS 5

static int parse_request_id(const char *text, uint64_t *request_id)
{
    const char *cursor;
    uint64_t parsed = 0U;

    if (text == NULL || request_id == NULL || text[0] == '\0')
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
    *request_id = parsed;
    return 0;
}

static int parse_department_id(const char *text, int64_t *department_id)
{
    const char *cursor;
    uint64_t parsed = 0U;

    if (text == NULL || department_id == NULL || text[0] == '\0')
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
        if (parsed > ((uint64_t)INT64_MAX - digit) / UINT64_C(10))
        {
            return -1;
        }
        parsed = parsed * UINT64_C(10) + digit;
    }
    if (parsed == 0U)
    {
        return -1;
    }
    *department_id = (int64_t)parsed;
    return 0;
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
        cJSON_AddStringToObject(root, "type", "list_doctors") == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddRawToObject(root, "department_id", department_id_text) == NULL)
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
    deadline.tv_sec += DOCTOR_RESPONSE_TIMEOUT_SECONDS;

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
            poll_status = poll(
                &descriptor,
                1U,
                (int)timeout_milliseconds);
            if (poll_status >= 0 || errno != EINTR)
            {
                break;
            }
        }
        if (poll_status <= 0 || (descriptor.revents & POLLIN) == 0)
        {
            response[0] = '\0';
            return -1;
        }

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

    response[0] = '\0';
    return -1;
}

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

static int find_object_value_span(
    const cJSON *object,
    const char *json,
    size_t length,
    const char *name,
    const char **value_begin,
    const char **value_end)
{
    const cJSON *child = object->child;
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

static int parse_uint64_span(
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

static int parse_positive_int64_span(
    const char *begin,
    const char *end,
    int64_t *value)
{
    uint64_t parsed;

    if (value == NULL || parse_uint64_span(begin, end, &parsed) != 0 ||
        parsed == 0U || parsed > (uint64_t)INT64_MAX)
    {
        return -1;
    }
    *value = (int64_t)parsed;
    return 0;
}

static int response_has_exact_request_id(
    const cJSON *root,
    const char *response,
    size_t response_length,
    uint64_t expected_request_id)
{
    const char *begin = NULL;
    const char *end = NULL;
    uint64_t parsed;

    return find_object_value_span(
               root,
               response,
               response_length,
               "request_id",
               &begin,
               &end) == 0 &&
        parse_uint64_span(begin, end, &parsed) == 0 &&
        parsed == expected_request_id;
}

static int doctor_has_required_fields(const cJSON *doctor)
{
    return cJSON_IsObject(doctor) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(doctor, "id")) &&
        cJSON_IsNumber(
            cJSON_GetObjectItemCaseSensitive(doctor, "department_id")) &&
        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(doctor, "name")) &&
        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(doctor, "title")) &&
        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(doctor, "specialty"));
}

static int doctor_integer_fields_are_valid(
    const cJSON *doctor,
    const char *json,
    size_t length)
{
    const char *id_begin = NULL;
    const char *id_end = NULL;
    const char *department_id_begin = NULL;
    const char *department_id_end = NULL;
    int64_t id;
    int64_t department_id;

    return find_object_value_span(
               doctor,
               json,
               length,
               "id",
               &id_begin,
               &id_end) == 0 &&
        find_object_value_span(
            doctor,
            json,
            length,
            "department_id",
            &department_id_begin,
            &department_id_end) == 0 &&
        parse_positive_int64_span(id_begin, id_end, &id) == 0 &&
        parse_positive_int64_span(
            department_id_begin,
            department_id_end,
            &department_id) == 0;
}

static int doctor_array_integer_fields_are_valid(
    const cJSON *doctors,
    const char *array_begin,
    const char *array_end)
{
    const cJSON *doctor = doctors->child;
    const char *cursor;

    if (array_begin == NULL || array_end == NULL ||
        array_begin >= array_end || *array_begin != '[')
    {
        return 0;
    }
    cursor = skip_json_space(array_begin + 1, array_end);
    while (doctor != NULL)
    {
        const char *doctor_end = skip_validated_value(cursor, array_end);

        if (doctor_end == NULL || cursor >= array_end || *cursor != '{' ||
            !doctor_integer_fields_are_valid(
                doctor,
                cursor,
                (size_t)(doctor_end - cursor)))
        {
            return 0;
        }
        cursor = skip_json_space(doctor_end, array_end);
        doctor = doctor->next;
        if (doctor != NULL)
        {
            if (cursor >= array_end || *cursor != ',')
            {
                return 0;
            }
            cursor = skip_json_space(cursor + 1, array_end);
        }
    }
    return cursor < array_end && *cursor == ']' && cursor + 1 == array_end;
}

static int validate_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int *business_ok)
{
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *doctors;
    const char *doctors_begin = NULL;
    const char *doctors_end = NULL;
    const char *parse_end = NULL;
    int index;

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
    doctors = get_unique_object_item(root, "doctors");

    if ((!cJSON_IsTrue(ok) && !cJSON_IsFalse(ok)) ||
        !cJSON_IsNumber(request_id) ||
        !response_has_exact_request_id(
            root,
            response,
            response_length,
            expected_request_id) ||
        cJSON_GetObjectItemCaseSensitive(root, "password") != NULL)
    {
        cJSON_Delete(root);
        return -1;
    }

    *business_ok = cJSON_IsTrue(ok);
    if (*business_ok)
    {
        if (!cJSON_IsArray(doctors) ||
            find_object_value_span(
                root,
                response,
                response_length,
                "doctors",
                &doctors_begin,
                &doctors_end) != 0 ||
            !doctor_array_integer_fields_are_valid(
                doctors,
                doctors_begin,
                doctors_end) ||
            cJSON_GetObjectItemCaseSensitive(root, "departments") != NULL ||
            cJSON_GetObjectItemCaseSensitive(root, "user_id") != NULL)
        {
            cJSON_Delete(root);
            return -1;
        }
        for (index = 0; index < cJSON_GetArraySize(doctors); ++index)
        {
            if (!doctor_has_required_fields(cJSON_GetArrayItem(doctors, index)))
            {
                cJSON_Delete(root);
                return -1;
            }
        }
    }
    else if (!cJSON_IsString(
                 cJSON_GetObjectItemCaseSensitive(root, "error_code")))
    {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

int main(int argc, char **argv)
{
    const char *host;
    const char *port;
    uint64_t request_id;
    int64_t department_id;
    clinic_socket_t socket_fd;
    char *request;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t request_length = 0U;
    size_t response_length = 0U;
    int business_ok = 0;
    int exit_status = EXIT_FAILURE;

    if (argc != 5)
    {
        fprintf(
            stderr,
            "usage: %s <server_ip> <port> <request_id> <department_id>\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    host = argv[1];
    port = argv[2];
    if (host[0] == '\0' || port[0] == '\0' ||
        parse_request_id(argv[3], &request_id) != 0 ||
        parse_department_id(argv[4], &department_id) != 0)
    {
        fprintf(stderr, "invalid doctor request arguments\n");
        return EXIT_FAILURE;
    }

    request = create_request(request_id, department_id, &request_length);
    if (request == NULL)
    {
        fprintf(stderr, "could not encode doctor request\n");
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
        fprintf(stderr, "doctor request failed\n");
    }
    else if (fwrite(response, 1U, response_length, stdout) == response_length &&
             business_ok)
    {
        exit_status = EXIT_SUCCESS;
    }
    else if (!business_ok)
    {
        fprintf(stderr, "doctor request returned a business error\n");
    }

    clinic_socket_close(socket_fd);
    clinic_net_cleanup();
    free(request);
    return exit_status;
}
