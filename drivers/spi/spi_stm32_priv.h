/* spi_stm32.h - STMicroelectronics STM32 SPI driver private definitions */

/*
 * Copyright (c) 2016 Linaro Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __SPI_STM32_PRIV_H__
#define __SPI_STM32_PRIV_H__

#include <clock_control/stm32_clock_control.h>

/* Control Register 1 (CR1) settings */
union __spi_cr1 {
	uint16_t val;
	struct {
		uint16_t cpha:1 __packed;
		uint16_t cpol:1 __packed;
		uint16_t mstr:1 __packed;
		uint16_t br:3 __packed;
		uint16_t spe:1 __packed;
		uint16_t lsb_first:1 __packed;
		uint16_t ssi:1 __packed;
		uint16_t ssm:1 __packed;
		uint16_t rx_only:1 __packed;
		uint16_t dff:1 __packed;
		uint16_t crc_next:1 __packed;
		uint16_t crc_en:1 __packed;
		uint16_t bidi_oe:1 __packed;
		uint16_t bidi_mode:1 __packed;
	} bit;
};

#define SPI_STM32_CR1_BIDIRECTIONAL_MODE_ENABLE		(0x1)
#define SPI_STM32_CR1_BIDIRECTIONAL_OUTPUT		(0x1)
#define SPI_STM32_CR1_HW_CRC_ENABLE			(0x1)
/* Only write after disabling SPI (SPE=0) */
#define SPI_STM32_CR1_DATA_FRAME_FORMAT_8BIT		(0x0)
/* Only write after disabling SPI (SPE=0) */
#define SPI_STM32_CR1_DATA_FRAME_FORMAT_16BIT		(0x1)
#define SPI_STM32_CR1_SW_SLAVE_MANAGEMENT		(0x1)
#define SPI_STM32_CR1_HW_SLAVE_MANAGEMENT		(0x0)
#define SPI_STM32_CR1_LSBFIRST				(0x1)
/* master switch for the module */
#define SPI_STM32_CR1_ENABLE				(0x1)
#define SPI_STM32_CR1_DISABLE				(0x0)

/* br */
#define SPI_STM32_CR1_BAUD_RATE_MASK			(0x7)

enum {
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_2 = 0,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_4,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_8,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_16,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_32,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_64,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_128,
	SPI_STM32_CR1_BAUD_RATE_PCLK_DIV_256,
};

#define SPI_STM32_CR1_MASTER_MODE				(0x1)
#define SPI_STM32_CR1_SLAVE_MODE				(0x0)
#define SPI_STM32_CR1_CLOCK_POLARITY_IDLE_LOW			(0x0)
#define SPI_STM32_CR1_CLOCK_POLARITY_IDLE_HIGH			(0x1)
#define SPI_STM32_CR1_CLOCK_PHASE_LEADING_EDGE_CAPTURE		(0x0)
#define SPI_STM32_CR1_CLOCK_PHASE_TRAILING_EDGE_CAPTURE		(0x1)

/* Control Register 2 (CR2) settings */
union __spi_cr2 {
	uint16_t val;
	struct {
		uint16_t rxdmaen:1 __packed; /* Bit 0 */
		uint16_t txdmaen:1 __packed;
		uint16_t ssoe:1 __packed;
		uint16_t rsvd__3:1 __packed;
		uint16_t frf:1 __packed;
		uint16_t errie:1 __packed;
		uint16_t rxneie:1 __packed;
		uint16_t txeie:1 __packed;
		uint16_t rsvd__8_15:8 __packed;
	} bit;
};

/* enable TI frame format */
#define SPI_STM32_CR2_FRAME_TI	(0x1 << 4)
/* interrupt whenever the following occurs:
 * - CRCERR, OVR, MODF error in SPI mode
 * - FRE in TI mode
 * - UDR, OVER, FRE in I2S mode
 */
#define SPI_STM32_CR2_ERRIE	(0x1 << 5)
/* interrupt whenever RXNE flag set */
#define SPI_STM32_CR2_RXNEIE	(0x1 << 6)
/* interrupt whenever TXE flag set */
#define SPI_STM32_CR2_TXEIE	(0x1 << 7)

