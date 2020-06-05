/****************************************************************************
 *
 *   Copyright (c) 2012-2016 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file init.c
 *
 * PX4FMU-specific early startup code.  This file implements the
 * board_app_initialize() function that is called early by nsh during startup.
 *
 * Code here is run before the rcS script is invoked; it should start required
 * subsystems and perform board-specific initialization.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/tasks.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/board.h>
#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#include <nuttx/analog/adc.h>
#include <nuttx/mm/gran.h>

#include <stm32.h>
#include "board_config.h"
#include <stm32_uart.h>

#include <arch/board/board.h>

#include <drivers/drv_hrt.h>
#include <drivers/drv_board_led.h>

#include <systemlib/px4_macros.h>

#include <px4_platform_common/init.h>
#include <px4_platform/board_dma_alloc.h>

#include <drivers/drv_pwm_output.h>
#include <px4_arch/io_timer.h>

/**
 * Ideally we'd be able to get these from up_internal.h,
 * but since we want to be able to disable the NuttX use
 * of leds for system indication at will and there is no
 * separate switch, we need to build independent of the
 * CONFIG_ARCH_LEDS configuration switch.
 */
__BEGIN_DECLS
extern void led_init(void);
extern void led_on(int led);
extern void led_off(int led);
__END_DECLS

/************************************************************************************
 * Name: board_on_reset
 *
 * Description:
 * Optionally provided function called on entry to board_system_reset
 * It should perform any house keeping prior to the rest.
 *
 * status - 1 if resetting to boot loader
 *          0 if just resetting
 *
 ************************************************************************************/

__EXPORT void board_on_reset(int status)
{
	// Configure the GPIO pins to outputs and keep them low.
	for (int i = 0; i < DIRECT_PWM_OUTPUT_CHANNELS; ++i) {
		px4_arch_configgpio(io_timer_channel_get_gpio_output(i));
	}

	/*
	 * On resets invoked from system (not boot) insure we establish a low
	 * output state (discharge the pins) on PWM pins before they become inputs.
	 */
	if (status >= 0) {
		up_mdelay(400);
	}
}

/************************************************************************************
 * Name: stm32_boardinitialize
 *
 * Description:
 *   All STM32 architectures must provide the following entry point.  This entry point
 *   is called early in the initialization -- after all memory has been configured
 *   and mapped but before any devices have been initialized.
 *
 ************************************************************************************/

__EXPORT void stm32_boardinitialize(void)
{
	// Reset all PWM to Low outputs.
	board_on_reset(-1);

	// Configure LEDs.
	board_autoled_initialize();

	board_spi_initialize();

	// Configure ADC pins.
	stm32_configgpio(GPIO_ADC1_IN2);	/* BATT_VOLTAGE_SENS */
	stm32_configgpio(GPIO_ADC1_IN3);	/* BATT_CURRENT_SENS */
	stm32_configgpio(GPIO_ADC1_IN4);	/* VDD_5V_SENS */
	stm32_configgpio(GPIO_ADC1_IN11);	/* RSSI analog in */

	// Configure CAN interface
	stm32_configgpio(GPIO_CAN1_RX);
	stm32_configgpio(GPIO_CAN1_TX);

	// Configure power supply control/sense pins.
	stm32_configgpio(GPIO_PERIPH_3V3_EN);
	stm32_configgpio(GPIO_VDD_BRICK_VALID);
	stm32_configgpio(GPIO_VDD_USB_VALID);

	stm32_configgpio(GPIO_SBUS_INV);
	stm32_configgpio(GPIO_SPEKTRUM_PWR_EN);

	stm32_configgpio(GPIO_8266_GPIO2);
	stm32_configgpio(GPIO_8266_GPIO0);

	// Safety - led on in led driver.
	stm32_configgpio(GPIO_BTN_SAFETY);
	stm32_configgpio(GPIO_PPM_IN);

#if defined(CONFIG_STM32_SPI4)

	/* We have SPI4 is GPIO_8266_GPIO2 PB4 pin 3 Low */
	if (stm32_gpioread(GPIO_8266_GPIO2) != 0) {
#endif /* CONFIG_STM32_SPI4 */
		stm32_configgpio(GPIO_8266_PD);
		stm32_configgpio(GPIO_8266_RST);
#if defined(CONFIG_STM32_SPI4)
	}

#endif /* CONFIG_STM32_SPI4 */

	// Configure heater GPIO.
	stm32_configgpio(GPIO_HEATER_INPUT);
	stm32_configgpio(GPIO_HEATER_OUTPUT);

#ifdef CONFIG_STM32_OTGFS
	stm32_configgpio(GPIO_OTGFS_VBUS);
#endif
}

