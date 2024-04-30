// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive composable cache controller Driver
 *
 * Copyright (C) 2018-2022 SiFive, Inc.
 *
 */

#define pr_fmt(fmt) "CCACHE: " fmt

#include <linux/align.h>
#include <linux/kdebug.h>
#include <linux/bitmap.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/bitfield.h>
#include <linux/cpu_pm.h>
#include <linux/pm.h>

#include <asm/cacheflush.h>
#include <asm/cacheinfo.h>
#include <asm/dma-noncoherent.h>
#include <soc/sifive/sifive_ccache.h>

/* ccache pmu counter */
#define SIFIVE_CCACHE_PMU_MAX_COUNTERS 64
#define SIFIVE_CCACHE_SELECT_BASE_OFFSET 0x2000
#define SIFIVE_CCACHE_CLIENT_FILTER_BASE_OFFSET 0x2800
#define SIFIVE_CCACHE_COUNTER_BASE_OFFSET 0x3000

#define SIFIVE_CCACHE_DIRECCFIX_LOW 0x100
#define SIFIVE_CCACHE_DIRECCFIX_HIGH 0x104
#define SIFIVE_CCACHE_DIRECCFIX_COUNT 0x108

#define SIFIVE_CCACHE_DIRECCFAIL_LOW 0x120
#define SIFIVE_CCACHE_DIRECCFAIL_HIGH 0x124
#define SIFIVE_CCACHE_DIRECCFAIL_COUNT 0x128

#define SIFIVE_CCACHE_DATECCFIX_LOW 0x140
#define SIFIVE_CCACHE_DATECCFIX_HIGH 0x144
#define SIFIVE_CCACHE_DATECCFIX_COUNT 0x148

#define SIFIVE_CCACHE_DATECCFAIL_LOW 0x160
#define SIFIVE_CCACHE_DATECCFAIL_HIGH 0x164
#define SIFIVE_CCACHE_DATECCFAIL_COUNT 0x168

#define SIFIVE_CCACHE_CONFIG 0x00
#define SIFIVE_CCACHE_CONFIG_BANK_MASK GENMASK_ULL(7, 0)
#define SIFIVE_CCACHE_CONFIG_WAYS_MASK GENMASK_ULL(15, 8)
#define SIFIVE_CCACHE_CONFIG_SETS_MASK GENMASK_ULL(23, 16)
#define SIFIVE_CCACHE_CONFIG_BLKS_MASK GENMASK_ULL(31, 24)

#define SIFIVE_CCACHE_FLUSH64 0x200
#define SIFIVE_CCACHE_FLUSH32 0x240

#define SIFIVE_CCACHE_WAYENABLE 0x08
#define SIFIVE_CCACHE_ECCINJECTERR 0x40
#define SIFIVE_CCACHE_WAYMASKS 0x800
#define SIFIVE_CCACHE_FEATUREDISABLE 0x1000

#define SIFIVE_CCACHE_MAX_ECCINTR 4
#define SIFIVE_CCACHE_LINE_SIZE 64

/* ccache pmu event */
struct sifive_ccache_pmu_event {
	struct perf_event **events;
	void __iomem *event_counter_base;
	void __iomem *event_select_base;
	u32 counters;
	DECLARE_BITMAP(used_mask, SIFIVE_CCACHE_PMU_MAX_COUNTERS);
};

struct sifive_ccache_pmu {
	struct pmu *pmu;
	struct hlist_node node;
	cpumask_t cpumask;
};

struct sifive_ccache_state {
	u32 masters;
	u32 wayenable;
	u32 featuredisable;
	u32 *waymask;
	u64 pmclientfilter;
};

static struct sifive_ccache_pmu sifive_ccache_pmu;
static struct sifive_ccache_pmu_event sifive_ccache_pmu_event;
static struct sifive_ccache_state sifive_ccache_state;

/* ccache */
static void __iomem *ccache_base;
static int g_irq[SIFIVE_CCACHE_MAX_ECCINTR];
static struct riscv_cacheinfo_ops ccache_cache_ops;
static int level;

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
 *     perf stat -a -e sifive_ccache_pmu/event=inner_put_partial_data_hit/ ls
 *     perf stat -a -e sifive_ccache_pmu/event=0x101/ ls
 * - cpumask, used by perf user space and other tools to know on which CPUs
 */

/* cpumask */
static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr,
						char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &sifive_ccache_pmu.cpumask);
};

static DEVICE_ATTR_RO(cpumask);

