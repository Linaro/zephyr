/* spi_nrf5.c - SPI driver for Nordic nRF5x SoCs */
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
 *
 */

#define SYS_LOG_LEVEL CONFIG_SYS_LOG_SPI_LEVEL

#include <device.h>
#include <errno.h>

#include <logging/sys_log.h>
#include <sys_io.h>
#include <board.h>
#include <init.h>
#include <gpio.h>
#include <spi.h>

#include "spi_nrf5_priv.h"
#include <spi/spi_nrf5.h>

#define DEV_CFG(dev)	\
	((const struct spi_nrf5_config * const)(dev)->config->config_info)
#define DEV_DATA(dev)	\
	((struct spi_nrf5_data * const)(dev)->driver_data)
#define SPI_REGS(dev)	\
	((volatile struct spi_slave_nrf5 *)(DEV_CFG(dev))->base_addr)

static inline bool is_buf_in_ram(const void *buf)
{
	return ((((uintptr_t) buf) & 0xE0000000) == 0x20000000);
}

static void spis_nrf5_print_cfg_registers(struct device *dev)
{
	volatile __attribute__((__unused__))
		struct spi_slave_nrf5 *regs = SPI_REGS(dev);

	SYS_LOG_DBG("\n"
		"SHORTS: %x, IRQ: %x, SEMSTAT: %x\n"
		"CONFIG: %x, STATUS: %x, ENABLE: %x\n"
		"SCKPIN: %x, MISOPIN: %x, MOSIPIN: %x, CSNPIN: %x\n"
		"RXD (PTR: %x, MAXCNT: %x, AMOUNT: %x)\n"
		"TXD (PTR: %x, MAXCNT: %x, AMOUNT: %x)",
		regs->SHORTS, regs->INTENSET, regs->SEMSTAT,
		regs->CONFIG, regs->STATUS, regs->ENABLE,
		regs->PINSEL_SCK, regs->PINSEL_MISO,
		regs->PINSEL_MOSI, regs->PINSEL_CSN,
		regs->RXD_PTR, regs->RXD_MAXCNT, regs->RXD_AMOUNT,
		regs->TXD_PTR, regs->TXD_MAXCNT, regs->TXD_AMOUNT);
}

/**
 * @brief Configure the SPI host controller for operating against slaves
 * @param dev Pointer to the device structure for the driver instance
 * @param config Pointer to the application provided configuration
 *
 * @return 0 if successful, another DEV_* code otherwise.
 */
static int spis_nrf5_configure(struct device *dev, struct spi_config *config)
{
	volatile struct spi_slave_nrf5 *spi_regs = SPI_REGS(dev);
	uint32_t flags;

	/* make sure module is disabled */
	spi_regs->ENABLE = 0;

	/* TODO: Muck with IRQ priority if needed */

	/* Clear any pending events */
	spi_regs->EVENTS_ACQUIRED = 0;
	spi_regs->EVENTS_ENDRX = 0;
	spi_regs->EVENTS_END = 0;
	spi_regs->INTENCLR = 0xFFFFFFFF; /* do we need to clear INT ?*/
	spi_regs->SHORTS = SPI_NRF5_SHORTCUT_END_ACQUIRE;
	spi_regs->INTENSET |= (SPIS_INTENSET_ACQUIRED_Msk |
			       SPIS_INTENSET_END_Msk);

	/* default transmit and over-read characters */
	spi_regs->DEF = 0x00000055;
	spi_regs->ORC = 0x000000AA;

	/* user configuration */
	flags = config->config;

	spi_regs->CONFIG = (flags & SPI_TRANSFER_LSB) ? (1 << 0) : 0;
	spi_regs->CONFIG |= (flags & SPI_MODE_CPHA) ? (1 << 1) : 0;
	spi_regs->CONFIG |= (flags & SPI_MODE_CPOL) ? (1 << 2) : 0;

	/* Enable the SPIS - peripherals sharing same ID will be disabled */
	spi_regs->ENABLE = SPIS_NRF5_ENABLE;

	spis_nrf5_print_cfg_registers(dev);

	SYS_LOG_DBG("SPI Slave Driver configured");

	return 0;
}

