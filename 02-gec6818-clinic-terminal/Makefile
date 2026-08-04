CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
COMMON_SOURCES = common/clinic_protocol.c common/clinic_frame.c common/clinic_net.c
SERVER_SOURCES = $(COMMON_SOURCES) common/clinic_json.c core/clinic_core.c \
	store/clinic_store.c store/clinic_store_sqlite.c \
	server/clinic_server_handler.c server/main.c
STORE_TEST_SOURCES = store/clinic_store.c store/clinic_store_sqlite.c
CORE_TEST_SOURCES = core/clinic_core.c $(STORE_TEST_SOURCES)
JSON_CORE_TEST_SOURCES = common/clinic_json.c $(CORE_TEST_SOURCES)
HANDLER_TEST_SOURCES = common/clinic_protocol.c common/clinic_json.c \
	$(CORE_TEST_SOURCES) server/clinic_server_handler.c

ifeq ($(OS),Windows_NT)
SOCKET_LIBS = -lws2_32
else
SOCKET_LIBS =
endif

BUILD_DIR = build
LINUX_BUILD_DIR = $(BUILD_DIR)/linux
TEST_BUILD_DIR = $(BUILD_DIR)/test
BOARD_TERMINAL_DIR = board/clinic_terminal
BOARD_LVGL_SMOKE_DIR = tools/board/lvgl_smoke
BOARD_NET_PROBE_DIR = tools/board/net_probe
SERVER_LIBS = $(SOCKET_LIBS) -lcjson -lsqlite3
AUTH_CLIENT_LIBS = $(SOCKET_LIBS) -lcjson
DEPARTMENT_CLIENT_LIBS = $(SOCKET_LIBS) -lcjson
DOCTOR_CLIENT_LIBS = $(SOCKET_LIBS) -lcjson
TICKET_CLIENT_LIBS = $(SOCKET_LIBS) -lcjson
JSON_TEST_LIBS = -lcjson
SQLITE_TEST_LIBS = -lsqlite3
JSON_SQLITE_TEST_LIBS = -lcjson -lsqlite3
TCP_AUTH_TEST_LIBS = $(SOCKET_LIBS) -lcjson
TCP_DEPARTMENT_TEST_LIBS = $(SOCKET_LIBS) -lcjson
TCP_DOCTOR_TEST_LIBS = $(SOCKET_LIBS) -lcjson
TCP_TICKET_TEST_LIBS = $(SOCKET_LIBS) -lcjson
BOARD_TRANSPORT_TEST_LIBS = $(SOCKET_LIBS) -pthread
LOCAL_FS_TEST_RUNNER = sh tests/run_in_local_tmp.sh

TEST_TARGETS = \
	$(BUILD_DIR)/test_protocol \
	$(TEST_BUILD_DIR)/test_clinic_json \
	$(TEST_BUILD_DIR)/test_department_json \
	$(TEST_BUILD_DIR)/test_doctor_json \
	$(TEST_BUILD_DIR)/test_ticket_json \
	$(TEST_BUILD_DIR)/test_clinic_store_sqlite \
	$(TEST_BUILD_DIR)/test_department_store \
	$(TEST_BUILD_DIR)/test_doctor_store \
	$(TEST_BUILD_DIR)/test_ticket_store \
	$(TEST_BUILD_DIR)/test_clinic_core \
	$(TEST_BUILD_DIR)/test_department_core \
	$(TEST_BUILD_DIR)/test_doctor_core \
	$(TEST_BUILD_DIR)/test_ticket_core \
	$(TEST_BUILD_DIR)/test_clinic_json_core \
	$(TEST_BUILD_DIR)/test_department_json_core \
	$(TEST_BUILD_DIR)/test_doctor_json_core \
	$(TEST_BUILD_DIR)/test_server_handler \
	$(TEST_BUILD_DIR)/test_department_handler \
	$(TEST_BUILD_DIR)/test_doctor_handler \
	$(TEST_BUILD_DIR)/test_ticket_handler \
	$(TEST_BUILD_DIR)/test_admin_data \
	$(TEST_BUILD_DIR)/test_pinyin_guard \
	$(TEST_BUILD_DIR)/test_credential_store \
	$(TEST_BUILD_DIR)/test_board_transport \
	$(TEST_BUILD_DIR)/test_tcp_auth \
	$(TEST_BUILD_DIR)/test_tcp_departments \
	$(TEST_BUILD_DIR)/test_tcp_doctors \
	$(TEST_BUILD_DIR)/test_tcp_ticket

