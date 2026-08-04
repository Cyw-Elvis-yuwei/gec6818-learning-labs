# 医路通稳定架构

## 中文判断提示

- 当前状态：稳定架构已纳入板端公共 TCP 传输模块和排队页 5 秒自动刷新。
- 这是什么意思：登录、科室、医生和号单客户端只处理各自 JSON 与业务结果，共用一套连接、截止时间、收发和换行分帧实现；排队页定时器只在 LVGL 主线程触发已有查询入口。
- 是否还需要用户提供信息：不需要；验证结论以 `docs/TEST_STATUS.md` 为准。
- 建议下一步：保持现有稳定边界，进入项目完成材料收口。
- 还缺什么：无号单及 `COMPLETED` / `CANCELLED` 状态的独立开发板证据仍待补充。

## 架构链路

以下链路描述项目稳定的职责边界；具体实现和验证状态以项目状态与测试状态文档为准。

```text
客户端/LVGL
→ 网络工作线程
→ UTF-8 单行 JSON + 换行
→ Linux epoll
→ frame
→ handler
→ JSON
→ clinic_core_handle
→ store
→ SQLite
```

## 分层职责

| 层 | 稳定职责 |
| --- | --- |
| 客户端/LVGL | 收集用户意图、显示状态与结果，不持有服务器端数据规则 |
| 网络工作线程 | 执行连接和阻塞式网络操作，通过线程安全机制与 UI 交换请求和结果 |
| 板端 `board_transport` | 为同步业务客户端统一管理连接、总截止时间、完整发送、接收和换行分帧，不解析业务 JSON |
| 单行 JSON 协议 | 使用 UTF-8 JSON，并以换行作为一帧结束标记 |
| Linux epoll | 管理监听 socket 和多个客户端连接，为每个连接维护独立接收状态 |
| frame | 从连接缓冲区提取完整帧，隔离半包、连续帧和超长输入 |
| handler | 连接协议适配与业务入口，组织解码、Core 调用和响应编码 |
| JSON | 在 JSON 与 `ClinicRequest`/`ClinicResponse` 之间转换 |
| `clinic_core_handle()` | 处理结构化请求、业务校验、Store 调用和稳定错误映射 |
| store | 提供与具体存储实现解耦的用户、科室、医生和取号数据接口 |
| SQLite | 服务器端持久化实现，负责数据库约束、prepared statement 和事务 |

## 不可破坏的原则

1. LVGL 回调不得直接执行阻塞 socket 操作；网络访问由独立工作线程承担。
2. JSON 层只负责协议转换，不执行 SQL，也不复制业务规则。
3. Core 不依赖 TCP、socket、epoll 或 cJSON，只处理结构化类型和 Store 接口。
4. Store 不依赖网络、JSON、Handler 或 LVGL。
5. SQLite 只存在于服务器端；GEC6818 客户端不直接打开业务数据库。
6. 响应负载通过 `ClinicResponseKind` 显式区分，不根据 ID、字段是否非零或消息文字推断类型。
7. 单条协议消息上限为 4096 字节，每连接接收缓冲区为 8192 字节；修改协议时不得悄然放宽这两个边界。
8. VMware Ubuntu 是主机构建、测试和服务器环境，不是 GEC6818 开发板，主机结果不能标记为板端验证。
9. `reference/six/six` 只是旧 LVGL 参考工程，不是当前医疗项目源码，也不是当前运行依赖。
10. `reference/gec6818_platform_kit` 是跨项目复用的平台资料、风险和探针集合，不是可直接运行的项目。

## 当前实现边界

- 服务器侧既有协议、epoll、Handler、Core、Store 和 SQLite 分层已经形成。
- 注册登录、科室和医生已有真实 TCP/主机客户端链路，其中板端真实 login TCP 请求已由用户实机验证完成。
- `get_ticket` 已完成 Handler、JSON、SQLite、真实 TCP 和主机客户端闭环。
- `get_current_ticket` 已完成按 `user_id` 查询当天最新号单及排队摘要的 JSON、Core、Store、SQLite 和主机客户端闭环；开发板实机结论以测试状态文档为准。
- `call_next` 已完成 Handler、JSON、SQLite、真实 TCP 和主机客户端闭环。
- 客户端/LVGL 和网络工作线程是稳定目标架构的一部分，登录链路已按该边界落地并完成实机验证。
- 登录成功后的医疗服务主页骨架、科室列表、医生列表、科室真实取号、板端当前号单查询以及排队状态自动/手动刷新与 `CALLED` 结果展示均已完成实机验证；真实 `authenticated_user_id` 已贯穿页面切换、取号和当前号单查询。

## 板端公共网络传输边界

