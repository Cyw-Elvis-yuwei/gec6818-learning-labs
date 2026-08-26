#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clinic_net.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST_DATABASE_PATH "/tmp/clinic_tcp_auth_test.db"
#define SERVER_PATH "./build/linux/clinic_server"
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT "19010"

static int failures = 0;

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void sleep_milliseconds(long milliseconds)
{
    struct timespec duration;

    duration.tv_sec = milliseconds / 1000L;
    duration.tv_nsec = (milliseconds % 1000L) * 1000000L;
    (void)nanosleep(&duration, NULL);
}

static pid_t start_server(void)
{
    pid_t process_id = fork();

    if (process_id == 0)
    {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0)
        {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
            (void)close(null_fd);
        }
        execl(
            SERVER_PATH,
            SERVER_PATH,
            TEST_DATABASE_PATH,
            SERVER_PORT,
            (char *)NULL);
        _exit(127);
    }
    return process_id;
}

static int wait_for_server(void)
{
    unsigned int attempt;

    for (attempt = 0U; attempt < 50U; ++attempt)
    {
        clinic_socket_t socket_fd;

        if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) == 0)
        {
            clinic_socket_close(socket_fd);
            return 0;
        }
        sleep_milliseconds(100L);
    }
    return -1;
}

static void stop_server(pid_t process_id)
{
    unsigned int attempt;
    int status;

    if (process_id <= 0)
    {
        return;
    }
    (void)kill(process_id, SIGTERM);
    for (attempt = 0U; attempt < 50U; ++attempt)
    {
        pid_t result = waitpid(process_id, &status, WNOHANG);
        if (result == process_id)
        {
            return;
        }
        sleep_milliseconds(100L);
    }
    (void)kill(process_id, SIGKILL);
    (void)waitpid(process_id, &status, 0);
    ++failures;
    fprintf(stderr, "FAIL: server required SIGKILL\n");
}

static int send_frame(
    const char *frame,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    clinic_socket_t socket_fd;
    size_t received_total = 0U;

    *response_length = 0U;
    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) != 0)
    {
        return -1;
    }
    if (clinic_net_send_all(socket_fd, frame, strlen(frame)) != 0)
    {
        clinic_socket_close(socket_fd);
        return -1;
    }

    while (received_total + 1U < response_capacity)
    {
        int received = recv(
            socket_fd,
            response + received_total,
            (int)(response_capacity - received_total - 1U),
            0);
        char *newline;

        if (received <= 0)
        {
            clinic_socket_close(socket_fd);
            return -1;
        }
        received_total += (size_t)received;
        response[received_total] = '\0';
        newline = memchr(response, '\n', received_total);
        if (newline != NULL)
        {
            *response_length = (size_t)(newline - response) + 1U;
            response[*response_length] = '\0';
            clinic_socket_close(socket_fd);
            return 0;
        }
    }

    clinic_socket_close(socket_fd);
    return -1;
}

static int64_t check_business_response(
    const char *response,
    int expected_ok,
    uint64_t expected_request_id,
    const char *expected_error_code)
{
    cJSON *root = cJSON_Parse(response);
    cJSON *ok;
    cJSON *request_id;
    cJSON *user_id;
    cJSON *error_code;
    int64_t parsed_user_id = 0;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    user_id = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(expected_ok ? cJSON_IsTrue(ok) : cJSON_IsFalse(ok));
    CHECK(cJSON_IsNumber(request_id));
    if (cJSON_IsNumber(request_id))
    {
        CHECK(request_id->valuedouble == (double)expected_request_id);
    }
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(response, "\"password\":") == NULL);

    if (expected_ok)
    {
        CHECK(cJSON_IsNumber(user_id));
        if (cJSON_IsNumber(user_id))
        {
            parsed_user_id = (int64_t)user_id->valuedouble;
            CHECK(parsed_user_id > 0);
        }
    }
    else
    {
        CHECK(user_id == NULL);
        CHECK(cJSON_IsString(error_code));
        if (cJSON_IsString(error_code))
        {
            CHECK(strcmp(error_code->valuestring, expected_error_code) == 0);
        }
    }
    cJSON_Delete(root);
    return parsed_user_id;
}

static void test_tcp_flows(void)
{
    static const char ping[] = "{\"type\":\"ping\",\"request_id\":1}\n";
    static const char register_request[] =
        "{\"type\":\"register\",\"request_id\":101,"
        "\"username\":\"tcp_user\",\"password\":\"123456\"}\n";
    static const char duplicate_request[] =
        "{\"type\":\"register\",\"request_id\":102,"
        "\"username\":\"tcp_user\",\"password\":\"123456\"}\n";
    static const char login_request[] =
        "{\"type\":\"login\",\"request_id\":103,"
        "\"username\":\"tcp_user\",\"password\":\"123456\"}\n";
    static const char wrong_password_request[] =
        "{\"type\":\"login\",\"request_id\":104,"
        "\"username\":\"tcp_user\",\"password\":\"wrong\"}\n";
    static const char invalid_json[] = "{not-json}\n";
    static const char second_login[] =
        "{\"type\":\"login\",\"request_id\":105,"
        "\"username\":\"tcp_user\",\"password\":\"123456\"}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length;
    cJSON *root;
    cJSON *type;
    cJSON *request_id;
    int64_t registered_user_id;
    int64_t login_user_id;

    CHECK(send_frame(
              ping,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("ping response: %s", response);
    root = cJSON_Parse(response);
    CHECK(root != NULL);
    type = cJSON_GetObjectItemCaseSensitive(root, "type");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsString(type));
    CHECK(strcmp(type->valuestring, "pong") == 0);
    CHECK(cJSON_IsNumber(request_id));
    CHECK(request_id->valuedouble == 1.0);
    cJSON_Delete(root);

    CHECK(send_frame(
              register_request,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("register response: %s", response);
    registered_user_id = check_business_response(response, 1, 101U, NULL);

    CHECK(send_frame(
              duplicate_request,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("duplicate response: %s", response);
    check_business_response(response, 0, 102U, "USERNAME_EXISTS");

    CHECK(send_frame(
              login_request,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("login response: %s", response);
    login_user_id = check_business_response(response, 1, 103U, NULL);
    CHECK(login_user_id == registered_user_id);

    CHECK(send_frame(
              wrong_password_request,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("wrong-password response: %s", response);
    check_business_response(response, 0, 104U, "INVALID_PASSWORD");

    CHECK(send_frame(
              invalid_json,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("invalid-json response: %s", response);
    check_business_response(response, 0, 0U, "INVALID_JSON");

    CHECK(send_frame(
              second_login,
              response,
              sizeof(response),
              &response_length) == 0);
    printf("recovery response: %s", response);
    CHECK(check_business_response(response, 1, 105U, NULL) == registered_user_id);
}

int main(void)
{
    pid_t server_process;

    (void)remove(TEST_DATABASE_PATH);
    if (clinic_net_startup() != 0)
    {
        fprintf(stderr, "network startup failed\n");
        return 1;
    }

    server_process = start_server();
    CHECK(server_process > 0);
    if (server_process > 0 && wait_for_server() == 0)
    {
        test_tcp_flows();
    }
    else
    {
        CHECK(0);
    }

    stop_server(server_process);
    clinic_net_cleanup();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d TCP auth test(s) failed\n", failures);
        return 1;
    }
    puts("TCP auth tests passed");
    return 0;
}