HOST_TARGETS = \
	$(BUILD_DIR)/clinic_server \
	$(BUILD_DIR)/clinic_ping_client \
	$(BUILD_DIR)/clinic_auth_client \
	$(BUILD_DIR)/test_protocol \
	$(LINUX_BUILD_DIR)/clinic_server \
	$(LINUX_BUILD_DIR)/clinic_ping_client \
	$(LINUX_BUILD_DIR)/clinic_auth_client \
	$(LINUX_BUILD_DIR)/clinic_department_client \
	$(LINUX_BUILD_DIR)/clinic_doctor_client \
	$(LINUX_BUILD_DIR)/clinic_ticket_client \
	$(LINUX_BUILD_DIR)/clinic_ticket_status_client \
	$(LINUX_BUILD_DIR)/clinic_admin_call_client \
	$(LINUX_BUILD_DIR)/host_current_ticket_client

.PHONY: all host build-all test board board-terminal board-lvgl-smoke board-net-probe \
	board-clean clean

all: host

host: $(HOST_TARGETS)

build-all: host board

board: board-terminal board-lvgl-smoke board-net-probe

board-terminal:
	+$(MAKE) -C $(BOARD_TERMINAL_DIR)

board-lvgl-smoke:
	+$(MAKE) -C $(BOARD_LVGL_SMOKE_DIR)

board-net-probe:
	+$(MAKE) -C $(BOARD_NET_PROBE_DIR)

board-clean:
	+$(MAKE) -C $(BOARD_TERMINAL_DIR) clean
	+$(MAKE) -C $(BOARD_LVGL_SMOKE_DIR) clean
	+$(MAKE) -C $(BOARD_NET_PROBE_DIR) clean

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LINUX_BUILD_DIR):
	mkdir -p $(LINUX_BUILD_DIR)

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

$(BUILD_DIR)/clinic_server: $(SERVER_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SERVER_SOURCES) -o $@ $(SERVER_LIBS)

$(BUILD_DIR)/clinic_ping_client: $(COMMON_SOURCES) terminal/host_ping_client.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_SOURCES) terminal/host_ping_client.c -o $@ $(SOCKET_LIBS)

$(BUILD_DIR)/clinic_auth_client: common/clinic_net.c terminal/host_auth_client.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_auth_client.c -o $@ $(AUTH_CLIENT_LIBS)

$(BUILD_DIR)/test_protocol: common/clinic_protocol.c common/clinic_frame.c tests/test_protocol.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_protocol.c common/clinic_frame.c tests/test_protocol.c -o $@ $(SOCKET_LIBS)

$(LINUX_BUILD_DIR)/clinic_server: $(SERVER_SOURCES) | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) $(SERVER_SOURCES) -o $@ $(SERVER_LIBS)

$(LINUX_BUILD_DIR)/clinic_ping_client: $(COMMON_SOURCES) terminal/host_ping_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) $(COMMON_SOURCES) terminal/host_ping_client.c -o $@ $(SOCKET_LIBS)

$(LINUX_BUILD_DIR)/clinic_auth_client: common/clinic_net.c terminal/host_auth_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_auth_client.c -o $@ $(AUTH_CLIENT_LIBS)

$(LINUX_BUILD_DIR)/clinic_department_client: common/clinic_net.c terminal/host_department_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_department_client.c -o $@ $(DEPARTMENT_CLIENT_LIBS)

$(LINUX_BUILD_DIR)/clinic_doctor_client: common/clinic_net.c terminal/host_doctor_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_doctor_client.c -o $@ $(DOCTOR_CLIENT_LIBS)

$(LINUX_BUILD_DIR)/clinic_ticket_client: common/clinic_net.c terminal/host_ticket_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_ticket_client.c -o $@ $(TICKET_CLIENT_LIBS)

