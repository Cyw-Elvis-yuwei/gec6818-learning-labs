#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clinic_net.h"
#include "clinic_protocol.h"

#include <cjson/cJSON.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
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

#define TEST_DATABASE_PATH "/tmp/clinic_tcp_ticket_test.db"
#define SERVER_PATH "./build/linux/clinic_server"
#define TICKET_CLIENT_PATH "./build/linux/clinic_ticket_client"
#define TICKET_STATUS_CLIENT_PATH "./build/linux/clinic_ticket_status_client"
#define ADMIN_CALL_CLIENT_PATH "./build/linux/clinic_admin_call_client"
#define CURRENT_TICKET_CLIENT_PATH "./build/linux/host_current_ticket_client"
#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT "19013"
#define RESPONSE_TIMEOUT_MILLISECONDS 5000
#define CLIENT_PROCESS_TIMEOUT_MILLISECONDS 7000

typedef struct ObservedTicket
{
    int64_t id;
    int64_t user_id;
    int64_t department_id;
    int64_t queue_number;
    char status[16];
    char service_date[11];
    int64_t created_time;
    int64_t called_time;
    int called_time_is_null;
} ObservedTicket;

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
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000L;
    delay.tv_nsec = (milliseconds % 1000L) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
}

static int server_port_is_free(void)
{
    clinic_socket_t listener;

    if (clinic_net_create_listener(SERVER_HOST, SERVER_PORT, &listener) != 0)
    {
        return 0;
    }
    clinic_socket_close(listener);
    return 1;
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
        int status;
        pid_t result = waitpid(process_id, &status, WNOHANG);

        if (result == process_id)
        {
            *process_reaped = 1;
            return -1;
        }
        if (result < 0 && errno != EINTR)
        {
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

        if (result == process_id || (result < 0 && errno == ECHILD))
        {
            return;
        }
        sleep_milliseconds(100L);
    }
    (void)kill(process_id, SIGKILL);
    while (waitpid(process_id, &status, 0) < 0 && errno == EINTR)
    {
    }
    ++failures;
    fprintf(stderr, "FAIL: server required SIGKILL\n");
}

