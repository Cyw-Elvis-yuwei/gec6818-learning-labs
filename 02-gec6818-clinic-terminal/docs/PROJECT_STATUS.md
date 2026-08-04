# 医路通项目状态

## 中文判断提示

- 当前状态：项目完成边界审查与最终验收材料已整理，等待用户确认当前范围是否收口。
- 这是什么意思：核心演示、最新入口语义反馈和排队页定时器生命周期均已记录；本阶段只收口材料，不新增功能。
- 是否还需要用户提供信息：需要用户确认最终验收边界；若接受当前范围，不再进入新的代码阶段。
- 建议下一步：用户确认最终验收边界与项目收口。
- 还缺什么：无号单、`COMPLETED`、`CANCELLED`、重复取号和超一屏滚动仍未专项验证；部分用户确认未保留原始命令、退出码和逐项日志。

## 项目身份

- 项目名称：医路通
- 项目全称：基于 GEC6818 的联网智慧医疗自助服务终端
- 更新时间：2026-07-24 20:17（Asia/Hong_Kong）
- 状态用途：供后续 Codex 任务直接恢复项目进度，不以旧聊天记录作为事实来源。

## 当前演示目标

目标是在 GEC6818 终端与 Ubuntu 服务器之间形成可演示的医疗自助服务闭环：用户注册或登录，浏览科室和医生，按科室取号，查询排队状态，管理员叫号后由终端刷新结果。

这是演示目标，不代表所有环节已经实现。系统仅演示医疗服务流程，不提供诊断、处方或医疗建议。

## 已完成模块

| 模块 | 状态 | 已有证据与边界 |
| --- | --- | --- |
| UTF-8 单行 JSON 协议与换行分帧 | 当前阶段完成 | 已在取号加入前的 19 项全量回归中验证 |
| Linux epoll 服务器 | 当前阶段完成 | 已验证半包、连续帧、断开恢复等既有服务器行为 |
| SQLite 注册与登录 | 当前阶段完成 | 既有真实 TCP 与主机端回归已通过 |
| 科室列表 | 当前阶段完成 | Store、Core、JSON、Handler、真实 TCP 与主机客户端已完成 |
| 医生列表 | 当前阶段完成 | Store、Core、JSON、Handler、真实 TCP 与主机客户端已完成 |
| 取号数据模型与 SQLite Store | 当前阶段完成 | `test_ticket_store` 及 `test_doctor_store` 窄范围回归通过 |
| 取号 Core | 当前阶段完成 | `test_ticket_core` 及 `test_doctor_core` 窄范围回归通过 |
| 取号 JSON | 当前阶段完成 | `test_ticket_json` 及 `test_doctor_json` 窄范围回归通过 |
| 取号离线 Handler | 当前阶段完成 | `test_ticket_handler` 通过，未涉及 socket 或 epoll |
| 取号真实 TCP | 当前阶段完成 | `test_tcp_ticket` 经真实 epoll 服务器和 TCP 连接运行通过 |
| 取号主机客户端 | 当前阶段完成 | `clinic_ticket_client` 实际构建，并由 TCP 测试 fork/exec 验证 |
| 取号完整主机端链路 | 当前阶段完成 | 客户端→TCP→epoll→Handler→Core→Store→SQLite 闭环通过 |
| get_ticket Store | 当前阶段完成 | `clinic_store_get_ticket()` 按 ticket ID 返回完整记录，定向测试已通过 |
| get_ticket Core | 当前阶段完成 | `CLINIC_REQ_GET_TICKET` 分发、响应复用及错误映射已通过定向测试 |
| get_ticket JSON | 当前阶段完成 | 严格请求解码和现有 ticket 响应编码复用已通过定向测试 |
| get_ticket 离线 Handler | 当前阶段完成 | 通用 Handler 无需修改，完整离线查询链路已通过定向测试 |
| get_ticket 真实 TCP | 当前阶段完成 | `test_tcp_ticket` 已通过真实 epoll 服务器完成状态查询 |
| get_ticket 主机查询客户端 | 当前阶段完成 | `clinic_ticket_status_client` 已实际构建并由 TCP 测试验证 |
| get_ticket 完整主机端链路 | 当前阶段完成 | 查询客户端→TCP→epoll→Handler→Core→SQLite→ticket JSON response 闭环通过 |
| get_current_ticket 后端协议与主机 TCP 闭环 | 当前阶段完成 | `build_exit=0`、`full_test_exit=0`，成功与 `CURRENT_TICKET_NOT_FOUND` 真实 TCP 分支均已由用户验证 |
| 管理员 call_next Store | 当前阶段完成 | `clinic_store_call_next()` 已实现，定向 Store 测试及医生 Store 回归已通过 |
| 管理员 call_next Core | 当前阶段完成 | `CLINIC_REQ_CALL_NEXT` 已接入 Core，ticket Core 及医生 Core 定向回归已通过 |
| 管理员 call_next JSON | 当前阶段完成 | `call_next` 请求已严格解码，ticket JSON 及医生 JSON 定向回归已通过 |
| 管理员 call_next Handler 离线链路 | 当前阶段完成 | 现有通用 Handler 分发链路直接支持 `call_next`，ticket 与医生 Handler 定向验证已通过 |
| 管理员 call_next 真实 TCP 与 CLI | 当前阶段完成 | `clinic_admin_call_client` 已通过真实 epoll server 完成管理员叫号闭环 |
| Ubuntu 管理端交互式叫号菜单 | 当前阶段完成 | 用户确认无参数菜单验证通过；动态科室、数字叫号、`r` 刷新和 `q` 退出的逐条日志及退出码未保留 |
| call_next WAITING 号单选择修复 | 当前阶段完成 | 旧 `CALLED` 幂等返回语义已删除；用户确认 `build_exit=0`、`full_test_exit=0`，`ticket.id=4` 正确由 `WAITING` 更新为 `CALLED`，`get_current_ticket` 与板端显示一致 |
| 主机端最终回归 | 当前阶段完成 | 全部 Makefile 聚合目标及 ticket Handler/TCP 补充测试在主机后端功能完成后通过 |
| GEC6818 实机环境探针 | 当前阶段完成 | 已确认板端架构、内核、glibc、动态加载器、framebuffer、触摸节点、字体路径和 IP |
| 最终交叉工具链确认 | 当前阶段完成 | 已选定 ARM GCC 5.4.0 工具链，并由板端 ABI 探针验证产物可运行 |
| GEC6818 最小 LVGL framebuffer 显示 | 当前阶段完成 | LVGL 8.3.0 程序已交叉构建并通过 `/dev/fb0` 实机显示与退出验证 |
| GEC6818 最小 LVGL 触摸输入 | 当前阶段完成 | `gslX680` 经 `/dev/input/event0`、evdev 和 LVGL pointer input 触发 `LV_EVENT_CLICKED`，实机点击验证通过 |
| GEC6818 LVGL 中文字体最小验证 | 当前阶段完成 | `/font/simkai.ttf` 经 LVGL 8.3 FreeType 支持清晰显示中文，并与 framebuffer、触摸输入同时稳定工作 |
| GEC6818 板端最小 TCP 网络 | 当前阶段完成 | ARM 网络探针已连接 Ubuntu epoll `clinic_server`，完成换行 ping/pong JSON 并以 `probe_exit=0` 退出 |
| GEC6818 LVGL 后台网络线程 | 当前阶段完成 | LVGL 主线程、后台 pthread、互斥状态交换和线程回收已通过成功、失败、连续重试及退出实机验证 |
| GEC6818 LVGL 医疗登录页骨架 | 当前阶段完成 | 用户已在 GEC6818 实机确认中文表单、双键盘切换、本地校验模态提示和退出流程按既定标准通过 |
| 板端真实 login TCP 请求 | 当前阶段完成 | 用户已在 GEC6818 实机验证真实 login 请求、错误密码拒绝、正确密码成功、离线恢复重试和正常退出 |
| GEC6818 注册 → 返回登录 → 登录 → 主页认证链路 | 当前阶段完成 | 用户已在 GEC6818 实机确认注册成功返回登录、用户名回填、密码清空、登录成功进入主页及长输入和字段切换 |
| clinic_terminal 板端构建与 cJSON 依赖 | 当前阶段完成 | 直接编译项目内 cJSON 1.7.19；产物为 ARM EABI5，使用 `/IOT` RPATH 和 pthread，不依赖外部 libcjson |
| GEC6818 LVGL 医疗服务主页骨架 | 当前阶段完成 | 用户已在 GEC6818 实机确认登录成功后进入主页、显示真实 `user_id=1`、四个入口当时的占位交互可用且登录页对象无残留 |
| GEC6818 三种服务入口语义拆分 | 局部验证 | 科室查询、医生查询、门诊取号使用显式流程模式；用户反馈最新统一产物演示检查通过，但未保留逐项 GEC6818 点击日志，不升级为完整专项证据 |
| GEC6818 科室列表真实请求与展示 | 当前阶段完成 | 用户已在 GEC6818 实机确认真实 `list_departments` 请求、SQLite 科室名称与 ID 展示、离线恢复、返回主页和正常退出 |
| GEC6818 医生列表真实请求与展示 | 当前阶段完成 | 用户已在 GEC6818 实机确认真实 `department_id` 请求、SQLite 医生数据展示、离线恢复、缓存返回科室页和正常退出 |
| GEC6818 医生选择后的科室真实取号 | 当前阶段完成 | 用户已在 GEC6818 实机确认真实 `create_ticket` 请求、SQLite 号单写入、真实号单展示、离线恢复和返回主页 |
| GEC6818 板端当前号单查询与排队状态展示 | 当前阶段完成 | 用户已在 GEC6818 实机确认真实 `get_current_ticket` 请求、`WAITING` 中文展示、离线恢复、返回主页和正常退出 |
| GEC6818 排队状态手动刷新与叫号结果展示 | 当前阶段完成 | 用户已在 GEC6818 实机确认手动刷新、管理员 `call_next(department_id)` 后的 `CALLED` 中文展示、真实 `called_time`、离线恢复和正常退出 |
| GEC6818 排队信息增强：当前叫号与前方 `WAITING` 人数 | 当前阶段完成 | 用户确认部署运行后当前叫号、前方人数、手动刷新和人数每次减少 1 的本轮检查全部通过 |
| GEC6818 排队页 5 秒自动刷新 | 当前阶段完成 | 用户明确反馈“验证也通过了，自动轮询”；未保留本轮构建命令、退出码和逐项板端日志 |
| 排队页定时器生命周期封装 | 当前阶段完成 | `queue_page.c` 仅集中定时器暂停、重置恢复和销毁路径；Ubuntu/ARM 构建回传 `clean=0`、`board_build=0`、`artifact=0`，用户随后确认 GEC6818 实机生命周期回归通过；未保留原始逐项日志 |
| 板端公共 TCP 传输模块 | 当前阶段完成 | `login_client`、`department_client`、`doctor_client`、`ticket_client` 已统一复用 `board_transport`；用户明确反馈本轮检查通过，原始退出码与完整日志未保留 |
| 统一构建入口 | 当前阶段完成 | 用户确认 `test=0`、`board_clean=0`、`build_all=0`；主机和三个 ARM 产物均存在，`file` 确认板端为 ARM EABI5 |
| GEC6818 最新统一产物部署与最终演示流程 | 当前阶段完成 | 用户反馈部署、启动、三种服务入口、门诊取号、排队刷新、叫号、异常恢复和退出检查通过；未保留具体上传与叫号退出码 |

