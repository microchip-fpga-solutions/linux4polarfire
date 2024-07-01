// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive private L2 cache controller Driver
 *
 * Copyright (C) 2018-2022 SiFive, Inc.
 */

#define pr_fmt(fmt) "pl2cache: " fmt

#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/cpu_pm.h>

#define SIFIVE_PL2_PMU_MAX_COUNTERS	64

#define SIFIVE_PL2_CONFIG1_OFFSET	0x1000
#define SIFIVE_PL2_CONFIG0_OFFSET	0x1008
#define SIFIVE_PL2_SELECT_BASE_OFFSET	0x2000
#define SIFIVE_PL2_PMCLIENT_OFFSET	0x2800
#define SIFIVE_PL2_COUNTER_BASE_OFFSET	0x3000

struct sifive_pl2_pmu_event {
	struct perf_event **events;
	void __iomem *event_counter_base;
	void __iomem *event_select_base;
	u32 counters;
	DECLARE_BITMAP(used_mask, SIFIVE_PL2_PMU_MAX_COUNTERS);
};

struct sifive_pl2_pmu {
	struct pmu *pmu;
	struct hlist_node node;
};

struct sifive_pl2_state {
	void __iomem *pl2_base;
	u32 config1;
	u64 config0;
	u64 pmclientfilter;
};

static bool pl2pmu_init_done;
static struct sifive_pl2_pmu sifive_pl2_pmu;
static DEFINE_PER_CPU(struct sifive_pl2_pmu_event, sifive_pl2_pmu_event);
static DEFINE_PER_CPU(struct sifive_pl2_state, sifive_pl2_state);

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
 *     perf stat -a -e sifive_pl2_pmu/inner_put_partial_data_hit/ ls
 *     perf stat -a -e sifive_pl2_pmu/event=0x101/ ls
 */

/* formats */

static ssize_t sifive_pl2_pmu_format_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

#define SIFIVE_PL2_PMU_PMU_FORMAT_ATTR(_name, _config)				      \
	(&((struct dev_ext_attribute[]) {				      \
		{ .attr = __ATTR(_name, 0444, sifive_pl2_pmu_format_show, NULL), \
		  .var = (void *)_config, }				      \
	})[0].attr.attr)

static struct attribute *sifive_pl2_pmu_formats[] = {
	SIFIVE_PL2_PMU_PMU_FORMAT_ATTR(event, "config:0-63"),
	NULL,
};

static struct attribute_group sifive_pl2_pmu_format_group = {
	.name = "format",
	.attrs = sifive_pl2_pmu_formats,
};

/* events */

