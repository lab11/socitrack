#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "logging.h"
#include "rtc.h"
#include "system.h"

void storage_retrieve_experiment_details(experiment_details_t *details) { memset(details, 0, sizeof(*details)); };
void storage_begin_reading(void) {}
void storage_end_reading(void) {}
uint32_t storage_retrieve_data_length(void) { return 0; }
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer) { return 0; }

static void AppTestTask(void *uid)
{
   // Initiate a BLE reset every 10 seconds
   const TickType_t ticks_between_resets = pdMS_TO_TICKS(10000);
   TickType_t last_wake_time = xTaskGetTickCount();
   while (true)
   {
      // Sleep until time for the next BLE reset
      vTaskDelayUntil(&last_wake_time, ticks_between_resets);
      print("INFO: Resetting BLE...\n");

      // Reset the BLE module
      bluetooth_reset();
      for (int i = 0; !bluetooth_is_advertising() && (i < BLE_INIT_TIMEOUT_MS); i += 100)
         vTaskDelay(pdMS_TO_TICKS(100));
      if (!bluetooth_is_advertising())
      {
         print("ERROR: Reset failed...resetting device!\n");
         system_reset(true);
      }
      else
         print("INFO: Reset successful!\n");
   }
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   battery_monitor_init();
   rtc_init();

   // Fetch the device UID and initialize the Bluetooth hardware
   static uint8_t uid[EUI_LEN];
   system_read_UID(uid, sizeof(uid));
   bluetooth_init(uid);
   print("Initialized BLE with address %02X:%02X:%02X:%02X:%02X:%02X\n", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);

   // Create the BLE task and start the task scheduler
   static StaticTask_t app_task_tcb, ble_task_tcb;
   static StackType_t app_task_stack[configMINIMAL_STACK_SIZE], ble_task_stack[2*configMINIMAL_STACK_SIZE];
   xTaskCreateStatic(BLETask, "BLETask", 2*configMINIMAL_STACK_SIZE, NULL, 3, ble_task_stack, &ble_task_tcb);
   xTaskCreateStatic(AppTestTask, "AppTask", configMINIMAL_STACK_SIZE, uid, 2, app_task_stack, &app_task_tcb);
   bluetooth_start_advertising();
   vTaskStartScheduler();

   // Should never reach this point
   return 0;
}