static struct attribute *sifive_ccache_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group sifive_ccache_pmu_cpumask_attr_group = {
	.attrs = sifive_ccache_pmu_cpumask_attrs,
};

/* formats */
static ssize_t sifive_ccache_pmu_format_show(struct device *dev,
					     struct device_attribute *attr,
						char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

#define SIFIVE_CCACHE_PMU_FORMAT_ATTR(_name, _config)				      \
	(&((struct dev_ext_attribute[]) {				      \
		{ .attr = __ATTR(_name, 0444, sifive_ccache_pmu_format_show, NULL), \
		  .var = (void *)_config, }				      \
	})[0].attr.attr)

static struct attribute *sifive_ccache_pmu_formats[] = {
	SIFIVE_CCACHE_PMU_FORMAT_ATTR(event, "config:0-63"),
	NULL,
};

static struct attribute_group sifive_ccache_pmu_format_group = {
	.name = "format",
	.attrs = sifive_ccache_pmu_formats,
};

/* events */
static ssize_t sifive_ccache_pmu_event_show(struct device *dev,
					    struct device_attribute *attr,
					  char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define SET_EVENT_SELECT(_event, _set)  (((u64)1 << ((_event) + 8)) | (_set))
#define CCACHE_PMU_EVENT_ATTR(_name, _event, _set) \
	PMU_EVENT_ATTR_ID(_name, sifive_ccache_pmu_event_show, \
			     SET_EVENT_SELECT(_event, _set))

enum ccache_pmu_event_set1 {
	INNER_PUT_FULL_DATA = 0,
	INNER_PUT_PARTIAL_DATA,
	INNER_ATOMIC_DATA,
	INNER_GET,
	INNER_PREFETCH_READ,
	INNER_PREFETCH_WRITE,
	INNER_ACQUIRE_BLOCK_NTOB,
	INNER_ACQUIRE_BLOCK_NTOT,
	INNER_ACQUIRE_BLOCK_BTOT,
	INNER_ACQUIRE_PERM_NTOT,
	INNER_ACQUIRE_PERM_BTOT,
	INNER_RELEASE_TTOB,
	INNER_RELEASE_TTON,
	INNER_RELEASE_BTON,
	INNER_RELEASE_DATA_TTOB,
	INNER_RELEASE_DATA_TTON,
	INNER_RELEASE_DATA_BTON,
	INNER_PROBE_BLOCK_TOT,
	INNER_PROBE_BLOCK_TOB,
	INNER_PROBE_BLOCK_TON,
	CCACHE_PMU_MAX_EVENT1_IDX
};

enum ccache_pmu_event_set2 {
	INNER_PUT_FULL_DATA_HIT = 0,
	INNER_PUT_PARTIAL_DATA_HIT,
	INNER_ATOMIC_DATA_HIT,
	INNER_GET_HIT,
	INNER_PREFETCH_HIT,
	INNER_ACQUIRE_BLOCK_HIT,
	INNER_ACQUIRE_PERM_HIT,
	INNER_RELEASE_HIT,
	INNER_RELEASE_DATA_HIT,
	OUTER_PROBE_HIT,
	INNER_PUT_FULL_DATA_HIT_SHARED,
	INNER_PUT_PARTIAL_DATA_HIT_SHARED,
	INNER_ATOMIC_DATA_HIT_SHARED,
	INNER_GET_HIT_SHARED,
	INNER_PREFETCH_HIT_SHARED,
	INNER_ACQUIRE_BLOCK_HIT_SHARED,
	INNER_ACQUIRE_PERM_HIT_SHARED,
	OUTER_PROBE_HIT_SHARED,
	OUTER_PROBE_HIT_DIRTY,
	CCACHE_PMU_MAX_EVENT2_IDX
};

enum ccache_pmu_event_set3 {
	OUTER_ACQUIRE_BLOCK_NTOB_MISS = 0,
	OUTER_ACQUIRE_BLOCK_NTOT_MISS,
	OUTER_ACQUIRE_BLOCK_BTOT_MISS,
	OUTER_ACQUIRE_PERM_NTOT_MISS,
	OUTER_ACQUIRE_PERM_BTOT_MISS,
	OUTER_RELEASE_TTOB_EVICTION,
	OUTER_RELEASE_TTON_EVICTION,
	OUTER_RELEASE_BTON_EVICTION,
	OUTER_RELEASE_DATA_TTOB_NOT_APPLICABLE,
	OUTER_RELEASE_DATA_TTON_DIRTY_EVICTION,
	OUTER_RELEASE_DATA_BTON_NOT_APPLICABLE,
	INNER_PROBE_BLOCK_TOT_CODE_MISS_HITS_OTHER_HARTS,
	INNER_PROBE_BLOCK_TOB_LOAD_MISS_HITS_OTHER_HARTS,
	INNER_PROBE_BLOCK_TON_STORE_MISS_HITS_OTHER_HARTS,
	CCACHE_PMU_MAX_EVENT3_IDX
};

enum ccache_pmu_event_set4 {
	INNER_HINT_HITS = 0,
	CCACHE_PMU_MAX_EVENT4_IDX
};

static struct attribute *sifive_ccache_pmu_events[] = {
	/*  pmEventSelect1 */
	CCACHE_PMU_EVENT_ATTR(inner_put_full_data, INNER_PUT_FULL_DATA, 1),
	CCACHE_PMU_EVENT_ATTR(inner_put_partial_data, INNER_PUT_PARTIAL_DATA, 1),
	CCACHE_PMU_EVENT_ATTR(inner_atomic_data, INNER_ATOMIC_DATA, 1),
	CCACHE_PMU_EVENT_ATTR(inner_get, INNER_GET, 1),
	CCACHE_PMU_EVENT_ATTR(inner_prefetch_read, INNER_PREFETCH_READ, 1),
	CCACHE_PMU_EVENT_ATTR(inner_prefetch_write, INNER_PREFETCH_WRITE, 1),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_block_ntob, INNER_ACQUIRE_BLOCK_NTOB, 1),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_block_ntot, INNER_ACQUIRE_BLOCK_NTOT, 1),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_block_btot, INNER_ACQUIRE_BLOCK_BTOT, 1),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_perm_ntot, INNER_ACQUIRE_PERM_NTOT, 1),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_perm_btot, INNER_ACQUIRE_PERM_BTOT, 1),
	CCACHE_PMU_EVENT_ATTR(inner_release_ttob, INNER_RELEASE_TTOB, 1),
	CCACHE_PMU_EVENT_ATTR(inner_release_tton, INNER_RELEASE_TTON, 1),
	CCACHE_PMU_EVENT_ATTR(inner_release_bton, INNER_RELEASE_BTON, 1),
	CCACHE_PMU_EVENT_ATTR(inner_release_data_ttob, INNER_RELEASE_DATA_TTOB, 1),
	CCACHE_PMU_EVENT_ATTR(inner_release_data_tton, INNER_RELEASE_DATA_TTON, 1),
	CCACHE_PMU_EVENT_ATTR(inner_release_data_bton, INNER_RELEASE_DATA_BTON, 1),
	CCACHE_PMU_EVENT_ATTR(inner_probe_block_tot, INNER_PROBE_BLOCK_TOT, 1),
	CCACHE_PMU_EVENT_ATTR(inner_probe_block_tob, INNER_PROBE_BLOCK_TOB, 1),
	CCACHE_PMU_EVENT_ATTR(inner_probe_block_ton, INNER_PROBE_BLOCK_TON, 1),

	/*  pmEventSelect2 */
	CCACHE_PMU_EVENT_ATTR(inner_put_full_data_hit, INNER_PUT_FULL_DATA_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_put_partial_data_hit, INNER_PUT_PARTIAL_DATA_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_atomic_data_hit, INNER_ATOMIC_DATA_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_get_hit, INNER_GET_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_prefetch_hit, INNER_PREFETCH_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_block_hit, INNER_ACQUIRE_BLOCK_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_perm_hit, INNER_ACQUIRE_PERM_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_release_hit, INNER_RELEASE_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_release_data_hit, INNER_RELEASE_DATA_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(outer_probe_hit, OUTER_PROBE_HIT, 2),
	CCACHE_PMU_EVENT_ATTR(inner_put_full_data_hit_shared, INNER_PUT_FULL_DATA_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(inner_put_partial_data_hit_shared,
			      INNER_PUT_PARTIAL_DATA_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(inner_atomic_data_hit_shared, INNER_ATOMIC_DATA_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(inner_get_hit_shared, INNER_GET_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(inner_prefetch_hit_shared, INNER_PREFETCH_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_block_hit_shared, INNER_ACQUIRE_BLOCK_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(inner_acquire_perm_hit_shared, INNER_ACQUIRE_PERM_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(outer_probe_hit_shared, OUTER_PROBE_HIT_SHARED, 2),
	CCACHE_PMU_EVENT_ATTR(outer_probe_hit_dirty, OUTER_PROBE_HIT_DIRTY, 2),

	/*  pmEventSelect3 */
	CCACHE_PMU_EVENT_ATTR(outer_acquire_block_ntob_miss, OUTER_ACQUIRE_BLOCK_NTOB_MISS, 3),
	CCACHE_PMU_EVENT_ATTR(outer_acquire_block_ntot_miss, OUTER_ACQUIRE_BLOCK_NTOT_MISS, 3),
	CCACHE_PMU_EVENT_ATTR(outer_acquire_block_btot_miss, OUTER_ACQUIRE_BLOCK_BTOT_MISS, 3),
	CCACHE_PMU_EVENT_ATTR(outer_acquire_perm_ntot_miss, OUTER_ACQUIRE_PERM_NTOT_MISS, 3),
	CCACHE_PMU_EVENT_ATTR(outer_acquire_perm_btot_miss, OUTER_ACQUIRE_PERM_BTOT_MISS, 3),
	CCACHE_PMU_EVENT_ATTR(outer_release_ttob_eviction, OUTER_RELEASE_TTOB_EVICTION, 3),
	CCACHE_PMU_EVENT_ATTR(outer_release_tton_eviction, OUTER_RELEASE_TTON_EVICTION, 3),
	CCACHE_PMU_EVENT_ATTR(outer_release_bton_eviction, OUTER_RELEASE_BTON_EVICTION, 3),
	CCACHE_PMU_EVENT_ATTR(outer_release_data_ttob_not_applicable,
			      OUTER_RELEASE_DATA_TTOB_NOT_APPLICABLE, 3),
	CCACHE_PMU_EVENT_ATTR(outer_release_data_tton_drity_eviction,
			      OUTER_RELEASE_DATA_TTON_DIRTY_EVICTION, 3),
	CCACHE_PMU_EVENT_ATTR(outer_release_data_bton_not_applicable,
			      OUTER_RELEASE_DATA_BTON_NOT_APPLICABLE, 3),
	CCACHE_PMU_EVENT_ATTR(inner_probe_block_tot_code_miss_hits_other_harts,
			      INNER_PROBE_BLOCK_TOT_CODE_MISS_HITS_OTHER_HARTS, 3),
	CCACHE_PMU_EVENT_ATTR(inner_probe_block_tob_load_miss_hits_other_harts,
			      INNER_PROBE_BLOCK_TOB_LOAD_MISS_HITS_OTHER_HARTS, 3),
	CCACHE_PMU_EVENT_ATTR(inner_probe_block_ton_store_miss_hits_other_harts,
			      INNER_PROBE_BLOCK_TON_STORE_MISS_HITS_OTHER_HARTS, 3),

	/*  pm_event_select4 */
	CCACHE_PMU_EVENT_ATTR(inner_hint_hits, INNER_HINT_HITS, 4),
	NULL
};

static struct attribute_group sifive_ccache_pmu_events_group = {
	.name = "events",
	.attrs = sifive_ccache_pmu_events,
};

/*
 * Per PMU device attribute groups
 */
static const struct attribute_group *sifive_ccache_pmu_attr_grps[] = {
	&sifive_ccache_pmu_format_group,
	&sifive_ccache_pmu_events_group,
	&sifive_ccache_pmu_cpumask_attr_group,
	NULL,
};

/*
 * Low-level functions: reading and writing counters
 */
static inline u64 read_counter(int idx)
{
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;

	if (WARN_ON_ONCE(idx < 0 || idx > ptr->counters))
		return -EINVAL;

	return readq(ptr->event_counter_base + idx * 8);
}

static inline void write_counter(int idx, u64 val)
{
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;

	writeq(val, ptr->event_counter_base + idx * 8);
}

/*
 * pmu->read: read and update the counter
 */
static void sifive_ccache_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = read_counter(idx);

		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	/* delta is the value to update the counter we maintain in the kernel. */
	delta = (new_raw_count - prev_raw_count) & ((1ULL << 63) - 1);
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
static void sifive_ccache_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		sifive_ccache_pmu.pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}

	/* Disable this counter to count events */
	writeq(0, ptr->event_select_base + (hwc->idx * 8));
}

/*
 * pmu->start: start the event.
 */
static void sifive_ccache_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	hwc->state = 0;
	perf_event_update_userpage(event);

	/* Set initial value 0 */
	local64_set(&hwc->prev_count, 0);
	write_counter(hwc->idx, 0);

	/* Enable counter to count these events */
	writeq(hwc->config, ptr->event_select_base + (hwc->idx * 8));
}

/*
 * pmu->add: add the event to PMU.
 */
static int sifive_ccache_pmu_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;
	int idx;
	u64 config = event->attr.config;
	u64 set = config & 0xff;
	u64 ev_type = config >> 8;

	/* Check if this is a valid set and event. */
	switch (set) {
	case 1:
		if (ev_type >= (1ULL << CCACHE_PMU_MAX_EVENT1_IDX))
			return -ENOENT;
		break;
	case 2:
		if (ev_type >= (1ULL << CCACHE_PMU_MAX_EVENT2_IDX))
			return -ENOENT;
		break;
	case 3:
		if (ev_type >= (1ULL << CCACHE_PMU_MAX_EVENT3_IDX))
			return -ENOENT;
		break;
	case 4:
		if (ev_type >= (1ULL << CCACHE_PMU_MAX_EVENT4_IDX))
			return -ENOENT;
		break;
	case 0:
	default:
		return -ENOENT;
	}

	for (idx = 0; idx < ptr->counters; ++idx) {
		/* To find a counter for this event. */
		if (!test_and_set_bit(idx, ptr->used_mask))
			break;
	}

	/* The counters are all in use. */
	if (idx == ptr->counters)
		return -EAGAIN;

	/* Found an available counter idx for this event. */
	hwc->idx = idx;
	ptr->events[hwc->idx] = event;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		sifive_ccache_pmu.pmu->start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

/*
 * pmu->del: delete the event from PMU.
 */
static void sifive_ccache_pmu_del(struct perf_event *event, int flags)
{
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;
	struct hw_perf_event *hwc = &event->hw;

	/* Stop the counter and release this counter. */
	ptr->events[hwc->idx] = NULL;
	sifive_ccache_pmu.pmu->stop(event, PERF_EF_UPDATE);
	clear_bit(hwc->idx, ptr->used_mask);
	perf_event_update_userpage(event);
}

/*
 * Event Initialization/Finalization
 */
static int sifive_ccache_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* Don't allocat hw counter yet. */
	hwc->idx = -1;
	hwc->config = event->attr.config;

	event->cpu = cpumask_first(&sifive_ccache_pmu.cpumask);

	return 0;
}

/*
 * Initialization
 */
static struct pmu sifive_ccache_generic_pmu = {
	.name		= "sifive_ccache_pmu",
	.task_ctx_nr	= perf_invalid_context,
	.event_init	= sifive_ccache_pmu_event_init,
	.add		= sifive_ccache_pmu_add,
	.del		= sifive_ccache_pmu_del,
	.start		= sifive_ccache_pmu_start,
	.stop		= sifive_ccache_pmu_stop,
	.read		= sifive_ccache_pmu_read,
	.attr_groups	= sifive_ccache_pmu_attr_grps,
	.capabilities   = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
};

static struct sifive_ccache_pmu sifive_ccache_pmu = {
	.pmu = &sifive_ccache_generic_pmu,
};

static int sifive_ccache_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct sifive_ccache_pmu *ptr = hlist_entry_safe(node, struct sifive_ccache_pmu, node);

	if (cpumask_empty(&ptr->cpumask))
		cpumask_set_cpu(cpu, &ptr->cpumask);

	return 0;
}

static int sifive_ccache_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct sifive_ccache_pmu *ptr = hlist_entry_safe(node, struct sifive_ccache_pmu, node);
	unsigned int target;

	/* Clear this cpu in cpumask */
	if (!cpumask_test_and_clear_cpu(cpu, &ptr->cpumask))
		return 0;

	/* Pick up a random online cpu */
	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	/* Migrate pmu event from cpu to target cpu */
	perf_pmu_migrate_context(ptr->pmu, cpu, target);

	/* Set target cpu to cpumask */
	cpumask_set_cpu(target, &ptr->cpumask);

	return 0;
}

