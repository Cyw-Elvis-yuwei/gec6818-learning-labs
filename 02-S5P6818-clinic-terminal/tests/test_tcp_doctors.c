#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clinic_net.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEST_DATABASE_PATH "/tmp/clinic_tcp_doctors_test.db"
#define SERVER_PATH "./build/linux/clinic_server"
#define DOCTOR_CLIENT_PATH "./build/linux/clinic_doctor_client"
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT "19012"
#define FAKE_SERVER_PORT "19001"

typedef struct ExpectedDoctor
{
    int64_t id;
    int64_t department_id;
    const char *name;
    const char *title;
    const char *specialty;
} ExpectedDoctor;

static const ExpectedDoctor EXPECTED_DOCTORS[] = {
    {1, 1, "张医生", "主任医师", "心血管内科"},
    {2, 1, "李医生", "副主任医师", "呼吸内科"},
    {3, 2, "王医生", "主任医师", "普通外科"},
    {4, 3, "赵医生", "主治医师", "儿科常见病"},
    {5, 4, "陈医生", "副主任医师", "眼科"},
    {6, 5, "刘医生", "主治医师", "口腔科"}};

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

static int wait_for_server(pid_t process_id, int *process_reaped)
{
    unsigned int attempt;

    if (process_reaped == NULL)
    {
        return -1;
    }
    *process_reaped = 0;
    for (attempt = 0U; attempt < 50U; ++attempt)
    {
        clinic_socket_t socket_fd;
        int process_status;
        pid_t wait_result = waitpid(process_id, &process_status, WNOHANG);

        if (wait_result == process_id)
        {
            *process_reaped = 1;
            return -1;
        }
        if (wait_result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == ECHILD)
            {
                *process_reaped = 1;
            }
            return -1;
        }

        if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) == 0)
        {
            clinic_socket_close(socket_fd);
            return 0;
        }
        sleep_milliseconds(100L);
    }
    return -1;
}

static int server_port_is_free(void)
{
    clinic_socket_t socket_fd;

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) == 0)
    {
        clinic_socket_close(socket_fd);
        return 0;
    }
    return 1;
}

static void stop_server(pid_t process_id)
{
    unsigned int attempt;
    int status;
    pid_t result;

    if (process_id <= 0)
    {
        return;
    }
    do
    {
        result = waitpid(process_id, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == process_id || (result < 0 && errno == ECHILD))
    {
        return;
    }
    if (result < 0)
    {
        ++failures;
        perror("waitpid before stopping server");
        return;
    }
    (void)kill(process_id, SIGTERM);
    for (attempt = 0U; attempt < 50U; ++attempt)
    {
        do
        {
            result = waitpid(process_id, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);

        if (result == process_id)
        {
            return;
        }
        if (result < 0)
        {
            if (errno == ECHILD)
            {
                return;
            }
            ++failures;
            perror("waitpid while stopping server");
            return;
        }
        sleep_milliseconds(100L);
    }
    (void)kill(process_id, SIGKILL);
    do
    {
        result = waitpid(process_id, &status, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0 && errno != ECHILD)
    {
        ++failures;
        perror("waitpid after killing server");
    }
    ++failures;
    fprintf(stderr, "FAIL: server required SIGKILL\n");
}

static int wait_for_socket_readable(
    clinic_socket_t socket_fd,
    int timeout_milliseconds)
{
    struct pollfd descriptor;
    int poll_result;

    descriptor.fd = socket_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    poll_result = poll(&descriptor, 1U, timeout_milliseconds);
    return poll_result > 0 && (descriptor.revents & POLLIN) != 0 ? 0 : -1;
}

static int wait_for_child_exit(
    pid_t process_id,
    int *exit_code,
    int *child_reaped)
{
    unsigned int attempt;
    int status;

    if (exit_code == NULL || child_reaped == NULL)
    {
        return -1;
    }
    *exit_code = -1;
    *child_reaped = 0;
    for (attempt = 0U; attempt < 80U; ++attempt)
    {
        pid_t result = waitpid(process_id, &status, WNOHANG);

        if (result == process_id)
        {
            *child_reaped = 1;
            if (WIFEXITED(status))
            {
                *exit_code = WEXITSTATUS(status);
                return 0;
            }
            return -1;
        }
        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == ECHILD)
            {
                *child_reaped = 1;
            }
            return -1;
        }
        sleep_milliseconds(100L);
    }
    (void)kill(process_id, SIGKILL);
    while (waitpid(process_id, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return -1;
        }
    }
    *child_reaped = 1;
    return -1;
}

static int receive_single_frame(
    clinic_socket_t socket_fd,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    size_t received_total = 0U;

    if (response == NULL || response_length == NULL || response_capacity < 2U)
    {
        return -1;
    }
    *response_length = 0U;
    response[0] = '\0';

    while (received_total + 1U < response_capacity)
    {
        if (wait_for_socket_readable(socket_fd, 5000) != 0)
        {
            response[0] = '\0';
            return -1;
        }
        int received = recv(
            socket_fd,
            response + received_total,
            (int)(response_capacity - received_total - 1U),
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
            size_t line_length = (size_t)(newline - response) + 1U;

            if (line_length != received_total)
            {
                response[0] = '\0';
                return -1;
            }
            *response_length = line_length;
            return 0;
        }
    }

    response[0] = '\0';
    return -1;
}

static int send_frame(
    const char *frame,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    clinic_socket_t socket_fd;
    int status;

    if (frame == NULL || response == NULL || response_length == NULL ||
        response_capacity < 2U)
    {
        return -1;
    }
    *response_length = 0U;
    response[0] = '\0';
    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) != 0)
    {
        return -1;
    }
    status = clinic_net_send_all(socket_fd, frame, strlen(frame));
    if (status == 0)
    {
        status = receive_single_frame(
            socket_fd,
            response,
            response_capacity,
            response_length);
    }
    clinic_socket_close(socket_fd);
    return status;
}

