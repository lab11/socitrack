// Header inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"
#include "ble_gap.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "nrf_delay.h"
#include "nrfx_wdt.h"
#include "rtc.h"
#include "rtc_external.h"
#include "sd_card.h"
#include "squarepoint_interface.h"
#include "system.h"
#include "timers.h"


// Application state variables -----------------------------------------------------------------------------------------

static app_flags_t _app_flags = { 0 };
static bool _charger_plugged_in = false;
static uint8_t _range_buffer[APP_BLE_MAX_BUFFER_LENGTH] = { 0 };
static volatile uint16_t _range_buffer_length = 0;


// Helper functions ----------------------------------------------------------------------------------------------------

static void watchdog_handler(void *p_context);
static uint32_t squarepoint_data_handler(uint8_t *data, uint32_t len, uint32_t timestamp);
static void imu_data_handler(bool in_motion, uint32_t timestamp, float* x_accel_data, float* y_accel_data, float* z_accel_data);

static void app_init(void)
{
   // Initialize flags that are not defaulted to false or 0
   _app_flags.squarepoint_enabled = true;
   _app_flags.battery_status_changed = true;
   _app_flags.device_in_motion = true;
   _app_flags.battery_too_low = true;
   _app_flags.battery_check_counter = APP_BATTERY_CHECK_TIMEOUT_SEC;
}

static void spi_init(void)
{
   // Set up and deactivate all Chip Selects (CS)
   nrfx_gpiote_out_config_t cs_sd_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_gpiote_out_config_t cs_imu_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_gpiote_out_config_t cs_rtc_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   APP_ERROR_CHECK(nrfx_gpiote_out_init(SD_CARD_SPI_CS, &cs_sd_pin_config));
   APP_ERROR_CHECK(nrfx_gpiote_out_init(IMU_SPI_CS, &cs_imu_pin_config));
   APP_ERROR_CHECK(nrfx_gpiote_out_init(RTC_SPI_CS, &cs_rtc_pin_config));

   // Make sure the SD Card starts out disabled
   nrfx_gpiote_out_config_t sd_enable_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   APP_ERROR_CHECK(nrfx_gpiote_out_init(SD_CARD_ENABLE, &sd_enable_pin_config));
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
      if (squarepoint_init(squarepoint_data_handler, ble_get_eui()) == NRFX_SUCCESS)
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
   // Initialize the GPIO subsystem, SPI chip selects, LEDs, and the application state
   initialize_gpio();
   spi_init();
   leds_init();
   led_on(RED);
   app_init();

   // Check and clear the reason for the chip reset
   printf("\n----------------------------------------------\n");
   printf("INFO: Chip experienced a reset with reason %lu\n", NRF_POWER->RESETREAS);
   NRF_POWER->RESETREAS = 0xFFFFFFFF;
   printf("INFO: Initializing nRF...\n");

   // Initialize the application timers and Bluetooth stack via the SoftDevice
   timers_init(watchdog_handler, &_app_flags.squarepoint_wakeup_triggered);
   ble_init(&_app_flags.bluetooth_is_advertising, &_app_flags.bluetooth_is_scanning, &_app_flags.sd_card_maintenance_mode);

   // Initialize essential hardware components
   rtc_init();
   buzzer_init();
   bool rtc_initialized = rtc_external_init();
   battery_monitor_init(&_app_flags.battery_status_changed);

   // Ensure that the RTC timestamp is valid
   uint32_t current_timestamp = rtc_get_current_time();
   if (rtc_initialized && (current_timestamp > MINIMUM_VALID_TIMESTAMP) && (current_timestamp < MAXIMUM_VALID_TIMESTAMP))
      nrfx_atomic_flag_set(&_app_flags.rtc_time_valid);
   else
   {
      printf("ERROR: RTC chip returned an impossible Unix timestamp: %lu\n", current_timestamp);
      buzzer_indicate_invalid_rtc_time();
   }

   // Initialize the IMU and wait until an SD card is inserted
   uint32_t num_retries = 3;
   imu_init(imu_data_handler);
   while (!sd_card_init(&_app_flags.sd_card_inserted, ble_get_eui()) && num_retries--)
   {
      buzzer_indicate_error();
      nrf_delay_ms(5000);
   }
   sd_card_create_log(nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid) ? current_timestamp : 0, true);
   printf("INFO: Initialized all hardware and software services\n");

   // Initialize communications with the SquarePoint module
   squarepoint_comms_init();
}

