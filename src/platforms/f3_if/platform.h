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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"
#include "version.h"

#include <setjmp.h>

#ifdef ENABLE_DEBUG
# define PLATFORM_HAS_DEBUG
# define USBUART_DEBUG
#endif

#define PLATFORM_HAS_TRACESWO
#define BOARD_IDENT "Black Magic Probe (F3_IF), (Firmware " FIRMWARE_VERSION ")"
#define DFU_IDENT   "Black Magic Firmware Upgrade (F3_IF)"

/* Important pin mappings for STM32 implementation:
 *
 * LED0 = 	PB5	(Green  LED : Running)
 * LED1 = 	PB6	(Orange LED : Idle)
 * LED2 = 	PB7	(Red LED    : Error)
 *
 * nTRST = 	PC1
 * SRST_OUT =   PC8
 * TDI = 	PA0
 * TMS = 	PA1 (input for SWDP)
 * TCK = 	PA6/SWCLK
 * TDO = 	PA6 (input for TRACESWO
 * nSRST =	PA5
 *
 * Force DFU mode button: PA0
 */

/* Hardware definitions... */
#define JTAG_PORT 	GPIOA
#define TDI_PORT	JTAG_PORT
#define TMS_PORT	JTAG_PORT
#define TCK_PORT	JTAG_PORT
#define TDO_PORT	JTAG_PORT
#define TDI_PIN		GPIO0
#define TMS_PIN		GPIO1
#define TCK_PIN		GPIO7
#define TDO_PIN		GPIO6

#define SWDIO_PORT 	JTAG_PORT
#define SWCLK_PORT 	JTAG_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define SRST_PORT	GPIOA
#define SRST_PIN	GPIO5

#define LED_PORT	GPIOB
#define LED_PORT_UART	GPIOB
#define LED_UART	GPIO6
#define LED_IDLE_RUN	GPIO5
#define LED_ERROR	GPIO7
/* PORTB does not stay active in system bootloader!*/
#define LED_BOOTLOADER	GPIO6

#define TMS_SET_MODE() \
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, \
	                GPIO_PUPD_NONE, TMS_PIN);
#define SWDIO_MODE_FLOAT() \
	gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, \
	                GPIO_PUPD_NONE, SWDIO_PIN);

#define SWDIO_MODE_DRIVE() \
	gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, \
	                GPIO_PUPD_NONE, SWDIO_PIN);


#define USB_DRIVER      st_usbfs_v1_usb_driver
#define USB_IRQ         NVIC_USB_LP_CAN1_RX0_IRQ
#define USB_ISR         usb_lp_can1_rx0_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		(2 << 4)
#define IRQ_PRI_USBUSART	(1 << 4)
#define IRQ_PRI_USBUSART_TIM	(3 << 4)
#define IRQ_PRI_TRACE		(0 << 4)

#define USBUSART USART2
#define USBUSART_CR1 USART2_CR1
#define USBUSART_IRQ NVIC_USART2_EXTI26_IRQ
#define USBUSART_CLK RCC_USART2
#define USBUSART_TX_PORT GPIOA
#define USBUSART_TX_PIN  GPIO3
#define USBUSART_RX_PORT GPIOA
#define USBUSART_RX_PIN  GPIO2
#define USBUSART_ISR usart2_exti26_isr
#define USBUSART_TIM TIM4
#define USBUSART_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM4)
#define USBUSART_TIM_IRQ NVIC_TIM4_IRQ
#define USBUSART_TIM_ISR tim4_isr
#define USART_SR(x) USART2_ISR

#define UART_PIN_SETUP() do {											\
		gpio_mode_setup(USBUSART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, \
						USBUSART_TX_PIN | USBUSART_RX_PIN);				\
		gpio_set_af(USBUSART_TX_PORT, GPIO_AF7,							\
					USBUSART_TX_PIN | USBUSART_RX_PIN);					\
	} while(0)

#define TRACE_TIM TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ   NVIC_TIM3_IRQ
#define TRACE_ISR   tim3_isr

#ifdef ENABLE_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);
# define DEBUG printf
#else
# define DEBUG(...)
#endif


#define gpio_set_val(port, pin, val) do {	\
	if(val)					\
		gpio_set((port), (pin));	\
	else					\
		gpio_clear((port), (pin));	\
} while(0)

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE_RUN, state);}
#define SET_ERROR_STATE(state)	{gpio_set_val(LED_PORT, LED_ERROR, state);}

static inline int platform_hwversion(void)
{
	return 0;
}

/* Use newlib provided integer only stdio functions */
#define sscanf siscanf
#define sprintf siprintf
#define vasprintf vasiprintf
#define snprintf sniprintf

#endif

