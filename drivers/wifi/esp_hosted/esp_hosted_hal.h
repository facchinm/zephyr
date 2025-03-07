/*
 * Copyright (c) 2025 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MICROPY_INCLUDED_DRIVERS_ESP_HOSTED_HAL_H
#define MICROPY_INCLUDED_DRIVERS_ESP_HOSTED_HAL_H
typedef enum {
    ESP_HOSTED_MODE_BT,
    ESP_HOSTED_MODE_WIFI,
} esp_hosted_mode_t;

// HAL functions.
int esp_hosted_hal_init(const struct device *dev);
void esp_hosted_hal_irq_enable(bool enable);
bool esp_hosted_hal_data_ready(const struct device *dev);
int esp_hosted_hal_spi_transfer(const struct device *dev, uint8_t *tx, uint8_t *rx, uint32_t size);

// Memory management functions.
// Note alloc/free need to match the Protobuf allocator signature.
void *esp_hosted_hal_alloc(void *user, size_t size);
void esp_hosted_hal_free(void *user, void *ptr);
void *esp_hosted_hal_calloc(size_t nmemb, size_t size);
void *esp_hosted_hal_realloc(void *ptr, size_t size);
#endif // MICROPY_INCLUDED_DRIVERS_ESP_HOSTED_HAL_H
