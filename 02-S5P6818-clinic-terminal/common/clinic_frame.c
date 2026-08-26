/*
 * 文件作用：解决 TCP 没有消息边界的问题。
 * recv 得到的字节先 append 到每连接缓冲区，next 再寻找换行符：没有换行表示半包，继续
 * 接收；一次收到多条表示粘包，只移出第一条并保留剩余数据供下一次处理。
 *
 * 模块同时处理 CRLF 和长度上限。单条消息超过 4096 字节或累计缓冲超过 8192 字节时
 * 返回超长错误，避免无限缓存；本文件不理解 JSON 内容和业务含义。
 *
 * 初学者理解：TCP 类似连续水流，recv( ) 只保证取到“一些字节”，不保证刚好取到发送方
 * 的一次 send( )。本模块相当于在水流中寻找换行分隔符，重新恢复应用层的一条条消息。
 */
#include "clinic_frame.h"

#include <string.h>

/* 新连接创建时从空缓冲开始；每个客户端必须拥有独立 buffer，不能互相混用。 */
void clinic_frame_buffer_init(clinic_frame_buffer_t *buffer)
{
    if (buffer != NULL)
    {
        buffer->length = 0U;
    }
}

/* 把本次 recv 的字节接到旧数据后面；容量不足立即拒绝，避免覆盖内存。 */
int clinic_frame_buffer_append(
    clinic_frame_buffer_t *buffer,
    const char *data,
    size_t length)
{
    if (buffer == NULL || data == NULL ||
        length > sizeof(buffer->data) - buffer->length)
    {
        return CLINIC_FRAME_TOO_LARGE;
    }

    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    return 0;
}

/*
 * 从累计字节中寻找第一处 '\n'：
 * - 找不到：属于半包，返回 NEED_MORE，缓冲内容原样保留；
 * - 找到：复制第一帧，并用 memmove 保留后面的粘包数据；
 * - 长度超限：返回 TOO_LARGE，不把危险数据交给 JSON 层。
 */
int clinic_frame_buffer_next(
    clinic_frame_buffer_t *buffer,
    char *line,
    size_t line_capacity,
    size_t *line_length)
{
    size_t newline_index = 0U;
    size_t frame_length;

    if (buffer == NULL || line == NULL || line_length == NULL)
    {
        return CLINIC_FRAME_TOO_LARGE;
    }

    while (newline_index < buffer->length &&
           buffer->data[newline_index] != '\n')
    {
        ++newline_index;
    }

    if (newline_index == buffer->length)
    {
        return buffer->length > CLINIC_MAX_FRAME_SIZE
            ? CLINIC_FRAME_TOO_LARGE
            : CLINIC_FRAME_NEED_MORE;
    }

    frame_length = newline_index;
    if (frame_length > 0U && buffer->data[frame_length - 1U] == '\r')
    {
        --frame_length;
    }

    if (frame_length + 1U > line_capacity ||
        frame_length > CLINIC_MAX_FRAME_SIZE)
    {
        return CLINIC_FRAME_TOO_LARGE;
    }

    memcpy(line, buffer->data, frame_length);
    line[frame_length] = '\0';
    *line_length = frame_length;

    memmove(
        buffer->data,
        buffer->data + newline_index + 1U,
        buffer->length - newline_index - 1U);
    buffer->length -= newline_index + 1U;

    return CLINIC_FRAME_READY;
}
