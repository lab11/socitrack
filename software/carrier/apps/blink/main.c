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
#include "nrf_spi_mngr.h"
#include "nrf_drv_spi.h"
#include "nrf_sdh.h"
#include "nrf_soc.h"
#include "app_uart.h"
#include "nrf_uart.h"
#include "SEGGER_RTT.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "boards.h"
#include "simple_logger.h"
#include "accelerometer_lis2dw12.h"


/*-----------------------------------------------------------------------*/
/* SPI & UART functions                                                		 */
/*-----------------------------------------------------------------------*/

// Use SPI0
#define SPI_INSTANCE_NR 0
NRF_SPI_MNGR_DEF(spi_instance, 5, SPI_INSTANCE_NR);

void spi_init(void) {

    nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
    spi_config.sck_pin    = CARRIER_SPI_SCLK;
    spi_config.miso_pin   = CARRIER_SPI_MISO;
    spi_config.mosi_pin   = CARRIER_SPI_MOSI;
    spi_config.ss_pin     = CARRIER_CS_ACC;
    spi_config.frequency  = NRF_DRV_SPI_FREQ_4M;
    spi_config.mode       = NRF_DRV_SPI_MODE_3;
    spi_config.bit_order  = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST;

    // Init SPI manager
    ret_code_t err_code = nrf_spi_mngr_init(&spi_instance, &spi_config);
    APP_ERROR_CHECK(err_code);
}


void uart_error_handle (app_uart_evt_t * p_event) {
    if (p_event->evt_type == APP_UART_COMMUNICATION_ERROR) {
        APP_ERROR_HANDLER(p_event->data.error_communication);
    } else if (p_event->evt_type == APP_UART_FIFO_ERROR) {
        APP_ERROR_HANDLER(p_event->data.error_code);
    }
}

#define UART_TX_BUF_SIZE     2048
#define UART_RX_BUF_SIZE     2048

void uart_init(void) {
    uint32_t err_code;

    const app_uart_comm_params_t comm_params =
            {
                    CARRIER_UART_RX,
                    CARRIER_UART_TX,
                    0,
                    0,
                    APP_UART_FLOW_CONTROL_DISABLED,
                    false,
                    NRF_UART_BAUDRATE_115200
            };

    APP_UART_FIFO_INIT(&comm_params,
                       UART_RX_BUF_SIZE,
                       UART_TX_BUF_SIZE,
                       uart_error_handle,
                       APP_IRQ_PRIORITY_LOW,
                       err_code);
    APP_ERROR_CHECK(err_code);

}


/*-----------------------------------------------------------------------*/
/* Accelerometer function                                                		 */
/*-----------------------------------------------------------------------*/

static int16_t x[32], y[32], z[32];

lis2dw12_wakeup_config_t wake_config = {
        .sleep_enable = 1,
        .threshold = 0x05,
        .wake_duration = 3,
        .sleep_duration = 2
};

lis2dw12_config_t config = {
        .odr = lis2dw12_odr_200,
        .mode = lis2dw12_low_power,
        .lp_mode = lis2dw12_lp_1,
        .cs_nopull = 0,
        .bdu = 1,
        .auto_increment = 1,
        .i2c_disable = 1,
        .int_active_low = 0,
        .on_demand = 1,
        .bandwidth = lis2dw12_bw_odr_2,
        .fs = lis2dw12_fs_4g,
        .high_pass = 0,
        .low_noise = 1,
};

static void acc_fifo_read_handler(void) {

    for(int i = 0; i < 32; i++) {
        printf("x: %d, y: %d, z: %d\n", x[i], y[i], z[i]);
    }

    // reset fifo
    lis2dw12_fifo_config_t fifo_config = {
            .mode = lis2dw12_fifo_bypass
    };
    lis2dw12_fifo_config(fifo_config);
    fifo_config.mode = lis2dw12_fifo_byp_to_cont;
    lis2dw12_fifo_config(fifo_config);
}

