/*
 * THE FOLLOWING FIRMWARE IS PROVIDED: (1) "AS IS" WITH NO WARRANTY; AND
 * (2)TO ENABLE ACCESS TO CODING INFORMATION TO GUIDE AND FACILITATE CUSTOMER.
 * CONSEQUENTLY, CMOSTEK SHALL NOT BE HELD LIABLE FOR ANY DIRECT, INDIRECT OR
 * CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE CONTENT
 * OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING INFORMATION
 * CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * Copyright (C) CMOSTEK SZ.
 */

/*!
 * @file    cmt2300a_hal.h
 * @brief   CMT2300A hardware abstraction layer
 *
 * @version 1.2
 * @date    Jul 17 2017
 * @author  CMOSTEK R@D
 */
 
#ifndef __CMT2300A_HAL_H
#define __CMT2300A_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#ifdef __cplusplus 
extern "C" { 
#endif

/* ************************************************************************
*  The following need to be modified by user
*  ************************************************************************ */
// GPIO
#define SCLK_GPIO  2
#define SDIO_GPIO   3
#define CSB_GPIO  4
#define FCSB_GPIO   5
#define IO3_GPIO    6

#define NS32_DELAY do { \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
} while (0)

#define NS64_DELAY do { \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
    __asm volatile("nop"); \
} while (0)


#define SCLK_HIGH gpio_put(SCLK_GPIO, 1)
#define SCLK_LOW gpio_put(SCLK_GPIO, 0)
#define SDIO_HIGH gpio_put(SDIO_GPIO, 1)
#define SDIO_LOW gpio_put(SDIO_GPIO, 0)
#define CSB_HIGH gpio_put(CSB_GPIO, 1)
#define CSB_LOW gpio_put(CSB_GPIO, 0)
#define FCSB_HIGH gpio_put(FCSB_GPIO, 1)
#define FCSB_LOW gpio_put(FCSB_GPIO, 0)
#define SDIO_GET gpio_get(SDIO_GPIO)

#define CMT2300A_DelayMs(ms)            sleep_ms(ms)
#define CMT2300A_DelayUs(us)            sleep_us(us)

#define CMT2300A_GetTickCount() \
    ((uint32_t)to_ms_since_boot(get_absolute_time()))

/* ************************************************************************ */

void CMT2300A_InitGpio(void);

uint8_t CMT2300A_ReadReg(uint8_t addr);
void CMT2300A_WriteReg(uint8_t addr, uint8_t dat);

void CMT2300A_ReadFifo(uint8_t buf[], uint16_t len);
void CMT2300A_WriteFifo(const uint8_t buf[], uint16_t len);

#ifdef __cplusplus 
} 
#endif

#endif
