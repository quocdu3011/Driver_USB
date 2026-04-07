MODULE_NAME := usb_guard
USER_APP := usb_guard_monitor
USER_APP_SRC := usb_guard_monitor.cpp

obj-m += $(MODULE_NAME).o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(CURDIR)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

.PHONY: all module app clean load unload reload status logs monitor monitor-json tui

all: module app

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

app: $(USER_APP)

$(USER_APP): $(USER_APP_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(USER_APP)

load: module
	sudo insmod ./$(MODULE_NAME).ko $(if $(WHITELIST),whitelist=$(WHITELIST),)

unload:
	-sudo rmmod $(MODULE_NAME)

reload: unload load

status:
	cat /proc/$(MODULE_NAME)/status

logs:
	sudo dmesg | tail -n 50

monitor: app
	./$(USER_APP) --watch 2

monitor-json: app
	./$(USER_APP) --json

tui: app
	./$(USER_APP) --tui --watch 1
