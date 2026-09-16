// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "system.h"


// Private Helper Functions --------------------------------------------------------------------------------------------

static void report_radio_instrumentation(void)
{
   // Report where the radio wake-up budget actually goes
   uint32_t wake_pin_us = 0, wake_ready_us = 0, wake_restore_us = 0;
   ranging_radio_get_wake_timing(&wake_pin_us, &wake_ready_us, &wake_restore_us);
   if (wake_pin_us || wake_ready_us || wake_restore_us)
      print("INFO: Radio wake-up = %u us pin + %u us to SPI_RDY + %u us restore = %u us of a %u us budget\n", wake_pin_us, wake_ready_us, wake_restore_us, wake_pin_us + wake_ready_us + wake_restore_us, (uint32_t)RADIO_WAKEUP_SAFETY_DELAY_US);

   // Slot-level health
   ranging_radio_stats_t radio_stats;
   ranging_radio_get_stats(&radio_stats);
   const uint32_t rx_total = radio_stats.rx_ok + radio_stats.rx_failed;
   if (rx_total)
      print("INFO: Ranging slots = %u/%u decoded (%u.%02u%%) across %u devices | TX too late %u, RX arm too late %u | full restores %u, wakes skipped %u\n", radio_stats.rx_ok, rx_total, (uint32_t)((100ULL * radio_stats.rx_ok) / rx_total), (uint32_t)(((10000ULL * radio_stats.rx_ok) / rx_total) % 100u), (uint32_t)radio_stats.network_size, radio_stats.tx_failed, radio_stats.rx_arm_failed, radio_stats.full_restores, radio_stats.wake_skipped);
   if (radio_stats.isr_count)
      print("INFO: Radio ISR = %u serviced, max %u us%s over %u event(s), %u exceeded %u us (%u.%02u%%)\n", radio_stats.isr_count, radio_stats.isr_max_us, radio_stats.cycle_counter_ok ? "" : " (UNAVAILABLE)", radio_stats.isr_max_events, radio_stats.isr_over_count, (uint32_t)RADIO_ISR_BUDGET_US, (uint32_t)((100ULL * radio_stats.isr_over_count) / radio_stats.isr_count), (uint32_t)(((10000ULL * radio_stats.isr_over_count) / radio_stats.isr_count) % 100u));
}


// Public API Functions ------------------------------------------------------------------------------------------------

void TimeAlignedTask(void *scheduled_experiment)
{
   // Set up local variables
   experiment_details_t *experiment_details = scheduled_experiment ? (experiment_details_t*)scheduled_experiment : NULL;
   uint32_t battery_voltage = 0, time_of_day = 0;
   bool experiment_ended = false;

   // Store whether the device is plugged in or not
   storage_write_charging_status(battery_monitor_is_plugged_in() ? BATTERY_PLUGGED : BATTERY_UNPLUGGED);

   // Ask to be watched, then run the periodic work immediately on the first pass
   system_watchdog_register(WATCHDOG_TASK_TIME_ALIGNED);
   const TickType_t checkin_ticks = pdMS_TO_TICKS(WATCHDOG_CHECKIN_INTERVAL_MS);
   const TickType_t work_period_ticks = pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_S * 1000);
   TickType_t next_work_at = xTaskGetTickCount(), next_report_at = xTaskGetTickCount();

   // Loop forever
   while (true)
   {
      // Check in with the watchdog and wait until the next work period
      system_watchdog_pet(WATCHDOG_TASK_TIME_ALIGNED);
      if ((int32_t)(xTaskGetTickCount() - next_report_at) >= 0)
      {
         next_report_at = xTaskGetTickCount() + pdMS_TO_TICKS(RADIO_STATS_REPORT_INTERVAL_S * 1000);
         report_radio_instrumentation();
      }
      if ((int32_t)(xTaskGetTickCount() - next_work_at) < 0)
      {
         vTaskDelay(checkin_ticks);
         continue;
      }
      next_work_at += work_period_ticks;
      if ((int32_t)(xTaskGetTickCount() - next_work_at) >= 0)
         next_work_at = xTaskGetTickCount() + work_period_ticks;

      // Anchor the real time once per loop so the log carries an RTC reference every BATTERY_CHECK_INTERVAL_S
      storage_write_time_anchor();

      // Send a configuration verification notification to the Application Task
      app_notify(APP_NOTIFY_VERIFY_CONFIGURATION);

      // Reconcile the charger state against the pins
      battery_monitor_poll_charger_state();

      // Record how close the firmware came to a fault without reaching one
      storage_write_diagnostics();

      // Read and store the current battery voltage
      battery_voltage = battery_monitor_get_level_mV();
      print("INFO: Battery voltage = %u mV\n", battery_voltage);
      if (battery_voltage)
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
      if ((battery_voltage && !battery_monitor_is_plugged_in() && (battery_voltage <= BATTERY_CRITICAL)) || experiment_ended)
         storage_flush_and_shutdown();
   }
}
