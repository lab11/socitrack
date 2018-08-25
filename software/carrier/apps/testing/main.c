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
#include "nrfx_power.h"
#include "nrf_gpiote.h"
#include "nrfx_gpiote.h"

// SPI
#include "nrf_spi_mngr.h"
#include "nrf_drv_spi.h"

// SoftDevice
#include "nrf_sdh.h"
#include "nrf_soc.h"

// BLE
#include "nrf_sdh_ble.h"
#include "ble_advdata.h"

// UART
#include "app_uart.h"
#include "nrf_uart.h"

// Clocks
#include "nrfx_rtc.h"
#include "nrf_drv_clock.h"

// Debug output
#include "SEGGER_RTT.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

// Custom libraries
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

// RTC0 is used by SoftDevice, RTC1 is used by app_timer
const nrfx_rtc_t rtc_instance = NRFX_RTC_INSTANCE(2);

// Function starting the internal LFCLK XTAL oscillator
/*static void lfclk_config(void) {
    ret_code_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);

    nrf_drv_clock_lfclk_request(NULL);
}*/

// Interrupt handler; currently not used
static void rtc_handler(nrfx_rtc_int_type_t int_type) {}

static void rtc_init(void) {

    // Initialize LFCLK if no SoftDevice available
    //lfclk_config();

    // Initialize RTC instance
    nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
    rtc_config.prescaler = 32; // Approximately 1000 Hz
    ret_code_t err_code = nrfx_rtc_init( &rtc_instance, &rtc_config, rtc_handler);
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
    nrf_gpio_cfg_output(CARRIER_SD_ENABLE);
    nrf_gpio_cfg_output(CARRIER_CS_SD);

    nrf_gpio_pin_set(CARRIER_SD_ENABLE);
    nrf_gpio_pin_set(CARRIER_CS_SD);

    // Initialize Real-time counter
    rtc_init();

    // Configs
    const char filename[] = "testfile.log";
    const char permissions[] = "a"; // w = write, a = append

    //printf("Waiting for SD card to be inserted...\n");

    // Wait for SC card
    while (!sd_card_inserted()) {};

    //printf("Detected SD card; trying to connect...\n");

    // Start file
    simple_logger_init(filename, permissions);

    // If no header, add it
    simple_logger_log_header("HEADER for file \'%s\', written on %s \n", filename, "08/23/18");
}

/*-----------------------------------------------------------------------*/
/* BLE functions                                                         */
/*-----------------------------------------------------------------------*/

#define DEVICE_NAME "TotTag"

#define APP_BLE_CONN_CFG_TAG            1                                  /**< A tag identifying the SoftDevice BLE configuration. */

#define NON_CONNECTABLE_ADV_INTERVAL    MSEC_TO_UNITS(100, UNIT_0_625_MS)  /**< The advertising interval for non-connectable advertisement (100 ms). This value can vary between 100ms to 10.24s). */

#define APP_BEACON_INFO_LENGTH          0x17                               /**< Total length of information advertised by the Beacon. */
#define APP_ADV_DATA_LENGTH             0x15                               /**< Length of manufacturer specific data in the advertisement. */
#define APP_DEVICE_TYPE                 0x02                               /**< 0x02 refers to Beacon. */
#define APP_MEASURED_RSSI               0xC3                               /**< The Beacon's measured RSSI at 1 meter distance in dBm. */
#define APP_COMPANY_IDENTIFIER          0x0059                             /**< Company identifier for Nordic Semiconductor ASA. as per www.bluetooth.org. */
#define APP_MAJOR_VALUE                 0x01, 0x02                         /**< Major value used to identify Beacons. */
#define APP_MINOR_VALUE                 0x03, 0x04                         /**< Minor value used to identify Beacons. */
#define APP_BEACON_UUID                 0x01, 0x12, 0x23, 0x34, \
                                        0x45, 0x56, 0x67, 0x78, \
                                        0x89, 0x9a, 0xab, 0xbc, \
                                        0xcd, 0xde, 0xef, 0xf0            /**< Proprietary UUID for Beacon. */

#define DEAD_BEEF                       0xDEADBEEF                         /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#if defined(USE_UICR_FOR_MAJ_MIN_VALUES)
#define MAJ_VAL_OFFSET_IN_BEACON_INFO   18                                 /**< Position of the MSB of the Major Value in m_beacon_info array. */
#define UICR_ADDRESS                    0x10001080                         /**< Address of the UICR register used by this example. The major and minor versions to be encoded into the advertising data will be picked up from this location. */
#endif

static ble_gap_adv_params_t m_adv_params;                                  /**< Parameters to be passed to the stack when starting advertising. */
static uint8_t              m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET; /**< Advertising handle used to identify an advertising set. */
static uint8_t              m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];  /**< Buffer for storing an encoded advertising set. */
static uint8_t              m_enc_srdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];  /**< Buffer for storing an encoded scan response set. */

// Struct that contains pointers to the encoded advertising data
static ble_gap_adv_data_t m_adv_data = {
    .adv_data =
    {
        .p_data = m_enc_advdata,
        .len    = BLE_GAP_ADV_SET_DATA_SIZE_MAX
    },
    .scan_rsp_data =
    {
        .p_data = m_enc_srdata,
        .len    = BLE_GAP_ADV_SET_DATA_SIZE_MAX

    }
};

