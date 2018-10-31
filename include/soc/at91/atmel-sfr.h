/*
 * Atmel SFR (Special Function Registers) register offsets and bit definitions.
 *
 * Copyright (C) 2016 Atmel
 *
 * Author: Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_MFD_SYSCON_ATMEL_SFR_H
#define _LINUX_MFD_SYSCON_ATMEL_SFR_H

#define AT91_SFR_DDRCFG		0x04	/* DDR Configuration Register */
#define AT91_SFR_EBICSA		0x04 	/* EBI Chip Select Register */
/* 0x08 ~ 0x0c: Reserved */
#define AT91_SFR_OHCIICR	0x10	/* OHCI INT Configuration Register */
#define AT91_SFR_OHCIISR	0x14	/* OHCI INT Status Register */
#define AT91_SFR_BU_OTPC_CONF_R0	0x18	/* SFR OTPC Configuration 0 Register */
#define AT91_SFR_BU_OTPC_CONF_R1	0x1c	/* SFR OTPC Configuration 1 Register */
#define AT91_SFR_BU_RC_XTAL_TRIM	0x18	/* SFR RC and XTAL Oscillator Trimming Register*/
#define AT91_SFR_UTMICKTRIM	0x30	/* UTMI Clock Trimming Register */
#define AT91_SFR_UTMIHSTRIM			0x34	/* UTMI High-Speed Trimming Register */
#define AT91_SFR_UTMIFSTRIM			0x38	/* UTMI Full-Speed Trimming Register */
#define AT91_SFR_UTMISWAP			0x3c	/* UTMI DP/DM Pin Swapping Register */
#define AT91_SFR_LS					0x7c	/* Light Sleep Register */
#define AT91_SFR_I2SCLKSEL	0x90	/* I2SC Register */
#define AT91_SFR_CAL0				0xb0	/* I/O Calibration 0 Register */
#define AT91_SFR_CAL1				0xb4	/* I/O Calibration 1 Register */
#define AT91_SFR_WPMR				0xe4	/* Write Protection Mode Register */

/* Field definitions */
#define AT91_SFR_CSA(cs, val)   (val << (cs))
#define AT91_SFR_DBPUC          BIT(8)
#define AT91_SFR_DBPDC          BIT(9)
#define AT91_SFR_EBI_DRIVE		BIT(16)
#define AT91_SFR_DQIEN_F		BIT(20)
#define AT91_SFR_NFD0_SELECT	BIT(24)
#define AT91_SFR_DDR_MP_EN		BIT(25)
#define AT91_SFR_EBI_NUM_CS		8


#define AT91_OHCIICR_SUSPEND_A	BIT(8)
#define AT91_OHCIICR_SUSPEND_B	BIT(9)
#define AT91_OHCIICR_SUSPEND_C	BIT(10)

#define AT91_OHCIICR_USB_SUSPEND	(AT91_OHCIICR_SUSPEND_A | \
					 AT91_OHCIICR_SUSPEND_B | \
					 AT91_OHCIICR_SUSPEND_C)

#define AT91_UTMICKTRIM_FREQ	GENMASK(1, 0)

#endif /* _LINUX_MFD_SYSCON_ATMEL_SFR_H */
