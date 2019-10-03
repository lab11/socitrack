/*

  UWB Localization Tag

*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_error.h"
#include "nrf_gpio.h"
#include "nrf_gpiote.h"
#include "nrfx_gpiote.h"
#include "nrfx_power.h"
#include "nrf_pwr_mgmt.h"
#include "nrfx_saadc.h"

// Apps
#include "app_error.h"
#include "app_util.h"
#include "app_timer.h"
#include "app_scheduler.h"

// SPI
#include "nrf_spi_mngr.h"
#include "nrf_drv_spi.h"

// SoftDevice
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_soc.h"

// BLE
#include "ble.h"
#include "ble_advertising.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_gap.h"
#include "ble_gatts.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "nrf_ble_es.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"

// UART
#include "app_uart.h"
#include "nrf_uart.h"
#include "nrf_drv_uart.h"

// Clocks
#include "nrfx_rtc.h"
#include "nrf_drv_clock.h"

// Debug output
#include "SEGGER_RTT.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

// Custom libraries
#include "accelerometer_lis2dw12.h"
#include "ble_config.h"
#include "boards.h"
#include "led.h"
#include "module_interface.h"
#include "rtc_ab1815.h"
#include "simple_logger.h"

/*******************************************************************************
 *   Static function declarations
 ******************************************************************************/

static void flush_sd_buffer();
static void write_to_sd(char * data, uint16_t length);

static void carrier_start_module(uint8_t role);
static void advertising_init();
static void on_adv_report(ble_gap_evt_adv_report_t const * p_adv_report);

/*******************************************************************************
 *   Application state
 ******************************************************************************/

// Main Application state ----------------------------------------------------------------------------------------------
static ble_app_t app;

// BLE state -----------------------------------------------------------------------------------------------------------

// Service UUID
const ble_uuid128_t CARRIER_BLE_SERV_LONG_UUID = {
        .uuid128 = {0x2e, 0x5d, 0x5e, 0x39, 0x31, 0x52, 0x45, 0x0c, 0x90, 0xee, 0x3f, 0xa2, 0x52, 0x31, 0x8c, 0xd6}
};
// Advertisement UUIDs: Eddystone
static ble_uuid_t m_adv_uuids[] = {
        {PHYSWEB_SERVICE_ID,                  BLE_UUID_TYPE_BLE}
};
// Scan Response UUIDs: Information service, Application Interface
static ble_uuid_t m_sr_uuids[] = {
        {BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE},
        {CARRIER_BLE_SERV_SHORT_UUID,         BLE_UUID_TYPE_VENDOR_BEGIN}
};

// Service and characteristics handle
static ble_gatts_char_handles_t carrier_ble_char_location_handle    = {.value_handle = CARRIER_BLE_CHAR_LOCATION};
static ble_gatts_char_handles_t carrier_ble_char_config_handle      = {.value_handle = CARRIER_BLE_CHAR_CONFIG};
static ble_gatts_char_handles_t carrier_ble_char_enable_handle      = {.value_handle = CARRIER_BLE_CHAR_ENABLE};
static ble_gatts_char_handles_t carrier_ble_char_status_handle      = {.value_handle = CARRIER_BLE_CHAR_STATUS};
static ble_gatts_char_handles_t carrier_ble_char_calibration_handle = {.value_handle = CARRIER_BLE_CHAR_CALIBRATION};
static uint16_t                 carrier_ble_service_handle          = 0;
static uint16_t                 carrier_ble_conn_handle             = BLE_CONN_HANDLE_INVALID;

static uint8_t  carrier_ble_address[6];
static uint16_t carrier_ble_device_id;

static ble_gap_scan_params_t const m_scan_params =
{
        .active            = 1,
        .interval          = APP_SCAN_INTERVAL,
        .window            = APP_SCAN_WINDOW,
        .timeout           = BLE_GAP_SCAN_TIMEOUT_UNLIMITED,
        .scan_phys         = BLE_GAP_PHY_1MBPS,
        .filter_policy     = BLE_GAP_SCAN_FP_ACCEPT_ALL, //Whitelist implemented in the application BLE handler
};

static ble_gap_conn_params_t const m_connection_param =
{
        MIN_CONN_INTERVAL,
        MAX_CONN_INTERVAL,
        SLAVE_LATENCY,
        CONN_SUP_TIMEOUT
};

static uint8_t m_scan_buffer_data[BLE_GAP_SCAN_BUFFER_MIN]; /**< Buffer where advertising reports will be stored by the SoftDevice. */

/** Pointer to the buffer where advertising reports will be stored by the SoftDevice. */
static ble_data_t m_scan_buffer =
{
        m_scan_buffer_data,
        BLE_GAP_SCAN_BUFFER_MIN
};

// Whitelisted addresses
ble_gap_addr_t pp_wl_addrs[APP_BLE_ADDR_NR];

// State defines -------------------------------------------------------------------------------------------------------

// GP Timer. Used to retry initializing the module.
APP_TIMER_DEF(watchdog_timer);

// GATT module instance
NRF_BLE_GATT_DEF(m_gatt);

// Context for the Queued Write module
NRF_BLE_QWR_DEF(m_qwr);

// Advertising module instance
BLE_ADVERTISING_DEF(m_advertising);


/*******************************************************************************
 *   Helper functions
 ******************************************************************************/

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

// Convert RTC ticks to milliseconds
uint32_t rtc_to_s(uint32_t ticks) {

    // If RTC returns actual ticks
    //return ticks / (NRFX_RTC_DEFAULT_CONFIG_FREQUENCY);

    // Using prescaler; ATTENTION: Prescaler is max 12bit, RTC max 24bit -> 24 days until clock overflows
    // Calculation: 32768 (2¹⁵) / (rtc_instance.p_reg->PRESCALER + 1) = 2¹⁵ / 2¹² = 2³ = 8
    return ticks / 8;
}

uint8_t ascii_to_i(uint8_t number) {

    if        ( (number >= '0') && (number <= '9')) {
        return (number - (uint8_t)'0');
    } else if ( (number >= 'A') && (number <= 'F')) {
        return (number - (uint8_t)'A' + (uint8_t)10);
    } else if ( (number >= 'a') && (number <= 'f')) {
        return (number - (uint8_t)'a' + (uint8_t)10);
    } else {
        printf("ERROR: Tried  converting non-hex ASCII: %i\n", number);
        return 0;
    }
}

bool addr_in_whitelist(ble_gap_addr_t const * ble_addr) {

    // Check whether device is in our whitelist
    for (uint16_t i = 0; i < APP_BLE_ADDR_NR; i++) {

        if (memcmp(&ble_addr->addr, pp_wl_addrs[i].addr, sizeof(pp_wl_addrs[i].addr)) == 0) {
            // Connect and signal discovery
            return true;
        }
    }

    return false;
}

bool node_is_master() {
    return (app.master_eui[0] == app.config.my_eui[0]);
}

uint32_t app_get_current_time() {

    uint32_t current_rtc_counter = rtc_to_s(nrfx_rtc_counter_get(&rtc_instance));

    uint32_t time = app.config.app_sync_time + (current_rtc_counter - app.config.app_sync_rtc_counter);

    //printf("DEBUG: Epoch time: %li; RTC counter: %li; Current time: %li; Result: %li\n", app.config.app_sync_time, app.config.app_sync_rtc_counter, rtc_to_s(nrfx_rtc_counter_get(&rtc_instance)), time);

    if ( (!app.config.app_sync_rtc_overflown)                   &&
         (current_rtc_counter < app.config.app_sync_rtc_counter)  ) {
        printf("WARNING: RTC overflow has occurred\n");

        // 24bit RTC has overflown - occurrence depends on prescaler
        app.config.app_sync_rtc_overflown = true;
        app.config.app_sync_rtc_overflow_counter++;

    } else if ( (app.config.app_sync_rtc_overflown)                    &&
                (current_rtc_counter > app.config.app_sync_rtc_counter)  ){
        printf("INFO: RTC overflow flag cleared, ready for next overflow\n");

        // Counter is again above the counter value where we sync'ed
        app.config.app_sync_rtc_overflown = false;
    }

    // Include overflow - check for efficiency when no overflow occurred so far (normal case for most deployments)
    if (app.config.app_sync_rtc_overflow_counter) {
        time += app.config.app_sync_rtc_overflow_counter * rtc_to_s(0x00FFFFFF);
    }

    return time;
}


/*******************************************************************************
 *   Buses: SPI, TWI & UART
 ******************************************************************************/

// Use SPI0
#define SPI_INSTANCE_NR 0

static nrf_drv_spi_t spi_instance = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE_NR);