static ssize_t sifive_pl2_pmu_event_show(struct device *dev,
					 struct device_attribute *attr,
					 char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define SET_EVENT_SELECT(_event, _set)	(((u64)1 << ((_event) + 8)) | (_set))
#define PL2_PMU_EVENT_ATTR(_name, _event, _set) \
		PMU_EVENT_ATTR_ID(_name, sifive_pl2_pmu_event_show, \
							 SET_EVENT_SELECT(_event, _set))

enum pl2_pmu_event_set1 {
	INNER_PUT_FULL_DATA = 0,
	INNER_PUT_PARTIAL_DATA,
	INNER_ARITHMETIC_DATA,
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
	INNER_RELEASE_DATA_TTOT,
	INNER_PROBE_BLOCK_TOT,
	INNER_PROBE_BLOCK_TOB,
	INNER_PROBE_BLOCK_TON,
	INNER_PROBE_PERM_TON,
	INNER_PROBE_ACK_TTOB,
	INNER_PROBE_ACK_TTON,
	INNER_PROBE_ACK_BTON,
	INNER_PROBE_ACK_TTOT,
	INNER_PROBE_ACK_BTOB,
	INNER_PROBE_ACK_NTON,
	INNER_PROBE_ACK_DATA_TTOB,
	INNER_PROBE_ACK_DATA_TTON,
	INNER_PROBE_ACK_DATA_TTOT,
	PL2_PMU_MAX_EVENT1_IDX
};

enum pl2_pmu_event_set2 {
	INNER_PUT_FULL_DATA_HIT = 0,
	INNER_PUT_PARTIAL_DATA_HIT,
	INNER_ARITHMETIC_DATA_HIT,
	INNER_GET_HIT,
	INNER_PREFETCH_READ_HIT,
	INNER_ACQUIRE_BLOCK_NTOB_HIT,
	INNER_ACQUIRE_PERM_NTOT_HIT,
	INNER_RELEASE_TTOB_HIT,
	INNER_RELEASE_DATA_TTOB_HIT,
	OUTER_PROBE_BLOCK_TOT_HIT,
	INNER_PUT_FULL_DATA_HIT_SHARED,
	INNER_PUT_PARTIAL_DATA_HIT_SHARED,
	INNER_ARITHMETIC_DATA_HIT_SHARED,
	INNER_GET_HIT_SHARED,
	INNER_PREFETCH_READ_HIT_SHARED,
	INNER_ACQUIRE_BLOCK_HIT_SHARED,
	INNER_ACQUIRE_PERM_NTOT_HIT_SHARED,
	OUTER_PROBE_BLOCK_TOT_HIT_SHARED,
	OUTER_PROBE_BLOCK_TOT_HIT_DIRTY,
	PL2_PMU_MAX_EVENT2_IDX
};

enum pl2_pmu_event_set3 {
	OUTER_PUT_FULL_DATA = 0,
	OUTER_PUT_PARTIAL_DATA,
	OUTER_ARITHMETIC_DATA,
	OUTER_GET,
	OUTER_PREFETCH_READ,
	OUTER_PREFETCH_WRITE,
	OUTER_ACQUIRE_BLOCK_NTOB,
	OUTER_ACQUIRE_BLOCK_NTOT,
	OUTER_ACQUIRE_BLOCK_BTOT,
	OUTER_ACQUIRE_PERM_NTOT,
	OUTER_ACQUIRE_PERM_BTOT,
	OUTER_RELEARE_TTOB,
	OUTER_RELEARE_TTON,
	OUTER_RELEARE_BTON,
	OUTER_RELEARE_DATA_TTOB,
	OUTER_RELEARE_DATA_TTON,
	OUTER_RELEARE_DATA_BTON,
	OUTER_RELEARE_DATA_TTOT,
	OUTER_PROBE_BLOCK_TOT,
	OUTER_PROBE_BLOCK_TOB,
	OUTER_PROBE_BLOCK_TON,
	OUTER_PROBE_PERM_TON,
	OUTER_PROBE_ACK_TTOB,
	OUTER_PROBE_ACK_TTON,
	OUTER_PROBE_ACK_BTON,
	OUTER_PROBE_ACK_TTOT,
	OUTER_PROBE_ACK_BTOB,
	OUTER_PROBE_ACK_NTON,
	OUTER_PROBE_ACK_DATA_TTOB,
	OUTER_PROBE_ACK_DATA_TTON,
	OUTER_PROBE_ACK_DATA_TTOT,
	PL2_PMU_MAX_EVENT3_IDX
};

enum pl2_pmu_event_set4 {
	INNER_HINT_HITS_MSHR = 0,
	INNER_READ_HITS_MSHR,
	INNER_WRITE_HITS_MSHR,
	INNER_READ_REPLAY,
	INNER_WRITE_REPLAY,
	OUTER_PROBE_REPLAY,
	PL2_PMU_MAX_EVENT4_IDX
};

static struct attribute *sifive_pl2_pmu_events[] = {
	PL2_PMU_EVENT_ATTR(inner_put_full_data, INNER_PUT_FULL_DATA, 1),
	PL2_PMU_EVENT_ATTR(inner_put_partial_data, INNER_PUT_PARTIAL_DATA, 1),
	PL2_PMU_EVENT_ATTR(inner_arithmetic_data, INNER_ARITHMETIC_DATA, 1),
	PL2_PMU_EVENT_ATTR(inner_get, INNER_GET, 1),
	PL2_PMU_EVENT_ATTR(inner_prefetch_read, INNER_PREFETCH_READ, 1),
	PL2_PMU_EVENT_ATTR(inner_prefetch_write, INNER_PREFETCH_WRITE, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_ntob, INNER_ACQUIRE_BLOCK_NTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_ntot, INNER_ACQUIRE_BLOCK_NTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_btot, INNER_ACQUIRE_BLOCK_BTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_ntot, INNER_ACQUIRE_PERM_NTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_btot, INNER_ACQUIRE_PERM_BTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_release_ttob, INNER_RELEASE_TTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_release_tton, INNER_RELEASE_TTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_bton, INNER_RELEASE_BTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_ttob, INNER_RELEASE_DATA_TTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_tton, INNER_RELEASE_DATA_TTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_bton, INNER_RELEASE_DATA_BTON, 1),
	PL2_PMU_EVENT_ATTR(inner_release_data_ttot, INNER_RELEASE_DATA_TTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_block_tot, INNER_PROBE_BLOCK_TOT, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_block_tob, INNER_PROBE_BLOCK_TOB, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_block_ton, INNER_PROBE_BLOCK_TON, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_perm_ton, INNER_PROBE_PERM_TON, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_ttob, INNER_PROBE_ACK_TTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_tton, INNER_PROBE_ACK_TTON, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_bton, INNER_PROBE_ACK_BTON, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_ttot, INNER_PROBE_ACK_TTOT, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_btob, INNER_PROBE_ACK_BTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_nton, INNER_PROBE_ACK_NTON, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_data_ttob, INNER_PROBE_ACK_DATA_TTOB, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_data_tton, INNER_PROBE_ACK_DATA_TTON, 1),
	PL2_PMU_EVENT_ATTR(inner_probe_ack_data_ttot, INNER_PROBE_ACK_DATA_TTOT, 1),

	PL2_PMU_EVENT_ATTR(inner_put_full_data_hit, INNER_PUT_FULL_DATA_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_put_partial_data_hit, INNER_PUT_PARTIAL_DATA_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_arithmetic_data_hit, INNER_ARITHMETIC_DATA_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_get_hit, INNER_GET_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_prefetch_read_hit, INNER_PREFETCH_READ_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_ntob_hit, INNER_ACQUIRE_BLOCK_NTOB_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_ntot_hit, INNER_ACQUIRE_PERM_NTOT_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_release_ttob_hit, INNER_RELEASE_TTOB_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_release_data_ttob_hit, INNER_RELEASE_DATA_TTOB_HIT, 2),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot_hit, OUTER_PROBE_BLOCK_TOT_HIT, 2),
	PL2_PMU_EVENT_ATTR(inner_put_full_data_hit_shared, INNER_PUT_FULL_DATA_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_put_partial_data_hit_shared, INNER_PUT_PARTIAL_DATA_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_arithmetic_data_hit_shared, INNER_ARITHMETIC_DATA_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_get_hit_shared, INNER_GET_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_prefetch_read_hit_shared, INNER_PREFETCH_READ_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_block_hit_shared, INNER_ACQUIRE_BLOCK_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(inner_acquire_perm_hit_shared, INNER_ACQUIRE_PERM_NTOT_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot_hit_shared, OUTER_PROBE_BLOCK_TOT_HIT_SHARED, 2),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot_hit_dirty, OUTER_PROBE_BLOCK_TOT_HIT_DIRTY, 2),

	PL2_PMU_EVENT_ATTR(outer_put_full_data, OUTER_PUT_FULL_DATA, 3),
	PL2_PMU_EVENT_ATTR(outer_put_partial_data, OUTER_PUT_PARTIAL_DATA, 3),
	PL2_PMU_EVENT_ATTR(outer_arithmetic_data, OUTER_ARITHMETIC_DATA, 3),
	PL2_PMU_EVENT_ATTR(outer_get, OUTER_GET, 3),
	PL2_PMU_EVENT_ATTR(outer_prefetch_read, OUTER_PREFETCH_READ, 3),
	PL2_PMU_EVENT_ATTR(outer_prefetch_write, OUTER_PREFETCH_WRITE, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_block_ntob, OUTER_ACQUIRE_BLOCK_NTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_block_ntot, OUTER_ACQUIRE_BLOCK_NTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_block_btot, OUTER_ACQUIRE_BLOCK_BTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_perm_ntot, OUTER_ACQUIRE_PERM_NTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_acquire_perm_btot, OUTER_ACQUIRE_PERM_BTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_release_ttob, OUTER_RELEARE_TTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_release_tton, OUTER_RELEARE_TTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_bton, OUTER_RELEARE_BTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_ttob, OUTER_RELEARE_DATA_TTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_tton, OUTER_RELEARE_DATA_TTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_bton, OUTER_RELEARE_DATA_BTON, 3),
	PL2_PMU_EVENT_ATTR(outer_release_data_ttot, OUTER_RELEARE_DATA_TTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tot, OUTER_PROBE_BLOCK_TOT, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_block_tob, OUTER_PROBE_BLOCK_TOB, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_block_ton, OUTER_PROBE_BLOCK_TON, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_perm_ton, OUTER_PROBE_PERM_TON, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_ttob, OUTER_PROBE_ACK_TTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_tton, OUTER_PROBE_ACK_TTON, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_bton, OUTER_PROBE_ACK_BTON, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_ttot, OUTER_PROBE_ACK_TTOT, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_btob, OUTER_PROBE_ACK_BTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_nton, OUTER_PROBE_ACK_NTON, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_data_ttob, OUTER_PROBE_ACK_DATA_TTOB, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_data_tton, OUTER_PROBE_ACK_DATA_TTON, 3),
	PL2_PMU_EVENT_ATTR(outer_probe_ack_data_ttot, OUTER_PROBE_ACK_DATA_TTOT, 3),

	PL2_PMU_EVENT_ATTR(inner_hint_hits_mshr, INNER_HINT_HITS_MSHR, 4),
	PL2_PMU_EVENT_ATTR(inner_read_hits_mshr, INNER_READ_HITS_MSHR, 4),
	PL2_PMU_EVENT_ATTR(inner_write_hits_mshr, INNER_WRITE_HITS_MSHR, 4),
	PL2_PMU_EVENT_ATTR(inner_read_replay, INNER_READ_REPLAY, 4),
	PL2_PMU_EVENT_ATTR(inner_write_replay, INNER_WRITE_REPLAY, 4),
	PL2_PMU_EVENT_ATTR(outer_probe_replay, OUTER_PROBE_REPLAY, 4),
	NULL
};