$(LINUX_BUILD_DIR)/clinic_ticket_status_client: common/clinic_net.c terminal/host_ticket_status_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_ticket_status_client.c -o $@ $(TICKET_CLIENT_LIBS)

$(LINUX_BUILD_DIR)/clinic_admin_call_client: common/clinic_net.c terminal/host_admin_call_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_admin_call_client.c -o $@ $(TICKET_CLIENT_LIBS)

$(LINUX_BUILD_DIR)/host_current_ticket_client: common/clinic_net.c terminal/host_current_ticket_client.c | $(LINUX_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c terminal/host_current_ticket_client.c -o $@ $(TICKET_CLIENT_LIBS)

$(TEST_BUILD_DIR)/test_clinic_json: common/clinic_json.c tests/test_clinic_json.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_json.c tests/test_clinic_json.c -o $@ $(JSON_TEST_LIBS)

$(TEST_BUILD_DIR)/test_department_json: common/clinic_json.c tests/test_department_json.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_json.c tests/test_department_json.c -o $@ $(JSON_TEST_LIBS)

$(TEST_BUILD_DIR)/test_doctor_json: common/clinic_json.c tests/test_doctor_json.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_json.c tests/test_doctor_json.c -o $@ $(JSON_TEST_LIBS)

$(TEST_BUILD_DIR)/test_ticket_json: common/clinic_json.c tests/test_ticket_json.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_json.c tests/test_ticket_json.c -o $@ $(JSON_TEST_LIBS)

$(TEST_BUILD_DIR)/test_clinic_store_sqlite: $(STORE_TEST_SOURCES) tests/test_clinic_store_sqlite.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(STORE_TEST_SOURCES) tests/test_clinic_store_sqlite.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_department_store: $(STORE_TEST_SOURCES) tests/test_department_store.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(STORE_TEST_SOURCES) tests/test_department_store.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_doctor_store: $(STORE_TEST_SOURCES) tests/test_doctor_store.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(STORE_TEST_SOURCES) tests/test_doctor_store.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_ticket_store: $(STORE_TEST_SOURCES) tests/test_ticket_store.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(STORE_TEST_SOURCES) tests/test_ticket_store.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_clinic_core: $(CORE_TEST_SOURCES) tests/test_clinic_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CORE_TEST_SOURCES) tests/test_clinic_core.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_department_core: $(CORE_TEST_SOURCES) tests/test_department_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CORE_TEST_SOURCES) tests/test_department_core.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_doctor_core: $(CORE_TEST_SOURCES) tests/test_doctor_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CORE_TEST_SOURCES) tests/test_doctor_core.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_ticket_core: $(CORE_TEST_SOURCES) tests/test_ticket_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(CORE_TEST_SOURCES) tests/test_ticket_core.c -o $@ $(SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_clinic_json_core: $(JSON_CORE_TEST_SOURCES) tests/test_clinic_json_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(JSON_CORE_TEST_SOURCES) tests/test_clinic_json_core.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_department_json_core: $(JSON_CORE_TEST_SOURCES) tests/test_department_json_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(JSON_CORE_TEST_SOURCES) tests/test_department_json_core.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_doctor_json_core: $(JSON_CORE_TEST_SOURCES) tests/test_doctor_json_core.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(JSON_CORE_TEST_SOURCES) tests/test_doctor_json_core.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_server_handler: $(HANDLER_TEST_SOURCES) tests/test_server_handler.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(HANDLER_TEST_SOURCES) tests/test_server_handler.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_department_handler: $(HANDLER_TEST_SOURCES) tests/test_department_handler.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(HANDLER_TEST_SOURCES) tests/test_department_handler.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_doctor_handler: $(HANDLER_TEST_SOURCES) tests/test_doctor_handler.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(HANDLER_TEST_SOURCES) tests/test_doctor_handler.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_ticket_handler: $(HANDLER_TEST_SOURCES) tests/test_ticket_handler.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(HANDLER_TEST_SOURCES) tests/test_ticket_handler.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_admin_data: $(HANDLER_TEST_SOURCES) tests/test_admin_data.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(HANDLER_TEST_SOURCES) tests/test_admin_data.c -o $@ $(JSON_SQLITE_TEST_LIBS)

$(TEST_BUILD_DIR)/test_pinyin_guard: board/clinic_terminal/pinyin_guard.c \
	board/clinic_terminal/pinyin_guard.h tests/test_pinyin_guard.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -Iboard/clinic_terminal -Ireference/six/six \
		-Ireference/six/six/lvgl -Ireference/six/six/lvgl/src \
		board/clinic_terminal/pinyin_guard.c tests/test_pinyin_guard.c -o $@

$(TEST_BUILD_DIR)/test_credential_store: board/clinic_terminal/credential_store.c \
	board/clinic_terminal/credential_store.h tests/test_credential_store.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -Iboard/clinic_terminal \
		board/clinic_terminal/credential_store.c tests/test_credential_store.c -o $@

$(TEST_BUILD_DIR)/test_board_transport: board/clinic_terminal/board_transport.c \
	board/clinic_terminal/board_transport.h common/clinic_net.c \
	common/clinic_frame.c tests/test_board_transport.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) -Iboard/clinic_terminal \
		board/clinic_terminal/board_transport.c common/clinic_net.c \
		common/clinic_frame.c tests/test_board_transport.c -o $@ \
		$(BOARD_TRANSPORT_TEST_LIBS)