void spi_init(void) {

    // Configure GPIOs
    nrf_gpio_cfg_input(CARRIER_SPI_MISO,  NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_output(CARRIER_SPI_MOSI);
    nrf_gpio_cfg_output(CARRIER_SPI_SCLK);

    // Setup Chip selects (CS)
    nrf_gpio_cfg_output(CARRIER_CS_SD);
    nrf_gpio_cfg_output(CARRIER_CS_ACC);
    nrf_gpio_pin_set(CARRIER_CS_SD);
    nrf_gpio_pin_set(CARRIER_CS_ACC);

#if (BOARD_V >= 0xF)
    nrf_gpio_cfg_output(CARRIER_CS_RTC);
    nrf_gpio_pin_set(CARRIER_CS_RTC);
#endif

    // Configure SPI lines
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


/*******************************************************************************
 *   Accelerometer functions
 ******************************************************************************/

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
        //printf("DEBUG: x: %d, y: %d, z: %d\n", x[i], y[i], z[i]);
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
    if (!nrfx_gpiote_is_init()) {
        ret_code_t err_code = nrfx_gpiote_init();
        APP_ERROR_CHECK(err_code);
    }

    nrfx_gpiote_in_config_t int_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(0);
    int_gpio_config.pull = NRF_GPIO_PIN_NOPULL;
    nrfx_gpiote_in_init(CARRIER_ACC_INT2, &int_gpio_config, acc_wakeup_handler);
    nrfx_gpiote_in_event_enable(CARRIER_ACC_INT2, 1);

    // Configure wakeups
    lis2dw12_wakeup_config(acc_wake_config);

    // Reset FIFO
    lis2dw12_fifo_reset();
}


/*******************************************************************************
 *  RTC functions
 ******************************************************************************/

static void rtc_external_init(void) {

#if (BOARD_V >= 0xF)
    // Startup RTC
    ab1815_init(&spi_instance);

    static ab1815_control_t ctrl_config = {
            .stop      = 0,
            .hour_12   = 0,
            .OUTB      = 0,
            .OUT       = 0,
            .rst_pol   = 0,
            .auto_rst  = 1,
            .write_rtc = 1,
            .psw_nirq2_function = 0,
            .fout_nirq_function = 0
    };
    static ab1815_int_config_t int_config = {
            .century_en = 1,
            .int_mode   = 0x3,
            .bat_low_en = 0,
            .timer_en   = 0,
            .alarm_en   = 0,
            .xt2_en     = 0,
            .xt1_en     = 0
    };

    // Set configs
    ab1815_set_config(ctrl_config);
    ab1815_set_int_config(int_config);

    // Initialize time - only done once after first flash of code
    ab1815_init_time();

    // Set our own time on the NRF
    struct timeval tv = ab1815_get_time_unix();
    app.config.app_sync_time        = tv.tv_sec;
    app.config.app_sync_rtc_counter = rtc_to_s(nrfx_rtc_counter_get(&rtc_instance));
    app.config.app_sync_rtc_overflow_counter = 0;
    app.config.app_sync_rtc_overflown        = false;
    ab1815_printTime(unix_to_ab1815(tv));
#else
    printf("INFO: Skipping RTC as compiling for older board (Version < revF)\n");
#endif
}


/*******************************************************************************
 *  SD card functions
 ******************************************************************************/

static bool sd_card_inserted() {

    // SD card inserted:    Pin connected to GND
    // SD card not present: Pin connected to VCC
    return !nrf_gpio_pin_read(CARRIER_SD_DETECT);
}

// Configs - Filename must NOT be longer than 8 characters + '.log', otherwise the file will not be written (due to FAT limitations)
const char sd_filename[]    = "data.log";
const char sd_permissions[] = "a,r"; // w = write, a = append, r = read

static void sd_card_init(void) {

    // Setup hardware
    nrf_gpio_cfg_input(CARRIER_SD_DETECT, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_output(CARRIER_SD_ENABLE);

    nrf_gpio_pin_set(CARRIER_SD_ENABLE);
    nrf_gpio_pin_set(CARRIER_CS_SD);

#ifdef APP_SD_REQUIRED
    if (!sd_card_inserted()) {
        printf("WARNING: Waiting for SD card to be inserted...\n");

        // Wait for SC card
        while (!sd_card_inserted()) {};

        printf("INFO: Detected SD card; trying to connect...\n");
    }
#endif

    // Start file
    simple_logger_init(sd_filename, sd_permissions);

    // If no header, add it
    simple_logger_log_header("### HEADER for file \'%s\', written on %s\n", sd_filename, "11/03/18");
}


static void flush_sd_buffer() {

    if(!sd_card_inserted()) {
        printf("ERROR: SD card not inserted!\n");
        return;
    }

    // Enable and select SD card
    simple_logger_power_on();

    // Appending the string terminator
    if ( (app.app_sdcard_buffer_length + 1) <= APP_SDCARD_BUFFER_LENGTH) {
        app.app_sdcard_buffer[app.app_sdcard_buffer_length] = '\0';
    } else {
        app.app_sdcard_buffer[APP_SDCARD_BUFFER_LENGTH - 1] = '\0';
        printf("WARNING: Overwriting buffer data!\n");
    }

    // Send data in chunks of 254 bytes, as this is the maximum which the nRF DMA can handle ( + 1 for '\0')
#define APP_LOG_CHUNK_SIZE  254

    uint8_t nr_writes = app.app_sdcard_buffer_length / APP_LOG_CHUNK_SIZE;

    if (app.app_sdcard_buffer_length % APP_LOG_CHUNK_SIZE) {
        nr_writes++; // Add another write if the integer division does not result exactly in complete chunks (basically CEIL)
    }

    //printf("DEBUG: Writing %i chunks of length %i to SD card\n", nr_writes, APP_LOG_CHUNK_SIZE);

    char write_buf[APP_LOG_CHUNK_SIZE + 1] = { 0 };

    for (uint8_t i = 0; i < nr_writes; i++) {

        if (i == (nr_writes - 1)) {
            // Last chunk
            uint8_t rest_length = app.app_sdcard_buffer_length - i * APP_LOG_CHUNK_SIZE;
            memcpy(write_buf, app.app_sdcard_buffer + i * APP_LOG_CHUNK_SIZE, rest_length);
            write_buf[rest_length] = '\0';
        } else {
            // Full chunks
            memcpy(write_buf, app.app_sdcard_buffer + i * APP_LOG_CHUNK_SIZE, APP_LOG_CHUNK_SIZE);
            write_buf[APP_LOG_CHUNK_SIZE] = '\0';
        }

        // Write data
        uint8_t ret_val = simple_logger_log("%s", write_buf);
        if (ret_val) {
            printf("WARNING: Received return code %i when trying to write to SD card!\n", ret_val);
        }
    }

    // Reset buffer
    memset( app.app_sdcard_buffer, 0, sizeof(app.app_sdcard_buffer));
    app.app_sdcard_buffer_length = 0;

    //printf("INFO: Successfully flushed buffer to SD card\n");

    // Turn off power to SD card again
    nrf_gpio_pin_clear(CARRIER_SD_ENABLE);
}

// Write to SD card by buffering locally and then writing larger chunks of data
static void write_to_sd(char * data, uint16_t length) {

    if ( (APP_SDCARD_BUFFER_LENGTH - app.app_sdcard_buffer_length) < length) {
        printf("ERROR: Insufficient buffer space left! Available %i, used %i, required %i\n", APP_SDCARD_BUFFER_LENGTH, app.app_sdcard_buffer_length, length);
        return;
    }

    // Append to buffer
    memcpy(app.app_sdcard_buffer + app.app_sdcard_buffer_length, data, length);
    app.app_sdcard_buffer_length += length;

    // If the buffer has less than the specified number of free space left, flush it
    if ( (APP_SDCARD_BUFFER_LENGTH - app.app_sdcard_buffer_length) < APP_SDCARD_MIN_BUFFER_SPACE) {
        flush_sd_buffer();
    }
}

static void log_ranges(const uint8_t* data, uint16_t length) {

#define APP_LOG_BUFFER_LINE     (10 + 1 + 2*8 + 7 + 1 + 6 + 1)
#define APP_LOG_BUFFER_LENGTH   (10 * APP_LOG_BUFFER_LINE)
#define APP_LOG_RANGE_LENGTH    (1 + 4)

    char log_buf[APP_LOG_BUFFER_LENGTH] = { 0 };
    uint16_t offset_data = 0;
    uint16_t offset_buf  = 0;

    // Jump over Interrupt reason
    offset_data += 1;

    uint8_t num_ranges = data[1];
    offset_data += 1;

    if ( ( ((length - 2) / APP_LOG_RANGE_LENGTH) != num_ranges) && ( ((length - 6) / APP_LOG_RANGE_LENGTH) != num_ranges) ) {
        printf("WARNING: Incorrect number of ranges!\n");
    }

    // Get current time
    uint32_t current_time_stamp = app_get_current_time();
    //printf("DEBUG: Current time: %010lu\n", current_time_stamp);

    for (uint8_t i = 0; i < num_ranges; i++) {

        // Add Timestamp
        sprintf(log_buf + offset_buf + 0, "%010lu\t", current_time_stamp);

        // Add EUI
        sprintf(log_buf + offset_buf + 11, "c0:98:e5:42:00:00:00:%02x\t", data[offset_data + 0]);

        // Add range - Little endian notation
        uint32_t range = data[offset_data + 1] + (data[offset_data + 2] << 1*8) + (data[offset_data + 3] << 2*8) + (data[offset_data + 4] << 3*8);

        if (range >= 1000000) {
            range = 0; // Negative ranges must be filtered, as they will print more than 6 characters
        }

        sprintf(log_buf + offset_buf + 35, "%06lu\n", range);

        offset_data += APP_LOG_RANGE_LENGTH;
        offset_buf  += APP_LOG_BUFFER_LINE;
    }

    // Write to SD
    write_to_sd(log_buf, offset_buf);

    //printf("INFO: Logged ranging with %i ranges to SD card\n", num_ranges);
}

// Add number of measurement in front of data
static uint16_t measurement_counter = 0;

static void log_ranges_raw(const uint8_t* data, uint16_t length) {

#define APP_LOG_RAW_BUFFER_LINE     (10 + 1 + 6 + 1 + 2 + 1 + 2 + 1 + 10 + 1)
#define APP_LOG_RAW_BUFFER_LENGTH   (30 * APP_LOG_RAW_BUFFER_LINE)
#define APP_LOG_RAW_RANGE_LENGTH    (4)

    char log_buf[APP_LOG_RAW_BUFFER_LENGTH] = { 0 };
    uint16_t offset_data = 0;
    uint16_t offset_buf  = 0;

    // Jump over Interrupt reason
    offset_data += 1;

    uint8_t num_ranges = 30;

    if ( ( ((length - 1) / APP_LOG_RAW_RANGE_LENGTH) % (1 + num_ranges) != 0) && ( ((length - 1 - sizeof(uint32_t)) / APP_LOG_RAW_RANGE_LENGTH) % (1 + num_ranges) != 0) ) {
        printf("WARNING: Incorrect number of ranges!");
    }

    // Get current time
    uint32_t current_time_stamp = app_get_current_time();
    //printf("DEBUG: Current time: %010lu\n", current_time_stamp);

    uint8_t num_responses;
    if ( (length - 1) % (1 + num_ranges) == 0) {
        // No epoch
        num_responses = (length - 1) / (APP_LOG_RAW_RANGE_LENGTH * (1 + num_ranges));
    } else {
        // With epoch
        num_responses = (length - 1 - sizeof(uint32_t)) / (APP_LOG_RAW_RANGE_LENGTH * (1 + num_ranges));
    }

    for (uint8_t j = 0; j < num_responses; j++) {

        uint8_t node_id = data[offset_data];
        offset_data += APP_LOG_RAW_RANGE_LENGTH;

        for (uint8_t i = 0; i < num_ranges; i++) {

            // Add Timestamp
            sprintf(log_buf + offset_buf + 0, "%010lu\t", current_time_stamp);

            // Add measurement number
            sprintf(log_buf + offset_buf + 11, "%06u\t", measurement_counter);

            // Add node ID
            sprintf(log_buf + offset_buf + 18, "%02u\t", node_id);

            // Add channel number
            sprintf(log_buf + offset_buf + 21, "%02u\t", i);

            // Add range - Little endian order
            uint32_t range = data[offset_data + 0] + (data[offset_data + 1] << 1 * 8) + (data[offset_data + 2] << 2 * 8) + (data[offset_data + 3] << 3 * 8);
            sprintf(log_buf + offset_buf + 24, "%010lu\n", range);

            offset_data += APP_LOG_RAW_RANGE_LENGTH;
            offset_buf += APP_LOG_RAW_BUFFER_LINE;
        }

        // Write to SD
        write_to_sd(log_buf, offset_buf);

        printf("INFO: Logged raw ranges from %02u to SD card\n", node_id);
        measurement_counter++;

        // Reset buffer again
        memset(log_buf, 0, sizeof(log_buf));
        offset_buf = 0;
    }

}

/* Application state size
 *
 * Prefix:                8 +  15
 *
 * Config:               10 +   3
 * Module_inited:         1 +   1
 * Calibration_index:     2 +   1
 * Current_location:     12 +   6
 *
 * Suffix:                8 +  19
 *
 * Total:                41 +  45 = 86
 *
 * ATTENTION: Make sure this is <= 255 bytes (max length of SPI transactions); otherwise, consider splitting it into multiple transactions
 */
#define APP_STATE_LENGTH 86

// Write application state to non-volatile memory
static void backup_app_state() {

    char state_buffer[APP_STATE_LENGTH + 1];
    memset(state_buffer, 0, sizeof(state_buffer));
    uint16_t buf_offset = 0;

    uint32_t curr_time = app.config.app_sync_time;

    // TODO: Add new state variables to function

    // Flush SD card buffer first
    flush_sd_buffer();

    // Start filling buffer

    // Prefix
    sprintf(state_buffer + buf_offset, "# APP STATE %08lx #\n", curr_time);
    buf_offset += 12 + 8 + 3;

    // Write state
    sprintf(state_buffer + buf_offset, "%01x\t%08lx\t%01x\n", app.config.app_role, app.config.app_sync_time, app.config.app_module_enabled); // Config
    buf_offset += 2 + 9 + 2;
    sprintf(state_buffer + buf_offset, "%01x\n", app.module_inited);
    buf_offset += 2;
    sprintf(state_buffer + buf_offset, "%02x\n", app.calibration_index);
    buf_offset += 3;

    for (uint8_t i = 0; i < 6; i++) {
        if (i < 5) {
            sprintf(state_buffer + buf_offset, "%02x\t", app.current_location[i]);
        } else {
            sprintf(state_buffer + buf_offset, "%02x\n", app.current_location[i]);
        }
        buf_offset += 3;
    }

    // Temporary data is not maintained
    /*sprintf(state_buffer + buf_offset, "%01x\n", app.app_raw_response_buffer_updated);
    buf_offset += 2;
    sprintf(state_buffer + buf_offset, "%04x\n", app.app_raw_response_length);
    buf_offset += 5;

    for (uint8_t i = 0; i < 128; i++) {
        if (i < 127) {
            sprintf(state_buffer + buf_offset, "%02x\t", app.app_raw_response_buffer[i]);
        } else {
            sprintf(state_buffer + buf_offset, "%02x\n", app.app_raw_response_buffer[i]);
        }
        buf_offset += 3;
    }*/

    // Suffix
    sprintf(state_buffer + buf_offset, "# END APP STATE %08lx #\n", curr_time);

    // Write to SD card
    state_buffer[APP_STATE_LENGTH] = '\0';
    //printf           ("%s", state_buffer);
    simple_logger_log("%s", state_buffer);

    printf("INFO: Backed up Application state to SD card\n");
}

// Read application state from non-volatile memory
static void restore_app_state() {

    uint8_t state_buffer[APP_STATE_LENGTH];
    memset(state_buffer, 0, sizeof(state_buffer));
    uint16_t buf_offset = 0;

    // Read state form SD card
    simple_logger_read(state_buffer, APP_STATE_LENGTH);

    // Parse buffer
    buf_offset += 12;
    uint32_t curr_time = ascii_to_i(state_buffer[buf_offset + 0]) << 28;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 1]) << 24;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 2]) << 20;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 3]) << 16;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 4]) << 12;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 5]) <<  8;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 6]) <<  4;
    curr_time +=         ascii_to_i(state_buffer[buf_offset + 7]);
    buf_offset += 8;
    printf("%.12s%08lx%.3s", state_buffer, curr_time, state_buffer + buf_offset);
    buf_offset += 3;

    app.config.app_role = ascii_to_i(state_buffer[buf_offset]);
    buf_offset += 2;
    app.config.app_sync_time =  ascii_to_i(state_buffer[buf_offset + 0]) << 28;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 1]) << 24;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 2]) << 20;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 3]) << 16;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 4]) << 12;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 5]) <<  8;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 6]) <<  4;
    app.config.app_sync_time += ascii_to_i(state_buffer[buf_offset + 7]);
    buf_offset += 9;
    app.config.app_module_enabled = ascii_to_i(state_buffer[buf_offset]);
    buf_offset += 2;
    printf("%01x\t%08lx\t%01x\n", app.config.app_role, app.config.app_sync_time, app.config.app_module_enabled); // Config

    app.module_inited = ascii_to_i(state_buffer[buf_offset]);
    buf_offset += 2;
    printf("%01x\n", app.module_inited);

    app.calibration_index = (ascii_to_i(state_buffer[buf_offset]) << 4) + ascii_to_i(state_buffer[buf_offset + 1]);
    buf_offset += 3;
    printf("%02x\n", app.calibration_index);

    for (uint8_t i = 0; i < 6; i++) {
        app.current_location[i] = (ascii_to_i(state_buffer[buf_offset]) << 4) + ascii_to_i(state_buffer[buf_offset + 1]);

        if (i < 5) {
            printf("%02x\t", app.current_location[i]);
        } else {
            printf("%02x\n", app.current_location[i]);
        }
        buf_offset += 3;
    }

    // Temporary data is not maintained
    /*app.app_raw_response_buffer_updated = ascii_to_i(state_buffer[buf_offset]);
    buf_offset += 2;
    printf("%01x\n", app.app_raw_response_buffer_updated);

    app.app_raw_response_length = (ascii_to_i(state_buffer[buf_offset]) << 12) + (ascii_to_i(state_buffer[buf_offset + 1]) << 8) + (ascii_to_i(state_buffer[buf_offset + 2]) << 4) + ascii_to_i(state_buffer[buf_offset + 3]);
    buf_offset += 5;
    printf("%04x\n", app.app_raw_response_length);

    for (uint8_t i = 0; i < 128; i++) {
        app.app_raw_response_buffer[i] = (ascii_to_i(state_buffer[buf_offset]) << 4) + ascii_to_i(state_buffer[buf_offset + 1]);

        if (i < 127) {
            printf("%02x\t", app.app_raw_response_buffer[i]);
        } else {
            printf("%02x\n", app.app_raw_response_buffer[i]);
        }
        buf_offset += 3;
    }*/
    app.app_raw_response_buffer_updated = 0;
    app.app_raw_response_length = 0;

    for (uint8_t i = 0; i < 128; i++) {
        app.app_raw_response_buffer[i] = 0;
    }

    printf("INFO: Restored Application state from SD card\n");
}


