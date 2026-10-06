// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "nandlog.h"
#include "radio_test.h"
#include "ranging.h"
#include "rtc.h"
#include "storage_records.h"
#include "system.h"
#include "usb.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

#define APP_TASK_STACK_WORDS            (4 * configMINIMAL_STACK_SIZE)
#define BLE_TASK_STACK_WORDS            (4 * configMINIMAL_STACK_SIZE)
#define RANGING_TASK_STACK_WORDS        (2 * configMINIMAL_STACK_SIZE)
#define STORAGE_TASK_STACK_WORDS        (2 * configMINIMAL_STACK_SIZE)
#define TIME_ALIGNED_TASK_STACK_WORDS   (2 * configMINIMAL_STACK_SIZE)

static StaticTask_t app_task_tcb, ble_task_tcb, ranging_task_tcb;
static StaticTask_t storage_task_tcb, time_aligned_task_tcb;
static StackType_t app_task_stack[APP_TASK_STACK_WORDS], ble_task_stack[BLE_TASK_STACK_WORDS];
static StackType_t ranging_task_stack[RANGING_TASK_STACK_WORDS], storage_task_stack[STORAGE_TASK_STACK_WORDS];
static StackType_t time_aligned_task_stack[TIME_ALIGNED_TASK_STACK_WORDS];
static uint32_t experiment_start_time;
static int32_t network_time_offset;


// Private Helper Functions --------------------------------------------------------------------------------------------

static bool experiment_is_active(const experiment_details_t *details)
{
   // Inside the deployment window and inside today's hours if it has them
   const uint32_t timestamp = rtc_get_timestamp(), time_of_day = rtc_get_time_of_day();
   const bool valid_experiment = rtc_is_valid() && details->num_devices && !details->is_terminated;
   return valid_experiment &&
         (timestamp >= details->experiment_start_time) && (timestamp < details->experiment_end_time) &&
         (!details->use_daily_times ||
            ((details->daily_start_time < details->daily_end_time) && (time_of_day >= details->daily_start_time) && (time_of_day < details->daily_end_time)) ||
            ((details->daily_start_time > details->daily_end_time) && ((time_of_day >= details->daily_start_time) || (time_of_day < details->daily_end_time))));
}

static void record_usb_boot(void)
{
#if !defined(_TEST_NO_STORAGE)
   static experiment_details_t details;
   storage_retrieve_experiment_details(&details);
   if (!experiment_is_active(&details))
      return;
   experiment_start_time = details.experiment_start_time;
   const uint32_t timestamp = app_get_experiment_time(0);
   const uint16_t reset_reason = system_get_reset_reason();
   const uint8_t plugged = BATTERY_PLUGGED;
   nandlog_store_record(STORAGE_TYPE_RESET_REASON, timestamp, &reset_reason, sizeof(reset_reason));
   nandlog_store_record(STORAGE_TYPE_CHARGING_EVENT, timestamp, &plugged, sizeof(plugged));
   nandlog_flush(true);
#endif
}


// Public API Functions ------------------------------------------------------------------------------------------------

uint32_t app_get_experiment_time(int32_t offset)
{
   // Clamp instead of wrapping
   const int64_t elapsed_ms = (int64_t)rtc_get_timestamp_diff_ms(experiment_start_time) + offset;
   return (elapsed_ms > 0) ? (uint32_t)elapsed_ms : 0;
}
uint32_t app_experiment_time_to_rtc_time(uint32_t experiment_time) { return (experiment_time / 1000) + experiment_start_time; }
uint32_t app_get_experiment_start_time(void) { return experiment_start_time; }

int32_t app_get_time_offset(void) { return network_time_offset; }
void app_set_time_offset(int32_t offset) { network_time_offset = offset; }

