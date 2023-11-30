// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive EC (extensible cache) PMU driver
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Copyright (C) Eric Lin <eric.lin@sifive.com>
 *
 */

#define pr_fmt(fmt) "ECACHE_PMU: " fmt

#include <linux/cpu_pm.h>
#include <linux/kdebug.h>
#include <linux/bitmap.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/device.h>

#ifdef CONFIG_CPU_PM
#define SIFIVE_EC_CHICKEN_OFF	0x100

u32 sifive_ec_chicken;
void __iomem *ec_base;
#endif

/* ecache pmu counter */
#define ECACHE_PMU_MAX_COUNTERS		32
#define ECACHE_SELECT_BASE		0x2000
#define ECACHE_CLIENT_FILTER_BASE	0x2200
#define ECACHE_COUNTER_INHIBIT_BASE	0x2800
#define ECACHE_COUNTER_BASE		0x3000

#define ECACHE_COUNTER_MASK   GENMASK_ULL(63, 0)

struct ec_slice {
	void __iomem *base;
};

/* ecache pmu event */
struct sifive_ecache_pmu {
	struct pmu pmu;
	struct ec_slice *slice;
	unsigned int slice_count;
	struct hlist_node node;
	cpumask_t cpumask;
	struct perf_event *events[ECACHE_PMU_MAX_COUNTERS];
	DECLARE_BITMAP(used_mask, ECACHE_PMU_MAX_COUNTERS);
	u32 counters;
	struct notifier_block	ecache_pm_nb;
};

#define to_ecache_pmu(p) (container_of(p, struct sifive_ecache_pmu, pmu))

#ifndef readq
static inline unsigned long long readq(void __iomem *addr)
{
	return readl(addr) | (((unsigned long long)readl(addr + 4)) << 32LL);
}
#endif

#ifndef writeq
static inline void writeq(unsigned long long v, void __iomem *addr)
{
	writel(lower_32_bits(v), addr);
	writel(upper_32_bits(v), addr + 4);
}
#endif

/*
 * Add sysfs attributes
 *
 * We export:
 * - formats, used by perf user space and other tools to configure events
 * - events, used by perf user space and other tools to create events
 *   symbolically, e.g.:
 *     perf stat -a -e sifive_ecache_pmu/event=inner_put_partial_data_hit/ ls
 *     perf stat -a -e sifive_ecache_pmu/event=0x101/ ls
 * - cpumask, used by perf user space and other tools to know on which CPUs
 */

/* cpumask */
static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr,
						char *buf)
{
	struct sifive_ecache_pmu *ecache_pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, &ecache_pmu->cpumask);
};

static DEVICE_ATTR_RO(cpumask);

static struct attribute *sifive_ecache_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group sifive_ecache_pmu_cpumask_attr_group = {
	.attrs = sifive_ecache_pmu_cpumask_attrs,
};