static int sifive_ccache_suspend(void)
{
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;
	static struct sifive_ccache_state *state = &sifive_ccache_state;
	struct perf_event *event;
	int idx;
	int enabled_event = bitmap_weight(ptr->used_mask, ptr->counters);

	state->wayenable = readl(ccache_base + SIFIVE_CCACHE_WAYENABLE);
	state->featuredisable = readl(ccache_base + SIFIVE_CCACHE_FEATUREDISABLE);
	for (idx = 0; idx < state->masters; idx++)
		state->waymask[idx] = readl(ccache_base + SIFIVE_CCACHE_WAYMASKS +
						idx * sizeof(u64));

	if (!enabled_event)
		return 0;

	for (idx = 0; idx < ptr->counters; idx++) {
		event = ptr->events[idx];
		if (!event)
			continue;

		sifive_ccache_pmu_stop(event, PERF_EF_UPDATE);
	}
	state->pmclientfilter = readq(ccache_base +
				      SIFIVE_CCACHE_CLIENT_FILTER_BASE_OFFSET);
	return 0;
}

static int sifive_ccache_resume(void)
{
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;
	static struct sifive_ccache_state *state = &sifive_ccache_state;
	struct perf_event *event;
	int idx;
	int enabled_event = bitmap_weight(ptr->used_mask, ptr->counters);

	writel(state->wayenable, ccache_base + SIFIVE_CCACHE_WAYENABLE);
	writel(state->featuredisable, ccache_base + SIFIVE_CCACHE_FEATUREDISABLE);
	for (idx = 0; idx < state->masters; idx++)
		writel(state->waymask[idx], ccache_base + SIFIVE_CCACHE_WAYMASKS +
						idx * sizeof(u64));

	if (!enabled_event)
		return 0;

	for (idx = 0; idx < ptr->counters; idx++) {
		event = ptr->events[idx];
		if (!event)
			continue;

		sifive_ccache_pmu_start(event, PERF_EF_RELOAD);
	}
	writeq(state->pmclientfilter, ccache_base +
		SIFIVE_CCACHE_CLIENT_FILTER_BASE_OFFSET);

	return 0;
}

