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

#define TEST_DATABASE_PATH "/tmp/clinic_tcp_departments_test.db"
#define SERVER_PATH "./build/linux/clinic_server"
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT "19011"

static const char *const expected_department_names[] = {
    "内科",
    "外科",
    "儿科",
    "眼科",
    "口腔科"};

static int failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
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
    response[0] = '\0';
    return -1;
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
    if (*first_length + 1U > first_capacity ||
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
    char unexpected[512];
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
    if ((descriptor.revents & POLLIN) != 0)
    {
        int received = recv(
            socket_fd,
            unexpected,
            (int)sizeof(unexpected),
            MSG_DONTWAIT);

        if (received > 0)
        {
            fprintf(stderr, "unexpected response bytes: ");
            (void)fwrite(unexpected, 1U, (size_t)received, stderr);
            fputc('\n', stderr);
        }
    }
    return -1;
}

static int receive_single_frame(
    clinic_socket_t socket_fd,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    size_t received_total = 0U;

    if (response == NULL || response_length == NULL ||
        response_capacity < 2U)
    {
        return -1;
    }
    *response_length = 0U;
    response[0] = '\0';

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
                fprintf(
                    stderr,
                    "unexpected bytes after completed response: %s\n",
                    newline + 1);
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

static void check_departments_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id)
{
    cJSON *root;
    cJSON *ok;
    cJSON *request_id;
    cJSON *departments;
    const char *parse_end = NULL;
    int index;

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
        return;
    }
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    departments = cJSON_GetObjectItemCaseSensitive(root, "departments");

    CHECK(cJSON_IsTrue(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(response_has_request_id(response, expected_request_id));
    if (cJSON_IsNumber(request_id))
    {
        CHECK(request_id->valuedouble == (double)expected_request_id);
    }
    CHECK(cJSON_IsArray(departments));
    CHECK(cJSON_GetArraySize(departments) == 5);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "user_id") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(response, "\"password\"") == NULL);

    for (index = 0; index < 5; ++index)
    {
        cJSON *department = cJSON_GetArrayItem(departments, index);
        cJSON *id = cJSON_GetObjectItemCaseSensitive(department, "id");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(department, "name");

        CHECK(cJSON_IsObject(department));
        CHECK(cJSON_IsNumber(id));
        if (cJSON_IsNumber(id))
        {
            CHECK(id->valuedouble == (double)(index + 1));
        }
        CHECK(cJSON_IsString(name));
        if (cJSON_IsString(name))
        {
            CHECK(strcmp(name->valuestring, expected_department_names[index]) == 0);
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
    cJSON *ok;
    cJSON *request_id;
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
    ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    error_code = cJSON_GetObjectItemCaseSensitive(root, "error_code");

    CHECK(cJSON_IsFalse(ok));
    CHECK(cJSON_IsNumber(request_id));
    CHECK(response_has_request_id(response, expected_request_id));
    CHECK(cJSON_IsString(error_code));
    if (cJSON_IsString(error_code))
    {
        if (strcmp(error_code->valuestring, expected_error_code) != 0)
        {
            fprintf(
                stderr,
                "expected error_code %s, got %s: %s",
                expected_error_code,
                error_code->valuestring,
                response);
        }
        CHECK(strcmp(error_code->valuestring, expected_error_code) == 0);
    }
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(strstr(response, "\"password\"") == NULL);
    cJSON_Delete(root);
}

static void test_first_independent_connection(void)
{
    static const char request[] =
        "{\"type\":\"list_departments\",\"request_id\":201}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;

    CHECK(send_frame(request, response, sizeof(response), &response_length) == 0);
    if (response_length > 0U)
    {
        check_departments_response(response, response_length, 201U);
    }
}

static void test_second_independent_connection(void)
{
    static const char request[] =
        "{\"type\":\"list_departments\",\"request_id\":202}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;

    CHECK(send_frame(request, response, sizeof(response), &response_length) == 0);
    if (response_length > 0U)
    {
        check_departments_response(response, response_length, 202U);
    }
}

static void test_uint64_max_request_id(void)
{
    static const char request[] =
        "{\"type\":\"list_departments\","
        "\"request_id\":18446744073709551615}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;

    CHECK(send_frame(request, response, sizeof(response), &response_length) == 0);
    if (response_length > 0U)
    {
        check_departments_response(response, response_length, UINT64_MAX);
        CHECK(strstr(
                  response,
                  "\"request_id\":18446744073709551615") != NULL);
    }
}

static void test_invalid_json_and_recovery(void)
{
    static const char invalid_request[] =
        "{\"type\":\"list_departments\",\"request_id\":203\n";
    static const char recovery_request[] =
        "{\"type\":\"list_departments\",\"request_id\":204}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;

    CHECK(send_frame(
              invalid_request,
              response,
              sizeof(response),
              &response_length) == 0);
    if (response_length > 0U)
    {
        check_error_response(response, response_length, 0U, "INVALID_JSON");
    }

    CHECK(send_frame(
              recovery_request,
              response,
              sizeof(response),
              &response_length) == 0);
    if (response_length > 0U)
    {
        check_departments_response(response, response_length, 204U);
    }
}

static void test_unknown_request_and_recovery(void)
{
    static const char unknown_request[] =
        "{\"type\":\"unknown_operation\",\"request_id\":205}\n";
    static const char recovery_request[] =
        "{\"type\":\"list_departments\",\"request_id\":209}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;

    CHECK(send_frame(
              unknown_request,
              response,
              sizeof(response),
              &response_length) == 0);
    if (response_length > 0U)
    {
        check_error_response(response, response_length, 205U, "UNKNOWN_REQUEST");
    }

    CHECK(send_frame(
              recovery_request,
              response,
              sizeof(response),
              &response_length) == 0);
    if (response_length > 0U)
    {
        check_departments_response(response, response_length, 209U);
    }
}

static void test_two_requests_in_one_send(void)
{
    static const char first_request[] =
        "{\"type\":\"list_departments\",\"request_id\":301}\n";
    static const char second_request[] =
        "{\"type\":\"list_departments\",\"request_id\":302}\n";
    char packed_requests[
        sizeof(first_request) + sizeof(second_request) - 1U];
    char first_response[CLINIC_MAX_FRAME_SIZE + 1U];
    char second_response[CLINIC_MAX_FRAME_SIZE + 1U];
    const size_t first_request_length = sizeof(first_request) - 1U;
    const size_t second_request_length = sizeof(second_request) - 1U;
    const size_t packed_length =
        first_request_length + second_request_length;
    size_t first_response_length = 0U;
    size_t second_response_length = 0U;
    clinic_socket_t socket_fd;

    memcpy(packed_requests, first_request, first_request_length);
    memcpy(
        packed_requests + first_request_length,
        second_request,
        second_request_length + 1U);
    CHECK(packed_length == strlen(packed_requests));

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) != 0)
    {
        CHECK(0);
        return;
    }

    CHECK(clinic_net_send_all(
              socket_fd,
              packed_requests,
              packed_length) == 0);
    CHECK(receive_two_frames(
              socket_fd,
              first_response,
              sizeof(first_response),
              &first_response_length,
              second_response,
              sizeof(second_response),
              &second_response_length) == 0);
    clinic_socket_close(socket_fd);

    if (first_response_length > 0U && second_response_length > 0U)
    {
        printf("packed response 301: %s", first_response);
        printf("packed response 302: %s", second_response);
        check_departments_response(
            first_response,
            first_response_length,
            301U);
        check_departments_response(
            second_response,
            second_response_length,
            302U);
    }
}

