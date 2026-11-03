# SPDX-License-Identifier: GPL-2.0
#
# Makefile for MPFS Motor Control IIO drivers
#

#
# For in kernel tree builds
#
obj-$(CONFIG_IIO_MPFS_MC_ADC) += mpfs_mc_adc.o
obj-$(CONFIG_IIO_MPFS_MC_PICON) += mpfs_mc_picon.o
obj-$(CONFIG_IIO_MPFS_MC_PWM) += mpfs_mc_pwm.o
obj-$(CONFIG_IIO_MPFS_MC_RATELIM) += mpfs_mc_ratelim.o
obj-$(CONFIG_IIO_MPFS_MC_SQMNG) += mpfs_mc_sqmng.o
obj-$(CONFIG_IIO_MPFS_MC_STPTHETA) += mpfs_mc_stptheta.o

#
# For out of kernel tree builds
#
ifneq ($(KERNELRELEASE),)
# Called from kernel build system
else

CONFIG_IIO_MPFS_MC_ADC ?= m
CONFIG_IIO_MPFS_MC_PICON ?= m
CONFIG_IIO_MPFS_MC_PWM ?= m
CONFIG_IIO_MPFS_MC_RATELIM ?= m
CONFIG_IIO_MPFS_MC_SQMNG ?= m
CONFIG_IIO_MPFS_MC_STPTHETA ?= m

ifdef KERNEL_SRC
  KERNEL_SRC_DIR := $(KERNEL_SRC)
else
  KERNEL_SRC_DIR ?= /lib/modules/$(shell uname -r)/build
endif

MC_IIO_CONFIGS := \
	CONFIG_IIO_MPFS_MC_ADC=$(CONFIG_IIO_MPFS_MC_ADC) \
	CONFIG_IIO_MPFS_MC_PICON=$(CONFIG_IIO_MPFS_MC_PICON) \
	CONFIG_IIO_MPFS_MC_PWM=$(CONFIG_IIO_MPFS_MC_PWM) \
	CONFIG_IIO_MPFS_MC_RATELIM=$(CONFIG_IIO_MPFS_MC_RATELIM) \
	CONFIG_IIO_MPFS_MC_SQMNG=$(CONFIG_IIO_MPFS_MC_SQMNG) \
	CONFIG_IIO_MPFS_MC_STPTHETA=$(CONFIG_IIO_MPFS_MC_STPTHETA)

all:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) $(MC_IIO_CONFIGS) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) $(MC_IIO_CONFIGS) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(CURDIR) clean

endif
