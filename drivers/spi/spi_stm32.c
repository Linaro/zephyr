/* spi_stm32.c - Driver implementation for STM32 SPI controller */

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
#include <misc/sys_log.h>
#include <misc/printk.h>
#include <misc/__assert.h>

#include <board.h>
#include <errno.h>

#include <clock_control/stm32_clock_control.h>

#include <spi.h>
#include <spi/spi_stm32.h>
#include "spi_stm32_priv.h"

/* convenience defines */
#define DEV_CFG(dev)	\
	((struct spi_stm32_config * const)(dev)->config->config_info)
#define DEV_DATA(dev)	\
	((struct spi_stm32_data * const)(dev)->driver_data)
#define SPI_REGS(dev)	\
	((volatile struct spi_stm32 *)(DEV_CFG(dev))->base_addr)

/* # of possible SPI baud rate scaler values */
#define SPI_STM32_NUM_SCALERS 8

/* SPI baud rate scaler values, programmed in SPI_CR1[BR] */
static const uint32_t baud_rate_scaler[SPI_STM32_NUM_SCALERS] = {
	2, 4, 8, 16, 32, 64, 128, 256
};

struct pending_transfer {
	struct device *dev;
};
static struct pending_transfer pending_transfers[4];

static const uint32_t tx_padding = 0x55;

static void spi_stm32_enable_irq(struct device *dev, uint16_t flags)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	spi_regs->cr2.val |= flags;
}

static void spi_stm32_disable_irq(struct device *dev, uint16_t flags)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	spi_regs->cr2.val &= ~flags;
}

static int spi_stm32_tx_empty(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	return spi_regs->sr.bit.txe;
}

static int spi_stm32_rx_not_empty(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	return spi_regs->sr.bit.rxne;
}

static void spi_stm32_rx_quiesce(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);
	uint16_t tmp;

	while (spi_stm32_rx_not_empty(dev)) {
		/* read the register to clear it */
		tmp = spi_regs->dr;
	}
	/* RX buffer is empty now */
}

static void spi_stm32_quiesce(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	spi_stm32_rx_quiesce(dev);
	/* RX/TX buffers empty now */
	while (spi_regs->sr.bit.bsy) {
		/* Twiddle thumbs */

		/* FIXME: Add a timeout? */
	}
	/* SPI is not busy now */
}

/**
 * @brief Stop SPI module operation.
 * @param dev Pointer to the device structure for the driver instance
 * @return None.
 */
static inline void spi_stm32_stop(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	/* Ensure module operation is stopped as per Section 20.3.8 */
	SYS_LOG_DBG("Prepare to stop");
	spi_stm32_quiesce(dev);

	spi_regs->cr1.bit.spe = SPI_STM32_CR1_DISABLE;
}

/**
 * @brief Enable SPI module operation.
 * @param dev Pointer to the device structure for the driver instance
 * @return None.
 */
static inline void spi_stm32_start(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	/* Allow module operation */
	spi_regs->cr1.bit.spe = SPI_STM32_CR1_ENABLE;
}

static void spi_stm32_show_cr(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);

	__attribute((__unused__)) int p = spi_regs->cr1.val;
	__attribute((__unused__)) int q = spi_regs->cr2.val;

	SYS_LOG_DBG("cr1: %x [br: %u, sw slave mgmt(ssm): %u, ssi (sw-only on "
		    "master): %u, mstr: %u]\ncr2: %x [ssoe (master-only): %u]",
		    p, spi_regs->cr1.bit.br, spi_regs->cr1.bit.ssm,
		    spi_regs->cr1.bit.ssi, spi_regs->cr1.bit.mstr,
		    q, spi_regs->cr2.bit.ssoe);
}