/*******************************************************************************
 *   Analog pins
 ******************************************************************************/
#define APP_BAT_VOLT_MAX        4200
#define APP_BAT_VOLT_DIV_UP     100
#define APP_BAT_VOLT_DIV_DOWN   22

const uint8_t bat_monitor_channel = 0;

void saadc_callback(nrfx_saadc_evt_t const * p_event) {

    // Dont use event handler, so this should never actually be called
    printf("WARNING: Received an SAADC event\n");
}

void bat_monitor_init() {
    ret_code_t err_code;

    nrfx_saadc_config_t        saadc_config         = NRFX_SAADC_DEFAULT_CONFIG;
    nrf_saadc_channel_config_t saadc_channel_config = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(CARRIER_BATTERY_PIN);

    // Enable SAADC
    err_code = nrfx_saadc_init(&saadc_config, saadc_callback);
    APP_ERROR_CHECK(err_code);

    // Enable comparison channel
    err_code = nrfx_saadc_channel_init(bat_monitor_channel, &saadc_channel_config);
    APP_ERROR_CHECK(err_code);
}

// Returns the current battery voltage in 1/100 of V
uint16_t get_battery_level() {

    // Get voltage reading
    nrf_saadc_value_t adc_sample;
    ret_code_t err_code = nrfx_saadc_sample_convert(bat_monitor_channel, &adc_sample);

    if (err_code != NRF_SUCCESS) {
        // ADC is busy
        printf("WARNING: ADC is busy\n");
        return 0;
    }

    uint16_t voltage_sample = adc_sample * 360 / (0x3FF); // 1/100 of V, based on reference voltage 0.6V with a gain of 1/6 and 10 bit return value

    // Scale it by the voltage dividing circuit
    uint16_t voltage = voltage_sample * (APP_BAT_VOLT_DIV_UP + APP_BAT_VOLT_DIV_DOWN) / APP_BAT_VOLT_DIV_DOWN;

    printf("INFO: Battery voltage level: ADC %i, BATIN %i, VBAT %i\n", adc_sample, voltage_sample, voltage);

    return voltage;
}


/*******************************************************************************
 *   CALLBACKS - In response to various BLE & hardware events
 ******************************************************************************/

//Callback function for asserts in the SoftDevice.
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    // Use better_error_handling.c
    /*printf("ERROR: assert in SoftDevice failed!\n");
    
    while(1) { led_toggle(CARRIER_LED_RED); }*/
    
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

