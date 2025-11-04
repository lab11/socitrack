// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "scheduler.h"
#include "storage.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t app_task_handle;
static uint8_t device_uid_short, imu_accuracy;
static uint8_t ble_scan_results[MAX_NUM_RANGING_DEVICES];
static volatile uint8_t discovered_devices[MAX_NUM_RANGING_DEVICES][1+EUI_LEN];
static volatile uint32_t seconds_to_activate_buzzer;
static volatile uint8_t num_discovered_devices;
static volatile bool devices_found, motion_changed, imu_data_ready;
static uint32_t download_start_timestamp, download_end_timestamp;
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
#ifndef _TEST_IMU_DATA
#if REVISION_ID < REVISION_N
      imu_clear_interrupts();
#endif
#endif
   }
   if (((notification & APP_NOTIFY_NETWORK_LOST)) || ((notification & APP_NOTIFY_NETWORK_CONNECTED)) ||
       ((notification & APP_NOTIFY_VERIFY_CONFIGURATION)))
      verify_app_configuration();
   if ((notification & APP_NOTIFY_NETWORK_FOUND))
   {
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
         for (uint8_t i = 0; i < num_discovered_devices; ++i)
            if ((discovered_devices[i][EUI_LEN] == ROLE_MASTER) && (discovered_devices[i][0] > device_uid_short))
            {
               scheduler_stop();
               while (ranging_active())
                  vTaskDelay(pdMS_TO_TICKS(10));
               ranging_begin(ROLE_PARTICIPANT);
               break;
            }
      }
      else
      {
         // Determine if an actively ranging device was located
         bool ranging_device_located = false, idle_device_located = false;
         for (uint8_t i = 0; !ranging_device_located && (i < num_discovered_devices); ++i)
            if ((discovered_devices[i][EUI_LEN] == ROLE_MASTER) || (discovered_devices[i][EUI_LEN] == ROLE_PARTICIPANT))
               ranging_device_located = true;
            else if (discovered_devices[i][EUI_LEN] == ROLE_IDLE)
               idle_device_located = true;

         // Start the ranging task based on the state of the detected devices
         if (ranging_device_located)
            ranging_begin(ROLE_PARTICIPANT);
         else if (idle_device_located)
         {
            // Search for the non-sleeping device with the highest ID that is higher than our own
            int32_t best_device_idx = -1;
            uint8_t highest_device_id = device_uid_short;
            for (uint8_t i = 0; i < num_discovered_devices; ++i)
               if (discovered_devices[i][0] > highest_device_id)
               {
                  best_device_idx = i;
                  highest_device_id = discovered_devices[i][0];
               }

            // If a potential master candidate device was found, attempt to connect to it
            if (best_device_idx >= 0)
               ranging_begin(ROLE_PARTICIPANT);
            else
               ranging_begin(ROLE_MASTER);
         }
      }

      // Reset the devices-found flag, store the scan results, and verify the app configuration
      devices_found = false;
      for (uint8_t i = 0; i < num_discovered_devices; ++i)
         ble_scan_results[i] = discovered_devices[i][0];
      storage_write_ble_scan_results(ble_scan_results, num_discovered_devices);
      verify_app_configuration();
   }
   if ((notification & APP_NOTIFY_BATTERY_EVENT))
      storage_flush_and_shutdown();
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
      static uint8_t transmit_buffer[MEMORY_PAGE_SIZE_BYTES];
      experiment_details_t details;

      // Transmit estimated total data length
      storage_begin_reading(download_start_timestamp, download_end_timestamp);
      storage_retrieve_experiment_details(&details);
      uint32_t total_data_chunks = storage_retrieve_num_data_chunks(download_end_timestamp);
   #ifdef _TEST_IMU_DATA
      uint32_t total_data_length = total_data_chunks * MEMORY_NUM_DATA_BYTES_PER_PAGE;
   #else
      uint32_t total_data_length = storage_retrieve_num_data_bytes();
   #endif
      transmit_log_data(&total_data_length, sizeof(total_data_length));
      transmit_log_data(&details, sizeof(details));

      // Transmit log file data in chunks
      for (uint32_t chunk = 0; chunk < total_data_chunks; ++chunk)
      {
         const uint32_t data_length = storage_retrieve_next_data_chunk(transmit_buffer);
         if (data_length)
            transmit_log_data(transmit_buffer, data_length);
      }
      storage_end_reading();
   }
#endif  // #ifdef __USE_SEGGER__
}

static void battery_event_handler(battery_event_t battery_event)
{
   // Notify the app of a change in the plugged-in status of the device
   if ((battery_event == BATTERY_PLUGGED) || (battery_event == BATTERY_UNPLUGGED))
      app_notify(APP_NOTIFY_BATTERY_EVENT, true);
}

static void motion_change_handler(bool in_motion)
{
   // Notify the app about a change in motion
   motion_changed = true;
   app_notify(APP_NOTIFY_IMU_EVENT, true);
}

static void data_ready_handler(imu_data_type_t data_types_ready)
{
   // Notify the app about a change in IMU data
   if (data_types_ready & IMU_ACCELEROMETER)
   {
      imu_data_ready = true;
      imu_read_accel_data(&imu_accel_data[0], &imu_accel_data[1], &imu_accel_data[2], &imu_accuracy);
   }
   app_notify(APP_NOTIFY_IMU_EVENT, true);
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
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(BLE_SCANNING_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_NETWORK_FOUND, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


// Public API Functions ------------------------------------------------------------------------------------------------

extern void app_maintenance_activate_find_my_tottag(uint32_t seconds_to_activate);
extern void app_maintenance_download_log_file(uint32_t start_time, uint32_t end_time);

void app_notify(app_notification_t notification, bool from_isr)
{
   // Call the correct notification function based on the current ISR context
   if (app_task_handle)
   {
      if (from_isr)
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
      app_notify(APP_NOTIFY_FIND_MY_TOTTAG_ACTIVATED, false);
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
      storage_disable(true);
      storage_enter_maintenance_mode();

      // Stop IMU
      imu_deinit();
   }
   else
   {
      // Start IMU
      imu_init();

      // Enable writing to storage
      storage_exit_maintenance_mode();
      storage_disable(false);
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
   scanning_timer_config.ui32Compare0 = (uint32_t)(BLE_SCANNING_TIMER_TICK_RATE_HZ / 2);
   am_hal_timer_config(BLE_SCANNING_TIMER_NUMBER, &scanning_timer_config);
   am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(BLE_SCANNING_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   NVIC_SetPriority(TIMER0_IRQn + BLE_SCANNING_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(TIMER0_IRQn + BLE_SCANNING_TIMER_NUMBER);

   // Register handlers for motion detection, battery status changes, and BLE events
   motion_changed = imu_data_ready = false;
   bluetooth_register_discovery_callback(ble_discovery_handler);
#ifndef _TEST_NO_BATTERY_CALLBACK
   if (battery_monitor_is_plugged_in())
      storage_flush_and_shutdown();
   else
      battery_register_event_callback(battery_event_handler);
#endif
   imu_register_motion_change_callback(motion_change_handler);
   imu_register_data_ready_callback(data_ready_handler);
#ifdef _TEST_IMU_DATA
   imu_enable_data_outputs(IMU_LINEAR_ACCELEROMETER | IMU_GYROSCOPE | IMU_MOTION_DETECT, 100000);
#else
   imu_enable_data_outputs(IMU_ACCELEROMETER | IMU_MOTION_DETECT, 500000);
#endif
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
   while (true)
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, portMAX_DELAY) == pdTRUE)
         handle_notification(notification_bits);
}
