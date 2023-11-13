#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "led.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "system.h"

static TaskHandle_t app_task_handle, ble_task_handle, ranging_task_handle;

void storage_retrieve_experiment_details(experiment_details_t *details) { memset(details, 0, sizeof(*details)); };
void storage_begin_reading(void) {}
void storage_end_reading(void) {}
uint32_t storage_retrieve_data_length(void) { return 0; }
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer) { return 0; }

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
   print("Initialized BLE with address %02X:%02X:%02X:%02X:%02X:%02X\n", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);

   // Initialize the ranging radio and put it into deep sleep
   ranging_radio_init(uid);
   ranging_radio_sleep(true);
   am_hal_delay_us(1000000);
   system_enable_interrupts(false);

   // Create tasks with the following priority order:
   //    IdleTask < AppTask < BLETask < RangingTask
   configASSERT1(xTaskCreate(RangingTask, "RangingTask", 512, uid, 5, &ranging_task_handle));
   configASSERT1(xTaskCreate(BLETask, "BLETask", 512, NULL, 3, &ble_task_handle));
   configASSERT1(xTaskCreate(AppTaskRanging, "AppTask", 512, uid, 2, &app_task_handle));

   // Start the task scheduler
   vTaskStartScheduler();
   return 0;
}
