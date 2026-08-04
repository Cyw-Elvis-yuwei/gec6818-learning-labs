/*
 * 文件作用（答辩）：项目跨平台 socket 基础封装。
 * 它统一 Windows/Linux 网络初始化、非阻塞设置、监听 socket 创建、客户端连接、完整发送、
 * 关闭和“是否暂时不可读写”的错误判断，供服务器、主机客户端和板端 transport 复用。
 *
 * 本文件只封装网络系统调用，不负责换行分帧、JSON、业务或数据库；Linux epoll 事件循环
 * 位于 server/main.c，板端带总截止时间的 poll 收发位于 board_transport.c。
 *
 * 初学者要点：socket 是操作系统提供的网络文件描述符；非阻塞模式下暂时没有数据会返回
 * EAGAIN/EWOULDBLOCK，而不是永久卡住。send_all 则处理一次 send 只写出部分字节的情况。
 */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include "clinic_net.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#endif

#include <string.h>

/* Windows 需要 WSAStartup，Linux 无需额外启动；用统一接口屏蔽平台差异。 */
int clinic_net_startup(void)
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void clinic_net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/* 把 socket 设为非阻塞，使 epoll/poll/select 能统一管理等待。 */
int clinic_net_set_nonblocking(clinic_socket_t socket_fd)
{
#ifdef _WIN32
    u_long mode = 1UL;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0)
    {
        return -1;
    }
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int clinic_net_last_error_would_block(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int configure_socket_reuse(clinic_socket_t socket_fd)
{
    int enabled = 1;
    return setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&enabled,
        (socklen_t)sizeof(enabled));
}

/* 创建服务器监听 socket：解析地址 -> socket -> SO_REUSEADDR -> bind -> listen。 */
int clinic_net_create_listener(
    const char *host,
    const char *port,
    clinic_socket_t *listener_out)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *cursor;
    int status;

    if (port == NULL || listener_out == NULL)
    {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(host, port, &hints, &results);
    if (status != 0)
    {
        return -1;
    }

    *listener_out = CLINIC_SOCKET_INVALID;
    for (cursor = results; cursor != NULL; cursor = cursor->ai_next)
    {
        clinic_socket_t socket_fd = (clinic_socket_t)socket(
            cursor->ai_family,
            cursor->ai_socktype,
            cursor->ai_protocol);

        if (socket_fd == CLINIC_SOCKET_INVALID)
        {
            continue;
        }

        if (configure_socket_reuse(socket_fd) == 0 &&
            bind(socket_fd, cursor->ai_addr, (socklen_t)cursor->ai_addrlen) == 0 &&
            listen(socket_fd, 16) == 0 &&
            clinic_net_set_nonblocking(socket_fd) == 0)
        {
            *listener_out = socket_fd;
            break;
        }

        clinic_socket_close(socket_fd);
    }

    freeaddrinfo(results);
    return *listener_out == CLINIC_SOCKET_INVALID ? -1 : 0;
}

/* 主机工具使用的通用同步连接；板端正式客户端使用带总 deadline 的 board_transport。 */
int clinic_net_connect(
    const char *host,
    const char *port,
    clinic_socket_t *socket_out)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *cursor;
    int status;

    if (host == NULL || port == NULL || socket_out == NULL)
    {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(host, port, &hints, &results);
    if (status != 0)
    {
        return -1;
    }

    *socket_out = CLINIC_SOCKET_INVALID;
    for (cursor = results; cursor != NULL; cursor = cursor->ai_next)
    {
        clinic_socket_t socket_fd = (clinic_socket_t)socket(
            cursor->ai_family,
            cursor->ai_socktype,
            cursor->ai_protocol);

        if (socket_fd == CLINIC_SOCKET_INVALID)
        {
            continue;
        }

        if (connect(socket_fd, cursor->ai_addr, (socklen_t)cursor->ai_addrlen) == 0)
        {
            *socket_out = socket_fd;
            break;
        }

        clinic_socket_close(socket_fd);
    }

    freeaddrinfo(results);
    return *socket_out == CLINIC_SOCKET_INVALID ? -1 : 0;
}

/* 循环 send 直到 length 字节全部发送；EINTR/暂时阻塞可以重试，其他错误退出。 */
int clinic_net_send_all(
    clinic_socket_t socket_fd,
    const char *data,
    size_t length)
{
    size_t sent = 0U;

    while (sent < length)
    {
        int result = send(socket_fd, data + sent, (int)(length - sent), 0);
        if (result <= 0)
        {
            return -1;
        }
        sent += (size_t)result;
    }

    return 0;
}