static int run_doctor_client_against_response(
    const char *request_id,
    const char *department_id,
    const char *response)
{
    clinic_socket_t listener = CLINIC_SOCKET_INVALID;
    clinic_socket_t client_socket = CLINIC_SOCKET_INVALID;
    pid_t process_id = -1;
    char request[512] = {0};
    char expected_request[512];
    size_t request_length = 0U;
    int exit_code = -1;
    int child_reaped = 0;
    int expected_length;

    if (request_id == NULL || department_id == NULL)
    {
        return -1;
    }
    expected_length = snprintf(
        expected_request,
        sizeof(expected_request),
        "{\"type\":\"list_doctors\",\"request_id\":%s,"
        "\"department_id\":%s}\n",
        request_id,
        department_id);
    if (expected_length < 0 ||
        (size_t)expected_length >= sizeof(expected_request))
    {
        return -1;
    }

    if (clinic_net_create_listener(
            SERVER_HOST,
            FAKE_SERVER_PORT,
            &listener) != 0)
    {
        return -1;
    }

    process_id = fork();
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
            DOCTOR_CLIENT_PATH,
            DOCTOR_CLIENT_PATH,
            SERVER_HOST,
            FAKE_SERVER_PORT,
            request_id,
            department_id,
            (char *)NULL);
        _exit(127);
    }
    if (process_id < 0 || wait_for_socket_readable(listener, 5000) != 0)
    {
        goto cleanup;
    }

    client_socket = accept(listener, NULL, NULL);
    if (client_socket == CLINIC_SOCKET_INVALID ||
        receive_single_frame(
            client_socket,
            request,
            sizeof(request),
            &request_length) != 0 ||
        request_length != (size_t)expected_length ||
        memcmp(request, expected_request, request_length) != 0 ||
        (response != NULL &&
         clinic_net_send_all(client_socket, response, strlen(response)) != 0))
    {
        goto cleanup;
    }

    if (response != NULL)
    {
        clinic_socket_close(client_socket);
        client_socket = CLINIC_SOCKET_INVALID;
        clinic_socket_close(listener);
        listener = CLINIC_SOCKET_INVALID;
    }
    (void)wait_for_child_exit(process_id, &exit_code, &child_reaped);

