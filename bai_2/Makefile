MODULE_NAME := usb_guard
USER_APP := usb_guard_monitor
USER_APP_SRC := usb_guard_monitor.cpp
GUI_APP := usb_guard_gui
GUI_APP_SRC := usb_guard_gui.cpp

obj-m += $(MODULE_NAME).o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(CURDIR)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

.PHONY: all module app gui clean load unload reload status logs monitor monitor-json tui gui-run

all: module app gui

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

app: $(USER_APP)

gui: $(GUI_APP)

$(USER_APP): $(USER_APP_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(GUI_APP): $(GUI_APP_SRC)
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(USER_APP) $(GUI_APP)

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

gui-run: gui
	./$(GUI_APP)