## 当前阶段

项目完成边界审查与最终材料收口：**当前阶段完成**。

本阶段同步 `PROJECT_BRIEF.md`、`PROJECT_STATUS.md`、`TEST_STATUS.md`、`FINAL_ACCEPTANCE.md` 和 `FINAL_DEMO_GUIDE.md`，统一当前完成层级、最终演示顺序、证据完整性标志和未覆盖边界；不修改源码、测试、构建配置或架构规则。

本阶段不运行构建、测试、程序或设备操作；既有用户回传的 `clean=0`、`board_build=0`、`artifact=0` 和 GEC6818 生命周期确认只作为已有验证事实引用，不重新解释为本阶段新验证。

## 排队状态查询（get_ticket）阶段事实

### Store

- 新增 `clinic_store_get_ticket()`，根据 `ticket_id` 查询完整 `ClinicTicket`。
- 不存在的记录稳定返回 `CLINIC_STORE_TICKET_NOT_FOUND`。
- 数据库中的 `called_time IS NULL` 映射为 C 结构体中的 `0`；失败路径清零输出。
- 已验证关闭数据库时的错误路径，以及重开数据库后仍可查询持久化记录。

### Core

- 新增 `CLINIC_REQ_GET_TICKET = 6`，并在 `ClinicRequest` 中新增 `int64_t ticket_id`。
- 成功响应复用 `CLINIC_RESPONSE_TICKET`，不增加重复响应类型。
- 已完成 `INVALID_ARGUMENT`、`TICKET_NOT_FOUND` 和 `DATABASE_ERROR` 映射。

### JSON

- 支持 `{"type":"get_ticket","request_id":...,"ticket_id":...}`。
- `ticket_id` 必须是唯一顶层正十进制 `int64_t`；拒绝缺失、0、负数、字符串、布尔、小数、指数、溢出、重复字段和嵌套字段冒充。
- 响应继续复用既有 ticket 编码。
- 当前 Ubuntu cJSON 不提供 `cJSON_ParseWithLengthOpts()`；已改为长度受控临时缓冲区配合 `cJSON_ParseWithOpts()`，并保留嵌入 NUL、尾随垃圾、分配失败和资源释放检查。

### Handler 离线链路

- 已通过 Handler 注册用户、创建 ticket，并使用响应中的真实 ticket ID 查询。
- 已验证完整字段、`WAITING`、`called_time=null`、不存在 ticket 的 `TICKET_NOT_FOUND`，以及 `ticket_id=0` 的 `INVALID_REQUEST`。
- 未修改生产 Handler；现有通用分发链路可以直接处理 get_ticket。

### 真实 TCP 与主机查询客户端

- 新增并验证 `clinic_ticket_status_client`。
- 调用形式：`clinic_ticket_status_client <server_ip> <port> <request_id> <ticket_id>`。
- 已验证链路：状态查询客户端→真实 TCP→epoll server→Handler→Core→SQLite→ticket JSON response。

## get_current_ticket 后端协议与主机 TCP 闭环阶段事实

- 新增独立请求 `get_current_ticket(user_id)`，按正整数 `user_id` 查询该用户今天创建的最新一张号单。
- 今日规则与 `create_ticket` 写入 `service_date` 的规则一致；SQLite 使用参数绑定，按 `id DESC LIMIT 1` 取最新记录。
- 查询不过滤 `WAITING`、`CALLED`、`COMPLETED` 或 `CANCELLED`；成功响应复用现有 Ticket 结构。
- 今天无号单时返回独立错误 `CURRENT_TICKET_NOT_FOUND`，不改变 `TICKET_NOT_FOUND` 的原语义。
- 用户真实验证结果为 `build_exit=0`、`full_test_exit=0`；成功分支返回 `request_id=3001`、`ticket.id=1`、`user_id=1`、`status=WAITING`，客户端退出码为 `0`。
- 无号单分支使用 `user_id=999999`，返回 `CURRENT_TICKET_NOT_FOUND`，客户端退出码为 `2`。
- 现有 `get_ticket(ticket_id)` 和 `TICKET_NOT_FOUND` 保持原语义；本阶段无数据库迁移。

## 管理员 call_next Store 阶段事实

- 原错误流程先查询指定科室当天已有的 `CALLED` 号单，只要存在就直接返回，导致后续仍为 `WAITING` 的号单不会被选择或更新。
- 真实失败证据为：`ticket.id=1` 已为 `CALLED`、`ticket.id=4` 仍为 `WAITING`；修复前再次执行 `call_next(department_id=1)` 错误地重复返回 `ticket.id=1`，用户 2 查询仍返回 `ticket.id=4`、`WAITING`。
- 修复后的 `clinic_store_call_next()` 处理范围仍限定为指定科室和当前本地日期 `service_date`，不处理历史 ticket。
- 每次调用直接按 `queue_number ASC, id ASC` 查询指定科室当天最早的 `WAITING` ticket。
- 选中后在事务内执行 `WAITING` → `CALLED`，写入当前 Unix 秒 `called_time`，再按 ID 重新读取并返回更新后的真实 Ticket。
- 当天没有 `WAITING` ticket 时继续返回既有 `CLINIC_STORE_NO_WAITING_TICKET`。
- 旧的“优先返回已有 `CALLED` ticket”幂等语义已删除，重复调用不得返回旧叫号结果。
- SQLite 事务使用 `BEGIN IMMEDIATE`；更新包含 `id` 与 `WAITING` 状态保护，成功提交，失败回滚并清零输出。
- 本次修复未修改协议字段、Core 路由、Ticket JSON 结构、状态枚举或 SQLite 表结构。

## 管理员 call_next Core 阶段事实

