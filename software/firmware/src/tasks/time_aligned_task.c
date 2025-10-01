// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "logging.h"
#include "rtc.h"


// Public API Functions ------------------------------------------------------------------------------------------------

void TimeAlignedTask(void *scheduled_experiment)
{
   // Set up local variables
   experiment_details_t *experiment_details = scheduled_experiment ? (experiment_details_t*)scheduled_experiment : NULL;
   static uint32_t battery_voltage, time_of_day;
   static bool experiment_ended;

   // Store whether the device is plugged in or not
   storage_write_motion_status(scheduled_experiment ? NOT_ON_CHARGER : ON_CHARGER);

   // Loop forever
   while (true)
   {
      // Send a configuration verification notification to the Application Task
      app_notify(APP_NOTIFY_VERIFY_CONFIGURATION, false);

      // Read and store the current battery voltage
      battery_voltage = battery_monitor_get_level_mV();
      print("INFO: Battery voltage = %u mV\n", battery_voltage);
      storage_write_battery_level(battery_voltage);

      // Determine if an active experiment has ended
      experiment_ended = false;
      if (experiment_details)
      {
         if (rtc_get_timestamp() > experiment_details->experiment_end_time)
            experiment_ended = true;
         else if (experiment_details->use_daily_times)
         {
            time_of_day = rtc_get_time_of_day();
            if (((experiment_details->daily_start_time < experiment_details->daily_end_time) &&
                  ((time_of_day < experiment_details->daily_start_time) || (time_of_day > experiment_details->daily_end_time))) ||
               ((experiment_details->daily_start_time > experiment_details->daily_end_time) &&
                  ((time_of_day < experiment_details->daily_start_time) && (time_of_day > experiment_details->daily_end_time))))
               experiment_ended = true;
         }
      }

      // Ask the storage task to shutdown if the battery is critically low or an experiment has ended
      if ((!battery_monitor_is_plugged_in() && (battery_voltage <= BATTERY_CRITICAL)) || experiment_ended)
         storage_flush_and_shutdown();

      // Sleep until the next time-aligned task iteration
      vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_S * 1000));
   }
}
