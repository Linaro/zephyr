/* btspi.c - Program to grab raw HCI frames to send over SPI */

/*
 * Copyright (c) 2016 Linaro Limited
 * based on btusb.c
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

#include <nanokernel.h>
#include <stdio.h>

/* Default SYS LOG DEBUG LEVEL */
#define SYS_LOG_LEVEL SYS_LOG_LEVEL_ERROR
#include <misc/sys_log.h>
#include <misc/printk.h>

#include <errno.h>

#include <device.h>
#include <gpio.h>

#include <net/buf.h>
#include <bluetooth/buf.h>
#include <bluetooth/hci_raw.h>

#include <spi.h>
#ifdef CONFIG_SPI_NRF5
#include <drivers/spi/spi_nrf5.h>
#endif

#define SPI_RX_FIBER_STACK_SIZE 1024
#define SPI_TX_FIBER_STACK_SIZE 1024
static char __stack bt_spi_rx_fiber_stack[SPI_RX_FIBER_STACK_SIZE];
static char __stack bt_spi_tx_fiber_stack[SPI_TX_FIBER_STACK_SIZE];

static struct device *spi_dev;
static struct device *gpio_dev;

static struct nano_fifo rx_queue;

/* Depends on the MAX_BUF_SIZE value */
#define SPI_BUF_HEADER_SIZE	1
/* Limit SPI buffer size to 255 based on the limit required by nRF51 */
#define SPI_MAX_BUF_SIZE	255

/* Bluetooth max buffer len (between cmd, evt and acl) */
/* TODO: Find the right value by checking the kconfig options used */
#define BT_MAX_BUF_SIZE 70 /* EVT_LEN=68 */

/* HCI command buffers */
#define CMD_BUF_SIZE (CONFIG_BLUETOOTH_HCI_SEND_RESERVE + \
		      sizeof(struct bt_hci_cmd_hdr) + \
		      CONFIG_BLUETOOTH_MAX_CMD_LEN)

static struct nano_fifo avail_tx;
static NET_BUF_POOL(tx_pool, CONFIG_BLUETOOTH_HCI_CMD_COUNT, CMD_BUF_SIZE,
		    &avail_tx, NULL, sizeof(uint8_t));

#define BT_L2CAP_MTU 251
/* Data size needed for ACL buffers */
#define BT_BUF_ACL_SIZE (CONFIG_BLUETOOTH_HCI_RECV_RESERVE + \
			 sizeof(struct bt_hci_acl_hdr) + \
			 4 /* L2CAP header size */ + \
			 BT_L2CAP_MTU)

static struct nano_fifo avail_acl_tx;
static NET_BUF_POOL(acl_tx_pool, 2, BT_BUF_ACL_SIZE, &avail_acl_tx, NULL,
		    sizeof(uint8_t));

/* FIXME: This make it nRF5 specific, generalise it later */
#define SPI_OP_MODE		SPI_NRF5_OP_MODE_SLAVE
#define SPI_FRAME_SIZE		SPI_WORD(8)
#define SPI_MAX_CLK_FREQ	128

/* TODO: Move to a proper place */
/* Slave uses RDY and REQ pins to coordinate the messages with master */
#define GPIO_DRV_NAME	CONFIG_GPIO_NRF5_P0_DEV_NAME
#if defined(CONFIG_BOARD_96B_CARBON_NRF51)
#define GPIO_RDY_PIN	29
#define GPIO_REQ_PIN	28
#elif defined(CONFIG_BOARD_NRF51_PCA10028)
#define GPIO_RDY_PIN	22
#define GPIO_REQ_PIN	21
#endif
#define GPIO_RDY_DIR	GPIO_DIR_OUT
#define GPIO_RDY_PULL	GPIO_PUD_PULL_DOWN
#define GPIO_REQ_DIR	GPIO_DIR_OUT
#define GPIO_REQ_PULL	GPIO_PUD_PULL_DOWN

static struct spi_config btspi_config = {
	.config = (SPI_FRAME_SIZE | SPI_OP_MODE),
	.max_sys_freq = SPI_MAX_CLK_FREQ,
};

struct nano_sem nano_sem_rx_fiber;
struct nano_sem nano_sem_tx_fiber;

/* TODO: move to standard utils */
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