- 新增请求枚举 `CLINIC_REQ_CALL_NEXT = 7`。
- 请求复用现有 `ClinicRequest.department_id`，没有增加重复字段。
- `clinic_core_handle()` 校验 `department_id > 0` 后调用 `clinic_store_call_next()`。
- 成功响应复用 `CLINIC_RESPONSE_TICKET`，返回完整的 `CALLED` ticket，不增加响应类型。
- `department_id <= 0` 映射为 `INVALID_ARGUMENT`。
- Store 科室不存在状态映射为既有 `DEPARTMENT_NOT_FOUND`。
- `CLINIC_STORE_NO_WAITING_TICKET` 映射为 `NO_WAITING_TICKET`。
- 其他 Store 错误映射为 `DATABASE_ERROR`。
- 失败响应保持 `request_id`，设置 `kind=CLINIC_RESPONSE_NONE`，并清零 ticket。
- `test_ticket_core` 和 `test_doctor_core` 定向回归均已通过。

## 管理员 call_next JSON 阶段事实

- 支持请求 `{"type":"call_next","request_id":...,"department_id":...}`。
- 解码结果为 `type=CLINIC_REQ_CALL_NEXT`，保持 `request_id`，并将 `department_id` 解码为正十进制 `int64_t`。
- 严格拒绝缺失、0、负数、字符串、布尔、小数、指数形式及 `int64_t` 溢出的 `department_id`。
- 严格拒绝重复顶层字段、嵌套字段冒充、尾随垃圾和嵌入 NUL。
- 成功响应继续复用 `CLINIC_RESPONSE_TICKET` 及现有 ticket 响应编码，没有新增重复编码。
- `test_ticket_json` 和 `test_doctor_json` 定向回归均已通过。

## 管理员 call_next Handler 离线链路阶段事实

- 已验证链路：`call_next` JSON → Handler 离线 frame 入口 → Core → Store → SQLite → `CALLED` ticket JSON。
- 当前测试通过 `clinic_server_handler_handle_frame()` 驱动完整离线链路。
- 生产 Handler 无需专门修改，现有通用分发链路可以直接处理 `call_next`。
- 当前稳定语义是每次调用都重新选择指定科室当天最早的 `WAITING` ticket，并将其更新为 `CALLED`。
- 旧的重复返回同一张 `CALLED` ticket 的断言已经失效；已有 `CALLED` ticket 不属于候选结果。
- 用户保留的当前源码全量测试结果为 `full_test_exit=0`；最终真实 Bug 回归证据见下一节。
- 已验证 `NO_WAITING_TICKET`、`DEPARTMENT_NOT_FOUND` 和非法 `department_id` 错误响应。
- `test_ticket_handler` 和 `test_doctor_handler` 定向验证均已通过。

## 管理员 call_next 真实 TCP 与 CLI 阶段事实

- 新增主机程序 `build/linux/clinic_admin_call_client`。
- 调用方式：`clinic_admin_call_client <server_ip> <port> <request_id> <department_id>`。
- 已验证完整链路：管理员 CLI → 真实 TCP → Linux epoll server → Handler → Core → Store → SQLite → `CALLED` ticket JSON。
- 用户确认当前源码构建与全量测试通过，结果分别为 `build_exit=0`、`full_test_exit=0`。
- 服务器尚未监听时的首次真实调用返回 `could not connect to 127.0.0.1:9000`、`call_next_exit=1`；这是服务器未启动造成的环境失败，不是修复回归失败。
- 启动最新服务器后，`request_id=5006` 的 `call_next(department_id=1)` 返回 `ticket.id=4`、`user_id=2`、`department_id=1`、`queue_number=2`、`status=CALLED`、`called_time=1784105823`，退出码为 `call_next_exit=0`。
- 随后的 `request_id=5007` 调用 `get_current_ticket(user_id=2)` 返回同一 Ticket 和相同 `called_time`，退出码为 `user2_ticket_exit=0`。
- 该真实回归证明 `call_next` 跳过旧 `CALLED` 号单 `ticket.id=1`，并将下一张 `WAITING` 号单 `ticket.id=4` 更新为 `CALLED`。
- 有效但无等待票的科室继续返回既有 `NO_WAITING_TICKET`。

### 主机端核心业务闭环

注册 → 登录 → 科室 → 医生 → 取号 → 查询 `WAITING` → 管理员叫号 → 查询 `CALLED`。

### 当前边界

- call_next Store：完成。
- call_next Core：完成。
- call_next JSON：完成。
- call_next Handler：完成。
- call_next TCP：完成。
- 管理员 CLI：完成。
- GEC6818 实机探针：完成。
- 最终工具链确认：完成。
- LVGL 最小显示：完成。
- LVGL 最小触摸输入：完成。
- LVGL 中文字体最小验证：完成。
- 板端最小 TCP 网络：完成。
- LVGL 后台网络线程：完成。
- LVGL 医疗登录页骨架：当前阶段完成。
- 登录页之外的医疗业务页面：主页骨架、科室列表、医生列表和医生选择后的科室真实取号阶段完成；当前号单查询与排队状态展示未开始。

## 最终主机阶段结论

- 主机端后端业务闭环已经完成：注册 → 登录 → 科室 → 医生 → 取号 → 查询 `WAITING` → 管理员 `call_next` → 查询 `CALLED`。
- get_ticket 完整主机 TCP 链路完成。
- call_next 完整主机 TCP 链路和管理员 CLI 完成。
- 主机端最终聚合回归及 ticket 补充定向回归完成。
- 历史 Windows → Ubuntu 应用层联调已确认：Ubuntu epoll 服务器监听 `0.0.0.0:9000`，Windows 客户端两次连接均收到 pong，客户端断开后服务器继续运行；完整命令和退出码未保留。
- 后端功能范围冻结，不再新增业务。

## GEC6818 实机与工具链阶段事实

### 实机环境

- 架构：`armv7l`。
- SoC：`s5p6818`。
- 内核：`Linux 3.4.39-gec`。
- C 运行库：`glibc 2.23`。
- 动态加载器：`/lib/ld-linux.so.3`。
- framebuffer：`/dev/fb0`，32 bpp，`virtual_size=800,1440`；实际显示目标按 800×480。
- 触摸屏：`gslX680`，输入节点 `/dev/input/event0`。
- 板端字体路径：`/font/simkai.ttf`。
- 开发板 IP：`192.168.10.42`。

### 最终交叉编译器

- 编译器：`/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc`。
- target：`arm-none-linux-gnueabi`。
- sysroot：`/usr/local/arm/5.4.0/usr/arm-none-linux-gnueabi/sysroot`。
- 产物：ELF 32-bit ARM EABI5，解释器 `/lib/ld-linux.so.3`，最低记录符号版本 `GLIBC_2.4`，目标为 Cortex-A15 / ARMv7。

### 实机运行证据

- 输出：`GEC6818_ABI_OK`。
- `pointer_bits=32`。
- `int_bits=32`。
- `long_bits=32`。
- `probe_exit=0`。

### 部署链路

Ubuntu 编译 → VMware 共享目录 → Windows PowerShell 使用 `scp -O` → `/IOT` → 开发板运行。

## GEC6818 最小 LVGL framebuffer 显示阶段事实

### 最小工程与显示配置

- 已创建最小 LVGL 8.3 工程：`tools/board/lvgl_smoke/`。
- LVGL 8.3.0 源码：`reference/six/six/lvgl`。
- framebuffer 驱动：`reference/six/six/lv_drivers/display/fbdev.c`。
- framebuffer：`/dev/fb0`。
- 逻辑分辨率：800×480。
- 色深：32 bpp。
- draw buffer：800×40 像素。

### 首次构建故障与修复

- 首次构建时，Makefile 的 LVGL 源文件处理丢失了目录信息，编译器只收到 `lv_disp.c` 等 basename。
- 结果是大量 LVGL `.c` 文件报 `No such file or directory`。
- 本次故障仅修改 `tools/board/lvgl_smoke/Makefile`，未修改应用源码、配置头、LVGL 或 fbdev 源码。
- Makefile 改为保留每个 LVGL 源文件的完整相对路径，并以 `reference/six/six/lv_drivers/display/fbdev.c` 的完整路径加入 fbdev；修复后构建成功。

### 构建产物与 ELF

- 构建产物：`build/board/clinic_lvgl_smoke`。
- ELF：32-bit ARM EABI5。
- interpreter：`/lib/ld-linux.so.3`。
- GNU/Linux ABI：3.2.0。

### 部署与实机验证

- 部署链路：Ubuntu 交叉编译 → VMware 共享目录 → Windows PowerShell 使用 `scp -O` → GEC6818 `/IOT/clinic_lvgl_smoke`。
- LCD 纯色背景显示正确，屏幕中央显示 `GEC6818 LVGL OK`。
- 未出现花屏、偏移、裁切或崩溃。
- Ctrl+C 可以正常停止程序。
- 退出状态：`run_exit=0`。