cleanup:
    if (client_socket != CLINIC_SOCKET_INVALID)
    {
        clinic_socket_close(client_socket);
    }
    if (listener != CLINIC_SOCKET_INVALID)
    {
        clinic_socket_close(listener);
    }
    if (process_id > 0 && !child_reaped)
    {
        int status;
        pid_t wait_result;

        do
        {
            wait_result = waitpid(process_id, &status, WNOHANG);
        } while (wait_result < 0 && errno == EINTR);
        if (wait_result == 0)
        {
            (void)kill(process_id, SIGKILL);
            do
            {
                wait_result = waitpid(process_id, &status, 0);
            } while (wait_result < 0 && errno == EINTR);
        }
    }
    return child_reaped ? exit_code : -1;
}

static int receive_two_frames(
    clinic_socket_t socket_fd,
    char *first_response,
    size_t first_capacity,
    size_t *first_length,
    char *second_response,
    size_t second_capacity,
    size_t *second_length)
{
    char combined[(CLINIC_MAX_FRAME_SIZE + 1U) * 2U + 1U];
    size_t combined_length = 0U;
    char *first_newline;
    char *second_newline;

    if (first_response == NULL || first_length == NULL ||
        second_response == NULL || second_length == NULL)
    {
        return -1;
    }
    *first_length = 0U;
    *second_length = 0U;
    first_response[0] = '\0';
    second_response[0] = '\0';

    for (;;)
    {
        first_newline = memchr(combined, '\n', combined_length);
        second_newline = first_newline == NULL
            ? NULL
            : memchr(
                  first_newline + 1,
                  '\n',
                  combined_length - (size_t)(first_newline + 1 - combined));
        if (second_newline != NULL)
        {
            break;
        }
        if (combined_length == sizeof(combined) - 1U)
        {
            return -1;
        }

        {
            if (wait_for_socket_readable(socket_fd, 5000) != 0)
            {
                return -1;
            }
            int received = recv(
                socket_fd,
                combined + combined_length,
                (int)(sizeof(combined) - combined_length - 1U),
                0);

            if (received <= 0)
            {
                return -1;
            }
            combined_length += (size_t)received;
        }
    }

    *first_length = (size_t)(first_newline - combined) + 1U;
    *second_length = (size_t)(second_newline - first_newline);
    if ((size_t)(second_newline - combined) + 1U != combined_length ||
        *first_length + 1U > first_capacity ||
        *second_length + 1U > second_capacity)
    {
        *first_length = 0U;
        *second_length = 0U;
        return -1;
    }
    memcpy(first_response, combined, *first_length);
    first_response[*first_length] = '\0';
    memcpy(second_response, first_newline + 1, *second_length);
    second_response[*second_length] = '\0';
    return 0;
}

static int expect_no_response(
    clinic_socket_t socket_fd,
    const char *fragment_name)
{
    struct pollfd descriptor;
    int poll_result;

    descriptor.fd = socket_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    poll_result = poll(&descriptor, 1U, 250);
    if (poll_result == 0)
    {
        return 0;
    }
    if (poll_result < 0)
    {
        perror("poll");
        return -1;
    }
    fprintf(
        stderr,
        "unexpected socket event after %s: revents=0x%x\n",
        fragment_name,
        (unsigned int)(unsigned short)descriptor.revents);
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
    const char *match;
    const char *end;

    if (written <= 0 || (size_t)written >= sizeof(expected))
    {
        return 0;
    }
    match = strstr(response, expected);
    if (match == NULL)
    {
        return 0;
    }
    end = match + (size_t)written;
    return *end == ',' || *end == '}';
}

static void check_doctor_item(
    const cJSON *doctor,
    const ExpectedDoctor *expected)
{
    cJSON *item;

    CHECK(cJSON_IsObject(doctor));
    item = cJSON_GetObjectItemCaseSensitive(doctor, "id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)expected->id);
    item = cJSON_GetObjectItemCaseSensitive(doctor, "department_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)expected->department_id);
    item = cJSON_GetObjectItemCaseSensitive(doctor, "name");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, expected->name) == 0);
    item = cJSON_GetObjectItemCaseSensitive(doctor, "title");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, expected->title) == 0);
    item = cJSON_GetObjectItemCaseSensitive(doctor, "specialty");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, expected->specialty) == 0);
}

