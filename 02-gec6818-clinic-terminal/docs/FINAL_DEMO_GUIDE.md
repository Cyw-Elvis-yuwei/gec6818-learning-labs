# 医路通——医疗排队自助终端最终演示手册

更新时间：2026-07-24 20:17（Asia/Hong_Kong）

## 中文判断提示

- 当前状态：统一构建、Ubuntu/ARM 回归、GEC6818 主要演示流程、三种入口语义反馈和排队页定时器生命周期均已有阶段记录；最新定时器回归由用户确认通过。
- 这是什么意思：注册登录、三种服务入口、排队查询、管理员叫号、5 秒自动/手动刷新、异常恢复和退出登录可以按当前材料演示；重复取号弹窗仍只能写“实现完成未验证”。
- 是否还需要继续讨论：不需要，除非要补做未专项边界或扩展 HTTP、服务器推送和复杂预约。
- 建议下一步：用户确认最终验收边界；正式演示时按本手册执行，不把未验证场景讲成通过。
- 还缺什么：重复取号、无号单、`COMPLETED`、`CANCELLED`、超长科室列表的独立证据，以及本轮部分用户确认的原始命令和逐项日志。

## 一、演示目标与统一口径

### 1. 项目定位

本项目是基于 GEC6818、嵌入式 Linux 和 LVGL 的联网医疗排队自助终端。完整链路为：

板端负责界面、输入和网络请求，服务端负责连接接入、业务规则和数据库持久化。详细的数据流和模块分工放在后面的“并发编程与网络编程重点讲解”里展开。

### 2. 业务口径

- “科室查询”只展示科室名称和编号，不进入医生列表，也不提供取号动作。
- “医生查询”先选择科室筛选医生，再展示医生姓名、职称和擅长方向；医生页只负责查询。
- “门诊取号”选择科室并确认后直接创建该科室号单，不选择医生。
- 号单保存 `user_id` 和 `department_id`，不绑定 `doctor_id`，不是指定医生、指定时间的复杂预约。
- 当前源码把同一用户、同一科室、同一服务日期下的 `WAITING` 或 `CALLED` 号单视为活动号单；再次取号时返回原号单，不创建第二张活动号单。
- 排队状态采用 5 秒自动刷新和手动刷新，不做服务器主动推送。
- 管理员叫号由 Ubuntu 命令行客户端执行，板端普通用户没有叫号权限。

### 3. 网络参数

| 项目 | 当前值 |
| --- | --- |
| Ubuntu服务器IP | `192.168.10.41` |
| GEC6818开发板IP | `192.168.10.42` |
| TCP端口 | `9000` |
| 板端程序 | `/IOT/clinic_terminal` |
| 服务端数据库 | `/mnt/hgfs/codex/build/data/clinic.db` |

## 二、演示前一天完成的准备

现场验收时不建议临时修改源码。以下检查应提前完成。

### 1. 主机测试

操作端：Ubuntu终端

```bash
cd /mnt/hgfs/codex
make -B test
full_test_exit=$?
echo "full_test_exit=$full_test_exit"
```

预期：

```text
full_test_exit=0
```

### 2. ARM交叉构建

```bash
cd /mnt/hgfs/codex

make board-clean
board_clean_exit=$?
echo "board_clean_exit=$board_clean_exit"

make -B build-all
build_all_exit=$?
echo "build_all_exit=$build_all_exit"

ls -l \
  build/linux/clinic_server \
  build/linux/clinic_ticket_client \
  build/linux/clinic_ticket_status_client \
  build/linux/clinic_admin_call_client \
  build/board/clinic_terminal \
  build/board/clinic_lvgl_smoke \
  build/board/clinic_net_probe

file \
  build/board/clinic_terminal \
  build/board/clinic_lvgl_smoke \
  build/board/clinic_net_probe
```

判断标准：`board_clean_exit=0`、`build_all_exit=0`，主机与板端产物均存在，三个板端产物被识别为 ARM EABI5 ELF。第三方 LVGL 警告不等于构建失败，以最终退出码和产物为准。

