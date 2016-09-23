/*
 * Copyright (c) 2016 Linaro Limited
 *               2016 Intel Corporation.
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
#include <flash.h>
#include <device.h>
#include <string.h>
#if defined(CONFIG_STDOUT_CONSOLE)
	#include <stdio.h>
	#define PRINT           printf
#else
	#include <misc/printk.h>
	#define PRINT           printk
#endif

static const struct flash_map {
	uint32_t start;
	uint32_t len;
} sector [] = {
#if 0
	/* enable when the driver can access the first sector without
	 * locking up
	 */

	{ .start = 0x0000000 , .len = KB(16) },
	{ .start = 0x0004000 , .len = KB(16) },
	{ .start = 0x0008000 , .len = KB(16) },
	{ .start = 0x000c000 , .len = KB(16) },
	{ .start = 0x0010000 , .len = KB(64) },
	{ .start = 0x0020000 , .len = KB(128) },
	{ .start = 0x0040000 , .len = KB(128) },
	{ .start = 0x0060000 , .len = KB(128) },
#else
	{ .start = 0x0000000 , .len = KB(16) },
	{ .start = 0x0004000 , .len = KB(16) },
	{ .start = 0x0008000 , .len = KB(16) },
	{ .start = 0x000c000 , .len = KB(64) },
	{ .start = 0x001c000 , .len = KB(128) },
	{ .start = 0x003c000 , .len = KB(128) },
	{ .start = 0x005c000 , .len = KB(128) },
#endif

};

static const  uint8_t val = 0xA5;
uint8_t buffer[KB(64)];

static char *STR(uint32_t len)
{
	if (len == KB(16)) {
		return "  16KB";
	} else if (len == KB(64)) {
		return "  64KB";
	} else if (len == KB(128)) {
		return " 128KB";
	} else if (len == 0) {
		return "   0KB";
	} else {
		return "    ";
	}
}

static void test_access(struct device *flash_dev, int i)
{
	uint32_t len = sector[i].len, offset = 0;
	uint8_t *p;
	int j, k;
	off_t q;

	/* handle the 128KB sectors (not enough memory to do a single pass) */
	if (sector[i].len > sizeof(buffer)) {
		offset = sector[i].len - sizeof(buffer);
		len = sizeof(buffer);
	}

	for (k = 0; k < (offset ? 2: 1) ; k++) {

		/* fill pattern */
		buffer[0] = val;
		for (j = 1; j < ARRAY_SIZE(buffer); j++ ) {
			buffer[j] = 0x000000FF & ~buffer[j-1];
		}

		p = (uint8_t *) buffer;
		q = sector[i].start + k * offset;

		if (flash_write(flash_dev, q , p, len) != 0) {
			PRINT("   Flash write failed sector %d\n", i);
		}

		/* clear buffer for reading */
		memset(buffer, 0x00, sizeof(buffer));

		if (flash_read(flash_dev, q, p, len) != 0) {
			PRINT("   Flash read failed sector %d\n", i);
		}

		PRINT("   sector %d %s ...%s", i,
			STR(k * offset), STR(len + k * len));

		/* check against pattern */
		for (j = 0; j < len; j++) {
			if (buffer[j] != (0x000000FF & ((j % 2) ? ~val: val))) {
				break;
			}
		}

		if (j < len ) {
			PRINT(" [KO]\n");
			PRINT("   Error sector %i\n"
			      "   sector start 0x%x, len 0x%x\n"
			      "   error on byte %d: val 0x%x expected 0x%x\n",
			      i, sector[i].start + k * offset, sector[i].len,
			      j, buffer[j], ((j % 2) ? ~val: val));
		} else {

			PRINT(" [OK]\n");
		}
	}
}

void main(void)
{
	struct device *flash_dev;
	int rc, i;

	flash_dev = device_get_binding("STM32F4_FLASH");

	if (!flash_dev) {
		PRINT("STM32F4 flash driver was not found!\n");
		return;
	}
	PRINT("\nTest start:\n");

	PRINT("\n - Erasing sectors \n");
	for (i = 0; i < ARRAY_SIZE(sector); i++ ) {

		rc = flash_erase(flash_dev, sector[i].start, sector[i].len);
		if (rc < 0) {
			PRINT("   sector %d %s [FAIL]\n", i, STR(sector[i].len));
		} else {
			PRINT("   sector %d %s [OK]\n", i, STR(sector[i].len));
		}
	}

	PRINT("\n - Checking all bytes in sectors (read/write)\n");
	for (i = 0; i < ARRAY_SIZE(sector); i++ ) {
		test_access(flash_dev, i);
	}

	PRINT("\nTest done\n");
}
