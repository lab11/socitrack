// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "ranging.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t app_task_handle, ble_task_handle, ranging_task_handle;
static TaskHandle_t storage_task_handle, time_aligned_task_handle;


// Public API Functions ------------------------------------------------------------------------------------------------

void run_tasks(void)
{
   // Fetch the device UID
   static uint8_t uid[EUI_LEN];
   system_read_UID(uid, sizeof(uid));

   // Initialize all required peripherals and enable interrupts
   battery_monitor_init();
   bluetooth_init(uid);
   buzzer_init();
   imu_init();
   leds_init();
   rtc_init();
   storage_init();
   system_enable_interrupts(true);

   // Initialize the ranging radio and put it into deep sleep
   ranging_radio_init(uid);
   ranging_radio_sleep(true);

   // Determine whether there is an active experiment taking place
   uint32_t timestamp = rtc_get_timestamp();
   experiment_details_t scheduled_experiment;
   storage_retrieve_experiment_details(&scheduled_experiment);
   bool active_experiment = rtc_is_valid() && scheduled_experiment.num_devices &&
         (timestamp >= scheduled_experiment.experiment_start_time) && (timestamp < scheduled_experiment.experiment_end_time);
   storage_disable(!active_experiment);

   // Determine whether to power off for some time based on the device state
   uint32_t wake_on_timestamp = 0;
   bool power_off = false, allow_ranging = !battery_monitor_is_plugged_in();
   if (allow_ranging)
   {
      if (battery_monitor_get_level_mV() < BATTERY_NOMINAL)
         power_off = true;
      else if (!active_experiment)
      {
         power_off = true;
         if (timestamp < scheduled_experiment.experiment_start_time)
            wake_on_timestamp = scheduled_experiment.experiment_start_time;
      }
   }

   // Enter power-down mode upon low voltage or unscheduled timestamp
   system_enable_interrupts(false);
   if (power_off)
   {
      system_enter_power_off_mode(PIN_BATTERY_INPUT_POWER_GOOD, wake_on_timestamp);
      system_reset();
   }

   // Create tasks with the following priority order:
   //    IdleTask < TimeAlignedTask < AppTask < BLETask < StorageTask < RangingTask
   configASSERT1(xTaskCreate(RangingTask, "RangingTask", 512, allow_ranging ? uid : NULL, 5, &ranging_task_handle));
   configASSERT1(xTaskCreate(StorageTask, "StorageTask", 512, allow_ranging ? uid : NULL, 4, &storage_task_handle));
   configASSERT1(xTaskCreate(BLETask, "BLETask", 512, NULL, 3, &ble_task_handle));
   configASSERT1(xTaskCreate(allow_ranging ? AppTaskRanging : AppTaskMaintenance, "AppTask", 512, uid, 2, &app_task_handle));
   configASSERT1(xTaskCreate(TimeAlignedTask, "TimeAlignedTask", 128, NULL, 1, &time_aligned_task_handle));

   // Start the task scheduler
   vTaskStartScheduler();
}
