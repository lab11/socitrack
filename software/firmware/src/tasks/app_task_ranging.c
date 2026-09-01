// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "nandlog.h"
#include "ranging.h"
#include "rtc.h"
#include "scheduler.h"
#include "storage_records.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t app_task_handle;
static uint8_t device_uid_short, imu_accuracy;
static uint8_t ble_scan_results[MAX_NUM_RANGING_DEVICES];
static uint32_t download_start_timestamp, download_end_timestamp;
static volatile uint8_t discovered_devices[MAX_NUM_RANGING_DEVICES][1+EUI_LEN];
static volatile uint32_t seconds_to_activate_buzzer;
static volatile battery_event_t pending_plugged_event, pending_charging_event;
static volatile bool pending_plugged_valid, pending_charging_valid, pending_critical_voltage;
static volatile bool devices_found, motion_changed, imu_data_ready;
static volatile uint16_t windows_since_config_verify;
static volatile uint8_t num_discovered_devices;
static int16_t imu_accel_data[3];


// Private Helper Functions --------------------------------------------------------------------------------------------

static void verify_app_configuration(void)
{
   // Verify the current state of the application
   print("INFO: Verifying TotTag application configuration\n");

   // Advertising should always be enabled
   if (!bluetooth_is_advertising())
   {
      bluetooth_start_advertising();
      for (uint32_t i = 0; !bluetooth_is_advertising() && (i < BLE_ADV_TIMEOUT_MS); i += 10)
         vTaskDelay(pdMS_TO_TICKS(10));
      if (!bluetooth_is_advertising())
      {
         bluetooth_reset();
         for (int i = 0; !bluetooth_is_advertising() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
         if (!bluetooth_is_advertising())
            system_reset(false);
      }
   }

   // Verify the current BLE-advertised role
   uint8_t current_role = scheduler_get_current_role();
   if (current_role != bluetooth_get_current_ranging_role())
   {
      bluetooth_set_current_ranging_role(current_role);
      for (uint32_t i = 0; !bluetooth_is_advertising() && (i < BLE_ADV_TIMEOUT_MS); i += 10)
         vTaskDelay(pdMS_TO_TICKS(10));
      if (!bluetooth_is_advertising())
      {
         bluetooth_reset();
         for (int i = 0; !bluetooth_is_advertising() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
         if (!bluetooth_is_advertising())
            system_reset(false);
      }
   }

   // Scanning should only be enabled if we are not already ranging with a network or if we are a Master
   if ((!ranging_active() || (current_role == ROLE_MASTER)) && !bluetooth_is_scanning())
   {
      bluetooth_start_scanning();
      for (uint32_t i = 0; !bluetooth_is_scanning() && (i < BLE_ADV_TIMEOUT_MS); i += 10)
         vTaskDelay(pdMS_TO_TICKS(10));
      if (!bluetooth_is_scanning())
      {
         bluetooth_reset();
         for (int i = 0; !bluetooth_is_scanning() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
         if (!bluetooth_is_scanning())
            system_reset(false);
      }
   }
   else if (ranging_active() && (current_role != ROLE_MASTER) && bluetooth_is_scanning())
   {
      bluetooth_stop_scanning();
      for (uint32_t i = 0; bluetooth_is_scanning() && (i < BLE_ADV_TIMEOUT_MS); i += 10)
         vTaskDelay(pdMS_TO_TICKS(10));
      if (bluetooth_is_scanning())
      {
         bluetooth_reset();
         for (int i = 0; bluetooth_is_scanning() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
         if (bluetooth_is_scanning())
            system_reset(false);
      }
   }
}

static void handle_notification(app_notification_t notification)
{
   // Handle the notification based on which bits are set
   if ((notification & APP_NOTIFY_IMU_EVENT))
   {
      if (motion_changed)
      {
         motion_changed = false;
         const bool in_motion = imu_read_in_motion();
         storage_write_motion_status(in_motion ? IN_MOTION : NOT_IN_MOTION);
         print("INFO: Motion change detected: %s\n", in_motion ? "MOVING" : "STATIONARY");
      }
      if (imu_data_ready)
      {
         // Write IMU data over the BLE characteristic
         imu_data_ready = false;
         print("INFO: IMU data received\n");
         bluetooth_write_imu_data((uint8_t*)imu_accel_data, sizeof(imu_accel_data));

         // Store relevant IMU data
#ifndef _TEST_NO_STORAGE
         storage_write_imu_data((uint8_t*)imu_accel_data, sizeof(imu_accel_data));
#endif
      }
#if REVISION_ID < REVISION_N
      imu_clear_interrupts();
#endif
   }
   if (((notification & APP_NOTIFY_NETWORK_LOST)) || ((notification & APP_NOTIFY_NETWORK_CONNECTED)) ||
       ((notification & APP_NOTIFY_VERIFY_CONFIGURATION)))
      verify_app_configuration();
   if ((notification & APP_NOTIFY_NETWORK_FOUND))
   {
      // Claim the discovery set in one step and work from the copy for the rest of this handler
      uint8_t discovered[MAX_NUM_RANGING_DEVICES][1 + EUI_LEN], num_discovered;
      AM_CRITICAL_BEGIN
      num_discovered = num_discovered_devices;
      memcpy(discovered, (const uint8_t*)discovered_devices, sizeof(discovered));
      devices_found = false;
      AM_CRITICAL_END

      // The producer already bounds this, but it indexes a fixed array from a value two contexts maintain
      if (num_discovered > MAX_NUM_RANGING_DEVICES)
         num_discovered = MAX_NUM_RANGING_DEVICES;

      // Stop scanning for additional devices
      bluetooth_stop_scanning();
      for (uint32_t i = 0; bluetooth_is_scanning() && (i < BLE_ADV_TIMEOUT_MS); i += 10)
         vTaskDelay(pdMS_TO_TICKS(10));
      if (bluetooth_is_scanning())
      {
         bluetooth_reset();
         for (int i = 0; bluetooth_is_scanning() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
            vTaskDelay(pdMS_TO_TICKS(100));
         if (bluetooth_is_scanning())
            system_reset(false);
      }

      // Proceed based on whether we are currently idle or ranging as the Master
      if (scheduler_get_current_role() == ROLE_MASTER)
      {
         // Restart as a Participant if another Master device with a higher ID was located
         for (uint8_t i = 0; i < num_discovered; ++i)
            if ((discovered[i][EUI_LEN] == ROLE_MASTER) && (discovered[i][0] > device_uid_short))
            {
               scheduler_stop();
               while (ranging_active())
                  vTaskDelay(pdMS_TO_TICKS(10));
               ranging_begin(ROLE_PARTICIPANT);
               break;
            }
      }
      else if (!ranging_active())
      {
         // Determine if an actively ranging device was located
         bool ranging_device_located = false, idle_device_located = false;
         for (uint8_t i = 0; !ranging_device_located && (i < num_discovered); ++i)
            if ((discovered[i][EUI_LEN] == ROLE_MASTER) || (discovered[i][EUI_LEN] == ROLE_PARTICIPANT))
               ranging_device_located = true;
            else if (discovered[i][EUI_LEN] == ROLE_IDLE)
               idle_device_located = true;

         // Start the ranging task based on the state of the detected devices
         if (ranging_device_located)
            ranging_begin(ROLE_PARTICIPANT);
         else if (idle_device_located)
         {
            // Search for the non-sleeping device with the highest ID that is higher than our own
            int32_t best_device_idx = -1;
            uint8_t highest_device_id = device_uid_short;
            for (uint8_t i = 0; i < num_discovered; ++i)
               if (discovered[i][0] > highest_device_id)
               {
                  best_device_idx = i;
                  highest_device_id = discovered[i][0];
               }

            // If a potential master candidate device was found, attempt to connect to it
            if (best_device_idx >= 0)
               ranging_begin(ROLE_PARTICIPANT);
            else
               ranging_begin(ROLE_MASTER);
         }
      }

      // Store the scan results and verify the app configuration
      for (uint8_t i = 0; i < num_discovered; ++i)
         ble_scan_results[i] = discovered[i][0];
      storage_write_ble_scan_results(ble_scan_results, num_discovered);
      verify_app_configuration();
   }
   if ((notification & APP_NOTIFY_BATTERY_EVENT))
   {
      // Make note of every latched event
      battery_event_t plugged_event = 0, charging_event = 0;
      bool critical_voltage = false;
      AM_CRITICAL_BEGIN
      if (pending_plugged_valid)
      {
         plugged_event = pending_plugged_event;
         pending_plugged_valid = false;
      }
      if (pending_charging_valid)
      {
         charging_event = pending_charging_event;
         pending_charging_valid = false;
      }
      critical_voltage = pending_critical_voltage;
      pending_critical_voltage = false;
      AM_CRITICAL_END

      // Record charging state first so that a plug transition and the charge state it caused appear in that order
      if (charging_event)
         storage_write_charging_status(charging_event);
      if (plugged_event)
         storage_write_charging_status(plugged_event);
      if (critical_voltage)
         storage_write_charging_status(BATTERY_CRITICAL_VOLTAGE);

      // A charger transition or a critical battery both end the current run
      if (plugged_event || critical_voltage)
         storage_flush_and_shutdown();
   }
   if ((notification & APP_NOTIFY_FIND_MY_TOTTAG_ACTIVATED))
      for (uint32_t seconds = 0; seconds < seconds_to_activate_buzzer; ++seconds)
      {
         buzzer_indicate_location();
         vTaskDelay(pdMS_TO_TICKS(1000));
      }
#ifdef __USE_SEGGER__
   if ((notification & APP_NOTIFY_DOWNLOAD_SEGGER_LOG))
   {
      // Define log file transmission variables
      static uint8_t transmit_buffer[NANDLOG_MAX_PAGE_SIZE_BYTES];
      experiment_details_t details;

      // Transmit the stream header, then every page with its own framing
      nandlog_begin_reading(storage_experiment_ms_from_rtc(download_start_timestamp), storage_experiment_ms_from_rtc(download_end_timestamp));
      storage_retrieve_experiment_details(&details);
      uint32_t total_data_chunks = 0, total_data_length = 0;
      nandlog_read_span(&total_data_chunks, &total_data_length);
      const nandlog_stream_header_t stream_header = {
         .magic = NANDLOG_STREAM_MAGIC,
         .format_version = NANDLOG_FORMAT_VERSION,
         .details_length = (uint16_t)sizeof(details),
         .total_pages = total_data_chunks,
         .total_payload_bytes = total_data_length
      };
      transmit_log_data(&stream_header, sizeof(stream_header));
      transmit_log_data(&details, sizeof(details));

      // Every page is framed with its sequence number, time bounds and payload CRC
      for (uint32_t chunk = 0; chunk < total_data_chunks; ++chunk)
      {
         nandlog_page_header_t page;
         const uint32_t data_length = nandlog_retrieve_next_page(transmit_buffer, &page);
         const nandlog_wire_page_t wire = {
            .seq = page.seq,
            .first_timestamp = page.first_timestamp,
            .last_timestamp = page.last_timestamp,
            .payload_length = page.payload_length,
            .record_count = page.record_count,
            .payload_crc = page.payload_crc
         };
         transmit_log_data(&wire, sizeof(wire));
         if (data_length)
            transmit_log_data(transmit_buffer, data_length);
      }
      nandlog_end_reading();

      // Push the final partial buffer; without this the tail of the transfer never leaves the device
      transmit_log_flush();
   }
#endif  // #ifdef __USE_SEGGER__
}

static void battery_event_handler(battery_event_t battery_event)
{
   // Latch the event by signal so that a plug transition and a charge transition arriving together are both delivered
   switch (battery_event)
   {
      case BATTERY_PLUGGED:
      case BATTERY_UNPLUGGED:
         pending_plugged_event = battery_event;
         pending_plugged_valid = true;
         break;
      case BATTERY_CHARGING:
      case BATTERY_NOT_CHARGING:
         pending_charging_event = battery_event;
         pending_charging_valid = true;
         break;
      case BATTERY_CRITICAL_VOLTAGE:
         pending_critical_voltage = true;
         break;
      default:
         return;
   }
   app_notify(APP_NOTIFY_BATTERY_EVENT);
}

static void motion_change_handler(bool in_motion)
{
   // Notify the app about a change in motion
   motion_changed = true;
   app_notify(APP_NOTIFY_IMU_EVENT);
}

static void imu_service_request_handler(void)
{
   app_notify(APP_NOTIFY_IMU_EVENT);
}

static void data_ready_handler(imu_data_type_t data_types_ready)
{
   // Notify the app about a change in IMU data
   if (data_types_ready & IMU_ACCELEROMETER)
   {
      imu_data_ready = true;
      imu_read_accel_data(&imu_accel_data[0], &imu_accel_data[1], &imu_accel_data[2], &imu_accuracy);
   }
   app_notify(APP_NOTIFY_IMU_EVENT);
}

static void ble_discovery_handler(const uint8_t ble_address[EUI_LEN], uint8_t ranging_role)
{
   // Keep track of all newly discovered non-sleeping devices
   if (scheduler_get_current_role() == ROLE_MASTER)
   {
      if ((ranging_role == ROLE_MASTER) && (ble_address[0] > device_uid_short))
      {
         num_discovered_devices = 1;
         for (uint8_t i = 0; i < EUI_LEN; ++i)
            discovered_devices[0][i] = ble_address[i];
         discovered_devices[0][EUI_LEN] = ranging_role;
         BaseType_t xHigherPriorityTaskWoken = pdFALSE;
         xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_NETWORK_FOUND, eSetBits, &xHigherPriorityTaskWoken);
      }
   }
   else if (ranging_role != ROLE_ASLEEP)
   {
      if (!devices_found)
      {
         devices_found = true;
         num_discovered_devices = 1;
         for (uint8_t i = 0; i < EUI_LEN; ++i)
            discovered_devices[0][i] = ble_address[i];
         discovered_devices[0][EUI_LEN] = ranging_role;
         am_hal_timer_clear(BLE_SCANNING_TIMER_NUMBER);
      }
      else if (num_discovered_devices < MAX_NUM_RANGING_DEVICES)
      {
         for (uint8_t i = 0; i < num_discovered_devices; ++i)
            if (memcmp((uint8_t*)discovered_devices[i], ble_address, EUI_LEN) == 0)
               return;
         for (uint8_t i = 0; i < EUI_LEN; ++i)
            discovered_devices[num_discovered_devices][i] = ble_address[i];
         discovered_devices[num_discovered_devices++][EUI_LEN] = ranging_role;
      }
   }
}

void am_timer03_isr(void)
{
   // Close the discovery window that ble_discovery_handler() opened, if one exists
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(BLE_SCANNING_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   if (devices_found)
      xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_NETWORK_FOUND, eSetBits, &xHigherPriorityTaskWoken);
   else if (++windows_since_config_verify >= BLE_CONFIG_VERIFY_WINDOWS)
   {
      // Keep a slow self-check
      windows_since_config_verify = 0;
      xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_VERIFY_CONFIGURATION, eSetBits, &xHigherPriorityTaskWoken);
   }
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


// Public API Functions ------------------------------------------------------------------------------------------------

extern void app_maintenance_activate_find_my_tottag(uint32_t seconds_to_activate);
extern void app_maintenance_download_log_file(uint32_t start_time, uint32_t end_time);

void app_notify(app_notification_t notification)
{
   if (app_task_handle)
   {
      if (xPortIsInsideInterrupt())
      {
         BaseType_t xHigherPriorityTaskWoken = pdFALSE;
         xTaskNotifyFromISR(app_task_handle, notification, eSetBits, &xHigherPriorityTaskWoken);
         portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
      }
      else
         xTaskNotify(app_task_handle, notification, eSetBits);
   }
}

void app_activate_find_my_tottag(uint32_t seconds_to_activate)
{
   // Notify application of the request to active FindMyTottag
   if (app_task_handle)
   {
      seconds_to_activate_buzzer = seconds_to_activate;
      app_notify(APP_NOTIFY_FIND_MY_TOTTAG_ACTIVATED);
   }
   else
      app_maintenance_activate_find_my_tottag(seconds_to_activate);
}

void app_download_log_file(uint32_t start_time, uint32_t end_time)
{
   // Notify application of the request to download log file
   if (!app_task_handle)
      app_maintenance_download_log_file(start_time, end_time);
   else
   {
      download_start_timestamp = start_time;
      download_end_timestamp = end_time;
      xTaskNotify(app_task_handle, APP_NOTIFY_DOWNLOAD_SEGGER_LOG, eSetBits);
   }
}

void app_allow_downloads(bool allow)
{
   // Enable data downloading from ranging mode
   if (allow)
   {
      print("INFO: Allowing downloads...\n");
      // Disable writing to storage
      nandlog_disable(true);
      nandlog_enter_maintenance_mode();

      // Stop IMU
      imu_deinit();
   }
   else
   {
      // Start IMU
      imu_init();

      // Enable writing to storage
      nandlog_exit_maintenance_mode();
      nandlog_disable(false);
   }
}

void AppTaskRanging(void *uid)
{
   // Store the UID and application task handle
   static uint32_t notification_bits = 0;
   device_uid_short = ((uint8_t*)uid)[0];
   app_task_handle = xTaskGetCurrentTaskHandle();

   // Initialize the BLE scanning window timer
   am_hal_timer_config_t scanning_timer_config;
   am_hal_timer_default_config_set(&scanning_timer_config);
   scanning_timer_config.eFunction = AM_HAL_TIMER_FN_UPCOUNT;
   scanning_timer_config.ui32Compare0 = (uint32_t)((BLE_SCANNING_TIMER_TICK_RATE_HZ / 1000) * BLE_DISCOVERY_WINDOW_MS);
   am_hal_timer_config(BLE_SCANNING_TIMER_NUMBER, &scanning_timer_config);
   am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(BLE_SCANNING_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   NVIC_SetPriority(TIMER0_IRQn + BLE_SCANNING_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(TIMER0_IRQn + BLE_SCANNING_TIMER_NUMBER);
   am_hal_timer_enable(BLE_SCANNING_TIMER_NUMBER);

   // Register handlers for motion detection, battery status changes, and BLE events
   motion_changed = imu_data_ready = false;
   bluetooth_register_discovery_callback(ble_discovery_handler);
#ifndef _TEST_NO_BATTERY_CALLBACK
   if (battery_monitor_is_plugged_in())
      storage_flush_and_shutdown();
   else
      battery_register_event_callback(battery_event_handler);
#endif
   imu_register_service_request_callback(imu_service_request_handler);
   imu_register_motion_change_callback(motion_change_handler);
   imu_register_data_ready_callback(data_ready_handler);
   imu_enable_data_outputs(IMU_ACCELEROMETER | IMU_MOTION_DETECT, 500000);
#ifndef _TEST_NO_STORAGE
   storage_write_motion_status(imu_read_in_motion() ? IN_MOTION : NOT_IN_MOTION);
#endif

   // Retrieve current experiment details from non-volatile storage
   static experiment_details_t current_experiment;
   storage_retrieve_experiment_details(&current_experiment);

   // Wait until the BLE stack has been fully initialized
   devices_found = false;
   for (int i = 0; !bluetooth_is_initialized() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
      vTaskDelay(pdMS_TO_TICKS(100));
   if (!bluetooth_is_initialized())
   {
      bluetooth_reset();
      for (int i = 0; !bluetooth_is_initialized() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
         vTaskDelay(pdMS_TO_TICKS(100));
      if (!bluetooth_is_initialized())
         system_reset(true);
   }

   // Update the BLE address whitelist
   bluetooth_clear_whitelist();
   for (uint8_t i = 0; i < current_experiment.num_devices; ++i)
      bluetooth_add_device_to_whitelist(current_experiment.uids[i]);
   bluetooth_set_current_ranging_role(ROLE_IDLE);

   // Loop forever, sleeping until an application notification is received
   system_watchdog_register(WATCHDOG_TASK_APP);
   const TickType_t checkin_ticks = pdMS_TO_TICKS(WATCHDOG_CHECKIN_INTERVAL_MS);
   while (true)
   {
      // Pet the watchdog to indicate that the application task is still alive
      system_watchdog_pet(WATCHDOG_TASK_APP);

      // Collect anything the IMU has queued
      imu_service_pending();

      // Wait for a notification from another task or interrupt, then handle it
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, checkin_ticks) == pdTRUE)
         handle_notification(notification_bits);
   }
}