// Information advertised by the Beacon
static uint8_t m_beacon_info[APP_BEACON_INFO_LENGTH] = {
        APP_DEVICE_TYPE,     // Manufacturer specific information. Specifies the device type in this implementation.
        APP_ADV_DATA_LENGTH, // Manufacturer specific information. Specifies the length of the manufacturer specific data in this implementation.
        APP_BEACON_UUID,     // 128 bit UUID value.
        APP_MAJOR_VALUE,     // Major arbitrary value that can be used to distinguish between Beacons.
        APP_MINOR_VALUE,     // Minor arbitrary value that can be used to distinguish between Beacons.
        APP_MEASURED_RSSI    // Manufacturer specific information. The Beacon's measured TX power in this implementation.
};

// Handler called in case of an assert in the SoftDevice
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

static void advertising_init(void) {
    uint32_t      err_code;
    ble_advdata_t advdata;
    ble_advdata_t srdata;
    uint8_t       flags = BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;

    // Set name
    ble_gap_conn_sec_mode_t sec_mode;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    err_code = sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    // Set manufacturer specific data
    ble_advdata_manuf_data_t manuf_specific_data;

    manuf_specific_data.company_identifier = APP_COMPANY_IDENTIFIER;

#if defined(USE_UICR_FOR_MAJ_MIN_VALUES)
    // If USE_UICR_FOR_MAJ_MIN_VALUES is defined, the major and minor values will be read from the
    // UICR instead of using the default values. The major and minor values obtained from the UICR
    // are encoded into advertising data in big endian order (MSB First).
    // To set the UICR used by this example to a desired value, write to the address 0x10001080
    // using the nrfjprog tool. The command to be used is as follows.
    // nrfjprog --snr <Segger-chip-Serial-Number> --memwr 0x10001080 --val <your major/minor value>
    // For example, for a major value and minor value of 0xabcd and 0x0102 respectively, the
    // the following command should be used.
    // nrfjprog --snr <Segger-chip-Serial-Number> --memwr 0x10001080 --val 0xabcd0102
    uint16_t major_value = ((*(uint32_t *)UICR_ADDRESS) & 0xFFFF0000) >> 16;
    uint16_t minor_value = ((*(uint32_t *)UICR_ADDRESS) & 0x0000FFFF);

    uint8_t index = MAJ_VAL_OFFSET_IN_BEACON_INFO;

    m_beacon_info[index++] = MSB_16(major_value);
    m_beacon_info[index++] = LSB_16(major_value);

    m_beacon_info[index++] = MSB_16(minor_value);
    m_beacon_info[index++] = LSB_16(minor_value);
#endif

    manuf_specific_data.data.p_data = (uint8_t *) m_beacon_info;
    manuf_specific_data.data.size   = APP_BEACON_INFO_LENGTH;

    // Build and set advertising data.
    memset(&advdata, 0, sizeof(advdata));

    advdata.name_type             = BLE_ADVDATA_NO_NAME;
    advdata.flags                 = flags;
    advdata.p_manuf_specific_data = &manuf_specific_data;

    // Build and set scan response data.
    memset(&srdata, 0, sizeof(srdata));
    srdata.name_type             = BLE_ADVDATA_FULL_NAME;

    // Initialize advertising parameters (used when starting advertising).
    memset(&m_adv_params, 0, sizeof(m_adv_params));

    m_adv_params.properties.type = BLE_GAP_ADV_TYPE_NONCONNECTABLE_SCANNABLE_UNDIRECTED;
    m_adv_params.p_peer_addr     = NULL;    // Undirected advertisement.
    m_adv_params.filter_policy   = BLE_GAP_ADV_FP_ANY;
    m_adv_params.interval        = NON_CONNECTABLE_ADV_INTERVAL;
    m_adv_params.duration        = 0;       // Never time out.

    // Set advertisement data
    err_code = ble_advdata_encode(&advdata, m_adv_data.adv_data.p_data, &m_adv_data.adv_data.len);
    APP_ERROR_CHECK(err_code);

    // Set scan response data
    err_code = ble_advdata_encode(&srdata, m_adv_data.scan_rsp_data.p_data, &m_adv_data.scan_rsp_data.len);
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &m_adv_params);
    APP_ERROR_CHECK(err_code);
}

static void advertising_start(void) {
    ret_code_t err_code;

    err_code = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    APP_ERROR_CHECK(err_code);
}

// Initialize BLE stack
static void ble_stack_init(void) {
    ret_code_t err_code;

    // Enable SoftDevice if not done so already
    //err_code = nrf_sdh_enable_request();
    //APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);
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
    // FIXME: DCDC mode not possible without RTT connected
    //nrf_power_dcdcen_set(1);
    nrf_sdh_enable_request();
    //sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
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
//#define TEST_SD_CARD
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

    // Initialize
    ble_stack_init();
    advertising_init();

    // Start execution.
    printf("Beacon example started...\n");
    advertising_start();

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
