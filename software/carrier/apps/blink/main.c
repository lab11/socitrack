/* Blink
 */

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "SEGGER_RTT.h"

#include "boards.h"
#include "simple_logger.h"

int main(void) {

    // Initialize ------------------------------------------------------------------------------------------------------
    SEGGER_RTT_Init();
    SEGGER_RTT_WriteString(0,"Initialized SEGGER RTT...\r\n");

    // Initialize GPIOs
    nrf_gpio_cfg_output(CARRIER_LED_GREEN);

    // Test SD card ----------------------------------------------------------------------------------------------------
    SEGGER_RTT_WriteString(0,"Testing SD card: ");
    const char filename[] = "testfile.log";
    const char permissions[] = "a"; // w = write, a = append

    // Start file
    simple_logger_init(filename, permissions);

    // If no header, add it
    simple_logger_log_header("HEADER for %s file, written on %s", "FILENAME", "08/16/18");

    // Write
    simple_logger_log("%s: Additional line added\n", "19:06");

    SEGGER_RTT_WriteString(0,"OK\r\n");

    // Test Accelerometer ----------------------------------------------------------------------------------------------
    SEGGER_RTT_WriteString(0,"Testing Accelerometer: ");

    SEGGER_RTT_WriteString(0,"OK\r\n");

    // Test LED --------------------------------------------------------------------------------------------------------
    SEGGER_RTT_WriteString(0,"Testing LED: <blinks green>\r\n");

    while (1)
    {
        nrf_gpio_pin_toggle(CARRIER_LED_GREEN);
        nrf_delay_ms(500);
    }
}
