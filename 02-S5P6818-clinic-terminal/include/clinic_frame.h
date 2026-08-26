/*
 * 文件作用：声明 TCP 换行分帧缓冲区和返回状态。
 * 每个连接保存独立 clinic_frame_buffer_t，append 累积 recv 字节，next 提取完整消息，
 * 从而统一处理半包、粘包、CRLF 和超长输入。
 */
#ifndef CLINIC_FRAME_H
#define CLINIC_FRAME_H

#include <stddef.h>

#include "clinic_protocol.h"

typedef struct clinic_frame_buffer
{
    char data[CLINIC_RECV_BUFFER_SIZE];
    size_t length;
} clinic_frame_buffer_t;

typedef enum clinic_frame_result
{
    CLINIC_FRAME_NEED_MORE = 0,
    CLINIC_FRAME_READY = 1,
    CLINIC_FRAME_TOO_LARGE = -1
} clinic_frame_result_t;

/* 新客户端连接建立时调用。 */
void clinic_frame_buffer_init(clinic_frame_buffer_t *buffer);

/* 累积本次 recv 字节；缓冲总量不能超过 CLINIC_RECV_BUFFER_SIZE。 */
int clinic_frame_buffer_append(
    clinic_frame_buffer_t *buffer,
    const char *data,
    size_t length);

/* 尝试提取第一条换行终止消息；NEED_MORE 时原数据保留。 */
int clinic_frame_buffer_next(
    clinic_frame_buffer_t *buffer,
    char *line,
    size_t line_capacity,
    size_t *line_length);

#endif
