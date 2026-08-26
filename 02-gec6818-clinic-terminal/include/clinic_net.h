/*
 * 文件作用：声明 Windows/Linux 共用的 socket 类型和基础网络操作。
 * 这里只封装启动、监听、连接、非阻塞、完整发送和关闭；分帧、JSON、业务和数据库
 * 分别由 frame、Handler/Core 和 Store 负责。
 */
#ifndef CLINIC_NET_H
#define CLINIC_NET_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET clinic_socket_t;
#define CLINIC_SOCKET_INVALID INVALID_SOCKET
#define clinic_socket_close closesocket
#else
#include <sys/socket.h>
#include <unistd.h>
typedef int clinic_socket_t;
#define CLINIC_SOCKET_INVALID (-1)
#define clinic_socket_close close
#endif

#include <stddef.h>

int clinic_net_startup(void);
void clinic_net_cleanup(void);

int clinic_net_set_nonblocking(clinic_socket_t socket_fd);
int clinic_net_last_error_would_block(void);

int clinic_net_create_listener(
    const char *host,
    const char *port,
    clinic_socket_t *listener_out);

int clinic_net_connect(
    const char *host,
    const char *port,
    clinic_socket_t *socket_out);

int clinic_net_send_all(
    clinic_socket_t socket_fd,
    const char *data,
    size_t length);

#endif
