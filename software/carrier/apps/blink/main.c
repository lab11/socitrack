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
#include "nrfx_rtc.h"
#include "nrf_drv_clock.h"
#include "SEGGER_RTT.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "boards.h"
#include "simple_logger.h"
#include "accelerometer_lis2dw12.h"


/*-----------------------------------------------------------------------*/
/* SPI & UART functions                                                	 */
/*-----------------------------------------------------------------------*/

// Use SPI0
#define SPI_INSTANCE_NR 0

// Use SPI manager
//NRF_SPI_MNGR_DEF(spi_instance, 5, SPI_INSTANCE_NR);
// Use SPI directly
static nrf_drv_spi_t spi_instance = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE_NR);

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
    //ret_code_t err_code = nrf_spi_mngr_init(&spi_instance, &spi_config);
    // Init SPI directly
    ret_code_t err_code = nrf_drv_spi_init(&spi_instance, &spi_config, NULL, NULL);
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

    // TODO: Configure Pull ups

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
/* Accelerometer functions                                               */
/*-----------------------------------------------------------------------*/

static int16_t x[32], y[32], z[32];

lis2dw12_wakeup_config_t acc_wake_config = {
        .sleep_enable   = 1,
        .threshold      = 0x05,
        .wake_duration  = 3,
        .sleep_duration = 2
};

lis2dw12_config_t acc_config = {
        .odr        = lis2dw12_odr_200,
        .mode       = lis2dw12_low_power,
        .lp_mode    = lis2dw12_lp_1,
        .cs_nopull  = 0,
        .bdu        = 1,
        .auto_increment = 1,
        .i2c_disable    = 1,
        .int_active_low = 0,
        .on_demand  = 1,
        .bandwidth  = lis2dw12_bw_odr_2,
        .fs         = lis2dw12_fs_4g,
        .high_pass  = 0,
        .low_noise  = 1,
};

static void acc_fifo_read_handler(void) {

    for(int i = 0; i < 32; i++) {
        printf("x: %d, y: %d, z: %d\n", x[i], y[i], z[i]);
    }

    // Reset FIFO
    //lis2dw12_fifo_reset();
}

static void acc_wakeup_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {

    lis2dw12_read_full_fifo(x, y, z, acc_fifo_read_handler);
}

static void acc_init(void) {

    // Turn on accelerometer
    lis2dw12_init(&spi_instance);

    lis2dw12_int_config_t acc_int_config = {0};
    acc_int_config.int1_wakeup = 1;
    acc_int_config.int2_fifo_full = 1;

    lis2dw12_reset();
    lis2dw12_config(acc_config);
    lis2dw12_interrupt_config(acc_int_config);
    lis2dw12_interrupt_enable(1);

    // Init GPIOTE, accelerometer interrupt
    nrfx_gpiote_init();
    nrfx_gpiote_in_config_t int_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(0);
    int_gpio_config.pull = NRF_GPIO_PIN_NOPULL;
    nrfx_gpiote_in_init(CARRIER_ACC_INT2, &int_gpio_config, acc_wakeup_handler);
    nrfx_gpiote_in_event_enable(CARRIER_ACC_INT2, 1);

    // Configure wakeups
    lis2dw12_wakeup_config(acc_wake_config);

    // Reset FIFO
    lis2dw12_fifo_reset();
}

/*-----------------------------------------------------------------------*/
/* SD card functions                                                     */
/*-----------------------------------------------------------------------*/

const nrfx_rtc_t rtc_instance = NRFX_RTC_INSTANCE(0);

// Function starting the internal LFCLK XTAL oscillator
static void lfclk_config(void)
{
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);

    nrf_drv_clock_lfclk_request(NULL);
}

static void rtc_init(void) {

    // Initialize LFCLK if no SoftDevice available
    //lfclk_config();

    // Initialize RTC instance
    nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
    rtc_config.prescaler = 32; // Approximately 1000 Hz
    ret_code_t err_code = nrfx_rtc_init( &rtc_instance, &rtc_config, NULL);
    APP_ERROR_CHECK(err_code);

    // Power on RTC instance
    nrfx_rtc_enable(&rtc_instance);
}


static bool sd_card_inserted() {

    // SD card inserted:    Pin connected to GND
    // SD card not present: Pin connected to VCC
    return !nrf_gpio_pin_read(CARRIER_SD_DETECT);
}

static void sd_card_init(void) {

    // Setup hardware
    nrf_gpio_cfg_input(CARRIER_SD_DETECT, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_input(CARRIER_SPI_MISO,  NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_output(CARRIER_SPI_MOSI);
    nrf_gpio_cfg_output(CARRIER_SPI_SCLK);
    nrf_gpio_cfg_output(CARRIER_CS_SD);

    nrf_gpio_pin_set(CARRIER_CS_SD);

    // Initialize Real-time counter
    rtc_init();

    // Configs
    const char filename[] = "testfile.log";
    const char permissions[] = "a"; // w = write, a = append

    printf("Waiting for SD card to be inserted...\n");

    // Wait for SC card
    while (!sd_card_inserted()) {};

    printf("Detected SD card; trying to connect...\n");

    // Start file
    simple_logger_init(filename, permissions);

    // If no header, add it
    simple_logger_log_header("HEADER for %s file, written on %s", filename, "08/23/18");
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

    // Init SoftDevice & DCDC regulator
    nrf_sdh_enable_request();
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
    printf(", SoftDevice");

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

    // Init SPI
    spi_init();
    nrf_gpio_cfg_output(CARRIER_CS_SD);
    nrf_gpio_cfg_output(CARRIER_CS_ACC);
    nrf_gpio_pin_set(CARRIER_CS_SD);
    nrf_gpio_pin_set(CARRIER_CS_ACC);
    printf(", SPI");

    // Init UART
    //uart_init();
    printf(" and UART...\n");


    // Test SD card ----------------------------------------------------------------------------------------------------
#define TEST_SD_CARD
#ifdef TEST_SD_CARD
    printf("Testing SD card: ");

    // Initialize
    sd_card_init();

    // Write sample lines
    int nr_lines = 5;
    for (int i = 0; i < nr_lines; i++) {

        uint32_t current_time_stamp = nrfx_rtc_counter_get(&rtc_instance);
        simple_logger_log("%i: Line %i - Additional line added\n", current_time_stamp, i+1);
    }

    printf("OK\r\n");
#endif

    // Test Accelerometer ----------------------------------------------------------------------------------------------
//#define TEST_ACC
#ifdef TEST_ACC
    printf("Testing Accelerometer: \n");

    // Initialize
    acc_init();

    // Gather readings
    int nr_readings = 5;
    printf("<gathering data>\n");

    for (int i = 0; i < nr_readings; i++) {

        ret_code_t err_code = sd_app_evt_wait();
        APP_ERROR_CHECK(err_code);

        printf("Received one set of data\n");
    }

    printf("Testing Accelerometer: OK\r\n");
#endif

    // Test BLE --------------------------------------------------------------------------------------------------------
#define TEST_BLE
#ifdef TEST_BLE
    printf("Testing BLE advertisements:");


    printf(" OK\n");
#endif

    // Test BLE --------------------------------------------------------------------------------------------------------
//#define TEST_MODULE
#ifdef TEST_MODULE
    printf("Testing module: ");


    printf(" OK\n");
#endif

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
