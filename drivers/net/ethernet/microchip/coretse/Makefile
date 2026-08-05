# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Microchip CoreTSE network device drivers.
#

#
# For in kernel tree variables
#
obj-$(CONFIG_MICROCHIP_CORETSE) += mchp_coretse.o
obj-$(CONFIG_CORE1588_HWTSTAMP) += mchp_core1588_ptp.o

#
# For out of kernel tree builds
#
ifneq ($(KERNELRELEASE),)
# Called from kernel build system
else

CONFIG_MICROCHIP_CORETSE ?= m
CONFIG_CORE1588_HWTSTAMP ?= y

ifdef KERNEL_SRC
  KERNEL_SRC_DIR := $(KERNEL_SRC)
else
  KERNEL_SRC_DIR ?= /lib/modules/$(shell uname -r)/build
endif

all:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) CONFIG_MICROCHIP_CORETSE=$(CONFIG_MICROCHIP_CORETSE) CONFIG_CORE1588_HWTSTAMP=$(CONFIG_CORE1588_HWTSTAMP) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) CONFIG_MICROCHIP_CORETSE=$(CONFIG_MICROCHIP_CORETSE) CONFIG_CORE1588_HWTSTAMP=$(CONFIG_CORE1588_HWTSTAMP) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) clean

endif
