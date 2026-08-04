# ADR-0003：TCP 使用单行 JSON 和换行分帧

- 状态：Accepted
- 日期：2026-07-23
- 范围：板端、主机客户端和 Ubuntu 服务器公共协议

## 背景

TCP 只提供有序字节流，没有“一次 send 对应一次 recv”的消息边界。一次请求可能被拆成多个 `recv`，多个请求也可能合并到一次 `recv`，即半包和粘包。

## 决策

- 每条应用层消息使用 UTF-8 JSON。
- JSON 文本后追加 `\n`，换行表示一条消息结束。
- 每条协议消息上限为 4096 字节，每个连接接收缓冲区上限为 8192 字节。
- `clinic_frame` 负责缓存、寻找换行、处理 CRLF、拆出完整帧和拒绝超长输入。
- JSON 层负责字段、类型、重复字段、整数范围和额外字段校验，不负责 socket 收发。

## 结果

- 半包会留在连接缓冲区，继续接收后再解析。
- 粘包会一次取出一帧，剩余数据留给下一次处理。
- Handler 收到的是完整 JSON 文本，Core 不需要知道 TCP 的分段细节。
- 固定边界限制了异常输入占用的内存。

## 未选择的方案

- 不把一次 `send()` 当成一条完整消息。
- 不在每个业务客户端重复实现一套分帧逻辑。
- 不让 JSON 解析器直接读取 socket。

## 依据

- `common/clinic_frame.c`
- `common/clinic_protocol.c`
- `common/clinic_json.c`
- `board/clinic_terminal/board_transport.c`
- `server/main.c`
- `docs/ARCHITECTURE.md`