/* Status Register (SR) settings */
union __spi_sr {
	uint16_t val;
	struct {
		uint16_t rxne:1 __packed;    /* Bit 0 */
		uint16_t txe:1 __packed;
		uint16_t chside:1 __packed;
		uint16_t udr:1 __packed;
		uint16_t crc_err:1 __packed;
		uint16_t modf:1 __packed;
		uint16_t ovr:1 __packed;
		uint16_t bsy:1 __packed;
		uint16_t fre:1 __packed;
		uint16_t rsvd__9_15:7 __packed;
	} bit;
};

/* RX buffer not empty */
#define SPI_STM32_SR_RXNE		(0x1 << 0)
/* TX buffer empty */
#define SPI_STM32_SR_TXE		(0x1 << 1)
/* Right channel received or needs transmission */
#define SPI_STM32_SR_CHANNEL_RIGHT	(0x1 << 2)
/* Left channel received or needs transmission */
#define SPI_STM32_SR_CHANNEL_LEFT	(0x0 << 2)
/* Underrun occurred */
#define SPI_STM32_SR_UNDERRUN		(0x1 << 3)
/* CRC doesn't match RXCRCR register */
#define SPI_STM32_SR_CRC_ERROR		(0x1 << 4)
/* Set by HW and needs software sequence in Section 20.3.10 to reset */
#define SPI_STM32_SR_MODE_ERROR		(0x1 << 5)
/* Overrun occurred */
#define SPI_STM32_SR_OVERRUN		(0x1 << 6)
/* SPI is busy or TX buffer not empty */
#define SPI_STM32_SR_BUSY		(0x1 << 7)
/* SPI is busy or TX buffer not empty */
#define SPI_STM32_SR_FRAME_ERROR	(0x1 << 8)

/* Registers:
 * Since the registers are 16 bits but aligned on 32 bit boundaries,
 * we need to fill the 16 bit holes to allow direct mapping of the
 * register addresses
 *
 * FIXME: Just represent as 32 bit?
 */
struct spi_stm32 {
	union __spi_cr1 cr1;
	uint16_t rsvd_hole1;
	union __spi_cr2 cr2;
	uint16_t rsvd_hole2;
	union __spi_sr sr;
	uint16_t rsvd_hole3;
	uint16_t dr;		/* data register, 8 or 16-bit depend on DFF */
	uint16_t rsvd_hole4;
	uint16_t crcpr;
	uint16_t rsvd_hole5;
	uint16_t rxcrcr;
	uint16_t rsvd_hole6;
	uint16_t txcrcr;
	uint16_t rsvd_hole7;
};


typedef void (*spi_stm32_config_t)();

/* platform configuration data */
struct spi_stm32_config {
	uint32_t num;
	uint32_t base_addr;		/* base address of SPI module reg */
#ifdef CONFIG_SOC_SERIES_STM32F4X
	/* TODO: Revert to clock_subsys_t like stm32f1 and
	 * remove data complexity from driver
	 */
	struct stm32f4x_pclken pclken;	/* SPI module's clock subsystem */
#endif
	spi_stm32_config_t config_func;	/* IRQ config function pointer */
};

/* runtime private data */
struct spi_stm32_data {
	uint32_t mode;		/* 0 = master, 1 = slave */
	uint8_t frame_sz;		/* frame/word size, in bits */
	device_sync_call_t sync;	/* synchronisationn */
	struct nano_sem sem;		/* semaphore for critical section */
	const uint8_t *tx_buf;
	uint8_t *rx_buf;
	uint32_t tx_buf_len;
	uint32_t rx_buf_len;
	uint32_t trans_len;
	uint32_t transmitted;
	uint32_t received;
	uint8_t error;
	uint32_t baud_rate;
	struct device *clock;
};

/* Register offsets */
#define SPI_STM32_REG_CR1		(0x00)
#define SPI_STM32_REG_CR2		(0x04)
#define SPI_STM32_REG_SR		(0x08)
#define SPI_STM32_REG_DR		(0x0C)
#define SPI_STM32_REG_CRCPR		(0x10)
#define SPI_STM32_REG_RXCRCR		(0x14)
#define SPI_STM32_REG_TXCRCR		(0x18)

#endif /* __SPI_STM32_PRIV_H__ */
