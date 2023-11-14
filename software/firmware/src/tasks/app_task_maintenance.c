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

void AppTaskMaintenance(void *uid)
{
   // Store the application task handle
   app_task_handle = xTaskGetCurrentTaskHandle();
   uint32_t notification_bits = 0;

   // Register handler for battery status changes and verify correct mode of operation
   battery_register_event_callback(battery_event_handler);
   if (!battery_monitor_is_plugged_in())
      storage_flush_and_shutdown();

   // Wait until the BLE stack has been fully initialized
   for (int i = 0; !bluetooth_is_initialized() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
      vTaskDelay(pdMS_TO_TICKS(100));
   if (!bluetooth_is_initialized())
      system_reset();

   // Clear the BLE address whitelist
   bluetooth_clear_whitelist();

   // Update the BLE role to ASLEEP and start advertising
   bluetooth_set_current_ranging_role(ROLE_ASLEEP);
   bluetooth_start_advertising(false);

   // Loop forever, sleeping until an application notification is received
   while (true)
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, portMAX_DELAY) == pdTRUE)
         handle_notification(notification_bits);
}
