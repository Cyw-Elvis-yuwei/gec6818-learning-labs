# ADR-0004：号单按科室创建，不绑定医生

- 状态：Accepted
- 日期：2026-07-23
- 范围：取号、排队统计和管理员叫号

## 背景

系统同时展示科室和医生信息，但当前项目目标是模拟医院科室排队，而不是实现指定医生预约。若把医生选择直接写入号单，会让“医生查询”和“门诊取号”混成预约语义，也会改变既有数据库和协议模型。

## 决策

- `create_ticket` 只接收 `user_id` 和 `department_id`。
- 医生查询只用于查看医生资料和按科室筛选，不在医生页面创建号单。
- 同一用户、同一科室、同一服务日期只能有一张 `WAITING` 或 `CALLED` 有效号单；重复取号返回原号单。
- `call_next` 在指定科室当天的 `WAITING` 号单中按 `queue_number ASC、id ASC` 选择最早号单，更新为 `CALLED` 并记录 `called_time`。
- `get_current_ticket` 由服务器返回本人号单、最近一次 `CALLED` 号码和前方 `WAITING` 数量。

## 结果

- 排队号码的业务单位明确为“科室 + 服务日期”。
- 医生查询、科室查询和门诊取号可以在板端展示为三个独立入口。
- 前方人数由服务器按数据库状态计算，板端不重复实现排队规则。
- 不需要新增预约表，也不需要给号单增加 `doctor_id`。

## 未选择的方案

- 不把“点击医生”解释为指定医生预约。
- 不在板端根据本地缓存的医生或号单列表计算等待人数。
- 不通过重复创建号单来表示同一用户的多次查询。

## 依据

- `include/clinic_types.h`
- `include/clinic_store.h`
- `core/clinic_core.c`
- `store/clinic_store_sqlite.c`
- `board/clinic_terminal/ticket_client.c`
- `board/clinic_terminal/queue_page.c`