static void check_doctors_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    size_t expected_start,
    size_t expected_count)
{
    cJSON *root;
    cJSON *request_id;
    cJSON *doctors;
    const char *parse_end = NULL;
    size_t index;

    CHECK(response != NULL);
    CHECK(response_length > 0U);
    if (response == NULL || response_length == 0U)
    {
        return;
    }
    CHECK(response[response_length - 1U] == '\n');
    CHECK(memchr(response, '\n', response_length - 1U) == NULL);

    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    CHECK(root != NULL);
    CHECK(parse_end == response + response_length);
    if (root == NULL || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return;
    }
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(request_id));
    CHECK(response_has_request_id(response, expected_request_id));
    doctors = cJSON_GetObjectItemCaseSensitive(root, "doctors");
    CHECK(cJSON_IsArray(doctors));
    CHECK(cJSON_GetArraySize(doctors) == (int)expected_count);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(response, "\"password\"") == NULL);

    for (index = 0U; index < expected_count; ++index)
    {
        CHECK(expected_start + index <
              sizeof(EXPECTED_DOCTORS) / sizeof(EXPECTED_DOCTORS[0]));
        if (expected_start + index <
            sizeof(EXPECTED_DOCTORS) / sizeof(EXPECTED_DOCTORS[0]))
        {
            check_doctor_item(
                cJSON_GetArrayItem(doctors, (int)index),
                &EXPECTED_DOCTORS[expected_start + index]);
        }
    }
    cJSON_Delete(root);
}

static void check_error_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    const char *expected_error_code)
{
    cJSON *root;
    cJSON *error_code;
    const char *parse_end = NULL;

    CHECK(response != NULL);
    CHECK(response_length > 0U);
    if (response == NULL || response_length == 0U)
    {
        return;
    }
    CHECK(response[response_length - 1U] == '\n');
    CHECK(memchr(response, '\n', response_length - 1U) == NULL);
    root = cJSON_ParseWithOpts(response, &parse_end, 1);
    CHECK(root != NULL);
    CHECK(parse_end == response + response_length);
    if (root == NULL || parse_end != response + response_length)
    {
        cJSON_Delete(root);
        return;
    }
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(cJSON_IsNumber(
        cJSON_GetObjectItemCaseSensitive(root, "request_id")));
    CHECK(response_has_request_id(response, expected_request_id));
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsString(error_code));
    CHECK(error_code != NULL &&
          strcmp(error_code->valuestring, expected_error_code) == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    cJSON_Delete(root);
}

static void test_independent_known_and_empty_departments(void)
{
    static const char department_one[] =
        "{\"type\":\"list_doctors\",\"request_id\":501,\"department_id\":1}\n";
    static const char department_two[] =
        "{\"type\":\"list_doctors\",\"request_id\":502,\"department_id\":2}\n";
    static const char unknown_department[] =
        "{\"type\":\"list_doctors\",\"request_id\":503,\"department_id\":999}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;

    CHECK(send_frame(
              department_one,
              response,
              sizeof(response),
              &response_length) == 0);
    check_doctors_response(response, response_length, 501U, 0U, 2U);

    CHECK(send_frame(
              department_two,
              response,
              sizeof(response),
              &response_length) == 0);
    check_doctors_response(response, response_length, 502U, 2U, 1U);

    CHECK(send_frame(
              unknown_department,
              response,
              sizeof(response),
              &response_length) == 0);
    check_doctors_response(response, response_length, 503U, 0U, 0U);
}

