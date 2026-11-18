/*
 * ==================================================================
 * Simple CMT2300A CW Generator
 * rev 1 - Nov 2026 - shabaz
 *
 * Hardware Pinout:
 * GPIO2: SCLK
 * GPIO3: SDIO
 * GPIO4: CSB
 * GPIO5: FCSB
 * GPIO6: IO3
 * GPIO25  : Onboard status LED (RP2040 Pico)
 * ==================================================================
 */


#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "cmt2300a_hal.h"
#include "radio.h"
#include "menu.h"



// -----------------------------------------------------------------------------
// Defines
// -----------------------------------------------------------------------------

// GPIO
#define LED_GPIO      25


// Helpers
#define LED_ON gpio_put(LED_GPIO, 1)
#define LED_OFF gpio_put(LED_GPIO, 0)



// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------
void board_init(void);


// -----------------------------------------------------------------------------
// Functions
// -----------------------------------------------------------------------------
void board_init(void) {
    // LED
    gpio_init(LED_GPIO); gpio_set_dir(LED_GPIO, GPIO_OUT);
    gpio_put(LED_GPIO, 0);

    stdio_init_all();

    CMT2300A_InitGpio();    

    sleep_ms(100); // let any attached hardware settle

}


// -----------------------------------------------------------------------------
// main function
// -----------------------------------------------------------------------------
int main(void) {
    board_init();
    LED_OFF;
    bool ok = radio_init();
    sleep_ms(1000);
    printf("Radio init: %s\r\n", ok ? "OK" : radio_error());
    menu_init();
    while (true) {
        menu_poll();
        sleep_ms(1);
        tight_loop_contents();
    }
}
