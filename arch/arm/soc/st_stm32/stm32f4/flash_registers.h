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
 */

#ifndef _STM32F4X_FLASH_REGISTERS_H_
#define _STM32F4X_FLASH_REGISTERS_H_

/*******************  Bits definition for FLASH_SR register  ******************/
#define FLASH_SR_EOP                         ((uint32_t)0x00000001)
#define FLASH_SR_SOP                         ((uint32_t)0x00000002)
#define FLASH_SR_WRPERR                      ((uint32_t)0x00000010)
#define FLASH_SR_PGAERR                      ((uint32_t)0x00000020)
#define FLASH_SR_PGPERR                      ((uint32_t)0x00000040)
#define FLASH_SR_PGSERR                      ((uint32_t)0x00000080)
#define FLASH_SR_BSY                         ((uint32_t)0x00010000)

/*******************  Bits definition for FLASH_CR register  ******************/
#define FLASH_CR_PG                          ((uint32_t)0x00000001)
#define FLASH_CR_SER                         ((uint32_t)0x00000002)
#define FLASH_CR_MER                         ((uint32_t)0x00000004)
#define FLASH_CR_SNB                         ((uint32_t)0x000000F8)
#define FLASH_CR_SNB_0                       ((uint32_t)0x00000008)
#define FLASH_CR_SNB_1                       ((uint32_t)0x00000010)
#define FLASH_CR_SNB_2                       ((uint32_t)0x00000020)
#define FLASH_CR_SNB_3                       ((uint32_t)0x00000040)
#define FLASH_CR_SNB_4                       ((uint32_t)0x00000080)
#define FLASH_CR_PSIZE                       ((uint32_t)0x00000300)
#define FLASH_CR_PSIZE_0                     ((uint32_t)0x00000100)
#define FLASH_CR_PSIZE_1                     ((uint32_t)0x00000200)
#define FLASH_CR_STRT                        ((uint32_t)0x00010000)
#define FLASH_CR_EOPIE                       ((uint32_t)0x01000000)
#define FLASH_CR_LOCK                        ((uint32_t)0x80000000)

/***/
#define FLASH_FLAG_EOP				FLASH_SR_EOP
#define FLASH_FLAG_OPERR			FLASH_SR_SOP
#define FLASH_FLAG_WRPERR			FLASH_SR_WRPERR
#define FLASH_FLAG_PGAERR			FLASH_SR_PGAERR
#define FLASH_FLAG_PGPERR			FLASH_SR_PGPERR
#define FLASH_FLAG_PGSERR			FLASH_SR_PGSERR
#define FLASH_FLAG_RDERR			((uint32_t)0x00000100)
#define FLASH_FLAG_BSY				FLASH_SR_BSY

#define FLASH_PSIZE_BYTE			((uint32_t)0x00000000)
#define FLASH_PSIZE_WORD			((uint32_t)0x00000200)
#define CR_PSIZE_MASK				((uint32_t)0xFFFFFCFF)

#define FLASH_KEY1				((uint32_t)0x45670123)
#define FLASH_KEY2				((uint32_t)0xCDEF89AB)
#define SECTOR_MASK				((uint32_t)0xFFFFFF07)

/**
 * @brief
 *
 * Based on reference manual:
 *
 * Chapter 3.4: Embedded Flash Memory
 */

enum {
	STM32F4X_FLASH_LATENCY_0 = 0x0,
	STM32F4X_FLASH_LATENCY_1 = 0x1,
	STM32F4X_FLASH_LATENCY_2 = 0x2,
	STM32F4X_FLASH_LATENCY_3 = 0x3,
	STM32F4X_FLASH_LATENCY_4 = 0x4,
	STM32F4X_FLASH_LATENCY_5 = 0x5,
};

union __flash_acr {
	uint32_t val;
	struct {
		uint32_t latency :4 __packed;
		uint32_t rsvd__4_7 :4 __packed;
		uint32_t prften :1 __packed;
		uint32_t icen :1 __packed;
		uint32_t dcen :1 __packed;
		uint32_t icrst :1 __packed;
		uint32_t dcrst :1 __packed;
		uint32_t rsvd__13_31 :19 __packed;
	} bit;
};

/* 3.8.7 Embedded flash registers */
struct stm32f4x_flash {
	union __flash_acr acr;
	uint32_t key;
	uint32_t optkey;
	volatile uint32_t status;
	volatile uint32_t ctrl;
	uint32_t optctrl;
};

/**
 * @brief setup embedded flash controller
 *
 * Configure flash access time latency depending on SYSCLK.
 */
static inline void __setup_flash(void)
{
	volatile struct stm32f4x_flash *regs;
	uint32_t tmpreg = 0;

	regs = (struct stm32f4x_flash *) FLASH_R_BASE;

	if (regs->ctrl & FLASH_CR_LOCK) {
		regs->key = FLASH_KEY1;
		regs->key = FLASH_KEY2;
	}

	if (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC <= 30000000) {
		regs->acr.bit.latency = STM32F4X_FLASH_LATENCY_0;
	} else if (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC <= 60000000) {
		regs->acr.bit.latency = STM32F4X_FLASH_LATENCY_1;
	} else if (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC <= 84000000) {
		regs->acr.bit.latency = STM32F4X_FLASH_LATENCY_2;
	}

	/* Make sure latency was set */
	tmpreg = regs->acr.bit.latency;
}

#endif	/* _STM32F4X_FLASHREGISTERS_H_ */