static struct attribute_group sifive_pl2_pmu_events_group = {
	.name = "events",
	.attrs = sifive_pl2_pmu_events,
};

/*
 * Per PMU device attribute groups
 */

static const struct attribute_group *sifive_pl2_pmu_attr_grps[] = {
	&sifive_pl2_pmu_format_group,
	&sifive_pl2_pmu_events_group,
	NULL,
};

/*
 * Low-level functions: reading and writing counters
 */

static inline u64 read_counter(int idx)
{
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);

	if (WARN_ON_ONCE(idx < 0 || idx > ptr->counters))
		return -EINVAL;

	return readq(ptr->event_counter_base + idx * 8);
}

static inline void write_counter(int idx, u64 val)
{
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);

	writeq(val, ptr->event_counter_base + idx * 8);
}

/*
 * pmu->read: read and update the counter
 */
static void sifive_pl2_pmu_read(struct perf_event *event)
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
static void sifive_pl2_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		sifive_pl2_pmu.pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}

	/* Disable this counter to count events */
	writeq(0, ptr->event_select_base + (hwc->idx * 8));
}

/*
 * pmu->start: start the event.
 */
static void sifive_pl2_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);

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
static int sifive_pl2_pmu_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);
	int idx;
	u64 config = event->attr.config;
	u64 set = config & 0xff;
	u64 ev_type = config >> 8;

	/* Check if this is a valid set and event. */
	switch (set) {
	case 1:
		if (ev_type >= (1ULL << PL2_PMU_MAX_EVENT1_IDX))
			return -ENOENT;
		break;
	case 2:
		if (ev_type >= (1ULL << PL2_PMU_MAX_EVENT2_IDX))
			return -ENOENT;
		break;
	case 3:
		if (ev_type >= (1ULL << PL2_PMU_MAX_EVENT3_IDX))
			return -ENOENT;
		break;
	case 4:
		if (ev_type >= (1ULL << PL2_PMU_MAX_EVENT4_IDX))
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
		sifive_pl2_pmu.pmu->start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

/*
 * pmu->del: delete the event from PMU.
 */
static void sifive_pl2_pmu_del(struct perf_event *event, int flags)
{
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);
	struct hw_perf_event *hwc = &event->hw;

	/* Stop the counter and release this counter. */
	ptr->events[hwc->idx] = NULL;
	sifive_pl2_pmu.pmu->stop(event, PERF_EF_UPDATE);
	clear_bit(hwc->idx, ptr->used_mask);
	perf_event_update_userpage(event);
}

