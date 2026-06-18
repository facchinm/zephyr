# SPDX-License-Identifier: Apache-2.0

if(CONFIG_WIFI_ST67W611M1)

  zephyr_library_include_directories(
    ${ZEPHYR_BASE}/drivers/modem # Allow including AT driver without ".."
  )

  zephyr_library_sources(
    st67w611m1_drv.c
    st67w611m1_spi.c
  )
endif()
