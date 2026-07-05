/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 1588 PTP support for Microchip Core1588 IP.
 *
 * Copyright (C) 2025 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Praveen Kumar Vattipalli <praveen.kumar@microchip.com>
 *
 */

#define CORE1588_GCFG			0x00
#define CORE1588_GCFG_EN		BIT(0)
#define CORE1588_GCFG_ONE_STEP		BIT(1)

#define CORE1588_IER			0x04

#define CORE1588_RIS			0x0C
#define CORE1588_RIS_RITTS		BIT(0)
#define CORE1588_RIS_RIRTS		BIT(1)
#define CORE1588_RIS_RIPTPTXSYNC	BIT(9)
#define CORE1588_RIS_RIPTPTXDELAYREQ	BIT(10)
#define CORE1588_RIS_RIPTPTXDELAYRESP	BIT(11)
#define CORE1588_RIS_RIPTPTXPDELAYREQ	BIT(12)
#define CORE1588_RIS_RIPTPTXPDELAYRESP	BIT(13)
#define CORE1588_RIS_RIPTPRXSYNC	BIT(14)
#define CORE1588_RIS_RIPTPRXDELAYREQ	BIT(15)
#define CORE1588_RIS_RIPTPRXDELAYRESP	BIT(16)
#define CORE1588_RIS_RIPTPRXPDELAYREQ	BIT(17)
#define CORE1588_RIS_RIPTPRXPDELAYRESP	BIT(18)
#define CORE1588_RIS_RITTSID		BIT(19)
#define CORE1588_RIS_RIRTSID		BIT(20)
#define CORE1588_RIS_RIPEERTTSID	BIT(21)
#define CORE1588_RIS_RIPEERRTSID	BIT(22)

#define RTC_NSEC_29BITS			0x30
#define RTC_SEC_LSB_32BITS		0x34
#define RTC_SEC_MSB_16BITS		0x78
#define MCHP_CORE1588_RTCCTRL		0x94
#define MCHP_CORE1588_RTCICR		0x98

#define MCHP_CORE1588_PEERRTSM		0xD0
#define MCHP_CORE1588_PEERRTSL		0xCC
#define MCHP_CORE1588_RTSL		0x1C
#define MCHP_CORE1588_RTSM		0x20
#define MCHP_CORE1588_RTSID2		0x24

#define MCHP_CORE1588_PEERTTSM		0xC0
#define MCHP_CORE1588_PEERTTSL		0xBC
#define MCHP_CORE1588_TTSL		0x10
#define MCHP_CORE1588_TTSM		0x14
#define MCHP_CORE1588_TTSID		0x18

#define MCHP_CORE1588_PEERTTSID	0xC8
#define MCHP_CORE1588_PEERRTSID2	0xD8

#define MCHP_CORE1588_TTSMSBSECSUBNS	0x70
#define MCHP_CORE1588_RTSMSBSECSUBNS	0x74
#define MCHP_CORE1588_PEERTTSMSBSECSUBNS 0xC4
#define MCHP_CORE1588_PEERRTSMSBSECSUBNS 0xD4

#define CORE1588_TSL_SIZE		32
#define CORE1588_TSH_SIZE		16

/* Bitfields in CORE1588 */
#define CORE1588_NSINCR_OFFSET		0
#define CORE1588_NSINCR_SIZE		30

/* Bitfields in TISUBN */
#define CORE1588_SUBNSINCR_OFFSET	0
#define CORE1588_SUBNSINCRL_OFFSET	24
#define CORE1588_SUBNSINCRL_SIZE	8
#define CORE1588_SUBNSINCRH_OFFSET	0
#define CORE1588_SUBNSINCRH_SIZE	16
#define CORE1588_SUBNSINCR_SIZE		24

/* Bitfields in TN */
#define CORE1588_TN_OFFSET		0 /* TSU timer value (ns) */
#define CORE1588_TN_SIZE		30

/* Bitfields in ADJ */
#define CORE1588_ADDSUB_OFFSET		31
#define CORE1588_ADDSUB_SIZE		1

/* Scaled PPM fraction */
#define PPM_FRACTION			16

#define CORE1588_TSEC_SIZE  (CORE1588_TSH_SIZE + CORE1588_TSL_SIZE)
#define CORE1588_SEC_MAX_VAL (((u64)1 << CORE1588_TSEC_SIZE) - 1)
#define CORE1588_NSEC_MAX_VAL ((1 << CORE1588_TN_SIZE) - 1)

#define mchp_core1588_MAX_ADJ		64000000
#define N_EXT_TS			0
#define N_EXT_PPS			0

/* Maximum time to wait for Core1588 to capture a TX timestamp (in usec).
 * A 1518-byte frame at 1Gbps takes ~12us to transmit. Add margin for
 * MAC FIFO latency.
 */
#define CORE1588_TX_TS_POLL_TIMEOUT_US	100
#define CORE1588_TX_TS_POLL_DELAY_US	5

/**
 * core1588_tsu_incr - structure defining sub nanoseconds and nanoseconds
 * @sub_ns: store sub-nanoseconds value
 * @ns: store nanoseconds value
 */
struct core1588_tsu_incr {
	u32 sub_ns;
	u32 ns;
};

/**
 * struct core1588_rx_ts_cache - Cached RX timestamps snapshot from IRQ context
 * @ev_sec: Event frame (Sync/Delay_Req) seconds
 * @ev_nsec: Event frame nanoseconds
 * @peer_sec: Peer event frame (Pdelay) seconds
 * @peer_nsec: Peer event frame nanoseconds
 */
struct core1588_rx_ts_cache {
	u32 ev_sec;
	u32 ev_nsec;
	u32 peer_sec;
	u32 peer_nsec;
};

/**
 * struct mchp_core1588_timer - board specific private data structure
 *
 * @dev: device
 * @base: pointer to control register
 * @ptp_clock: pointer to registered PTP clock device
 * ptp_clock_info: structure defining PTP hardware capabilities
 * @tsu_incr: structure defining sub nanoseconds and nanoseconds
 * @reg_lock: spinlock controlling access to buf_list and sequence
 * @phc_index: index holding ptp hardware clock
 * @tstamp_config: hardware timestamping configuration
 * @rx_ts: cached RX timestamps read+cleared atomically in IRQ context
 * @ptp_rxstamp: utility function which reads RX time stamp
 * @ptp_txstamp: utility function which checks for TX time stamp
 * @get_hwtst: get current hardware timestamping configuration
 * @set_hwtst: user entry point for timestamp mode
 */
struct mchp_core1588_timer {
	struct device		*dev;
	void __iomem		*base;
	struct ptp_clock	*ptp_clock;
	struct ptp_clock_info	ptp_clock_info;
	struct core1588_tsu_incr tsu_incr;
	spinlock_t		reg_lock; /* For reg access */
	int			phc_index;
	struct hwtstamp_config tstamp_config;
	struct core1588_rx_ts_cache rx_ts;
	int (*ptp_rxstamp)(struct mchp_core1588_timer *timer, struct sk_buff *skb);
	int (*ptp_txstamp)(struct mchp_core1588_timer *timer, struct sk_buff *skb);
	int (*get_hwtst)(struct mchp_core1588_timer *timer, struct ifreq *rq);
	int (*set_hwtst)(struct mchp_core1588_timer *timer, struct ifreq *ifr, int cmd);
};
