obj-m += secure_aes.o
secure_aes-objs := secure_aes_driver.o aes_core.o pkcs7.o

KDIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR := $(CURDIR)

all:
	$(MAKE) -C $(KDIR) M="$(MODULE_DIR)" modules

clean:
	$(MAKE) -C $(KDIR) M="$(MODULE_DIR)" clean

.PHONY: all clean