static int wait_for_readable(clinic_socket_t socket_fd)
{
    struct pollfd descriptor;
    int status;

    descriptor.fd = socket_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    do
    {
        status = poll(&descriptor, 1U, RESPONSE_TIMEOUT_MILLISECONDS);
    } while (status < 0 && errno == EINTR);
    return status > 0 &&
        (descriptor.revents & (POLLIN | POLLHUP)) != 0 ? 0 : -1;
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
        response_capacity < CLINIC_MAX_FRAME_SIZE + 1U)
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

    while (received_total < CLINIC_MAX_FRAME_SIZE)
    {
        int received;
        char *newline;

        if (wait_for_readable(socket_fd) != 0)
        {
            clinic_socket_close(socket_fd);
            return -1;
        }
        received = recv(
            socket_fd,
            response + received_total,
            (int)(CLINIC_MAX_FRAME_SIZE - received_total),
            0);
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

static int deadline_milliseconds_remaining(const struct timespec *deadline)
{
    struct timespec now;
    int64_t seconds;
    long nanoseconds;
    int64_t milliseconds;

    if (deadline == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return -1;
    }
    seconds = (int64_t)deadline->tv_sec - (int64_t)now.tv_sec;
    nanoseconds = deadline->tv_nsec - now.tv_nsec;
    if (nanoseconds < 0L)
    {
        --seconds;
        nanoseconds += 1000000000L;
    }
    if (seconds < 0)
    {
        return 0;
    }
    milliseconds = seconds * INT64_C(1000) +
        (nanoseconds + 999999L) / 1000000L;
    if (milliseconds <= 0)
    {
        return 0;
    }
    return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

static int run_client_process(
    const char *client_path,
    const char *request_id,
    const char *primary_id,
    const char *secondary_id,
    char *output,
    size_t output_capacity,
    size_t *output_length)
{
    int descriptors[2];
    pid_t process_id;
    size_t received_total = 0U;
    struct timespec deadline;
    int status = 0;
    int descriptor_open = 0;
    int child_reaped = 0;

    if (client_path == NULL || request_id == NULL || primary_id == NULL ||
        output == NULL || output_length == NULL || output_capacity == 0U ||
        pipe(descriptors) != 0)
    {
        return -1;
    }
    *output_length = 0U;
    output[0] = '\0';
    process_id = fork();
    if (process_id == 0)
    {
        int null_fd;

        (void)close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0)
        {
            _exit(127);
        }
        (void)close(descriptors[1]);
        null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0)
        {
            (void)dup2(null_fd, STDERR_FILENO);
            (void)close(null_fd);
        }
        if (secondary_id != NULL)
        {
            execl(
                client_path,
                client_path,
                SERVER_HOST,
                SERVER_PORT,
                request_id,
                primary_id,
                secondary_id,
                (char *)NULL);
        }
        else
        {
            execl(
                client_path,
                client_path,
                SERVER_HOST,
                SERVER_PORT,
                request_id,
                primary_id,
                (char *)NULL);
        }
        _exit(127);
    }
    (void)close(descriptors[1]);
    if (process_id < 0)
    {
        (void)close(descriptors[0]);
        return -1;
    }
    descriptor_open = 1;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
    {
        goto terminate_child;
    }
    deadline.tv_sec += CLIENT_PROCESS_TIMEOUT_MILLISECONDS / 1000;
    deadline.tv_nsec +=
        (CLIENT_PROCESS_TIMEOUT_MILLISECONDS % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }

    for (;;)
    {
        struct pollfd descriptor;
        int timeout_milliseconds;
        int poll_status;
        ssize_t received;

        if (received_total + 1U >= output_capacity)
        {
            goto terminate_child;
        }
        timeout_milliseconds = deadline_milliseconds_remaining(&deadline);
        if (timeout_milliseconds <= 0)
        {
            goto terminate_child;
        }
        descriptor.fd = descriptors[0];
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        do
        {
            poll_status = poll(&descriptor, 1U, timeout_milliseconds);
        } while (poll_status < 0 && errno == EINTR &&
                 (timeout_milliseconds =
                      deadline_milliseconds_remaining(&deadline)) > 0);
        if (poll_status <= 0 ||
            (descriptor.revents & (POLLERR | POLLNVAL)) != 0 ||
            (descriptor.revents & (POLLIN | POLLHUP)) == 0)
        {
            goto terminate_child;
        }
        received = read(
            descriptors[0],
            output + received_total,
            output_capacity - received_total - 1U);
        if (received > 0)
        {
            received_total += (size_t)received;
            continue;
        }
        if (received == 0)
        {
            break;
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            goto terminate_child;
        }
    }
    (void)close(descriptors[0]);
    descriptor_open = 0;
    output[received_total] = '\0';
    *output_length = received_total;

    while (!child_reaped)
    {
        pid_t wait_result = waitpid(process_id, &status, WNOHANG);

        if (wait_result == process_id)
        {
            child_reaped = 1;
            break;
        }
        if (wait_result < 0 && errno != EINTR)
        {
            goto terminate_child;
        }
        if (deadline_milliseconds_remaining(&deadline) <= 0)
        {
            goto terminate_child;
        }
        sleep_milliseconds(10L);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;

terminate_child:
    if (descriptor_open)
    {
        (void)close(descriptors[0]);
    }
    if (!child_reaped)
    {
        (void)kill(process_id, SIGKILL);
        while (waitpid(process_id, &status, 0) < 0 && errno == EINTR)
        {
        }
    }
    output[received_total] = '\0';
    *output_length = received_total;
    return -1;
}

static int64_t check_registration_response(
    const char *response,
    uint64_t expected_request_id)
{
    int64_t user_id = 0;
    cJSON *root = cJSON_Parse(response);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)expected_request_id);
    item = cJSON_GetObjectItemCaseSensitive(root, "user_id");
    CHECK(cJSON_IsNumber(item));
    if (cJSON_IsNumber(item))
    {
        user_id = (int64_t)item->valuedouble;
        CHECK(user_id > 0);
    }
    cJSON_Delete(root);
    return user_id;
}