static int sifive_ccache_dev_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = -EINVAL;
	struct device_node *ccache_node = pdev->dev.of_node;
	struct sifive_ccache_pmu_event *ptr = &sifive_ccache_pmu_event;
	static struct sifive_ccache_state *state = &sifive_ccache_state;
	void __iomem *ccache_base;

	/* Get counter numbers. */
	ret = of_property_read_u32(ccache_node, "sifive,perfmon-counters", &ptr->counters);
	if (ret) {
		pr_err("Not found sifive,perfmon-counters property\n");
		goto early_err;
	}
	pr_info("perfmon-counters: %d\n", ptr->counters);

	/* Allocate perf_event. */
	ptr->events = kcalloc(ptr->counters, sizeof(struct perf_event), GFP_KERNEL);

	/* Get master numbers. */
	ret = of_property_read_u32(ccache_node, "sifive,max-master-id", &state->masters);
	if (ret) {
		pr_err("Not found sifive,max-master-id property\n");
		goto early_err;
	}
	pr_info("masters: %d\n", state->masters);
	state->waymask = kcalloc(state->masters, sizeof(u32), GFP_KERNEL);

	/* Set base address of select and counter registers. */
	ccache_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ccache_base)) {
		ret = PTR_ERR(ccache_base);
		goto probe_err;
	}

	/* Print CCACHE configs. */
	ptr->event_select_base = ccache_base + SIFIVE_CCACHE_SELECT_BASE_OFFSET;
	ptr->event_counter_base = ccache_base + SIFIVE_CCACHE_COUNTER_BASE_OFFSET;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_RISCV_SIFIVE_CCACHE_ONLINE,
				       &sifive_ccache_pmu.node);
	if (ret) {
		pr_err("Failed to register hotplug instance: %d\n", ret);
		goto early_err;
	}

	ret = perf_pmu_register(sifive_ccache_pmu.pmu, sifive_ccache_pmu.pmu->name, -1);
	if (ret) {
		cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_SIFIVE_CCACHE_ONLINE,
					    &sifive_ccache_pmu.node);
		pr_err("Failed to register sifive_ccache_pmu.pmu: %d\n", ret);
		goto early_err;
	}

	return 0;