/**
 * @brief Read and/or write a defined amount of data through an SPI driver
 *
 * @param dev Pointer to the device structure for the driver instance
 * @param tx_buf Memory buffer that data should be transferred from
 * @param tx_buf_len Size of the memory buffer available for reading from
 * @param rx_buf Memory buffer that data should be transferred to
 * @param rx_buf_len Size of the memory buffer available for writing to
 *
 * @return 0 if successful, another DEV_* code otherwise.
 */
static int spis_nrf5_transceive(struct device *dev, const void *tx_buf,
			 uint32_t tx_buf_len, void *rx_buf, uint32_t rx_buf_len)
{
	volatile struct spi_slave_nrf5 *spi_regs = SPI_REGS(dev);
	struct spi_nrf5_data *priv_data = DEV_DATA(dev);

	__ASSERT(!(tx_buf_len && !tx_buf  || rx_buf_len && !rx_buf),
		"spi_nrf5_transceive: ERROR - NULL buffers");

	/* Buffer needs to be in RAM for EasyDMA to work */
	if (!tx_buf || !is_buf_in_ram(tx_buf)) {
		SYS_LOG_ERR("Invalid TX buf %p", tx_buf);
		return -EINVAL;
	}
	if (!rx_buf || !is_buf_in_ram(rx_buf)) {
		SYS_LOG_ERR("Invalid RX buf %p", rx_buf);
		return -EINVAL;
	}

	priv_data->error = 0;

	if (spi_regs->SEMSTAT == 1) {
		spi_regs->TXD_PTR = (uint32_t) tx_buf;
		spi_regs->RXD_PTR = (uint32_t) rx_buf;
		spi_regs->TXD_MAXCNT = tx_buf_len;
		spi_regs->RXD_MAXCNT = rx_buf_len;
		spi_regs->TASKS_RELEASE = 1;
	} else {
		SYS_LOG_ERR("SEM not allocated to the CPU, transfer still in progress?");
		return -EIO;
	}

	/* wait for transfer to complete */
	k_sem_take(&priv_data->device_sync_sem, K_FOREVER);

	if (priv_data->error) {
		priv_data->error = 0;
		return -EIO;
	}

	return 0;
}

/**
 * @brief Complete SPI module data transfer operations.
 * @param dev Pointer to the device structure for the driver instance
 * @param error Error condition (0 = no error, otherwise an error occurred)
 * @return None.
 */
static void spis_nrf5_complete(struct device *dev, uint32_t error)
{
	volatile __attribute__((__unused__))
		struct spi_slave_nrf5 *spi_regs = SPI_REGS(dev);
	struct spi_nrf5_data *priv_data = DEV_DATA(dev);

	SYS_LOG_DBG("bytes transferred: TX: %u, RX: %u [%s]",
		spi_regs->TXD_AMOUNT, spi_regs->RXD_AMOUNT,
		error == 0 ? "OK":"KO");

	priv_data->error = error ? 1 : 0;

	k_sem_give(&priv_data->device_sync_sem);
}

/**
 * @brief SPI module interrupt handler.
 * @param arg Pointer to the device structure for the driver instance
 * @return None.
 */
static void spis_nrf5_isr(void *arg)
{
	struct device *dev = arg;
	volatile struct spi_slave_nrf5 *spi_regs = SPI_REGS(dev);
	uint32_t error = 0;
	uint32_t tmp;

	/* We get an interrupt for the following reasons:
	 * 1. Semapthore ACQUIRED:
	 *      Semaphore is assigned to the CPU again (always happens
	 *      after END if the END_ACQUIRE SHORTS is set)
	 * 2. End of Granted SPI transaction:
	 *      Used to unblocked the caller, finishing the transaction
	 */

	/* NOTE:
	 * Section 15.8.1 of nrf52 manual suggests reading back the register
	 * to cause a 4-cycle delay to prevent the interrupt from
	 * re-occurring
	 */

	if (spi_regs->EVENTS_END) {
		spi_regs->EVENTS_END = 0;
		/* force register flush (per spec) */
		tmp = spi_regs->EVENTS_END;

		spis_nrf5_complete(dev, error);
	}
	if (spi_regs->EVENTS_ACQUIRED) {
		spi_regs->EVENTS_ACQUIRED = 0;
		/* force registesr flush (per spec) */
		tmp = spi_regs->EVENTS_ACQUIRED;
	}
}

