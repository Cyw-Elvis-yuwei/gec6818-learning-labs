/*
 * 文件作用：Ubuntu 主机端按 ticket_id 查询号单的客户端。
 * 它发送 get_ticket 并解析完整 Ticket，适合验证取号后数据是否已经写入 SQLite；
 * 与按 user_id 查询当天最新号单的 host_current_ticket_client 含义不同。
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

#define TICKET_STATUS_TIMEOUT_SECONDS 5
#define TICKET_SERVICE_DATE_LENGTH 10U

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
    int64_t ticket_id,
    size_t *request_length)
{
    char request_id_text[32];
    char ticket_id_text[32];
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
        ticket_id_text,
        sizeof(ticket_id_text),
        "%" PRId64,
        ticket_id);
    if (written < 0 || (size_t)written >= sizeof(ticket_id_text))
    {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", "get_ticket") == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddRawToObject(root, "ticket_id", ticket_id_text) == NULL)
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
    deadline.tv_sec += TICKET_STATUS_TIMEOUT_SECONDS;

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

static int status_is_known(const cJSON *status)
{
    return cJSON_IsString(status) &&
        (strcmp(status->valuestring, "WAITING") == 0 ||
         strcmp(status->valuestring, "CALLED") == 0 ||
         strcmp(status->valuestring, "COMPLETED") == 0 ||
         strcmp(status->valuestring, "CANCELLED") == 0);
}

static int ticket_is_valid(const cJSON *ticket, int64_t expected_ticket_id)
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

    return cJSON_IsNumber(id) &&
        id->valuedouble == (double)expected_ticket_id &&
        cJSON_IsNumber(user_id) && user_id->valuedouble > 0.0 &&
        cJSON_IsNumber(department_id) && department_id->valuedouble > 0.0 &&
        cJSON_IsNumber(queue_number) && queue_number->valuedouble > 0.0 &&
        status_is_known(status) &&
        cJSON_IsString(service_date) &&
        strlen(service_date->valuestring) == TICKET_SERVICE_DATE_LENGTH &&
        cJSON_IsNumber(created_time) && created_time->valuedouble > 0.0 &&
        (cJSON_IsNull(called_time) ||
         (cJSON_IsNumber(called_time) && called_time->valuedouble > 0.0));
}

static int validate_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int64_t expected_ticket_id,
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
        if (!ticket_is_valid(ticket, expected_ticket_id) ||
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

int main(int argc, char **argv)
{
    const char *host;
    const char *port;
    uint64_t request_id;
    int64_t ticket_id;
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
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
            "usage: %s <server_ip> <port> <request_id> <ticket_id>\n",
            argv[0]);
        return EXIT_FAILURE;
    }
    host = argv[1];
    port = argv[2];
    if (host[0] == '\0' || !port_is_valid(port) ||
        parse_uint64_text(argv[3], &request_id) != 0 ||
        parse_positive_int64_text(argv[4], &ticket_id) != 0)
    {
        fprintf(stderr, "invalid ticket status arguments\n");
        return EXIT_FAILURE;
    }
    request = create_request(request_id, ticket_id, &request_length);
    if (request == NULL)
    {
        fprintf(stderr, "could not encode ticket status request\n");
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
            ticket_id,
            &business_ok) != 0)
    {
        fprintf(stderr, "ticket status request failed\n");
    }
    else if (fwrite(response, 1U, response_length, stdout) != response_length)
    {
        fprintf(stderr, "could not write ticket status response\n");
    }
    else if (!business_ok)
    {
        fprintf(stderr, "ticket status request returned a business error\n");
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
