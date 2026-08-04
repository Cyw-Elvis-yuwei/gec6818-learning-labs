#define _POSIX_C_SOURCE 200809L

#include "board_transport.h"

#include "clinic_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define TEST_REQUEST_CAPACITY 256U

static int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if(!(condition)) {                                                     \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                               \
            ++failures;                                                        \
        }                                                                      \
    } while(0)

typedef struct MockServer {
    int listener;
    pthread_t thread;
    int thread_started;
    const char *response;
    size_t response_length;
    size_t split_at;
    unsigned int response_delay_ms;
    char received[TEST_REQUEST_CAPACITY];
    size_t received_length;
} MockServer;

static void sleep_milliseconds(unsigned int milliseconds)
{
    struct timespec delay;

    delay.tv_sec = (time_t)(milliseconds / 1000U);
    delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    while(nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static int send_bytes(int socket_fd, const char *data, size_t length)
{
    size_t sent_length = 0U;

    while(sent_length < length) {
        ssize_t sent = send(
            socket_fd,
            data + sent_length,
            length - sent_length,
            MSG_NOSIGNAL);

        if(sent > 0) {
            sent_length += (size_t)sent;
            continue;
        }
        if(sent < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

static void *mock_server_worker(void *argument)
{
    MockServer *server = argument;
    struct pollfd listener_event;
    int client_socket = -1;

    listener_event.fd = server->listener;
    listener_event.events = POLLIN;
    listener_event.revents = 0;
    if(poll(&listener_event, 1U, 2000) > 0 &&
       (listener_event.revents & POLLIN) != 0) {
        client_socket = accept(server->listener, NULL, NULL);
    }

    if(client_socket >= 0) {
        while(server->received_length + 1U < sizeof(server->received)) {
            ssize_t received = recv(
                client_socket,
                server->received + server->received_length,
                sizeof(server->received) - server->received_length - 1U,
                0);

            if(received > 0) {
                size_t previous_length = server->received_length;
                server->received_length += (size_t)received;
                server->received[server->received_length] = '\0';
                if(memchr(
                       server->received + previous_length,
                       '\n',
                       (size_t)received) != NULL) {
                    break;
                }
                continue;
            }
            if(received < 0 && errno == EINTR) {
                continue;
            }
            break;
        }

        if(server->response_delay_ms > 0U) {
            sleep_milliseconds(server->response_delay_ms);
        }
        if(server->response != NULL && server->response_length > 0U) {
            size_t split_at = server->split_at;

            if(split_at == 0U || split_at >= server->response_length) {
                split_at = server->response_length;
            }
            (void)send_bytes(client_socket, server->response, split_at);
            if(split_at < server->response_length) {
                sleep_milliseconds(10U);
                (void)send_bytes(
                    client_socket,
                    server->response + split_at,
                    server->response_length - split_at);
            }
        }
        (void)close(client_socket);
    }

    (void)close(server->listener);
    server->listener = -1;
    return NULL;
}

static int start_mock_server(MockServer *server, char *port, size_t port_capacity)
{
    struct sockaddr_in address;
    socklen_t address_length = (socklen_t)sizeof(address);
    int enabled = 1;

    if(server == NULL || port == NULL || port_capacity < 6U) {
        return -1;
    }

    server->listener = socket(AF_INET, SOCK_STREAM, 0);
    if(server->listener < 0) {
        return -1;
    }
    (void)setsockopt(
        server->listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        &enabled,
        (socklen_t)sizeof(enabled));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0U);
    if(bind(
           server->listener,
           (const struct sockaddr *)&address,
           (socklen_t)sizeof(address)) != 0 ||
       listen(server->listener, 1) != 0 ||
       getsockname(
           server->listener,
           (struct sockaddr *)&address,
           &address_length) != 0 ||
       snprintf(port, port_capacity, "%u", (unsigned int)ntohs(address.sin_port)) < 0) {
        (void)close(server->listener);
        server->listener = -1;
        return -1;
    }

    if(pthread_create(&server->thread, NULL, mock_server_worker, server) != 0) {
        (void)close(server->listener);
        server->listener = -1;
        return -1;
    }
    server->thread_started = 1;
    return 0;
}

static void finish_mock_server(MockServer *server)
{
    if(server != NULL && server->thread_started) {
        CHECK(pthread_join(server->thread, NULL) == 0);
        server->thread_started = 0;
    }
}

static void test_invalid_arguments_clear_output(void)
{
    char response[32] = "stale";
    size_t response_length = 99U;

    CHECK(clinic_board_transport_exchange(
              NULL,
              "9000",
              "{}\n",
              3U,
              100U,
              response,
              sizeof(response),
              &response_length) == CLINIC_BOARD_TRANSPORT_INVALID_ARGUMENT);
    CHECK(response_length == 0U);
    CHECK(response[0] == '\0');
}

static void test_split_frame_success(void)
{
    static const char request[] = "{\"type\":\"ping\",\"request_id\":1}\n";
    static const char response_text[] = "{\"ok\":true}\r\n";
    MockServer server;
    char port[6] = {0};
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;

    memset(&server, 0, sizeof(server));
    server.listener = -1;
    server.response = response_text;
    server.response_length = sizeof(response_text) - 1U;
    server.split_at = 5U;
    CHECK(start_mock_server(&server, port, sizeof(port)) == 0);

    CHECK(clinic_board_transport_exchange(
              "127.0.0.1",
              port,
              request,
              sizeof(request) - 1U,
              1000U,
              response,
              sizeof(response),
              &response_length) == CLINIC_BOARD_TRANSPORT_OK);
    finish_mock_server(&server);

    CHECK(response_length == strlen("{\"ok\":true}"));
    CHECK(strcmp(response, "{\"ok\":true}") == 0);
    CHECK(server.received_length == sizeof(request) - 1U);
    CHECK(memcmp(server.received, request, sizeof(request) - 1U) == 0);
}

static void test_peer_close_is_receive_error(void)
{
    static const char request[] = "{}\n";
    MockServer server;
    char port[6] = {0};
    char response[32] = {0};
    size_t response_length = 0U;

    memset(&server, 0, sizeof(server));
    server.listener = -1;
    CHECK(start_mock_server(&server, port, sizeof(port)) == 0);

    CHECK(clinic_board_transport_exchange(
              "127.0.0.1",
              port,
              request,
              sizeof(request) - 1U,
              1000U,
              response,
              sizeof(response),
              &response_length) == CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR);
    finish_mock_server(&server);
    CHECK(response_length == 0U);
}

static void test_oversized_frame_is_protocol_error(void)
{
    static const char request[] = "{}\n";
    MockServer server;
    char oversized[CLINIC_MAX_FRAME_SIZE + 1U];
    char port[6] = {0};
    char response[CLINIC_MAX_FRAME_SIZE + 1U] = {0};
    size_t response_length = 0U;

    memset(oversized, 'x', sizeof(oversized));
    memset(&server, 0, sizeof(server));
    server.listener = -1;
    server.response = oversized;
    server.response_length = sizeof(oversized);
    CHECK(start_mock_server(&server, port, sizeof(port)) == 0);

    CHECK(clinic_board_transport_exchange(
              "127.0.0.1",
              port,
              request,
              sizeof(request) - 1U,
              1000U,
              response,
              sizeof(response),
              &response_length) == CLINIC_BOARD_TRANSPORT_FRAME_ERROR);
    finish_mock_server(&server);
    CHECK(response_length == 0U);
}

static void test_deadline_is_receive_error(void)
{
    static const char request[] = "{}\n";
    static const char response_text[] = "{}\n";
    MockServer server;
    char port[6] = {0};
    char response[32] = {0};
    size_t response_length = 0U;

    memset(&server, 0, sizeof(server));
    server.listener = -1;
    server.response = response_text;
    server.response_length = sizeof(response_text) - 1U;
    server.response_delay_ms = 200U;
    CHECK(start_mock_server(&server, port, sizeof(port)) == 0);

    CHECK(clinic_board_transport_exchange(
              "127.0.0.1",
              port,
              request,
              sizeof(request) - 1U,
              50U,
              response,
              sizeof(response),
              &response_length) == CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR);
    finish_mock_server(&server);
    CHECK(response_length == 0U);
}

static void test_invalid_numeric_host_is_send_error(void)
{
    char response[32] = {0};
    size_t response_length = 0U;

    CHECK(clinic_board_transport_exchange(
              "999.0.0.1",
              "9000",
              "{}\n",
              3U,
              100U,
              response,
              sizeof(response),
              &response_length) == CLINIC_BOARD_TRANSPORT_SEND_ERROR);
    CHECK(response_length == 0U);
}

int main(void)
{
    test_invalid_arguments_clear_output();
    test_split_frame_success();
    test_peer_close_is_receive_error();
    test_oversized_frame_is_protocol_error();
    test_deadline_is_receive_error();
    test_invalid_numeric_host_is_send_error();

    if(failures != 0) {
        fprintf(stderr, "%d board transport test(s) failed\n", failures);
        return 1;
    }
    puts("board transport tests passed");
    return 0;
}