static inline int bt_spi_transceive(const void *tx_buf, uint32_t tx_buf_len,
				    void *rx_buf, uint32_t rx_buf_len)
{
	int ret = 0;

	SYS_LOG_DBG("bt_spi_transceive: /RDY set to 1 -> 0 (notify master)");
	gpio_pin_write(gpio_dev, GPIO_RDY_PIN, 1);
	gpio_pin_write(gpio_dev, GPIO_RDY_PIN, 0);
	ret = spi_transceive(spi_dev, tx_buf, tx_buf_len, rx_buf, rx_buf_len);

	return ret;
}

static inline int bt_spi_tx(struct net_buf *buf)
{
	uint8_t spi_tx_buf[SPI_MAX_BUF_SIZE];
	uint8_t spi_rx_buf[2] = { 0 };
	uint8_t remaining = SPI_MAX_BUF_SIZE;
	uint8_t *offset;
	struct net_buf *buf_extra;
	int ret = 0;

	/* Make sure the SPI buffer size is large enough */
	if (buf->len > BT_MAX_BUF_SIZE) {
		SYS_LOG_ERR("Buf larger than max buf size");
		net_buf_unref(buf);
		return -EINVAL;
	}

	hexdump("<", buf->data, buf->len);

	memset(spi_tx_buf, 0, sizeof(spi_tx_buf));

	/* Save the first package, wait for master then add possible extras */
	spi_tx_buf[0] = 1;
	spi_tx_buf[1] = (uint8_t) bt_buf_get_type(buf);
	spi_tx_buf[2] = (uint8_t) buf->len;
	offset = spi_tx_buf + 3;
	memcpy(offset, buf->data, buf->len);
	offset += buf->len;
	remaining -= buf->len + 3;
	net_buf_unref(buf);

	/* To send data we first must notify the master side with /REQ */
	SYS_LOG_DBG("setting /REQ to 1 -> 0");
	gpio_pin_write(gpio_dev, GPIO_REQ_PIN, 1);
	gpio_pin_write(gpio_dev, GPIO_REQ_PIN, 0);

	/* Wait until rx fiber says we're good to go */
	nano_fiber_sem_take(&nano_sem_tx_fiber, TICKS_UNLIMITED);
	SYS_LOG_DBG("took sem tx fiber");

	/* In case there are already more bufs in the queue, send more */
	while (remaining >= BT_MAX_BUF_SIZE + 2) {
		buf_extra = net_buf_get_timeout(&rx_queue, 0, TICKS_NONE);
		if (!buf_extra) {
			break;
		}
		if (buf_extra->len > BT_MAX_BUF_SIZE) {
			SYS_LOG_ERR("Buf larger than max buf size");
			net_buf_unref(buf_extra);
			nano_fiber_sem_give(&nano_sem_rx_fiber);
			return -EINVAL;
		}
		hexdump("<", buf_extra->data, buf_extra->len);
		spi_tx_buf[0] += 1;
		offset[0] = (uint8_t) bt_buf_get_type(buf_extra);
		offset[1] = (uint8_t) buf_extra->len;
		offset += 2;
		memcpy(offset, buf_extra->data, buf_extra->len);
		offset += buf_extra->len;
		remaining -= buf_extra->len + 2;
		net_buf_unref(buf_extra);
	}

	SYS_LOG_DBG("sending spi buf len %d", offset - spi_tx_buf);
	ret = bt_spi_transceive(spi_tx_buf, offset - spi_tx_buf,
				spi_rx_buf, sizeof(spi_rx_buf));
	if (ret < 0) {
		SYS_LOG_ERR("SPI transceive error: %d", ret);
	}

	SYS_LOG_DBG("sem give tx fiber");
	nano_fiber_sem_give(&nano_sem_rx_fiber);

	return ret;
}

