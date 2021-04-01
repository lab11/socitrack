// Header inclusions ---------------------------------------------------------------------------------------------------

#include <string.h>
#include "battery.h"
#include "ble_config.h"
#include "ble_gap.h"
#include "bluetooth.h"
#include "boards.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "nrf_delay.h"
#include "nrf_drv_power.h"
#include "nrf_pwr_mgmt.h"
#include "nrfx_gpiote.h"
#include "nrfx_power.h"
#include "nrfx_wdt.h"
#include "rtc.h"
#include "rtc_external.h"
#include "sd_card.h"
#include "SEGGER_RTT.h"
#include "squarepoint_interface.h"
#include "system.h"
#include "timers.h"
#include "usb.h"


// Application state variables -----------------------------------------------------------------------------------------

static app_flags_t _app_flags = { 0 };
static nrfx_spim_t _rtc_sd_spi_instance = RTC_SD_SPI_BUS;
static nrfx_spim_t _imu_spi_instance = IMU_SPI_BUS;
static uint8_t _range_buffer[APP_BLE_BUFFER_LENGTH] = { 0 };
static uint16_t _range_buffer_length = 0;


// Helper functions ----------------------------------------------------------------------------------------------------

static void watchdog_handler(void *p_context);
static void squarepoint_data_handler(uint8_t *data, uint32_t len);

static void app_init(void)
{
   // Initialize flags that are not defaulted to false or 0
   _app_flags.squarepoint_enabled = true;
   _app_flags.sd_card_inserted = true;
   _app_flags.battery_status_changed = true;
   _app_flags.device_in_motion = true;
   _app_flags.calibration_index = BLE_CALIBRATION_INDEX_INVALID;
}

static void spi_init(void)
{
   // Configure SPI pins
   nrf_gpio_cfg_input(RTC_SD_SPI_MISO, NRF_GPIO_PIN_PULLUP);
   nrfx_gpiote_out_config_t spi_mosi_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_config_t spi_sclk_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_init(RTC_SD_SPI_MOSI, &spi_mosi_pin_config);
   nrfx_gpiote_out_init(RTC_SD_SPI_SCLK, &spi_sclk_pin_config);
#if (BOARD_V >= 0x11)
   nrf_gpio_cfg_input(IMU_SPI_MISO, NRF_GPIO_PIN_PULLUP);
   nrfx_gpiote_out_config_t spi_imu_mosi_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_config_t spi_imu_sclk_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_init(IMU_SPI_MOSI, &spi_imu_mosi_pin_config);
   nrfx_gpiote_out_init(IMU_SPI_SCLK, &spi_imu_sclk_pin_config);
#endif

   // Setup Chip Selects (CS)
   nrfx_gpiote_out_config_t cs_sd_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_gpiote_out_config_t cs_imu_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_gpiote_out_init(CARRIER_CS_SD, &cs_sd_pin_config);
   nrfx_gpiote_out_init(CARRIER_CS_IMU, &cs_imu_pin_config);
#if (BOARD_V >= 0xF)
   nrfx_gpiote_out_config_t cs_rtc_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_gpiote_out_init(CARRIER_CS_RTC, &cs_rtc_pin_config);
#endif

   // Make sure SPI lines are valid and not floating
   nrfx_gpiote_out_config_t sd_enable_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_init(CARRIER_SD_ENABLE, &sd_enable_pin_config);

   // Configure RTC/SD-Card SPI lines
   nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;
   spi_config.sck_pin = RTC_SD_SPI_SCLK;
   spi_config.miso_pin = RTC_SD_SPI_MISO;
   spi_config.mosi_pin = RTC_SD_SPI_MOSI;
   spi_config.ss_pin = CARRIER_CS_SD;
   spi_config.frequency = NRF_SPIM_FREQ_4M;
   spi_config.mode = NRF_SPIM_MODE_3;
   spi_config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;

   // Initialize RTC/SD-Card SPI
   APP_ERROR_CHECK(nrfx_spim_init(&_rtc_sd_spi_instance, &spi_config, NULL, NULL));
}