static int decode_ticket_payload(
    const char *response,
    ObservedTicket *observed_ticket)
{
    cJSON *root;
    cJSON *ticket;
    cJSON *id;
    cJSON *user_id;
    cJSON *department_id;
    cJSON *queue_number;
    cJSON *status;
    cJSON *service_date;
    cJSON *created_time;
    cJSON *called_time;
    size_t status_length;
    size_t service_date_length;

    if (response == NULL || observed_ticket == NULL) {
        return -1;
    }

    memset(observed_ticket, 0, sizeof(*observed_ticket));
    root = cJSON_Parse(response);
    if (root == NULL) {
        return -1;
    }

    ticket = cJSON_GetObjectItemCaseSensitive(root, "ticket");
    id = cJSON_GetObjectItemCaseSensitive(ticket, "id");
    user_id = cJSON_GetObjectItemCaseSensitive(ticket, "user_id");
    department_id = cJSON_GetObjectItemCaseSensitive(ticket, "department_id");
    queue_number = cJSON_GetObjectItemCaseSensitive(ticket, "queue_number");
    status = cJSON_GetObjectItemCaseSensitive(ticket, "status");
    service_date = cJSON_GetObjectItemCaseSensitive(ticket, "service_date");
    created_time = cJSON_GetObjectItemCaseSensitive(ticket, "created_time");
    called_time = cJSON_GetObjectItemCaseSensitive(ticket, "called_time");

    if (!cJSON_IsObject(ticket) ||
        !cJSON_IsNumber(id) || id->valuedouble <= 0.0 ||
        !cJSON_IsNumber(user_id) || user_id->valuedouble <= 0.0 ||
        !cJSON_IsNumber(department_id) || department_id->valuedouble <= 0.0 ||
        !cJSON_IsNumber(queue_number) || queue_number->valuedouble <= 0.0 ||
        !cJSON_IsString(status) || status->valuestring == NULL ||
        !cJSON_IsString(service_date) || service_date->valuestring == NULL ||
        !cJSON_IsNumber(created_time) || created_time->valuedouble <= 0.0) {
        cJSON_Delete(root);
        return -1;
    }

    status_length = strlen(status->valuestring);
    service_date_length = strlen(service_date->valuestring);
    if (status_length >= sizeof(observed_ticket->status) ||
        service_date_length != sizeof(observed_ticket->service_date) - 1U) {
        cJSON_Delete(root);
        return -1;
    }
    if (strcmp(status->valuestring, "WAITING") == 0)
    {
        if (!cJSON_IsNull(called_time))
        {
            cJSON_Delete(root);
            return -1;
        }
        observed_ticket->called_time = 0;
        observed_ticket->called_time_is_null = 1;
    }
    else if (strcmp(status->valuestring, "CALLED") == 0)
    {
        if (!cJSON_IsNumber(called_time) || called_time->valuedouble <= 0.0 ||
            called_time->valuedouble >= 9223372036854775808.0)
        {
            cJSON_Delete(root);
            return -1;
        }
        observed_ticket->called_time = (int64_t)called_time->valuedouble;
        if ((double)observed_ticket->called_time != called_time->valuedouble)
        {
            cJSON_Delete(root);
            return -1;
        }
        observed_ticket->called_time_is_null = 0;
    }
    else
    {
        cJSON_Delete(root);
        return -1;
    }

    observed_ticket->id = (int64_t)id->valuedouble;
    observed_ticket->user_id = (int64_t)user_id->valuedouble;
    observed_ticket->department_id = (int64_t)department_id->valuedouble;
    observed_ticket->queue_number = (int64_t)queue_number->valuedouble;
    memcpy(observed_ticket->status, status->valuestring, status_length + 1U);
    memcpy(
        observed_ticket->service_date,
        service_date->valuestring,
        service_date_length + 1U);
    observed_ticket->created_time = (int64_t)created_time->valuedouble;

    cJSON_Delete(root);
    return 0;
}

static int observed_tickets_are_equal(
    const ObservedTicket *left,
    const ObservedTicket *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }

    return left->id == right->id &&
           left->user_id == right->user_id &&
           left->department_id == right->department_id &&
           left->queue_number == right->queue_number &&
           strcmp(left->status, right->status) == 0 &&
           strcmp(left->service_date, right->service_date) == 0 &&
           left->created_time == right->created_time &&
           left->called_time == right->called_time &&
           left->called_time_is_null == right->called_time_is_null;
}

static int64_t check_ticket_response(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id,
    int64_t expected_ticket_id)
{
    int64_t ticket_id = 0;
    cJSON *root = cJSON_Parse(response);
    cJSON *ticket;
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return 0;
    }
    CHECK(response_length == strlen(response));
    CHECK(response_length > 0U && response[response_length - 1U] == '\n');
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)expected_request_id);
    ticket = cJSON_GetObjectItemCaseSensitive(root, "ticket");
    CHECK(cJSON_IsObject(ticket));
    if (cJSON_IsObject(ticket))
    {
        item = cJSON_GetObjectItemCaseSensitive(ticket, "id");
        CHECK(cJSON_IsNumber(item));
        if (cJSON_IsNumber(item))
        {
            ticket_id = (int64_t)item->valuedouble;
            CHECK(ticket_id > 0);
            if (expected_ticket_id > 0)
            {
                CHECK(ticket_id == expected_ticket_id);
            }
        }
        item = cJSON_GetObjectItemCaseSensitive(ticket, "queue_number");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble == 1.0);
        item = cJSON_GetObjectItemCaseSensitive(ticket, "status");
        CHECK(cJSON_IsString(item));
        CHECK(item != NULL && strcmp(item->valuestring, "WAITING") == 0);
    }
    cJSON_Delete(root);
    return ticket_id;
}