## GEC6818 最小 LVGL 触摸验证阶段事实

### 工程与输入配置

- `tools/board/lvgl_smoke/` 已加入最小触摸验证。
- 触摸屏：`gslX680`。
- 输入设备：`/dev/input/event0`。
- 输入链路：evdev → LVGL pointer input → `LV_EVENT_CLICKED`。
- 原始 X 坐标范围：0～1024。
- 原始 Y 坐标范围：0～600。
- 坐标映射目标：800×480。

### 实机验证

- 居中的 `TOUCH ME` 按钮显示正常。
- 点击按钮后，同一标签文字变为 `TOUCH OK`。
- 点击位置正确，无明显偏移。
- framebuffer 显示与触摸输入可以同时工作。
- 程序运行稳定。

## GEC6818 LVGL 中文字体最小验证阶段事实

### 字体与运行配置

- `tools/board/lvgl_smoke/` 已完成中文字体最小验证。
- 开发板字体：`/font/simkai.ttf`。
- FreeType 运行库：`/IOT/libfreetype.so.6`。
- 字体支持：LVGL 8.3 FreeType。
- 实机运行环境包含：`LD_LIBRARY_PATH=/IOT:/lib:/usr/lib`。
- 实机显示文本：`医路通 中文显示正常`。

### 实机验证

- 中文显示清晰，无方框、乱码或缺字。
- framebuffer 显示正常。
- `TOUCH ME` 按钮仍可点击，点击后正常变为 `TOUCH OK`。
- 中文字体和触摸输入可以同时工作。
- 程序运行稳定。

## GEC6818 板端最小 TCP 网络验证阶段事实

### 探针与网络环境

- 已创建 `tools/board/net_probe/main.c` 和 `tools/board/net_probe/Makefile`。
- 构建产物：`build/board/clinic_net_probe`。
- Ubuntu 服务器地址：`192.168.10.41`。
- GEC6818 地址：`192.168.10.42`。
- TCP 端口：`9000`。
- Ubuntu 与 GEC6818 双向 ping 已通过，丢包率为 0%。

### 实际验证链路

GEC6818 → TCP → Ubuntu epoll `clinic_server` → ping JSON → pong JSON。

开发板实机输出：

```text
{"ok":true,"type":"pong","request_id":1,"message":"clinic server is alive"}
BOARD_TCP_PONG_OK
probe_exit=0
```

### 验证结论与命令边界

- ARM 网络客户端能够连接 Ubuntu 服务器。
- 换行 JSON 的发送和接收正常。
- 单一 5 秒截止时间机制正常。
- 板端与主机端协议兼容。
- 开发板没有 `ss` 命令。
- 板端输出的 `server stopped` 是 `||` 分支结果，不能作为 Ubuntu 服务停止证据。
- 服务监听状态必须在 Ubuntu 检查。

## GEC6818 LVGL 后台网络线程验证阶段事实

### 已验证线程模型

- `tools/board/lvgl_smoke/` 已完成 LVGL 后台网络线程验证。
- LVGL 点击回调只创建 pthread，不执行阻塞网络操作。
- 后台线程执行 TCP ping/pong，且不调用任何 `lv_*` API。
- `pthread_mutex_t` 保护共享状态。
- LVGL 主线程读取共享结果、更新 UI，并通过 `pthread_join()` 回收线程。

### UI 状态与实机结果

- UI 状态：`等待检测`、`连接中...`、`服务器在线`、`连接失败`。
- 服务器在线时检测成功，同一进程能够连续多次成功检测。
- 服务器关闭时显示 `连接失败`，失败后仍可重新检测。
- 网络请求期间触摸和界面不阻塞。
- Ctrl+C 正常退出。

### 调试结论与最终构建

- 最初误判为线程无法重复使用；运行日志随后证明 `pthread_create`、worker、`pthread_join` 和线程标志清理均正常。
- 实际问题是网络返回过快，`RUNNING` 状态未被主循环显示；最终在 LVGL 点击回调中立即显示 `连接中...`，后台线程仍不操作 LVGL。
- 临时 `[CLICK]`、`[WORKER]`、`[MAIN]`、`[UI]` 日志已删除。
- 日志清理后重新交叉构建成功：`build_exit=0`。
- 构建产物为 ELF 32-bit ARM EABI5，interpreter 为 `/lib/ld-linux.so.3`。

## GEC6818 LVGL 医疗登录页骨架阶段事实

### 已完成能力

- `board/clinic_terminal/` 已形成可在 GEC6818 上运行的 LVGL 医疗登录页骨架。
- 页面包含中文标题、用户名输入框、密码输入框和登录按钮。
- 用户名使用 K26 拼音键盘及中文候选栏。
- 密码使用独立英文键盘，并保持密码字符隐藏。
- 文本框具有明确的焦点边框和闪烁光标提示。
- 点击页面空白区域可以收起键盘和候选栏，同时保留输入内容。
- 登录按钮执行本地非空校验，校验结果通过模态消息框显示。
- 页面没有常驻校验文本。
- Ctrl+C 可以正常退出。

### 用户实机确认

- 用户已在 GEC6818 开发板确认用户名 K26 拼音键盘和密码英文键盘可见、可用。
- 两种键盘可以正常切换，点击空白可以收起输入区域。
- 登录按钮清晰可辨并具有按下反馈。
- 四种本地非空校验结果均通过可关闭的模态消息框显示，关闭后输入内容保留。
- 用户明确确认登录页阶段按既定验收标准通过。

### 板端真实登录实现与构建依赖

- 登录成功后，板端代码保存服务器响应中的 `user_id`；主机真实登录响应已确认 `user_id=1`。
- 尚未通过额外调试界面或运行日志单独观察板端内部保存值；这不等于 `user_id` 保存未实现。
- `third_party/cjson` 使用 cJSON 1.7.19，`clinic_terminal` 直接编译 `cJSON.c`，不依赖外部 `libcjson`。
- 构建产物为 32-bit ARM EABI5，解释器为 `/lib/ld-linux.so.3`，`RPATH=/IOT`。
- `NEEDED` 包含 `libpthread.so.0`，不包含 `libcjson`。

### 当前边界

- LVGL 医疗登录页骨架：当前阶段完成。
- 真实 login TCP 请求：当前阶段完成，已由用户在 GEC6818 实机验证。
- 登录成功后的 `user_id` 保存与主页传递：当前阶段完成；用户已在主页实机确认显示服务器返回的 `user_id=1`。
- 登录成功页面跳转和医疗服务主页骨架：当前阶段完成。
- 注册页面：未开始。
- 科室列表真实请求与展示：当前阶段完成，已由用户在 GEC6818 实机验证。
- 医生列表真实请求与展示：当前阶段完成，已由用户在 GEC6818 实机验证。
- 医生选择后的科室真实取号：当前阶段完成，已由用户在 GEC6818 实机验证。
- 当前号单查询与排队状态展示：当前阶段完成，已由用户在 GEC6818 实机验证。

## GEC6818 LVGL 医疗服务主页骨架阶段事实

### 页面切换与用户身份

- 正确密码登录成功后，用户确认“登录成功”消息框，程序进入医疗服务主页。
- 主页显示服务器真实登录响应传递的用户身份，用户实机看到 `用户 ID：1`，没有硬编码固定用户 ID。
- 错误密码仍显示“用户名或密码错误”并停留在登录页，不发生页面跳转。

### 主页内容与占位边界

- 主页显示“医路通”和“欢迎使用医疗服务”。
- 主页显示“科室查询”“医生查询”“预约取号”“排队查询”四个入口。
- “科室查询”和“排队查询”入口现已分别接入真实科室请求和真实当前号单请求；“医生查询”“预约取号”仍保持主页占位行为。
- 主页骨架阶段本身没有请求业务数据；后续科室列表阶段已独立完成真实请求与展示。

### 实机结果

- 切换后登录页键盘、拼音候选栏、输入框及其他登录页对象无残留。
- 页面切换和入口交互期间 UI 无冻结。
- Ctrl+C 可以正常退出。

## GEC6818 科室列表真实请求与展示阶段事实

### 真实请求与数据展示

- 登录进入主页后，点击“科室查询”由 pthread 后台线程发送真实 `list_departments` 请求，LVGL 主线程不执行阻塞网络操作。
- Ubuntu `clinic_server` 从 SQLite 返回真实科室数据，板端成功显示科室名称和科室 ID。
- 当前 SQLite 返回 5 个科室，展示内容没有超出一屏。

### 页面交互与恢复

- 在科室列表阶段的验收时点，点击具体科室只显示“请选择医生”，当时不请求医生列表；后续医生列表阶段已完成，见下节。
- 从科室页面返回主页不需要重新登录，并继续保留 `authenticated_user_id`。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程可以重新请求并成功展示科室。
- 请求、页面切换和返回期间 UI 无冻结，其他三个主页入口无回归，程序可以正常退出。