static void hardware_init(void)
{
   // Initialize the GPIO subsystem and the LEDs
   initialize_gpio();
   leds_init();
   led_on(RED);

   // Initialize the RTT library
   printf("\n----------------------------------------------\n");
   uint32_t reset_reason = nrf_power_resetreas_get();
   if (reset_reason)
   {
      printf("WARNING: Chip experienced a reset with reason %lu\n", reset_reason);
      nrf_power_resetreas_clear(0xFFFFFFFF);
   }
   printf("INFO: Initializing nRF...\n");

   // Initialize the application state and essential hardware components
   app_init();
   buzzer_init();
   timers_init(watchdog_handler, &_app_flags.squarepoint_wakeup_triggered);
   rtc_init();

   // Initialize the power driver and USB stack
   const nrfx_power_config_t power_config = { .dcdcen = 1 };
   nrf_drv_power_init(&power_config);
   nrf_pwr_mgmt_init();
   usb_init();

   // Initialize the Bluetooth stack via a SoftDevice
   if (ble_init(&_app_flags.squarepoint_enabled, &_app_flags.squarepoint_running, &_app_flags.bluetooth_is_scanning, &_app_flags.calibration_index) != NRF_SUCCESS)
      while (true)
      {
         buzzer_indicate_error();
         nrf_delay_ms(2500);
      }

   // Tell the SoftDevice to use the DC/DC regulator and low-power mode
   sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
   sd_power_mode_set(NRF_POWER_MODE_LOWPWR);

   // Initialize SPI buses
   spi_init();
   printf("INFO: Initialized critical hardware and software services\n");

   // Wait until SD Card is inserted
   while (!sd_card_init(&_rtc_sd_spi_instance, &_app_flags.sd_card_inserted, ble_get_eui()))
   {
      buzzer_indicate_error();
      nrf_delay_ms(2500);
   }

   // Enable the external Real-Time Clock and ensure that the fetched timestamp is valid
   rtc_external_init(&_rtc_sd_spi_instance);
   uint32_t current_timestamp = rtc_get_current_time(), num_retries = 3;
   while (--num_retries && ((current_timestamp < MINIMUM_VALID_TIMESTAMP) || (current_timestamp > MAXIMUM_VALID_TIMESTAMP)))
   {
      printf("ERROR: RTC chip returned an impossible Unix timestamp: %lu\n", current_timestamp);
      rtc_external_init(&_rtc_sd_spi_instance);
      buzzer_indicate_invalid_rtc_time();
      nrf_delay_ms(1000);
      current_timestamp = rtc_get_current_time();
   }
   if ((current_timestamp > MINIMUM_VALID_TIMESTAMP) && (current_timestamp < MAXIMUM_VALID_TIMESTAMP))
      nrfx_atomic_flag_set(&_app_flags.rtc_time_valid);

   // Initialize supplementary hardware components
   imu_init(&_imu_spi_instance, &_app_flags.imu_data_ready);
   battery_monitor_init(&_app_flags.battery_status_changed);
   sd_card_create_log(nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid) ? rtc_get_current_time() : 0);
   led_off();
   printf("INFO: Initialized supplementary hardware and software services\n");
}

static void update_leds(uint32_t app_running, uint32_t network_discovered)
{
   if (!nrfx_atomic_flag_fetch(&_app_flags.sd_card_inserted))          // RED = SD card not inserted
      led_on(RED);
   else if (!nrfx_atomic_flag_fetch(&_app_flags.squarepoint_inited))   // PURPLE = Cannot communicate with SquarePoint
      led_on(PURPLE);
   else if (app_running)                                               // GREEN = App running
      led_on(GREEN);
   else if (network_discovered)                                        // ORANGE = Network discovered, app not running
      led_on(ORANGE);
   else                                                                // BLUE = No network discovered
      led_on(BLUE);
}

