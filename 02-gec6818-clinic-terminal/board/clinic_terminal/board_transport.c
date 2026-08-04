/*
 * 文件作用（答辩）：板端四个业务客户端共用的同步 TCP 传输模块。
 * 它统一完成数值 IPv4 连接、非阻塞 poll 等待、完整发送、接收超时和换行分帧。
 *
 * 关键流程：一次 exchange 共用同一个 CLOCK_MONOTONIC 总截止时间，收到的数据
 * 交给 clinic_frame 提取一条完整 JSON；半包继续接收，粘包只取第一条完整帧，
 * 超长帧和对端断开返回明确状态。该模块不创建线程、不解析业务 JSON、不调用 LVGL；
 * 调用它的线程由 main.c 管理。
 *
 * 答辩阅读地图：make_deadline 生成一次请求的总截止时间；wait_for_socket 用 poll 等待
 * socket 可读/可写；connect_with_deadline 完成非阻塞连接；send_with_deadline 解决一次
 * send 未必发完；receive_frame_with_deadline 解决一次 recv 未必收到完整消息；exchange
 * 把这些步骤串起来，并在唯一 cleanup 出口关闭 socket。
 */
#define _POSIX_C_SOURCE 200809L

#include "board_transport.h"

#include "clinic_frame.h"
#include "clinic_net.h"

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define RECEIVE_CHUNK_SIZE 1024U

/* 使用单调时钟生成绝对截止时间，不受用户修改系统时间影响。 */
static int make_deadline(
    unsigned int timeout_ms,
    struct timespec *deadline)
{
    if(deadline == NULL || timeout_ms == 0U ||
       clock_gettime(CLOCK_MONOTONIC, deadline) != 0) {
        return -1;
    }