/* Fiber responsible for receiving data from master */
static void bt_spi_rx_fiber(void)
{
	uint8_t spi_rx_buf[SPI_MAX_BUF_SIZE];
	uint8_t spi_tx_buf[2] = { 0 };
	uint8_t spi_buf_len;
	uint8_t bt_buf_type;
	struct net_buf *buf;
	int ret;

	SYS_LOG_DBG("Starting bt_spi_rx_fiber");

	while (1) {
		memset(&spi_rx_buf, 0, sizeof(spi_rx_buf));
		ret = bt_spi_transceive(spi_tx_buf, sizeof(spi_tx_buf),
					spi_rx_buf, sizeof(spi_rx_buf));
		if (ret != 0) {
			SYS_LOG_ERR("SPI transceive error: %d", ret);
			continue;
		}
		spi_buf_len = spi_rx_buf[0];
		SYS_LOG_DBG("Received SPI buf buf size %d", spi_buf_len);

		if (spi_buf_len == 0) {
			/* Let the tx task to do its job and wait */
			SYS_LOG_DBG("sem give tx fiber");
			nano_fiber_sem_give(&nano_sem_tx_fiber);
			SYS_LOG_DBG("sem take rx fiber, wait for tx fiber");
			nano_fiber_sem_take(&nano_sem_rx_fiber,
							TICKS_UNLIMITED);
			continue;
		}

		bt_buf_type = spi_rx_buf[1];

		switch (bt_buf_type) {
		case BT_BUF_CMD:
			SYS_LOG_DBG("BT rx buf type BUF_CMD");
			buf = net_buf_get(&avail_tx, 0);
			if (!buf) {
				SYS_LOG_ERR("Cannot get free tx buffer");
				continue;
			}
			break;
		case BT_BUF_ACL_OUT:
			SYS_LOG_DBG("BT rx buf type ACL_OUT");
			buf = net_buf_get(&avail_acl_tx, 0);
			if (!buf) {
				SYS_LOG_ERR("Cannot get free acl tx buffer");
				continue;
			}
			break;
		default:
			SYS_LOG_ERR("Unknown bt buf type %d, invalid data",
					bt_buf_type);
			continue;
		}

		memcpy(net_buf_add(buf, spi_buf_len - 2), spi_rx_buf + 2,
							spi_buf_len - 2);
		bt_buf_set_type(buf, bt_buf_type);
		SYS_LOG_DBG("Sending %p, len %d, type %d", buf, buf->len,
							bt_buf_type);
		hexdump(">", buf->data, buf->len);
		bt_send(buf);
	}
}

static void bt_spi_tx_fiber(void)
{
	struct net_buf *buf;

	while (1) {
		/* With TICKS_UNLIMITED we always get a valid buffer */
		buf = net_buf_get_timeout(&rx_queue, 0, TICKS_UNLIMITED);
		bt_spi_tx(buf);
	}
}

void main(void)
{
	int ret;

	printk("Starting Bluetooth RAW over SPI\n");

	spi_dev = device_get_binding(CONFIG_BLUETOOTH_SPI_ON_DEV_NAME);
	if (!spi_dev) {
		SYS_LOG_ERR("Cannot find device %s",
				CONFIG_BLUETOOTH_SPI_ON_DEV_NAME);
		return;
	}

	/* Initialize the SPI driver with the right configuration */
	ret = spi_configure(spi_dev, &btspi_config);
	if (ret < 0) {
		SYS_LOG_ERR("Failed to configure %s",
				CONFIG_BLUETOOTH_SPI_ON_DEV_NAME);
		return;
	}

	/* Initialize the /REQ and /RDY GPIO pins */
	gpio_dev = device_get_binding(GPIO_DRV_NAME);
	gpio_pin_configure(gpio_dev, GPIO_RDY_PIN,
				GPIO_RDY_DIR | GPIO_RDY_PULL);
	gpio_pin_configure(gpio_dev, GPIO_REQ_PIN,
				GPIO_REQ_DIR | GPIO_REQ_PULL);

	/* Initialize the buffer pools */
	net_buf_pool_init(tx_pool);
	net_buf_pool_init(acl_tx_pool);
	nano_fifo_init(&rx_queue);

	nano_sem_init(&nano_sem_rx_fiber);
	nano_sem_init(&nano_sem_tx_fiber);

	/* Tx/Rx fibers */
	fiber_start(bt_spi_rx_fiber_stack, sizeof(bt_spi_rx_fiber_stack),
			(nano_fiber_entry_t) bt_spi_rx_fiber, 0, 0, 7, 0);
	fiber_start(bt_spi_tx_fiber_stack, sizeof(bt_spi_tx_fiber_stack),
			(nano_fiber_entry_t) bt_spi_tx_fiber, 0, 0, 7, 0);

	bt_enable_raw(&rx_queue);

	printk("Bluetooth RAW driver enabled\n");
}