static nrfx_err_t start_squarepoint(void)
{
   // Wake up the SquarePoint module
   log_printf("INFO: Starting the SquarePoint module...\n");
   squarepoint_wakeup_module();

   // Initialize the SquarePoint module and communications
   nrfx_err_t err_code = squarepoint_init(&_app_flags.squarepoint_data_received, squarepoint_data_handler, ble_get_eui());
   if (err_code == NRFX_SUCCESS)
      nrfx_atomic_flag_set(&_app_flags.squarepoint_inited);
   else
   {
      log_printf("ERROR: Unable to communicate with the SquarePoint module\n");
      nrfx_atomic_flag_clear(&_app_flags.squarepoint_inited);
      return err_code;
   }

   // Set this device to be the scheduler if we already are or if none exists and we have the highest EUI
   schedule_role_t scheduler_role = PARTICIPANT;
   if ((memcmp(ble_get_scheduler_eui(), ble_get_eui(), BLE_GAP_ADDR_LEN) == 0) ||
         ((memcmp(ble_get_scheduler_eui(), ble_get_empty_eui(), BLE_GAP_ADDR_LEN) == 0) &&
               (memcmp(ble_get_eui(), ble_get_highest_network_eui(), BLE_GAP_ADDR_LEN) > 0)))
         scheduler_role = SCHEDULER;

   // Start the SquarePoint ranging application
   err_code = squarepoint_start_application(rtc_get_current_time(), ble_get_device_role(), scheduler_role);
   if (err_code != NRFX_SUCCESS)
   {
      log_printf("ERROR: Failed to start the SquarePoint ranging app!\n");
      return err_code;
   }
   else
   {
      // Update the application state
      ble_reset_discovered_devices();
      log_printf("INFO: SquarePoint module successfully started!\n");
      nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
      nrfx_atomic_flag_clear(&_app_flags.squarepoint_needs_reset);
      nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
   }
   return NRFX_SUCCESS;
}

#ifdef BLE_CALIBRATION
static nrfx_err_t start_squarepoint_calibration(void)
{
   // Wake up the SquarePoint module
   printf("INFO: Starting the SquarePoint module in calibration mode...\n");
   squarepoint_wakeup_module();

   // Initialize the SquarePoint module and communications
   nrfx_err_t err_code = squarepoint_init(&_app_flags.squarepoint_data_received, squarepoint_data_handler, ble_get_eui());
   if (err_code == NRFX_SUCCESS)
      nrfx_atomic_flag_set(&_app_flags.squarepoint_inited);
   else
   {
      printf("ERROR: Unable to communicate with the SquarePoint module\n");
      nrfx_atomic_flag_clear(&_app_flags.squarepoint_inited);
      return err_code;
   }

   // Start the SquarePoint calibration application
   err_code = squarepoint_start_calibration((uint8_t)nrfx_atomic_u32_fetch(&_app_flags.calibration_index));
   if (err_code != NRFX_SUCCESS)
   {
      printf("ERROR: Failed to start the SquarePoint calibration app!\n");
      return err_code;
   }
   else
   {
      // Update the application state
      printf("INFO: SquarePoint calibration successfully started!\n");
      nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
      nrfx_atomic_flag_clear(&_app_flags.squarepoint_needs_reset);
      nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
   }
   return NRFX_SUCCESS;
}
#endif


// Callbacks and data handlers -----------------------------------------------------------------------------------------