    deadline->tv_sec += (time_t)(timeout_ms / 1000U);
    deadline->tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if(deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
    return 0;
}

/* 每次 poll 前重算剩余时间，使连接、发送和接收共同消耗同一个总超时。 */
static int remaining_timeout_ms(const struct timespec *deadline)
{
    struct timespec now;
    int64_t seconds;
    int64_t nanoseconds;
    int64_t milliseconds;

    if(deadline == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }

    seconds = (int64_t)deadline->tv_sec - (int64_t)now.tv_sec;
    nanoseconds = (int64_t)deadline->tv_nsec - (int64_t)now.tv_nsec;
    if(nanoseconds < 0) {
        --seconds;
        nanoseconds += INT64_C(1000000000);
    }
    if(seconds < 0 || (seconds == 0 && nanoseconds <= 0)) {
        return 0;
    }

    if(seconds > (int64_t)INT_MAX / 1000) {
        return INT_MAX;
    }
    milliseconds = seconds * 1000 +
                   (nanoseconds + INT64_C(999999)) / INT64_C(1000000);
    return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

/*
 * 非阻塞 socket 暂时不可读/写并不是失败；poll 会睡眠等待事件，避免忙循环占满 CPU。
 * EINTR 表示被信号打断，可以继续等待；真正超时或坏描述符才返回失败。
 */
static int wait_for_socket(
    clinic_socket_t socket_fd,
    short events,
    const struct timespec *deadline)
{
    struct pollfd descriptor;

    descriptor.fd = socket_fd;
    descriptor.events = events;
    descriptor.revents = 0;

    for(;;) {
        int timeout_ms = remaining_timeout_ms(deadline);
        int wait_result;

        if(timeout_ms <= 0) {
            errno = ETIMEDOUT;
            return -1;
        }

        descriptor.revents = 0;
        wait_result = poll(&descriptor, 1U, timeout_ms);
        if(wait_result > 0) {
            if((descriptor.revents & POLLNVAL) != 0) {
                errno = EBADF;
                return -1;
            }
            if((descriptor.revents & (events | POLLERR | POLLHUP)) != 0) {
                return 0;
            }
            continue;
        }
        if(wait_result == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if(errno != EINTR) {
            return -1;
        }
    }
}

/*
 * 创建非阻塞 IPv4 socket。connect 返回 EINPROGRESS 时，等待 POLLOUT 后必须再读取
 * SO_ERROR；“变得可写”只表示连接流程结束，不等于连接一定成功。
 */
static int connect_with_deadline(
    const char *server_ip,
    const char *server_port,
    const struct timespec *deadline,
    clinic_socket_t *socket_out)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;

    if(server_ip == NULL || server_port == NULL || socket_out == NULL) {
        return -1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;
    *socket_out = CLINIC_SOCKET_INVALID;

    if(getaddrinfo(server_ip, server_port, &hints, &addresses) != 0) {
        return -1;
    }

    for(address = addresses; address != NULL; address = address->ai_next) {
        clinic_socket_t socket_fd;
        int connect_result;
        int socket_error = 0;
        socklen_t socket_error_length = (socklen_t)sizeof(socket_error);

        socket_fd = (clinic_socket_t)socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol);
        if(socket_fd == CLINIC_SOCKET_INVALID) {
            continue;
        }
        if(clinic_net_set_nonblocking(socket_fd) != 0) {
            clinic_socket_close(socket_fd);
            continue;
        }

        connect_result = connect(
            socket_fd,
            address->ai_addr,
            (socklen_t)address->ai_addrlen);
        if(connect_result != 0) {
            if(errno != EINPROGRESS && !clinic_net_last_error_would_block()) {
                clinic_socket_close(socket_fd);
                continue;
            }
            if(wait_for_socket(socket_fd, POLLOUT, deadline) != 0 ||
               getsockopt(
                   socket_fd,
                   SOL_SOCKET,
                   SO_ERROR,
                   &socket_error,
                   &socket_error_length) != 0 ||
               socket_error != 0) {
                clinic_socket_close(socket_fd);
                continue;
            }
        }

        *socket_out = socket_fd;
        break;
    }

    freeaddrinfo(addresses);
    return *socket_out == CLINIC_SOCKET_INVALID ? -1 : 0;
}

/* TCP send 可能只发送一部分，因此用 sent_length 循环直到 request 全部写出。 */
static int send_with_deadline(
    clinic_socket_t socket_fd,
    const char *data,
    size_t length,
    const struct timespec *deadline)
{
    size_t sent_length = 0U;

    while(sent_length < length) {
        ssize_t sent;

        if(wait_for_socket(socket_fd, POLLOUT, deadline) != 0) {
            return -1;
        }

        sent = send(
            socket_fd,
            data + sent_length,
            length - sent_length,
            MSG_NOSIGNAL);
        if(sent > 0) {
            sent_length += (size_t)sent;
            continue;
        }
        if(sent < 0 && (errno == EINTR || clinic_net_last_error_would_block())) {
            continue;
        }
        return -1;
    }

    return 0;
}

/*
 * recv 得到的是字节块而不是 JSON 消息。数据先追加到 frame_buffer，再按 '\n' 提取一帧；
 * NEED_MORE 继续收，READY 返回一条完整响应，超限或断开映射成明确传输错误。
 */
static ClinicBoardTransportStatus receive_frame_with_deadline(
    clinic_socket_t socket_fd,
    const struct timespec *deadline,
    char *line,
    size_t line_capacity,
    size_t *line_length)
{
    clinic_frame_buffer_t frame_buffer;
    char chunk[RECEIVE_CHUNK_SIZE];

    clinic_frame_buffer_init(&frame_buffer);
    for(;;) {
        int frame_result = clinic_frame_buffer_next(
            &frame_buffer,
            line,
            line_capacity,
            line_length);

        if(frame_result == CLINIC_FRAME_READY) {
            return CLINIC_BOARD_TRANSPORT_OK;
        }
        if(frame_result == CLINIC_FRAME_TOO_LARGE) {
            return CLINIC_BOARD_TRANSPORT_FRAME_ERROR;
        }
        if(wait_for_socket(socket_fd, POLLIN, deadline) != 0) {
            return CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR;
        }

        for(;;) {
            ssize_t received = recv(socket_fd, chunk, sizeof(chunk), 0);

            if(received > 0) {
                if(clinic_frame_buffer_append(
                       &frame_buffer,
                       chunk,
                       (size_t)received) != 0) {
                    return CLINIC_BOARD_TRANSPORT_FRAME_ERROR;
                }
                break;
            }
            if(received == 0) {
                return CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR;
            }
            if(errno == EINTR) {
                continue;
            }
            if(clinic_net_last_error_would_block()) {
                break;
            }
            return CLINIC_BOARD_TRANSPORT_RECEIVE_ERROR;
        }
    }
}

/*
 * 板端一次完整“请求—响应”交换的公共入口。
 * 参数校验 -> 建立总 deadline -> 网络初始化 -> 连接 -> 完整发送 -> 接收一帧 -> 统一清理。
 * 这是同步函数，但它由 main.c 的工作线程调用，所以不会卡住 LVGL 主线程。
 */
ClinicBoardTransportStatus clinic_board_transport_exchange(
    const char *server_ip,
    const char *server_port,
    const char *request,
    size_t request_length,
    unsigned int timeout_ms,
    char *response,
    size_t response_capacity,
    size_t *response_length)
{
    struct timespec deadline;
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
    ClinicBoardTransportStatus status;
    int network_started = 0;

    if(response_length != NULL) {
        *response_length = 0U;
    }
    if(response != NULL && response_capacity > 0U) {
        response[0] = '\0';
    }
    if(server_ip == NULL || server_ip[0] == '\0' ||
       server_port == NULL || server_port[0] == '\0' ||
       request == NULL || request_length == 0U ||
       response == NULL || response_capacity == 0U ||
       response_length == NULL || timeout_ms == 0U) {
        return CLINIC_BOARD_TRANSPORT_INVALID_ARGUMENT;
    }

    if(make_deadline(timeout_ms, &deadline) != 0 ||
       clinic_net_startup() != 0) {
        return CLINIC_BOARD_TRANSPORT_INITIALIZATION_ERROR;
    }
    network_started = 1;

    if(connect_with_deadline(
           server_ip,
           server_port,
           &deadline,
           &socket_fd) != 0 ||
       send_with_deadline(
           socket_fd,
           request,
           request_length,
           &deadline) != 0) {
        status = CLINIC_BOARD_TRANSPORT_SEND_ERROR;
        goto cleanup;
    }

    status = receive_frame_with_deadline(
        socket_fd,
        &deadline,
        response,
        response_capacity,
        response_length);

cleanup:
    if(socket_fd != CLINIC_SOCKET_INVALID) {
        clinic_socket_close(socket_fd);
    }
    if(network_started) {
        clinic_net_cleanup();
    }
    return status;
}