- `board_transport` 提供一次同步 request/response exchange；它不创建线程，调用方仍是 `main.c` 管理的既有网络 worker。
- 单次 exchange 使用同一个 `CLOCK_MONOTONIC` 总截止时间，连接、发送和接收不会分别重新获得完整超时时间。
- 传输层只接收数值 IPv4 地址，统一使用非阻塞 socket、`poll`、完整发送和 `clinic_frame` 换行分帧。
- `login_client`、`department_client`、`doctor_client` 和 `ticket_client` 继续持有各自请求编码、严格响应解析、`request_id` 校验和业务错误分类。
- 传输层不依赖 LVGL、cJSON、Core、Store 或 SQLite；后台网络线程仍不得调用任何 `lv_*` API。
- 4096 字节消息上限和 8192 字节接收缓冲边界继续由公共 frame 模块定义，传输层不另建一套边界。

## 登录链路

```text
LVGL 登录按钮
→ 主线程复制输入并设置 RUNNING
→ pthread 后台线程
→ login_client 同步网络接口
→ board_transport
→ clinic_net / clinic_frame
→ clinic_server
→ SQLite
→ 登录结果与成功响应中的 user_id 写入共享状态
→ LVGL 主线程 join 并显示消息框
```

### 模块职责

- `main.c`：负责登录 UI、线程状态管理、成功响应 `user_id` 保存、主线程结果显示和登录页到主页的 screen 切换。
- `login_client`：负责同步 TCP 登录请求和响应分类。
- `home_page`：负责主页 UI、真实 `user_id` 显示、三种服务流程入口、当前号单请求回调和退出登录交互，不直接执行网络请求。
- `cJSON`：项目内使用 1.7.19，`clinic_terminal` 直接编译 `third_party/cjson/cJSON.c`，不依赖外部 `libcjson`。
- 服务器：负责 SQLite 认证。

### 依赖边界

- 后台线程不得调用 `lv_*` API。
- `login_client` 不依赖 LVGL。
- 板端不链接 SQLite。
- `clinic_terminal` 产物为 32-bit ARM EABI5，解释器为 `/lib/ld-linux.so.3`，`RPATH=/IOT`。
- `NEEDED` 包含 `libpthread.so.0`，不包含 `libcjson`。
- 用户主动开启“记住密码”后，板端凭据可写入 `/IOT/.clinic_terminal_credentials`，权限限定为 `0600`；关闭记住并成功登录后删除，不宣称硬件级加密。
- UI 不直接执行阻塞 `connect`、`send` 或 `recv`。

## 页面切换链路

```text
登录页 screen
→ 登录成功消息框确认
→ 主循环处理切换标记
→ 创建主页 screen
→ 传入 authenticated_user_id
→ 加载主页
→ 删除旧登录 screen
```

### 主页模块边界

- `home_page` 不访问网络或 SQLite，也不依赖 `login_client` 内部状态。
- 主页创建接口接收目标 screen、字体、登录成功后保存的 `authenticated_user_id`、科室请求回调和当前号单请求回调；网络状态仍由 `main.c` 管理。
- 登录页键盘、拼音候选栏、输入框和页面事件随旧登录 screen 删除，不进入主页对象树。
- `ClinicServiceFlow` 将主页入口区分为科室查询、医生查询和门诊取号；三者只复用科室列表请求，进入科室页后的行为彼此独立。
- 科室查询只显示科室名称和编号；医生查询选择科室后才请求医生；门诊取号选择科室并确认后直接提交 `user_id` 与 `department_id`，不经过医生页。
- 任一主页网络请求启动后禁用其他主页导航入口；流程模式随请求上下文保存，快速连点不能改变正在执行请求的业务语义。
- 主页和科室页使用独立 screen；切换前清理旧页面对象指针，登录页对象不会重新进入后续页面。

## 科室列表链路

```text
主页
→ 科室请求回调
→ main.c 设置 RUNNING 并启动 pthread
→ department_client 同步网络接口
→ board_transport
→ clinic_net / clinic_frame
→ clinic_server / SQLite
→ 科室结果写入共享状态
→ LVGL 主线程 join
→ 创建 department_page
```

### 模块职责

- `main.c`：管理科室请求线程状态、防重复提交、主线程结果处理、主页与科室页 screen 切换及退出时线程回收。
- `department_client`：构造真实 `list_departments` 请求，执行同步 TCP 通信，解析并分类有界科室响应。
- `department_page`：显示服务器返回的科室名称和 ID；科室查询模式弹出科室信息，医生查询模式把科室交给医生请求，门诊取号模式先确认再把科室交给取号请求。
- `clinic_server` / SQLite：查询并返回真实科室数据，板端不复制服务器数据规则。

### 依赖边界