void app_set_experiment_start_time(uint32_t start_time)
{
   // Every record timestamp is relative to this value
   experiment_start_time = start_time;
}

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
      nandlog_init();
      record_usb_boot();

      // Create the USB processing tasks
      uid[0] = uid[1] = uid[2] = uid[3] = 0xEF;
      xTaskCreateStatic(UsbTask, "UsbTask", APP_TASK_STACK_WORDS, NULL, configMAX_PRIORITIES-1, app_task_stack, &app_task_tcb);
      xTaskCreateStatic(UsbCdcTask, "UsbCdcTask", BLE_TASK_STACK_WORDS, NULL, configMAX_PRIORITIES-2, ble_task_stack, &ble_task_tcb);
      xTaskCreateStatic(AppTaskMaintenance, "AppTask", STORAGE_TASK_STACK_WORDS, uid, configMAX_PRIORITIES-2, storage_task_stack, &storage_task_tcb);
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
      nandlog_init();
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
            .daily_start_time = 0,
            .daily_end_time = 86400,
            .use_daily_times = 0,
            .num_devices = 2,
            .uids = {},
            .uid_name_mappings = {},
            .is_terminated = 0
         };
         //new exp details can only be set in maintenance mode
         nandlog_begin_session();
         if (storage_store_experiment_details(&details))
            app_set_experiment_start_time(details.experiment_start_time);
         if (!battery_monitor_is_plugged_in())
            nandlog_end_session();
      }
#endif

      // Determine whether there is an active experiment taking place
      static experiment_details_t scheduled_experiment;
      const bool radio_test = radio_test_boot(&scheduled_experiment);
      if (!radio_test)
         storage_retrieve_experiment_details(&scheduled_experiment);
      const uint32_t timestamp = rtc_get_timestamp(), time_of_day = rtc_get_time_of_day();
      const bool valid_experiment = rtc_is_valid() && scheduled_experiment.num_devices && !scheduled_experiment.is_terminated;
      const bool active_experiment = radio_test || experiment_is_active(&scheduled_experiment);
      experiment_start_time = scheduled_experiment.experiment_start_time;
      nandlog_disable(radio_test || !active_experiment);

      // Determine whether to power off for some time based on the device state
      uint32_t wake_on_timestamp = 0;
      bool power_off = false, allow_ranging = radio_test || !battery_monitor_is_plugged_in();
      if (allow_ranging)
      {
         uint32_t battery_level = battery_monitor_get_level_mV();
         if ((battery_level < BATTERY_NOMINAL) && !battery_monitor_is_plugged_in())
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

      // Arm the watchdog only here past the last point at which this boot could still decide to sleep instead of run
      system_watchdog_enable();

      // Create tasks with the following priority order:
      //    IdleTask < TimeAlignedTask < AppTask < BLETask < StorageTask < RangingTask
      xTaskCreateStatic(StorageTask, "StorageTask", STORAGE_TASK_STACK_WORDS, allow_ranging ? uid : NULL, 4, storage_task_stack, &storage_task_tcb);
#if !defined(_TEST_NO_EXP_DETAILS)
      xTaskCreateStatic(RangingTask, "RangingTask", RANGING_TASK_STACK_WORDS, allow_ranging ? &scheduled_experiment : NULL, 5, ranging_task_stack, &ranging_task_tcb);
#else
      xTaskCreateStatic(RangingTask, "RangingTask", RANGING_TASK_STACK_WORDS, allow_ranging ? uid : NULL, 5, ranging_task_stack, &ranging_task_tcb);
#endif
      xTaskCreateStatic(BLETask, "BLETask", BLE_TASK_STACK_WORDS, NULL, 3, ble_task_stack, &ble_task_tcb);
      xTaskCreateStatic(allow_ranging ? AppTaskRanging : AppTaskMaintenance, "AppTask", APP_TASK_STACK_WORDS, uid, 2, app_task_stack, &app_task_tcb);
      xTaskCreateStatic(TimeAlignedTask, "TimeAlignedTask", TIME_ALIGNED_TASK_STACK_WORDS, allow_ranging ? &scheduled_experiment : NULL, 1, time_aligned_task_stack, &time_aligned_task_tcb);
   }

   // Start the task scheduler
   vTaskStartScheduler();
}