static void test_two_connections_can_remain_open_together(void)
{
    static const char first_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":520,\"department_id\":1}\n";
    static const char second_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":521,\"department_id\":2}\n";
    clinic_socket_t first_socket;
    clinic_socket_t second_socket;
    char first_response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    char second_response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t first_length = 0U;
    size_t second_length = 0U;
    int first_connected = 0;
    int second_connected = 0;

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &first_socket) == 0)
    {
        first_connected = 1;
    }
    CHECK(first_connected);
    if (first_connected &&
        clinic_net_connect(SERVER_HOST, SERVER_PORT, &second_socket) == 0)
    {
        second_connected = 1;
    }
    CHECK(second_connected);
    if (first_connected && second_connected)
    {
        CHECK(clinic_net_send_all(
                  first_socket,
                  first_request,
                  sizeof(first_request) - 1U) == 0);
        CHECK(clinic_net_send_all(
                  second_socket,
                  second_request,
                  sizeof(second_request) - 1U) == 0);
        CHECK(receive_single_frame(
                  first_socket,
                  first_response,
                  sizeof(first_response),
                  &first_length) == 0);
        CHECK(receive_single_frame(
                  second_socket,
                  second_response,
                  sizeof(second_response),
                  &second_length) == 0);
        check_doctors_response(first_response, first_length, 520U, 0U, 2U);
        check_doctors_response(second_response, second_length, 521U, 2U, 1U);
    }
    if (second_connected)
    {
        CHECK(clinic_socket_close(second_socket) == 0);
    }
    if (first_connected)
    {
        CHECK(clinic_socket_close(first_socket) == 0);
    }
}

static void test_uint64_max_request_id(void)
{
    static const char request[] =
        "{\"type\":\"list_doctors\","
        "\"request_id\":18446744073709551615,\"department_id\":1}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;

    CHECK(send_frame(request, response, sizeof(response), &response_length) == 0);
    check_doctors_response(response, response_length, UINT64_MAX, 0U, 2U);
    CHECK(strstr(
              response,
              "\"request_id\":18446744073709551615") != NULL);
}

static void test_doctor_client_validates_response_integers_and_top_level_id(void)
{
    static const char valid_spaced_response[] =
        "{\"ok\":true,\"request_id\" : 501,\"doctors\":[]}\n";
    static const char nested_mismatch_response[] =
        "{\"ok\":true,\"request_id\":999,"
        "\"meta\":{\"request_id\":501},\"doctors\":[]}\n";
    static const char duplicate_request_id_response[] =
        "{\"ok\":true,\"request_id\":501,"
        "\"request_id\":501,\"doctors\":[]}\n";
    static const char fractional_doctor_id_response[] =
        "{\"ok\":true,\"request_id\":501,\"doctors\":[{"
        "\"id\":1.5,\"department_id\":1,\"name\":\"A\","
        "\"title\":\"B\",\"specialty\":\"C\"}]}\n";
    static const char out_of_range_department_id_response[] =
        "{\"ok\":true,\"request_id\":501,\"doctors\":[{"
        "\"id\":1,\"department_id\":9223372036854775808,"
        "\"name\":\"A\",\"title\":\"B\",\"specialty\":\"C\"}]}\n";
    static const char maximum_int64_response[] =
        "{\"ok\":true,"
        "\"request_id\" : 18446744073709551615,\"doctors\":[{"
        "\"id\":9223372036854775807,"
        "\"department_id\":9223372036854775807,"
        "\"name\":\"A\",\"title\":\"B\",\"specialty\":\"C\"}]}\n";
    struct timespec stall_started;
    struct timespec stall_finished;
    int stall_exit_code;
    int64_t stall_elapsed_milliseconds;

    CHECK(run_doctor_client_against_response(
              "501",
              "1",
              valid_spaced_response) == 0);
    CHECK(run_doctor_client_against_response(
              "501",
              "1",
              nested_mismatch_response) == EXIT_FAILURE);
    CHECK(run_doctor_client_against_response(
              "501",
              "1",
              duplicate_request_id_response) == EXIT_FAILURE);
    CHECK(run_doctor_client_against_response(
              "501",
              "1",
              fractional_doctor_id_response) == EXIT_FAILURE);
    CHECK(run_doctor_client_against_response(
              "501",
              "1",
              out_of_range_department_id_response) == EXIT_FAILURE);
    CHECK(run_doctor_client_against_response(
              "18446744073709551615",
              "9223372036854775807",
              maximum_int64_response) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &stall_started) == 0);
    stall_exit_code = run_doctor_client_against_response("501", "1", NULL);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &stall_finished) == 0);
    stall_elapsed_milliseconds =
        ((int64_t)stall_finished.tv_sec - (int64_t)stall_started.tv_sec) *
            INT64_C(1000) +
        (stall_finished.tv_nsec - stall_started.tv_nsec) / 1000000L;
    CHECK(stall_exit_code == EXIT_FAILURE);
    CHECK(stall_elapsed_milliseconds >= INT64_C(4000));
}

