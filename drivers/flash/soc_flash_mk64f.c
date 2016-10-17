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

#include "fsl_common.h"
#include "fsl_flash.h"

struct flash_priv {
	flash_config_t config;
};

static int flash_k64f_erase(struct device *dev, off_t offset, size_t len)
{
	struct flash_priv *priv = dev->driver_data;
	uint32_t addr;
	status_t rc;
	int key;

	addr = offset + priv->config.PFlashBlockBase;

	key = irq_lock();
	rc = FLASH_Erase(&priv->config, addr, len, kFLASH_apiEraseKey);
	irq_unlock(key);

	return (rc == kStatus_Success) ? 0 : -1;
}

static int flash_k64f_read(struct device *dev, off_t offset,
			       void *data, size_t len)
{
	struct flash_priv *priv = dev->driver_data;
	uint32_t addr;

	addr = offset + priv->config.PFlashBlockBase;
	memcpy(data, (void *) addr, len);

	return 0;
}

static int flash_k64f_write(struct device *dev, off_t offset,
				const void *data, size_t len)
{
	struct flash_priv *priv = dev->driver_data;
	uint32_t addr;
	status_t rc;
	int key;

	addr = offset + priv->config.PFlashBlockBase;

	if (offset % sizeof(uint32_t)) {
		return -1;
	}

	key = irq_lock();
	rc = FLASH_Program(&priv->config, addr, (uint32_t *) data, len);
	irq_unlock(key);

	return (rc == kStatus_Success) ? 0 : -1;
}

static int flash_k64f_write_protection(struct device *dev, bool enable)
{
	return 0;
}

static struct flash_priv flash_data;

static struct flash_driver_api flash_k64f_api = {
	.write_protection = flash_k64f_write_protection,
	.erase = flash_k64f_erase,
	.write = flash_k64f_write,
	.read = flash_k64f_read,
};

static int flash_k64f_init(struct device *dev)
{
	struct flash_priv *p = dev->driver_data;
	status_t rc;

	rc = FLASH_Init(&p->config);
	return (rc == kStatus_Success) ? 0 : -1;
}

DEVICE_AND_API_INIT(flash_k64f, CONFIG_SOC_FLASH_MK64F12_DEV_NAME,
		    flash_k64f_init, &flash_data, NULL, SECONDARY,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &flash_k64f_api);
