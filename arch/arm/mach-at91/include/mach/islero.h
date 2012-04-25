#ifndef ISLERO_H
#define ISLERO_H

/*
 * Peripheral identifiers/interrupts (Non Secure).
 */
#define ISLERO_ID_ST1		1
#define ISLERO_ID_ST2		2
#define AT91_ID_ST	ISLERO_ID_ST2
#define ISLERO_ID_DMA0_NS	3
#define ISLERO_ID_HSMCI0	4
#define ISLERO_ID_USART0	5
#define ISLERO_ID_PMU		6
#define ISLERO_ID_PIOA_NS	7

/*
 * User Peripheral physical base addresses.
 */
#define ISLERO_BASE_DMA0_NS	0xff011000
#define ISLERO_BASE_SHA		0xff01a000
#define ISLERO_BASE_AIC_S	0xff017000
#define ISLERO_BASE_AIC_NS	0xff012000
#define ISLERO_BASE_HMATRIX2	0xff015000
#define ISLERO_BASE_ST1		0xff000000
#define ISLERO_BASE_ST2		0xff004000
#define ISLERO_BASE_USART0	0xff010000
#define ISLERO_BASE_HSMCI0	0xff013000
#define ISLERO_BASE_SPI0	0xff00c000
#define ISLERO_BASE_TC0		0xff018000
#define ISLERO_BASE_TC1		0xff018040
#define ISLERO_BASE_TC2		0xff018080
#define ISLERO_BASE_TCB0	0xff018000
#define ISLERO_BASE_PIOA_NS	0xff01b000

#define AT91_PIOA_NS		(ISLERO_BASE_PIOA_NS - AT91_BASE_SYS)
#define AT91_ST			(ISLERO_BASE_ST2 - AT91_BASE_SYS)
#define AT91_DBGU		(ISLERO_BASE_USART0 - AT91_BASE_SYS)

/*
 * Internal Memory.
 */
#define ISLERO_SRAM0_BASE	0x00010000
#define ISLERO_SRAM0_SIZE	(SZ_64K)
#define ISLERO_SRAM1_BASE	0x00050000
#define ISLERO_SRAM1_SIZE	(SZ_64K)
#define ISLERO_SRAM2_BASE	0x0b000000
#define ISLERO_SRAM2_SIZE	(SZ_32K)

/*
 * DMA0 peripheral identifiers
 * for hardware handshaking interface
 */
#define AT_DMA_ID_HSMCI0	1
#define AT_DMA_ID_SPI0_TX	2
#define AT_DMA_ID_SPI0_RX	3

#endif
