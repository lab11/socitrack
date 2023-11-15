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

static StaticTask_t app_task_tcb, ble_task_tcb, ranging_task_tcb;
static StaticTask_t storage_task_tcb, time_aligned_task_tcb;
static StackType_t app_task_stack[configMINIMAL_STACK_SIZE], ble_task_stack[2*configMINIMAL_STACK_SIZE];
static StackType_t ranging_task_stack[configMINIMAL_STACK_SIZE], storage_task_stack[configMINIMAL_STACK_SIZE];
static StackType_t time_aligned_task_stack[configMINIMAL_STACK_SIZE];
static experiment_details_t scheduled_experiment;


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
   storage_retrieve_experiment_details(&scheduled_experiment);
   uint32_t timestamp = rtc_get_timestamp(), time_of_day = rtc_get_time_of_day();
   bool valid_experiment = rtc_is_valid() && scheduled_experiment.num_devices;
   bool active_experiment = valid_experiment &&
         (timestamp >= scheduled_experiment.experiment_start_time) && (timestamp < scheduled_experiment.experiment_end_time) &&
         (!scheduled_experiment.use_daily_times ||
            ((scheduled_experiment.daily_start_time < scheduled_experiment.daily_end_time) &&
               (time_of_day >= scheduled_experiment.daily_start_time) && (time_of_day < scheduled_experiment.daily_end_time)) ||
            ((scheduled_experiment.daily_start_time > scheduled_experiment.daily_end_time) &&
               ((time_of_day >= scheduled_experiment.daily_start_time) || (time_of_day < scheduled_experiment.daily_end_time))));
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
         if (valid_experiment)
         {
            if (timestamp < scheduled_experiment.experiment_start_time)
               wake_on_timestamp = scheduled_experiment.experiment_start_time;
            else if (scheduled_experiment.use_daily_times && (timestamp < scheduled_experiment.experiment_end_time))
               wake_on_timestamp = timestamp + scheduled_experiment.daily_start_time + ((time_of_day < scheduled_experiment.daily_start_time) ? 0 : 86400) - time_of_day;
         }
      }
      buzzer_indicate_unplugged();
   }
   else
      buzzer_indicate_plugged_in();
   am_hal_delay_us(1000000);

   // Enter power-down mode upon low voltage or unscheduled timestamp
   system_enable_interrupts(false);
   if (power_off)
   {
      system_enter_power_off_mode(PIN_BATTERY_INPUT_POWER_GOOD, wake_on_timestamp);
      system_reset();
   }

   // Create tasks with the following priority order:
   //    IdleTask < TimeAlignedTask < AppTask < BLETask < RangingTask < StorageTask
   xTaskCreateStatic(StorageTask, "StorageTask", configMINIMAL_STACK_SIZE, allow_ranging ? uid : NULL, 5, storage_task_stack, &storage_task_tcb);
   xTaskCreateStatic(RangingTask, "RangingTask", configMINIMAL_STACK_SIZE, allow_ranging ? uid : NULL, 4, ranging_task_stack, &ranging_task_tcb);
   xTaskCreateStatic(BLETask, "BLETask", 2*configMINIMAL_STACK_SIZE, NULL, 3, ble_task_stack, &ble_task_tcb);
   xTaskCreateStatic(allow_ranging ? AppTaskRanging : AppTaskMaintenance, "AppTask", configMINIMAL_STACK_SIZE, uid, 2, app_task_stack, &app_task_tcb);
   xTaskCreateStatic(TimeAlignedTask, "TimeAlignedTask", configMINIMAL_STACK_SIZE, allow_ranging ? &scheduled_experiment : NULL, 1, time_aligned_task_stack, &time_aligned_task_tcb);

   // Start the task scheduler
   vTaskStartScheduler();
}
