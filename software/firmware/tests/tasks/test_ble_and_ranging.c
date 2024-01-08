#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "led.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "system.h"

static StaticTask_t app_task_tcb, ble_task_tcb, ranging_task_tcb, time_aligned_task_tcb;
static StackType_t app_task_stack[configMINIMAL_STACK_SIZE], ble_task_stack[2*configMINIMAL_STACK_SIZE];
static StackType_t ranging_task_stack[configMINIMAL_STACK_SIZE], time_aligned_task_stack[configMINIMAL_STACK_SIZE];

int main(void)
{
   // Set up system hardware and fetch the device UID
   setup_hardware();
   static uint8_t uid[EUI_LEN];
   system_read_UID(uid, sizeof(uid));

   // Initialize all required peripherals and enable interrupts
   battery_monitor_init();
   bluetooth_init(uid);
   leds_init();
   rtc_init();
   system_enable_interrupts(true);
   print("Initialized BLE with address %02X:%02X:%02X:%02X:%02X:%02X\n", uid[5], uid[4], uid[3], uid[2], uid[1], uid[0]);

   // Initialize the ranging radio and put it into deep sleep
   ranging_radio_init(uid);
   ranging_radio_sleep(true);
   am_hal_delay_us(1000000);
   system_enable_interrupts(false);

   // Create tasks with the following priority order:
   //    IdleTask < AppTask < BLETask < RangingTask
   xTaskCreateStatic(RangingTask, "RangingTask", configMINIMAL_STACK_SIZE, uid, 4, ranging_task_stack, &ranging_task_tcb);
   xTaskCreateStatic(BLETask, "BLETask", 2*configMINIMAL_STACK_SIZE, NULL, 3, ble_task_stack, &ble_task_tcb);
   xTaskCreateStatic(AppTaskRanging, "AppTask", configMINIMAL_STACK_SIZE, uid, 2, app_task_stack, &app_task_tcb);
   xTaskCreateStatic(TimeAlignedTask, "TimeAlignedTask", configMINIMAL_STACK_SIZE, NULL, 1, time_aligned_task_stack, &time_aligned_task_tcb);

   // Start the task scheduler
   vTaskStartScheduler();
   return 0;
}
