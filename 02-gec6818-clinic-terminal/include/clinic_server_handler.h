/*
 * 文件作用（答辩）：声明服务器 Handler 的一帧请求/一帧响应接口。
 * Handler 持有 Core 指针，负责协议解码、调用 clinic_core_handle() 和响应编码；
 * socket 与 epoll 在 server/main.c，业务规则在 Core，SQL 在 Store/SQLite。
 */
#ifndef CLINIC_SERVER_HANDLER_H
#define CLINIC_SERVER_HANDLER_H

#include "clinic_core.h"

#include <stddef.h>

typedef struct ClinicServerHandler
{
    ClinicCore *core;
} ClinicServerHandler;

/* 绑定 Core，形成 Handler -> Core 的单向依赖。 */
int clinic_server_handler_init(
    ClinicServerHandler *handler,
    ClinicCore *core);

/* 输入不含换行的一帧 JSON，输出缓冲区中生成带换行的完整响应。 */
int clinic_server_handler_handle_frame(
    ClinicServerHandler *handler,
    const char *frame,
    size_t frame_length,
    char *output,
    size_t output_capacity,
    size_t *output_length);

#endif