static void watchdog_handler(void *p_context)     // This function is triggered every WATCHDOG_CHECK_RATE (currently 1s)
{
   // Feed the watchdog timer and update the BLE state
   nrfx_wdt_feed();
   ble_second_has_elapsed();

   // Increment the battery voltage check timeout counter
   if (nrfx_atomic_u32_fetch_add(&_app_flags.battery_check_counter, 1) >= APP_BATTERY_CHECK_TIMEOUT_SEC)
   {
      nrfx_atomic_u32_store(&_app_flags.battery_check_counter, 0);
      nrfx_atomic_flag_set(&_app_flags.battery_check_time);
   }

   // Validate communications connection with the SquarePoint module
   if (!nrfx_atomic_flag_fetch(&_app_flags.squarepoint_inited))
   {
      // Wake up and initialize the SquarePoint module
      log_printf("INFO: Connecting to the SquarePoint module...\n");
      squarepoint_wakeup_module();
      if (squarepoint_init(&_app_flags.squarepoint_data_received, squarepoint_data_handler, ble_get_eui()) == NRFX_SUCCESS)
      {
         nrfx_atomic_flag_set(&_app_flags.squarepoint_inited);
         nrfx_atomic_u32_store(&_app_flags.squarepoint_comms_error_count, 0);
         log_printf("INFO: SquarePoint module connection successful\n");
      }
      else if (nrfx_atomic_u32_fetch_add(&_app_flags.squarepoint_comms_error_count, 1) >= SQUAREPOINT_ERROR_NOTIFY_COUNT)
      {
         buzzer_indicate_error();
         nrfx_atomic_u32_store(&_app_flags.squarepoint_comms_error_count, 0);
      }
   }

   // Determine if the SquarePoint module has frozen
   if (!nrfx_atomic_flag_fetch(&_app_flags.squarepoint_enabled))
      nrfx_atomic_flag_clear(&_app_flags.squarepoint_needs_reset);
   else if (nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running) &&
         (nrfx_atomic_u32_fetch_add(&_app_flags.squarepoint_timeout_counter, 1) > APP_RUNNING_RESPONSE_TIMEOUT_SEC))
      nrfx_atomic_flag_set(&_app_flags.squarepoint_needs_reset);
}

static void squarepoint_data_handler(uint8_t *data, uint32_t len)
{
   // Handle the incoming message
   log_printf("INFO: SquarePoint module sent ");
   switch (data[0])
   {
      case SQUAREPOINT_INCOMING_RANGES:
      {
         // Display the callback reason
         log_printf("RANGES, included number of ranges: %i\n", data[1]);
         const uint8_t packet_overhead = 2 + SQUAREPOINT_EUI_LEN, num_ranges = data[1];
         uint32_t range = 0, epoch = 0;

         // Output the received ranging data
         for (uint8_t i = 0; i < num_ranges; ++i)
            if (memcmp(data + packet_overhead + (i * APP_LOG_RANGE_LENGTH), ble_get_empty_eui(), SQUAREPOINT_EUI_LEN))
            {
               uint8_t offset = packet_overhead + (i * APP_LOG_RANGE_LENGTH);
               memcpy(&range, data + offset + SQUAREPOINT_EUI_LEN, sizeof(range));
               log_printf("INFO:     Device %02X with millimeter range %lu\n", data[offset + 0], range);
            }

         // Copy the ranging data to the ranging buffer
         _range_buffer_length = (uint16_t)MIN(len - 1, APP_BLE_BUFFER_LENGTH);
         memcpy(_range_buffer, data + 1, _range_buffer_length);

         // Update the application epoch
         memcpy(&epoch, data + packet_overhead + (num_ranges * APP_LOG_RANGE_LENGTH), sizeof(epoch));
         if ((epoch > MINIMUM_VALID_TIMESTAMP) && (epoch < MAXIMUM_VALID_TIMESTAMP))
         {
            rtc_set_current_time(epoch);
            log_printf("INFO:     Updated current epoch time: %lu\n", epoch);
         }
         else
         {
            epoch = rtc_get_current_time();
            memcpy(data + packet_overhead + (num_ranges * APP_LOG_RANGE_LENGTH), &epoch, sizeof(epoch));
         }


         // Update the scheduler EUI
         ble_set_scheduler_eui(data + 2, SQUAREPOINT_EUI_LEN);

         // Trigger SD log file storage from the main loop
         nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
         nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
         nrfx_atomic_flag_set(&_app_flags.range_buffer_updated);
         break;
      }
      case SQUAREPOINT_INCOMING_CALIBRATION:
      {
         // Copy the calibration data to the ranging buffer
         _range_buffer_length = (uint16_t)MIN(len - 1, APP_BLE_BUFFER_LENGTH);
         memcpy(_range_buffer, data + 1, _range_buffer_length);

         // Trigger BLE transmission from the main loop
         nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
         nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
         nrfx_atomic_flag_set(&_app_flags.range_buffer_updated);
         break;
      }
      case SQUAREPOINT_INCOMING_STOPPED:
      {
         // Acknowledge the information message
         log_printf("STOPPED, setting module to STOPPED\n");
         squarepoint_ack();

         // Update the app state
         ble_clear_scheduler_eui();
         ble_reset_discovered_devices();
         nrfx_atomic_flag_clear(&_app_flags.squarepoint_running);
         nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
         break;
      }
      case SQUAREPOINT_INCOMING_WAKEUP:
      {
         // Start the wakeup timer
         log_printf("WAKEUP, setting module wake-up timer\n");
         uint16_t wakeup_delay = 4 * (uint16_t)data[1];
         if (wakeup_timer_start(wakeup_delay) != NRF_SUCCESS)
            log_printf("ERROR: Unable to start module wake-up timer!!\n");
         break;
      }
      case SQUAREPOINT_INCOMING_REQUEST_TIME:
      {
         log_printf("REQUEST_TIME, responding with current timestamp\n");
         nrfx_atomic_flag_set(&_app_flags.squarepoint_time_epoch_requested);
         nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
         nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
         break;
      }
      case SQUAREPOINT_INCOMING_PING:
      {
         log_printf("PING, responding with keep-alive acknowledgment\n");
         squarepoint_ack();
         break;
      }
      default:
         log_printf("UNKNOWN, unknown interrupt reason!\n");
         break;
   }
}