static const struct spi_driver_api nrf5_spis_api = {
	.transceive = spis_nrf5_transceive,
	.configure = spis_nrf5_configure,
	.slave_select = NULL,
};

static int spis_nrf5_init(struct device *dev)
{
	volatile struct spi_slave_nrf5 *spi_regs = SPI_REGS(dev);
	struct spi_nrf5_data *priv_data = DEV_DATA(dev);
	const struct spi_nrf5_config *cfg = DEV_CFG(dev);
	struct device *gpio_dev;
	int ret;

	SYS_LOG_DBG("SPI Slave driver init: %p", dev);

	/* Enable constant latency for faster SPIS response */
	NRF_POWER->TASKS_CONSTLAT = 1;

	spi_regs->ENABLE = 0;

	gpio_dev = device_get_binding(CONFIG_GPIO_NRF5_P0_DEV_NAME);

	ret = gpio_pin_configure(gpio_dev, CONFIG_SPI_0_NRF5_GPIO_MISO,
				  GPIO_DIR_IN | GPIO_PUD_NORMAL);
	__ASSERT_NO_MSG(!ret);

	ret = gpio_pin_configure(gpio_dev, CONFIG_SPI_0_NRF5_GPIO_MOSI,
				  GPIO_DIR_IN | GPIO_PUD_NORMAL);
	__ASSERT_NO_MSG(!ret);

	ret = gpio_pin_configure(gpio_dev, CONFIG_SPI_0_NRF5_GPIO_CS,
				  GPIO_DIR_IN | GPIO_PUD_PULL_UP);
	__ASSERT_NO_MSG(!ret);

	ret = gpio_pin_configure(gpio_dev, CONFIG_SPI_0_NRF5_GPIO_SCK,
				  GPIO_DIR_IN | GPIO_PUD_NORMAL);
	__ASSERT_NO_MSG(!ret);

	spi_regs->PINSEL_MOSI = CONFIG_SPI_0_NRF5_GPIO_MOSI;
	spi_regs->PINSEL_MISO = CONFIG_SPI_0_NRF5_GPIO_MISO;
	spi_regs->PINSEL_SCK  = CONFIG_SPI_0_NRF5_GPIO_SCK;
	spi_regs->PINSEL_CSN  = CONFIG_SPI_0_NRF5_GPIO_CS;

	cfg->config_func();

	k_sem_init(&priv_data->device_sync_sem, 0, 1);

	SYS_LOG_DBG("SPI Slave driver initialized on device: %p", dev);

	return 0;
}

/* system bindings */
#ifdef CONFIG_SPI_0

#ifdef CONFIG_SOC_SERIES_NRF52X
	#define NRF5_SPIS_0_IRQ NRF5_IRQ_SPI0_TWI0_IRQn
#elif CONFIG_SOC_SERIES_NRF51X
	#define NRF5_SPIS_0_IRQ NRF5_IRQ_SPI1_TWI1_IRQn
#endif

static void spis_config_0_irq(void);

static struct spi_nrf5_data spis_nrf5_data_port_0;

static const struct spi_nrf5_config spis_nrf5_config_0 = {
#ifdef CONFIG_SOC_SERIES_NRF52X
	.base_addr = NRF_SPIS0_BASE,
#elif CONFIG_SOC_SERIES_NRF51X
	.base_addr = NRF_SPIS1_BASE,
#endif
	.config_func = spis_config_0_irq
};

DEVICE_AND_API_INIT(spis_nrf5_port_0, CONFIG_SPI_0_NAME, spis_nrf5_init,
		    &spis_nrf5_data_port_0, &spis_nrf5_config_0, PRE_KERNEL_1,
		    CONFIG_SPI_INIT_PRIORITY, &nrf5_spis_api);

static void spis_config_0_irq(void)
{
	IRQ_CONNECT(NRF5_SPIS_0_IRQ, CONFIG_SPI_0_IRQ_PRI,
		    spis_nrf5_isr, DEVICE_GET(spis_nrf5_port_0), 0);
	irq_enable(NRF5_SPIS_0_IRQ);
}

#endif /* CONFIG_SPI_0 */