static void check_success_ticket_envelope(
    const char *response,
    size_t response_length,
    uint64_t expected_request_id)
{
    cJSON *root = cJSON_Parse(response);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    CHECK(response_length == strlen(response));
    CHECK(response_length > 0U && response[response_length - 1U] == '\n');
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)expected_request_id);
    CHECK(cJSON_IsObject(
        cJSON_GetObjectItemCaseSensitive(root, "ticket")));
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "password") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "departments") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "doctors") == NULL);
    cJSON_Delete(root);
}

static void check_current_ticket_summary(
    const char *response,
    int64_t expected_current_called_queue_number,
    int64_t expected_waiting_ahead_count)
{
    cJSON *root = cJSON_Parse(response);
    cJSON *summary;
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    summary = cJSON_GetObjectItemCaseSensitive(root, "queue_summary");
    CHECK(cJSON_IsObject(summary));
    if (cJSON_IsObject(summary))
    {
        CHECK(cJSON_GetArraySize(summary) == 2);
        item = cJSON_GetObjectItemCaseSensitive(
            summary,
            "current_called_queue_number");
        if (expected_current_called_queue_number == 0)
        {
            CHECK(cJSON_IsNull(item));
        }
        else
        {
            CHECK(cJSON_IsNumber(item));
            CHECK(item != NULL && item->valuedouble ==
                (double)expected_current_called_queue_number);
        }
        item = cJSON_GetObjectItemCaseSensitive(
            summary,
            "waiting_ahead_count");
        CHECK(cJSON_IsNumber(item));
        CHECK(item != NULL && item->valuedouble ==
            (double)expected_waiting_ahead_count);
    }
    cJSON_Delete(root);
}

static int build_call_next_frame(
    char *frame,
    size_t frame_capacity,
    uint64_t request_id,
    int64_t department_id)
{
    int written;

    if (frame == NULL || frame_capacity == 0U)
    {
        return -1;
    }
    written = snprintf(
        frame,
        frame_capacity,
        "{\"type\":\"call_next\",\"request_id\":%" PRIu64
        ",\"department_id\":%" PRId64 "}\n",
        request_id,
        department_id);
    return written > 0 && (size_t)written < frame_capacity ? 0 : -1;
}

static void check_error_response(
    const char *response,
    uint64_t expected_request_id,
    const char *expected_error_code)
{
    cJSON *root = cJSON_Parse(response);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == (double)expected_request_id);
    item = cJSON_GetObjectItemCaseSensitive(root, "error_code");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, expected_error_code) == 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "ticket") == NULL);
    cJSON_Delete(root);
}

static void check_ping_response(const char *response)
{
    cJSON *root = cJSON_Parse(response);
    cJSON *item;

    CHECK(root != NULL);
    if (root == NULL)
    {
        return;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "type");
    CHECK(cJSON_IsString(item));
    CHECK(item != NULL && strcmp(item->valuestring, "pong") == 0);
    item = cJSON_GetObjectItemCaseSensitive(root, "request_id");
    CHECK(cJSON_IsNumber(item));
    CHECK(item != NULL && item->valuedouble == 804.0);
    cJSON_Delete(root);
}

