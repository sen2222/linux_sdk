MODULE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KERNELDIR ?= $(abspath $(MODULE_DIR)/../../../../system/kernel)
CURRENT_PATH := $(MODULE_DIR)
obj-m := led_dts_chdev.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean
