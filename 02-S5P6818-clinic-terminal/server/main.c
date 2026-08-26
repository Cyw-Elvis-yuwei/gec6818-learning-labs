/*
 * 文件作用：Ubuntu TCP 服务器入口，负责启动数据库、Core、Handler 和事件循环。
 * Linux 下使用 epoll 统一监听服务器 socket 与多个客户端连接；每个连接拥有独立的
 * clinic_frame_buffer，用换行符从 TCP 字节流中提取完整请求。
 *
 * 关键流程：accept 新连接 -> 非阻塞 recv -> frame 提取一条消息 -> Handler 处理 ->
 * send 返回 JSON。服务器本文件只负责连接和资源生命周期，不编写 SQL、不复制业务规则；
 * 退出时关闭全部连接、Store 和网络环境。Windows 的 select 分支主要用于主机测试兼容。
 *
 * 阅读地图：main() 装配 SQLite Store、Core 和 Handler；run_epoll_server() 监听事件；
 * handle_connection_read() 把 recv 字节送入 frame；handle_frame() 调 Handler 并回发结果。
 * 这体现了“网络层只负责何时收发，Handler 负责收到的内容是什么意思”。
 */
#include "clinic_core.h"
#include "clinic_frame.h"
#include "clinic_net.h"
#include "clinic_protocol.h"
#include "clinic_server_handler.h"
#include "clinic_store_sqlite.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/epoll.h>
#include <sys/stat.h>
#else
#include <direct.h>
#endif

#define CLINIC_SERVER_PORT "9000"
#define CLINIC_DEFAULT_DATABASE_PATH "build/data/clinic.db"
#define CLINIC_MAX_EVENTS 32

static volatile sig_atomic_t stop_requested = 0;

/*
 * 每个客户端连接的服务器状态。
 * socket_fd 标识连接；frames 保存该客户端尚未拼成完整行的字节；next 用于连接链表。
 * 独立 frame buffer 可以防止 A 客户端的半包与 B 客户端的数据拼在一起。
 */
typedef struct clinic_connection
{
    clinic_socket_t socket_fd;
    clinic_frame_buffer_t frames;
    struct clinic_connection *next;
} clinic_connection_t;

static void request_server_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

/* 网络/分帧阶段已经能确定的错误，直接编码为统一 JSON 错误响应。 */
static int send_protocol_error(
    clinic_socket_t socket_fd,
    uint64_t request_id,
    const char *error_code,
    const char *message)
{
    char response[CLINIC_MAX_FRAME_SIZE + 128U];
    int length = clinic_protocol_encode_error(
        request_id,
        error_code,
        message,
        response,
        sizeof(response));

    return length < 0 ? -1 : clinic_net_send_all(socket_fd, response, (size_t)length);
}

/*
 * 此时 line 已经是一条完整消息。服务器把它交给 Handler 处理，再用 send_all 完整发回。
 * 本函数不知道请求是登录还是取号，也不直接接触 Core、Store 和 SQLite。
 */
static int handle_frame(
    ClinicServerHandler *handler,
    clinic_socket_t socket_fd,
    const char *line,
    size_t line_length)
{
    char response[CLINIC_MAX_FRAME_SIZE + 128U];
    size_t response_length = 0U;

    if (clinic_server_handler_handle_frame(
            handler,
            line,
            line_length,
            response,
            sizeof(response),
            &response_length) != 0)
    {
        return -1;
    }
    return clinic_net_send_all(
        socket_fd,
        response,
        response_length);
}

/*
 * 处理一个客户端的“可读”事件。由于 socket 是非阻塞的，需要循环 recv 到 EAGAIN：
 * 每批字节先 append，再循环 next，所以一次事件既能补齐半包，也能连续处理多个粘包。
 * recv==0 表示对端正常关闭；协议超长或收发失败则通知上层移除连接。
 */
static int handle_connection_read(
    ClinicServerHandler *handler,
    clinic_connection_t *connection)
{
    char receive_buffer[2048];
    char line[CLINIC_MAX_FRAME_SIZE + 1U];

    for (;;)
    {
        int received = recv(
            connection->socket_fd,
            receive_buffer,
            (int)sizeof(receive_buffer),
            0);

        if (received > 0)
        {
            size_t line_length;

            if (clinic_frame_buffer_append(
                    &connection->frames,
                    receive_buffer,
                    (size_t)received) != 0)
            {
                send_protocol_error(
                    connection->socket_fd,
                    0U,
                    "MESSAGE_TOO_LARGE",
                    "receive buffer exceeds 8192 bytes");
                return -1;
            }

            for (;;)
            {
                int frame_result = clinic_frame_buffer_next(
                    &connection->frames,
                    line,
                    sizeof(line),
                    &line_length);

                if (frame_result == CLINIC_FRAME_NEED_MORE)
                {
                    break;
                }

                if (frame_result == CLINIC_FRAME_TOO_LARGE)
                {
                    send_protocol_error(
                        connection->socket_fd,
                        0U,
                        "MESSAGE_TOO_LARGE",
                        "message exceeds 4096 bytes");
                    return -1;
                }

                if (handle_frame(
                        handler,
                        connection->socket_fd,
                        line,
                        line_length) != 0)
                {
                    return -1;
                }
            }

            continue;
        }

        if (received == 0)
        {
            return -1;
        }

        if (clinic_net_last_error_would_block())
        {
            return 0;
        }

        return -1;
    }
}

