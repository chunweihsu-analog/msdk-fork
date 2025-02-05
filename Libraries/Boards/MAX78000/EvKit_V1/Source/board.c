/******************************************************************************
 * Copyright (C) 2022 Maxim Integrated Products, Inc., All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL MAXIM INTEGRATED BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Maxim Integrated
 * Products, Inc. shall not be used except as stated in the Maxim Integrated
 * Products, Inc. Branding Policy.
 *
 * The mere transfer of this software does not imply any licenses
 * of trade secrets, proprietary technology, copyrights, patents,
 * trademarks, maskwork rights, or any other form of intellectual
 * property whatsoever. Maxim Integrated Products, Inc. retains all
 * ownership rights.
 *
 ******************************************************************************/

#include <stdio.h>
#include "mxc_device.h"
#include "mxc_sys.h"
#include "mxc_assert.h"
#include "board.h"
#include "uart.h"
#include "gpio.h"
#include "mxc_pins.h"
#include "led.h"
#include "pb.h"
#include "lpgcr_regs.h"
#include "simo_regs.h"
#include "tft_ssd2119.h"
#include "tsc2046.h"

/***** Global Variables *****/
mxc_uart_regs_t *ConsoleUart = MXC_UART_GET_UART(CONSOLE_UART);
extern uint32_t SystemCoreClock;
const mxc_gpio_cfg_t pb_pin[] = {
    { MXC_GPIO2, MXC_GPIO_PIN_6, MXC_GPIO_FUNC_IN, MXC_GPIO_PAD_PULL_UP, MXC_GPIO_VSSEL_VDDIOH },
    { MXC_GPIO2, MXC_GPIO_PIN_7, MXC_GPIO_FUNC_IN, MXC_GPIO_PAD_PULL_UP, MXC_GPIO_VSSEL_VDDIOH },
};
const unsigned int num_pbs = (sizeof(pb_pin) / sizeof(mxc_gpio_cfg_t));

const mxc_gpio_cfg_t led_pin[] = {
    { MXC_GPIO0, MXC_GPIO_PIN_2, MXC_GPIO_FUNC_OUT, MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIOH },
    { MXC_GPIO0, MXC_GPIO_PIN_3, MXC_GPIO_FUNC_OUT, MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIOH },
};
const unsigned int num_leds = (sizeof(led_pin) / sizeof(mxc_gpio_cfg_t));
/***** File Scope Variables *****/
// const uart_cfg_t uart_cfg = {
//     UART_PARITY_DISABLE,
//     UART_DATA_SIZE_8_BITS,
//     UART_STOP_1,
//     UART_FLOW_CTRL_DIS,
//     UART_FLOW_POL_DIS,
//     CONSOLE_BAUD
// };

// const sys_cfg_uart_t uart_sys_cfg = {MAP_A,Enable};    // There is no special system configuration parameters for UART on MAX32650
// const sys_cfg_i2c_t i2c_sys_cfg = NULL;     // There is no special system configuration parameters for I2C on MAX32650
// const sys_cfg_spixc_t spixc_sys_cfg = NULL;   // There is no special system configuration parameters for SPIXC on MAX32650

// const spixc_cfg_t mx25_spixc_cfg = {
//     0, //mode
//     0, //ssel_pol
//     1000000 //baud
// };

/******************************************************************************/
void mxc_assert(const char *expr, const char *file, int line)
{
    printf("MXC_ASSERT %s #%d: (%s)\n", file, line, expr);

    while (1) {}
}

#ifndef __riscv
void TS_SPI_Init(void)
{
    int master = 1;
    int quadMode = 0;
    int numSlaves = 0;
    int ssPol = 0;

    mxc_spi_pins_t ts_pins = {
        // CLK, MISO, MOSI enabled, SS IDx = 2
        .clock = true, .ss0 = false, .ss1 = false,   .ss2 = true,
        .miso = true,  .mosi = true, .sdio2 = false, .sdio3 = false,
    };

    MXC_SPI_Init(TS_SPI, master, quadMode, numSlaves, ssPol, TS_SPI_FREQ, ts_pins);

    // Set SPI pins to VDDIOH (3.3V) to be compatible with TFT display
    MXC_GPIO_SetVSSEL(MXC_GPIO0, MXC_GPIO_VSSEL_VDDIOH,
                      MXC_GPIO_PIN_5 | MXC_GPIO_PIN_6 | MXC_GPIO_PIN_7 | MXC_GPIO_PIN_10);
    MXC_SPI_SetDataSize(TS_SPI, 8);
    MXC_SPI_SetWidth(TS_SPI, SPI_WIDTH_STANDARD);
}

