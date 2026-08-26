/*
 * 文件作用：最小 TCP 探活客户端。
 * 它连接服务器并发送带 request_id 的 ping，接收 pong，用来快速确认 IP、端口、socket、
 * 换行协议和服务器事件循环是否可用，不涉及医疗业务和 SQLite。
 */
#include "clinic_net.h"
#include "clinic_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "9000";
    clinic_socket_t socket_fd;
    char request[256];
    char response[CLINIC_MAX_FRAME_SIZE + 1U];
    size_t response_length = 0U;
    int request_length;

    if (clinic_net_startup() != 0 ||
        clinic_net_connect(host, port, &socket_fd) != 0)
    {
        fprintf(stderr, "could not connect to %s:%s\n", host, port);
        clinic_net_cleanup();
        return EXIT_FAILURE;
    }

    request_length = clinic_protocol_encode_ping(1U, request, sizeof(request));
    if (request_length < 0 ||
        clinic_net_send_all(socket_fd, request, (size_t)request_length) != 0)
    {
        fprintf(stderr, "could not send ping\n");
        clinic_socket_close(socket_fd);
        clinic_net_cleanup();
        return EXIT_FAILURE;
    }

    while (response_length + 1U < sizeof(response))
    {
        int received = recv(
            socket_fd,
            response + response_length,
            (int)(sizeof(response) - response_length - 1U),
            0);

        if (received <= 0)
        {
            fprintf(stderr, "server closed before sending a response\n");
            clinic_socket_close(socket_fd);
            clinic_net_cleanup();
            return EXIT_FAILURE;
        }

        response_length += (size_t)received;
        response[response_length] = '\0';
        if (strchr(response, '\n') != NULL)
        {
            break;
        }
    }

    printf("%s", response);
    clinic_socket_close(socket_fd);
    clinic_net_cleanup();
    return EXIT_SUCCESS;
}