/*
 * Event Initialization/Finalization
 */

static int sifive_pl2_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* Don't allocate hw counter yet. */
	hwc->idx = -1;
	hwc->config = event->attr.config;

	return 0;
}

/*
 * Initialization
 */

static struct pmu sifive_pl2_generic_pmu = {
	.name		= "sifive_pl2_pmu",
	.task_ctx_nr	= perf_sw_context,
	.event_init	= sifive_pl2_pmu_event_init,
	.add		= sifive_pl2_pmu_add,
	.del		= sifive_pl2_pmu_del,
	.start		= sifive_pl2_pmu_start,
	.stop		= sifive_pl2_pmu_stop,
	.read		= sifive_pl2_pmu_read,
	.attr_groups	= sifive_pl2_pmu_attr_grps,
	.capabilities   = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
};

static struct sifive_pl2_pmu sifive_pl2_pmu = {
	.pmu = &sifive_pl2_generic_pmu,
};

static void sifive_pl2_state_save(struct sifive_pl2_state *pl2_state)
{
	void __iomem *pl2_base = pl2_state->pl2_base;

	if (!pl2_base)
		return;

	pl2_state->config1 = readl(pl2_base + SIFIVE_PL2_CONFIG1_OFFSET);
	pl2_state->config0 = readq(pl2_base + SIFIVE_PL2_CONFIG0_OFFSET);
	pl2_state->pmclientfilter = readq(pl2_base + SIFIVE_PL2_PMCLIENT_OFFSET);
}