/* formats */
static ssize_t sifive_ecache_pmu_format_show(struct device *dev,
					     struct device_attribute *attr,
						char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

#define SIFIVE_ECACHE_PMU_FORMAT_ATTR(_name, _config)                      \
	(&((struct dev_ext_attribute[]) {                                      \
		{ .attr = __ATTR(_name, 0444, sifive_ecache_pmu_format_show, NULL),\
		  .var = (void *)_config, }                                        \
	})[0].attr.attr)

static struct attribute *sifive_ecache_pmu_formats[] = {
	SIFIVE_ECACHE_PMU_FORMAT_ATTR(event, "config:0-63"),
	NULL,
};

static struct attribute_group sifive_ecache_pmu_format_group = {
	.name = "format",
	.attrs = sifive_ecache_pmu_formats,
};

/* events */
static ssize_t sifive_ecache_pmu_event_show(struct device *dev,
					    struct device_attribute *attr,
					  char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define SET_EVENT_SELECT(_event, _set)  (((u64)1 << ((_event) + 8)) | (_set))
#define ECACHE_PMU_EVENT_ATTR(_name, _event, _set)        \
	PMU_EVENT_ATTR_ID(_name, sifive_ecache_pmu_event_show,\
			     SET_EVENT_SELECT(_event, _set))

enum ecache_pmu_event_set1 {
	INNER_REQUEST = 0,
	INNER_RD_REQUEST,
	INNER_WR_REQUEST,
	INNER_PF_REQUEST,
	OUTER_PRB_REQUEST,
	INNER_REQUEST_HIT,
	INNER_RD_REQUEST_HIT,
	INNER_WR_REQUEST_HIT,
	INNER_PF_REQUEST_HIT,
	OUTER_PRB_REQUEST_HIT,
	INNER_REQUEST_HITPF,
	INNER_RD_REQUEST_HITPF,
	INNER_WR_REQUEST_HITPF,
	INNER_PF_REQUEST_HITPF,
	OUTER_PRB_REQUEST_HITPF,
	INNER_REQUEST_MISS,
	INNER_RD_REQUEST_MISS,
	INNER_WR_REQUEST_MISS,
	INNER_PF_REQUEST_MISS,
	OUTER_PRB_REQUEST_MISS,
	ECACHE_PMU_MAX_EVENT1_IDX
};

enum ecache_pmu_event_set2 {
	OUTER_REQUEST = 0,
	OUTER_RD_REQUEST,
	OUTER_PUT_REQUEST,
	OUTER_EV_REQUEST,
	OUTER_PF_REQUEST,
	INNER_PRB_REQUEST,
	INNER_REQUEST_WCYC,
	INNER_RD_REQUEST_WCYC,
	INNER_WR_REQUEST_WCYC,
	INNER_PF_REQUEST_WCYC,
	OUTER_PRB_REQUEST_WCYC,
	OUTER_REQUEST_WCYC,
	OUTER_RD_REQUEST_WCYC,
	OUTER_PUT_REQUEST_WCYC,
	OUTER_EV_REQUEST_WCYC,
	OUTER_PF_REQUEST_WCYC,
	INNER_PRB_REQUEST_WCYC,
	INNER_AG_WCYC,
	INNER_AP_WCYC,
	INNER_AH_WCYC,
	INNER_BP_WCYC,
	INNER_CP_WCYC,
	INNER_CX_WCYC,
	INNER_DG_WCYC,
	INNER_DP_WCYC,
	INNER_DX_WCYC,
	INNER_EG_WCYC,
	OUTER_AG_WCYC,
	OUTER_AP_WCYC,
	OUTER_AH_WCYC,
	OUTER_BP_WCYC,
	OUTER_CP_WCYC,
	OUTER_CX_WCYC,
	OUTER_DG_WCYC,
	OUTER_DP_WCYC,
	OUTER_DX_WCYC,
	OUTER_EG_WCYC,
	ECACHE_PMU_MAX_EVENT2_IDX
};

static struct attribute *sifive_ecache_pmu_events[] = {
	/*  pmEventSelect1 */
	ECACHE_PMU_EVENT_ATTR(inner_request, INNER_REQUEST, 1),
	ECACHE_PMU_EVENT_ATTR(inner_rd_request, INNER_RD_REQUEST, 1),
	ECACHE_PMU_EVENT_ATTR(inner_wr_request, INNER_WR_REQUEST, 1),
	ECACHE_PMU_EVENT_ATTR(inner_pf_request, INNER_PF_REQUEST, 1),
	ECACHE_PMU_EVENT_ATTR(outer_prb_request, OUTER_PRB_REQUEST, 1),
	ECACHE_PMU_EVENT_ATTR(inner_request_hit, INNER_REQUEST_HIT,  1),
	ECACHE_PMU_EVENT_ATTR(inner_rd_request_hit, INNER_RD_REQUEST_HIT, 1),
	ECACHE_PMU_EVENT_ATTR(inner_wr_request_hit, INNER_WR_REQUEST_HIT, 1),
	ECACHE_PMU_EVENT_ATTR(inner_pf_request_hit, INNER_PF_REQUEST_HIT, 1),
	ECACHE_PMU_EVENT_ATTR(outer_prb_request_hit, OUTER_PRB_REQUEST_HIT, 1),
	ECACHE_PMU_EVENT_ATTR(inner_request_hitpf, INNER_REQUEST_HITPF,  1),
	ECACHE_PMU_EVENT_ATTR(inner_rd_request_hitpf, INNER_RD_REQUEST_HITPF, 1),
	ECACHE_PMU_EVENT_ATTR(inner_wr_request_hitpf, INNER_WR_REQUEST_HITPF, 1),
	ECACHE_PMU_EVENT_ATTR(inner_pf_request_hitpf, INNER_PF_REQUEST_HITPF, 1),
	ECACHE_PMU_EVENT_ATTR(outer_prb_request_hitpf, OUTER_PRB_REQUEST_HITPF, 1),
	ECACHE_PMU_EVENT_ATTR(inner_request_miss, INNER_REQUEST_MISS,  1),
	ECACHE_PMU_EVENT_ATTR(inner_rd_request_miss, INNER_RD_REQUEST_MISS, 1),
	ECACHE_PMU_EVENT_ATTR(inner_wr_request_miss, INNER_WR_REQUEST_MISS, 1),
	ECACHE_PMU_EVENT_ATTR(inner_pf_request_miss, INNER_PF_REQUEST_MISS, 1),
	ECACHE_PMU_EVENT_ATTR(outer_prb_request_miss, OUTER_PRB_REQUEST_MISS, 1),

	/*  pmEventSelect2 */
	ECACHE_PMU_EVENT_ATTR(outer_request, OUTER_REQUEST, 2),
	ECACHE_PMU_EVENT_ATTR(outer_rd_request, OUTER_RD_REQUEST, 2),
	ECACHE_PMU_EVENT_ATTR(outer_put_request, OUTER_PUT_REQUEST, 2),
	ECACHE_PMU_EVENT_ATTR(outer_ev_request, OUTER_EV_REQUEST, 2),
	ECACHE_PMU_EVENT_ATTR(outer_pf_request, OUTER_PF_REQUEST, 2),
	ECACHE_PMU_EVENT_ATTR(inner_prb_request, INNER_PRB_REQUEST, 2),
	ECACHE_PMU_EVENT_ATTR(inner_request_wcyc, INNER_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_rd_request_wcyc, INNER_RD_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_wr_request_wcyc, INNER_WR_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_pf_request_wcyc, INNER_PF_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_prb_request_wcyc, OUTER_PRB_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_request_wcyc, OUTER_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_rd_request_wcyc, OUTER_RD_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_put_request_wcyc, OUTER_PUT_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_ev_request_wcyc, OUTER_EV_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_pf_request_wcyc, OUTER_PF_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_prb_request_wcyc, INNER_PRB_REQUEST_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_ag_wcyc, INNER_AG_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_ap_wcyc, INNER_AP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_ah_wcyc, INNER_AH_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_bp_wcyc, INNER_BP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_cp_wcyc, INNER_CP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_cx_wcyc, INNER_CX_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_dg_wcyc, INNER_DG_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_dp_wcyc, INNER_DP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_dx_wcyc, INNER_DX_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(inner_eg_wcyc, INNER_EG_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_ag_wcyc, OUTER_AG_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_ap_wcyc, OUTER_AP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_ah_wcyc, OUTER_AH_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_bp_wcyc, OUTER_BP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_cp_wcyc, OUTER_CP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_cx_wcyc, OUTER_CX_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_dg_wcyc, OUTER_DG_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_dp_wcyc, OUTER_DP_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_dx_wcyc, OUTER_DX_WCYC, 2),
	ECACHE_PMU_EVENT_ATTR(outer_eg_wcyc, OUTER_EG_WCYC, 2),
	NULL
};

static struct attribute_group sifive_ecache_pmu_events_group = {
	.name = "events",
	.attrs = sifive_ecache_pmu_events,
};

/*
 * Per PMU device attribute groups
 */
static const struct attribute_group *sifive_ecache_pmu_attr_grps[] = {
	&sifive_ecache_pmu_format_group,
	&sifive_ecache_pmu_events_group,
	&sifive_ecache_pmu_cpumask_attr_group,
	NULL,
};

/*
 * Low-level functions: reading and writing counters
 */
static inline u64 read_counter(struct sifive_ecache_pmu *ecache_pmu, int idx)
{
	void __iomem *counter_base;
	u64 ec_count = 0;
	int i;

	if (WARN_ON_ONCE(idx < 0 || idx > ecache_pmu->counters))
		return -EINVAL;

	for (i = 0; i < ecache_pmu->slice_count; ++i) {
		counter_base = ecache_pmu->slice[i].base + ECACHE_COUNTER_BASE;
		ec_count = ec_count + readq(counter_base + idx * 8);
	}

	return ec_count;
}

static void sifive_ecache_pmu_disable(struct sifive_ecache_pmu *ecache_pmu,
				      struct hw_perf_event *hwc)
{
	unsigned long ctr_inhbt = 0;
	int i;

	for (i = 0; i < ecache_pmu->slice_count; ++i) {
		/* Disable counter to count events */
		ctr_inhbt = readq(ecache_pmu->slice[i].base + ECACHE_COUNTER_INHIBIT_BASE);
		set_bit(hwc->idx, &ctr_inhbt);
		writeq(ctr_inhbt, ecache_pmu->slice[i].base + ECACHE_COUNTER_INHIBIT_BASE);
	}
}

static void sifive_ecache_pmu_enable(struct sifive_ecache_pmu *ecache_pmu,
				     struct hw_perf_event *hwc)
{
	unsigned long ctr_inhbt = 0;
	int i;

	for (i = 0; i < ecache_pmu->slice_count; ++i) {
		/* Reset counter value */
		writeq(0, ecache_pmu->slice[i].base + ECACHE_COUNTER_BASE + (hwc->idx * 8));

		/* Set event selector to these events */
		writeq(hwc->config, ecache_pmu->slice[i].base +
				ECACHE_SELECT_BASE + (hwc->idx * 8));

		/* Enable counter to count events */
		ctr_inhbt = readq(ecache_pmu->slice[i].base + ECACHE_COUNTER_INHIBIT_BASE);
		clear_bit(hwc->idx, &ctr_inhbt);
		writeq(ctr_inhbt, ecache_pmu->slice[i].base + ECACHE_COUNTER_INHIBIT_BASE);
	}
}

/*
 * pmu->read: read and update the counter
 */
static void sifive_ecache_pmu_read(struct perf_event *event)
{
	struct sifive_ecache_pmu *ecache_pmu = to_ecache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = read_counter(ecache_pmu, idx);

		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	/* delta is the value to update the counter we maintain in the kernel. */
	delta = (new_raw_count - prev_raw_count) & ECACHE_COUNTER_MASK;
	local64_add(delta, &event->count);
}

/*
 * State transition functions:
 *
 * stop()/start() & add()/del()
 */

/*
 * pmu->stop: stop the counter
 */
static void sifive_ecache_pmu_stop(struct perf_event *event, int flags)
{
	struct sifive_ecache_pmu *ecache_pmu = to_ecache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) &&	!(hwc->state & PERF_HES_UPTODATE)) {
		sifive_ecache_pmu_read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
	sifive_ecache_pmu_disable(ecache_pmu, hwc);
}

/*
 * pmu->start: start the event.
 */
static void sifive_ecache_pmu_start(struct perf_event *event, int flags)
{
	struct sifive_ecache_pmu *ecache_pmu = to_ecache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	hwc->state = 0;
	perf_event_update_userpage(event);

	/* Set initial value 0 */
	local64_set(&hwc->prev_count, 0);
	sifive_ecache_pmu_enable(ecache_pmu, hwc);
}

/*
 * pmu->add: add the event to PMU.
 */
static int sifive_ecache_pmu_add(struct perf_event *event, int flags)
{
	struct sifive_ecache_pmu *ecache_pmu = to_ecache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	u64 config = event->attr.config;
	u64 set = config & 0xff;
	u64 ev_type = config >> 8;

	/* Check if this is a valid set and event. */
	switch (set) {
	case 1:
		if (ev_type >= (1ULL << ECACHE_PMU_MAX_EVENT1_IDX))
			return -ENOENT;
		break;
	case 2:
		if (ev_type >= (1ULL << ECACHE_PMU_MAX_EVENT2_IDX))
			return -ENOENT;
		break;
	case 0:
	default:
		return -ENOENT;
	}

	for (idx = 0; idx < ecache_pmu->counters; ++idx) {
		/* To find a counter for this event. */
		if (!test_and_set_bit(idx, ecache_pmu->used_mask))
			break;
	}

	/* The counters are all in use. */
	if (idx == ecache_pmu->counters)
		return -EAGAIN;

	/* Found an available counter idx for this event. */
	hwc->idx = idx;
	ecache_pmu->events[hwc->idx] = event;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		sifive_ecache_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

/*
 * pmu->del: delete the event from PMU.
 */
static void sifive_ecache_pmu_del(struct perf_event *event, int flags)
{
	struct sifive_ecache_pmu *ecache_pmu = to_ecache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	/* Stop the counter and release this counter. */
	ecache_pmu->events[hwc->idx] = NULL;
	sifive_ecache_pmu_stop(event, PERF_EF_UPDATE);
	clear_bit(hwc->idx, ecache_pmu->used_mask);
	perf_event_update_userpage(event);
}

/*
 * Event Initialization
 */
static int sifive_ecache_pmu_event_init(struct perf_event *event)
{
	struct sifive_ecache_pmu *ecache_pmu = to_ecache_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	/* Don't allocate hw counter yet. */
	hwc->idx = -1;
	hwc->config = event->attr.config;

	event->cpu = cpumask_first(&ecache_pmu->cpumask);

	return 0;
}

static int sifive_ecache_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct sifive_ecache_pmu *ecache_pmu = hlist_entry_safe(node,
								struct sifive_ecache_pmu, node);

	if (cpumask_empty(&ecache_pmu->cpumask))
		cpumask_set_cpu(cpu, &ecache_pmu->cpumask);

	return 0;
}

static int sifive_ecache_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct sifive_ecache_pmu *ecache_pmu = hlist_entry_safe(node,
								struct sifive_ecache_pmu, node);
	unsigned int target;

	/* Clear this cpu in cpumask */
	if (!cpumask_test_and_clear_cpu(cpu, &ecache_pmu->cpumask))
		return 0;

	/* Pick up a random online cpu */
	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	/* Migrate pmu event from cpu to target cpu */
	perf_pmu_migrate_context(&ecache_pmu->pmu, cpu, target);

	/* Set target cpu to cpumask */
	cpumask_set_cpu(target, &ecache_pmu->cpumask);

	return 0;
}

#ifdef CONFIG_CPU_PM
#define SIFIVE_EC_CHICKEN_OFF	0x100
static int sifive_ec_suspend(void)
{
	sifive_ec_chicken = readl((char *)ec_base + SIFIVE_EC_CHICKEN_OFF);

	return 0;
}

static int sifive_ec_resume(void)
{
	writel(sifive_ec_chicken, (char *)ec_base + SIFIVE_EC_CHICKEN_OFF);

	return 0;
}

static int sifive_ec_pm_notify(struct notifier_block *b, unsigned long cmd,
			       void *v)
{
	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		sifive_ec_suspend();
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		sifive_ec_resume();
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int sifive_ec_pm_pmu_notify(struct notifier_block *b, unsigned long cmd,
				   void *v)
{
	struct sifive_ecache_pmu *ec_pmu = container_of(b, struct sifive_ecache_pmu, ecache_pm_nb);
	struct perf_event *event;
	int idx;
	int enabled = bitmap_weight(ec_pmu->used_mask, ec_pmu->counters);

	if (!enabled)
		return NOTIFY_OK;

	for (idx = 0; idx < ec_pmu->counters; idx++) {
		event = ec_pmu->events[idx];
		if (!event)
			continue;

		switch (cmd) {
		case CPU_PM_ENTER:
			/* Stop and update the counter */
			sifive_ecache_pmu_stop(event, PERF_EF_UPDATE);
			break;
		case CPU_PM_ENTER_FAILED:
		case CPU_PM_EXIT:
			sifive_ecache_pmu_start(event, PERF_EF_RELOAD);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block sifive_ec_pm_notifier_block = {
	.notifier_call = sifive_ec_pm_notify,
};

static int sifive_ec_pm_pmu_register(struct sifive_ecache_pmu *pmu)
{
	pmu->ecache_pm_nb.notifier_call = sifive_ec_pm_pmu_notify;
	return cpu_pm_register_notifier(&pmu->ecache_pm_nb);
}

static void sifive_ec_pm_pmu_unregister(struct sifive_ecache_pmu *pmu)
{
	cpu_pm_unregister_notifier(&pmu->ecache_pm_nb);
}
#else
static inline int sifive_ec_pm_pmu_register(struct sifive_ecache_pmu *pmu) { return 0; }
static inline void sifive_ec_pm_pmu_unregister(struct sifive_ecache_pmu *pmu) { }
#endif

static void sifive_ecache_pmu_destroy(struct sifive_ecache_pmu *pmu)
{
	sifive_ec_pm_pmu_unregister(pmu);
	cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_SIFIVE_ECACHE_ONLINE, &pmu->node);
}

static void sifive_ec_pmu_init(struct sifive_ecache_pmu *ecache_pmu)
{
	int i;

	for (i = 0; i < ecache_pmu->slice_count; ++i) {
		writeq(-1, ecache_pmu->slice[i].base + ECACHE_COUNTER_INHIBIT_BASE);
		writeq(0, ecache_pmu->slice[i].base + ECACHE_CLIENT_FILTER_BASE);
	}
}

static int sifive_ecache_pmu_dev_probe(struct platform_device *pdev)
{
	struct device_node *ecache_node = pdev->dev.of_node;
	struct device_node *child_node;
	struct resource res;
	struct sifive_ecache_pmu *ecache_pmu;
	struct ec_slice *ec_slice;
	char *name;
	unsigned int child_counters = 0;
	int ret, slice_count, i = 0;

	ecache_pmu = devm_kzalloc(&pdev->dev, sizeof(*ecache_pmu), GFP_KERNEL);
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "sifive_ecache_pmu");
	if (!ecache_pmu || !name)
		return -ENOMEM;

	platform_set_drvdata(pdev, ecache_pmu);
	ecache_pmu->pmu = (struct pmu) {
		.task_ctx_nr = perf_invalid_context,
		.event_init = sifive_ecache_pmu_event_init,
		.add        = sifive_ecache_pmu_add,
		.del        = sifive_ecache_pmu_del,
		.start      = sifive_ecache_pmu_start,
		.stop       = sifive_ecache_pmu_stop,
		.read       = sifive_ecache_pmu_read,
		.attr_groups = sifive_ecache_pmu_attr_grps,
		.capabilities = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
	};

	slice_count = of_get_child_count(ecache_node);
	if (!slice_count) {
		ret = -ENODEV;
		goto early_err;
	}

	ecache_pmu->slice = kcalloc(slice_count, sizeof(*ec_slice), GFP_KERNEL);
	if (!ecache_pmu->slice) {
		ret = -ENOMEM;
		goto early_err;
	}

	ecache_pmu->counters = ECACHE_PMU_MAX_COUNTERS;
	for_each_child_of_node(ecache_node, child_node) {
		if (of_address_to_resource(child_node, 0, &res)) {
			ret = -ENODEV;
			goto err_node_put;
		}
		ecache_pmu->slice[i].base = ioremap(res.start, resource_size(&res));
		if (!ecache_pmu->slice[i].base) {
			ret = -ENOMEM;
			goto err_unmap;
		}
		i++;

		/* Get counter numbers from sub node */
		ret = of_property_read_u32(child_node, "sifive,perfmon-counters", &child_counters);
		if (ret) {
			pr_err("Not found sifive,perfmon-counters property\n");
			goto err_unmap;
		}
		ecache_pmu->counters = min(child_counters, ecache_pmu->counters);
	}

	pr_info("perfmon-counters: %d\n", ecache_pmu->counters);

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_RISCV_SIFIVE_ECACHE_ONLINE,
				       &ecache_pmu->node);
	if (ret) {
		pr_err("Failed to register hotplug instance: %d\n", ret);
		goto err_unmap;
	}

	ret = perf_pmu_register(&ecache_pmu->pmu, name, -1);
	if (ret) {
		cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_SIFIVE_ECACHE_ONLINE,
					    &ecache_pmu->node);
		pr_err("Failed to register sifive_ecache_pmu.pmu: %d\n", ret);
		goto err_unmap;
	}

	ecache_pmu->slice_count = slice_count;
	sifive_ec_pmu_init(ecache_pmu);

	ret = sifive_ec_pm_pmu_register(ecache_pmu);
	if (ret)
		goto out_unregister;

#ifdef CONFIG_CPU_PM
	ec_base = ecache_pmu->slice[0].base;
	cpu_pm_register_notifier(&sifive_ec_pm_notifier_block);
#endif

	return 0;

out_unregister:
	sifive_ecache_pmu_destroy(ecache_pmu);
err_unmap:
	while (--i >= 0)
		iounmap(ecache_pmu->slice[i].base);
err_node_put:
	of_node_put(child_node);
	of_node_put(ecache_node);
early_err:
	return ret;
}

static const struct of_device_id sifive_ecache_ids[] = {
	{ .compatible = "sifive,extensiblecache0" },
	{ /* sentinel value */ }
};

static struct platform_driver sifive_ecache_pmu_driver = {
	.driver = {
		   .name = "SiFive-ECACHE-PMU",
		   .of_match_table = sifive_ecache_ids,
		   },
	.probe = sifive_ecache_pmu_dev_probe,
};

static int __init sifive_ecache_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_RISCV_SIFIVE_ECACHE_ONLINE,
				      "perf/sifive/ecache:online",
				     sifive_ecache_pmu_online_cpu,
				     sifive_ecache_pmu_offline_cpu);
	if (ret)
		pr_err("Failed to register CPU hotplug notifier %d\n", ret);

	ret = platform_driver_register(&sifive_ecache_pmu_driver);
	if (ret)
		pr_err("Failed to register sifive_ecache_pmu_driver: %d\n", ret);

	return ret;
}
device_initcall(sifive_ecache_pmu_init);
