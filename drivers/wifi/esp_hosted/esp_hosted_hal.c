/*
 * Copyright (c) 2025 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>

#include "esp_hosted_wifi.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(esp_hosted_hal, CONFIG_WIFI_LOG_LEVEL);

int esp_hosted_hal_init(const struct device *dev) {
	const esp_hosted_config_t *config = dev->config;

    // Configure pins.
    gpio_pin_configure_dt(&config->cs_gpio, GPIO_OUTPUT);
    gpio_pin_configure_dt(&config->dataready_gpio, GPIO_OUTPUT);
    gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT);

    // Perform a hard-reset
    gpio_pin_set_dt(&config->cs_gpio, 1);
    gpio_pin_set_dt(&config->dataready_gpio, 1);
    gpio_pin_set_dt(&config->reset_gpio, 0);
    k_msleep(100);
    gpio_pin_set_dt(&config->reset_gpio, 1);
    k_msleep(500);

    // Configure handshake/dataready pins.
    gpio_pin_configure_dt(&config->dataready_gpio, GPIO_INPUT);
    gpio_pin_configure_dt(&config->handshake_gpio, GPIO_INPUT);

	if (!spi_is_ready_dt(&config->spi_bus)) {
        LOG_ERR("SPI device is not ready");
		return -ENODEV;
	}

    return 0;
}

void esp_hosted_hal_irq_enable(bool enable) {

}

bool esp_hosted_hal_data_ready(const struct device *dev) {
	const esp_hosted_config_t *config = dev->config;

    return gpio_pin_get_dt(&config->dataready_gpio);
}

int esp_hosted_hal_spi_transfer(const struct device *dev, uint8_t *tx, uint8_t *rx, uint32_t size) {
	const esp_hosted_config_t *config = dev->config;

	const struct spi_buf tx_buf = { .buf = tx ? tx : rx, .len = tx ? size : 0 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	const struct spi_buf rx_buf = { .buf = rx ? rx : tx, .len = rx ? size : 0 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    // Wait for handshake pin to go high.
    for (uint64_t start = k_uptime_get(); ;k_msleep(1)) {
        if (gpio_pin_get_dt(&config->handshake_gpio) &&
           (rx == NULL || gpio_pin_get_dt(&config->dataready_gpio))) {
            break;
        }
        int64_t delta = k_uptime_get() - start; //k_uptime_delta(&start);
        if (delta >= 1000) {
            LOG_ERR("timeout waiting for handshake %" PRId64, delta);
            return -ETIMEDOUT;
        }
    }

    // Transfer SPI buffers.
    gpio_pin_set_dt(&config->cs_gpio, 0);
    k_usleep(10);
	if (spi_transceive_dt(&config->spi_bus, tx ? &tx_set : NULL, rx ? &rx_set : NULL)) {
		LOG_ERR("spi_transceive failed");
		return -EIO;
	}
    gpio_pin_set_dt(&config->cs_gpio, 1);
    k_usleep(100);

    if (gpio_pin_get_dt(&config->dataready_gpio)) {
        // mod_network_poll_events();
    }
    return 0;
}

void *esp_hosted_hal_alloc(void *user, size_t size) {
    (void)user;
    return k_malloc(size);
}

void esp_hosted_hal_free(void *user, void *ptr) {
    (void)user;
    k_free(ptr);
}

void *esp_hosted_hal_calloc(size_t nmemb, size_t size) {
    return NULL;
}

void *esp_hosted_hal_realloc(void *ptr, size_t size) {
    return NULL;
}