// Function for handling the WRITE CHARACTERISTIC BLE event.
void on_ble_write(const ble_evt_t* p_ble_evt)
{
    const ble_gatts_evt_write_t *p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    if (p_evt_write->handle == carrier_ble_char_config_handle.value_handle) {

        // Handle a write to the characteristic that configures the device in terms of role and what epoch time we have currently

        // Parse input
        uint8_t len = p_evt_write->len;
        printf("INFO: Received CONFIG evt: %s, length %i\n", (const char*)p_evt_write->data, len);

        const char expected_response_role[] = "Role: ";
        const uint8_t expected_response_role_offset = 6;
        const uint8_t role_length = 1;

        uint8_t response_role = ascii_to_i(p_evt_write->data[expected_response_role_offset]);

        const char expected_response_time[] = "; Time: ";
        const uint8_t expected_response_time_offset = expected_response_role_offset + role_length + 8;
        const uint8_t time_length = 10; // At compilation time, epoch time is 1536358069927 (in ms); we only store in s, as else we cannot use uint32!

        uint32_t response_time = 0;
        for (int i = expected_response_time_offset; i < (expected_response_time_offset + time_length); i++) {
            response_time = 10*response_time + ascii_to_i(p_evt_write->data[i]); // Add another cipher
        }

        // Apply configurations

        // ROLE: Decide on what to turn on depending on role
        app.config.app_role = response_role;

        switch(app.config.app_role)
        {
            case APP_ROLE_INIT_RESP: {
                printf("INFO: Setting node to HYBRID\n");

                break;
            }
            case APP_ROLE_INIT_NORESP: {
                printf("INFO: Setting node to TAG\n");

                break;
            }
            case APP_ROLE_NOINIT_RESP: {
                printf("INFO: Setting node to ANCHOR\n");

                break;
            }
            case APP_ROLE_NOINIT_NORESP: {
                printf("INFO: Setting node to SUPPORT\n");

                break;
            }
            default:
                printf("INFO: Setting node to DEFAULT\n");

        }

        // TIME: Setup time
        app.config.app_sync_time        = response_time;
        app.config.app_sync_rtc_counter = rtc_to_s(nrfx_rtc_counter_get(&rtc_instance));
        app.config.app_sync_rtc_overflow_counter = 0;
        app.config.app_sync_rtc_overflown        = false;
        printf("INFO: Set config time: %lu\n", app.config.app_sync_time);

    } else if (p_evt_write->handle == carrier_ble_char_enable_handle.value_handle) {

        // Handle a write to the characteristic that is used for enabling and disabling functionality
        uint8_t len = p_evt_write->len;
        printf("INFO: Received ENABLE evt: %s, length %i\n", (const char*)p_evt_write->data, len);

        const char expected_response_ranging[] = "Ranging: ";
        const uint8_t expected_response_ranging_offset = 9;
        const uint8_t ranging_length = 1;

        uint8_t response_ranging = ascii_to_i(p_evt_write->data[expected_response_ranging_offset]);

        // RANGING
        app.config.app_module_enabled = response_ranging;

        // Stop or start the module based on the value we just got
        if (app.config.app_module_enabled) {

            // Main loop will now start the module if it is not running yet
            if (app.config.app_module_running) {
                printf("WARNING: Module already running!\n");
            } else {
                printf("INFO: Starting module...\n");
            }

        } else {

            printf("INFO: Stopping module...\n");

            // Do not stop advertisements, as we want to reconfigure it afterwards and need them for it!
            // Disabling the module will not allow wake-ups though

            // Stop the module
            // ATTENTION: This must occur outside of the callback function, as otherwise, the Tx callback will never be triggered
        }

    } else if (p_evt_write->handle == carrier_ble_char_status_handle.value_handle) {

        // Handle a write to the characteristic that is used for setting the status of the device
        uint8_t len = p_evt_write->len;
        printf("INFO: Received STATUS evt: %s, length %i\n", (const char*)p_evt_write->data, len);

        const char expected_response_state[] = "Backup: ";
        const uint8_t expected_response_state_offset = 8;
        const uint8_t state_length = 1;

        uint8_t response_state = ascii_to_i(p_evt_write->data[expected_response_state_offset]);

        // Write current application state to SD card
        if (response_state) {
            backup_app_state();
        } else {
            restore_app_state();
        }

    } else if (p_evt_write->handle == carrier_ble_char_calibration_handle.value_handle) {

        // Handle a write to the characteristic that starts calibration
        uint8_t len = p_evt_write->len;
        printf("INFO: Received CALIBRATION evt: %s, length %i\n", (const char*)p_evt_write->data, len);

        const char expected_response_calib[] = "Calibration: ";
        const uint8_t expected_response_calib_offset = 13;

        uint8_t response = ascii_to_i(p_evt_write->data[expected_response_calib_offset]);

        app.calibration_index = response;

        // Calibration will be triggered in the main loop

    } else {
        printf("ERROR: Unknown handle: %i\n", p_evt_write->handle);
    }
}

// Function for handling BLE events
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    ret_code_t err_code;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTS_EVT_WRITE: {
            // Decide which characteristics is used
            on_ble_write(p_ble_evt);
        }
        /*case BLE_GAP_EVT_SEC_PARAMS_REQUEST: {
            // Pairing not supported
            err_code = sd_ble_gap_sec_params_reply(p_ble_evt->evt.common_evt.conn_handle,
                                                   BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,
                                                   NULL,
                                                   NULL);
            APP_ERROR_CHECK(err_code);
            break;
        }*/
        case BLE_GATTS_EVT_SYS_ATTR_MISSING: {
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(p_ble_evt->evt.common_evt.conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GAP_EVT_CONNECTED: {
#ifndef NRF_DISABLE_LEDS
            led_on(CARRIER_LED_GREEN);
            led_off(CARRIER_LED_BLUE);
#endif
            NRF_LOG_INFO("Connected.");

            carrier_ble_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, carrier_ble_conn_handle);
            APP_ERROR_CHECK(err_code);

            // Continue advertising, but nonconnectably
            m_advertising.adv_params.properties.type = BLE_GAP_ADV_TYPE_NONCONNECTABLE_SCANNABLE_UNDIRECTED;

            // Notice that ble_advertising_start() IGNORES some input parameters and sets them to defaults
            err_code = sd_ble_gap_adv_set_configure(&m_advertising.adv_handle, m_advertising.p_adv_data, &m_advertising.adv_params);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }
            err_code = sd_ble_gap_adv_start(m_advertising.adv_handle, m_advertising.conn_cfg_tag);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }

            // Connected to device. Set initial CCCD attributes to NULL
            err_code = sd_ble_gatts_sys_attr_set(carrier_ble_conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);

            // Check whether another tag; if yes, start module
            if (addr_in_whitelist(&p_ble_evt->evt.gap_evt.params.connected.peer_addr)) {
                if (!app.network_discovered) {
                    printf("INFO: Discovered other device in proximity\n");
                    app.network_discovered = true;
                }

                // Disconnect again; this direct disconnect can cause NRF_ERROR_INVALID_STATE for the sd_ble_gatts_exchange_mtu_reply() function, as it does not exchange in time
                err_code = sd_ble_gap_disconnect(carrier_ble_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
                APP_ERROR_CHECK(err_code);
            }

            break;
        }
        case BLE_GAP_EVT_DISCONNECTED: {
#ifndef NRF_DISABLE_LEDS
            // LED indication will be changed when advertising starts.
            led_on(CARRIER_LED_BLUE);
            led_off(CARRIER_LED_GREEN);
#endif
            NRF_LOG_INFO("Disconnected.");
            carrier_ble_conn_handle = BLE_CONN_HANDLE_INVALID;

            // Go back to advertising connectably
            m_advertising.adv_params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;

            // Restart advertisements
            err_code = sd_ble_gap_adv_set_configure(&m_advertising.adv_handle, m_advertising.p_adv_data, &m_advertising.adv_params);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }
            err_code = sd_ble_gap_adv_start(m_advertising.adv_handle, m_advertising.conn_cfg_tag);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }

            // Re-start scanning, as connecting automatically stops this
            //err_code = sd_ble_gap_scan_stop();
            //APP_ERROR_CHECK(err_code);

#ifndef APP_BLE_CALIBRATION
            err_code = sd_ble_gap_scan_start(&m_scan_params, &m_scan_buffer);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }
#endif

            break;
        }
        case BLE_GAP_EVT_ADV_REPORT: {
            /*ble_gap_addr_t const * peer_addr = &p_ble_evt->evt.gap_evt.params.adv_report.peer_addr;
            printf("DEBUG: Discovered another device with address %02x:%02x:%02x:%02x:%02x:%02x\n", peer_addr->addr[5],
                                                                                             peer_addr->addr[4],
                                                                                             peer_addr->addr[3],
                                                                                             peer_addr->addr[2],
                                                                                             peer_addr->addr[1],
                                                                                             peer_addr->addr[0]);*/

            on_adv_report(&p_ble_evt->evt.gap_evt.params.adv_report);

            break;
        }
        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST: {
            // Accept parameters requested by peer.
            err_code = sd_ble_gap_conn_param_update(p_ble_evt->evt.gap_evt.conn_handle,
                                                    &p_ble_evt->evt.gap_evt.params.conn_param_update_request.conn_params);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GAP_EVT_TIMEOUT: {
            // We have not specified a timeout for scanning, so only connection attemps can timeout.
            if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN) {
                printf("WARNING: Connection attempts timed out\n");
            }
            break;
        }
        case BLE_GATTC_EVT_TIMEOUT: {
            // Disconnect on GATT Client timeout event.
            NRF_LOG_DEBUG("GATT Client Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GATTS_EVT_TIMEOUT: {
            // Disconnect on GATT Server timeout event.
            NRF_LOG_DEBUG("GATT Server Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;
        }
        default:
            // No implementation needed.
            break;
    }
}

// Handles advertising events
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    ret_code_t err_code;

    switch (ble_adv_evt) {
        case BLE_ADV_EVT_FAST: {
            //NRF_LOG_INFO("Fast advertising.");
            break;
        }
        case BLE_ADV_EVT_IDLE: {
            NRF_LOG_INFO("Application is idle.");

            // Start advertisements
            err_code = sd_ble_gap_adv_set_configure(&m_advertising.adv_handle, m_advertising.p_adv_data, &m_advertising.adv_params);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }
            err_code = sd_ble_gap_adv_start(m_advertising.adv_handle, m_advertising.conn_cfg_tag);
            if (err_code != NRF_ERROR_INVALID_STATE) {
                APP_ERROR_CHECK(err_code);
            }
            break;
        }
        default:
            break;
    }
}

