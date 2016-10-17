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
#include <stdio.h>

#define FLASH_SIZE	KB(1024)
#define SECTOR_LEN	KB(4)
#define MAX_UINT8 	(0xFF)

#define NBR_SECTORS	FLASH_SIZE/SECTOR_LEN

static struct flash_map {
	uint32_t start;
} sector[NBR_SECTORS];

#define BUFFER_LEN	(4 * SECTOR_LEN)
#define BUFFERS_IN(x)	((x)/BUFFER_LEN)

static struct  {
	uint8_t firmware[BUFFER_LEN];
	uint8_t write[BUFFER_LEN];
	uint8_t read[BUFFER_LEN];

} buffer;

static char *sstrstr(char *haystack, char *needle, size_t length)
{
	size_t needle_length = strlen(needle);
	size_t i;

	for (i = 0; i < length; i++) {
		if (i + needle_length > length) {
			return NULL;
		}

		if (strncmp(&haystack[i], needle, needle_length) == 0) {
			return &haystack[i];
		}
	}
	return NULL;
}

/*
 * The size of the test in flash must be < 16KB (ie 4 sectors)
 */
void main(void)
{
	const char progress_mark[] = "/-\\|";
	char msg[] = "MK64F Flash Test";
	struct device *flash_dev;
	int progress, rc, i, j;
	char c;

	printf("\n%s\n", msg);

	for (i = 0; i < NBR_SECTORS; i++) {
		sector[i].start = i * SECTOR_LEN;
	}

	flash_dev = device_get_binding("MK64F_FLASH");
	if (!flash_dev) {
		printf("MK64F flash driver was not found!\n");
		return;
	}

	printf("- Looking for firmware\t\t: ");
	memset(buffer.firmware, 0x00, sizeof(buffer.firmware));

	if (flash_read(flash_dev,
		(off_t) 0x00,
		(uint8_t *) buffer.firmware, sizeof(buffer.firmware))) {
		printf(" Flash read failed [ERROR]\n");
	} else {
		if (sstrstr(buffer.firmware, msg, sizeof(buffer.firmware))) {
			printf("[FOUND]\n");
		} else {
			printf("[error: firmware NOT FOUND]\n");
			return;
		}
	}

	printf("- Flash erase (16KB --> 1MB)\t: ");
	if (flash_erase(flash_dev, (off_t) sector[4].start, KB(1024 - 16)) < 0) {
		printf("[FAILED]\n");
	}
	else {
		printf("[PASSED]\n");
	}

	printf("- Flash check (16KB --> 1MB)\t: ");

	/* set write buffer with pattern */
	for (j = 0; j < sizeof(buffer.write); j++) {
		buffer.write[j] = j % MAX_UINT8;
	}

	progress = 0;
	for ( i = 0; i < BUFFERS_IN(KB(1024 - 16)) ; i++) {

		rc = flash_write(flash_dev,
			(off_t) sector[4 + i * BUFFER_LEN/SECTOR_LEN].start,
			buffer.write, sizeof(buffer.write));
		if (rc < 0) {
			printf("flash write failed\n");
		}

		memset(buffer.read, 0x00, sizeof(buffer.read));
		rc = flash_read(flash_dev,
			(off_t) sector[4 + i * BUFFER_LEN/SECTOR_LEN].start,
			buffer.read, sizeof(buffer.read));
		if (rc < 0) {
			printf("flash read failed\n");
		}

		c = progress_mark[progress];
		printf("%c\b",c);
		progress = (progress + 1) % (sizeof(progress_mark) - 1);

		if (memcmp(buffer.read, buffer.write, sizeof(buffer.read))) {
			printf("[FAILED]\n");
			goto done;
		}
	}
	printf("[PASSED]\n");
done:
	printf("- Test done.\n");

}