/* 新连接对象建立时同时初始化自己的分帧缓冲区。 */
static clinic_connection_t *create_connection(clinic_socket_t socket_fd)
{
    clinic_connection_t *connection = (clinic_connection_t *)calloc(1U, sizeof(*connection));
    if (connection == NULL)
    {
        return NULL;
    }

    connection->socket_fd = socket_fd;
    clinic_frame_buffer_init(&connection->frames);
    return connection;
}

static void destroy_connection(clinic_connection_t *connection)
{
    if (connection != NULL)
    {
        clinic_socket_close(connection->socket_fd);
        free(connection);
    }
}

static void remove_connection(
    clinic_connection_t **connections,
    clinic_connection_t *connection)
{
    clinic_connection_t **cursor = connections;

    while (*cursor != NULL)
    {
        if (*cursor == connection)
        {
            *cursor = connection->next;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void destroy_all_connections(clinic_connection_t *connections)
{
    while (connections != NULL)
    {
        clinic_connection_t *next = connections->next;
        destroy_connection(connections);
        connections = next;
    }
}

#ifndef _WIN32
/*
 * Linux 正式服务器事件循环。
 * epoll 中 data.ptr==NULL 专门代表监听 socket；非 NULL 则指向具体客户端连接。
 * 监听 socket 可读时循环 accept 到 EAGAIN，客户端可读时调用 handle_connection_read；
 * 断开或出错时先从 epoll/链表移除，再关闭 socket 和释放连接对象。
 */
static void run_epoll_server(
    clinic_socket_t listener,
    ClinicServerHandler *handler,
    const char *server_port)
{
    int epoll_fd = epoll_create1(0);
    struct epoll_event listener_event;
    struct epoll_event events[CLINIC_MAX_EVENTS];
    clinic_connection_t *connections = NULL;

    if (epoll_fd < 0)
    {
        perror("epoll_create1");
        return;
    }

    memset(&listener_event, 0, sizeof(listener_event));
    listener_event.events = EPOLLIN;
    listener_event.data.ptr = NULL;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener, &listener_event) != 0)
    {
        perror("epoll_ctl(ADD listener)");
        close(epoll_fd);
        return;
    }

    printf("clinic server listening on 0.0.0.0:%s (epoll)\n", server_port);
    fflush(stdout);

    while (!stop_requested)
    {
        int ready = epoll_wait(epoll_fd, events, CLINIC_MAX_EVENTS, -1);
        int index;

        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("epoll_wait");
            break;
        }

        for (index = 0; index < ready; ++index)
        {
            clinic_connection_t *connection = (clinic_connection_t *)events[index].data.ptr;

            if (connection == NULL)
            {
                for (;;)
                {
                    clinic_socket_t client_socket = accept(listener, NULL, NULL);
                    struct epoll_event client_event;

                    if (client_socket == CLINIC_SOCKET_INVALID)
                    {
                        if (clinic_net_last_error_would_block())
                        {
                            break;
                        }
                        perror("accept");
                        break;
                    }

                    if (clinic_net_set_nonblocking(client_socket) != 0)
                    {
                        clinic_socket_close(client_socket);
                        continue;
                    }

                    connection = create_connection(client_socket);
                    if (connection == NULL)
                    {
                        clinic_socket_close(client_socket);
                        continue;
                    }

                    memset(&client_event, 0, sizeof(client_event));
                    client_event.events = EPOLLIN | EPOLLRDHUP;
                    client_event.data.ptr = connection;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &client_event) != 0)
                    {
                        destroy_connection(connection);
                    }
                    else
                    {
                        connection->next = connections;
                        connections = connection;
                    }
                }
                continue;
            }

            if ((events[index].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0 ||
                handle_connection_read(handler, connection) != 0)
            {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connection->socket_fd, NULL);
                remove_connection(&connections, connection);
                destroy_connection(connection);
            }
        }
    }

    destroy_all_connections(connections);
    close(epoll_fd);
}
#else
/* Windows 没有 epoll，保留 select 分支用于主机兼容测试，不是 S5P6818 演示的正式服务路径。 */
static void run_select_server(
    clinic_socket_t listener,
    ClinicServerHandler *handler,
    const char *server_port)
{
    clinic_connection_t *connections[FD_SETSIZE] = {0};
    int index;

    printf("clinic server listening on 0.0.0.0:%s (select compatibility path)\n", server_port);
    fflush(stdout);

    while (!stop_requested)
    {
        fd_set read_set;
        int select_result;

        FD_ZERO(&read_set);
        FD_SET(listener, &read_set);

        for (index = 0; index < FD_SETSIZE; ++index)
        {
            if (connections[index] != NULL)
            {
                FD_SET(connections[index]->socket_fd, &read_set);
            }
        }

        select_result = select(0, &read_set, NULL, NULL, NULL);
        if (select_result < 0)
        {
            int error = WSAGetLastError();
            if (error == WSAEINTR)
            {
                continue;
            }
            fprintf(stderr, "select failed: %d\n", error);
            break;
        }

        if (FD_ISSET(listener, &read_set))
        {
            clinic_socket_t client_socket = accept(listener, NULL, NULL);
            if (client_socket != CLINIC_SOCKET_INVALID)
            {
                clinic_connection_t *connection;
                int slot;

                if (clinic_net_set_nonblocking(client_socket) != 0)
                {
                    clinic_socket_close(client_socket);
                    continue;
                }

                connection = create_connection(client_socket);
                if (connection == NULL)
                {
                    clinic_socket_close(client_socket);
                    continue;
                }

                for (slot = 0; slot < FD_SETSIZE - 1; ++slot)
                {
                    if (connections[slot] == NULL)
                    {
                        break;
                    }
                }

                if (slot < FD_SETSIZE - 1)
                {
                    connections[slot] = connection;
                }
                else
                {
                    destroy_connection(connection);
                }
            }
        }

        for (index = 0; index < FD_SETSIZE; ++index)
        {
            if (connections[index] != NULL &&
                FD_ISSET(connections[index]->socket_fd, &read_set) &&
                handle_connection_read(handler, connections[index]) != 0)
            {
                destroy_connection(connections[index]);
                connections[index] = NULL;
            }
        }
    }

    for (index = 0; index < FD_SETSIZE; ++index)
    {
        destroy_connection(connections[index]);
    }
}
#endif

