/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
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
#include <device.h>
#include <gpio.h>

#if defined(CONFIG_BOARD_96B_CARBON)
#define PORT_LED1	"GPIOD"
#define PORT_LED2	"GPIOB"
#define LED1 2
#define LED2 5
#else
/* default assumes nucleo_f103rb board */
#define PORT_LED1	"GPIOB"
#define PORT_LED2	"GPIOB"
#define LED1 5
#define LED2 8
#endif

void main(void)
{
	int cnt = 0;
	struct device *gpio_dev_1;
	struct device *gpio_dev_2;

	gpio_dev_1 = device_get_binding(PORT_LED1);
	gpio_dev_2 = device_get_binding(PORT_LED2);

	gpio_pin_configure(gpio_dev_1, LED1, GPIO_DIR_OUT);
	gpio_pin_configure(gpio_dev_2, LED2, GPIO_DIR_OUT);

	while (1) {
		gpio_pin_write(gpio_dev_1, LED1, cnt % 2);
		gpio_pin_write(gpio_dev_2, LED2, (cnt + 1) % 2);
		task_sleep(SECONDS(1));
		cnt++;
	}
}