probe_err:
	/* Free memory. */
	kfree(ptr->events);

early_err:
	return ret;
}


enum {
	QUIRK_NONSTANDARD_CACHE_OPS	= BIT(0),
	QUIRK_BROKEN_DATA_UNCORR	= BIT(1),
};

static const struct of_device_id sifive_ccache_ids[] = {
	{ .compatible = "sifive,fu540-c000-ccache" },
	{ .compatible = "sifive,fu740-c000-ccache" },
	{ .compatible = "starfive,jh7100-ccache",
	  .data = (void *)(QUIRK_NONSTANDARD_CACHE_OPS | QUIRK_BROKEN_DATA_UNCORR) },
	{ .compatible = "microchip,mpfs-ccache",
	  .data = (void *)(QUIRK_NONSTANDARD_CACHE_OPS) },
	{ .compatible = "sifive,ccache0" },
	{ /* end of table */ }
};

static struct platform_driver sifive_ccache_driver = {
	.driver = {
		   .name = "SiFive-CCACHE",
		   .of_match_table = sifive_ccache_ids,
		   },
	.probe = sifive_ccache_dev_probe,
};

enum {
	DIR_CORR = 0,
	DATA_CORR,
	DATA_UNCORR,
	DIR_UNCORR,
};

#ifdef CONFIG_DEBUG_FS
static struct dentry *sifive_test;

