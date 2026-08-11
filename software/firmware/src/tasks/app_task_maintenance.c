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
#include "tusb.h"
#include "usb.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t app_task_handle;
static volatile uint32_t seconds_to_activate_buzzer;
static volatile battery_event_t pending_battery_event;
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
   {
      // Record the battery event, including a critical-voltage trip
      const battery_event_t battery_event = pending_battery_event;
      if ((battery_event >= BATTERY_PLUGGED) && (battery_event <= BATTERY_CRITICAL_VOLTAGE))
         storage_write_charging_status(battery_event);
      if ((battery_event == BATTERY_PLUGGED) || (battery_event == BATTERY_UNPLUGGED) || (battery_event == BATTERY_CRITICAL_VOLTAGE))
         storage_flush_and_shutdown();
   }
   if ((notification & APP_NOTIFY_DOWNLOAD_SEGGER_LOG))
   {
      // Define log file transmission variables
      static uint8_t transmit_buffer[MEMORY_PAGE_SIZE_BYTES];
      experiment_details_t details;

      // Transmit the stream header, then every page with its own framing
      storage_begin_reading(download_start_timestamp, download_end_timestamp);
      storage_retrieve_experiment_details(&details);
      uint32_t total_data_chunks = storage_retrieve_num_data_chunks(download_end_timestamp);
   #ifdef _TEST_IMU_DATA
      uint32_t total_data_length = total_data_chunks * MEMORY_NUM_DATA_BYTES_PER_PAGE;
   #else
      uint32_t total_data_length = storage_retrieve_num_data_bytes();
   #endif
      const storage_stream_header_t stream_header = {
         .magic = STORAGE_STREAM_MAGIC,
         .format_version = STORAGE_FORMAT_VERSION,
         .details_length = (uint16_t)sizeof(details),
         .total_pages = total_data_chunks,
         .total_payload_bytes = total_data_length
      };
      transmit_log_data(&stream_header, sizeof(stream_header));
      transmit_log_data(&details, sizeof(details));

      // Every page is framed with its sequence number, time bounds and payload CRC
      for (uint32_t chunk = 0; chunk < total_data_chunks; ++chunk)
      {
         storage_page_header_t page;
         const uint32_t data_length = storage_retrieve_next_page(transmit_buffer, &page);
         const storage_wire_page_t wire = {
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
      storage_end_reading();

      // Push the final partial buffer; without this the tail of the transfer never leaves the device
      transmit_log_flush();
   }
}

static void battery_event_handler(battery_event_t battery_event)
{
   // Hand every battery event to the app task
   pending_battery_event = battery_event;
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xTaskNotifyFromISR(app_task_handle, APP_NOTIFY_BATTERY_EVENT, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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

   // Determine if we are in USB maintenance mode
   bool usb_maintenance = (((uint8_t*)uid)[0] == 0xEF) && (((uint8_t*)uid)[1] == 0xEF) && (((uint8_t*)uid)[2] == 0xEF) && (((uint8_t*)uid)[3] == 0xEF);
   if (!usb_maintenance)
   {
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
   }

   // Loop forever, sleeping until an application notification is received
   while (true)
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, portMAX_DELAY) == pdTRUE)
         handle_notification(notification_bits);
}