用户随后为排队页定时器生命周期封装回传 `clean=0`、`board_build=0`、`artifact=0`，并确认 GEC6818 生命周期回归通过；完整原始构建日志未在当前记录中保留。若答辩前再次改动源码，仍应重新执行完整构建，不能沿用旧产物。

### 3. 上传开发板

操作端：Windows PowerShell

```powershell
cd E:\codex

scp -O .\build\board\clinic_terminal `
  root@192.168.10.42:/IOT/clinic_terminal

$uploadExit = $LASTEXITCODE
"upload_exit=$uploadExit"

if ($uploadExit -ne 0) {
    throw "clinic_terminal transfer failed"
}
```

预期：`upload_exit=0`。

## 三、现场演示前预清理

### 1. 停止旧进程

操作端：Ubuntu终端

```bash
cd /mnt/hgfs/codex
pkill -x clinic_server 2>/dev/null || true
pkill -x clinic_admin_call_client 2>/dev/null || true
```

开发板旧程序仍在运行时，先在开发板终端按 `Ctrl+C`。

### 2. 选择数据模式

#### 模式A：保留已有演示数据（推荐现场使用）

不删除数据库，只检查现有用户和号单。

#### 模式B：从全新数据库开始

仅在服务器停止后执行：

```bash
cd /mnt/hgfs/codex

if [ -f build/data/clinic.db ]; then
    cp build/data/clinic.db \
      "build/data/clinic.db.demo-backup-$(date +%Y%m%d-%H%M%S)"
fi

rm -f build/data/clinic.db \
      build/data/clinic.db-journal \
      build/data/clinic.db-wal \
      build/data/clinic.db-shm
```

删除后，服务器会重新创建表、科室和医生基础数据；用户需要重新注册。

### 3. 可选：清空记住密码状态

如果要从“记住密码：否”开始演示，先备份再清除：

```sh
cd /IOT

if [ -f .clinic_terminal_credentials ]; then
    cp .clinic_terminal_credentials \
      ".clinic_terminal_credentials.demo-backup-$(date +%Y%m%d-%H%M%S)"
fi

rm -f .clinic_terminal_credentials
```

不要使用 `cat` 展示凭据文件内容。

## 四、现场启动

### 1. 启动服务器

操作端：Ubuntu终端A

```bash
cd /mnt/hgfs/codex
./build/linux/clinic_server
```

预期输出包含：

```text
clinic server listening on 0.0.0.0:9000
clinic database: build/data/clinic.db
```

该终端保持运行，不要关闭。

### 2. 启动开发板程序

操作端：开发板终端

```sh
cd /IOT
chmod +x clinic_terminal
export LD_LIBRARY_PATH=/IOT:/lib:/usr/lib
./clinic_terminal
```

预期：屏幕显示“医路通智慧医疗终端”登录页。

## 五、12分钟正式演示流程

### 第1步：中文输入与注册（约1分钟）

1. 点击“注册账号”。
2. 点击用户名输入框，展示拼音键盘和候选词。
3. 输入用户名、密码和确认密码。
4. 点击“注册”。
5. 注册成功后返回登录页，用户名自动回填，密码保持清空。

讲解口径：

```text
拼音输入法和触摸事件都运行在LVGL主线程。
候选区只保留首屏候选，避免当前LVGL版本的候选翻页越界风险。
```

已有账号时，可以跳过注册，直接进入下一步。

### 第2步：记住密码与登录（约1分钟）

1. 输入正确用户名和密码。
2. 点击“记住密码：否”，切换为“记住密码：是”。
3. 点击“登录”。
4. 登录成功后进入主页，主页显示真实用户 ID。

讲解口径：

```text
只有服务器确认登录成功后才保存凭据。
登录失败不会覆盖此前保存的正确密码。
本地凭据文件权限为0600，但不宣称硬件级加密。
```

可选展示文件权限：

```sh
stat -c 'mode=%a size=%s path=%n' \
  /IOT/.clinic_terminal_credentials
