/*
 * 文件作用（答辩）：Ubuntu 主机端科室查询客户端。
 * 它发送 list_departments 并打印服务器返回的真实科室，主要用于脱离 LVGL 验证
 * TCP、Handler、Core、Store 和 SQLite 科室链路，不是开发板正式界面。
 */
#include "clinic_net.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *create_request(uint64_t request_id, size_t *request_length)
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

static int receive_response(
    clinic_socket_t socket_fd,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    size_t received_total = 0U;

    if (response == NULL || response_length == NULL ||
        response_capacity < CLINIC_MAX_FRAME_SIZE + 1U)
    {
        return -1;
    }
    *response_length = 0U;
    response[0] = '\0';

    while (received_total < CLINIC_MAX_FRAME_SIZE)
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

    response[0] = '\0';
    return -1;
}

static int response_has_request_id(
    const char *response,
    uint64_t expected_request_id)
{
    char expected[64];
    int written = snprintf(
        expected,
        sizeof(expected),
        "\"request_id\":%" PRIu64,
        expected_request_id);

    return written > 0 && (size_t)written < sizeof(expected) &&
        strstr(response, expected) != NULL;
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
    cJSON *departments;
    cJSON *message;
    const char *parse_end = NULL;

    if (response == NULL || business_ok == NULL || response_length == 0U ||
        response[response_length - 1U] != '\n' ||
        memchr(response, '\n', response_length - 1U) != NULL ||
        !response_has_request_id(response, expected_request_id))
    {
        return -1;
    }

    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    if (!cJSON_IsObject(root) || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return -1;
    }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    departments = cJSON_GetObjectItemCaseSensitive(root, "departments");
    message = cJSON_GetObjectItemCaseSensitive(root, "message");

    if ((!cJSON_IsTrue(ok) && !cJSON_IsFalse(ok)) ||
        !cJSON_IsNumber(request_id) || !cJSON_IsString(message) ||
        cJSON_GetObjectItemCaseSensitive(root, "password") != NULL)
    {
        cJSON_Delete(root);
        return -1;
    }

    *business_ok = cJSON_IsTrue(ok);
    if (*business_ok && !cJSON_IsArray(departments))
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
    clinic_socket_t socket_fd;
    char *request;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t request_length = 0U;
    size_t response_length = 0U;
    int business_ok = 0;
    int exit_status = EXIT_FAILURE;

    if (argc != 4)
    {
        fprintf(
            stderr,
            "usage: %s <server_ip> <port> <request_id>\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    host = argv[1];
    port = argv[2];
    if (host[0] == '\0' || port[0] == '\0' ||
        parse_request_id(argv[3], &request_id) != 0)
    {
        fprintf(stderr, "invalid department request arguments\n");
        return EXIT_FAILURE;
    }

    request = create_request(request_id, &request_length);
    if (request == NULL)
    {
        fprintf(stderr, "could not encode department request\n");
        return EXIT_FAILURE;
    }

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
        fprintf(stderr, "department request failed\n");
    }
    else
    {
        if (fwrite(response, 1U, response_length, stdout) == response_length &&
            business_ok)
        {
            exit_status = EXIT_SUCCESS;
        }
        else if (!business_ok)
        {
            fprintf(stderr, "department request returned a business error\n");
        }
    }

    clinic_socket_close(socket_fd);
    clinic_net_cleanup();
    free(request);
    return exit_status;
}
