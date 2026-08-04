# ADR-0001：服务器采用 Handler、Core、Store、SQLite 分层

- 状态：Accepted
- 日期：2026-07-23
- 范围：Ubuntu 服务器业务链路

## 背景

项目同时包含 TCP/JSON 协议、医疗排队业务规则和 SQLite 数据访问。如果这些职责集中在 socket 回调或单个业务函数中，协议错误、业务规则和数据库细节会互相耦合，难以单独测试和维护。

## 决策

采用以下稳定分层：

```text
epoll/socket
  → Handler
  → clinic_core_handle()
  → Store 接口
  → SQLite 实现
```

- Handler 负责请求 JSON 的严格解析、响应编码和协议错误。
- Core 负责按 `ClinicRequest.type` 分发业务、校验业务参数、调用 Store 和映射稳定错误。
- Store 提供用户、科室、医生、号单和管理台数据接口。
- `clinic_store_sqlite.c` 是当前唯一直接执行 SQL 的实现。
- Core 不依赖 socket、epoll、cJSON 或 sqlite3；Store 不依赖 LVGL 和网络。

## 结果

- 可以对 JSON、Handler、Core 和 Store 分层测试。
- 更换数据存储实现时，Core 的业务接口不需要跟着改写。
- SQLite 集中在服务器端，开发板只通过 TCP/JSON 获取业务结果。
- 排查问题时可以沿着“协议 → 业务 → 数据访问”逐层定位。

## 未选择的方案

- 不让 Handler 直接拼 SQL。
- 不让板端直接访问 SQLite 文件。
- 不把业务规则复制到 JSON 解析器或 LVGL 页面。

## 依据

- `server/clinic_server_handler.c`
- `core/clinic_core.c`
- `include/clinic_store.h`
- `store/clinic_store.c`
- `store/clinic_store_sqlite.c`