static void sifive_pl2_state_restore(struct sifive_pl2_state *pl2_state)
{
	void __iomem *pl2_base = pl2_state->pl2_base;

	if (!pl2_base)
		return;

	writel(pl2_state->config1, pl2_base + SIFIVE_PL2_CONFIG1_OFFSET);
	writeq(pl2_state->config0, pl2_base + SIFIVE_PL2_CONFIG0_OFFSET);
	writeq(pl2_state->pmclientfilter, pl2_base + SIFIVE_PL2_PMCLIENT_OFFSET);
}

/*
 * CPU Hotplug call back function
 */
static int sifive_pl2_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct sifive_pl2_state *pl2_state = this_cpu_ptr(&sifive_pl2_state);

	sifive_pl2_state_restore(pl2_state);

	return 0;
}

static int sifive_pl2_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct sifive_pl2_state *pl2_state = this_cpu_ptr(&sifive_pl2_state);
	/* Save the pl2 state */
	sifive_pl2_state_save(pl2_state);

	return 0;
}

/*
 *  PM notifer for suspend to ram
 */
#ifdef CONFIG_CPU_PM
static int sifive_pl2_pmu_pm_notify(struct notifier_block *b, unsigned long cmd,
				    void *v)
{
	struct sifive_pl2_pmu_event *ptr = this_cpu_ptr(&sifive_pl2_pmu_event);
	struct perf_event *event;
	int idx;
	int enabled_event = bitmap_weight(ptr->used_mask, ptr->counters);

	if (!enabled_event)
		return NOTIFY_OK;

	for (idx = 0; idx < ptr->counters; idx++) {
		event = ptr->events[idx];
		if (!event)
			continue;

		switch (cmd) {
		case CPU_PM_ENTER:
			/* Stop and update the counter */
			sifive_pl2_pmu_stop(event, PERF_EF_UPDATE);
			break;
		case CPU_PM_ENTER_FAILED:
		case CPU_PM_EXIT:
			 /*
			  * Restore and enable the counter.
			  *
			  * Requires RCU read locking to be functional,
			  * wrap the call within RCU_NONIDLE to make the
			  * RCU subsystem aware this cpu is not idle from
			  * an RCU perspective for the sifive_pl2_pmu_start() call
			  * duration.
			  */
			sifive_pl2_pmu_start(event, PERF_EF_RELOAD);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static int sifive_pl2_pm_notify(struct notifier_block *b, unsigned long cmd,
				void *v)
{
	struct sifive_pl2_state *pl2_state = this_cpu_ptr(&sifive_pl2_state);

	switch (cmd) {
	case CPU_PM_ENTER:
		/* Save the pl2 state */
		sifive_pl2_state_save(pl2_state);
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		sifive_pl2_state_restore(pl2_state);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block sifive_pl2_pmu_pm_notifier_block = {
	.notifier_call = sifive_pl2_pmu_pm_notify,
};

static struct notifier_block sifive_pl2_pm_notifier_block = {
	.notifier_call = sifive_pl2_pm_notify,
};

void sifive_pl2_pmu_pm_init(void)
{
	cpu_pm_register_notifier(&sifive_pl2_pmu_pm_notifier_block);
	cpu_pm_register_notifier(&sifive_pl2_pm_notifier_block);
}

#else
static inline void sifive_pl2_pmu_pm_init(void) { }
#endif /* CONFIG_CPU_PM */

static const struct of_device_id sifive_pl2_pmu_of_ids[] = {
	{ .compatible = "sifive,pl2cache0" },
	{ .compatible = "sifive,pl2cache1" },
	{ /* sentinel value */ }
};

static void pl2_config_read(void __iomem *pl2_base, int cpu)
{
	u32 regval, bank, way, set, cacheline;

	regval = readl(pl2_base);
	bank = regval & 0xff;
	pr_info("in the CPU: %d\n", cpu);
	pr_info("No. of Banks in the cache: %d\n", bank);
	way = (regval & 0xff00) >> 8;
	pr_info("No. of ways per bank: %d\n", way);
	set = (regval & 0xff0000) >> 16;
	pr_info("Total sets: %llu\n", (uint64_t)1 << set);
	cacheline = (regval & 0xff000000) >> 24;
	pr_info("Bytes per cache block: %llu\n", (uint64_t)1 << cacheline);
	pr_info("Size: %d\n", way << (set + cacheline));
}

static int sifive_pl2_pmu_dev_probe(struct platform_device *pdev)
{
	struct resource *res;
	int cpu, ret = -EINVAL;
	struct device_node *cpu_node, *pl2_node;
	struct sifive_pl2_pmu_event *ptr = NULL;
	struct sifive_pl2_state *pl2_state = NULL;
	void __iomem *pl2_base;

	/* Traverse all cpu nodes to find the one mapping to its pl2 node. */
	for_each_cpu(cpu, cpu_possible_mask) {
		cpu_node = of_cpu_device_node_get(cpu);
		pl2_node = of_parse_phandle(cpu_node, "next-level-cache", 0);

		/* Found it! */
		if (dev_of_node(&pdev->dev) == pl2_node) {
			/* Use cpu to get its percpu data sifive_pl2_pmu_event. */
			ptr = per_cpu_ptr(&sifive_pl2_pmu_event, cpu);
			pl2_state = per_cpu_ptr(&sifive_pl2_state, cpu);
			break;
		}
	}

	if (!ptr) {
		pr_err("Not found the corresponding cpu_node in dts.\n");
		goto early_err;
	}

	/* Get counter numbers. */
	ret = of_property_read_u32(pl2_node, "sifive,perfmon-counters", &ptr->counters);
	if (ret) {
		pr_err("Not found sifive,perfmon-counters property\n");
		goto early_err;
	}
	pr_info("perfmon-counters: %d for CPU %d\n", ptr->counters, cpu);

	/* Allocate perf_event. */
	ptr->events = kcalloc(ptr->counters, sizeof(struct perf_event), GFP_KERNEL);

	/* Set base address of select and counter registers. */
	pl2_base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(pl2_base)) {
		ret = PTR_ERR(pl2_base);
		goto probe_err;
	}

	/* Print pL2 configs. */
	pl2_config_read(pl2_base, cpu);
	ptr->event_select_base = pl2_base + SIFIVE_PL2_SELECT_BASE_OFFSET;
	ptr->event_counter_base = pl2_base + SIFIVE_PL2_COUNTER_BASE_OFFSET;
	pl2_state->pl2_base = pl2_base;

	if (!pl2pmu_init_done) {
		ret = perf_pmu_register(sifive_pl2_pmu.pmu, sifive_pl2_pmu.pmu->name, -1);
		if (ret) {
			cpuhp_state_remove_instance(CPUHP_AP_PERF_RISCV_SIFIVE_PL2_ONLINE,
						    &sifive_pl2_pmu.node);
			pr_err("Failed to register sifive_pl2_pmu.pmu: %d\n", ret);
			goto probe_err;
		}
		sifive_pl2_pmu_pm_init();
		pl2pmu_init_done = true;
	}

	return 0;

probe_err:
	/* Free memory. */
	kfree(ptr->events);
early_err:
	return ret;
}

static struct platform_driver sifive_pl2_pmu_driver = {
	.driver = {
		   .name = "SiFive-pL2-PMU",
		   .of_match_table = sifive_pl2_pmu_of_ids,
		   },
	.probe = sifive_pl2_pmu_dev_probe,
};

static int __init sifive_pl2_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_RISCV_SIFIVE_PL2_ONLINE,
				      "perf/sifive/pl2:online",
				      sifive_pl2_pmu_online_cpu,
				      sifive_pl2_pmu_offline_cpu);
	if (ret)
		pr_err("Failed to register CPU hotplug notifier %d\n", ret);

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_RISCV_SIFIVE_PL2_ONLINE,
				       &sifive_pl2_pmu.node);
	if (ret)
		pr_err("Failed to add hotplug instance: %d\n", ret);

	ret = platform_driver_register(&sifive_pl2_pmu_driver);
	if (ret)
		pr_err("Failed to register sifive_pl2_pmu_driver: %d\n", ret);

	return ret;
}

device_initcall(sifive_pl2_pmu_init);