static uint32_t adv_report_parse(uint8_t type, const ble_data_t * p_advdata, ble_data_t * p_typedata)
{
    uint32_t index = 0;
    uint8_t * p_data;

    p_data = p_advdata->p_data;

    while (index < p_advdata->len)
    {
        uint8_t field_length = p_data[index];
        uint8_t field_type   = p_data[index + 1];

        if (field_type == type)
        {
            p_typedata->p_data   = &p_data[index + 2];
            p_typedata->len      = field_length;
            return NRF_SUCCESS;
        } else {
            index += field_length + 1;
        }
    }
    return NRF_ERROR_NOT_FOUND;
}

static void on_device_discovery(ble_gap_addr_t const * peer_addr)
{
    ret_code_t err_code;

    printf("DEBUG: Discovered address: %02x:%02x:%02x:%02x:%02x:%02x\n", peer_addr->addr[5],
                                                                         peer_addr->addr[4],
                                                                         peer_addr->addr[3],
                                                                         peer_addr->addr[2],
                                                                         peer_addr->addr[1],
                                                                         peer_addr->addr[0]);

    if(app.network_discovered) {
        // We already know that we are in the network
        return;
    } else {
        // Init module ourselves in the main loop
        app.network_discovered = true;
    }

    // Connect to device
    // TODO: Enable this again and communicate the Master EUI to the other device; until this is possible, we only enable networking upon advertisement discovery
#ifdef PROTOCOL_JOIN_ON_CONNECT
    err_code = sd_ble_gap_connect(peer_addr, &m_scan_params, &m_connection_param, APP_BLE_CONN_CFG_TAG);
    APP_ERROR_CHECK(err_code);
#else
    // Re-enable scanning
    err_code = sd_ble_gap_scan_start(NULL, &m_scan_buffer);
    APP_ERROR_CHECK(err_code);
#endif

    // Disconnect again in event handler

}

static void standard_reconfigure_master_eui(uint8_t* discovered_master_eui) {

    if (app.master_eui[0] == discovered_master_eui[0]) {
        // Same Master, we just resent the notification
        return;
    }

    printf("INFO: Switched Master EUI from %02X to %02X\n", app.master_eui[0], discovered_master_eui[0]);

    // We only use a single byte to identify devices
    for (uint8_t i = 0; i < 1; i++) {
        app.master_eui[i] = discovered_master_eui[i];
    }

    // Change advertisement data
    app.app_ble_advdata[APP_ADVDATA_OFFSET_MASTER_EUI] = app.master_eui[0];

    // Stop advertisements
    ret_code_t err_code = sd_ble_gap_adv_stop(m_advertising.adv_handle);
    if (err_code != NRF_ERROR_INVALID_STATE) {
        APP_ERROR_CHECK(err_code);
    }

    // Restart advertisements
    err_code = sd_ble_gap_adv_set_configure(&m_advertising.adv_handle, m_advertising.p_adv_data, &m_advertising.adv_params);
    if (err_code != NRF_ERROR_INVALID_STATE) {
        APP_ERROR_CHECK(err_code);
    }
    err_code = sd_ble_gap_adv_start(m_advertising.adv_handle, m_advertising.conn_cfg_tag);
    if (err_code != NRF_ERROR_INVALID_STATE) {
        APP_ERROR_CHECK(err_code);
    }
}

// Resets the Master EUI and restarts the module
static void standard_reconfigure_module(uint8_t discovered_master_eui) {

    //printf("INFO: Discovered new Master\n");

    // Change Master EUI
    standard_reconfigure_master_eui(&discovered_master_eui);

    // Re-configure module in main loop
    app.config.app_module_running = false;
}

// Handles advertising reports
static void on_adv_report(ble_gap_evt_adv_report_t const * p_adv_report)
{
    ret_code_t err_code;

#ifdef APP_BLE_TEST_ADV
    // Signal received advertisement and keep on scanning
    if (addr_in_whitelist(&p_adv_report->peer_addr)) {
        led_toggle(CARRIER_LED_BLUE);
    }

    err_code = sd_ble_gap_scan_start(NULL, &m_scan_buffer);
    APP_ERROR_CHECK(err_code);
    return;
#endif

    //printf("DEBUG: Received advertisement from %i\n", p_adv_report->peer_addr.addr[0]);

    // If it is a scan response, we dont need to analyse it
    if (p_adv_report->type.scan_response) {
        // Could extract full name here

    } // If device is in our whitelist, we just discovered it
    else if (addr_in_whitelist(&p_adv_report->peer_addr)) {

        // Find Master EUI
        uint8_t discovered_master_eui = 0;
        ble_data_t advdata;
        err_code = adv_report_parse(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, &p_adv_report->data, &advdata);

        if (err_code == NRF_SUCCESS) {
            // Parse manufacturer-specific data
            uint16_t company_identifier = advdata.p_data[0] + (advdata.p_data[1] << 1*8);
            if ( company_identifier != APP_COMPANY_IDENTIFIER) {
                printf("ERROR: Incorrect company identifier received!\n");
            }

            uint8_t service_id = advdata.p_data[2];
            if ( service_id != APP_SERVICE_IDENTIFIER) {
                printf("ERROR: Incorrect service identifier received!\n");
            }

            // Obtain Master EUI of node; if == 0, it is not part of a network
            discovered_master_eui = advdata.p_data[3];

            if (discovered_master_eui > 0x00) {
                printf("INFO: Network with master %i discovered\n", discovered_master_eui);
            } else {
                printf("INFO: Discovered new node %i without network\n", p_adv_report->peer_addr.addr[0]);
            }

        } else {
            printf("WARNING: Found no Master EUI in advertisement!\n");
        }

        if (!app.network_discovered) {
            // Not connected to a network yet
            on_device_discovery(&p_adv_report->peer_addr);

            uint8_t discovered_adv_eui = p_adv_report->peer_addr.addr[0];

            // Decide who is going to be a Master
            if (discovered_master_eui > 0x00) {
                // Advertising node is already part of an existing network, so join
                standard_reconfigure_module(discovered_master_eui);
            } else {
                // Start a new network
                if (app.config.my_eui[0] > discovered_adv_eui) {
                    // Become the Glossy Master of the new network ourselves
                    standard_reconfigure_module(app.config.my_eui[0]);
                } else {
                    // Join the network which will be started by the other node
                    standard_reconfigure_module(discovered_adv_eui);
                }
            }

        } else {

            // Switch if new Master of an existing network found with a higher EUI than the current one
            if (app.master_eui[0] < discovered_master_eui) {
                standard_reconfigure_module(discovered_master_eui);
            }
        }
    }

    // Restart scanning
    err_code = sd_ble_gap_scan_start(NULL, &m_scan_buffer);
    APP_ERROR_CHECK(err_code);
}

// Function for handling Queued Write Module errors
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/*******************************************************************************
 *   Module Callbacks
 ******************************************************************************/

void updateData (uint8_t * data, uint32_t len)
{
    uint16_t copy_len = (uint16_t) MIN(len, APP_BLE_BUFFER_LENGTH);

    // Copy for transmit
	memcpy(app.app_raw_response_buffer, data, copy_len);
	app.app_raw_response_length = copy_len;

    /* Print ranging packet
       0:   Length of the included payload (not including this byte)
       1:   Interrupt_reason

       Packet:
       0:   Number of ranges
       1-9: Ranging response Nr. 1

       Per ranging:
       8:   EUI
       4:   Range in mm
    */
	printf("INFO: Interrupt with reason %i", data[0]);

	if (data[0] == HOST_IFACE_INTERRUPT_RANGES) {
        printf(", included number of anchors: %i\r\n", data[1]);

        const uint8_t packet_overhead = 2;
        const uint8_t packet_euid     = 1;
        const uint8_t packet_range    = 4;
        const uint8_t ranging_length  = packet_euid + packet_range;
        uint8_t nr_ranges = data[1];

        for (uint8_t i = 0; i < nr_ranges; i++) {
            uint8_t offset = packet_overhead + i * ranging_length;
            printf("DEBUG: Nr %i", i + 1);
            printf(": Anchor %02X with range ", data[offset + 0]);

            // Little-endian notation
            offset += packet_euid;
            int32_t range = data[offset] + (data[offset + 1] << 1*8) + (data[offset + 2] << 2*8) + (data[offset + 3] << 3*8);
            
            if (range > ONEWAY_TAG_RANGE_MIN) {
                printf("%li", range);
            } else if (range == (int32_t)ONEWAY_TAG_RANGE_ERROR_NO_OFFSET) {
                printf("ERROR_NO_OFFSET");
            } else if (range == (int32_t)ONEWAY_TAG_RANGE_ERROR_TOO_FEW_RANGES) {
                printf("ERROR_TOO_FEW_RANGES");
            } else if (range == (int32_t)ONEWAY_TAG_RANGE_ERROR_MISC) {
                printf("ERROR_MISC");
            } else {
                printf("INVALID");
            }

            printf("\r\n");
        }

        // Check if we received a new epoch
        if (!node_is_master() && (copy_len > (packet_overhead + nr_ranges * ranging_length))) {

            uint8_t offset = packet_overhead + nr_ranges * ranging_length;
            uint32_t epoch = data[offset] + (data[offset + 1] << 1*8) + (data[offset + 2] << 2*8) + (data[offset + 3] << 3*8);

            app.config.app_sync_time        = epoch;
            app.config.app_sync_rtc_counter = rtc_to_s(nrfx_rtc_counter_get(&rtc_instance));
            app.config.app_sync_rtc_overflow_counter = 0;
            app.config.app_sync_rtc_overflown        = false;
            printf("INFO: Updated current epoch time: %li\n", epoch);
        }

        // Trigger moduleDataUpdate from main loop
        app.app_raw_response_buffer_updated = true;

    } else if (data[0] == HOST_IFACE_INTERRUPT_CALIBRATION) {
	    printf(", set to calibration\n");

	    app.app_raw_response_buffer_updated = true;

	} else if (data[0] == HOST_IFACE_INTERRUPT_MASTER_EUI) {
	    printf(", changing master EUI to %i\n", data[1]);

	    standard_reconfigure_master_eui(data + 1);

	    // If Master EUI is set to 0, we descheduled from existing networks
	    if (data[1] == 0x00) {
	        app.network_discovered        = false;
	        app.config.app_module_running = false;

	        printf("INFO: Left the network... again searching for a new network\n");
	    }

	} else if (data[0] == HOST_IFACE_INTERRUPT_RANGES_RAW) {
	    printf(", storing raw ranges\n");

        // Check if we received a new epoch
        uint8_t packet_overhead = 1;
        uint8_t nr_ranges       = 30;

        if (!node_is_master() && ( (len - packet_overhead) % ((1 + nr_ranges) * sizeof(int32_t)) != 0)) {

            uint8_t num_responses = (len - packet_overhead - sizeof(uint32_t)) / ((1 + nr_ranges) * sizeof(int32_t));

            uint8_t offset = packet_overhead + num_responses * (1 + nr_ranges) * sizeof(int32_t);

            // Read epoch
            uint32_t epoch = data[offset] + (data[offset + 1] << 1*8) + (data[offset + 2] << 2*8) + (data[offset + 3] << 3*8);

            app.config.app_sync_time        = epoch;
            app.config.app_sync_rtc_counter = rtc_to_s(nrfx_rtc_counter_get(&rtc_instance));
            app.config.app_sync_rtc_overflow_counter = 0;
            app.config.app_sync_rtc_overflown        = false;
            printf("INFO: Updated current epoch time: %li\n", epoch);
        }

	    app.app_raw_response_buffer_updated = true;
	}
}


