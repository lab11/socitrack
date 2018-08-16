/* Blink
 */

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"

#include "boards.h"

int main(void) {

    // Initialize
    nrf_gpio_cfg_output(CARRIER_LED_GREEN);

    // Enter main loop.
    while (1)
    {
        nrf_gpio_pin_toggle(CARRIER_LED_GREEN);
        nrf_delay_ms(500);
    }
}