static void test_ticket_tcp_flow(pid_t server_process)
{
    static const char register_frame[] =
        "{\"type\":\"register\",\"request_id\":800,"
        "\"username\":\"tcp-ticket-user\","
        "\"password\":\"teaching-password\"}\n";
    static const char second_register_frame[] =
        "{\"type\":\"register\",\"request_id\":805,"
        "\"username\":\"tcp-ticket-user-two\","
        "\"password\":\"teaching-password\"}\n";
    static const char unknown_user_frame[] =
        "{\"type\":\"create_ticket\",\"request_id\":803,"
        "\"user_id\":999999,\"department_id\":1}\n";
    static const char ping_frame[] =
        "{\"type\":\"ping\",\"request_id\":804}\n";
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    char duplicate_frame[256];
    char call_next_frame[256];
    char user_id_text[32];
    char second_user_id_text[32];
    char ticket_id_text[32];
    char department_id_text[32];
    size_t response_length = 0U;
    int64_t user_id;
    int64_t second_user_id;
    int64_t ticket_id;
    ObservedTicket created_ticket;
    ObservedTicket second_created_ticket;
    ObservedTicket current_ticket;
    ObservedTicket queried_ticket;
    ObservedTicket called_by_client;
    ObservedTicket second_called;
    ObservedTicket current_called;
    ObservedTicket current_second_called;
    ObservedTicket queried_called;
    int written;

    CHECK(send_frame(
              register_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    user_id = check_registration_response(response, 800U);
    CHECK(user_id > 0);

    CHECK(send_frame(
              second_register_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    second_user_id = check_registration_response(response, 805U);
    CHECK(second_user_id > 0);
    CHECK(second_user_id != user_id);

    written = snprintf(user_id_text, sizeof(user_id_text), "%" PRId64, user_id);
    CHECK(written > 0 && (size_t)written < sizeof(user_id_text));
    written = snprintf(
        second_user_id_text,
        sizeof(second_user_id_text),
        "%" PRId64,
        second_user_id);
    CHECK(written > 0 && (size_t)written < sizeof(second_user_id_text));
    CHECK(run_client_process(
              CURRENT_TICKET_CLIENT_PATH,
              "900",
              user_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 2);
    check_error_response(
        response,
        900U,
        "CURRENT_TICKET_NOT_FOUND");

    CHECK(run_client_process(
              TICKET_CLIENT_PATH,
              "801",
              user_id_text,
              "1",
              response,
              sizeof(response),
              &response_length) == 0);
    ticket_id = check_ticket_response(response, response_length, 801U, 0);
    CHECK(ticket_id > 0);
    CHECK(decode_ticket_payload(response, &created_ticket) == 0);
    CHECK(created_ticket.id == ticket_id);
    CHECK(created_ticket.user_id == user_id);
    CHECK(created_ticket.department_id == 1);

    CHECK(run_client_process(
              TICKET_CLIENT_PATH,
              "806",
              second_user_id_text,
              "1",
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 806U);
    CHECK(decode_ticket_payload(response, &second_created_ticket) == 0);
    CHECK(second_created_ticket.id > created_ticket.id);
    CHECK(second_created_ticket.user_id == second_user_id);
    CHECK(second_created_ticket.department_id == created_ticket.department_id);
    CHECK(second_created_ticket.queue_number == 2);
    CHECK(strcmp(second_created_ticket.status, "WAITING") == 0);
    CHECK(second_created_ticket.called_time_is_null);

    CHECK(run_client_process(
              CURRENT_TICKET_CLIENT_PATH,
              "904",
              user_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 904U);
    check_current_ticket_summary(response, 0, 0);
    CHECK(decode_ticket_payload(response, &current_ticket) == 0);
    CHECK(observed_tickets_are_equal(&current_ticket, &created_ticket));

    written = snprintf(
        ticket_id_text,
        sizeof(ticket_id_text),
        "%" PRId64,
        ticket_id);
    CHECK(written > 0 && (size_t)written < sizeof(ticket_id_text));

    CHECK(run_client_process(
              TICKET_STATUS_CLIENT_PATH,
              "902",
              ticket_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 0);
    CHECK(check_ticket_response(response, response_length, 902U, ticket_id) == ticket_id);
    CHECK(decode_ticket_payload(response, &queried_ticket) == 0);
    CHECK(observed_tickets_are_equal(&created_ticket, &queried_ticket));

    CHECK(run_client_process(
              TICKET_STATUS_CLIENT_PATH,
              "903",
              "9223372036854775807",
              NULL,
              response,
              sizeof(response),
              &response_length) != 0);
    check_error_response(response, 903U, "TICKET_NOT_FOUND");
    CHECK(kill(server_process, 0) == 0);

    written = snprintf(
        duplicate_frame,
        sizeof(duplicate_frame),
        "{\"type\":\"create_ticket\",\"request_id\":802,"
        "\"user_id\":%" PRId64 ",\"department_id\":1}\n",
        user_id);
    CHECK(written > 0 && (size_t)written < sizeof(duplicate_frame));
    CHECK(send_frame(
              duplicate_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    CHECK(check_ticket_response(
              response,
              response_length,
              802U,
              ticket_id) == ticket_id);

    CHECK(send_frame(
              unknown_user_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, 803U, "USER_NOT_FOUND");

    written = snprintf(
        department_id_text,
        sizeof(department_id_text),
        "%" PRId64,
        created_ticket.department_id);
    CHECK(written > 0 && (size_t)written < sizeof(department_id_text));
    CHECK(run_client_process(
              ADMIN_CALL_CLIENT_PATH,
              "1101",
              department_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 1101U);
    CHECK(decode_ticket_payload(response, &called_by_client) == 0);
    CHECK(called_by_client.id == created_ticket.id);
    CHECK(called_by_client.user_id == created_ticket.user_id);
    CHECK(called_by_client.department_id == created_ticket.department_id);
    CHECK(called_by_client.queue_number == created_ticket.queue_number);
    CHECK(strcmp(called_by_client.status, "CALLED") == 0);
    CHECK(strcmp(
              called_by_client.service_date,
              created_ticket.service_date) == 0);
    CHECK(called_by_client.created_time == created_ticket.created_time);
    CHECK(!called_by_client.called_time_is_null);
    CHECK(called_by_client.called_time > 0);

    CHECK(build_call_next_frame(
              call_next_frame,
              sizeof(call_next_frame),
              1102U,
              created_ticket.department_id) == 0);
    CHECK(send_frame(
              call_next_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 1102U);
    CHECK(decode_ticket_payload(response, &second_called) == 0);
    CHECK(second_called.id == second_created_ticket.id);
    CHECK(second_called.id != called_by_client.id);
    CHECK(second_called.user_id == second_created_ticket.user_id);
    CHECK(second_called.department_id == second_created_ticket.department_id);
    CHECK(second_called.queue_number == second_created_ticket.queue_number);
    CHECK(strcmp(second_called.status, "CALLED") == 0);
    CHECK(strcmp(
              second_called.service_date,
              second_created_ticket.service_date) == 0);
    CHECK(second_called.created_time == second_created_ticket.created_time);
    CHECK(!second_called.called_time_is_null);
    CHECK(second_called.called_time > 0);

    CHECK(build_call_next_frame(
              call_next_frame,
              sizeof(call_next_frame),
              1103U,
              created_ticket.department_id) == 0);
    CHECK(send_frame(
              call_next_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, 1103U, "NO_WAITING_TICKET");

    CHECK(run_client_process(
              TICKET_STATUS_CLIENT_PATH,
              "1104",
              ticket_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 1104U);
    CHECK(decode_ticket_payload(response, &queried_called) == 0);
    CHECK(observed_tickets_are_equal(&queried_called, &called_by_client));
    CHECK(strcmp(queried_called.status, "CALLED") == 0);
    CHECK(queried_called.called_time == called_by_client.called_time);

    CHECK(run_client_process(
              CURRENT_TICKET_CLIENT_PATH,
              "1106",
              user_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 1106U);
    check_current_ticket_summary(response, 2, 0);
    CHECK(decode_ticket_payload(response, &current_called) == 0);
    CHECK(observed_tickets_are_equal(&current_called, &called_by_client));
    CHECK(strcmp(current_called.status, "CALLED") == 0);

    CHECK(run_client_process(
              CURRENT_TICKET_CLIENT_PATH,
              "1107",
              second_user_id_text,
              NULL,
              response,
              sizeof(response),
              &response_length) == 0);
    check_success_ticket_envelope(response, response_length, 1107U);
    check_current_ticket_summary(response, 2, 0);
    CHECK(decode_ticket_payload(response, &current_second_called) == 0);
    CHECK(observed_tickets_are_equal(&current_second_called, &second_called));
    CHECK(strcmp(current_second_called.status, "CALLED") == 0);

    CHECK(build_call_next_frame(
              call_next_frame,
              sizeof(call_next_frame),
              1105U,
              3) == 0);
    CHECK(send_frame(
              call_next_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    check_error_response(response, 1105U, "NO_WAITING_TICKET");

    CHECK(send_frame(
              ping_frame,
              response,
              sizeof(response),
              &response_length) == 0);
    check_ping_response(response);
    CHECK(kill(server_process, 0) == 0);
}

int main(void)
{
    pid_t server_process = -1;
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
        test_ticket_tcp_flow(server_process);
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
    if (remove(TEST_DATABASE_PATH) != 0 && errno != ENOENT)
    {
        CHECK(0);
    }
    CHECK(access(TEST_DATABASE_PATH, F_OK) != 0);

    if (failures != 0)
    {
        fprintf(stderr, "%d TCP ticket test(s) failed\n", failures);
        return 1;
    }
    puts("TCP ticket tests passed");
    return 0;
}