static void spi_stm32_show_status(struct device *dev, char *fn)
{
	__attribute((__unused__)) volatile struct spi_stm32 *spi_regs;

	spi_regs = SPI_REGS(dev);

	SYS_LOG_DBG("%s: show_status: %x", fn, spi_regs->sr.val);
	SYS_LOG_DBG("%s: txe: %u, rxne: %u, ovr: %u, modf: %u, "
		    "bsy: %u, fre: %u", fn,
		    spi_regs->sr.bit.txe,
		    spi_regs->sr.bit.rxne,
		    spi_regs->sr.bit.ovr,
		    spi_regs->sr.bit.modf,
		    spi_regs->sr.bit.bsy,
		    spi_regs->sr.bit.fre);
}

static void spi_stm32_clear_errors(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	uint32_t status = spi_regs->sr.val;
	uint32_t tmp;

	if (status & SPI_STM32_SR_OVERRUN) {
		SYS_LOG_ERR("Overrun error detected. Trying to recover.");
		tmp = spi_regs->dr;
		tmp = spi_regs->sr.val;
	}

	if (status & SPI_STM32_SR_MODE_ERROR) {
		SYS_LOG_ERR("MODF error detected. Trying to recover.");
		if (priv_data->mode == SPI_STM32_MASTER_MODE) {
			spi_regs->cr1.bit.mstr = 1;
			SYS_LOG_DBG("Restoring master mode");
		} else if (priv_data->mode == SPI_STM32_SLAVE_MODE) {
			spi_regs->cr1.bit.mstr = 0;
			SYS_LOG_DBG("Restoring slave mode");
		}
	}

	if (status & SPI_STM32_SR_CRC_ERROR) {
		SYS_LOG_ERR("CRC error detected. Trying to recover.");
		/* TODO: Fill up recovery process */
	}

	status = spi_regs->sr.val;

	if (status & (SPI_STM32_SR_OVERRUN | SPI_STM32_SR_MODE_ERROR |
		      SPI_STM32_SR_CRC_ERROR)) {
		SYS_LOG_ERR("Error still persists, system might be unstable");
	}
}

/**
 * @brief Set a SPI baud rate nearest to the desired rate, without exceeding it.
 * @param dev Pointer to the device structure for the driver instance
 * @param req_baud The desired baud rate.
 *
 * @return An index into the baud_rate_scaler table or an error code
 */
static uint32_t spi_stm32_set_baud_rate(struct device *dev, uint32_t req_baud)
{
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	struct spi_stm32_config *cfg = DEV_CFG(dev);
	uint32_t clock;
	uint32_t div;
	uint32_t i;

	/* This uses a simple function that doesn't try to hard to change
	 * the parent clock divider to make a closer match to the requested
	 * baudrate. It is left to the user to change the value of PCLK1/PCLK2.
	 */
#if CONFIG_SOC_SERIES_STM32F4X
	clock_control_get_rate(priv_data->clock,
			(clock_control_subsys_t *) &cfg->pclken, &clock);
#else
	SYS_LOG_ERR("Unknown clock setup for the SPI device. Aborting.");
	return -ENOTSUP;
#endif
	/* Baud rate calculations
	 *
	 * Desired SPI baud rate = f_clk / SPI divider
	 * Example (STM32F401, SPI2, PCLK2 @ 42MHz, Requested 300KHz)
	 *
	 *  f_clk == PCLK1 == 42MHz
	 *  desired SPI baudrate = 300KHz
	 *  Hence, SPI divider = 42MHz / 300KHz = 140
	 *
	 * Therefore, divider = 256, since 128 would give baudrate more
	 * than asked for.
	 */
	div = (int) clock / req_baud;
	SYS_LOG_DBG("Requested rate: %u Hz, parent clock: %u Hz, div: %u",
		req_baud, clock, div);
	/* Return first divider greater than div */
	for (i = 0; i < SPI_STM32_NUM_SCALERS; i++) {
		if (baud_rate_scaler[i] >= div) {
			priv_data->baud_rate = clock / baud_rate_scaler[i];
			return i;
		}
	}

	return -EINVAL;
}