static ssize_t ccache_write(struct file *file, const char __user *data,
			    size_t count, loff_t *ppos)
{
	unsigned int val;

	if (kstrtouint_from_user(data, count, 0, &val))
		return -EINVAL;
	if ((val < 0xFF) || (val >= 0x10000 && val < 0x100FF))
		writel(val, ccache_base + SIFIVE_CCACHE_ECCINJECTERR);
	else
		return -EINVAL;
	return count;
}

static const struct file_operations ccache_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = ccache_write
};

static void setup_sifive_debug(void)
{
	sifive_test = debugfs_create_dir("sifive_ccache_cache", NULL);

	debugfs_create_file("sifive_debug_inject_error", 0200,
			    sifive_test, NULL, &ccache_fops);
}
#endif

static void ccache_config_read(void)
{
	u32 cfg;

	cfg = readl(ccache_base + SIFIVE_CCACHE_CONFIG);
	pr_info("%llu banks, %llu ways, sets/bank=%llu, bytes/block=%llu\n",
		FIELD_GET(SIFIVE_CCACHE_CONFIG_BANK_MASK, cfg),
		FIELD_GET(SIFIVE_CCACHE_CONFIG_WAYS_MASK, cfg),
		BIT_ULL(FIELD_GET(SIFIVE_CCACHE_CONFIG_SETS_MASK, cfg)),
		BIT_ULL(FIELD_GET(SIFIVE_CCACHE_CONFIG_BLKS_MASK, cfg)));

	cfg = readl(ccache_base + SIFIVE_CCACHE_WAYENABLE);
	pr_info("Index of the largest way enabled: %u\n", cfg);
}

