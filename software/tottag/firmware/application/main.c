// Header inclusions ---------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

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

#pragma GCC diagnostic pop


// Application state variables -----------------------------------------------------------------------------------------

static app_flags_t _app_flags = { 0 };
static nrf_drv_spi_t _rtc_sd_spi_instance = NRF_DRV_SPI_INSTANCE(RTC_SD_SPI_BUS_IDX);
static nrf_drv_spi_t _imu_spi_instance = NRF_DRV_SPI_INSTANCE(IMU_SPI_BUS_IDX);
static uint8_t _range_buffer[APP_BLE_BUFFER_LENGTH] = { 0 };
static volatile uint16_t _range_buffer_length = 0;


// Helper functions ----------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdate-time"

static void watchdog_handler(void *p_context);
static void squarepoint_data_handler(uint8_t *data, uint32_t len);
static void imu_data_handler(bool in_motion, float* x_accel_data, float* y_accel_data, float* z_accel_data);

static void app_init(void)
{
   // Initialize flags that are not defaulted to false or 0
   _app_flags.squarepoint_enabled = true;
   _app_flags.sd_card_inserted = true;
   _app_flags.battery_status_changed = true;
   _app_flags.device_in_motion = true;
   _app_flags.battery_too_low = true;
   _app_flags.battery_check_counter = APP_BATTERY_CHECK_TIMEOUT_SEC;
   _app_flags.calibration_index = BLE_CALIBRATION_INDEX_INVALID;
}

static void spi_init(void)
{
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
}

static void squarepoint_comms_init(void)
{
   // Log the SquarePoint initialization attempt
   log_printf("INFO: Connecting to the SquarePoint module...\n");
   uint32_t repeat_failure = 0;
   bool needs_init = true;

   // Initialize the SquarePoint module
   while (needs_init)
   {
      if (squarepoint_init(&_app_flags.squarepoint_data_received, squarepoint_data_handler, ble_get_eui()) == NRFX_SUCCESS)
      {
         log_printf("INFO: SquarePoint module connection successful\n");
         needs_init = false;
      }
      else
      {
         if ((++repeat_failure % 100) == 0)
         {
            log_printf("ERROR: SquarePoint module connection unsuccessful!\n");
            buzzer_indicate_error();
         }
         nrf_delay_ms(50);
      }
   }
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
   if (ble_init(&_app_flags.bluetooth_is_advertising, &_app_flags.bluetooth_is_scanning, &_app_flags.calibration_index) != NRF_SUCCESS)
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

   // Enable the external Real-Time Clock and ensure that the fetched timestamp is valid
   rtc_external_init(&_rtc_sd_spi_instance);
   uint32_t current_timestamp = rtc_get_current_time(), num_retries = 3;
   while (--num_retries && ((current_timestamp < MINIMUM_VALID_TIMESTAMP) || (current_timestamp > MAXIMUM_VALID_TIMESTAMP)))
   {
      printf("ERROR: RTC chip returned an impossible Unix timestamp: %lu\n", current_timestamp);
      rtc_external_init(&_rtc_sd_spi_instance);
      buzzer_indicate_invalid_rtc_time();
      nrf_delay_ms(2000);
      current_timestamp = rtc_get_current_time();
   }
   if ((current_timestamp > MINIMUM_VALID_TIMESTAMP) && (current_timestamp < MAXIMUM_VALID_TIMESTAMP))
      nrfx_atomic_flag_set(&_app_flags.rtc_time_valid);

   // Initialize supplementary hardware components
   imu_init(&_imu_spi_instance, &_app_flags.imu_data_ready, &_app_flags.imu_motion_changed, imu_data_handler);
   battery_monitor_init(&_app_flags.battery_status_changed);

   // Wait until an SD Card is inserted
   while (!sd_card_init(&_app_flags.sd_card_inserted, ble_get_eui()))
   {
      buzzer_indicate_error();
      nrf_delay_ms(5000);
   }
   sd_card_create_log(nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid) ? rtc_get_current_time() : 0, true);
   printf("INFO: Initialized supplementary hardware and software services\n");

   // Initialize communications with the SquarePoint module
   squarepoint_comms_init();
}