### 滚动验证边界

- 科室页已实现可滚动容器。
- 本次真实数据只有 5 个科室，未触发内容溢出或实际滚动。
- 因此只能确认滚动实现存在，不能将超出一屏时的滚动交互写成已通过实机验证。

## GEC6818 医生列表真实请求与展示阶段事实

### 真实请求与数据展示

- 用户在科室页面选择真实科室后，板端将该科室真实 `department_id` 交给后台请求链路并请求医生列表。
- Ubuntu `clinic_server` 从 SQLite 返回该科室的真实医生数据。
- 医生页面正确显示当前科室名称，以及医生 ID、医生姓名、职称和专长。
- 请求期间 UI 无冻结。

### 页面交互与恢复

- 在医生列表阶段的验收时点，点击医生只显示“暂未开放取号”，当时没有发起真实取号请求；后续科室真实取号阶段已完成，见下节。
- 从医生页返回后回到原科室列表，不重新登录、不重新请求科室，并保留 `authenticated_user_id` 和科室缓存。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程可以重新请求医生并成功展示。
- Ctrl+C 可以正常退出。

### 阶段边界

- 本阶段只完成医生列表真实请求、展示和页面返回闭环；后续真实取号证据见下节。
- 在本节验收时点，真实取号请求尚未验证；新建号单后的排队状态、重复取号限制和叫号后的界面刷新至今仍未验证。

## GEC6818 医生选择后的科室真实取号阶段事实

### 协议与业务语义

- 当前阶段准确名称为“医生选择后的科室真实取号”。
- `create_ticket` 请求只包含真实 `authenticated_user_id` 对应的 `user_id` 和医生所属科室的 `department_id`，协议不包含 `doctor_id`。
- 用户从医生列表选择医生后，实际为该医生所属科室取号，不是预约或绑定指定医生。
- 医生页面明确显示“点击医生后，将为其所属科室取号”。

### 真实请求与号单展示

- 板端通过 pthread 后台线程发送真实 `create_ticket` 请求，请求期间 UI 无冻结。
- Ubuntu `clinic_server` 成功向 SQLite 写入号单并返回真实号单响应。
- 板端号单页面成功显示号单 ID、科室名称和 ID、用户 ID、排队序号、状态和服务日期。
- 号单页面不显示医生姓名或医生 ID，不暗示号单绑定指定医生。

### 页面返回与离线恢复

- 返回主页时不重新登录，保留 `authenticated_user_id`，且不自动重新请求科室或医生。
- 服务器离线时显示“无法连接服务器”；服务器恢复后，同一板端进程可以再次操作。
- Ctrl+C 可以正常退出。

### 业务错误验证边界

- `SERVER_ERROR` 映射为“取号失败”的实现存在。
- 本轮实机验证没有触发服务器具体业务拒绝条件。
- 本轮没有验证重复取号限制，不得把重复取号写成板端实机测试通过。
- 排队状态自动刷新和叫号后的界面更新尚未实现或验证。

## GEC6818 板端当前号单查询与排队状态展示阶段事实

### 真实请求与号单展示

- 用户在主页点击“排队查询”后，板端使用真实 `authenticated_user_id` 发送 `get_current_ticket` 请求。
- 请求由 pthread 后台线程执行，LVGL 主线程不执行阻塞网络操作，请求期间 UI 无冻结。
- 排队状态页面实机显示服务器返回的号单 ID、用户 ID、科室 ID、排队序号、当前状态、服务日期、创建时间和叫号时间。
- 本轮真实号单状态为 `WAITING`，页面正确显示“等待叫号”；`called_time=null` 正确显示“尚未叫号”。

### 页面返回、离线恢复与回归

- 从排队状态页面返回主页不重新登录，并保留 `authenticated_user_id`。
- 服务器离线时页面提示“无法连接服务器”，LVGL UI 不冻结。
- 服务器恢复后，同一板端进程可以重新查询成功。
- 科室、医生和科室真实取号流程无回归，Ctrl+C 可以正常退出。

### 未覆盖边界

- 主机端已验证 `CURRENT_TICKET_NOT_FOUND`，但本轮没有在板端实机触发该无号单分支。
- 在本阶段验收时，`CALLED`、`COMPLETED` 和 `CANCELLED` 的板端中文显示尚未获得实机证据；后续手动刷新阶段已补充 `CALLED` 的实机证据，`COMPLETED` 和 `CANCELLED` 仍未覆盖。
- 在本阶段验收时只支持从主页发起一次手动查询；后续已完成页面内手动刷新，自动轮询和服务器推送仍未实现。

## GEC6818 排队状态手动刷新与叫号结果展示阶段事实

- 历史验证准确时间：未在当前记录中保留。

### 手动刷新与页面交互

- 用户在排队状态页面点击“刷新状态”后，页面显示“正在刷新...”，刷新和返回按钮在请求期间不可用，UI 无冻结。
- 未叫号时刷新后仍显示“等待叫号”和“尚未叫号”。
- 刷新复用 `get_current_ticket(user_id)` 手动查询；板端只负责查询和展示，不发送 `call_next`，也没有新增叫号按钮。

### 管理端叫号与板端结果展示

- Ubuntu 管理端使用 `build/linux/clinic_admin_call_client <server_ip> <port> <request_id> <department_id>`，`call_next` 使用真实 `department_id`。
- 修复后的管理端调用将 `ticket.id=4` 更新为 `CALLED`；板端再次手动刷新后显示“已叫号”和服务器返回的真实非空 `called_time`，不显示英文 `CALLED`。
- 板端显示的号单 ID 仍为 4、用户 ID 仍为 2、科室 ID 仍为 1，与管理端响应和 `get_current_ticket` 查询一致。

### 离线恢复与页面生命周期

- 服务器离线刷新时显示“无法连接服务器”，保留刷新前的号单详情，恢复按钮且 UI 无冻结。
- 服务器恢复后，同一板端进程可以再次刷新成功。
- 返回主页无需重新登录，Ctrl+C 可以正常退出。

### 未覆盖边界

- 板端 `CURRENT_TICKET_NOT_FOUND` 分支仍未实机触发。
- `COMPLETED` 和 `CANCELLED` 状态仍未实机覆盖。
- 未实现自动轮询或服务器推送，本轮也不能证明完整项目已经完成。

## GEC6818 排队信息增强源码实现阶段事实

### 后端与协议

- 新增 `ClinicQueueSummary`，由 Store 同时返回当前号单和排队摘要。
- 当前叫号只查询同科室、同日期最近一次 `CALLED` 号单，排序为 `called_time DESC, id DESC`；无叫号以 JSON `null` 传输。
- 前方人数只统计同科室、同日期、号码更小且状态为 `WAITING` 的号单；本人状态不是 `WAITING` 时返回 0。
- `get_current_ticket` 请求格式保持不变；`create_ticket`、`get_ticket` 和 `call_next` 响应结构保持不变；不新增表结构。
- 板端和主机当前号单解析器要求 `queue_summary` 的字段集合精确匹配，并拒绝缺失、重复、额外字段和类型/数值错误。

### 板端与生命周期

- 排队页保留本人号码，新增“当前叫号”和“前方等待”两行。
- 本人非 `WAITING` 时显示“前方等待：无需等待”。
- 继续复用现有手动刷新后台线程、一次消费并 `join` 的生命周期；未增加自动轮询、推送、叫号按钮或页面线程。
- 网络失败仍保留旧详情和摘要，并恢复刷新/返回按钮；用户已确认本轮检查全部通过。

### 阶段边界

- 用户已执行主机回归、板端构建部署和 GEC6818 实机验收，并确认本轮检查全部通过。
- 用户确认前方等待人数在每次叫号并刷新后减少 1；未补写未保留的具体退出码和原始终端日志。
- 本阶段最终状态：当前阶段完成。

## 未完成模块

| 模块 | 状态 | 边界 |
| --- | --- | --- |
| GEC6818 主页独立医生查询入口 | 局部验证 | 用户反馈最新统一产物演示检查通过；当前记录未保留逐项 GEC6818 点击日志，因此保留局部验证边界 |
| 排队页定时器生命周期封装 | 当前阶段完成 | Ubuntu/ARM 构建与产物存在性检查通过；用户已确认本次源码在 GEC6818 上完成生命周期回归，原始逐项日志未保留 |
| 项目完成边界审查与最终材料收口 | 当前阶段完成 | 已同步最终验收边界、演示顺序、证据完整性说明和未覆盖边界；等待用户确认当前范围是否收口 |

## 当前未完成边界

