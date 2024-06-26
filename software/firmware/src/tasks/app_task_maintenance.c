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
#include "storage.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t app_task_handle;
static volatile uint32_t seconds_to_activate_buzzer;
static uint32_t download_start_timestamp, download_end_timestamp;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void handle_notification(app_notification_t notification)
{
   // Handle the notification based on which bits are set
   if ((notification & APP_NOTIFY_FIND_MY_TOTTAG_ACTIVATED) != 0)
      for (uint32_t seconds = 0; seconds < seconds_to_activate_buzzer; ++seconds)
      {
         buzzer_indicate_location();
         vTaskDelay(pdMS_TO_TICKS(1000));
      }
   if ((notification & APP_NOTIFY_BATTERY_EVENT) != 0)
      storage_flush_and_shutdown();
#ifdef __USE_SEGGER__
   if ((notification & APP_NOTIFY_DOWNLOAD_SEGGER_LOG))
   {
      // Define log file transmission variables
      static uint8_t transmit_buffer[MEMORY_PAGE_SIZE_BYTES];
      experiment_details_t details;

      // Transmit estimated total data length
      storage_begin_reading(download_start_timestamp);
      storage_retrieve_experiment_details(&details);
      uint32_t total_data_chunks = storage_retrieve_num_data_chunks(download_end_timestamp);
      uint32_t total_data_length = storage_retrieve_num_data_bytes();
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
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_BATTERY_EVENT, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}


// Public API Functions ------------------------------------------------------------------------------------------------

void app_maintenance_activate_find_my_tottag(uint32_t seconds_to_activate)
{
   // Notify application of the request to active FindMyTottag
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   seconds_to_activate_buzzer = seconds_to_activate;
   xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_FIND_MY_TOTTAG_ACTIVATED, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void app_maintenance_download_log_file(uint32_t start_time, uint32_t end_time)
{
   download_start_timestamp = start_time;
   download_end_timestamp = end_time;
   xTaskNotify(app_task_handle, APP_NOTIFY_DOWNLOAD_SEGGER_LOG, eSetBits);
}

void AppTaskMaintenance(void *uid)
{
   // Store the application task handle
   static uint32_t notification_bits = 0;
   app_task_handle = xTaskGetCurrentTaskHandle();

   // Register handler for battery status changes and verify correct mode of operation
   battery_register_event_callback(battery_event_handler);
   if (!battery_monitor_is_plugged_in())
      storage_flush_and_shutdown();

   // Wait until the BLE stack has been fully initialized
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

   // Clear the BLE address whitelist
   bluetooth_clear_whitelist();

   // Update the BLE role to ASLEEP and start advertising
   bluetooth_set_current_ranging_role(ROLE_ASLEEP);
   bluetooth_start_advertising();

   // Loop forever, sleeping until an application notification is received
   while (true)
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, portMAX_DELAY) == pdTRUE)
         handle_notification(notification_bits);
}