- `department_client` 不依赖 LVGL，不访问 SQLite；调用它的后台线程不得调用任何 `lv_*` API。
- `department_page` 不访问网络或 SQLite，只接收主线程传入的有界科室结果。
- LVGL 主线程不执行阻塞 `connect`、`send` 或 `recv`，只在后台线程完成后执行 `join` 和页面更新。
- 科室页面返回时由主循环重建主页，并继续传入已认证的 `authenticated_user_id`，不重新登录。
- 科室列表使用可滚动容器；当前 5 条真实数据未触发溢出，实际滚动交互的验证边界记录在测试状态文档中。

## 医生列表链路

```text
科室页面选择科室
→ 回调传递 department_id 和科室名称
→ main.c 创建医生请求线程
→ doctor_client
→ board_transport
→ clinic_net / clinic_frame
→ clinic_server / SQLite
→ 医生结果写入共享状态
→ LVGL 主线程 join
→ 创建 doctor_page
```

### 模块职责

- `main.c`：保存选中的 `department_id` 和科室名称，管理医生请求线程、主线程结果处理、医生页切换及退出时线程回收。
- `doctor_client`：使用真实 `department_id` 构造同步 TCP 请求，解析并分类有界医生响应，不管理页面对象。
- `doctor_page`：只负责显示筛选科室以及服务器返回的医生姓名、职称和专长；点击医生仅显示详情，不拥有取号回调。
- `clinic_server` / SQLite：按真实 `department_id` 查询并返回医生数据。

### 依赖边界

- `doctor_client` 不依赖 LVGL，不访问 SQLite；调用它的后台线程不得调用任何 `lv_*` API。
- `doctor_page` 不访问网络或 SQLite，只接收主线程传入的医生结果和当前科室信息。
- 医生页返回时使用已保留的科室缓存重建科室页，不重新登录，也不重新请求科室数据；`authenticated_user_id` 继续保留。
- 医生页没有取号入口，也不向取号上下文传递医生 ID 或姓名。

## 科室真实取号链路

```text
门诊取号页面选择科室并确认
→ 获取 department_id
→ main.c 补充 authenticated_user_id
→ pthread 后台线程
→ ticket_client
→ board_transport / clinic_net / clinic_frame
→ create_ticket
→ clinic_server / SQLite
→ 取号结果写入共享状态
→ LVGL 主线程 join
→ ticket_page
```

### 协议与模块边界

- `create_ticket` 只发送 `request_id`、`user_id` 和 `department_id`；协议不包含 `doctor_id`，服务器号单也不返回 `doctor_id`。
- 板端 `TicketRequestContext` 不保存 `doctor_id` 或医生姓名；取号链路从科室页直接开始。
- `main.c` 从登录状态补充真实 `authenticated_user_id`，管理取号线程、主线程结果处理和页面切换。
- `ticket_client` 构造同步 `create_ticket` 请求并解析真实号单响应，不依赖 LVGL，也不访问 SQLite。
- `ticket_page` 展示服务器真实号单字段；科室名称来自此前服务器科室数据对应的 `department_id`，页面不显示医生姓名或医生 ID。
- `clinic_server` / SQLite 按真实 `user_id` 和 `department_id` 创建并返回号单，板端不复制服务器业务规则。
- 返回主页时保留 `authenticated_user_id`，不自动重新请求科室。
- 该板端取号链路不负责当前号单查询、排队状态自动刷新或叫号后的界面更新。

## 当前号单与排队摘要后端查询链路

```text
host_current_ticket_client
→ get_current_ticket(user_id)
→ UTF-8 JSON + 换行 / Linux epoll / Handler
→ clinic_core_handle
→ clinic_store_get_current_ticket
→ SQLite 查询当天最新号单
→ 聚合同科室当天最近一次 CALLED 与前方 WAITING 数量
→ Ticket + QueueSummary 响应
```

### 协议与存储边界

- `get_current_ticket` 是使用 `user_id` 的独立请求；既有 `get_ticket(ticket_id)` 和 `TICKET_NOT_FOUND` 保持原语义。
- SQLite 使用与 `create_ticket` 一致的本地当天 `service_date` 规则，以参数绑定查询 `user_id` 和日期，并按 `id DESC LIMIT 1` 返回最新一条。
- 查询不过滤 `WAITING`、`CALLED`、`COMPLETED` 或 `CANCELLED`，也不定义额外的“活跃号单”语义。
- 成功响应继续使用 `CLINIC_RESPONSE_TICKET`，并在 `get_current_ticket` 成功响应中增加严格的 `queue_summary` 对象；当前叫号按同科室、同日期 `CALLED` 的 `called_time DESC, id DESC` 取最近一条，无叫号传输 JSON `null`。
- `waiting_ahead_count` 只统计同科室、同日期、号码更小且状态为 `WAITING` 的号单；本人不是 `WAITING` 时为 0。
- Store 查询失败时同时清零 `ClinicTicket` 和 `ClinicQueueSummary`；无当日号单时返回 `CURRENT_TICKET_NOT_FOUND`。
- 该查询不需要数据库迁移，不修改现有表结构。
- `get_current_ticket` 请求格式不变；`create_ticket`、`get_ticket` 和 `call_next` 的响应结构不增加排队摘要。5 秒自动刷新属于板端页面行为，服务器推送仍不属于当前实现。