- GEC6818 实机环境探针、最终工具链确认、最小 LVGL framebuffer 显示、最小 LVGL 触摸输入、LVGL 中文字体最小验证、板端最小 TCP 网络、LVGL 后台网络线程、LVGL 医疗登录页骨架、医疗服务主页骨架、科室列表、医生列表、科室真实取号、当前号单查询与排队状态展示以及手动刷新与 `CALLED` 结果展示已经完成。
- VMware Ubuntu 继续作为编译环境，实际运行证据来自 IP 为 `192.168.10.42` 的 GEC6818 开发板。
- 真实 login TCP 请求、登录成功后的 `user_id` 保存与页面传递、科室列表、医生列表、科室真实取号、原有板端 `get_current_ticket` 查询、管理端叫号后的手动刷新闭环、排队信息增强以及 `call_next` WAITING 号单选择修复均已完成。主页独立医生查询入口和三种服务入口语义已由用户反馈本次最新产物演示检查通过，但逐项日志未保留；板端无号单分支、`COMPLETED` 和 `CANCELLED` 状态仍未做专项实机验证；5 秒自动刷新和定时器生命周期已由用户确认，服务器推送仍未实现。
- 本次定时器生命周期封装已取得 Ubuntu/ARM 构建和产物存在性证据；用户已确认 GEC6818 的反复进出页面、自动/手动刷新交互、断网恢复和定时器销毁回归通过，但未提供原始逐项日志。

## 下一唯一阶段

**用户确认最终验收边界与项目收口**

阅读最终验收边界和演示手册，确认是否接受当前核心演示范围及其未覆盖边界；若要补做某个未验证场景，再另开一个单一验证阶段，本阶段不新增功能。

## 完成状态定义

| 状态 | 定义 |
| --- | --- |
| 未开始 | 尚无对应实现，或没有证据表明已开始 |
| 实现中 | 正在修改，目标范围尚未完整落地 |
| 实现完成未验证 | 代码已存在，但没有符合要求的实际构建或运行证据 |
| 局部验证 | 只有部分层、部分场景或窄范围回归通过，不能代表整个阶段 |
| 当前阶段完成 | 当前阶段规定的实现与定向验证均已完成，且没有越过阶段边界 |
| needs-info | 缺少必须由用户、外部环境或真实设备提供的信息，不能安全推断 |

“当前阶段完成”只表示指定范围已经收口。主机端完整业务闭环、主机端最终回归、`get_current_ticket` 后端协议与主机 TCP 闭环、GEC6818 工具链、基础 LVGL 能力、登录、主页、科室、医生、科室真实取号、板端当前号单查询、手动刷新与 `CALLED` 结果展示、统一构建入口以及本次最新产物演示流程均已有对应证据；项目整体仍等待用户确认最终验收边界，不扩展为原始 PRD 全部完成。

## 证据与追溯

当前状态依据实际命令输出和阶段报告维护，不具备 Git commit 级追溯能力。

## 最新项目状态

- 当前阶段：项目完成边界审查与最终验收材料已整理，当前阶段完成。
- 已确认范围：认证链路、科室查询、医生查询、按科室取号、当前号单、当前叫号、前方等待人数、自动/手动刷新、定时器生命周期、叫号后人数变化、部署和异常恢复。
- 当前唯一下一阶段：用户确认最终验收边界与项目收口。
- 明确未纳入完成声明：服务器推送、复杂预约，以及未实际触发的 `COMPLETED`、`CANCELLED` 和无号单边界场景。
- 本轮用户构建结果：`clean=0`、`board_build=0`、`artifact=0`；用户随后明确反馈“验证通过”，完成本次 GEC6818 生命周期回归确认，但未保留原始逐项日志。
- 最终材料结果：已同步项目简报、项目状态、测试状态、最终验收边界和最终演示手册；未修改源码、测试、构建文件或架构文档。
- 最新源码补充：主页已使用“科室查询/医生查询/门诊取号”三态流程；用户反馈本次最新产物的入口语义和演示流程检查通过。
- 最新源码补充：主页已增加“退出登录”按钮，用户反馈本次演示中的退出登录检查通过。
- 最新源码补充：管理员客户端无参数交互模式已由用户确认验证通过；原四参数模式继续保留，本次未保留逐条命令输出和原始退出码。
- 最新源码补充：四个板端业务客户端已统一复用 `board_transport`，连接、截止时间、收发和分帧规则只有一份实现；用户确认本轮检查通过。
- 构建入口补充：根 `Makefile` 已提供 `host`、`test`、`board`、`build-all` 和 `board-clean`；用户已完成本次入口修改后的 Ubuntu 回归，三个板端产物均确认是 ARM EABI5。

最终验收清单和演示顺序见 [`docs/FINAL_ACCEPTANCE.md`](FINAL_ACCEPTANCE.md)。

## AU. 排队页定时器生命周期封装 GEC6818 实机回归确认

更新时间：2026-07-24 20:10（Asia/Hong_Kong）

### V-AU-01：GEC6818 实机生命周期回归

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | GEC6818 开发板 |
| 验证编号 | V-AU-01 |
| 对应代码阶段 | 排队页定时器生命周期封装 |
| 验证范围 | 反复进入/返回排队页、自动与手动刷新、刷新期间按钮状态、断网恢复、页面返回后的定时器销毁 |
| 用户结果 | 用户明确反馈“验证通过” |
| 原始命令、部署记录、退出码和逐项操作日志 | 未在当前记录中保留，不补写具体值 |
| 证据完整性错误标志 | `1`：缺少原始命令、部署记录、退出码和逐项日志 |
| 功能结论 | 用户确认本次 GEC6818 生命周期回归通过 |
| 不能证明 | 具体命令退出码、产物哈希、逐项操作原始日志，以及未列出的其他场景 |

### 阶段结论

- 本阶段状态：当前阶段完成。
- 功能结论：用户确认本次定时器生命周期回归通过。
- 证据完整性结论：证据完整性错误标志为 `1`，因为当前记录没有原始命令、退出码和逐项日志。
- 下一唯一阶段：项目完成边界审查与最终材料收口。

后续更新本文件时，应只把真实执行过的构建、测试或设备命令写成“已验证”；源码或测试目标存在本身不构成通过证据。

## AF. 主页入口与退出登录源码加固阶段

### 本轮修改

- 删除主页文件中已经没有调用方的遗留“功能开发中”占位回调，避免保留无效入口代码。
- 退出登录点击事件在设置 `logout_requested` 后立即禁用按钮，防止同一轮 LVGL 事件处理期间重复触发退出流程。
- 未修改 TCP/JSON 协议、SQLite 表结构、取号语义、叫号语义或后台线程模型。

### 当前证据边界

- `home_page.c` 已通过当前 Windows 环境 Clang 的 C11 语法检查。
- `main.c` 不能在当前 Windows 环境直接完成完整语法编译，原因是环境没有 Linux `pthread.h`；这不替代 Ubuntu ARM 构建。
- 本轮修改尚未在 Ubuntu 重新交叉构建，尚未在 GEC6818 实机验证。

### 阶段状态

实现完成未验证，等待用户执行。

## AG. 医生查询提示与退出后拼音键盘修复

### 根因与修改

- 医生页原先无条件创建“点击医生后，将为其所属科室取号”说明标签，导致科室查询和医生查询模式也显示取号提示；该阶段删除了说明标签，当时的“预约取号”入口后来已在 AJ 阶段更名为“门诊取号”。
- 退出登录时原先在旧主页仍为活动屏幕时创建登录页。LVGL 拼音输入法会把候选面板创建到当前活动屏幕，旧主页随后删除会连带删除候选面板；现已调整为先加载新登录屏幕，再创建登录页和拼音输入法对象。

### 当前证据边界

- 医生页已通过 Windows Clang 严格 C11 语法检查。
- 已完成误导文案消失和退出屏幕创建顺序的静态检查。
- 尚未在 Ubuntu 重新 ARM 交叉编译，尚未在 GEC6818 实机验证。

### 阶段状态

实现完成未验证，等待用户执行。

## AH. 拼音候选状态保护实机确认

- 登录页与注册页不再清空 LVGL 永久字典索引 `py_num` / `py_pos`。
- 无匹配拼音时清除旧候选状态并隐藏候选面板。
- 为规避当前 LVGL 候选末页越界风险，板端仅保留首屏候选，不开放候选翻页。
- 用户已按本阶段验证步骤执行检查，并明确反馈“检查通过”。
- 当前记录未保留具体构建退出码、运行退出码和完整终端输出，因此不补写具体值。

### 阶段状态

当前阶段完成。

## AI. 登录页记住密码功能实机确认

