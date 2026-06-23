#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>

/* Magic number to indicate a double-reset */
#define BOOT_MAGIC_NUMBER 0xDEADBEEF

/* * STM32H5 System Memory (ROM Bootloader) Address.
 */
#define STM32H5_ROM_BOOTLOADER_ADDR 0x0BF97000

/* Get the base address of the backup SRAM from Devicetree */
#define BACKUP_SRAM_NODE DT_INST(0, st_stm32_backup_sram)
#define BACKUP_SRAM_ADDR DT_REG_ADDR(BACKUP_SRAM_NODE)

/**
 * @brief Standard ARM Cortex-M routine to jump to a ROM Bootloader
 */
static void jump_to_rom_bootloader(void)
{
	void (*bootloader_jump)(void);
	uint32_t jump_addr = *(__IO uint32_t *)(STM32H5_ROM_BOOTLOADER_ADDR + 4);

	bootloader_jump = (void (*)(void))jump_addr;

	/* 1. Disable all interrupts */
	irq_lock();

	/* 2. Disable SysTick */
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;

	/* Note: If the ROM bootloader fails to enumerate via USB, you may also
	 * need to reset the clock tree here (e.g., calling HAL_RCC_DeInit())
	 * so the bootloader starts with a clean slate. */

	/* 3. Initialize the Stack Pointer to the bootloader's MSP */
	__set_MSP(*(__IO uint32_t *)STM32H5_ROM_BOOTLOADER_ADDR);

	/* 4. Jump to ROM bootloader */
	bootloader_jump();

	while (1) {
		/* Should never reach here */
	}
}

/**
 * @brief Initialization function called by Zephyr during boot
 */
static int mini_bootloader_init(void)
{
	/* 1. Verify and initialize the Backup SRAM device */
	const struct device *const backup_memory = DEVICE_DT_GET_ONE(st_stm32_backup_sram);

	if (!device_is_ready(backup_memory)) {
		printk("ERROR: BackUp SRAM device is not ready\n");
		return 0; /* Return 0 to continue booting the main app anyway */
	}

	/* 2. Create a pointer to the first 32-bit word in Backup SRAM */
	volatile uint32_t *backup_reg = (volatile uint32_t *)BACKUP_SRAM_ADDR;

	/* 3. Check the contents of the backup register */
	if (*backup_reg == BOOT_MAGIC_NUMBER) {
		/* * CONDITION MET: Board was reset within the 500ms window!
		 */
		printk("Magic number found! Jumping to ROM Bootloader...\n");

		/* Clear the magic number so we don't get permanently stuck in bootloader mode */
		*backup_reg = 0x00000000;

		/* Jump to ST System Memory */
		jump_to_rom_bootloader();
	} else {
		/* * NORMAL BOOT: Write magic number and start the countdown
		 */
		printk("Starting 500ms boot window...\n");
		*backup_reg = BOOT_MAGIC_NUMBER;

		/* * Wait 500ms. Because we are using the APPLICATION init level,
		 * the kernel is running and we can safely use k_msleep().
		 * If the user resets the board now, the backup RAM retains the magic number.
		 */
		k_msleep(500);

		/* Window expired without a reset. Clear magic number and continue normal boot. */
		*backup_reg = 0x00000000;
		printk("Boot window expired. Continuing to application main()...\n");
	}

	/* Returning 0 automatically hands control back to Zephyr to finish booting and call main()
	 */
	return 0;
}

/* Register the function to run at the APPLICATION level */
SYS_INIT(mini_bootloader_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