/****************************************************************************
 * Name: board_app_initialize
 *
 * Description:
 *   Perform application specific initialization.  This function is never
 *   called directly from application code, but only indirectly via the
 *   (non-standard) boardctl() interface using the command BOARDIOC_INIT.
 *
 * Input Parameters:
 *   arg - The boardctl() argument is passed to the board_app_initialize()
 *         implementation without modification.  The argument has no
 *         meaning to NuttX; the meaning of the argument is a contract
 *         between the board-specific initalization logic and the the
 *         matching application logic.  The value cold be such things as a
 *         mode enumeration value, a set of DIP switch switch settings, a
 *         pointer to configuration data read from a file or serial FLASH,
 *         or whatever you would like to do with it.  Every implementation
 *         should accept zero/NULL as a default configuration.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure to indicate the nature of the failure.
 *
 ****************************************************************************/

__EXPORT int board_app_initialize(uintptr_t arg)
{
	px4_platform_init();

	// Configure the DMA allocator.
	if (board_dma_alloc_init() < 0) {
		syslog(LOG_ERR, "DMA alloc FAILED\n");
	}

	/**
	 * Poll at 1ms intervals for received bytes that have not triggered
	 * a DMA event.
	 */
	static struct hrt_call serial_dma_call;
	hrt_call_every(&serial_dma_call, 1000, 1000, (hrt_callout)stm32_serial_dma_poll, NULL);

	// Initial LED state.
	drv_led_start();
	led_off(LED_RED);
	led_off(LED_GREEN);
	led_off(LED_BLUE);

	if (board_hardfault_init(2, true) != 0) {
		led_on(LED_RED);
	}

	// Power down the heater.
	stm32_gpiowrite(GPIO_HEATER_OUTPUT, 0);

#if defined(CONFIG_STM32_SPI4)

	if (stm32_gpioread(GPIO_8266_GPIO2) == 0) {
		syslog(LOG_INFO, "[boot] 8266_GPIO2 - Low Initialize SPI port 4 \n");
	}

#endif /* defined(CONFIG_STM32_SPI4) */

	board_spi_start();


#if defined(CONFIG_MMCSD)
	struct sdio_dev_s *sdio = sdio_initialize(CONFIG_NSH_MMCSDSLOTNO);

	if (!sdio) {
		led_on(LED_RED);
		syslog(LOG_ERR, "[boot] Failed to initialize SDIO slot %d\n", CONFIG_NSH_MMCSDSLOTNO);
		return -ENODEV;
	}

	// bind the SDIO interface to the MMC/SD driver.
	int ret = mmcsd_slotinitialize(CONFIG_NSH_MMCSDMINOR, sdio);

	if (ret != OK) {
		led_on(LED_RED);
		syslog(LOG_ERR, "[boot] Failed to bind SDIO to the MMC/SD driver: %d\n", ret);
		return ret;
	}

	// Then let's guess and say that there is a card in the slot. There is no card detect GPIO.
	sdio_mediachange(sdio, true);
#endif /* CONFIG_MMCSD */

	return OK;
}
