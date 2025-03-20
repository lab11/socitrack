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
#include "usb.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static StaticTask_t app_task_tcb, ble_task_tcb, ranging_task_tcb;
static StaticTask_t storage_task_tcb, time_aligned_task_tcb;
static StackType_t app_task_stack[configMINIMAL_STACK_SIZE], ble_task_stack[2*configMINIMAL_STACK_SIZE];
static StackType_t ranging_task_stack[configMINIMAL_STACK_SIZE], storage_task_stack[configMINIMAL_STACK_SIZE];
static StackType_t time_aligned_task_stack[configMINIMAL_STACK_SIZE];
static uint32_t experiment_start_time;


// Public API Functions ------------------------------------------------------------------------------------------------

uint32_t app_get_experiment_time(int32_t offset) { return (uint32_t)(rtc_get_timestamp_diff_ms(experiment_start_time) + offset); }
uint32_t app_experiment_time_to_rtc_time(uint32_t experiment_time) { return (experiment_time / 1000) + experiment_start_time; }

void run_tasks(void)
{
   // Fetch the device UID
   static uint8_t uid[EUI_LEN];
   system_read_UID(uid, sizeof(uid));

   // Determine whether to enter USB maintenance mode
   usb_init();
   if (usb_cable_connected())
   {
      // Initialize all required peripherals and enable interrupts
      battery_monitor_init();
      buzzer_init();
      rtc_init();
      storage_init();

      // Create the USB processing tasks
      uid[0] = uid[1] = uid[2] = uid[3] = 0xEF;
      xTaskCreateStatic(UsbTask, "UsbTask", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-1, app_task_stack, &app_task_tcb);
      xTaskCreateStatic(UsbCdcTask, "UsbCdcTask", 2 * configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-2, ble_task_stack, &ble_task_tcb);
      xTaskCreateStatic(AppTaskMaintenance, "AppTask", configMINIMAL_STACK_SIZE, uid, configMAX_PRIORITIES-2, storage_task_stack, &storage_task_tcb);
   }
   else
   {
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

#ifdef _USE_DEFAULT_EXP_DETAILS
      // only set immediately after flashing, not on reboot
      if (!rtc_is_valid())
      {
         rtc_set_time_to_compile_time();
         //default exp details
         uint32_t current_timestamp = rtc_get_timestamp();
         experiment_details_t details = {
            .experiment_start_time = current_timestamp,
            .experiment_end_time = current_timestamp + 604800,
            .daily_start_time = 1,
            .daily_end_time = 23,
            .num_devices = 2,
            .uids = {},
            .uid_name_mappings = {}
         };
         //new exp details can only be set in maintenance mode
         storage_enter_maintenance_mode();
         storage_store_experiment_details(&details);
         if (!battery_monitor_is_plugged_in())
            storage_exit_maintenance_mode();
      }
#endif

      // Determine whether there is an active experiment taking place
      static experiment_details_t scheduled_experiment;
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
      experiment_start_time = scheduled_experiment.experiment_start_time;
      storage_disable(!active_experiment);

      // Determine whether to power off for some time based on the device state
      uint32_t wake_on_timestamp = 0;
      bool power_off = false, allow_ranging = !battery_monitor_is_plugged_in();
      if (allow_ranging)
      {
         uint32_t battery_level = battery_monitor_get_level_mV();
         if (battery_level < BATTERY_NOMINAL)
         {
            print("WARNING: Battery level (%u mV) is too low to begin ranging!\n", battery_level);
            power_off = true;
         }
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
         system_reset(true);
      }

      // Create tasks with the following priority order:
      //    IdleTask < TimeAlignedTask < AppTask < BLETask < RangingTask < StorageTask
      xTaskCreateStatic(StorageTask, "StorageTask", configMINIMAL_STACK_SIZE, allow_ranging ? uid : NULL, 5, storage_task_stack, &storage_task_tcb);
#if !defined(_TEST_NO_EXP_DETAILS)
      xTaskCreateStatic(RangingTask, "RangingTask", configMINIMAL_STACK_SIZE, allow_ranging ? &scheduled_experiment : NULL, 4, ranging_task_stack, &ranging_task_tcb);
#else 
      xTaskCreateStatic(RangingTask, "RangingTask", configMINIMAL_STACK_SIZE, allow_ranging ? uid : NULL, 4, ranging_task_stack, &ranging_task_tcb);
#endif
      xTaskCreateStatic(BLETask, "BLETask", 2 * configMINIMAL_STACK_SIZE, NULL, 3, ble_task_stack, &ble_task_tcb);
      xTaskCreateStatic(allow_ranging ? AppTaskRanging : AppTaskMaintenance, "AppTask", configMINIMAL_STACK_SIZE, uid, 2, app_task_stack, &app_task_tcb);
      xTaskCreateStatic(TimeAlignedTask, "TimeAlignedTask", configMINIMAL_STACK_SIZE, allow_ranging ? &scheduled_experiment : NULL, 1, time_aligned_task_stack, &time_aligned_task_tcb);
   }

   // Start the task scheduler
   vTaskStartScheduler();
}
