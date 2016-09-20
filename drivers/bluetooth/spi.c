/* spi.c - SPI based Bluetooth driver */

/*
 * Copyright (c) 2016 Linaro Limited
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

#include <errno.h>
#include <stddef.h>

#include <nanokernel.h>
#include <arch/cpu.h>
#include <atomic.h>
#include <sections.h>

#include <board.h>
#include <init.h>
#include <spi.h>
#include <gpio.h>
#include <misc/util.h>
#include <misc/byteorder.h>
#include <misc/stack.h>
#include <misc/nano_work.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/log.h>
#include <bluetooth/hci.h>
#include <bluetooth/driver.h>

#if !defined(CONFIG_BLUETOOTH_DEBUG_DRIVER)
#undef BT_DBG
#define BT_DBG(fmt, ...)
#endif

/* SPI / GPIO Board Configuration */
/* TODO: Make this generic */
#if defined(CONFIG_SPI_STM32)
#include <spi/spi_stm32.h>
#define SPI_MAX_CLK_FREQ	SPI_STM32_CLK_FREQ_2MHZ
#define SPI_CONFIG_EXTRA	SPI_STM32_SLAVE_HW_SS_OUTPUT
#define SOC_MASTER_MODE		SPI_STM32_MASTER_MODE
/* TODO: Extract values from Kconfig */
#define GPIO_DRV_NAME		"GPIOB"
/* Pin RDY is used by the slave to announce it is ready to transfer over spi */
#define GPIO_RDY_DIR		GPIO_DIR_IN
#define GPIO_RDY_PULL		GPIO_PUD_PULL_DOWN
#define GPIO_RDY_PIN		0
#define GPIO_RDY_EDGE		(GPIO_INT_EDGE | GPIO_INT_ACTIVE_HIGH)
/* Pin REQ is used by the slave to request permission to send data to master */
#define GPIO_REQ_DIR		GPIO_DIR_IN
#define GPIO_REQ_PULL		GPIO_PUD_PULL_DOWN
#define GPIO_REQ_PIN		1
#define GPIO_REQ_EDGE		(GPIO_INT_EDGE | GPIO_INT_ACTIVE_HIGH)
#if defined(CONFIG_BOARD_96B_CARBON)
#define GPIO_NRF51_RESET_PIN	4
#endif
#else /* CONFIG_SPI_STM32 */
#define SPI_MAX_CLK_FREQ	128
#define SPI_CONFIG_EXTRA	0
#endif

/* Hard-code size to 8 bits */
#define SPI_FRAME_SIZE		SPI_WORD(8)
#define SPI_OP_MODE		SOC_MASTER_MODE
#define SPI_SLAVE		0

/* Depends on the MAX_BUF_SIZE value */
#define SPI_BUF_HEADER_SIZE	1
/* Limit SPI buffer size to 255 based on the limit required by nRF51 */
#define SPI_MAX_BUF_SIZE	255

#define SPI_RDY_WAIT_TIMEOUT	1000

static BT_STACK_NOINIT(spi_send_fiber_stack, 512);
static BT_STACK_NOINIT(spi_recv_fiber_stack, 512);

struct nano_sem nano_sem_req;
struct nano_sem nano_sem_rdy;
struct nano_sem nano_sem_spi_active;

static struct device *spi_dev;
static struct device *gpio_dev;

struct nano_fifo bt_tx_queue;

static struct gpio_callback gpio_req_cb;
static struct gpio_callback gpio_rdy_cb;

static struct spi_config spi_conf = {
	.config = (SPI_FRAME_SIZE | SPI_OP_MODE | SPI_CONFIG_EXTRA),
	.max_sys_freq = SPI_MAX_CLK_FREQ,
};

#if defined(CONFIG_BLUETOOTH_DEBUG_DRIVER)
static void hexdump(const char *str, const uint8_t *packet, size_t length)
{
	int n = 0;

	if (!length) {
		printf("%s zero-length signal packet\n", str);
		return;
	}

	while (length--) {
		if (n % 16 == 0) {
			printf("%s %08X ", str, n);
		}

		printf("%02X ", *packet++);

		n++;
		if (n % 8 == 0) {
			if (n % 16 == 0) {
				printf("\n");
			} else {
				printf(" ");
			}
		}
	}

	if (n % 16) {
		printf("\n");
	}
}
#else
#define hexdump(str, packet, length)
#endif

static void _spi_show(struct spi_config *spi_conf)
{
	BT_DBG("SPI Configuration: %x", spi_conf->config);
	BT_DBG("\tbits per word: %u",
		SPI_WORD_SIZE_GET(spi_conf->config));
	BT_DBG("\tMode: %u", SPI_MODE(spi_conf->config));
	BT_DBG("\tMax speed Hz: 0x%X", spi_conf->max_sys_freq);
#if defined(CONFIG_SPI_STM32)
	BT_DBG("\tOperating mode: %s",
		SPI_STM32_OP_MODE_GET(spi_conf->config) ? "slave": "master");
#endif
}