static void test_request_split_across_three_sends(void)
{
    static const char full_request[] =
        "{\"type\":\"list_departments\",\"request_id\":303}\n";
    static const char first_fragment[] = "{\"type\":\"list_";
    static const char second_fragment[] = "departments\",\"request_id\":";
    static const char third_fragment[] = "303}\n";
    const size_t first_length = sizeof(first_fragment) - 1U;
    const size_t second_length = sizeof(second_fragment) - 1U;
    const size_t third_length = sizeof(third_fragment) - 1U;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;
    clinic_socket_t socket_fd;
    int status;

    CHECK(first_length == strlen(first_fragment));
    CHECK(second_length == strlen(second_fragment));
    CHECK(third_length == strlen(third_fragment));
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
    CHECK(memchr(first_fragment, '\n', first_length) == NULL);
    CHECK(memchr(second_fragment, '\n', second_length) == NULL);
    CHECK(memchr(third_fragment, '\n', third_length - 1U) == NULL);
    CHECK(third_fragment[third_length - 1U] == '\n');

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_fd) != 0)
    {
        CHECK(0);
        return;
    }

    status = clinic_net_send_all(
        socket_fd,
        full_request,
        first_length);
    CHECK(status == 0);
    if (status != 0)
    {
        clinic_socket_close(socket_fd);
        return;
    }
    status = expect_no_response(socket_fd, "first fragment");
    CHECK(status == 0);
    if (status != 0)
    {
        clinic_socket_close(socket_fd);
        return;
    }

    status = clinic_net_send_all(
        socket_fd,
        full_request + first_length,
        second_length);
    CHECK(status == 0);
    if (status != 0)
    {
        clinic_socket_close(socket_fd);
        return;
    }
    status = expect_no_response(socket_fd, "second fragment");
    CHECK(status == 0);
    if (status != 0)
    {
        clinic_socket_close(socket_fd);
        return;
    }

    status = clinic_net_send_all(
        socket_fd,
        full_request + first_length + second_length,
        third_length);
    CHECK(status == 0);
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
        printf("partial response 303: %s", response);
        check_departments_response(response, response_length, 303U);
        CHECK(expect_no_response(
                  socket_fd,
                  "completed response") == 0);
    }
    clinic_socket_close(socket_fd);
}