/**
 * @brief Configure the SPI host controller for operating against slaves
 * @param dev Pointer to the device structure for the driver instance
 * @param config Pointer to the application provided configuration
 *
 * @return 0 if successful, an error code otherwise.
 */
static int spi_stm32_configure(struct device *dev, struct spi_config *config)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	uint32_t flags = config->config;
	uint32_t frame_sz;	/* frame size, in bits */
	int ret;

	SYS_LOG_INF("dev %p (regs @ 0x%x), config 0x%x, freq 0x%x", dev,
			spi_regs, config->config, config->max_sys_freq);

	/*
	 * Ensure module operation is stopped and module is reset
	 */
	spi_stm32_stop(dev);

	/* Set operational mode */
	if (SPI_STM32_OP_MODE_GET(flags) == SPI_STM32_MASTER_MODE) {
		priv_data->mode = SPI_STM32_MASTER_MODE;
		spi_regs->cr1.bit.mstr = 1;
	} else if (SPI_STM32_OP_MODE_GET(flags) == SPI_STM32_SLAVE_MODE) {
		priv_data->mode = SPI_STM32_SLAVE_MODE;
		spi_regs->cr1.bit.mstr = 0;
	} else {
		SYS_LOG_ERR("Invalid operation mode!");
		return -1;
	}

	/* Set baud rate */
	ret = spi_stm32_set_baud_rate(dev, config->max_sys_freq);
	if (ret >= 0) {
		spi_regs->cr1.bit.br = ret;
		SYS_LOG_INF("SPI baud rate set to: %u Hz [div %u]",
			priv_data->baud_rate, baud_rate_scaler[ret]);
	} else {
		SYS_LOG_ERR("Unable to set SPI to %u Hz, "
					"try changing APBx prescaler");
		return ret;
	}

	/* Frame format */
	if (flags & SPI_STM32_FRAME_TI) {
		spi_regs->cr2.bit.frf = 1;
		/* TI mode doesn't require CPOL, CPHA, LSB, SSM, SSI, SSOE */
		goto out;
	}

	/* Slave control */
	if (SPI_STM32_SLAVE_MGMT_MODE_GET(flags) == SPI_STM32_SLAVE_SW) {
		spi_regs->cr1.bit.ssm = SPI_STM32_CR1_SW_SLAVE_MANAGEMENT;
		/* SSI setting as per 20.3.2 and 20.3.3 - very confusing HW */
		if (SPI_STM32_OP_MODE_GET(flags) == SPI_STM32_MASTER_MODE) {
			spi_regs->cr1.bit.ssi = 1;
		} else {
			spi_regs->cr1.bit.ssi = 0;
		}
	} else {
		spi_regs->cr1.bit.ssm = SPI_STM32_CR1_HW_SLAVE_MANAGEMENT;
	}
	if (SPI_STM32_SLAVE_MGMT_MODE_GET(flags) ==
				SPI_STM32_SLAVE_HW_SS_OUTPUT) {
		spi_regs->cr2.bit.ssoe = 1;
	} else if (SPI_STM32_SLAVE_MGMT_MODE_GET(flags) ==
				SPI_STM32_SLAVE_HW_NO_OUTPUT) {
		spi_regs->cr2.bit.ssoe = 0;
	}

	/* Set clock polarity and phase */
	if (flags & SPI_MODE_CPOL) {
		spi_regs->cr1.bit.cpol =
			SPI_STM32_CR1_CLOCK_POLARITY_IDLE_HIGH;
	}
	if (flags & SPI_MODE_CPHA) {
		spi_regs->cr1.bit.cpha =
			SPI_STM32_CR1_CLOCK_PHASE_TRAILING_EDGE_CAPTURE;
	}

	/* Byte ordering */
	if (flags & SPI_TRANSFER_LSB) {
		spi_regs->cr1.bit.lsb_first = SPI_STM32_CR1_LSBFIRST;
	}

