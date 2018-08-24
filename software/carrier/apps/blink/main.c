/* Test application for Carrier board
 *
 * @author: Andreas Biri
 * @date: 2018-08-15
 */

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_gpiote.h"
#include "nrfx_gpiote.h"
#include "SEGGER_RTT.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "boards.h"



int main(void) {

    // Initialize ------------------------------------------------------------------------------------------------------

    // Initialize RTT library
    ret_code_t error_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    printf("Initialized SEGGER RTT");

    // FIXME: BUG FIX -> Enable SD card to pull nRESET high
    nrf_gpio_cfg_output(CARRIER_SD_ENABLE);
    nrf_gpio_pin_set(CARRIER_SD_ENABLE);

    // Initialize GPIOs
    nrf_gpio_cfg_output(CARRIER_LED_BLUE);
    nrf_gpio_cfg_output(CARRIER_LED_GREEN);
    nrf_gpio_cfg_output(CARRIER_LED_RED);

    nrf_gpio_pin_set(CARRIER_LED_BLUE);
    nrf_gpio_pin_set(CARRIER_LED_GREEN);
    nrf_gpio_pin_clear(CARRIER_LED_RED);

    nrf_delay_ms(2000);

    nrf_gpio_pin_set(CARRIER_LED_RED);
    nrf_gpio_pin_set(CARRIER_LED_GREEN);
    nrf_gpio_pin_clear(CARRIER_LED_BLUE);

    nrf_delay_ms(2000);

    // Test LED --------------------------------------------------------------------------------------------------------
    printf("Testing LED: <blinks green>\r\n");

    // Turn off Blue
    nrf_gpio_pin_set(CARRIER_LED_BLUE);

    while (1)
    {
        nrf_gpio_pin_toggle(CARRIER_LED_GREEN);
        nrf_delay_ms(500);
    }
}