static void update_leds(uint32_t network_discovered)
{
   if (!nrfx_atomic_flag_fetch(&_app_flags.sd_card_inserted))            // RED = SD card not inserted
      led_on(RED);
   else if (nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))     // GREEN = App running
      led_on(GREEN);
   else if (network_discovered)                                          // ORANGE = Network discovered, app not running
      led_on(ORANGE);
   else                                                                  // BLUE = No network discovered
      led_on(BLUE);
}

static nrfx_err_t start_squarepoint(void)
{
   // Initialize the SquarePoint module and communications
   squarepoint_comms_init();

   // Set this device to be the scheduler if we already are or if none exists and we have the highest EUI
   schedule_role_t scheduler_role = PARTICIPANT;
   if ((memcmp(ble_get_scheduler_eui(), ble_get_eui(), BLE_GAP_ADDR_LEN) == 0) ||
         ((memcmp(ble_get_scheduler_eui(), ble_get_empty_eui(), BLE_GAP_ADDR_LEN) == 0) &&
               (memcmp(ble_get_eui(), ble_get_highest_network_eui(), BLE_GAP_ADDR_LEN) > 0)))
         scheduler_role = SCHEDULER;

   // Start the SquarePoint ranging application
   log_printf("INFO: Starting the SquarePoint module...\n");
   nrfx_err_t err_code = squarepoint_start_application(rtc_get_current_time(), ble_get_device_role(), scheduler_role);
   if (err_code != NRFX_SUCCESS)
   {
      log_printf("ERROR: Failed to start the SquarePoint ranging app!\n");
      return err_code;
   }
   else
   {
      // Update the application state
      ble_stop_scanning();
      log_printf("INFO: SquarePoint module successfully started!\n");
      nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
      nrfx_atomic_flag_clear(&_app_flags.bluetooth_single_scanning);
      nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
      nrfx_atomic_u32_store(&_app_flags.bluetooth_single_scan_timer, BLE_SINGLE_SCAN_INTERVAL_SEC);
   }
   return NRFX_SUCCESS;
}

#ifdef BLE_CALIBRATION
static nrfx_err_t start_squarepoint_calibration(void)
{
   // Initialize the SquarePoint module and communications
   squarepoint_comms_init();

   // Start the SquarePoint calibration application
   printf("INFO: Starting the SquarePoint module in calibration mode...\n");
   nrfx_err_t err_code = squarepoint_start_calibration((uint8_t)nrfx_atomic_u32_fetch(&_app_flags.calibration_index));
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
      nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
   }
   return NRFX_SUCCESS;
}
#endif


// Callbacks and data handlers -----------------------------------------------------------------------------------------

