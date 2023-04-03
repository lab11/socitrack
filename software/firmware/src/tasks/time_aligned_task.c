// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"


// Public API Functions ------------------------------------------------------------------------------------------------

void TimeAlignedTask(void *params)
{
   // Set up local variables
   const TickType_t ticks_between_iterations = pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_S * 1000);
   TickType_t last_wake_time = xTaskGetTickCount();

   // Loop forever
   while (true)
   {
      // Send a configuration verification notification to the Application Task
      app_notify(APP_NOTIFY_VERIFY_CONFIGURATION, false);

      // Read and store the current battery voltage
      uint32_t battery_voltage = battery_monitor_get_level_mV();
      storage_write_battery_level(battery_voltage);

      // Ask the storage task to shutdown if the battery is critically low
      if (battery_voltage <= BATTERY_CRITICAL)
         storage_flush_and_shutdown();

      // Sleep until next time-aligned task iteration
      vTaskDelayUntil(&last_wake_time, ticks_between_iterations);
   }
}