static void acc_wakeup_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {

    lis2dw12_read_full_fifo(x, y, z, acc_fifo_read_handler);
}

static void acc_init(void) {

    // Init GPIOTE, accelerometer interrupt
    nrfx_gpiote_init();
    nrfx_gpiote_in_config_t int_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(0);
    int_gpio_config.pull = NRF_GPIO_PIN_NOPULL;
    nrfx_gpiote_in_init(CARRIER_ACC_INT2, &int_gpio_config, acc_wakeup_handler);
    nrfx_gpiote_in_event_enable(CARRIER_ACC_INT2, 1);

    // Turn on accelerometer
    lis2dw12_init(&spi_instance);

    lis2dw12_int_config_t int_config = {0};
    int_config.int1_wakeup = 1;
    int_config.int2_fifo_full = 1;

    lis2dw12_reset();
    lis2dw12_config(config);
    lis2dw12_interrupt_config(int_config);
    lis2dw12_interrupt_enable(1);

    // Clear FIFO
    lis2dw12_fifo_config_t fifo_config;
    fifo_config.mode = lis2dw12_fifo_bypass;
    lis2dw12_fifo_config(fifo_config);
    fifo_config.mode = lis2dw12_fifo_byp_to_cont;
    lis2dw12_fifo_config(fifo_config);

    lis2dw12_wakeup_config(wake_config);
}


/*-----------------------------------------------------------------------*/
/* MAIN test                                                    		 */
/*-----------------------------------------------------------------------*/

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

    // FIXME: Delay startup (for probing with oscilloscope)
    nrf_delay_ms(2000);

    nrf_gpio_pin_set(CARRIER_LED_RED);
    nrf_gpio_pin_set(CARRIER_LED_GREEN);
    nrf_gpio_pin_clear(CARRIER_LED_BLUE);

    // Init UART
    uart_init();
    printf(", UART");

    // Init SPI
    spi_init();
    nrf_gpio_cfg_output(CARRIER_CS_SD);
    nrf_gpio_cfg_output(CARRIER_CS_ACC);
    nrf_gpio_pin_set(CARRIER_CS_SD);
    nrf_gpio_pin_set(CARRIER_CS_ACC);
    printf(", SPI");

    // Init SoftDevice & DCDC regulator
    nrf_sdh_enable_request();
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
    printf(" and SoftDevice...\n");


    // Test SD card ----------------------------------------------------------------------------------------------------
//#define TEST_SD_CARD
#ifdef TEST_SD_CARD
    printf("Testing SD card: ");

    const char filename[] = "testfile.log";
    const char permissions[] = "a"; // w = write, a = append

    // Start file
    simple_logger_init(filename, permissions);
    DEBUG("inited\r\n");

    // If no header, add it
    simple_logger_log_header("HEADER for %s file, written on %s", "FILENAME", "08/16/18");
    DEBUG("wrote header\r\n");

    // Write
    simple_logger_log("%s: Additional line added\n", "19:06");
    DEBUG("wrote line\r\n");

    printf("OK\r\n");
#endif

    // Test Accelerometer ----------------------------------------------------------------------------------------------
#define TEST_ACC
#ifdef TEST_ACC
    printf("Testing Accelerometer: ");

    // Initialize
    acc_init();

    // Gather readings
    int nr_readings = 10;
    printf("<gathering data>\n");

    for (int i = 0; i < nr_readings; i++) {
        ret_code_t err_code = sd_app_evt_wait();
        APP_ERROR_CHECK(err_code);
    }

    printf("Testing Accelerometer: OK\r\n");
#endif

    // Test LED --------------------------------------------------------------------------------------------------------
    printf("Testing LED: <blinks green>\r\n");

    while (1)
    {
        nrf_gpio_pin_toggle(CARRIER_LED_GREEN);
        nrf_delay_ms(500);
    }
}
