/*
 * 文件作用：Ubuntu 主机端注册/登录命令行客户端，用于联调和自动化验证。
 * 它构造认证 JSON、经 TCP 发给真实服务器并打印响应；不是 S5P6818 的正式 LVGL 页面。
 * 该工具帮助单独验证网络和服务器认证链路，生产业务规则仍在 Core/Store/SQLite。
 */
#include "clinic_net.h"
#include "clinic_protocol.h"
#include "clinic_types.h"

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

static char *create_request(
    const char *action,
    uint64_t request_id,
    const char *username,
    const char *password,
    size_t *request_length)
{
    char request_id_text[32];
    cJSON *root = NULL;
    char *json = NULL;
    char *request = NULL;
    size_t json_length;

    if (snprintf(
            request_id_text,
            sizeof(request_id_text),
            "%" PRIu64,
            request_id) < 0)
    {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL ||
        cJSON_AddStringToObject(root, "type", action) == NULL ||
        cJSON_AddRawToObject(root, "request_id", request_id_text) == NULL ||
        cJSON_AddStringToObject(root, "username", username) == NULL ||
        cJSON_AddStringToObject(root, "password", password) == NULL)
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
    request = malloc(json_length + 2U);
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

static int receive_response(clinic_socket_t socket_fd)
{
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;

    while (response_length < CLINIC_MAX_FRAME_SIZE)
    {
        int received = recv(
            socket_fd,
            response + response_length,
            (int)(CLINIC_MAX_FRAME_SIZE - response_length),
            0);
        char *newline;

        if (received <= 0)
        {
            return -1;
        }
        response_length += (size_t)received;
        response[response_length] = '\0';
        newline = memchr(response, '\n', response_length);
        if (newline != NULL)
        {
            size_t line_length = (size_t)(newline - response) + 1U;
            return fwrite(response, 1U, line_length, stdout) == line_length
                ? 0
                : -1;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *action;
    const char *host;
    const char *port;
    const char *username;
    const char *password;
    uint64_t request_id;
    clinic_socket_t socket_fd;
    char *request;
    size_t request_length = 0U;
    int exit_status = EXIT_FAILURE;

    if (argc != 7)
    {
        fprintf(
            stderr,
            "usage: %s register|login <server_ip> <port> "
            "<request_id> <username> <password>\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    action = argv[1];
    host = argv[2];
    port = argv[3];
    username = argv[5];
    password = argv[6];
    if ((strcmp(action, "register") != 0 && strcmp(action, "login") != 0) ||
        parse_request_id(argv[4], &request_id) != 0 ||
        username[0] == '\0' || password[0] == '\0' ||
        strlen(username) > CLINIC_USERNAME_MAX_LENGTH ||
        strlen(password) > CLINIC_PASSWORD_MAX_LENGTH)
    {
        fprintf(stderr, "invalid authentication request arguments\n");
        return EXIT_FAILURE;
    }

    request = create_request(
        action,
        request_id,
        username,
        password,
        &request_length);
    if (request == NULL)
    {
        fprintf(stderr, "could not encode authentication request\n");
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

    if (clinic_net_send_all(socket_fd, request, request_length) == 0 &&
        receive_response(socket_fd) == 0)
    {
        exit_status = EXIT_SUCCESS;
    }
    else
    {
        fprintf(stderr, "authentication request failed\n");
    }

    clinic_socket_close(socket_fd);
    clinic_net_cleanup();
    free(request);
    return exit_status;
}