static void test_incomplete_disconnect_and_new_connection(
    pid_t *server_process)
{
    static const char incomplete_request[] =
        "{\"type\":\"list_departments\",\"request_id\":304";
    static const char recovery_request[] =
        "{\"type\":\"list_departments\",\"request_id\":305}\n";
    const size_t incomplete_length = sizeof(incomplete_request) - 1U;
    const size_t recovery_length = sizeof(recovery_request) - 1U;
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;
    clinic_socket_t socket_a;
    clinic_socket_t socket_b;
    int socket_a_number;
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
    CHECK(incomplete_length == strlen(incomplete_request));
    CHECK(memchr(incomplete_request, '}', incomplete_length) == NULL);
    CHECK(memchr(incomplete_request, '\n', incomplete_length) == NULL);
    CHECK(recovery_length == strlen(recovery_request));

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_a) != 0)
    {
        CHECK(0);
        return;
    }
    socket_a_number = (int)socket_a;

    status = clinic_net_send_all(
        socket_a,
        incomplete_request,
        incomplete_length);
    CHECK(status == 0);
    if (status == 0)
    {
        status = expect_no_response(socket_a, "incomplete request");
        CHECK(status == 0);
    }

    shutdown_result = shutdown(socket_a, SHUT_RDWR);
    if (shutdown_result != 0)
    {
        shutdown_error = errno;
    }
    CHECK(shutdown_result == 0 || shutdown_error == ENOTCONN);
    if (shutdown_result != 0 && shutdown_error != ENOTCONN)
    {
        errno = shutdown_error;
        perror("shutdown connection A");
    }
    close_result = clinic_socket_close(socket_a);
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
                "server exited after incomplete disconnect: status=%d\n",
                server_status);
            *server_process = -1;
        }
        else
        {
            perror("waitpid server after incomplete disconnect");
        }
        return;
    }
    printf(
        "server alive after incomplete disconnect: pid=%ld\n",
        (long)*server_process);

    if (clinic_net_connect(SERVER_HOST, SERVER_PORT, &socket_b) != 0)
    {
        CHECK(0);
        return;
    }
    printf(
        "disconnect recovery sockets: A=%d B=%d (B is a new connect)\n",
        socket_a_number,
        (int)socket_b);

    status = clinic_net_send_all(
        socket_b,
        recovery_request,
        recovery_length);
    CHECK(status == 0);
    if (status == 0)
    {
        status = receive_single_frame(
            socket_b,
            response,
            sizeof(response),
            &response_length);
        CHECK(status == 0);
    }
    if (status == 0)
    {
        printf("disconnect recovery response 305: %s", response);
        CHECK(strstr(response, "\"request_id\":304") == NULL);
        check_departments_response(response, response_length, 305U);
        CHECK(expect_no_response(
                  socket_b,
                  "disconnect recovery response") == 0);
    }
    CHECK(clinic_socket_close(socket_b) == 0);
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
        test_first_independent_connection();
        test_second_independent_connection();
        test_uint64_max_request_id();
        test_invalid_json_and_recovery();
        test_unknown_request_and_recovery();
        test_two_requests_in_one_send();
        test_request_split_across_three_sends();
        test_incomplete_disconnect_and_new_connection(&server_process);
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
        fprintf(stderr, "%d TCP department test(s) failed\n", failures);
        return 1;
    }
    puts("TCP department tests passed");
    return 0;
}
