APP_DIR := app
DRIVER_DIR := driver
SCRIPT_DIR := scripts
TEST_DIR := tests

CLI_BIN := $(APP_DIR)/secure_file_app
GUI_BIN := $(APP_DIR)/secure_file_gui
MODULE_BIN := $(DRIVER_DIR)/secure_aes.ko
MODULE_NAME := secure_aes
DEVICE_NODE := /dev/secure_aes

.DEFAULT_GOAL := help

.PHONY: help check-path all driver app cli gui clean \
	load unload reload status logs run-cli run-gui \
	test demo install-deps install-launcher

help:
	@echo "Cac target ho tro cho bai_1:"
	@echo "  make all             Build driver + CLI + GUI"
	@echo "  make driver          Build kernel module"
	@echo "  make app             Build CLI + GUI"
	@echo "  make cli             Build secure_file_app"
	@echo "  make gui             Build secure_file_gui"
	@echo "  make clean           Xoa file build trong driver/ va app/"
	@echo "  make load            Nap driver secure_aes"
	@echo "  make unload          Go driver secure_aes"
	@echo "  make reload          Go roi nap lai driver"
	@echo "  make status          Xem trang thai module va /dev/secure_aes"
	@echo "  make logs            Xem 50 dong dmesg cuoi"
	@echo "  make run-cli         Chay CLI voi --help"
	@echo "  make run-gui         Chay GUI"
	@echo "  make test            Chay test end-to-end"
	@echo "  make demo            Build + load driver + test"
	@echo "  make install-deps    Cai dependency tren Ubuntu/Zorin"
	@echo "  make install-launcher Cai launcher desktop cho GUI"

check-path:
	@if printf '%s' '$(CURDIR)' | grep -q '[[:space:]]'; then \
		echo "Error: Linux kernel external module builds do not work reliably from paths containing spaces."; \
		echo "Move bai_1 to a path without spaces, for example: ~/bai_1"; \
		exit 1; \
	fi

all: check-path driver app

driver: check-path
	$(MAKE) -C $(DRIVER_DIR)

app: check-path
	$(MAKE) -C $(APP_DIR)

cli: check-path
	$(MAKE) -C $(APP_DIR) secure_file_app

gui: check-path
	$(MAKE) -C $(APP_DIR) secure_file_gui

clean:
	$(MAKE) -C $(DRIVER_DIR) clean
	$(MAKE) -C $(APP_DIR) clean

load: check-path
	bash $(SCRIPT_DIR)/load_driver.sh

unload:
	bash $(SCRIPT_DIR)/unload_driver.sh

reload: unload load

status:
	@echo "== lsmod =="
	@lsmod | grep '^$(MODULE_NAME)\b' || echo "Module $(MODULE_NAME) chua duoc nap."
	@echo
	@echo "== Device node =="
	@if [ -e $(DEVICE_NODE) ]; then ls -l $(DEVICE_NODE); else echo "$(DEVICE_NODE) chua ton tai."; fi
	@echo
	@echo "== Build outputs =="
	@if [ -f $(MODULE_BIN) ]; then ls -l $(MODULE_BIN); else echo "$(MODULE_BIN) chua duoc build."; fi
	@if [ -x $(CLI_BIN) ]; then ls -l $(CLI_BIN); else echo "$(CLI_BIN) chua duoc build."; fi
	@if [ -x $(GUI_BIN) ]; then ls -l $(GUI_BIN); else echo "$(GUI_BIN) chua duoc build."; fi

logs:
	sudo dmesg | tail -n 50

run-cli: cli
	./$(CLI_BIN) --help

run-gui: gui
	bash $(SCRIPT_DIR)/run_gui.sh

test: all
	bash $(TEST_DIR)/verify.sh

demo: check-path
	bash $(SCRIPT_DIR)/run_demo.sh

install-deps:
	bash $(SCRIPT_DIR)/install_deps_zorin.sh

install-launcher: gui
	bash $(SCRIPT_DIR)/install_launcher.sh