out:
	/*
	 * Frame size is limited to 16 bits (vs. 8 bit value in
	 * struct spi_config), programmed as: (frame_size - 1)
	 */
	frame_sz = SPI_WORD_SIZE_GET(flags);
	if (frame_sz > SPI_STM32_WORD_SIZE_MAX) {
		SYS_LOG_ERR("Frame size larger than max word size");
		return -ENOTSUP;
	}

	/* 8-bit frames */
	/* FIXME: Support 16-bit too */
	spi_regs->cr1.bit.dff = SPI_STM32_CR1_DATA_FRAME_FORMAT_8BIT;
	priv_data->frame_sz = frame_sz;

	/* Initialize Tx/Rx queue */
	priv_data->tx_buf = priv_data->rx_buf = NULL;
	priv_data->tx_buf_len = priv_data->rx_buf_len = 0;

	spi_stm32_show_cr(dev);
	SYS_LOG_INF("STM32 SPI Driver configured");

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
static int spi_stm32_transceive(struct device *dev,
				const void *tx_buf, uint32_t tx_buf_len,
				void *rx_buf, uint32_t rx_buf_len)
{
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	struct spi_stm32_config *cfg = DEV_CFG(dev);
	uint32_t num = cfg->num;
	uint16_t irqs;

	/* Critical section */
	nano_sem_take(&priv_data->sem, TICKS_UNLIMITED);
	if (pending_transfers[num].dev) {
		nano_sem_give(&priv_data->sem);
		return -EBUSY;
	}
	pending_transfers[num].dev = dev;
	nano_sem_give(&priv_data->sem);

	SYS_LOG_DBG(": dev %p, Tx: %p (%u), Rx: %p (%u)",
		    dev, tx_buf, tx_buf_len, rx_buf, rx_buf_len);

#ifdef CONFIG_SYS_LOG_SPI_LEVEL
	__ASSERT(!((tx_buf_len && (tx_buf == NULL)) ||
		   (rx_buf_len && (rx_buf == NULL))),
		 "spi_stm32_transceive: ERROR - NULL buffer");
#endif

	/* Set buffers info */
	priv_data->tx_buf = tx_buf;
	priv_data->rx_buf = rx_buf;
	priv_data->tx_buf_len = tx_buf_len;
	priv_data->rx_buf_len = rx_buf_len;
	priv_data->error = 0;
	priv_data->transmitted = 0;
	priv_data->received = 0;
	priv_data->trans_len = max(tx_buf_len, rx_buf_len);

	/* enable transfer operations - must be before enabling interrupts */
	spi_stm32_start(dev);

	/*
	 * Enable interrupts:
	 * - Transmit Buffer Empty (no more data to send)
	 * - Receive Buffer Not empty (new data arrived)
	 * - Errors
	 *
	 * NOTE: DMA requests are not yet supported.
	 */
	irqs = (SPI_STM32_CR2_ERRIE | SPI_STM32_CR2_RXNEIE |
			SPI_STM32_CR2_TXEIE);
	spi_stm32_enable_irq(dev, irqs);

	spi_stm32_show_status(dev, "transcieve-after-irq-enable");

	/* wait for transfer to complete */
	device_sync_call_wait(&priv_data->sync);

	/* check completion status */
	if (priv_data->error) {
		spi_stm32_clear_errors(dev);
		priv_data->error = 0;
		return -EIO;
	}

	return 0;
}

static void spi_stm32_push_byte(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	uint8_t data;

	if (priv_data->transmitted < priv_data->tx_buf_len) {
		data = *(uint8_t *)(priv_data->tx_buf);
		priv_data->tx_buf++;
	} else {
		data = 0x55; /* dummy char to clock when out of TX buffer */
	}
	/* Write data to MOSI/MISO */
	spi_regs->dr = data;
	priv_data->transmitted++;
	SYS_LOG_DBG(": [0x%x, (%c), %d]", data, data, data);
}

