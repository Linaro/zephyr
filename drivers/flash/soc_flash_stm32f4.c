/*
 * Copyright (c) 2016 Linaro Limited
 *               2016 Intel Corporation
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

#include <misc/sys_log.h>
#include <nanokernel.h>
#include <device.h>
#include <string.h>
#include <flash.h>
#include <errno.h>
#include <init.h>
#include <soc.h>

#include <flash_registers.h>

static const struct flash_map {
      uint32_t start;
      uint32_t len;
      uint16_t id;
} sector [] = {
	/* TODO: investigate why sector 0 is not accessible */
	{ .start = 0x08000000 , .len = KB(16),   .id = 0x0000 },
	{ .start = 0x08004000 , .len = KB(16),   .id = 0x0008 },
	{ .start = 0x08008000 , .len = KB(16),   .id = 0x0010 },
	{ .start = 0x0800c000 , .len = KB(16),   .id = 0x0018 },
	{ .start = 0x08010000 , .len = KB(64),   .id = 0x0020 },
	{ .start = 0x08020000 , .len = KB(128),  .id = 0x0028 },
	{ .start = 0x08040000 , .len = KB(128),  .id = 0x0030 },
	{ .start = 0x08060000 , .len = KB(128),  .id = 0x0038 },
};

#define MAX_OFFSET  (sector[ARRAY_SIZE(sector) - 1].len + \
	sector[ARRAY_SIZE(sector) - 1].start  - sector[0].start)

struct flash_priv {
	struct stm32f4x_flash *regs;
	struct nano_sem sem;
};

static int check_status(struct stm32f4x_flash *regs)
{
	uint32_t error = 0;

	/* TODO:  enable FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR */
	error = FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_RDERR |
		FLASH_FLAG_OPERR;

	if (regs->status & error) {
		SYS_LOG_ERR("error: 0x%x, status 0x%x", error, regs->status);
		return -1;
	}

	return 0;
}

static int get_sector(off_t offset)
{
	uint32_t addr = sector[0].start + offset;
	int i;

	for (i = 0; i < ARRAY_SIZE(sector) ; i++) {
		if (addr > sector[i].start) {
			continue;
		} else if (addr < sector[i].start) {
			return (i - 1);
		} else {
			return i;
		}
	}

	return (i - 1);
}

static int wait_flash_idle(struct stm32f4x_flash *regs)
{
	int rc;

	rc = check_status(regs);
	if (rc < 0) {
		return -1;
	}

	while (regs->status & FLASH_FLAG_BSY) {
		continue;
	}

	return 0;
}

static inline void program_byte(uint8_t *p, uint8_t val,
	struct stm32f4x_flash *regs)
{
	uint32_t tmp;

	regs->ctrl &= CR_PSIZE_MASK;
	regs->ctrl |= FLASH_PSIZE_BYTE;
	regs->ctrl |= FLASH_CR_PG;

	/* flush the register write */
	tmp = regs->ctrl;

	*p = val;

	while (regs->status & FLASH_FLAG_BSY) {
		continue;
	}

	regs->ctrl &= (~FLASH_CR_PG);
}

static int program_flash(uint32_t address, uint8_t data, struct flash_priv *p)
{
	int rc;

	nano_sem_take(&p->sem, TICKS_UNLIMITED);
	rc = wait_flash_idle(p->regs);
	if (rc < 0) {
		nano_sem_give(&p->sem);
		return -1;
	}

	program_byte((uint8_t *) address, data, p->regs);

	rc = wait_flash_idle(p->regs);
	nano_sem_give(&p->sem);

	return rc;
}

static int erase_sector(uint16_t sector, struct stm32f4x_flash *regs)
{
	uint32_t tmp;
	int rc = 0;

	rc = wait_flash_idle(regs);
	if (rc < 0) {
		SYS_LOG_ERR("erasing sector");
		return -1;
	}

	regs->ctrl &= CR_PSIZE_MASK;
	regs->ctrl |= FLASH_PSIZE_BYTE;
	regs->ctrl &= SECTOR_MASK;
	regs->ctrl |= FLASH_CR_SER | sector;

	/* flush the register write */
	tmp = regs->ctrl;
	regs->ctrl |= FLASH_CR_STRT;

	while (regs->status & FLASH_FLAG_BSY) {
		continue;
	}
	rc = wait_flash_idle(regs);
	if (rc < 0) {
		SYS_LOG_ERR("erasing sector");
		return -1;
	}

	return 0;
}

static int flash_stm32f_erase(struct device *dev, off_t offset, size_t len)
{
	struct flash_priv *p = dev->driver_data;
	int start, end, i;
	int rc = 0;

	if (offset < 0 || offset + len > MAX_OFFSET) {
		return -1;
	}

	end = get_sector(offset + len - 1);
	start = get_sector(offset);

	for (i = start; i <= end ; i++ ) {
		rc = erase_sector(sector[i].id, p->regs);
		if (rc < 0) {
			return -1;
		}
	}

	return rc;
}

static int flash_stm32f_read(struct device *dev, off_t offset,
				void *data, size_t len)
{
	uint32_t addr = sector[0].start + offset;

	if (offset < 0 || offset + len > MAX_OFFSET) {
		return -1;
	}

	memcpy(data, (void *) addr, len);

	return 0;
}

static int flash_stm32f_write(struct device *dev, off_t offset,
				const void *data, size_t len)
{
	uint32_t addr = sector[0].start + offset;
	struct flash_priv *p = dev->driver_data;
	const uint8_t *sptr = data;
	int rc, i;

	if (offset < 0 || offset + len > MAX_OFFSET) {
		return -1;
	}

	p->regs->status = FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR
		| FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR;

	for (i = 0; i < len / sizeof(*sptr); i++) {
		rc = program_flash(addr, sptr[i], p);
		if (rc < 0) {
			return -1;
		}

		addr += sizeof(*sptr);
	}

	return 0;
}

static int flash_stm32f_write_protection(struct device *dev, bool enable)
{
	return 0;
}

static struct flash_priv flash_data = {
	.regs = (struct stm32f4x_flash *) FLASH_R_BASE,
};

static struct flash_driver_api flash_stm32f4_api = {
	.write_protection = flash_stm32f_write_protection,
	.erase = flash_stm32f_erase,
	.write = flash_stm32f_write,
	.read = flash_stm32f_read,
};

static int stm32f4_flash_init(struct device *dev)
{
	struct flash_priv *p = dev->driver_data;

	nano_sem_init(&p->sem);
	nano_sem_give(&p->sem);

	if (p->regs->ctrl & FLASH_CR_LOCK) {
		p->regs->key = FLASH_KEY1;
		p->regs->key = FLASH_KEY2;
	}

	return 0;
}

DEVICE_AND_API_INIT(stm32f4x_flash, CONFIG_SOC_FLASH_STM32F4_DEV_NAME,
		    stm32f4_flash_init, &flash_data, NULL, SECONDARY,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &flash_stm32f4_api);


