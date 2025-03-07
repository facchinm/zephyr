# Copyright (c) 2025 Arduino SA
# SPDX-License-Identifier: Apache-2.0

if (CONFIG_WIFI_ESP_HOSTED)
zephyr_library_sources(
  esp_hosted_hal.c
  esp_hosted_pb.c
  esp_hosted_wifi.c
  protobuf-c.c
)
zephyr_include_directories(./)
endif()