$(TEST_BUILD_DIR)/test_tcp_auth: common/clinic_net.c tests/test_tcp_auth.c $(LINUX_BUILD_DIR)/clinic_server | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c tests/test_tcp_auth.c -o $@ $(TCP_AUTH_TEST_LIBS)

$(TEST_BUILD_DIR)/test_tcp_departments: common/clinic_net.c tests/test_tcp_departments.c $(LINUX_BUILD_DIR)/clinic_server | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c tests/test_tcp_departments.c -o $@ $(TCP_DEPARTMENT_TEST_LIBS)

$(TEST_BUILD_DIR)/test_tcp_doctors: common/clinic_net.c tests/test_tcp_doctors.c $(LINUX_BUILD_DIR)/clinic_server $(LINUX_BUILD_DIR)/clinic_doctor_client | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c tests/test_tcp_doctors.c -o $@ $(TCP_DOCTOR_TEST_LIBS)

$(TEST_BUILD_DIR)/test_tcp_ticket: common/clinic_net.c tests/test_tcp_ticket.c \
	$(LINUX_BUILD_DIR)/clinic_server \
	$(LINUX_BUILD_DIR)/clinic_ticket_client \
	$(LINUX_BUILD_DIR)/clinic_ticket_status_client \
	$(LINUX_BUILD_DIR)/clinic_admin_call_client \
	$(LINUX_BUILD_DIR)/host_current_ticket_client | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) common/clinic_net.c tests/test_tcp_ticket.c -o $@ $(TCP_TICKET_TEST_LIBS)

test: $(TEST_TARGETS) tests/run_in_local_tmp.sh
	$(BUILD_DIR)/test_protocol
	$(TEST_BUILD_DIR)/test_clinic_json
	$(TEST_BUILD_DIR)/test_department_json
	$(TEST_BUILD_DIR)/test_doctor_json
	$(TEST_BUILD_DIR)/test_ticket_json
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_clinic_store_sqlite)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_department_store)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_doctor_store)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_ticket_store)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_clinic_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_department_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_doctor_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_ticket_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_clinic_json_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_department_json_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_doctor_json_core)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_server_handler)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_department_handler)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_doctor_handler)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_ticket_handler)
	$(LOCAL_FS_TEST_RUNNER) $(abspath $(TEST_BUILD_DIR)/test_admin_data)
	$(TEST_BUILD_DIR)/test_pinyin_guard
	$(TEST_BUILD_DIR)/test_credential_store
	$(TEST_BUILD_DIR)/test_board_transport
	$(TEST_BUILD_DIR)/test_tcp_auth
	$(TEST_BUILD_DIR)/test_tcp_departments
	$(TEST_BUILD_DIR)/test_tcp_doctors
	$(TEST_BUILD_DIR)/test_tcp_ticket

clean:
	rm -rf $(BUILD_DIR)
