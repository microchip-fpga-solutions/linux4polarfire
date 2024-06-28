// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/acpi.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <asm/sbi.h>
#include <asm/processor.h>
#include <asm/timex.h>

unsigned long riscv_timebase __ro_after_init;
EXPORT_SYMBOL_GPL(riscv_timebase);

void __init time_init(void)
{
	struct device_node *cpu;
	struct acpi_table_rhct *rhct;
	acpi_status status;
	int size;
	const __be32 *prop;

	if (acpi_disabled) {
		cpu = of_find_node_by_path("/cpus");
		if (!cpu)
			goto panic_no_freq;

		prop = of_get_property(cpu, "timebase-frequency", &size);
		if (!prop)
			goto panic_no_freq;

		riscv_timebase = of_read_number(prop, size / 4);
		of_clk_init(NULL);
	} else {
		status = acpi_get_table(ACPI_SIG_RHCT, 0, (struct acpi_table_header **)&rhct);
		if (ACPI_FAILURE(status))
			panic("RISC-V ACPI system with no RHCT table\n");

		riscv_timebase = rhct->time_base_freq;
		acpi_put_table((struct acpi_table_header *)rhct);
	}

	lpj_fine = riscv_timebase / HZ;

	timer_probe();

	tick_setup_hrtimer_broadcast();
	return;

panic_no_freq:
	panic("RISC-V system with no 'timebase-frequency' in DTS\n");
}
