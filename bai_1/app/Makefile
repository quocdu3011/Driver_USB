CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pedantic -std=c11
CPPFLAGS += -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -I../driver

CLI_TARGET := secure_file_app
GUI_TARGET := secure_file_gui

COMMON_SRCS := file_io.c driver_client.c hex_utils.c secure_file_service.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)
CLI_OBJS := main.o $(COMMON_OBJS)
GUI_OBJS := gui_main.o $(COMMON_OBJS)

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0 2>/dev/null)

all: $(CLI_TARGET) $(GUI_TARGET)

$(CLI_TARGET): $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CLI_OBJS) -o $@

$(GUI_TARGET): check-gtk $(GUI_OBJS)
	$(CC) $(CFLAGS) $(GUI_OBJS) -o $@ $(GTK_LIBS)

check-gtk:
	@command -v pkg-config >/dev/null 2>&1 || { echo "pkg-config is required to build $(GUI_TARGET)."; exit 1; }
	@pkg-config --exists gtk+-3.0 || { echo "Install libgtk-3-dev to build $(GUI_TARGET)."; exit 1; }

main.o: main.c secure_file_service.h ../driver/ioctl_defs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

gui_main.o: check-gtk
gui_main.o: gui_main.c secure_file_service.h ../driver/ioctl_defs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

%.o: %.c file_io.h driver_client.h hex_utils.h secure_file_service.h ../driver/ioctl_defs.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(CLI_TARGET) $(GUI_TARGET) *.o

.PHONY: all clean check-gtk
