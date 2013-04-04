/*
 * Chip-specific header file for the SAMA5D4 family
 *
 *  Copyright (C) 2013 Atmel Corporation,
 *                     Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * Common definitions.
 * Based on SAMA5D4 datasheet.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef SAMA5D4_H
#define SAMA5D4_H

/*
 * Peripheral identifiers/interrupts.
 */
//#define AT91_ID_FIQ		 0	/* Advanced Interrupt Controller (FIQ) */
//#define AT91_ID_SYS		 1	/* System Peripherals */
#define SAMA5D4_ID_PIOD		 5	/* PIOD */
#define SAMA5D4_ID_USART0	 6	/* USART0 */
#define SAMA5D4_ID_PIOA		23	/* PIOA */
#define SAMA5D4_ID_PIOB		24	/* PIOB */
#define SAMA5D4_ID_PIOC		25	/* PIOC */
#define SAMA5D4_ID_PIOE		26	/* PIOE */
#define SAMA5D4_ID_USART3	30	/* USART3 */
#define SAMA5D4_ID_DMA1		31	/* DMA Controller 1 */
#define SAMA5D4_ID_HSMCI0	35	/* MCI */
#define SAMA5D4_ID_TC0		40	/* Timer Counter 0 */
#define SAMA5D4_ID_TC1		41	/* Timer Counter 1 */
#define SAMA5D4_ID_DBGU		50	/* debug Unit (usually no special interrupt line) */

/*
 * User Peripheral physical base addresses.
 */
#define SAMA5D4_BASE_AIC	0xfc06e000 /* (AIC non-secure) Base Address */
#define SAMA5D4_BASE_USART3	0xfc00c000 /* (USART3 non-secure) Base Address */
//#define SAMA5D4_BASE_TC0	0xf0010000 /* (TC0) Base Address */
//#define SAMA5D4_BASE_TC1	0xf0010040 /* (TC1) Base Address */
//#define SAMA5D4_BASE_GMAC	0xf0028000 /* (GMAC) Base Address */
//#define SAMA5D4_BASE_LCDC	0xf0030000 /* (HLCDC5) Base Address */
//#define SAMA5D4_BASE_HSMCI0	0xf0000000 /* (MMCI) Base Address */
//#define SAMA5D4_BASE_EMAC	0xf802c000 /* (EMAC) Base Address */
//#define SAMA5D4_BASE_UDPHS	0xf8030000
//#define AT91_BASE_SYS		0xffffc000

/*
 * System Peripherals (offset from AT91_BASE_SYS)
 */
//#define AT91_DMA1	(0xffffe800 - AT91_BASE_SYS)
//#define AT91_DBGU	AT91_BASE_DBGU1
//#define AT91_PIOA	(0xfffff200 - AT91_BASE_SYS)
//#define AT91_PIOB	(0xfffff400 - AT91_BASE_SYS)
//#define AT91_PIOC	(0xfffff600 - AT91_BASE_SYS)
//#define AT91_PIOD	(0xfffff800 - AT91_BASE_SYS)
//#define AT91_PIOE	(0xfffffA00 - AT91_BASE_SYS)

/*
 * Internal Memory.
 */
#define SAMA5D4_NS_SRAM_BASE     0x00210000      /* Internal SRAM base address Non-Secure */
#define SAMA5D4_NS_SRAM_SIZE     (64 * SZ_1K)   /* Internal SRAM size Non-Secure part (64Kb) */

#endif