void moduleDataUpdate ()
{
    // Clear flag
    app.app_raw_response_buffer_updated = false;

	if( (carrier_ble_conn_handle != BLE_CONN_HANDLE_INVALID) && (app.app_raw_response_length <= APP_BLE_MAX_CHAR_LEN) ) {

	    ble_gatts_hvx_params_t notify_params;
		memset(&notify_params, 0, sizeof(notify_params));

        notify_params.handle = carrier_ble_char_location_handle.value_handle;
		notify_params.type   = BLE_GATT_HVX_NOTIFICATION;
		notify_params.offset = 0;
		notify_params.p_len  = &app.app_raw_response_length;
		notify_params.p_data = app.app_raw_response_buffer;

		ret_code_t err_code = sd_ble_gatts_hvx(carrier_ble_conn_handle, &notify_params);

        if (err_code == NRF_ERROR_INVALID_STATE) {
            // Error means notify is not enabled by the client. IGNORE
        } else if (err_code == NRF_ERROR_RESOURCES) {
            // Too many notifications in queue... just ignore and wait for device to read them
        } else {
            APP_ERROR_CHECK(err_code);
        }

        //printf("DEBUG: Sent BLE packet of length %i \r\n", app.app_raw_response_length);
	}

	// Store locally in log
	if (app.app_raw_response_buffer[0] == HOST_IFACE_INTERRUPT_RANGES) {
	    log_ranges(app.app_raw_response_buffer, app.app_raw_response_length);
	} else if (app.app_raw_response_buffer[0] == HOST_IFACE_INTERRUPT_RANGES_RAW) {
	    log_ranges_raw(app.app_raw_response_buffer, app.app_raw_response_length);
	}

}

bool app_timer_triggered_epoch = false;

// This function is triggered every WATCHDOG_CHECK_RATE (currently 10s)
static void watchdog_handler (void* p_context)
{
    uint32_t err_code;

    if (!app.module_inited) {
        err_code = module_init(&app.module_interrupt_thrown, updateData);
        APP_ERROR_CHECK(err_code);

        if (err_code == NRF_SUCCESS) {
            app.module_inited = true;

            printf("INFO: Watchdog initialized module\n");
        }
    }

    // Increase counter first - else everything is triggered simultaneously at == 0
    app.timer_counter++;
    //printf("DEBUG: Current Timer Counter: %li\n", app.timer_counter);

    // Epoch timer
    if ( ( (app.timer_counter % 6) == 0) && node_is_master()) {
        app_timer_triggered_epoch = true;
    }

}

// If no pending operation, sleep until the next event occurs
static void power_manage(void)
{
    nrf_pwr_mgmt_run();

    // After wake-up, check whether things occured
    app_sched_execute();
}


/*******************************************************************************
 *   INIT FUNCTIONS
 ******************************************************************************/

// Initializing the BLE stack, including the SoftDevice and the BLE event interrupt
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_app_ram_start_get(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Overwrite some of the default configurations for the BLE stack.
    ble_cfg_t ble_cfg;

    // Set default parameters
    //nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    //APP_ERROR_CHECK(err_code);

    // Configure the maximum number of connections.
    memset(&ble_cfg, 0, sizeof(ble_cfg));
    ble_cfg.conn_cfg.conn_cfg_tag                     = APP_BLE_CONN_CFG_TAG;
    ble_cfg.conn_cfg.params.gap_conn_cfg.conn_count   = NRF_SDH_BLE_TOTAL_LINK_COUNT;
    ble_cfg.conn_cfg.params.gap_conn_cfg.event_length = NRF_SDH_BLE_GAP_EVENT_LENGTH;
    err_code = sd_ble_cfg_set(BLE_CONN_CFG_GAP, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);

    // Configure the connection roles
    memset(&ble_cfg, 0, sizeof(ble_cfg));
    ble_cfg.gap_cfg.role_count_cfg.periph_role_count  = NRF_SDH_BLE_PERIPHERAL_LINK_COUNT;
    ble_cfg.gap_cfg.role_count_cfg.central_role_count = NRF_SDH_BLE_CENTRAL_LINK_COUNT;
    ble_cfg.gap_cfg.role_count_cfg.central_sec_count  = BLE_GAP_ROLE_COUNT_CENTRAL_SEC_DEFAULT;
    err_code = sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);

    // Configure max ATT MTU size
    memset(&ble_cfg, 0x00, sizeof(ble_cfg));
    ble_cfg.conn_cfg.conn_cfg_tag                 = APP_BLE_CONN_CFG_TAG;
    ble_cfg.conn_cfg.params.gatt_conn_cfg.att_mtu = NRF_SDH_BLE_GATT_MAX_MTU_SIZE;
    err_code = sd_ble_cfg_set(BLE_CONN_CFG_GATT, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);

    // Configure number of custom UUIDS.
    memset(&ble_cfg, 0, sizeof(ble_cfg));
    ble_cfg.common_cfg.vs_uuid_cfg.vs_uuid_count = NRF_SDH_BLE_VS_UUID_COUNT;
    err_code = sd_ble_cfg_set(BLE_COMMON_CFG_VS_UUID, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);

    // Configure the GATTS attribute table.
    memset(&ble_cfg, 0x00, sizeof(ble_cfg));
    ble_cfg.gatts_cfg.attr_tab_size.attr_tab_size = NRF_SDH_BLE_GATTS_ATTR_TAB_SIZE;
    err_code = sd_ble_cfg_set(BLE_GATTS_CFG_ATTR_TAB_SIZE, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);

    // Configure Service Changed characteristic.
    memset(&ble_cfg, 0x00, sizeof(ble_cfg));
    ble_cfg.gatts_cfg.service_changed.service_changed = NRF_SDH_BLE_SERVICE_CHANGED;
    err_code = sd_ble_cfg_set(BLE_GATTS_CFG_SERVICE_CHANGED, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);


    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

// GAP initialization: set up all the necessary GAP (Generic Access Profile) parameters of the device. It also sets the permissions and appearance
static void gap_params_init(void)
{
    ret_code_t              err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;
    uint8_t                 device_name[] = APP_DEVICE_NAME;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode, device_name, (uint16_t)strlen((const char *)device_name));
    APP_ERROR_CHECK(err_code);

    // Set BLE address

    // Get address from Flash
    //#define BLE_FLASH_ADDRESS 0x0003fff8
    //memcpy(carrier_ble_address, (uint8_t*) BLE_FLASH_ADDRESS, 6);
    //carrier_ble_device_id = (uint16_t)( (uint16_t)carrier_ble_address[1] << (uint8_t)8) | carrier_ble_address[0];

    // Use ID
#ifdef BLE_ADDRESS
#define XID_STR(ID) ID_STR(ID)
#define ID_STR(ID) #ID
#define BLE_ADDRESS_LENGTH 6

    const char ble_address_string[] = XID_STR(BLE_ADDRESS);
    for (int i=0; i < BLE_ADDRESS_LENGTH; i++) {
        // For each 8bits, read the string and convert it from hex to a long; store it in LSB-first order
        carrier_ble_address[(BLE_ADDRESS_LENGTH-1) - i] = (uint8_t)strtoul(&ble_address_string[3*i], NULL, 16);
    }

    printf("DEBUG: Bluetooth address: %02x:%02x:%02x:%02x:%02x:%02x\n", carrier_ble_address[5],
                                                                 carrier_ble_address[4],
                                                                 carrier_ble_address[3],
                                                                 carrier_ble_address[2],
                                                                 carrier_ble_address[1],
                                                                 carrier_ble_address[0]);

    // Set address
    ble_gap_addr_t gap_addr = {.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC};
    memcpy(gap_addr.addr, carrier_ble_address, BLE_ADDRESS_LENGTH);
    err_code = sd_ble_gap_addr_set(&gap_addr);
    APP_ERROR_CHECK(err_code);

    // Use first bit of the BLE address as our EUI
    app.config.my_eui[0] = carrier_ble_address[0];
#endif

    // Connection parameters: should be the same ones as in m_connection_param
    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);

    // Tx Power: default is 0dBm, max is +8dBm (0x8); possible values are in register TXPOWER
    //err_code = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_SCAN_INIT, NULL, 0x8);
    //APP_ERROR_CHECK(err_code);
}


// Initializing the GATT module.
static void gatt_init(void)
{
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);
}

