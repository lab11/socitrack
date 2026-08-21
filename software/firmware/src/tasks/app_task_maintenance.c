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
#include "storage_records.h"
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
      static uint8_t transmit_buffer[NANDLOG_MAX_PAGE_SIZE_BYTES];
      experiment_details_t details;

      // Transmit the stream header, then every page with its own framing
      nandlog_begin_reading(storage_experiment_ms_from_rtc(download_start_timestamp), storage_experiment_ms_from_rtc(download_end_timestamp));
      storage_retrieve_experiment_details(&details);

      // A pending request list turns this into a repair round: only the named pages are sent, and the
      // experiment details are omitted because the host already holds them from the original transfer
      const bool retransmitting = (nandlog_retransmit_count() > 0);
      uint32_t total_data_chunks, total_data_length;
      if (retransmitting)
      {
         total_data_chunks = nandlog_retransmit_count();
         total_data_length = nandlog_retransmit_total_bytes();
      }
      else
      {
         nandlog_read_span(&total_data_chunks, &total_data_length);
      }
      const nandlog_stream_header_t stream_header = {
         .magic = NANDLOG_STREAM_MAGIC,
         .format_version = NANDLOG_FORMAT_VERSION,
         .details_length = retransmitting ? 0 : (uint16_t)sizeof(details),
         .total_pages = total_data_chunks,
         .total_payload_bytes = total_data_length
      };
      transmit_log_data(&stream_header, sizeof(stream_header));
      if (!retransmitting)
         transmit_log_data(&details, sizeof(details));

      // Every page is framed with its sequence number, time bounds and payload CRC
      for (uint32_t chunk = 0; chunk < total_data_chunks; ++chunk)
      {
         nandlog_page_header_t page;
         const uint32_t data_length = retransmitting ? nandlog_retrieve_retransmit_page(chunk, transmit_buffer, &page)
                                                     : nandlog_retrieve_next_page(transmit_buffer, &page);
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
      nandlog_retransmit_clear();

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
   system_watchdog_register(WATCHDOG_TASK_APP);
   const TickType_t checkin_ticks = pdMS_TO_TICKS(WATCHDOG_CHECKIN_INTERVAL_MS);
   while (true)
   {
      system_watchdog_pet(WATCHDOG_TASK_APP);
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, checkin_ticks) == pdTRUE)
         handle_notification(notification_bits);
   }
}