static void watchdog_handler(void *p_context)     // This function is triggered every WATCHDOG_CHECK_RATE (currently 1s)
{
   // Feed the watchdog timer and set the second-elapsed flag
   nrfx_wdt_feed();
   nrfx_atomic_flag_set(&_app_flags.elapsed_second);

   // Decrement the single-scan BLE timer and update its state
   if (!nrfx_atomic_u32_sub_hs(&_app_flags.bluetooth_single_scan_timer, 1))
   {
      if (nrfx_atomic_flag_fetch(&_app_flags.bluetooth_single_scanning))
      {
         nrfx_atomic_flag_clear(&_app_flags.bluetooth_single_scanning);
         nrfx_atomic_u32_store(&_app_flags.bluetooth_single_scan_timer, BLE_SINGLE_SCAN_INTERVAL_SEC);
      }
      else
      {
         nrfx_atomic_flag_set(&_app_flags.bluetooth_single_scanning);
         nrfx_atomic_u32_store(&_app_flags.bluetooth_single_scan_timer, BLE_SINGLE_SCAN_DURATION_SEC);
      }
   }

   // Increment the battery voltage check timeout counter
   nrfx_atomic_u32_add(&_app_flags.battery_check_counter, 1);

   // Increment the SquarePoint range reception timeout counter
   nrfx_atomic_u32_add(&_app_flags.squarepoint_timeout_counter, 1);

   // Reset the device if required
   if (nrfx_atomic_flag_fetch(&_app_flags.device_reset_required))
      while (true);
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
         nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
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
         if (ble_set_scheduler_eui(data + 2, SQUAREPOINT_EUI_LEN))
         {
            // Scheduler EUI changed so there must have been a network conflict
            log_printf("WARNING: Scheduler EUI changed unexpectedly due to a network conflict...restarting\n");
            nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, APP_RUNNING_RESPONSE_TIMEOUT_SEC + 10);
         }
         else       // Trigger SD log file storage from the main loop
            nrfx_atomic_flag_set(&_app_flags.range_buffer_updated);

         // Reset the SquarePoint communications timeout counter
         if (num_ranges)
            nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
         break;
      }
      case SQUAREPOINT_INCOMING_CALIBRATION:
      {
         // Copy the calibration data to the ranging buffer
         _range_buffer_length = (uint16_t)MIN(len - 1, APP_BLE_BUFFER_LENGTH);
         memcpy(_range_buffer, data + 1, _range_buffer_length);

         // Trigger BLE transmission from the main loop
         nrfx_atomic_flag_set(&_app_flags.squarepoint_running);
         nrfx_atomic_flag_set(&_app_flags.range_buffer_updated);
         break;
      }
      case SQUAREPOINT_INCOMING_STOPPED:
      {
         // Acknowledge the information message
         log_printf("STOPPED, setting module to STOPPED\n");
         squarepoint_ack();

         // Update the app state
         nrfx_atomic_flag_clear(&_app_flags.squarepoint_running);
         ble_clear_scheduler_eui();
         sd_card_flush();
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

static void imu_data_handler(bool in_motion, float* x_accel_data, float* y_accel_data, float* z_accel_data)
{
   // Handle changes in detection of motion
   if (in_motion)
   {
      if (!nrfx_atomic_flag_set_fetch(&_app_flags.device_in_motion))
         sd_card_log_motion(true, rtc_get_current_time(), false);
   }
   else if (nrfx_atomic_flag_clear_fetch(&_app_flags.device_in_motion))
      sd_card_log_motion(false, rtc_get_current_time(), false);
}


// Main function -------------------------------------------------------------------------------------------------------

int main(void)
{
   // Initialize hardware services and start the watchdog timer
   hardware_init();
   APP_ERROR_CHECK(watchdog_timer_start(WATCHDOG_CHECK_RATE_MS));

   // Loop forever
   bool charger_plugged_in = false;
   uint32_t app_enabled = 1, current_timestamp = 0;
   while (true)
   {
      // Go to sleep until something happens
      nrf_pwr_mgmt_run();

      // Check if the SquarePoint module has communicated over TWI
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_data_received))
         squarepoint_handle_incoming_data();

      // Handle USB communications
      usb_handle_comms();

      // Check if there is new IMU data to handle
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.imu_motion_changed) || nrfx_atomic_flag_clear_fetch(&_app_flags.imu_data_ready))
         imu_handle_incoming_data();

      // Perform second-aligned tasks
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.elapsed_second))
      {
         // Handle BLE time-based tasks
         ble_second_has_elapsed();

         // Force a hard reset upon expiration of a specified number of seconds
#if defined(DEVICE_FORCE_RESET_INTERVAL_SEC) && (DEVICE_FORCE_RESET_INTERVAL_SEC > 0)
         if (nrfx_atomic_u32_add(&_app_flags.device_reset_counter, 1) > DEVICE_FORCE_RESET_INTERVAL_SEC)
         {
            sd_card_flush();
            nrfx_atomic_flag_set(&_app_flags.device_reset_required);
         }
#endif
      }

      // Check if new ranging data was received
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.range_buffer_updated))
      {
         sd_card_log_ranges(_range_buffer, _range_buffer_length);
         ble_update_ranging_data(_range_buffer, _range_buffer_length);
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
      if (nrfx_atomic_u32_fetch(&_app_flags.battery_check_counter) >= APP_BATTERY_CHECK_TIMEOUT_SEC)
      {
         uint16_t batt_mv = battery_monitor_get_level_mV();
         if (batt_mv <= BATTERY_VOLTAGE_CRITICAL)
         {
            log_printf("WARNING: Battery voltage is getting critically low @ %hu mV!\n", batt_mv);
            nrfx_atomic_u32_store(&_app_flags.battery_check_counter, (APP_BATTERY_CHECK_TIMEOUT_SEC < 60) ? 0 : (APP_BATTERY_CHECK_TIMEOUT_SEC - 60));
            nrfx_atomic_flag_set(&_app_flags.battery_too_low);
            buzzer_indicate_low_battery();
         }
         else
         {
            log_printf("INFO: Battery voltage currently %hu mV\n", batt_mv);
            nrfx_atomic_u32_store(&_app_flags.battery_check_counter, 0);
            nrfx_atomic_flag_clear(&_app_flags.battery_too_low);
         }
         sd_card_log_battery(batt_mv, rtc_get_current_time(), !nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running));
      }

      // Check if the battery charging status has changed
      if (nrfx_atomic_flag_clear_fetch(&_app_flags.battery_status_changed))
      {
         // Indicate a change in charging status using the buzzer and log the event
         bool is_plugged_in = battery_monitor_is_plugged_in(), is_charging = battery_monitor_is_charging();
         if (charger_plugged_in != is_plugged_in)
            buzzer_indicate_plugged_status(is_plugged_in);
         sd_card_log_charging(is_plugged_in, is_charging, rtc_get_current_time(), !nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running));
         usb_change_power_status(is_plugged_in);
         charger_plugged_in = is_plugged_in;

         // Disable SquarePoint if charging or plugged in
