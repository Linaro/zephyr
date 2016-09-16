/* spi.c - SPI test source file */

/*
 * Copyright (c) 2015 Intel Corporation.
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
#include <zephyr.h>

#define SYS_LOG_LEVEL SYS_LOG_SPI_LEVEL
#include <misc/sys_log.h>
#include <misc/printk.h>
#include <sys_clock.h>
#include <string.h>
#include <spi.h>

#define DRV_NAME				CONFIG_SPI_0_NAME
#define SET_FRAME_SIZE				SPI_WORD(8)
#define SPI_MAX_CLK_FREQ_250KHZ			128
#define SPI_SLAVE				0

#if defined(CONFIG_SPI_SLAVE)
	char *msg = "SPI slave transfers";
	#define MARK				(0x5A)

	#if defined(CONFIG_SPI_STM32)
		#include <spi/spi_stm32.h>
		#define MODE			SPI_STM32_SLAVE_MODE
		#define CONFIGS			SPI_STM32_SLAVE_HW_NO_OUTPUT
	#elif defined(CONFIG_SPI_NRF5)
		#include <spi/spi_nrf5.h>
		#define MODE			SPI_NRF5_OP_MODE_SLAVE
		#define CONFIGS			0
	#else
		#error test not supported
	#endif
#else
	char *msg = "SPI master transfers";
	#define	MARK				(0xA5)

	#if defined(CONFIG_SPI_STM32)
		#include <spi/spi_stm32.h>
		#define MODE			SPI_STM32_MASTER_MODE
		#define CONFIGS			SPI_STM32_SLAVE_HW_SS_OUTPUT
	#else
		#error	test not supported
	#endif
#endif

#define BUF_LEN		255
unsigned char rbuf[BUF_LEN];
unsigned char wbuf[BUF_LEN];

#define MAX_LOOP	1000000
#define str1	"\b"
#define str2	"\b\b"
#define str3	"\b\b\b"
#define str4	"\b\b\b\b"
#define str5	"\b\b\b\b\b"
#define str6	"\b\b\b\b\b\b"
static void report_progress(int i)
{
	char *p;

	if (i < 10) {
		p = str1;
	} else if (i < 100) {
		p = str2;
	} else if (i < 1000) {
		p = str3;
	} else if (i < 10000) {
		p = str4;
	} else if (i < 100000) {
		p = str5;
	} else if (i < 1000000) {
		p = str6;
	} else {
		/* ignore */
		return;
	}

	/* TODO: fix printk implementation to accept the format %09d */
	printk("%d%s", i, p);
}

static int check_transfer(unsigned char *p, unsigned char *q, int len)
{
	int i;

	for (i = 0; i < len; i++, p++, q++) {
		if ((*p & 0x000000FF) != (~*q & 0x000000FF)) {
			printk("expected 0x%x, received 0x%x\n"
			       "transfer size %d bytes\n"
			       "error on byte %d\n", *q, *p, len, i);
			return -1;
		}
	}

	return 0;
}

struct spi_config spi_conf = {
	.config = MODE | CONFIGS | SET_FRAME_SIZE,
	.max_sys_freq = SPI_MAX_CLK_FREQ_250KHZ,
};

void main(void)
{
	struct device *spi;
	int len = BUF_LEN;
	int i, j, k;
	int ret;

	printk("SPI Test Application\n\n %s: ", msg);

	spi = device_get_binding(DRV_NAME);
	if (!spi) {
		SYS_LOG_ERR("cannot find device %s", DRV_NAME);
	}

	spi_configure(spi, &spi_conf);
	spi_slave_select(spi, SPI_SLAVE);

	wbuf[0] = MARK;
	for (i = 1; i < BUF_LEN; i++) {
		wbuf[i] = ~wbuf[i-1];
	}

	for (j = 1, k = 0; j++, len--) {

		if (len == 0)
			len = BUF_LEN;

		ret = spi_transceive(spi, wbuf, len, rbuf, len);
		if (ret  < 0) {
			SYS_LOG_ERR("spi_transcieve error (%i)", ret);
		}

		ret = check_transfer(rbuf, wbuf, len);
		if (ret && j > 1) {
			printk("failed after %d transfers\n", j + k * MAX_LOOP);
			break;
		}
#ifndef CONFIG_SYS_LOG
		else {
			if (j >= MAX_LOOP) {
				printk("\n %s: ", msg);

				/* start again (j) and keep count (k) */
				j = 1;
				k++;
			}
			report_progress(j);
		}
#endif
	}

	printk("done\n");
}
