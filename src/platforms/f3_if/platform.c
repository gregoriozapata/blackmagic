/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017 Uwe Bonnes bon@elektron,ikp,physik.tu-darmstadt.de
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file implements the platform specific functions for the STM32F3_IF
 * implementation.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
#include "morse.h"

#include <libopencm3/stm32/f3/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/flash.h>

extern uint32_t _ebss;

inline void set_clock(void)
{
	uint32_t cfgr;
	rcc_osc_on(RCC_HSE);
	rcc_wait_for_osc_ready(RCC_HSE);
	cfgr = (((72 / 8) - 2) << RCC_CFGR_PLLMUL_SHIFT) |
		RCC_CFGR_PLLSRC |
		(RCC_CFGR_PPRE1_DIV_2 << RCC_CFGR_PPRE1_SHIFT);
	RCC_CFGR = cfgr;
	rcc_osc_on(RCC_PLL);
	rcc_wait_for_osc_ready(RCC_PLL);
	flash_set_ws(2);
	cfgr |= RCC_CFGR_SWS_PLL;
	RCC_CFGR = cfgr;
	rcc_wait_for_sysclk_status(RCC_PLL);
}

void platform_init(void)
{
	volatile uint32_t *magic = (uint32_t *) &_ebss;
	/* If RCC_CFGR is not at it's reset value, the bootloader was executed
	 * and SET_ADDRESS got us to this place. On F3, without further efforts,
	 * DFU does not start in that case.
	 * So issue an reset to allow a clean start!
	 */
	if (RCC_CFGR)
		scb_reset_system();
	SYSCFG_MEMRM &= ~3;
	/* Buttom is BOOT0, so buttom is already evaluated!*/
	if (((magic[0] == BOOTMAGIC0) && (magic[1] == BOOTMAGIC1))) {
		magic[0] = 0;
		magic[1] = 0;
		/* Jump to the built in bootloader by mapping System flash.
		   As we just come out of reset, no other deinit is needed!*/
		SYSCFG_MEMRM |=  1;
		scb_reset_core();
	}
	set_clock();
	/* Detach USB by pulling PA9/USB_DETACH_N low.*/
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO9);
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO9);
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_CRC);
	rcc_periph_clock_enable(RCC_USB);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF14, GPIO11 | GPIO12);

//	GPIOA_OSPEEDR |= 0x800A; /* Set High speed on PA0,PA1, PA7*/
	GPIOA_OSPEEDR |= 0x4005; /* Set medium speed on PA0,PA1, PA7*/
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_INPUT,
					GPIO_PUPD_PULLUP, TMS_PIN);
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT,
					GPIO_PUPD_PULLUP, TDI_PIN);
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT,
					GPIO_PUPD_NONE, TCK_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT,
					GPIO_PUPD_PULLUP, TDO_PIN);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT,
					GPIO_PUPD_NONE,
					LED_UART | LED_IDLE_RUN | LED_ERROR | LED_BOOTLOADER);
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT,
					GPIO_PUPD_NONE,
					SRST_PIN);
	gpio_set(SRST_PORT, SRST_PIN);
	gpio_set_output_options(SRST_PORT, GPIO_OTYPE_OD,
							GPIO_OSPEED_2MHZ, SRST_PIN);
	platform_timing_init();
	gpio_set(GPIOA, GPIO9); /* Attach USB*/
	cdcacm_init();
	usbuart_init();
}

void platform_srst_set_val(bool assert)
{
	gpio_set_val(SRST_PORT, SRST_PIN, !assert);
}

bool platform_srst_get_val(void) { return false; }

const char *platform_target_voltage(void)
{
	return "ABSENT!";
}

void platform_request_boot(void)
{
	uint32_t *magic = (uint32_t *) &_ebss;
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	scb_reset_system();
}