static void imu_data_handler(void)
{
   // Read data from the accelerometer
   if (imu_read_accelerometer_data(NULL, NULL, NULL) == NRFX_SUCCESS)
   {
      // Log a change in motion
      if (!nrfx_atomic_flag_set_fetch(&_app_flags.device_in_motion))
         log_motion(true, rtc_get_current_time(), false);
   }
   else if (nrfx_atomic_flag_clear_fetch(&_app_flags.device_in_motion))
      log_motion(false, rtc_get_current_time(), false);
}


// Main function -------------------------------------------------------------------------------------------------------

int main(void)
{
   // Initialize hardware services and start the watchdog timer
   hardware_init();
   APP_ERROR_CHECK(watchdog_timer_start(WATCHDOG_CHECK_RATE_MS));

   // Loop forever
   bool charger_plugged_in = false;
   uint32_t app_enabled = 1, app_running = 0, network_discovered = 0, hfclk_running = 0;
   while (1)
   {
      // Go to sleep until something happens
      if ((sd_clock_hfclk_is_running(&hfclk_running) == NRF_SUCCESS) && hfclk_running)
         sd_clock_hfclk_release();
      nrf_pwr_mgmt_run();

      // Check if the SquarePoint module has communicated over TWI
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_data_received))
         squarepoint_handle_incoming_data();

      // Handle USB communications
      usb_handle_comms();

      // Check if IMU data is ready to be read
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.imu_data_ready))
         imu_data_handler();

      // Check if new ranging data was received
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.range_buffer_updated))
      {
         log_ranges(_range_buffer, _range_buffer_length);
         ble_update_ranging_data(_range_buffer, &_range_buffer_length);
      }

      // Handle any timer interrupts
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_wakeup_triggered) && (squarepoint_wakeup_radio() != NRFX_SUCCESS))
         log_printf("ERROR: Failed to send wake-up command to the SquarePoint module!\n");
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_time_epoch_requested))
      {
         uint32_t current_time = rtc_get_current_time();
         log_printf("INFO: Sending timestamp %lu to SquarePoint\n", current_time);
         if (squarepoint_set_time(current_time) != NRFX_SUCCESS)
            log_printf("ERROR: Failed to send current timestamp to the SquarePoint module!\n");
      }

      // Check on current battery voltage levels
      app_running = nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running);
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.battery_check_time))
      {
         uint16_t batt_mv = battery_monitor_get_level_mV();
         if (batt_mv < BATTERY_VOLTAGE_CRITICAL)
            log_printf("WARNING: Battery voltage is getting critically low @ %hu mV!\n", batt_mv);
         else
            log_printf("INFO: Battery voltage currently %hu mV\n", batt_mv);
         log_battery(batt_mv, rtc_get_current_time(), !app_running);
      }

      // Check if the battery charging status has changed
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.battery_status_changed))
      {
         // Indicate a change in charging status using the buzzer and log the event
         bool is_plugged_in = battery_monitor_is_plugged_in(), is_charging = battery_monitor_is_charging();
         if (charger_plugged_in != is_plugged_in)
            buzzer_indicate_plugged_status(is_plugged_in);
         log_charging(is_plugged_in, is_charging, rtc_get_current_time(), !app_running);
         usb_change_power_status(is_plugged_in);
         charger_plugged_in = is_plugged_in;

         // Disable SquarePoint and LEDs if charging or plugged in
#ifdef STOP_BLE_AND_SQUAREPOINT_WHEN_CHARGING
         if (is_plugged_in || is_charging)
         {
            leds_disable();
            nrfx_atomic_flag_clear(&_app_flags.squarepoint_enabled);
         }
         else
         {
            leds_enable();
            nrfx_atomic_flag_set(&_app_flags.squarepoint_enabled);
         }
#endif
      }

      // Check if the SquarePoint module should be started or stopped based on runtime status and network discovery
      network_discovered = ble_is_network_available();
      app_enabled = nrfx_atomic_flag_fetch(&_app_flags.squarepoint_enabled);
      if (app_enabled && network_discovered && !app_running)
         start_squarepoint();
      else if (!app_enabled && app_running)
         squarepoint_stop();
