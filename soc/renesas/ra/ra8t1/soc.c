/*
 * Copyright (c) 2024 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for Renesas RA8T1 family processor
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <cmsis_core.h>
#include <zephyr/arch/arm/nmi.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>

LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);

#include <bsp_api.h>

/* Enable Instrcution cache, Branch prediction, Loop and branch info cache */
#define CCR_CACHE_ENABLE (SCB_CCR_IC_Msk | SCB_CCR_BP_Msk | SCB_CCR_LOB_Msk)

uint32_t SystemCoreClock BSP_SECTION_EARLY_INIT;

volatile uint32_t g_protect_pfswe_counter BSP_SECTION_EARLY_INIT;

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * This needs to be run from the very beginning.
 * So the init priority has to be 0 (zero).
 *
 * @return 0
 */
static int renesas_ra8t1_init(void)
{
	SystemCoreClock = BSP_MOCO_HZ;
	g_protect_pfswe_counter = 0;

	SCB->CCR |= (uint32_t) CCR_CACHE_ENABLE;
	barrier_dsync_fence_full();
	barrier_isync_fence_full();

	return 0;
}

SYS_INIT(renesas_ra8t1_init, PRE_KERNEL_1, 0);