static void test_invalid_unknown_and_field_errors_recover(void)
{
    static const char invalid_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":504,\"department_id\":1\n";
    static const char invalid_fields[] =
        "{\"type\":\"list_doctors\",\"request_id\":505,\"department_id\":0}\n";
    static const char unknown_request[] =
        "{\"type\":\"future_query\",\"request_id\":506}\n";
    static const char recovery_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":507,\"department_id\":2}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;

    CHECK(send_frame(
              invalid_request,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, response_length, 0U, "INVALID_JSON");

    CHECK(send_frame(
              recovery_request,
              response,
              sizeof(response),
              &response_length) == 0);
    check_doctors_response(response, response_length, 507U, 2U, 1U);

    CHECK(send_frame(
              invalid_fields,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, response_length, 505U, "INVALID_REQUEST");

    CHECK(send_frame(
              unknown_request,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, response_length, 506U, "UNKNOWN_REQUEST");

    CHECK(send_frame(
              recovery_request,
              response,
              sizeof(response),
              &response_length) == 0);
    check_doctors_response(response, response_length, 507U, 2U, 1U);
}

static void test_two_doctor_requests_in_one_send(void)
{
    static const char first_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":601,\"department_id\":1}\n";
    static const char second_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":602,\"department_id\":2}\n";
    char packed_requests[sizeof(first_request) + sizeof(second_request) - 1U];
    char first_response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    char second_response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t first_length = sizeof(first_request) - 1U;
    size_t second_length = sizeof(second_request) - 1U;
    size_t first_response_length = 0U;
    size_t second_response_length = 0U;
    clinic_socket_t socket_fd;

    memcpy(packed_requests, first_request, first_length);
    memcpy(
        packed_requests + first_length,
        second_request,
        second_length + 1U);
    CHECK(strlen(packed_requests) == first_length + second_length);

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) != 0)
    {
        CHECK(0);
        return;
    }
    CHECK(clinic_net_send_all(
              socket_fd,
              packed_requests,
              first_length + second_length) == 0);
    CHECK(receive_two_frames(
              socket_fd,
              first_response,
              sizeof(first_response),
              &first_response_length,
              second_response,
              sizeof(second_response),
              &second_response_length) == 0);
    CHECK(expect_no_response(socket_fd, "two doctor responses") == 0);
    clinic_socket_close(socket_fd);

    if (first_response_length > 0U && second_response_length > 0U)
    {
        check_doctors_response(
            first_response,
            first_response_length,
            601U,
            0U,
            2U);
        check_doctors_response(
            second_response,
            second_response_length,
            602U,
            2U,
            1U);
    }
}

static void test_doctor_request_split_across_three_sends(void)
{
    static const char full_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":603,\"department_id\":1}\n";
    static const char first_fragment[] = "{\"type\":\"list_";
    static const char second_fragment[] = "doctors\",\"request_id\":603,";
    static const char third_fragment[] = "\"department_id\":1}\n";
    size_t first_length = sizeof(first_fragment) - 1U;
    size_t second_length = sizeof(second_fragment) - 1U;
    size_t third_length = sizeof(third_fragment) - 1U;
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;
    clinic_socket_t socket_fd;
    int status;

    CHECK(first_length + second_length + third_length ==
          sizeof(full_request) - 1U);
    CHECK(memcmp(full_request, first_fragment, first_length) == 0);
    CHECK(memcmp(
              full_request + first_length,
              second_fragment,
              second_length) == 0);
    CHECK(memcmp(
              full_request + first_length + second_length,
              third_fragment,
              third_length) == 0);

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) != 0)
    {
        CHECK(0);
        return;
    }
    status = clinic_net_send_all(socket_fd, first_fragment, first_length);
    CHECK(status == 0);
    if (status == 0)
    {
        status = expect_no_response(socket_fd, "first doctor fragment");
        CHECK(status == 0);
    }
    if (status == 0)
    {
        status = clinic_net_send_all(
            socket_fd,
            second_fragment,
            second_length);
        CHECK(status == 0);
    }
    if (status == 0)
    {
        status = expect_no_response(socket_fd, "second doctor fragment");
        CHECK(status == 0);
    }
    if (status == 0)
    {
        status = clinic_net_send_all(socket_fd, third_fragment, third_length);
        CHECK(status == 0);
    }
    if (status == 0)
    {
        status = receive_single_frame(
            socket_fd,
            response,
            sizeof(response),
            &response_length);
        CHECK(status == 0);
    }
    if (status == 0)
    {
        check_doctors_response(response, response_length, 603U, 0U, 2U);
        CHECK(expect_no_response(socket_fd, "completed doctor response") == 0);
    }
    clinic_socket_close(socket_fd);
}