```

只展示权限、大小和路径，不展示文件内容。

### 第3步：科室查询（约1分钟）

1. 主页点击“科室查询”。
2. 展示服务端返回的真实科室列表。
3. 点击一个科室。
4. 确认只显示科室名称和编号，按钮只有“关闭”。
5. 确认没有进入医生列表，也没有创建号单。
6. 返回主页。

### 第4步：医生查询（约1分钟）

1. 主页点击“医生查询”。
2. 在“选择科室筛选医生”页面选择科室。
3. 点击医生条目。
4. 确认详情显示姓名、职称、擅长方向和所属科室。
5. 确认详情框只有“关闭”，没有任何取号按钮。
6. 返回主页。

强调：点击医生条目本身只看详情，不会直接产生号单。

### 第5步：门诊取号（约1分钟）

1. 主页点击“门诊取号”。
2. 选择目标科室。
3. 确认框显示取号科室名称、编号和“确认获取该科室当日排队号”。
4. 先点击“取消”，确认不会创建号单。
5. 再次选择科室并点击“确认取号”。
6. 页面提示号单创建成功并显示本人排队号码。
7. 昨日新增项专项检查：返回主页，再次对同一科室确认取号。
8. 预期进入原号单页面并弹出“您已有有效号单，本次未重复取号，正在显示原号单”，关闭后核对号单 ID 和排队号码没有变化。

讲解口径：

```text
门诊取号直接选择科室。
create_ticket按user_id和department_id取号，板端取号上下文不保存doctor_id。
同一用户、同一科室、同一天已有WAITING或CALLED号单时，服务器返回原活动号单，板端用弹窗解释本次没有重复创建。
```

证据提醒：上述第 7～8 项中的重复取号弹窗仍未完成最新专项回归和 GEC6818 实机确认；未完成专项检查时，正式演示停在第 6 项即可。三种入口的最新演示反馈已记录，但原始逐项日志未保留。

### 第6步：排队查询（约1分钟）

1. 返回主页。
2. 点击“排队查询”。
3. 展示：

   - 本人排队号码；
   - 当前号单状态；
   - 当前科室正在叫的号码；
   - 本人前方 `WAITING` 人数；
   - 号单日期与时间。

没有当前叫号时显示“暂无叫号”。

进入页面后定时器在 LVGL 主线程中等待完整的 5 秒周期；自动刷新与手动刷新共用同一查询入口。

### 第7步：管理员叫号（约1分钟）

操作端：Ubuntu终端B

```bash
cd /mnt/hgfs/codex
./build/linux/clinic_admin_call_client
call_exit=$?
echo "call_exit=$call_exit"
```

程序会从本机 `127.0.0.1:9000` 动态读取科室并显示中文菜单。输入科室前面的数字即可叫号，输入 `r` 刷新科室，输入 `q` 退出。退出后预期 `call_exit=0`。

该无参数交互菜单已由用户在 Ubuntu 上确认“验证通过”；状态文档未保留完整菜单输出和原始退出码，因此不补写具体值。

自动化或故障排查时仍可使用原有完整参数模式：

```bash
./build/linux/clinic_admin_call_client \
  127.0.0.1 9000 8001 1
