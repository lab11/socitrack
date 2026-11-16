#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "logging.h"
#include "rtc.h"
#include "system.h"

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

   // Create the BLE task and start the task scheduler
   static StaticTask_t ble_task_tcb;
   static StackType_t ble_task_stack[2*configMINIMAL_STACK_SIZE];
   xTaskCreateStatic(BLETask, "BLETask", 2 * configMINIMAL_STACK_SIZE, NULL, 3, ble_task_stack, &ble_task_tcb);
   bluetooth_start_advertising();
   vTaskStartScheduler();

   // Should never reach this point
   return 0;
}
