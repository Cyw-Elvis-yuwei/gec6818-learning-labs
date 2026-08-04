/*
 * 文件作用（答辩）：GEC6818 的最小 TCP 网络探针，不包含 LVGL 和医疗业务。
 * 它向 Ubuntu 服务器发送换行结尾的 ping JSON 并等待 pong，用于快速区分“开发板网络/
 * IP/端口问题”和“正式终端页面或业务代码问题”。
 */
#define _POSIX_C_SOURCE 200809L

#include "clinic_net.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    NETWORK_TIMEOUT_SECONDS = 5,
    RESPONSE_MAX_BYTES = 4096
};

static const char ping_request[] =
    "{\"type\":\"ping\",\"request_id\":1}\n";

static volatile sig_atomic_t timeout_expired = 0;

static void handle_timeout(int signal_number)
{
    (void)signal_number;
    timeout_expired = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction timeout_action = {0};
    struct sigaction pipe_action = {0};

    timeout_action.sa_handler = handle_timeout;
    if (sigemptyset(&timeout_action.sa_mask) != 0 ||
        sigaction(SIGALRM, &timeout_action, NULL) != 0) {
        return -1;
    }

    pipe_action.sa_handler = SIG_IGN;
    if (sigemptyset(&pipe_action.sa_mask) != 0 ||
        sigaction(SIGPIPE, &pipe_action, NULL) != 0) {
        return -1;
    }

    return 0;
}

static int port_is_valid(const char *port)
{
    unsigned long value = 0UL;
    const unsigned char *cursor = (const unsigned char *)port;

    if (port == NULL || *port == '\0') {
        return 0;
    }

    while (*cursor != '\0') {
        if (*cursor < (unsigned char)'0' ||
            *cursor > (unsigned char)'9') {
            return 0;
        }

        value = value * 10UL + (unsigned long)(*cursor - (unsigned char)'0');
        if (value > 65535UL) {
            return 0;
        }
        ++cursor;
    }

    return value >= 1UL;
}

static int response_is_pong(const char *response, size_t response_length)
{
    if (memchr(response, '\0', response_length) != NULL) {
        return 0;
    }

    return strstr(response, "\"ok\":true") != NULL &&
           strstr(response, "\"type\":\"pong\"") != NULL &&
           strstr(response, "\"request_id\":1") != NULL;
}

int main(int argc, char **argv)
{
    const char *server_ip;
    const char *server_port;
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
    char response[RESPONSE_MAX_BYTES + 1U];
    size_t response_length = 0U;
    int network_started = 0;
    int received_line = 0;
    int exit_code = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <server_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    server_ip = argv[1];
    server_port = argv[2];

    if (*server_ip == '\0') {
        fputs("server_ip must not be empty\n", stderr);
        return EXIT_FAILURE;
    }
    if (!port_is_valid(server_port)) {
        fputs("port must be an integer from 1 to 65535\n", stderr);
        return EXIT_FAILURE;
    }
    if (install_signal_handlers() != 0) {
        fputs("could not install network timeout handlers\n", stderr);
        return EXIT_FAILURE;
    }
    if (clinic_net_startup() != 0) {
        fputs("could not initialize clinic_net\n", stderr);
        return EXIT_FAILURE;
    }

    network_started = 1;
    timeout_expired = 0;
    (void)alarm(NETWORK_TIMEOUT_SECONDS);

    if (clinic_net_connect(server_ip, server_port, &socket_fd) != 0) {
        if (timeout_expired != 0) {
            fprintf(
                stderr,
                "connection to %s:%s timed out after %d seconds\n",
                server_ip,
                server_port,
                NETWORK_TIMEOUT_SECONDS
            );
        }
        else {
            fprintf(
                stderr,
                "could not connect to %s:%s\n",
                server_ip,
                server_port
            );
        }
        goto cleanup;
    }

    if (clinic_net_send_all(
            socket_fd,
            ping_request,
            sizeof(ping_request) - 1U) != 0) {
        if (timeout_expired != 0) {
            fputs("sending ping timed out after 5 seconds\n", stderr);
        }
        else {
            fputs("could not send ping request\n", stderr);
        }
        goto cleanup;
    }

    while (response_length < RESPONSE_MAX_BYTES) {
        size_t available = RESPONSE_MAX_BYTES - response_length;
        ssize_t received = recv(
            socket_fd,
            response + response_length,
            available,
            0
        );

        if (received == 0) {
            fputs("server disconnected before newline response\n", stderr);
            goto cleanup;
        }
        if (received < 0) {
            if (errno == EINTR && timeout_expired == 0) {
                continue;
            }
            if (timeout_expired != 0) {
                fputs("receiving pong timed out after 5 seconds\n", stderr);
            }
            else {
                fputs("could not receive server response\n", stderr);
            }
            goto cleanup;
        }

        {
            char *newline = memchr(
                response + response_length,
                '\n',
                (size_t)received
            );

            response_length += (size_t)received;
            if (newline != NULL) {
                response_length = (size_t)(newline - response) + 1U;
                received_line = 1;
                break;
            }
        }
    }

    (void)alarm(0U);

    if (!received_line) {
        fprintf(
            stderr,
            "response exceeded %d bytes without newline\n",
            RESPONSE_MAX_BYTES
        );
        goto cleanup;
    }

    response[response_length] = '\0';
    if (fwrite(response, 1U, response_length, stdout) != response_length ||
        fflush(stdout) != 0) {
        fputs("could not write complete server response\n", stderr);
        goto cleanup;
    }

    if (!response_is_pong(response, response_length)) {
        fputs("server response is not the expected pong\n", stderr);
        goto cleanup;
    }

    fputs("BOARD_TCP_PONG_OK\n", stdout);
    exit_code = EXIT_SUCCESS;

cleanup:
    (void)alarm(0U);
    if (socket_fd != CLINIC_SOCKET_INVALID) {
        clinic_socket_close(socket_fd);
    }
    if (network_started != 0) {
        clinic_net_cleanup();
    }
    return exit_code;
}