```

最后一个参数 `1` 是真实科室 ID，必须与号单科室一致；该兼容模式会直接输出服务器 JSON。

### 第8步：手动刷新（约1分钟）

1. 开发板停留在排队页面。
2. 点击“刷新状态”，观察请求期间刷新和返回按钮暂不可用。
3. 展示当前叫号、号单状态和真实叫号时间更新。
4. 请求结束后确认按钮恢复，自动刷新重新等待完整的 5 秒周期。

如果本人的号单不是队首，则当前叫号更新、前方等待人数减少；如果本人正好是队首，则本人状态变为“已叫号”。

### 第9步：断网与恢复（约2分钟）

1. Ubuntu终端A按 `Ctrl+C` 停止服务器。
2. 开发板点击“刷新状态”。
3. 展示连接失败提示、旧数据保留、刷新和返回按钮恢复可用；确认页面不会留下连续重试的旧定时器。
4. Ubuntu重新启动服务器：

```bash
cd /mnt/hgfs/codex
./build/linux/clinic_server
```

5. 开发板再次刷新，确认查询恢复。

### 第10步：退出、回填与清除（约2分钟）

1. 返回主页，点击“退出登录”。
2. 确认回到登录页，用户名和密码自动回填，“记住密码：是”。
3. 使用回填内容再次登录，确认可以进入主页。
4. 再次退出，将按钮改为“记住密码：否”。
5. 使用正确密码登录成功，再次退出。
6. 确认用户名和密码不再自动回填。

可选检查凭据文件已删除：

```sh
if [ ! -e /IOT/.clinic_terminal_credentials ]; then
    echo "credential_removed=1"
else
    echo "credential_removed=0"
fi
```

## 六、后台数据库核验（可选，约2分钟）

只读查询可以在服务器运行时执行；如需修改数据，应先停止服务器。

```bash
cd /mnt/hgfs/codex
sqlite3 build/data/clinic.db
```

```sql
.headers on
.mode column

.tables

SELECT id, username
FROM users
ORDER BY id;

SELECT id, name
FROM departments
ORDER BY id;

SELECT id, department_id, name, title, specialty
FROM doctors
ORDER BY department_id, id;

SELECT
    t.id,
    u.username,
    d.name AS department,
    t.queue_number,
    CASE t.status
        WHEN 0 THEN 'WAITING'
        WHEN 1 THEN 'CALLED'
        WHEN 2 THEN 'COMPLETED'
        WHEN 3 THEN 'CANCELLED'
        ELSE 'UNKNOWN'
    END AS status,
    t.service_date,
    t.called_time
FROM tickets AS t
JOIN users AS u ON u.id = t.user_id
JOIN departments AS d ON d.id = t.department_id
ORDER BY t.service_date, t.department_id, t.queue_number;

.quit
```

安全提醒：演示时不要查询或展示 `users.password`。

## 七、并发编程与网络编程重点讲解

### 1. 并发模型

```text
LVGL主线程：创建页面、处理触摸、消费结果、更新UI、删除对象
网络工作线程：复制稳定参数、连接TCP、发送JSON、接收响应、解析结果、退出
主线程：pthread_join后读取结果
```

关键原则：

- 网络线程不直接调用 LVGL。
- 页面销毁前等待对应 worker 结束。
- worker 参数使用自己的稳定副本。
- 线程结果只消费一次，并正确 `pthread_join`。

### 2. TCP消息边界

TCP没有天然消息边界，一次 `send` 不保证对应一次 `recv`。项目使用：

```text
UTF-8 JSON + \n
```

服务器维护接收缓冲区，处理：

- 半包；
- 粘包；
- 连续多帧；
- 空行和非法JSON；
- 4096消息边界；
- 8192接收缓冲边界。

### 3. epoll与业务分层

```text
epoll监听连接与可读事件
→ Handler完成协议入口
→ clinic_core_handle()执行业务规则
→ clinic_store访问SQLite
```

SQLite只位于服务器端，板端不直接操作数据库。

如果把整个服务器的数据流串起来看，就是开发板先通过 TCP 发起单行 JSON 请求，服务器用 epoll 收到连接和数据后，先交给 Handler 做协议解析和校验，再交给 `clinic_core_handle()` 处理具体业务规则，Core 需要数据时再通过 Store 去访问 SQLite，最后把处理结果封装成 JSON 原路返回给开发板。

这样分层的目的很明确：Handler 只管协议和参数，Core 只管业务判断，Store 只管数据库访问，板端也不直接碰数据库，后续测试和排错都更清晰。

## 八、现场常见故障与快速处理

### 1. 服务器端口被占用

```bash
pkill -x clinic_server 2>/dev/null || true
cd /mnt/hgfs/codex
./build/linux/clinic_server
```

### 2. 板端无法连接服务器

检查：

```sh
ping -c 3 192.168.10.41
```

确认服务器IP为 `192.168.10.41`、端口为 `9000`。

### 3. 上传失败

```powershell
ping 192.168.10.42
scp -O .\build\board\clinic_terminal `
  root@192.168.10.42:/IOT/clinic_terminal
```