static ATOMIC_NOTIFIER_HEAD(ccache_err_chain);

int register_sifive_ccache_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ccache_err_chain, nb);
}
EXPORT_SYMBOL_GPL(register_sifive_ccache_error_notifier);

int unregister_sifive_ccache_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ccache_err_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_sifive_ccache_error_notifier);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
static void ccache_flush_range(phys_addr_t start, size_t len)
{
	phys_addr_t end = start + len;
	phys_addr_t line;

	if (!len)
		return;

	mb();
	for (line = ALIGN_DOWN(start, SIFIVE_CCACHE_LINE_SIZE); line < end;
			line += SIFIVE_CCACHE_LINE_SIZE) {
#ifdef CONFIG_32BIT
		writel(line >> 4, ccache_base + SIFIVE_CCACHE_FLUSH32);
#else
		writeq(line, ccache_base + SIFIVE_CCACHE_FLUSH64);
#endif
		mb();
	}
}

static const struct riscv_nonstd_cache_ops ccache_mgmt_ops __initconst = {
	.wback = &ccache_flush_range,
	.inv = &ccache_flush_range,
	.wback_inv = &ccache_flush_range,
};
#endif /* CONFIG_RISCV_NONSTANDARD_CACHE_OPS */

static int ccache_largest_wayenabled(void)
{
	return readl(ccache_base + SIFIVE_CCACHE_WAYENABLE) & 0xFF;
}

static ssize_t number_of_ways_enabled_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return sysfs_emit(buf, "%u\n", ccache_largest_wayenabled());
}

static DEVICE_ATTR_RO(number_of_ways_enabled);

static struct attribute *priv_attrs[] = {
	&dev_attr_number_of_ways_enabled.attr,
	NULL,
};

static const struct attribute_group priv_attr_group = {
	.attrs = priv_attrs,
};

static const struct attribute_group *ccache_get_priv_group(struct cacheinfo
							   *this_leaf)
{
	/* We want to use private group for composable cache only */
	if (this_leaf->level == level)
		return &priv_attr_group;
	else
		return NULL;
}