void gpio_slave_req(struct device *gpio, struct gpio_callback *cb,
		    uint32_t pins)
{
	nano_isr_sem_give(&nano_sem_req);
}

void gpio_slave_rdy(struct device *gpio, struct gpio_callback *cb,
		    uint32_t pins)
{
	nano_isr_sem_give(&nano_sem_rdy);
}

static inline int bt_spi_transceive(const void *tx_buf, uint32_t tx_buf_len,
				    void *rx_buf, uint32_t rx_buf_len)
{
	BT_DBG("");

	/* Wait for the sem release */
	nano_fiber_sem_take(&nano_sem_rdy, SPI_RDY_WAIT_TIMEOUT);

	/* Can't go too fast, otherwise might read invalid data from slave */
	fiber_sleep(2);
	return spi_transceive(spi_dev, tx_buf, tx_buf_len, rx_buf, rx_buf_len);
}

static void spi_recv_fiber(void)
{
	BT_DBG("");

	uint8_t spi_tx_buf[2];
	uint8_t spi_rx_buf[SPI_MAX_BUF_SIZE];
	uint8_t buf_len;
	uint8_t bt_buf_type;
	uint8_t *offset;
	struct net_buf *buf;
	int ret;

	while (1) {
		nano_fiber_sem_take(&nano_sem_req, TICKS_UNLIMITED);
		BT_DBG("SPI slave request to send buf");

		/* Send empty header, announce we are ready to receive data */
		nano_fiber_sem_take(&nano_sem_spi_active, TICKS_UNLIMITED);
		BT_DBG("header - empty, announce ready to receive");
		memset(spi_tx_buf, 0, sizeof(spi_tx_buf));
		ret = bt_spi_transceive(spi_tx_buf, sizeof(spi_tx_buf),
					spi_rx_buf, sizeof(spi_rx_buf));
		if (ret < 0) {
			BT_ERR("SPI transceive error (empty header): %d", ret);
			nano_fiber_sem_give(&nano_sem_spi_active);
			continue;
		}

		/* Retrieve the data from the slave */
		memset(&spi_rx_buf, 0, sizeof(spi_rx_buf));
		ret = bt_spi_transceive(spi_tx_buf, sizeof(spi_tx_buf),
					spi_rx_buf, sizeof(spi_rx_buf));
		if (ret < 0) {
			BT_ERR("SPI transceive error: %d", ret);
			nano_fiber_sem_give(&nano_sem_spi_active);
			continue;
		}
		nano_fiber_sem_give(&nano_sem_spi_active);

		if (spi_rx_buf[0] == 0) {
			BT_ERR("Received 0 buffers over SPI, invalid data");
			continue;
		}

		BT_DBG("received %u bufs", spi_rx_buf[0]);
		offset = spi_rx_buf + 1;

		for (int i = 0; i < spi_rx_buf[0]; i++) {
			bt_buf_type = offset[0];
			buf_len = offset[1];
			offset += 2;

			switch (bt_buf_type) {
			case BT_BUF_EVT:
				BT_DBG("BT rx buf type EVT");
				buf = bt_buf_get_evt(bt_buf_type);
				if (!buf) {
					BT_ERR("No available event buffers!");
					continue;
				}
				break;
			case BT_BUF_ACL_IN:
				BT_DBG("BT rx buf type ACL_IN");
				buf = bt_buf_get_acl();
				if (!buf) {
					BT_ERR("No available ACL buffers!");
					continue;
				}
				break;
			default:
				BT_ERR("Unknown bt buf type %d, invalid data",
						bt_buf_type);
				continue;
			}

			memcpy(net_buf_add(buf, buf_len), offset, buf_len);
			hexdump("=>", buf->data, buf->len);
			bt_recv(buf);
			offset += buf_len;
			buf = NULL;
		}

		stack_analyze("SPI recv fiber", spi_recv_fiber_stack,
					sizeof(spi_recv_fiber_stack));
	}
}