必须检查 `$LASTEXITCODE`，失败后不要继续运行旧二进制。

### 4. 管理员叫号返回无等待号单

确认：

- 已使用演示用户在目标科室取号；
- 命令最后的 `department_id` 与号单一致；
- 号单日期为当天；
- 号单状态仍为 `WAITING`。

### 5. 构建出现第三方警告

先看最终 `build_exit`。警告不等于失败；若出现 `undefined reference`、`error:` 或产物不存在，则按失败处理。

### 6. 程序发生段错误

立即停止演示并保留：

```sh
run_exit=$?
echo "run_exit=$run_exit"
```

通常 `139` 表示段错误。不要反复操作覆盖现场信息。

## 九、项目亮点与差异化总结

1. 不是单机本地文件演示，而是 GEC6818 与 Ubuntu 分离的联网系统。
2. 使用 pthread 隔离网络工作与 LVGL UI，体现嵌入式并发编程。
3. 使用 TCP、JSON换行分帧和 epoll，体现完整网络编程链路。
4. 使用服务器端 SQLite 管理用户、科室、医生和号单状态。
5. 实现真实排队语义：本人号码、当前叫号、前方等待人数和叫号时间。
6. 支持断网提示、旧数据保留和网络恢复后重试。
7. 支持中文字体、拼音输入、退出重登和按用户选择记住密码。
8. 当前源码可识别已有活动号单并返回原号单，避免同一科室同一天出现两张活动号单。
9. 管理员叫号客户端支持动态科室中文菜单，现场不必手工输入四个参数。

## 十、验证边界与明确不宣称范围

### 1. 当前验证边界

- 用户已确认统一构建、目标 ARM ABI，以及注册/登录、科室查询、医生查询、门诊取号、排队状态、管理员叫号、手动刷新、异常恢复和退出登录的主要演示流程。
- Ubuntu 管理端无参数交互式叫号菜单已有用户“验证通过”的反馈，但没有保留完整输出和退出码。
- 用户已确认排队页 5 秒自动刷新以及本轮定时器生命周期回归通过；本轮构建回传 `clean=0`、`board_build=0`、`artifact=0`，原始逐项日志未保留。
- 重复取号识别与弹窗在上述整套实机确认之后修改；首次回归因旧测试断言失败，断言已经修正，但状态文档尚无重跑和最新板端实机证据。
- 无号单、`COMPLETED`、`CANCELLED` 和超长科室列表没有专项实机触发证据。

### 2. 明确不宣称完成的范围

- HTTP图形化管理后台；
- 服务器主动推送；
- 指定医生和指定时间的复杂预约；
- 医疗诊断、处方或医疗建议；
- 未做专项实机触发的 `COMPLETED`、`CANCELLED`、无号单和超长列表边界。

## 十一、三十秒收尾话术

```text
医路通完成了从注册登录、科室查询、医生查询、门诊取号、排队查询到管理员叫号的完整闭环。
板端使用LVGL主线程负责界面，pthread工作线程负责TCP/JSON通信；服务器使用epoll处理连接，Handler负责协议解析，clinic_core_handle负责业务判断，Store负责SQLite访问，数据最终原路返回给板端。
项目重点体现了嵌入式图形交互、并发编程、网络编程和医疗排队状态管理，并通过手动刷新、断网保留旧数据、退出重登和记住密码提升了终端稳定性与演示完整性。
```
