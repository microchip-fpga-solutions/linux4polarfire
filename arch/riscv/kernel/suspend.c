// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/cpu_pm.h>
#include <linux/ftrace.h>
#include <linux/thread_info.h>
#include <asm/csr.h>
#include <asm/suspend.h>

void suspend_save_csrs(struct suspend_context *context)
{
	context->scratch = csr_read(CSR_SCRATCH);
	context->tvec = csr_read(CSR_TVEC);
	context->ie = csr_read(CSR_IE);

	/*
	 * No need to save/restore IP CSR (i.e. MIP or SIP) because:
	 *
	 * 1. For no-MMU (M-mode) kernel, the bits in MIP are set by
	 *    external devices (such as interrupt controller, timer, etc).
	 * 2. For MMU (S-mode) kernel, the bits in SIP are set by
	 *    M-mode firmware and external devices (such as interrupt
	 *    controller, etc).
	 */

#ifdef CONFIG_MMU
	context->satp = csr_read(CSR_SATP);
#endif
}

void suspend_restore_csrs(struct suspend_context *context)
{
	csr_write(CSR_SCRATCH, context->scratch);
	csr_write(CSR_TVEC, context->tvec);
	csr_write(CSR_IE, context->ie);

#ifdef CONFIG_MMU
	csr_write(CSR_SATP, context->satp);
#endif
}

int cpu_suspend(unsigned long arg,
		int (*finish)(unsigned long arg,
			      unsigned long entry,
			      unsigned long context))
{
	int rc = 0;
	struct suspend_context context = { 0 };

	/* Finisher should be non-NULL */
	if (!finish)
		return -EINVAL;

	/* Save additional CSRs*/
	suspend_save_csrs(&context);

	/*
	 * Function graph tracer state gets incosistent when the kernel
	 * calls functions that never return (aka finishers) hence disable
	 * graph tracing during their execution.
	 */
	pause_graph_tracing();

	/* Save context on stack */
	if (__cpu_suspend_enter(&context)) {
		/* Call the finisher */
		rc = finish(arg, __pa_symbol(__cpu_resume_enter),
			    (ulong)&context);

		/*
		 * Should never reach here, unless the suspend finisher
		 * fails. Successful cpu_suspend() should return from
		 * __cpu_resume_entry()
		 */
		if (!rc)
			rc = -EOPNOTSUPP;
	}

	/* Enable function graph tracer */
	unpause_graph_tracing();

	/* Restore additional CSRs */
	suspend_restore_csrs(&context);

	return rc;
}

#ifdef CONFIG_CPU_PM
static int cpu_ext_suspend(struct notifier_block *self, unsigned long cmd,
			   void *v)
{
	struct task_struct *cur_task = get_current();
	struct pt_regs *regs = task_pt_regs(cur_task);

	switch (cmd) {
	case CPU_PM_ENTER:
#ifdef CONFIG_FPU
		if (has_fpu()) {
			if (unlikely(regs->status & SR_SD))
				fstate_save(cur_task, regs);
		}
#endif
#ifdef CONFIG_VECTOR
		if (has_vector()) {
			if (unlikely(regs->status & SR_SD))
				vstate_save(cur_task, regs);
		}
#endif

		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
#ifdef CONFIG_FPU
		if (has_fpu())
			fstate_restore(cur_task, regs);
#endif
#ifdef CONFIG_VECTOR
		if (has_vector())
			vstate_restore(cur_task, regs);
#endif
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block cpu_pm_notifier_block = {
	.notifier_call = cpu_ext_suspend,
};

void cpu_pm_suspend_init(void)
{
	cpu_pm_register_notifier(&cpu_pm_notifier_block);
}

#else
static inline void cpu_pm_suspend_init(void) { }
#endif /* CONFIG_CPU_PM */
