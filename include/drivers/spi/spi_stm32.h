/* spi_stm32.h - ST Microelectronics STM32 SPI controller driver utilities */

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *           (c) 2016 Linaro Limited.
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

#ifndef __SPI_STM32_H__
#define __SPI_STM32_H__

/* Supported SPI frequencies */
#define SPI_STM32_CLK_FREQ_200KHZ	200000
#define SPI_STM32_CLK_FREQ_400KHZ	400000
#define SPI_STM32_CLK_FREQ_600KHZ	600000
#define SPI_STM32_CLK_FREQ_800KHZ	800000
#define SPI_STM32_CLK_FREQ_1MHZ	1000000
#define SPI_STM32_CLK_FREQ_2MHZ	2000000
#define SPI_STM32_CLK_FREQ_4MHZ	4000000
#define SPI_STM32_CLK_FREQ_6MHZ	6000000
#define SPI_STM32_CLK_FREQ_8MHZ	8000000
#define SPI_STM32_CLK_FREQ_10MHZ	10000000
#define SPI_STM32_CLK_FREQ_20MHZ	20000000

/*
 * Device configuration
 *
 * Device-independent configuration:
 * Bits [0 : 11] in the config parameter of the spi_configure() API are defined
 * with the following fields.
 *
 * SCK polarity     [ 0 ]       - SCK inactive state (0 = low, 1 = high)
 * SCK phase        [ 1 ]       - Data captured/changed on which SCK edge:
 *                              -   0 = leading/following edges, respectively
 *                              -   1 = following/leading edges, respectively
 * loop_mode        [ 2 ]       - Not used/Unsupported
 * transfer_mode    [ 3 ]       - First significant bit (0 = MSB, 1 = LSB)
 * word_size        [ 4 : 7 ]   - Size of a data train in bits
 * unused           [ 8 : 11 ]  - Unused word_size field bits
 *
 * Device-specific configuration:
 * Bits [12 : 31] in the config parameter of the spi_configure() API are
 * available, with the following fields defined for this device.
 *
 * Operational Mode [12:13]      - Master (00), Slave (01)
 * Slave management [14:15]      - Slave selection management
 *                               -   00 (HW, SSOE=1, master mode)
 *                               -   01 (HW, SSOE=0, multi-master or slave mode)
 *                               -   11 (SW, SSI bit in CR1 selects slave)
 * Frame format     [16]         - Frame format selection
 *                               -   0 (Motorola mode)
 *                               -   1 (TI mode - overrides CPOL, CPHA, SSM,
 *                                      SSI, SSOE)
 */

/* STM32 SPI word/frame size is limited to 16 bits, as: (size - 1) */
#define SPI_STM32_WORD_SIZE_MAX	(16)

/* Operation mode */
#define SPI_STM32_OP_MODE_MASK       (0x3 << 12)
#define SPI_STM32_OP_MODE(_in_)      ((_in_) << 12)
#define SPI_STM32_OP_MODE_GET(_in_)  ((_in_) & SPI_STM32_OP_MODE_MASK)

/* Slave management */
#define SPI_STM32_SLAVE_MGMT_MODE_MASK    (0x3 << 14)
#define SPI_STM32_SLAVE_MGMT_MODE(_in_)  ((_in_) << 14)
#define SPI_STM32_SLAVE_MGMT_MODE_GET(_in_)	\
	((_in_) & SPI_STM32_SLAVE_MGMT_MODE_MASK)

/* Frame format */
#define SPI_STM32_FRAME_MOTOROLA     (0x0 << 16)
#define SPI_STM32_FRAME_TI           (0x1 << 16)

#define SPI_STM32_MASTER_MODE        SPI_STM32_OP_MODE(0x0)
#define SPI_STM32_SLAVE_MODE         SPI_STM32_OP_MODE(0x1)
#define SPI_STM32_SLAVE_HW_SS_OUTPUT SPI_STM32_SLAVE_MGMT_MODE(0x0)
#define SPI_STM32_SLAVE_HW_NO_OUTPUT SPI_STM32_SLAVE_MGMT_MODE(0x1)
#define SPI_STM32_SLAVE_SW           SPI_STM32_SLAVE_MGMT_MODE(0x3)

#endif /* __SPI_STM32_H__ */
