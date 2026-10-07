#
# (C) Copyright 2000-2003
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.
#
# SPDX-License-Identifier:	GPL-2.0+
#

extra-y	:= start.o

obj-y	+= cache_v7.o

obj-y	+= cpu.o cp15.o
obj-y	+= syslib.o

ifneq ($(CONFIG_MX6),)
ifneq ($(CONFIG_SKIP_LOWLEVEL_INIT),y)
obj-y += lowlevel_init.o
endif
endif

ifneq ($(CONFIG_ARMV7_NONSEC),)
obj-y += nonsec_virt.o virt-v7.o virt-dt.o
endif

obj-$(CONFIG_ARMV7_PSCI) += psci.o
obj-$(CONFIG_SYS_ARCH_TIMER) += arch_timer.o
obj-$(CONFIG_MX6) += mx6/