static void test_incomplete_doctor_disconnect_and_new_connection(
    pid_t *server_process)
{
    static const char incomplete_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":604,\"department_id\":";
    static const char recovery_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":605,\"department_id\":2}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;
    clinic_socket_t first_socket;
    clinic_socket_t second_socket;
    int status;
    int shutdown_result;
    int shutdown_error = 0;
    int close_result;
    int server_status;
    pid_t wait_result;

    CHECK(server_process != NULL);
    if (server_process == NULL || *server_process <= 0)
    {
        return;
    }
    CHECK(memchr(
              incomplete_request,
              '\n',
              sizeof(incomplete_request) - 1U) == NULL);

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &first_socket) != 0)
    {
        CHECK(0);
        return;
    }
    status = clinic_net_send_all(
        first_socket,
        incomplete_request,
        sizeof(incomplete_request) - 1U);
    CHECK(status == 0);
    if (status == 0)
    {
        status = expect_no_response(first_socket, "incomplete doctor request");
        CHECK(status == 0);
    }

    shutdown_result = shutdown(first_socket, SHUT_RDWR);
    if (shutdown_result != 0)
    {
        shutdown_error = errno;
    }
    CHECK(shutdown_result == 0 || shutdown_error == ENOTCONN);
    close_result = clinic_socket_close(first_socket);
    CHECK(close_result == 0);
    if (status != 0 || close_result != 0)
    {
        return;
    }

    sleep_milliseconds(100L);
    wait_result = waitpid(*server_process, &server_status, WNOHANG);
    CHECK(wait_result == 0);
    if (wait_result != 0)
    {
        if (wait_result == *server_process)
        {
            fprintf(
                stderr,
                "server exited after incomplete doctor disconnect: status=%d\n",
                server_status);
            *server_process = -1;
        }
        else
        {
            perror("waitpid server after doctor disconnect");
        }
        return;
    }

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &second_socket) != 0)
    {
        CHECK(0);
        return;
    }
    status = clinic_net_send_all(
        second_socket,
        recovery_request,
        sizeof(recovery_request) - 1U);
    CHECK(status == 0);
    if (status == 0)
    {
        status = receive_single_frame(
            second_socket,
            response,
            sizeof(response),
            &response_length);
        CHECK(status == 0);
    }
    if (status == 0)
    {
        CHECK(strstr(response, "\"request_id\":604") == NULL);
        check_doctors_response(response, response_length, 605U, 2U, 1U);
        CHECK(expect_no_response(
                  second_socket,
                  "doctor disconnect recovery response") == 0);
    }
    CHECK(clinic_socket_close(second_socket) == 0);
}

static void test_ping_auth_and_department_regressions(void)
{
    static const char ping_request[] =
        "{\"type\":\"ping\",\"request_id\":701}\n";
    static const char register_request[] =
        "{\"type\":\"register\",\"request_id\":702,"
        "\"username\":\"tcp-doctor-user\","
        "\"password\":\"teaching-password\"}\n";
    static const char login_request[] =
        "{\"type\":\"login\",\"request_id\":703,"
        "\"username\":\"tcp-doctor-user\","
        "\"password\":\"teaching-password\"}\n";
    static const char wrong_password_request[] =
        "{\"type\":\"login\",\"request_id\":704,"
        "\"username\":\"tcp-doctor-user\","
        "\"password\":\"wrong-password\"}\n";
    static const char department_request[] =
        "{\"type\":\"list_departments\",\"request_id\":705}\n";
    static const char doctor_request[] =
        "{\"type\":\"list_doctors\",\"request_id\":706,\"department_id\":1}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;
    cJSON *root;
    cJSON *item;
    double registered_user_id = 0.0;

    CHECK(send_frame(
              ping_request,
              response,
              sizeof(response),
              &response_length) == 0);
    root = cJSON_Parse(response);
    CHECK(root != NULL);
    item = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "type");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "pong") == 0);
    CHECK(response_has_request_id(response, 701U));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(send_frame(
              register_request,
              response,
              sizeof(response),
              &response_length) == 0);
    root = cJSON_Parse(response);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(response_has_request_id(response, 702U));
    item = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsNumber(item));
    if (cJSON_IsNumber(item))
    {
        registered_user_id = item->valuedouble;
        CHECK(registered_user_id > 0.0);
    }
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    cJSON_Delete(root);

    CHECK(send_frame(
              login_request,
              response,
              sizeof(response),
              &response_length) == 0);
    root = cJSON_Parse(response);
    CHECK(root != NULL);
    CHECK(root == NULL ||
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(response_has_request_id(response, 703U));
    item = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == registered_user_id);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);

    CHECK(send_frame(
              wrong_password_request,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, response_length, 704U, "INVALID_PASSWORD");

    CHECK(send_frame(
              department_request,
              response,
              sizeof(response),
              &response_length) == 0);
    root = cJSON_Parse(response);
    CHECK(root != NULL);
    item = root == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(root, "departments");
    CHECK(cJSON_IsArray(item));
    CHECK(cJSON_GetArraySize(item) == 5);
    CHECK(response_has_request_id(response, 705U));
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(root == NULL ||
          cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    cJSON_Delete(root);

    CHECK(send_frame(
              doctor_request,
              response,
              sizeof(response),
              &response_length) == 0);
    check_doctors_response(response, response_length, 706U, 0U, 2U);
}

int main(void)
{
    pid_t server_process;
    int server_reaped = 0;

    (void)remove(TEST_DATABASE_PATH);
    (void)signal(SIGPIPE, SIG_IGN);
    if (clinic_net_startup() != 0)
    {
        fprintf(stderr, "network startup failed\n");
        return 1;
    }
    if (!server_port_is_free())
    {
        fprintf(stderr, "port %s is already in use\n", SERVER_PORT);
        clinic_net_cleanup();
        return 1;
    }

    server_process = start_server();
    CHECK(server_process > 0);
    if (server_process > 0 &&
        wait_for_server(server_process, &server_reaped) == 0)
    {
        test_independent_known_and_empty_departments();
        test_two_connections_can_remain_open_together();
        test_uint64_max_request_id();
        test_doctor_client_validates_response_integers_and_top_level_id();
        test_invalid_unknown_and_field_errors_recover();
        test_two_doctor_requests_in_one_send();
        test_doctor_request_split_across_three_sends();
        test_incomplete_doctor_disconnect_and_new_connection(&server_process);
        if (server_process > 0)
        {
            test_ping_auth_and_department_regressions();
        }
    }
    else
    {
        if (server_reaped)
        {
            server_process = -1;
        }
        CHECK(0);
    }

    stop_server(server_process);
    clinic_net_cleanup();
    (void)remove(TEST_DATABASE_PATH);

    if (failures != 0)
    {
        fprintf(stderr, "%d TCP doctor test(s) failed\n", failures);
        return 1;
    }
    puts("TCP doctor tests passed");
    return 0;
}
