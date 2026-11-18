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
 * @file    cmt2300a_hal.c
 * @brief   CMT2300A hardware abstraction layer
 *
 * @version 1.2
 * @date    Jul 17 2017
 * @author  CMOSTEK R@D
 */
 
#include "cmt2300a_hal.h"

/*! ********************************************************
* @name    CMT2300A_InitGpio
* @desc    Initializes the CMT2300A interface GPIOs.
* *********************************************************/
void CMT2300A_InitGpio(void)
{
	// SCLK
    gpio_init(SCLK_GPIO); gpio_set_dir(SCLK_GPIO, GPIO_OUT);
    gpio_put(SCLK_GPIO, 0);

    // SDIO
    gpio_init(SDIO_GPIO); gpio_set_dir(SDIO_GPIO, GPIO_OUT);
    gpio_put(SDIO_GPIO, 0);

    // CSB, set high
    gpio_init(CSB_GPIO); gpio_put(CSB_GPIO, 1);
    gpio_set_dir(CSB_GPIO, GPIO_OUT);
    gpio_put(CSB_GPIO, 1);

    // FCSB, set high
    gpio_init(FCSB_GPIO); gpio_put(FCSB_GPIO, 1);
    gpio_set_dir(FCSB_GPIO, GPIO_OUT);
    gpio_put(FCSB_GPIO, 1);

    // Radio IO3, set low
    gpio_init(IO3_GPIO); gpio_set_dir(IO3_GPIO, GPIO_OUT);
    gpio_put(IO3_GPIO, 0);
	
}

void sdio_as_output(void) {
    gpio_put(SDIO_GPIO, 0);
    gpio_set_dir(SDIO_GPIO, GPIO_OUT);
}

void sdio_as_input(void) {
    gpio_set_dir(SDIO_GPIO, GPIO_IN);
}

/*! ********************************************************
* @name    CMT2300A_ReadReg
* @desc    Read the CMT2300A register at the specified address.
* @param   addr: register address
* @return  Register value
* *********************************************************/
uint8_t CMT2300A_ReadReg(uint8_t addr)
{
    uint8_t dat = 0;

    SCLK_LOW;
    FCSB_HIGH;
    CSB_LOW;
    NS64_DELAY;

    // Bit 7 is 1 for register read
    addr |= 0x80;

    sdio_as_output();

    // Send address, MSB first
    for (int i = 0; i < 8; i++) {
        if (addr & 0x80)
            SDIO_HIGH;
        else
            SDIO_LOW;

        NS64_DELAY;
        SCLK_HIGH;
        NS64_DELAY;

        if (i == 7)
        {
            // Release SDIO before final falling edge
            sdio_as_input();
            NS32_DELAY;
        }

        SCLK_LOW;
        addr <<= 1;
    }

    // Read register data
    for (int i = 0; i < 8; i++) {
        NS64_DELAY;
        dat <<= 1;

        SCLK_HIGH;
        NS64_DELAY;

        if (SDIO_GET)
            dat |= 0x01;

        SCLK_LOW;
    }

    NS64_DELAY;
    CSB_HIGH;

    return dat;
}

/*! ********************************************************
* @name    CMT2300A_WriteReg
* @desc    Write the CMT2300A register at the specified address.
* @param   addr: register address
*          dat: register value
* *********************************************************/
void CMT2300A_WriteReg(uint8_t addr, uint8_t dat)
{
    SCLK_LOW;
    FCSB_HIGH;

    NS64_DELAY;
    CSB_LOW;
    NS64_DELAY;

    sdio_as_output();

    // Bit 7 is 0 for write
    addr &= 0x7F;

    // Send 8-bit address, MSB first
    for (int i = 0; i < 8; i++) {
        if (addr & 0x80)
            SDIO_HIGH;
        else
            SDIO_LOW;

        NS64_DELAY;
        SCLK_HIGH;
        NS64_DELAY;

        SCLK_LOW;
        addr <<= 1;
    }

    NS64_DELAY;

    // Send 8-bit data, MSB first
    for (int i = 0; i < 8; i++) {
        if (dat & 0x80)
            SDIO_HIGH;
        else
            SDIO_LOW;

        NS64_DELAY;
        SCLK_HIGH;
        NS64_DELAY;

        SCLK_LOW;
        dat <<= 1;
    }

    NS64_DELAY;
    CSB_HIGH;

    // Leave SDIO as an input
    SDIO_HIGH;
    sdio_as_input();

    FCSB_HIGH;
}

/*! ********************************************************
* @name    CMT2300A_ReadFifo
* @desc    Reads the contents of the CMT2300A FIFO.
* @param   buf: buffer where to copy the FIFO read data
*          len: number of bytes to be read from the FIFO
* *********************************************************/
void CMT2300A_ReadFifo(uint8_t buf[], uint16_t len)
{
    CSB_HIGH;
    FCSB_HIGH;
    SCLK_LOW;

    sdio_as_input();

    for (uint16_t i = 0; i < len; i++) {
        FCSB_LOW;
        NS64_DELAY;
        NS64_DELAY;

        uint8_t dat = 0;

        // Read 8-bit data, MSB first
        for (int j = 0; j < 8; j++) {
            SCLK_LOW;
            NS64_DELAY;

            dat <<= 1;

            SCLK_HIGH;
            NS64_DELAY;

            if (SDIO_GET)
                dat |= 0x01;
        }

        buf[i] = dat;

        SCLK_LOW;

        // Wait > 2 us before releasing FCSB
        sleep_us(3);

        FCSB_HIGH;

        // Wait > 4 us before next byte
        sleep_us(5);
    }

    sdio_as_input();
    FCSB_HIGH;
}

/*! ********************************************************
* @name    CMT2300A_WriteFifo
* @desc    Writes the buffer contents to the CMT2300A FIFO.
* @param   buf: buffer containing data to be put on the FIFO
*          len: number of bytes to be written to the FIFO
* *********************************************************/
void CMT2300A_WriteFifo(const uint8_t buf[], uint16_t len)
{
    CSB_HIGH;
    FCSB_HIGH;
    SCLK_LOW;

    sdio_as_output();

    for (uint16_t i = 0; i < len; i++) {
        uint8_t dat = buf[i];

        // Prepare first data bit
        if (dat & 0x80)
            SDIO_HIGH;
        else
            SDIO_LOW;

        FCSB_LOW;

        // Allow FCSB setup time
        NS64_DELAY;
        NS64_DELAY;

        // Send 8 bits
        for (int j = 0; j < 8; j++) {
            if (dat & 0x80)
                SDIO_HIGH;
            else
                SDIO_LOW;

            NS64_DELAY;
            SCLK_HIGH;
            NS64_DELAY;
            SCLK_LOW;

            dat <<= 1;
        }

        sleep_us(3);
        FCSB_HIGH;
        sleep_us(5);
    }

    SDIO_HIGH;
    sdio_as_input();

    FCSB_HIGH;
}