## 板端当前号单查询链路

```text
主页排队查询
→ main.c 创建 pthread 后台线程
→ ticket_client get_current_ticket
→ board_transport
→ clinic_net / clinic_frame
→ clinic_server / SQLite
→ Ticket 与 QueueSummary 查询结果写入共享状态
→ LVGL 主线程 join
→ queue_page 显示本人号码、当前叫号与前方等待人数
```

### 页面与依赖边界

- 主页只通过回调通知 `main.c` 发起查询；阻塞式连接、发送和接收均由后台线程执行。
- `ticket_client` 对 `get_current_ticket` 严格解析 `queue_summary`，并校验响应用户与 `authenticated_user_id` 一致；不依赖 LVGL，也不访问 SQLite。
- 只有服务器错误码精确等于 `CURRENT_TICKET_NOT_FOUND` 时才映射为无号单，其他服务器错误不改变含义。
- `queue_page` 只展示主线程传入的真实 Ticket 与 QueueSummary 字段，不访问网络或 SQLite；本人非 `WAITING` 时显示无需等待。
- 返回主页时删除旧排队页面并保留 `authenticated_user_id`，不重新登录。
- 当前支持从主页发起查询，以及在排队页面每 5 秒自动刷新和用户手动刷新；不包含服务器推送。

## 排队状态自动/手动刷新与叫号结果展示链路

```text
Ubuntu 管理端
→ call_next(department_id)
→ 查询当天该科室最早的 WAITING 号单（queue_number ASC, id ASC）
→ 在事务中原子更新为 CALLED 并写入 called_time
→ 按 ID 重新读取并返回更新后的真实 Ticket
→ 板端 queue_page 每 5 秒定时触发或用户点击刷新
→ main.c 复用当前号单后台线程
→ get_current_ticket(user_id)
→ LVGL 主线程原位更新 queue_page
```

### call_next 稳定语义与边界

- Ubuntu 管理端使用 `clinic_admin_call_client` 执行 `call_next(department_id)`；板端不发送 `call_next`，也不提供叫号按钮。
- `call_next` 每次只以指定科室当天仍为 `WAITING` 的号单为候选，并按 `queue_number ASC, id ASC` 选择最早一张。
- 选中后在 `BEGIN IMMEDIATE` 事务内执行 `WAITING` → `CALLED`、写入 `called_time`，按 ID 重新读取并返回更新后的真实 Ticket。
- 没有 `WAITING` 号单时返回既有无等待号单错误。
- 不得优先返回已有 `CALLED` 号单，也不得把 `call_next` 解释成重复返回旧叫号结果。
- 本次选择逻辑修复不改变协议字段、Core 路由、Ticket JSON 结构、状态枚举或 SQLite 表结构。
- `queue_page` 通过回调通知 `main.c` 发起刷新，不直接访问网络或 SQLite。
- 刷新期间禁用刷新和返回操作，避免在线程运行期间删除正在使用的页面对象。
- 刷新成功后由 LVGL 主线程原位更新号单字段；网络错误保留上一次有效号单详情并恢复按钮。
- 刷新成功后同时原位更新当前叫号和前方等待人数；网络错误保留上一次有效号单及摘要并恢复按钮。
- `queue_page` 拥有页面私有的 LVGL 5 秒定时器；定时器运行在主线程，只复用刷新回调，不直接进行 TCP/JSON 通信。
- 请求开始时暂停定时器；主线程 `join` 并处理结果后重置、恢复定时器，避免多个当前号单 worker 并发运行。
- 返回主页和页面清理路径先删除定时器并清空指针，避免页面删除后仍回调旧对象。
- 当前更新方式是 5 秒自动刷新加用户手动刷新；不包含服务器推送。

## 变更约束

- 新业务应沿现有层次逐层接入，不在 Handler 中写 SQL，不在 JSON 中实现业务。
- 新响应负载应增加明确的 `ClinicResponseKind`，并保持失败响应不携带陈旧负载。
- 任何 GEC6818、Framebuffer、触摸、ABI 或工具链结论都必须引用真实探针证据，不能由 VMware 或旧工程推断。