- 登录页增加“记住密码：否/是”可切换按钮，不依赖未进入当前板端链接产物的 LVGL checkbox 组件。
- 只有登录成功后才按按钮状态保存或清除本地凭据；登录失败不会覆盖已保存的正确密码。
- 保存文件为 `/IOT/.clinic_terminal_credentials`，权限限定为 `0600`。
- 退出登录和程序重启后可以回填已记住的用户名、密码和按钮状态。
- 取消记住并成功登录后删除本地凭据，后续不再回填。
- 注册新账号成功后清除旧账号的本地记住状态。
- 用户已确认板端重新构建成功，并确认部署、运行及本轮全部功能检查通过。
- 当前记录未保留完整构建日志、部署日志和原始退出码，不补写具体值。

### 阶段状态

当前阶段完成。

## AJ. 科室查询、医生查询与门诊取号语义拆分

### 本轮修改

- 新增 `ClinicServiceFlow` 三态流程，替代只能区分浏览/取号的布尔值。
- “科室查询”显示科室信息和坐诊医生，医生条目只打开详情，不提供取号动作。
- “医生查询”先按科室筛选，医生详情显示姓名、职称和擅长方向；只有点击“前往科室取号”才会进入既有取号请求。
- 主页“预约取号”更名为“门诊取号”；医生详情中的确认文案明确号单按科室创建，不代表预约指定医生。
- 科室请求期间禁用主页全部导航入口，并把流程模式保存在请求上下文中，防止快速连点改变正在执行请求的语义。
- 未修改 TCP/JSON 字段、SQLite、`create_ticket(user_id, department_id)`、叫号语义或线程数量。

### 当前证据边界

- `home_page.c`、`department_page.c`、`doctor_page.c` 已在 Windows Clang 下通过 C11、`-Wall -Wextra -Wpedantic -Werror` 语法检查。
- Windows 环境缺少 `pthread.h`，不能完成 `main.c` 的 Linux 目标语法编译；该限制不等于源码构建失败，也不能代替 Ubuntu ARM 构建。
- 之前的实机通过记录发生在本轮修改之前，不能扩展为本轮结论。

### 阶段状态

实现完成未验证，等待用户执行。

## AK. 三个入口完全拆分与直接科室取号

### 本轮修改

- 科室查询点击科室后只显示科室名称和编号，不再请求或展示医生。
- 医生查询保留“选择科室筛选医生 → 查看医生详情”，医生详情只提供关闭按钮。
- 门诊取号改为“选择科室 → 取消/确认取号 → 创建科室号单”，不再经过医生列表。
- `TicketRequestContext`、`TicketUiController` 和 `ticket_page` 删除板端冗余的医生 ID、医生姓名及医生页所有权。
- 取号加载状态和失败提示改由科室页持有；成功后科室页清理并切换到号单页。
- 未修改 `create_ticket` 请求字段、服务器业务、SQLite、排队、叫号或认证功能。

### 生命周期边界

- 科室确认框在 LVGL 主线程关闭后才启动取号 worker。
- worker 只访问稳定的请求上下文副本，不调用 LVGL。
- 主线程 join 后检查科室页仍为活动 screen，再恢复按钮、显示错误或切换号单页。

### 当前证据边界

- 四个页面模块通过 Windows Clang 严格 C11 语法检查。
- `main.c` 使用临时 POSIX 声明桩完成接口语法检查，检查后声明桩已删除；该检查不替代 Linux/ARM 构建。
- 源码契约检查确认医生页无取号行为、取号上下文无医生字段、门诊取号从科室页直接开始。
- 本轮尚无 ARM 构建和 GEC6818 实机证据。

### 阶段状态

实现完成未验证，等待用户执行。

## AL. 辅助目录整理后的 Ubuntu/ARM 构建验证

更新时间：2026-07-17 16:49（Asia/Hong_Kong）

### 本阶段范围

- 开发板辅助程序已从 `board/` 归入 `tools/board/`。
- 历史工作指令已归入 `.scratch/operations/`，诊断产物已归入 `build/diagnostics/`。
- 根目录新增工程入口说明 `README.md`。
- 本阶段未修改产品业务源码、协议、数据库、线程模型或开发板 `/IOT` 运行路径。

### 用户实际执行证据

用户在 Ubuntu 共享目录 `/mnt/hgfs/codex` 执行了主机回归以及正式终端、LVGL 显示探针和网络探针的清理/构建。用户回传的退出码为：

```text
test=0 terminal_clean=0 terminal_build=0 smoke_clean=0 smoke_build=0 probe_clean=0 probe_build=0
```

产物存在且大小为：

```text
build/board/clinic_terminal      448056 bytes
build/board/clinic_lvgl_smoke    299876 bytes
build/board/clinic_net_probe      10964 bytes
```

`file` 输出确认三个文件均为 `ELF 32-bit LSB executable, ARM, EABI5`，动态链接器为 `/lib/ld-linux.so.3`，并保留 `debug_info`、未 strip。

### 结论与边界

- 本阶段用户验证错误标志：`0`。
- 该证据证明目录调整后的 Ubuntu 构建路径可用，且产物为目标 ARM ABI。
- 该证据不证明开发板已运行本次最新源码，也不替代 GEC6818 实机点击、断网恢复和完整演示验收。
- 文档阶段完成，不改变已有用户验证结论。

## AM. 统一构建入口实现阶段

更新时间：2026-07-17 17:04（Asia/Hong_Kong）

### 本轮修改

- 根 `Makefile` 新增 `host`、`board`、`build-all`、`board-terminal`、`board-lvgl-smoke`、`board-net-probe` 和 `board-clean` 目标。
- `all` 保持主机目标语义，`build-all` 明确表示主机目标加全部板端目标，避免隐式改变既有命令含义。
- `build.ps1` 改为根 Makefile 调度器，支持 `host`、`test`、`board`、`build-all`、`board-clean`、`all` 和 `clean`。
- `README.md` 补充统一入口和目标说明。
- 未修改产品业务源码、协议、数据库、线程、LVGL 第三方源码或 `/IOT` 运行命令。

### 本机静态检查

- PowerShell AST 语法检查：错误标志 `0`。
- Makefile 目标、板端路径和递归调度检查：错误标志 `0`。
- 当前 Windows 未安装 GNU Make，因此没有执行本阶段构建；不把静态检查写成构建通过。

### 阶段状态

实现完成未验证，等待用户执行。

## AN. 统一构建入口 Ubuntu/ARM 回归确认

更新时间：2026-07-17 17:10（Asia/Hong_Kong）

### 用户实际执行证据

- `make -B test` 已执行，`test=0`。
- `make board-clean` 已执行，`board_clean=0`。
- `make -B build-all` 已执行，`build_all=0`。
- 主机产物存在：`clinic_server`、`clinic_ticket_client`、`clinic_ticket_status_client`、`clinic_admin_call_client`。
- 板端产物存在：`clinic_terminal`（448056 bytes）、`clinic_lvgl_smoke`（299876 bytes）、`clinic_net_probe`（10964 bytes）。
- `file` 确认三个板端产物均为 `ELF 32-bit LSB executable, ARM, EABI5`，动态链接器为 `/lib/ld-linux.so.3`，含 `debug_info` 且未 strip。

### 阶段状态

当前阶段完成。

## AO. GEC6818 最新统一产物部署与最终演示流程确认

更新时间：2026-07-17 17:19（Asia/Hong_Kong）

### 用户实际确认

- 用户按本阶段步骤完成最新 `clinic_terminal` 产物部署和开发板启动。
- 用户按顺序检查注册/登录、科室查询、医生查询、门诊取号、排队状态、管理员叫号、手动刷新、异常恢复和退出登录流程。
- 用户明确反馈：`检查通过`。
- 本次记录未保留上传命令、管理员叫号命令的具体退出码和完整终端日志，不补写具体数值。

### 阶段边界

- 本记录证明本阶段用户实际演示检查得到通过反馈。
- 本记录不把无号单、`COMPLETED`、`CANCELLED` 等未专项触发场景写成已验证。
- 自动轮询、服务器推送和复杂预约仍不属于当前实现。

### 阶段状态

当前阶段完成。

## AP. 重复取号识别与板端提示

更新时间：2026-07-17 20:12（Asia/Hong_Kong）

### 本轮修改

- 保留同一用户、同一科室、同一服务日期只能存在一张 `WAITING` 或 `CALLED` 号单的数据库约束。
- Store 在返回已有活动号单时使用独立状态，Core 将成功响应消息设为 `active ticket retrieved`；首次创建仍返回 `ticket created`。
- 板端首次取号显示“取号成功”；重复取号先进入原号单页面，再弹出“您已有有效号单，本次未重复取号，正在显示原号单”。
- 重复取号弹窗由 LVGL 主线程在票据页成为活动屏幕后创建；弹窗指针由 `ClinicTicketPage` 持有，关闭、返回、退出和页面清理时统一清空。
- 未新增协议字段，未修改 `create_ticket(user_id, department_id)` 请求、排队统计、叫号语义或线程模型。