static void spi_stm32_pull_byte(struct device *dev)
{
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	uint8_t data;

	/* Read data from MOSI/MISO */
	data = (uint8_t) spi_regs->dr;
	if (priv_data->received < priv_data->rx_buf_len) {
		*(uint8_t *)(priv_data->rx_buf) = data;
		priv_data->rx_buf++;
	}
	priv_data->received++;

	SYS_LOG_DBG(": [0x%x, (%c), %d]", data, data, data);
}

static void spi_stm32_txrx(struct device *dev)
{
	struct spi_stm32_data *priv_data = DEV_DATA(dev);

#ifdef CONFIG_SPI_SLAVE
	/* NOTE: In case of slave, there is no guarantee that its Tx
	 * buffer will get emptied since it is clocked by Master
	 */
	nano_sem_take(&priv_data->sem, TICKS_UNLIMITED);

	if (spi_stm32_tx_empty(dev)) {
		spi_stm32_push_byte(dev);
	}

	if (spi_stm32_rx_not_empty(dev)) {
		spi_stm32_pull_byte(dev);
	}

	nano_sem_give(&priv_data->sem);

#else /* MASTER */

	while ((priv_data->transmitted < priv_data->tx_buf_len) ||
	       (priv_data->received < priv_data->rx_buf_len)) {

		nano_sem_take(&priv_data->sem, TICKS_UNLIMITED);

		while (!spi_stm32_tx_empty(dev)) {
		}
		spi_stm32_push_byte(dev);

		while (!spi_stm32_rx_not_empty(dev)) {
		}
		spi_stm32_pull_byte(dev);

		nano_sem_give(&priv_data->sem);
	}
#endif
}

/**
 * @brief Complete SPI module data transfer operations.
 * @param dev Pointer to the device structure for the driver instance
 * @param error Error condition (0 = no error, otherwise an error occurred)
 * @return None.
 */
static void spi_stm32_complete(struct device *dev, uint32_t error)
{
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	struct spi_stm32_config *cfg = DEV_CFG(dev);
	uint32_t num = cfg->num;
	struct pending_transfer *pending = &pending_transfers[num];
	uint16_t irqs = (SPI_STM32_CR2_RXNEIE | SPI_STM32_CR2_TXEIE |
			 SPI_STM32_CR2_ERRIE);

	/* if received == trans_len, then transmitted == trans_len */
	if (!(priv_data->received == priv_data->trans_len) && !error) {
		return;
	}

	priv_data->error = error;

	nano_sem_take(&priv_data->sem, TICKS_UNLIMITED);
	pending->dev = NULL;
	nano_sem_give(&priv_data->sem);

	spi_stm32_disable_irq(dev, irqs);
	priv_data->tx_buf = priv_data->rx_buf = NULL;
	priv_data->tx_buf_len = priv_data->rx_buf_len = 0;

	/* Disable transfer operations */
	spi_stm32_stop(dev);

	SYS_LOG_DBG("Total: Tx [%u], Rx [%u] bytes",
		priv_data->transmitted, priv_data->received);

	if (error) {
		SYS_LOG_ERR("Transaction aborted due to error!");
	}

	/* Signal completion */
	device_sync_call_complete(&priv_data->sync);
}

/**
 * @brief SPI module interrupt handler.
 * @param arg Pointer to the device structure for the driver instance
 * @return None.
 */
void spi_stm32_isr(void *arg)
{
	struct device *dev = arg;
	volatile struct spi_stm32 *spi_regs = SPI_REGS(dev);
	uint32_t error = 0;
	uint16_t status = spi_regs->sr.val;

	SYS_LOG_DBG("spi_stm32_isr: dev %p, status 0x%x", dev, status);

	if (status & (SPI_STM32_SR_OVERRUN | SPI_STM32_SR_MODE_ERROR |
		      SPI_STM32_SR_CRC_ERROR)) {
		error = 1;
		goto out;
	}
	spi_stm32_txrx(dev);

out:
	/* finish processing, if data transfer is complete */
	spi_stm32_complete(dev, error);
}