static void advertising_init(void)
{
    ret_code_t             err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));

    // Advertisement

    // Custom advertisement data
    ble_advdata_manuf_data_t             manuf_data_adv;
    manuf_data_adv.company_identifier    = APP_COMPANY_IDENTIFIER; // UMich's Company ID
    manuf_data_adv.data.p_data           = app.app_ble_advdata;
    manuf_data_adv.data.size             = sizeof(app.app_ble_advdata);
    init.advdata.p_manuf_specific_data   = &manuf_data_adv;

    // Add device name in advertisements
    // init.advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    // For shorter names, adjust as follows. The full name will still be displayed in connections
    init.advdata.name_type               = BLE_ADVDATA_NO_NAME;
    //init.advdata.short_name_len          = 4; // Advertise the first X letters of the name

    // Physical Web data
    const char* url_str = PHYSWEB_URL;
    const uint8_t header_len = 3;
    uint8_t url_frame_length = header_len + strlen((char*)url_str); // Change to 4 if URLEND is applied
    uint8_t m_url_frame[url_frame_length];
    m_url_frame[0] = PHYSWEB_URL_TYPE;
    m_url_frame[1] = PHYSWEB_TX_POWER;
    m_url_frame[2] = PHYSWEB_URLSCHEME_HTTPS;
    for (uint8_t i=0; i<strlen((char*)url_str); i++) {
        m_url_frame[i+3] = url_str[i];
    }

    // Advertise Physical Web service
    ble_advdata_service_data_t service_data;
    service_data.service_uuid            = PHYSWEB_SERVICE_ID;
    service_data.data.p_data             = m_url_frame;
    service_data.data.size               = url_frame_length;
    init.advdata.p_service_data_array    = &service_data;
    init.advdata.service_data_count      = 1;

    //printf("DEBUG: Advertising Eddystone address %s with total length %i\n", url_str, url_frame_length);

    init.advdata.include_appearance      = false;
    init.advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.advdata.uuids_complete.p_uuids  = m_adv_uuids;

    // Scan response

    // Custom advertisement data
    /*ble_advdata_manuf_data_t             manuf_data_resp;
    //#define MANUF_RESP_LENGTH            20
    //uint8_t data_resp[MANUF_RESP_LENGTH] = {0};
    manuf_data_resp.company_identifier   = APP_COMPANY_IDENTIFIER;
    manuf_data_resp.data.p_data          = app.app_ble_advdata;
    manuf_data_resp.data.size            = APP_BLE_ADVDATA_LENGTH;
    init.srdata.p_manuf_specific_data    = &manuf_data_resp;*/

    init.srdata.name_type                = BLE_ADVDATA_FULL_NAME;
    init.srdata.uuids_complete.uuid_cnt  = sizeof(m_sr_uuids) / sizeof(m_sr_uuids[0]);
    init.srdata.uuids_complete.p_uuids   = m_sr_uuids;

    init.config.ble_adv_fast_enabled  = true;
#ifndef APP_BLE_CALIBRATION
    init.config.ble_adv_fast_interval = (uint32_t)APP_ADV_INTERVAL;
#else
    // Increase number of advertisements so the scanner finds all of them simultaneously
    init.config.ble_adv_fast_interval = (uint32_t)APP_ADV_INTERVAL_CALIBRATION;
#endif
    //init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;

    // Define Event handler
    init.evt_handler = on_adv_evt;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

// Used for the initialization of the service; initializes a specific characteristic
void ble_characteristic_add(uint8_t read, uint8_t write, uint8_t notify, uint8_t vlen,
                               uint8_t data_len, uint8_t* data,
                               uint16_t uuid,
                               ble_gatts_char_handles_t* char_handle)
{
    ret_code_t err_code;

    // Add characteristics
    ble_uuid128_t base_uuid = CARRIER_BLE_SERV_LONG_UUID;
    ble_uuid_t    char_uuid = {.uuid = uuid};

    err_code = sd_ble_uuid_vs_add(&base_uuid, &char_uuid.type);
    APP_ERROR_CHECK(err_code);

    // Add read/write properties to our characteristic
    ble_gatts_char_md_t char_md;
    memset(&char_md, 0, sizeof(char_md));
    char_md.char_props.read   = read;
    char_md.char_props.write  = write;
    char_md.char_props.notify = notify;

    // Configuring Client Characteristic Configuration Descriptor (CCCD) metadata and add to char_md structure
    ble_gatts_attr_md_t cccd_md;
    memset(&cccd_md, 0, sizeof(cccd_md));
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
    cccd_md.vloc                = BLE_GATTS_VLOC_STACK;
    char_md.p_cccd_md           = &cccd_md;

    // Configure the attribute metadata
    ble_gatts_attr_md_t attr_md;
    memset(&attr_md, 0, sizeof(attr_md));
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr_md.vlen = vlen;

    // Configure the characteristic value attribute
    ble_gatts_attr_t    attr_char_value;
    memset(&attr_char_value, 0, sizeof(attr_char_value));
    attr_char_value.p_uuid      = &char_uuid;
    attr_char_value.p_attr_md   = &attr_md;

    // Set characteristic length in number of bytes
    attr_char_value.max_len     = data_len;
    attr_char_value.init_len    = 0;
    attr_char_value.p_value     = data;

    // Add our new characteristics to the service
    err_code = sd_ble_gatts_characteristic_add(carrier_ble_service_handle, &char_md, &attr_char_value, char_handle);
    APP_ERROR_CHECK(err_code);
}

// Initializing services that will be used by the application.
static void services_init(void)
{
    ret_code_t         err_code;
    nrf_ble_qwr_init_t qwr_init = {0};

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;

    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);

    // Initialize our own BLE service
    ble_uuid128_t base_uuid = CARRIER_BLE_SERV_LONG_UUID;
    ble_uuid_t    service_uuid = {.uuid = CARRIER_BLE_SERV_SHORT_UUID};

    err_code = sd_ble_uuid_vs_add(&base_uuid, &service_uuid.type);
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service_uuid, &carrier_ble_service_handle);
    APP_ERROR_CHECK(err_code);

    // Add characteristics
    ble_characteristic_add(1, 0, 1, 0,
                           128, app.app_raw_response_buffer,
                           CARRIER_BLE_CHAR_LOCATION,
                           &carrier_ble_char_location_handle);

    ble_characteristic_add(1, 1, 0, 0,
                           28, (uint8_t*) &app.config,
                           CARRIER_BLE_CHAR_CONFIG,
                           &carrier_ble_char_config_handle);

    ble_characteristic_add(1, 1, 0, 0,
                           10, (uint8_t*) &app.config.app_module_enabled,
                           CARRIER_BLE_CHAR_ENABLE,
                           &carrier_ble_char_enable_handle);

    ble_characteristic_add(1, 1, 0, 0,
                           9, (uint8_t*) &app.module_inited,
                           CARRIER_BLE_CHAR_STATUS,
                           &carrier_ble_char_status_handle);

    ble_characteristic_add(1, 1, 0, 0,
                           14, &app.calibration_index,
                           CARRIER_BLE_CHAR_CALIBRATION,
                           &carrier_ble_char_calibration_handle);
}

// Setup role as CENTRAL for scanning
static void central_init(void)
{
    // Create whitelist for all our devices
    ble_gap_addr_t whitelisted_ble_address = {
            .addr_type = BLE_GAP_ADDR_TYPE_PUBLIC,
            .addr      = {APP_BLE_ADDR_MIN,0x00,0x42,0xe5,0x98,0xc0}
    };

    for (uint16_t i = 0; i < (APP_BLE_ADDR_MAX - APP_BLE_ADDR_MIN + 1); i++) {
        memcpy(&pp_wl_addrs[i], &whitelisted_ble_address, sizeof(ble_gap_addr_t));
        whitelisted_ble_address.addr[0]++;
    }

    // Set whitelist: only works if less than BLE_GAP_WHITELIST_ADDR_MAX_COUNT devices max
    //sd_ble_gap_whitelist_set(pp_wl_addrs, APP_BLE_ADDR_MAX - APP_BLE_ADDR_MIN + 1);

}

static void scan_start(void)
{
    ret_code_t err_code;

    //err_code = sd_ble_gap_scan_stop();
    //APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gap_scan_start(&m_scan_params, &m_scan_buffer);
    APP_ERROR_CHECK(err_code);

    printf("INFO: Started scanning...\n");
}