static void update_leds(uint32_t network_discovered)
{
   if (!nrfx_atomic_flag_fetch(&_app_flags.sd_card_inserted))            // RED = SD card not inserted
      led_on(RED);
   else if (nrfx_atomic_flag_fetch(&_app_flags.sd_card_maintenance_mode))// PURPLE = SD card maintenance mode over BLE
      led_on(PURPLE);
   else if (nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))     // GREEN = App running
      led_on(GREEN);
   else if (network_discovered)                                          // ORANGE = Network discovered, app not running
      led_on(ORANGE);
   else                                                                  // BLUE = No network discovered
      led_on(BLUE);
}

static nrfx_err_t start_squarepoint(uint32_t timestamp)
{
   // Attempt to resynchronize the internal clock from the RTC chip
   log_printf("INFO: Synchronizing internal clock from RTC...\n");
   uint32_t rtc_timestamp = rtc_external_sync_to_internal();
   if (rtc_timestamp > 0)
      timestamp = rtc_timestamp;
   else
      log_printf("WARNING: Synchronization failed...using existing internal clock time\n");

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
   nrfx_err_t err_code = squarepoint_start_application(timestamp, ble_get_device_role(), scheduler_role);
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

   // Ensure that the main loop has not become frozen
   if (nrfx_atomic_u32_fetch(&_app_flags.seconds_in_loop_iteration) &&
         (nrfx_atomic_u32_add(&_app_flags.seconds_in_loop_iteration, 1) >= WATCHDOG_FROZEN_LOOP_MAX_SECONDS))
      nrfx_atomic_flag_set(&_app_flags.device_reset_required);

   // Reset the device if required
   if (nrfx_atomic_flag_fetch(&_app_flags.device_reset_required))
   {
      sd_card_flush();
      while (true);
   }
}

static uint32_t squarepoint_data_handler(uint8_t *data, uint32_t len, uint32_t timestamp)
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
         _range_buffer_length = (uint16_t)MIN(len - 1, APP_BLE_MAX_BUFFER_LENGTH);
         memcpy(_range_buffer, data + 1, _range_buffer_length);

         // Update the application epoch
         memcpy(&epoch, data + packet_overhead + (num_ranges * APP_LOG_RANGE_LENGTH), sizeof(epoch));
         if ((epoch > MINIMUM_VALID_TIMESTAMP) && (epoch < MAXIMUM_VALID_TIMESTAMP))
         {
            timestamp = epoch;
            rtc_set_current_time(epoch);
            log_printf("INFO:     Updated current epoch time: %lu\n", epoch);
         }
         else
            memcpy(data + packet_overhead + (num_ranges * APP_LOG_RANGE_LENGTH), &timestamp, sizeof(timestamp));

         // Update the scheduler EUI
         if (ble_set_scheduler_eui(data + 2, SQUAREPOINT_EUI_LEN))
         {
            // Scheduler EUI changed so there must have been a network conflict
            log_printf("WARNING: Scheduler EUI changed unexpectedly due to a network conflict...restarting\n");
            nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, APP_RUNNING_RESPONSE_TIMEOUT_SEC + 10);
         }
         else
         {
            // Store the received ranges to the SD card
            sd_card_log_ranges(_range_buffer, _range_buffer_length);
            ble_update_ranging_data(_range_buffer, _range_buffer_length);
         }

         // Reset the SquarePoint communications timeout counter
         if (num_ranges)
            nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
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
   return timestamp;
}

static void imu_data_handler(bool in_motion, uint32_t timestamp, float* x_accel_data, float* y_accel_data, float* z_accel_data)
{
   // Handle detection of changes in motion
   if (in_motion)
   {
      if (!nrfx_atomic_flag_set_fetch(&_app_flags.device_in_motion))
         sd_card_log_motion(true, timestamp, false);
   }
   else if (nrfx_atomic_flag_clear_fetch(&_app_flags.device_in_motion))
      sd_card_log_motion(false, timestamp, false);
}


// Main functionality --------------------------------------------------------------------------------------------------