static struct spi_driver_api stm32_spi_api = {
	.configure = spi_stm32_configure,
	.slave_select = NULL,
	.transceive = spi_stm32_transceive,
};

static inline void __spi_stm32_get_clock(struct device *dev)
{
	struct spi_stm32_data *priv_data = DEV_DATA(dev);
	struct device *clk =
		device_get_binding(STM32_CLOCK_CONTROL_NAME);

	__ASSERT_NO_MSG(clk);
	priv_data->clock = clk;
}

int spi_stm32_init(struct device *dev)
{
	struct spi_stm32_config *cfg = DEV_CFG(dev);
	struct spi_stm32_data *priv_data = DEV_DATA(dev);

	__spi_stm32_get_clock(dev);

	/* Enable module clocking */
	clock_control_on(priv_data->clock,
			(clock_control_subsys_t *) &cfg->pclken);

	/*
	 * Ensure module operation is stopped and module is reset
	 */
	spi_stm32_stop(dev);
	/* TODO: Reset the module through APB2RSTR */

	/* Set up the synchronous call mechanism */
	device_sync_call_init(&priv_data->sync);
	nano_sem_init(&priv_data->sem);
	nano_sem_give(&priv_data->sem);

	/* Configure and enable SPI module IRQs */
	cfg->config_func();

	SYS_LOG_INF("STM32 SPI Driver initialized on device: %p", dev);

	return 0;
}

/* FIXME: SPI Kconfig starts counting from 0, stm32 starts counting from spi 1,
 * make it less confusing by having per-board/per-soc Kconfig
 */

/* system bindings */
#ifdef CONFIG_SPI_0

void spi_config_1_irq(void);

struct spi_stm32_data spi_stm32_data_port_1;

struct spi_stm32_config spi_stm32_config_1 = {
	.num = 0,
	.base_addr = SPI1_ADDR,
	.pclken = { .bus = STM32F4X_CLOCK_BUS_APB2,
		    .enr = STM32F4X_CLOCK_ENABLE_SPI1 },
	.config_func = spi_config_1_irq,
};

DEVICE_AND_API_INIT(spi_stm32_port_1, CONFIG_SPI_0_NAME, spi_stm32_init,
		    &spi_stm32_data_port_1, &spi_stm32_config_1, PRIMARY,
		    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &stm32_spi_api);


void spi_config_1_irq(void)
{
	IRQ_CONNECT(STM32F4_IRQ_SPI1, CONFIG_SPI_0_IRQ_PRI,
		    spi_stm32_isr, DEVICE_GET(spi_stm32_port_1), 0);
	irq_enable(STM32F4_IRQ_SPI1);
}

#endif /* CONFIG_SPI_0 */

#ifdef CONFIG_SPI_1

void spi_config_2_irq(void);

struct spi_stm32_data spi_stm32_data_port_2;

struct spi_stm32_config spi_stm32_config_2 = {
	.num = 1,
	.base_addr = SPI2_ADDR,
	.pclken = { .bus = STM32F4X_CLOCK_BUS_APB1,
		    .enr = STM32F4X_CLOCK_ENABLE_SPI2 },
	.config_func = spi_config_2_irq,
};

DEVICE_AND_API_INIT(spi_stm32_port_2, CONFIG_SPI_1_NAME, spi_stm32_init,
		    &spi_stm32_data_port_2, &spi_stm32_config_2, PRIMARY,
		    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &stm32_spi_api);


void spi_config_2_irq(void)
{
	IRQ_CONNECT(STM32F4_IRQ_SPI2, CONFIG_SPI_1_IRQ_PRI,
		    spi_stm32_isr, DEVICE_GET(spi_stm32_port_2), 0);
	irq_enable(STM32F4_IRQ_SPI2);
}

#endif /* CONFIG_SPI_1 */
