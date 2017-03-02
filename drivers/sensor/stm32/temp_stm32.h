/*
 * Copyright (c) 2017 Linaro Limited.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _STM32_TEMP_H_
#define _STM32_TEMP_H_

#include <stdint.h>

#include <device.h>
#include <soc.h>

struct temp_stm32_config {
	ADC_TypeDef *adc;
	ADC_Common_TypeDef *adc_common;
	uint8_t adc_channel;
#if CONFIG_SOC_SERIES_STM32F4X
	struct stm32f4x_pclken pclken;
#endif
};

struct temp_stm32_data {
	struct device *clock;
};

#endif	/* _STM32_TEMP_H_ */
