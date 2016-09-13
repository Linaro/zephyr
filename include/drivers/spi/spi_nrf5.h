/* spi_nrf5.h - Nordic Semiconductor nRF5x SPI controller driver utilities */

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *               2016 Linaro Limited.
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

#ifndef __SPI_NRF5_H__
#define __SPI_NRF5_H__

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
 * Operational Mode [ 12 ]      - Master (0), Slave (1)
 *
 */

#define SPI_NRF5_OP_MODE_SET(_in_)	((_in_) << 12)

/* Additional mode settings on nRF5 */
#define SPI_NRF5_OP_MODE_SLAVE SPI_NRF5_OP_MODE_SET(1)
#define SPI_NRF5_OP_MODE_MASTER SPI_NRF5_OP_MODE_SET(0)

/* NRF5 SPI word/frame size is limited to 8 bits, represented as: (size - 1) */
#define SPI_NRF5_WORD_SIZE_MAX	(8)

#endif /* __SPI_NRF5_H__ */