#ifdef BLE_CALIBRATION
      else if (!app_running && (nrfx_atomic_u32_fetch(&_app_flags.calibration_index) != BLE_CALIBRATION_INDEX_INVALID))
         start_squarepoint_calibration();
#endif

      // Update the BLE advertising and scanning states
      if (!app_enabled)
      {
         if (nrfx_atomic_flag_clear_fetch(&_app_flags.bluetooth_is_scanning))
            ble_stop_scanning();
         if (nrfx_atomic_flag_clear_fetch(&_app_flags.bluetooth_is_advertising))
            ble_stop_advertising();
      }
      else
      {
         if (!nrfx_atomic_flag_set_fetch(&_app_flags.bluetooth_is_advertising) && (ble_start_advertising() != NRF_SUCCESS))
            nrfx_atomic_flag_clear(&_app_flags.bluetooth_is_advertising);
#ifndef BLE_CALIBRATION
         if (!app_running && !nrfx_atomic_flag_set_fetch(&_app_flags.bluetooth_is_scanning) && (ble_start_scanning() != NRF_SUCCESS))
            nrfx_atomic_flag_clear(&_app_flags.bluetooth_is_scanning);
         else if (app_running && nrfx_atomic_flag_clear_fetch(&_app_flags.bluetooth_is_scanning))
            ble_stop_scanning();
#endif
      }

      // If the SquarePoint module appears to have crashed, try to reset it and re-discover networks
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_needs_reset))
      {
         // Update the app state and reset the SquarePoint module
         log_printf("INFO: SquarePoint communications appear to be down...restarting\n");
         ble_clear_scheduler_eui();
         ble_reset_discovered_devices();
         nrfx_atomic_flag_clear(&_app_flags.squarepoint_running);
         nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
         squarepoint_stop();
      }

      // Update the LED status indicators
      update_leds(app_running, network_discovered);
   }
}