#ifdef STOP_BLE_AND_SQUAREPOINT_WHEN_CHARGING
         if (is_plugged_in || is_charging)
            nrfx_atomic_flag_clear(&_app_flags.squarepoint_enabled);
         else
            nrfx_atomic_flag_set(&_app_flags.squarepoint_enabled);
#endif
      }

      // Check if the SquarePoint module should be started or stopped based on runtime status and network discovery
      app_enabled = nrfx_atomic_flag_fetch(&_app_flags.squarepoint_enabled) && !nrfx_atomic_flag_fetch(&_app_flags.battery_too_low);
      if (app_enabled && ble_is_network_available() && !nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))
      {
         // Either start SquarePoint or request the correct RTC time based on our current status
         if (nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid))
            start_squarepoint();
         else if ((current_timestamp = ble_request_timestamp()) > 0)
         {
            sd_card_log_updated_epoch(current_timestamp);
            sd_card_create_log(current_timestamp, false);
            nrfx_atomic_flag_set(&_app_flags.rtc_time_valid);
            ab1815_set_timestamp(current_timestamp);
            log_printf("INFO: Setting timestamp to the network response: %lu\n", current_timestamp);
         }
      }
      else if (!app_enabled && nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))
         squarepoint_stop();
#ifdef BLE_CALIBRATION
      else if (!nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running) &&
              (nrfx_atomic_u32_fetch(&_app_flags.calibration_index) != BLE_CALIBRATION_INDEX_INVALID))
         start_squarepoint_calibration();
#endif

      // Update the BLE advertising and scanning states
      if (!app_enabled)
      {
         if (nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
            ble_stop_scanning();
         if (nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_advertising))
            ble_stop_advertising();
      }
      else
      {
         if (!nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_advertising) && nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid) && !nrfx_atomic_flag_fetch(&_app_flags.battery_too_low))
            ble_start_advertising();
#ifndef BLE_CALIBRATION
         if (nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))
         {
            if (nrfx_atomic_flag_fetch(&_app_flags.bluetooth_single_scanning) && !nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
               ble_start_scanning();
            else if (!nrfx_atomic_flag_fetch(&_app_flags.bluetooth_single_scanning) && nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
               ble_stop_scanning();
         }
         else if (!nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
            ble_start_scanning();
#endif
      }

      // If the SquarePoint module appears to have crashed, try to reset it and re-discover networks
      if (app_enabled && nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running) &&
            (nrfx_atomic_u32_fetch(&_app_flags.squarepoint_timeout_counter) > APP_RUNNING_RESPONSE_TIMEOUT_SEC))
      {
         // Update the app state and reset the SquarePoint module
         log_printf("INFO: SquarePoint communications appear to be down...restarting\n");
         nrfx_atomic_flag_clear(&_app_flags.squarepoint_running);
         ble_clear_scheduler_eui();
         squarepoint_stop();
         sd_card_flush();
      }

      // Update the LED status indicators
      update_leds(ble_is_network_available());

      // Reset board if the SD card has been removed
      if (!nrfx_atomic_flag_fetch(&_app_flags.sd_card_inserted))
         nrfx_atomic_flag_set(&_app_flags.device_reset_required);
   }
}

#pragma GCC diagnostic pop
