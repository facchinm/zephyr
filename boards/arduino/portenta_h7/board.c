/*
 * Copyright (c) 2022 Benjamin Björnsson <benjamin.bjornsson@gmail.com>.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

static int board_init(void)
{

	/* Set led1 inactive since the Arduino bootloader leaves it active */
	const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

	if (!gpio_is_ready_dt(&led1)) {
		return -ENODEV;
	}

	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);

#define PMIC_I2C DT_NODELABEL(i2c1)

#if DT_NODE_HAS_STATUS(PMIC_I2C, okay)
	const struct device *const i2c_dev = DEVICE_DT_GET(PMIC_I2C);
	if (!device_is_ready(i2c_dev)) {
		return -ENODEV;
	}
	uint32_t i2c_cfg = I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER;
	i2c_configure(i2c_dev, i2c_cfg);

	i2c_reg_write_byte(i2c_dev, 8 << 1, 0x52, 0x9);
	i2c_reg_write_byte(i2c_dev, 8 << 1, 0x53, 0xF);
	i2c_reg_write_byte(i2c_dev, 8 << 1, 0x3B, 0xF);
	i2c_reg_write_byte(i2c_dev, 8 << 1, 0x35, 0xF);
#endif

	return 0;
}

SYS_INIT(board_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
