# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Microchip Time Sensitive Network device
#

#
# For in kernel tree variables
#
obj-$(CONFIG_MICROCHIP_CORETSN) += microchip-tsn.o

#
# For out of kernel tree builds
#
ifneq ($(KERNELRELEASE),)
# Called from kernel build system
else

CONFIG_MICROCHIP_CORETSN ?= m

ifdef KERNEL_SRC
  KERNEL_SRC_DIR := $(KERNEL_SRC)
else
  KERNEL_SRC_DIR ?= /lib/modules/$(shell uname -r)/build
endif

all:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) CONFIG_MICROCHIP_CORETSN=$(CONFIG_MICROCHIP_CORETSN) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) CONFIG_MICROCHIP_CORETSN=$(CONFIG_MICROCHIP_CORETSN) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) clean

endif