static void spi_send_fiber(void)
{
	uint8_t spi_tx_buf[SPI_MAX_BUF_SIZE];
	uint8_t spi_rx_buf[2];
	struct net_buf *buf;
	int ret;

	BT_DBG("");

	while (1) {
		buf = net_buf_get_timeout(&bt_tx_queue, 0, TICKS_UNLIMITED);
		nano_fiber_sem_take(&nano_sem_spi_active, TICKS_UNLIMITED);

		hexdump("<=", buf->data, buf->len);

		/* Make sure the SPI buffer size is large enough */
		if (buf->len + 2 > sizeof(spi_tx_buf)) {
			BT_ERR("Net buffer too big, discarting %p len %d",
					buf, buf->len);
			goto send_done;
		}

		/* Set buffer and send over SPI */
		memset(spi_tx_buf, 0, sizeof(spi_tx_buf));
		spi_tx_buf[0] = (uint8_t) buf->len + 2; /* len and buf type */
		spi_tx_buf[1] = (uint8_t) bt_buf_get_type(buf);
		memcpy(spi_tx_buf + 2, buf->data, buf->len);
		BT_DBG("sending spi buf, type %d len %d",
				spi_tx_buf[1], spi_tx_buf[0]);
		ret = bt_spi_transceive(spi_tx_buf, spi_tx_buf[0],
					spi_rx_buf, sizeof(spi_rx_buf));
		if (ret < 0) {
			BT_ERR("SPI transceive error: %d", ret);
		}
send_done:
		nano_fiber_sem_give(&nano_sem_spi_active);
		net_buf_unref(buf);
		stack_analyze("SPI send fiber", spi_send_fiber_stack,
					sizeof(spi_send_fiber_stack));
	}
}

static int spi_open(void)
{
	BT_DBG("");

	spi_configure(spi_dev, &spi_conf);
	spi_slave_select(spi_dev, SPI_SLAVE);
	_spi_show(&spi_conf);

	/* GPIOs, /REQ and /RDY */
	gpio_dev = device_get_binding(GPIO_DRV_NAME);
	if (gpio_dev == NULL) {
		BT_ERR("Failed to initialize GPIO driver %s",
						GPIO_DRV_NAME);
		return -EIO;
	}
	gpio_pin_configure(gpio_dev, GPIO_REQ_PIN, GPIO_REQ_DIR |
					GPIO_INT | GPIO_REQ_EDGE);
	gpio_init_callback(&gpio_req_cb, gpio_slave_req, BIT(GPIO_REQ_PIN));
	gpio_add_callback(gpio_dev, &gpio_req_cb);
	gpio_pin_enable_callback(gpio_dev, GPIO_REQ_PIN);

	gpio_pin_configure(gpio_dev, GPIO_RDY_PIN, GPIO_RDY_DIR |
					GPIO_INT | GPIO_RDY_EDGE);
	gpio_init_callback(&gpio_rdy_cb, gpio_slave_rdy, BIT(GPIO_RDY_PIN));
	gpio_add_callback(gpio_dev, &gpio_rdy_cb);
	gpio_pin_enable_callback(gpio_dev, GPIO_RDY_PIN);

#if defined(CONFIG_BOARD_96B_CARBON)
	/* Reset nRF51 */
	gpio_pin_configure(gpio_dev, GPIO_NRF51_RESET_PIN,
					GPIO_DIR_OUT | GPIO_PUD_PULL_DOWN);
	gpio_pin_write(gpio_dev, GPIO_NRF51_RESET_PIN, 1);
#endif

	nano_sem_init(&nano_sem_req);
	nano_sem_init(&nano_sem_rdy);
	nano_sem_init(&nano_sem_spi_active);
	nano_sem_give(&nano_sem_spi_active);

	nano_fifo_init(&bt_tx_queue);

	fiber_start(spi_send_fiber_stack, sizeof(spi_send_fiber_stack),
			(nano_fiber_entry_t) spi_send_fiber, 0, 0, 7, 0);

	fiber_start(spi_recv_fiber_stack, sizeof(spi_recv_fiber_stack),
			(nano_fiber_entry_t) spi_recv_fiber, 0, 0, 7, 0);

	return 0;
}

static int spi_queue(struct net_buf *buf)
{
	BT_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);

	if ((bt_buf_get_type(buf) != BT_BUF_CMD) &&
			(bt_buf_get_type(buf) != BT_BUF_ACL_OUT)) {
		BT_ERR("Unknown packet type %u", bt_buf_get_type(buf));
		return -1;
	}

	net_buf_put(&bt_tx_queue, buf);

	return 0;
}

static struct bt_driver drv = {
	.name		= "SPI_Bus",
	.bus		= BT_DRIVER_BUS_SPI,
	.open		= spi_open,
	.send		= spi_queue,
};

static int _bt_spi_init(struct device *unused)
{
	ARG_UNUSED(unused);

	spi_dev = device_get_binding(CONFIG_BLUETOOTH_SPI_ON_DEV_NAME);
	if (spi_dev == NULL) {
		return -EINVAL;
	}

	bt_driver_register(&drv);

	return 0;
}

SYS_INIT(_bt_spi_init, NANOKERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