void TS_SPI_Transmit(uint8_t datain, uint16_t *dataout)
{
    int i;
    uint8_t rx[2] = { 0, 0 };
    mxc_spi_req_t request;

    request.spi = TS_SPI;
    request.ssDeassert = 0;
    request.txData = (uint8_t *)(&datain);
    request.rxData = NULL;
    request.txLen = 1;
    request.rxLen = 0;
    request.ssIdx = 2;

    MXC_SPI_SetFrequency(TS_SPI, TS_SPI_FREQ);
    MXC_SPI_SetDataSize(TS_SPI, 8);

    MXC_SPI_MasterTransaction(&request);

    // Wait to clear TS busy signal
    for (i = 0; i < 100; i++) {
        __asm volatile("nop\n");
    }

    request.ssDeassert = 1;
    request.txData = NULL;
    request.rxData = (uint8_t *)(rx);
    request.txLen = 0;
    request.rxLen = 2;

    MXC_SPI_MasterTransaction(&request);

    if (dataout != NULL) {
        *dataout = (rx[1] | (rx[0] << 8)) >> 4;
    }
}
#endif // __riscv

/******************************************************************************/
int Board_Init(void)
{
#ifndef __riscv
    int err;

    // Set SWDCLK and SWDIO pads to 3.3V
    // MXC_GPIO0->vssel |= (3 << 28);

    // Enable GPIO
    MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_GPIO0);
    MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_GPIO1);
    MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_GPIO2);

    if ((err = Console_Init()) < E_NO_ERROR) {
        return err;
    }

    // Set UART 0 pads to 3.3V
    MXC_GPIO0->vssel |= (0xF << 0);

    if ((err = PB_Init()) != E_NO_ERROR) {
        MXC_ASSERT_FAIL();
        return err;
    }

    if ((err = LED_Init()) != E_NO_ERROR) {
        MXC_ASSERT_FAIL();
        return err;
    }

    MXC_SIMO->vrego_c = 0x43; // Set CNN voltage

    /* TFT reset and backlight signal */
    mxc_tft_spi_config tft_spi_config = {
        .regs = MXC_SPI0,
        .gpio = { MXC_GPIO0, MXC_GPIO_PIN_5 | MXC_GPIO_PIN_6 | MXC_GPIO_PIN_7 | MXC_GPIO_PIN_11,
                  MXC_GPIO_FUNC_ALT1, MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIOH },
        .freq = 25000000,
        .ss_idx = 1,
    };

    /* TFT reset signal */
    mxc_gpio_cfg_t tft_reset_pin = { MXC_GPIO0, MXC_GPIO_PIN_19, MXC_GPIO_FUNC_OUT,
                                     MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIOH };
    /* Initialize TFT display */
    MXC_TFT_PreInit(&tft_spi_config, &tft_reset_pin, NULL);

    // SPI config is included here for compabibility with MXC_TS_PreInit.  The
    // actual SPI initialization is done by TS_SPI_Init above.
    // PreInit still needs to be used to properly assign the interrupt/busy pins.
    mxc_ts_spi_config ts_spi_config = {
        .regs = MXC_SPI0,
        .gpio = { MXC_GPIO0, MXC_GPIO_PIN_5 | MXC_GPIO_PIN_6 | MXC_GPIO_PIN_7 | MXC_GPIO_PIN_10,
                  MXC_GPIO_FUNC_ALT1, MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIOH },
        .freq = 1000000,
        .ss_idx = 2,
    };

    /* Touch screen controller interrupt signal */
    mxc_gpio_cfg_t int_pin = { MXC_GPIO0, MXC_GPIO_PIN_17, MXC_GPIO_FUNC_IN, MXC_GPIO_PAD_NONE,
                               MXC_GPIO_VSSEL_VDDIOH };
    /* Touch screen controller busy signal */
    mxc_gpio_cfg_t busy_pin = { MXC_GPIO0, MXC_GPIO_PIN_16, MXC_GPIO_FUNC_IN, MXC_GPIO_PAD_NONE,
                                MXC_GPIO_VSSEL_VDDIOH };
    /* Initialize Touch Screen controller */
    MXC_TS_PreInit(&ts_spi_config, &int_pin, &busy_pin);

#endif // __riscv
    return E_NO_ERROR;
}

/******************************************************************************/
int Console_Init(void)
{
    int err;

    if ((err = MXC_UART_Init(ConsoleUart, CONSOLE_BAUD, MXC_UART_IBRO_CLK)) != E_NO_ERROR) {
        return err;
    }

    return E_NO_ERROR;
}

/******************************************************************************/
int Console_Shutdown(void)
{
    int err;

    if ((err = MXC_UART_Shutdown(ConsoleUart)) != E_NO_ERROR) {
        return err;
    }

    return E_NO_ERROR;
}

/******************************************************************************/
void NMI_Handler(void)
{
    __NOP();
}

#ifdef __riscv
/******************************************************************************/
int Debug_Init(void)
{
    // Set up RISCV JTAG pins (P1[0..3] AF2)
    MXC_GPIO1->en0_clr = 0x0f;
    MXC_GPIO1->en1_set = 0x0f;
    MXC_GPIO1->en2_clr = 0x0f;

    return E_NO_ERROR;
}
#endif // __riscv

/******************************************************************************/
int Camera_Power(int on)
{
    return E_NOT_SUPPORTED;
}

/******************************************************************************/
int Microphone_Power(int on)
{
    return E_NOT_SUPPORTED;
}