### 当前证据边界

- `core/clinic_core.c` 与 `store/clinic_store.c` 已通过 Windows Clang C11 严格语法检查；`ticket_page.c` 已通过 C11 语法检查。
- Windows 环境缺少 Linux `netdb.h`、SQLite 开发头和 ARM 工具链，未执行本轮完整主机测试或板端构建。
- 用户首次执行主机回归时，`test_ticket_store` 因一处仍期望旧 `CLINIC_STORE_OK` 的断言失败，`full_test_exit=2`；该断言已改为活动号单新状态，尚待重跑。
- 既有 GEC6818 演示通过证据早于本轮修改，不能替代本轮重复取号提示的实机验证。

### 阶段状态

实现完成未验证，等待用户执行。

## AQ. Ubuntu 管理端交互式叫号菜单

更新时间：2026-07-17（Asia/Hong_Kong）

### 本轮修改

- `clinic_admin_call_client` 无参数启动时连接本机 `127.0.0.1:9000`，先发送现有 `list_departments` 请求，再按服务器返回的真实科室生成中文菜单。
- 输入菜单数字后发送现有 `call_next(department_id)`；成功时显示科室、当前叫号和号单 ID，无等待号单时显示中文提示。
- 输入 `r` 重新读取科室列表，输入 `q` 正常退出；请求 ID 由客户端自动生成并逐次递增。
- 刷新失败时保留最后一次成功的科室列表；每次请求均使用独立 TCP 连接并在结束后关闭。
- 原四参数调用方式继续保留，便于自动化回归和故障排查。

### 保持不变

- 未修改 `list_departments`、`call_next` 的请求或响应字段。
- 未修改 Handler、Core、Store、SQLite、叫号顺序和 GEC6818 板端功能。
- 未在普通用户板端增加叫号权限。

### 当前证据边界

- 已完成源码结构、UTF-8 字符串、资源关闭、刷新失败保留旧列表和兼容入口静态检查。
- 用户在 Ubuntu 执行本阶段验证后明确反馈“验证通过”。
- 当前记录未保留逐条构建输出、菜单输出和原始退出码，因此不补写具体数值。
- 本次确认只覆盖 Ubuntu 管理端交互式叫号，不新增 GEC6818 实机结论。

### 阶段状态

当前阶段完成。

## AR. 板端公共 TCP 传输模块

更新时间：2026-07-18 14:33（Asia/Hong_Kong）

### 本轮修改

- 新增 `board/clinic_terminal/board_transport.c` 与 `.h`，统一数值 IPv4 连接、单一总截止时间、非阻塞 socket 等待、完整发送、接收和换行分帧。
- `login_client`、`department_client`、`doctor_client`、`ticket_client` 已删除各自重复的传输辅助函数，统一调用 `clinic_board_transport_exchange()`。
- 各业务客户端继续独立负责请求 JSON 编码、严格响应字段校验和既有错误消息映射；没有把业务语义放入传输层。
- 根 `Makefile` 新增 `test_board_transport`，覆盖参数错误、拆分 CRLF 帧、对端提前关闭、超长帧、接收超时和非法数值地址。
- 板端 Makefile 已把公共传输源码和头文件纳入正式终端构建。

### 保持不变

- 未修改任何 TCP/JSON 字段、请求类型、错误码、Handler、Core、Store 或 SQLite 表结构。
- 未修改 LVGL 页面、线程数量、worker 参数副本、主线程 `join` 和页面生命周期规则。
- `create_ticket`、`get_current_ticket`、`call_next`、重复取号及手动刷新语义保持不变。

### 用户验证证据

- 用户按本阶段提供的专项测试、完整回归、ARM 构建和四类板端网络业务回归路径完成检查，并明确反馈“检查通过”。
- 当前对话未保留逐条命令输出、原始退出码、产物哈希和完整板端操作日志，因此不补写具体数值。

### 阶段状态

当前阶段完成。

## AS. GEC6818 排队页 5 秒自动刷新

更新时间：2026-07-21 11:39（Asia/Hong_Kong）

### 本轮修改

- 排队页新增一个由 LVGL 主线程管理的 5 秒定时器；首次进入页面后等待完整周期再触发查询。
- 自动刷新和“刷新状态”按钮复用现有 `current_ticket_refresh_clicked()`，最终仍由原有 pthread worker 执行 `get_current_ticket`。
- 请求开始时暂停定时器并禁用刷新、返回按钮；请求完成后重新从 5 秒开始计时，避免断网时连续重试占满操作窗口。
- 返回按钮触发时先删除定时器，再通知 `main.c` 切页；页面统一清理路径也会删除并清空定时器指针。
- 排队页标题显示“每 5 秒自动刷新”，手动刷新入口继续保留。

### 保持不变

- 未修改 `get_current_ticket` 请求或响应字段，未修改 Handler、Core、Store、SQLite 和 `call_next` 语义。
- 未增加服务器推送、额外页面线程或并发网络请求；网络 worker 仍不直接调用 LVGL。
- 查询失败仍保留上一次成功数据；后续定时刷新和手动刷新都复用原有错误恢复路径。

### 当前证据边界

- `queue_page.c` 已在 Windows Clang 下通过 C11、`-Wall -Wextra -Wpedantic -Werror` 严格语法检查。
- 用户明确反馈：“验证也通过了，自动轮询”。
- 本轮未保留 Ubuntu ARM 构建命令、上传记录、原始退出码或逐项板端操作日志，不补写具体数值。
- `docs/TEST_STATUS.md` 已同步用户确认，验证范围不扩展为无号单、`COMPLETED`、`CANCELLED` 或服务器推送通过。

### 阶段状态

当前阶段完成。

## AT. 排队页定时器生命周期封装 Ubuntu/ARM 构建验证

更新时间：2026-07-24 20:05（Asia/Hong_Kong）

### 本轮修改

- `board/clinic_terminal/queue_page.c` 新增定时器暂停、重置恢复和销毁辅助函数。
- 刷新开始、刷新结束、返回页面和页面清理路径统一复用这些辅助函数。
- 未修改 5 秒刷新周期、`get_current_ticket` worker、协议、服务器、Store、SQLite 或页面交互语义。

### V-AT-01：Ubuntu/ARM 构建与产物存在性检查

| 项目 | 记录 |
| --- | --- |
| 执行者 | 用户 |
| 环境 | Ubuntu |
| 工作目录 | `/mnt/hgfs/codex` |
| 命令或验证名称 | `make -C board/clinic_terminal clean`；`make -C board/clinic_terminal -j"$(nproc)"`；`test -f build/board/clinic_terminal` |
| 对应代码阶段 | 排队页定时器生命周期封装 |
| 用户回传结果 | `clean=0`、`board_build=0`、`artifact=0` |
| 原始完整构建日志 | 未在当前记录中保留；仅保留用户回传的退出码汇总 |
| 文件证据 | `build/board/clinic_terminal` 存在，存在性检查退出码为 `0` |
| 错误标志 | `0` |
| 能够证明 | 本次板端清理、Ubuntu/ARM 构建和目标产物存在性检查通过 |
| 不能证明 | GEC6818 部署、启动、自动/手动刷新、反复进出页面、断网恢复或定时器销毁行为 |

### 阶段结论

- 本阶段状态：局部验证。
- 用户验证错误标志：`0`。
- 下一唯一阶段：GEC6818 排队页定时器生命周期回归。

## AV. 项目完成边界审查与最终验收材料收口

更新时间：2026-07-24 20:17（Asia/Hong_Kong）

### 本阶段处理

- 同步 `PROJECT_BRIEF.md` 的当前阶段和稳定边界。
- 同步 `PROJECT_STATUS.md` 的完成层级、未覆盖边界和下一唯一阶段。
- 同步 `TEST_STATUS.md` 的文档阶段记录与用户验证证据边界。
- 更新 `FINAL_ACCEPTANCE.md` 和 `FINAL_DEMO_GUIDE.md`，纳入 5 秒自动刷新、定时器生命周期确认和当前未专项验证场景。
- 未修改源码、测试、构建配置或 `ARCHITECTURE.md`；未运行构建、测试、程序或设备操作。

### 阶段结论

- 本阶段状态：当前阶段完成。
- 功能结论：不新增或改变功能；材料与当前用户验证事实、证据完整性标志和未覆盖边界一致。
- 项目整体结论：仍等待用户确认当前最终验收边界，不宣称原始 PRD 全部完成。
- 下一唯一阶段：用户确认最终验收边界与项目收口。