static int create_directory_if_missing(const char *path)
{
#ifdef _WIN32
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0755);
#endif
    return result == 0 || errno == EEXIST ? 0 : -1;
}

static int ensure_default_database_directory(void)
{
    return create_directory_if_missing("build") == 0 &&
        create_directory_if_missing("build/data") == 0
        ? 0
        : -1;
}

/*
 * 服务器装配入口：解析端口和数据库路径 -> 初始化网络 -> 打开 SQLite Store -> 初始化 Core
 * -> 初始化 Handler -> 创建监听 socket -> 进入 epoll/select。任一步失败都走清理路径。
 */
int main(int argc, char **argv)
{
    const char *database_path;
    const char *server_port;
    clinic_socket_t listener;
    ClinicStore store;
    ClinicCore core;
    ClinicServerHandler handler;
    int exit_status = EXIT_FAILURE;

    if (argc > 3)
    {
        fprintf(stderr, "usage: %s [database_path] [port]\n", argv[0]);
        return EXIT_FAILURE;
    }
    database_path = argc == 2 ? argv[1] : CLINIC_DEFAULT_DATABASE_PATH;
    if (argc == 3)
    {
        database_path = argv[1];
    }
    server_port = argc == 3 ? argv[2] : CLINIC_SERVER_PORT;
    if (argc == 1 && ensure_default_database_directory() != 0)
    {
        fprintf(stderr, "could not create build/data directory\n");
        return EXIT_FAILURE;
    }

    clinic_store_init(&store);
    if (clinic_store_sqlite_open(&store, database_path) != CLINIC_STORE_OK)
    {
        fprintf(stderr, "could not open database: %s\n", database_path);
        return EXIT_FAILURE;
    }
    if (clinic_core_init(&core, &store) != 0 ||
        clinic_server_handler_init(&handler, &core) != 0)
    {
        fprintf(stderr, "could not initialize clinic core\n");
        (void)clinic_store_close(&store);
        return EXIT_FAILURE;
    }

    if (clinic_net_startup() != 0)
    {
        fprintf(stderr, "network startup failed\n");
        (void)clinic_store_close(&store);
        return EXIT_FAILURE;
    }

    if (clinic_net_create_listener(NULL, server_port, &listener) != 0)
    {
        fprintf(stderr, "could not listen on port %s\n", server_port);
        clinic_net_cleanup();
        (void)clinic_store_close(&store);
        return EXIT_FAILURE;
    }

    (void)signal(SIGINT, request_server_stop);
    (void)signal(SIGTERM, request_server_stop);
    printf("clinic database: %s\n", database_path);
    fflush(stdout);

#ifndef _WIN32
    run_epoll_server(listener, &handler, server_port);
#else
    run_select_server(listener, &handler, server_port);
#endif

    clinic_socket_close(listener);
    clinic_net_cleanup();
    if (clinic_store_close(&store) == CLINIC_STORE_OK)
    {
        exit_status = EXIT_SUCCESS;
    }
    return exit_status;
}