static irqreturn_t ccache_int_handler(int irq, void *device)
{
	unsigned int add_h, add_l;

	if (irq == g_irq[DIR_CORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DIRECCFIX_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DIRECCFIX_LOW);
		pr_err("DirError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DirError interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DIRECCFIX_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_CE,
					   "DirECCFix");
	}
	if (irq == g_irq[DIR_UNCORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DIRECCFAIL_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DIRECCFAIL_LOW);
		/* Reading this register clears the DirFail interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DIRECCFAIL_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_UE,
					   "DirECCFail");
		panic("CCACHE: DirFail @ 0x%08X.%08X\n", add_h, add_l);
	}
	if (irq == g_irq[DATA_CORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DATECCFIX_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DATECCFIX_LOW);
		pr_err("DataError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataError interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DATECCFIX_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_CE,
					   "DatECCFix");
	}
	if (irq == g_irq[DATA_UNCORR]) {
		add_h = readl(ccache_base + SIFIVE_CCACHE_DATECCFAIL_HIGH);
		add_l = readl(ccache_base + SIFIVE_CCACHE_DATECCFAIL_LOW);
		pr_err("DataFail @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataFail interrupt sig */
		readl(ccache_base + SIFIVE_CCACHE_DATECCFAIL_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_UE,
					   "DatECCFail");
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_CPU_PM
static int sifive_ccache_pm_notify(struct notifier_block *b, unsigned long cmd,
				   void *v)
{
	switch (cmd) {
	case CPU_CLUSTER_PM_ENTER:
		sifive_ccache_suspend();
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		sifive_ccache_resume();
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block sifive_ccache_pm_notifier_block = {
	.notifier_call = sifive_ccache_pm_notify,
};
#endif

static int __init sifive_ccache_init(void)
{
	struct device_node *np;
	struct resource res;
	int i, rc, intr_num;
	const struct of_device_id *match;
	unsigned long quirks;

	np = of_find_matching_node_and_match(NULL, sifive_ccache_ids, &match);
	if (!np)
		return -ENODEV;

	quirks = (uintptr_t)match->data;

	if (of_address_to_resource(np, 0, &res)) {
		rc = -ENODEV;
		goto err_node_put;
	}

	ccache_base = ioremap(res.start, resource_size(&res));
	if (!ccache_base) {
		rc = -ENOMEM;
		goto err_node_put;
	}

	if (of_property_read_u32(np, "cache-level", &level)) {
		rc = -ENOENT;
		goto err_unmap;
	}

	intr_num = of_property_count_u32_elems(np, "interrupts");
	if (!intr_num) {
		pr_err("No interrupts property\n");
		rc = -ENODEV;
		goto err_unmap;
	}

	for (i = 0; i < intr_num; i++) {
		g_irq[i] = irq_of_parse_and_map(np, i);

		if (i == DATA_UNCORR && (quirks & QUIRK_BROKEN_DATA_UNCORR))
			continue;

		rc = request_irq(g_irq[i], ccache_int_handler, 0, "ccache_ecc",
				 NULL);
		if (rc) {
			pr_err("Could not request IRQ %d\n", g_irq[i]);
			goto err_free_irq;
		}
	}
	of_node_put(np);

#ifdef CONFIG_RISCV_NONSTANDARD_CACHE_OPS
	if (quirks & QUIRK_NONSTANDARD_CACHE_OPS) {
		riscv_cbom_block_size = SIFIVE_CCACHE_LINE_SIZE;
		riscv_noncoherent_supported();
		riscv_noncoherent_register_cache_ops(&ccache_mgmt_ops);
	}
#endif

	ccache_config_read();

	ccache_cache_ops.get_priv_group = ccache_get_priv_group;
	riscv_set_cacheinfo_ops(&ccache_cache_ops);

	rc = cpuhp_setup_state_multi(CPUHP_AP_PERF_RISCV_SIFIVE_CCACHE_ONLINE,
				     "perf/sifive/ccache:online",
				     sifive_ccache_pmu_online_cpu,
				     sifive_ccache_pmu_offline_cpu);
	if (rc)
		pr_err("Failed to register CPU hotplug notifier %d\n", rc);

	rc = platform_driver_register(&sifive_ccache_driver);
	if (rc)
		pr_err("Failed to register sifive_ccache_pmu_driver: %d\n", rc);

#ifdef CONFIG_CPU_PM
	cpu_pm_register_notifier(&sifive_ccache_pm_notifier_block);
#endif

#ifdef CONFIG_DEBUG_FS
	setup_sifive_debug();
#endif
	return 0;

err_free_irq:
	while (--i >= 0)
		free_irq(g_irq[i], NULL);
err_unmap:
	iounmap(ccache_base);
err_node_put:
	of_node_put(np);
	return rc;
}

arch_initcall(sifive_ccache_init);