void normal_mode_process(void)
{
   // Handle any incoming SquarePoint module data
   uint32_t current_timestamp = squarepoint_handle_incoming_data(rtc_get_current_time());

   // Handle any new IMU data
   imu_handle_incoming_data(current_timestamp);

   // Handle any SquarePoint maintenance tasks
   if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_wakeup_triggered) && (squarepoint_wakeup_radio() != NRFX_SUCCESS))
      log_printf("ERROR: Failed to send wake-up command to the SquarePoint module!\n");
   if (nrfx_atomic_flag_clear_fetch(&_app_flags.squarepoint_time_epoch_requested))
   {
      log_printf("INFO: Sending timestamp %lu to SquarePoint\n", current_timestamp);
      if (squarepoint_set_time(current_timestamp) != NRFX_SUCCESS)
         log_printf("ERROR: Failed to send current timestamp to the SquarePoint module!\n");
   }

   // Perform second-aligned BLE tasks
   if (nrfx_atomic_flag_clear_fetch(&_app_flags.elapsed_second))
      ble_second_has_elapsed();

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
      sd_card_log_battery(batt_mv, current_timestamp, !nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running));
   }

   // Check if the battery charging status has changed
   if (nrfx_atomic_flag_clear_fetch(&_app_flags.battery_status_changed))
   {
      // Indicate a change in charging status using the buzzer and log the event
      bool is_plugged_in = battery_monitor_is_plugged_in(), is_charging = battery_monitor_is_charging();
      if (_charger_plugged_in != is_plugged_in)
         buzzer_indicate_plugged_status(is_plugged_in);
      sd_card_log_charging(is_plugged_in, is_charging, current_timestamp, !nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running));
      _charger_plugged_in = is_plugged_in;

      // Disable SquarePoint if charging or plugged in
#ifdef STOP_BLE_AND_SQUAREPOINT_WHEN_CHARGING
      if (is_plugged_in || is_charging)
         nrfx_atomic_flag_clear(&_app_flags.squarepoint_enabled);
      else
         nrfx_atomic_flag_set(&_app_flags.squarepoint_enabled);
#endif
   }

   // Check if the SquarePoint module should be started or stopped based on runtime status and network discovery
   uint32_t app_enabled = nrfx_atomic_flag_fetch(&_app_flags.squarepoint_enabled) && !nrfx_atomic_flag_fetch(&_app_flags.battery_too_low);
   if (app_enabled && ble_is_network_available() && !nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))
   {
      // Either start SquarePoint or request the correct RTC time based on our current status
      if (nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid))
         start_squarepoint(current_timestamp);
      else if (((current_timestamp = ble_request_timestamp()) > 0) && rtc_external_set_timestamp(current_timestamp))
      {
         sd_card_log_updated_epoch(current_timestamp);
         sd_card_create_log(current_timestamp, false);
         nrfx_atomic_flag_set(&_app_flags.rtc_time_valid);
         log_printf("INFO: RTC clock was successfully set to the current timestamp\n");
      }
   }
   else if (!app_enabled && nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))
      squarepoint_stop();

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
      if (!nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_advertising) && nrfx_atomic_flag_fetch(&_app_flags.rtc_time_valid))
         ble_start_advertising();
      if (nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running))
      {
         if (nrfx_atomic_flag_fetch(&_app_flags.bluetooth_single_scanning) && !nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
            ble_start_scanning();
         else if (!nrfx_atomic_flag_fetch(&_app_flags.bluetooth_single_scanning) && nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
            ble_stop_scanning();
      }
      else if (!nrfx_atomic_flag_fetch(&_app_flags.bluetooth_is_scanning))
         ble_start_scanning();
   }

   // If the SquarePoint module appears to have crashed, try to reset it and re-discover networks
   if (app_enabled && nrfx_atomic_flag_fetch(&_app_flags.squarepoint_running) && (nrfx_atomic_u32_fetch(&_app_flags.squarepoint_timeout_counter) > APP_RUNNING_RESPONSE_TIMEOUT_SEC))
   {
      // Update the app state and reset the SquarePoint module
      log_printf("INFO: SquarePoint communications appear to be down...restarting SquarePoint\n");
      nrfx_atomic_u32_store(&_app_flags.squarepoint_timeout_counter, 0);
      nrfx_atomic_flag_clear(&_app_flags.squarepoint_running);
      ble_clear_scheduler_eui();
      squarepoint_stop();
   }

   // Update the LED status indicators
   update_leds(ble_is_network_available());
}

int main(void)
{
   // Initialize hardware services and start the watchdog timer
   hardware_init();
   APP_ERROR_CHECK(watchdog_timer_start(WATCHDOG_CHECK_RATE_MS));

   // Loop forever
   while (true)
   {
      // Go to sleep until something happens
      sd_app_evt_wait();
      //TODO: printf("Processing Something: %lu\n", rtc_get_current_time());

      // Prohibit all main-loop activity while externally interfacing with the SD card
      if (nrfx_atomic_flag_fetch(&_app_flags.sd_card_maintenance_mode))
      {
         update_leds(0);
         ble_sd_card_maintenance();
      }
      else
      {
         nrfx_atomic_u32_store(&_app_flags.seconds_in_loop_iteration, 1);
         normal_mode_process();
         nrfx_atomic_u32_store(&_app_flags.seconds_in_loop_iteration, 0);
      }
   }
}