// Initializing the Connection Parameters module.
static void conn_params_init(void)
{
    ret_code_t             err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;//&m_connection_param;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = true; // Can also add a on_conn_params_evt as a handler

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

void ble_init(void)
{
    //printf("DEBUG: Started initializing BLE...\n");

    ble_stack_init();
    //printf("DEBUG: Stack initialized\n");

    gap_params_init();
    //printf("DEBUG: GAP parameters initialized\n");

    gatt_init();
    //printf("DEBUG: GATT module initialized\n");

    services_init();
    //printf("DEBUG: BLE services initialized\n");

    advertising_init();
    //printf("DEBUG: Advertising initialized\n");

    central_init();
    //printf("DEBUG: Scanning initialized\n");

    conn_params_init();
    //printf("DEBUG: Connection parameters initialized\n");

    // Instead of advertising directly, use Eddystone library
    //nrf_ble_es_init(on_es_evt);
    //printf("DEBUG: Eddystone beaconing initialized\n");
}

// Non-BLE inits -------------------------------------------------------------------------------------------------------

void app_init(void) {

    // Configuration

    app.config.app_role             = APP_ROLE_INIT_RESP; // Default to HYBRID
    app.config.app_sync_time        = 0;
    app.config.app_sync_rtc_counter = 0;
    app.config.app_sync_rtc_overflow_counter = 0;
    app.config.app_sync_rtc_overflown        = false;
    memset(app.config.my_eui, 0, sizeof(app.config.my_eui));

    // We default to enable discovery, but not start the module at the beginning
    app.config.app_module_enabled = true;
    app.config.app_module_running = false;

    // Set to effective -1
    app.calibration_index = APP_BLE_CALIBRATION_INDEX_INVALID;

    // Clear buffers
    app.module_inited           = false;
    app.module_interrupt_thrown = false;
    app.network_discovered      = false;

    memset(app.master_eui, 0, sizeof(app.master_eui));

    memset(app.current_location, 0, 6);

    app.app_raw_response_buffer_updated = false;
    app.app_raw_response_length = APP_BLE_BUFFER_LENGTH;
    memset(app.app_raw_response_buffer, 0, sizeof(app.app_raw_response_buffer));

    app.app_sdcard_buffer_length = 0;
    memset(app.app_sdcard_buffer, 0, sizeof(app.app_sdcard_buffer));

    memset(app.app_ble_advdata, 0, sizeof(app.app_ble_advdata));

    // Set the beginning of the manufacturer-specific advertisement data

    // TotTag's service identifier, expected to be first byte of advertisement
    app.app_ble_advdata[APP_ADVDATA_OFFSET_SERVICE_ID] = APP_SERVICE_IDENTIFIER;
}

static void timers_init (void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // Create and start watchdog
    err_code = app_timer_create(&watchdog_timer, APP_TIMER_MODE_REPEATED, watchdog_handler);
    APP_ERROR_CHECK(err_code);

}

static void timers_start (void)
{
    // Start the watchdog
    ret_code_t err_code = app_timer_start(watchdog_timer, WATCHDOG_CHECK_RATE, NULL);
    APP_ERROR_CHECK(err_code);
}

static void rtc_internal_init(void) {

    // Initialize LFCLK if no SoftDevice available
    //lfclk_config();

    // Initialize RTC instance
    nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
    rtc_config.prescaler = 4095; // Approximately 8 Hz; PRESCALER is 12bit register (2^12 - 1 = 4095)
    ret_code_t err_code = nrfx_rtc_init( &rtc_instance, &rtc_config, rtc_handler);
    APP_ERROR_CHECK(err_code);

    // Power on RTC instance
    nrfx_rtc_enable(&rtc_instance);
}

static void scheduler_init(void)
{
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}

static void power_management_init(void)
{
    ret_code_t err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

void carrier_hw_init(void)
{
    ret_code_t err_code;

    // Initialization
    nrfx_gpiote_init();
    led_init(CARRIER_LED_RED);
    led_init(CARRIER_LED_BLUE);
    led_init(CARRIER_LED_GREEN);

    // Signal initialization
#ifndef NRF_DISABLE_LEDS
    led_on(CARRIER_LED_RED);
    led_off(CARRIER_LED_BLUE);
    led_off(CARRIER_LED_GREEN);
#else
    led_off(CARRIER_LED_RED);
    led_off(CARRIER_LED_BLUE);
    led_off(CARRIER_LED_GREEN);
#endif

    // Initialize RTT library
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    printf("\r\n----------------------------------------------\r\n");
    printf("INFO: Initializing nRF...\r\n");

    // Check that we didn't have to do a hardware reset before; if so, state the reason
    uint32_t reset_reason = nrf_power_resetreas_get();

    if (reset_reason) {
        printf("WARNING: Chip experienced a reset with reason %lu\n", reset_reason);
        nrf_power_resetreas_clear(0xFFFFFFFF);
    }

    // Init DCDC regulator when not using the SoftDevice
    //nrf_power_dcdcen_set(1);

    // Initialize ------------------------------------------------------------------------------------------------------

    app_init();
    timers_init();
    scheduler_init();
    power_management_init();

    printf("INFO: Initialized software modules\n");

    // Software services init
    // ATTENTION: inside ble_init(), we further initialize the SoftDevice (triggering the low-power clock)
    ble_init();
    rtc_internal_init();

    // As the SoftDevice is now enabled, we can tell it to use the DC/DC regulator
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);

    printf("INFO: Initialized software services\n");

    // Buses init
    spi_init();
    //twi_init(); // Moved to module_interface.c

    printf("INFO: Initialized buses\n");
}

void carrier_start_module(uint8_t role) {
    ret_code_t err_code;

    if (app.config.app_module_running) {
        // Module is already enabled!
        printf("ERROR: Module already enabled!\n");
        return;
    }

    if (!app.module_inited) {
        err_code = module_init(&app.module_interrupt_thrown, updateData);
        if (err_code == NRF_SUCCESS) {
            app.module_inited = true;
            printf("INFO: Finished initialization\r\n");
        } else {
            printf("ERROR: Failed initialization!\r\n");
            return;
        }
    }

    // Configure whether we are the Glossy Master
    bool is_glossy_master = node_is_master();

    switch(role) {
        case APP_ROLE_INIT_NORESP: {
            printf("INFO: Role - INITIATOR\n");

            err_code = module_start_role(APP_ROLE_INIT_NORESP, is_glossy_master, app.master_eui[0]);

            if (err_code != NRF_SUCCESS) {
                printf("ERROR: Failed to start ranging!\r\n");
                return;
            } else {
                printf("INFO: Started ranging...\r\n");
            }
            break;
        }
        case APP_ROLE_INIT_RESP: {

            if (is_glossy_master) {
                printf("INFO: Role - HYBRID Master\n");
            } else {
                printf("INFO: Role - HYBRID\n");
            }

            err_code = module_start_role(APP_ROLE_INIT_RESP, is_glossy_master, app.master_eui[0]);

            if (err_code != NRF_SUCCESS) {
                printf("ERROR: Failed to start the module!\r\n");
                return;
            } else {
                printf("INFO: Started the module...\r\n");
            }
            break;
        }
        case APP_ROLE_NOINIT_RESP: {

            if (is_glossy_master) {
                printf("INFO: Role - RESPONDER Master\n");
            } else {
                printf("INFO: Role - RESPONDER\n");
            }

            err_code = module_start_role(APP_ROLE_NOINIT_RESP, is_glossy_master, app.master_eui[0]);

            if (err_code != NRF_SUCCESS) {
                printf("ERROR: Failed to start responding!\r\n");
                return;
            } else {
#ifdef GLOSSY_MASTER
                printf("Started responding as Glossy master...\r\n");
#else
                printf("INFO: Started responding...\r\n");
#endif
            }
            break;
        }
        case APP_ROLE_NOINIT_NORESP: {

            printf("INFO: Role - SUPPORT\n");

            err_code = module_start_role(APP_ROLE_NOINIT_NORESP, is_glossy_master, app.master_eui[0]);

            if (err_code != NRF_SUCCESS) {
                printf("ERROR: Failed to start the module!\r\n");
                return;
            } else {
                printf("INFO: Started the module...\r\n");
            }
            break;
        }
        default:
            // Role of this tag has not been configured yet
            //printf("ERROR: Unknown role during init!\n");
            return;
    }

    app.config.app_module_running = true;
}


/*******************************************************************************
 *   MAIN FUNCTION
 ******************************************************************************/

int main (void)
{
    ret_code_t err_code;

    carrier_hw_init();

    // Peripherals -----------------------------------------------------------------------------------------------------

    // Hardware services init
    sd_card_init();
    rtc_external_init();
    //acc_init();
    //bat_monitor_init();

    printf("INFO: Initialized hardware services\n");

    // Module connection setup
    err_code = module_hw_init();
    APP_ERROR_CHECK(err_code);

    // Start scanning (Role: Central)
#ifndef APP_BLE_CALIBRATION
    scan_start();
#else
    printf("WARNING: Did not start scanning because we are in Calibration mode\n");
#endif

    // Start advertisements (Role: Peripheral)
    err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);

    printf("INFO: Started broadcasting BLE advertisements...\n");

    // Contact module --------------------------------------------------------------------------------------------------

#ifndef APP_BLE_TEST_ADV
    // Init the state machine on the module
    err_code = module_init(&app.module_interrupt_thrown, updateData);
    if (err_code == NRF_SUCCESS) {
        app.module_inited = true;
        printf("INFO: Finished initialization\r\n");
    } else {
        printf("ERROR: Failed initialization!\r\n");
    }
#endif

//#define BYPASS_USER_INTERFACE
#ifdef BYPASS_USER_INTERFACE

//#define GLOSSY_MASTER
#ifdef GLOSSY_MASTER
    app.config.my_eui[0] = 0x01;
    app.master_eui[0]    = 0x01;
#endif

// Test without having to rely on BLE for configuration
#ifdef ROLE_INITIATOR
    // Start the ranging
    carrier_start_module(APP_ROLE_INIT_NORESP);
#endif
#ifdef ROLE_RESPONDER
    // Start responding to polls
    carrier_start_module(APP_ROLE_NOINIT_RESP);
#endif

#else
    // Wait for BLE to configure us as a specific device
    // Done automatically upon discovery
    printf("INFO: Waiting for discovery of other devices...\n");
#endif

    // Start timers
    timers_start();

#ifndef NRF_DISABLE_LEDS
    // Signal end of initialization
    led_off(CARRIER_LED_RED);
    led_on(CARRIER_LED_BLUE);
#endif

    // -----------------------------------------------------------------------------------------------------------------

    // Loop: update location and advertise
    while (1) {

        // For power measurements: Disable timers (timers_init()) and comment the lines below power_manage()
        //printf("DEBUG: Going back go sleep...\r\n");
        power_manage();

        // I2C and data handling
        if (app.module_interrupt_thrown) {
            // Module is trying to communicate over I2C
            module_interrupt_dispatch();
        }

        if (app.app_raw_response_buffer_updated) {
            // Received new data over I2C which we can expose over the BLE characteristics
            //printf("DEBUG: Updating location...\r\n");
            moduleDataUpdate();
        }

        // Network discovery
        if (app.config.app_module_enabled && app.network_discovered && !app.config.app_module_running) {
            // Initialize module with configured role
            printf("INFO: Starting node\n");
            carrier_start_module(app.config.app_role);

        } else if (!app.config.app_module_enabled && app.config.app_module_running) {

            // Stop execution after we disabled the module
            module_sleep();

            app.config.app_module_running = false;

            printf("INFO: Module stopped\n");

        } else if ( (app.calibration_index != APP_BLE_CALIBRATION_INDEX_INVALID) && !app.config.app_module_running) {
            // Configure this node for calibration and set the calibration node
            // index. If 0, this node will immediately start calibration.
            err_code = module_start_calibration(app.calibration_index);
            if (err_code == NRF_SUCCESS) {
                app.config.app_module_running = true;
                printf("INFO: Started calibration with index %i\n", app.calibration_index);
            } else {
                printf("ERROR: Failed to start calibration!\n");
            }
        }


        // If application count triggered an action, we must catch it here as well
        if (app_timer_triggered_epoch) {
            // Epoch counter update
            app_timer_triggered_epoch = false;

            uint32_t current_time = app_get_current_time();
            printf("INFO: Distributing global timestamp %lu\n", current_time);
            module_set_time(current_time);
        }
    }
}
