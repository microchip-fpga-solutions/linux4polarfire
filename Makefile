# SPDX-License-Identifier: GPL-2.0
#
# Top-level Makefile for out-of-tree kernel modules.
# Auto-discovers all driver subdirectories containing a Makefile
# and builds them against the target kernel.
#
# Usage:
#   make KERNEL_SRC=/path/to/kernel/source
#

DRIVERS := $(shell find drivers -name Makefile | xargs -I{} dirname {})

all:
	@for d in $(DRIVERS); do \
		echo "Building $$d"; \
		$(MAKE) -C $$d KERNEL_SRC=$(KERNEL_SRC) all || exit 1; \
	done

modules_install:
	@for d in $(DRIVERS); do \
		$(MAKE) -C $$d KERNEL_SRC=$(KERNEL_SRC) modules_install || exit 1; \
	done

clean:
	@for d in $(DRIVERS); do \
		$(MAKE) -C $$d KERNEL_SRC=$(KERNEL_SRC) clean; \
	done

.PHONY: all modules_install clean
