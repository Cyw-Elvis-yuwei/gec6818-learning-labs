/*
 * 文件作用：Ubuntu 主机端门诊取号客户端。
 * 它发送 create_ticket(user_id, department_id) 并展示真实号单，供 TCP 集成测试和排障；
 * 请求不包含 doctor_id，重复取号是否返回原号单由服务器业务层决定。
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

#define TICKET_RESPONSE_TIMEOUT_SECONDS 5

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
    int64_t user_id,
    int64_t department_id,
    size_t *request_length)
{
    char request_id_text[32];
    char user_id_text[32];
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
    written = snprintf(user_id_text, sizeof(user_id_text), "%" PRId64, user_id);
    if (written < 0 || (size_t)written >= sizeof(user_id_text))
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
        cJSON_AddStringToObject(root, "type", "create_ticket") == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddRawToObject(root, "user_id", user_id_text) == NULL ||
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
    deadline.tv_sec += TICKET_RESPONSE_TIMEOUT_SECONDS;

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

static int ticket_status_is_known(const cJSON *status)
{
    return cJSON_IsString(status) &&
        (strcmp(status->valuestring, "WAITING") == 0 ||
         strcmp(status->valuestring, "CALLED") == 0 ||
         strcmp(status->valuestring, "COMPLETED") == 0 ||
         strcmp(status->valuestring, "CANCELLED") == 0);
}

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
        cJSON *ticket_id;
        cJSON *queue_number;
        cJSON *status;

        if (!cJSON_IsObject(ticket))
        {
            cJSON_Delete(root);
            return -1;
        }
        ticket_id = get_unique_object_item(ticket, "id");
        queue_number = get_unique_object_item(ticket, "queue_number");
        status = get_unique_object_item(ticket, "status");
        if (!cJSON_IsNumber(ticket_id) || ticket_id->valuedouble <= 0.0 ||
            !cJSON_IsNumber(queue_number) || queue_number->valuedouble <= 0.0 ||
            !ticket_status_is_known(status))
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
    int64_t user_id;
    int64_t department_id;
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
    char *request;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t request_length = 0U;
    size_t response_length = 0U;
    int business_ok = 0;
    int exit_status = EXIT_FAILURE;

    if (argc != 6)
    {
        fprintf(
            stderr,
            "usage: %s <server_ip> <port> <request_id> <user_id> <department_id>\n",
            argv[0]);
        return EXIT_FAILURE;
    }
    host = argv[1];
    port = argv[2];
    if (host[0] == '\0' || !port_is_valid(port) ||
        parse_uint64_text(argv[3], &request_id) != 0 ||
        parse_positive_int64_text(argv[4], &user_id) != 0 ||
        parse_positive_int64_text(argv[5], &department_id) != 0)
    {
        fprintf(stderr, "invalid ticket request arguments\n");
        return EXIT_FAILURE;
    }

    request = create_request(
        request_id,
        user_id,
        department_id,
        &request_length);
    if (request == NULL)
    {
        fprintf(stderr, "could not encode ticket request\n");
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
        fprintf(stderr, "ticket request failed\n");
    }
    else if (fwrite(response, 1U, response_length, stdout) != response_length)
    {
        fprintf(stderr, "could not write ticket response\n");
    }
    else if (!business_ok)
    {
        fprintf(stderr, "ticket request returned a business error\n");
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
