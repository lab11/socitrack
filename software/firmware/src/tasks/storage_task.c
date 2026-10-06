// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "imu.h"
#include "logging.h"
#include "nandlog.h"
#include "ranging.h"
#include "scheduler.h"
#include "rtc.h"
#include "storage_records.h"
#include "system.h"
#include "timers.h"


// Storage Task and Notification Types ---------------------------------------------------------------------------------

#define MAX_NUM_DATA_ITEMS      STORAGE_QUEUE_MAX_NUM_ITEMS

#ifndef _FW_DIRTY
#define _FW_DIRTY               0
#endif

_Static_assert(STORAGE_DIAGNOSTIC_NUM_STACKS == (WATCHDOG_NUM_TASKS + 1), "a diagnostics record reports every watchdog task's stack and then the timer service's");

typedef struct storage_item_t { uint32_t timestamp, value; uint8_t type; } storage_item_t;
typedef struct imu_data_t { uint8_t data[MAX_IMU_DATA_LENGTH]; uint32_t length; } imu_data_t;
typedef struct ranging_data_t { uint8_t data[MAX_COMPRESSED_RANGE_DATA_LENGTH]; uint32_t length; } ranging_data_t;
typedef struct ble_data_t { uint8_t data[1 + MAX_NUM_RANGING_DEVICES]; uint32_t length; } ble_data_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static volatile bool shutdown_requested;
static volatile uint16_t records_dropped;
static uint32_t previous_imu_timestamp;
static uint8_t ucQueueStorage[STORAGE_QUEUE_MAX_NUM_ITEMS * sizeof(storage_item_t)];
static StaticQueue_t xQueueBuffer;
static QueueHandle_t storage_queue;

#if !defined(_TEST_NO_STORAGE)

static imu_data_t imu_data[MAX_NUM_DATA_ITEMS];
static ranging_data_t range_data[MAX_NUM_DATA_ITEMS];
static ble_data_t ble_data[MAX_NUM_DATA_ITEMS];

#if DIAGNOSTIC_BUILD
#define MAX_NUM_ABORT_ITEMS     4
static storage_radio_abort_t abort_data[MAX_NUM_ABORT_ITEMS];
#endif

#endif  // #if !defined(_TEST_NO_STORAGE)


// Private Helper Functions --------------------------------------------------------------------------------------------

static bool enqueue_storage_item(const storage_item_t *item)
{
   // A record produced before the queue exists is discarded rather than handed to FreeRTOS as a null handle
   if (!storage_queue)
      return false;
   if (xQueueSendToBack(storage_queue, item, 0) == pdPASS)
      return true;

   // Anything else lost here would leave a gap in the log with nothing to say why
   if (records_dropped < UINT16_MAX)
      ++records_dropped;
   return false;
}

#ifndef _TEST_NO_STORAGE

static uint16_t saturate_u16(uint32_t value)
{
   return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
}

static uint32_t firmware_revision_code(void)
{
   // The build stamps the leading eight hex digits of its git commit, which fill a 32-bit word exactly
   static const char revision[] = STRINGIZE_VAL(_FW_REVISION);
   uint32_t code = 0;
   for (const char *digit = revision; *digit; ++digit)
   {
      const char c = *digit;
      const uint32_t nibble = ((c >= '0') && (c <= '9')) ? (uint32_t)(c - '0') :
                              ((c >= 'a') && (c <= 'f')) ? (uint32_t)(c - 'a' + 10) :
                              ((c >= 'A') && (c <= 'F')) ? (uint32_t)(c - 'A' + 10) : 16u;
      if (nibble > 15u)
         return 0;
      code = (code << 4) | nibble;
   }
   return code;
}

#endif  // #ifndef _TEST_NO_STORAGE


// Public API Functions ------------------------------------------------------------------------------------------------

void storage_queue_init(void)
{
   // Created before the scheduler starts
   if (!storage_queue)
      storage_queue = xQueueCreateStatic(STORAGE_QUEUE_MAX_NUM_ITEMS, sizeof(storage_item_t), ucQueueStorage, &xQueueBuffer);
}

#if !defined(_TEST_NO_STORAGE)

bool storage_flush_and_shutdown(void)
{
   // Shutdown is the one request that must not be dropped
   if (!storage_queue)
      return false;
   shutdown_requested = true;
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = 0, .type = STORAGE_TYPE_SHUTDOWN };
   xQueueSendToFront(storage_queue, &storage_item, 0);
   return true;
}

void storage_write_time_anchor(void)
{
   // The anchor's payload is this device's OWN clock, the experiment time with no network offset applied
   // So the pair (timestamp, value) is (network time, local time) at one instant and the offset is exactly their difference.
   // Absolute wall time is recoverable as experiment_start_time + value/1000
   const uint32_t local_clock = app_get_experiment_time(0);
   const int64_t network_clock = (int64_t)local_clock + app_get_time_offset();
   const storage_item_t storage_item = { .timestamp = (network_clock > 0) ? (uint32_t)network_clock : 0,
                                         .value = local_clock, .type = STORAGE_TYPE_TIME_ANCHOR };
   enqueue_storage_item(&storage_item);
}

void storage_write_diagnostics(void)
{
   // The payload is gathered in the storage task at dispatch rather than here
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = 0, .type = STORAGE_TYPE_DIAGNOSTICS };
   enqueue_storage_item(&storage_item);
}

void storage_write_battery_level(uint32_t battery_voltage_mV)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = battery_voltage_mV, .type = STORAGE_TYPE_VOLTAGE };
   enqueue_storage_item(&storage_item);
}

void storage_write_motion_status(motion_code_t motion_code)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = (uint32_t)motion_code, .type = STORAGE_TYPE_MOTION };
   enqueue_storage_item(&storage_item);
}

void storage_write_charging_status(battery_event_t battery_event)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = (uint32_t)battery_event, .type = STORAGE_TYPE_CHARGING_EVENT };
   enqueue_storage_item(&storage_item);
}

void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len, int32_t timestamp_offset)
{
   static uint32_t range_data_index = 0;

   // Joining a network re-bases the whole log's clock in one step to match the master's
   const int32_t previous_offset = app_get_time_offset();
   const int32_t offset_delta = timestamp_offset - previous_offset;
   if ((offset_delta > TIME_BASE_CHANGE_THRESHOLD_MS) || (offset_delta < -TIME_BASE_CHANGE_THRESHOLD_MS))
      storage_write_time_anchor();
   app_set_time_offset(timestamp_offset);
   const uint32_t record_timestamp = timestamp;
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = range_data_index, .type = STORAGE_TYPE_RANGES };
   memcpy(range_data[range_data_index].data, ranging_data, ranging_data_len);
   range_data[range_data_index].length = ranging_data_len;

   // The slot is only consumed if the record actually made it onto the queue
   if (enqueue_storage_item(&storage_item))
      range_data_index = (range_data_index + 1) % MAX_NUM_DATA_ITEMS;
}

void storage_write_ble_scan_results(uint8_t *found_devices, uint32_t num_devices)
{
   static uint32_t ble_data_index = 0;
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = ble_data_index, .type = STORAGE_TYPE_BLE_SCAN };
   ble_data[ble_data_index].data[0] = (uint8_t)num_devices;
   memcpy(ble_data[ble_data_index].data + 1, found_devices, num_devices);
   ble_data[ble_data_index].length = 1 + num_devices;
   if (enqueue_storage_item(&storage_item))
      ble_data_index = (ble_data_index + 1) % MAX_NUM_DATA_ITEMS;
}

void storage_write_imu_data(const uint8_t *data, uint32_t data_len)
{
   // Ensure that IMU data is not stored more frequently than 2Hz
   static uint32_t imu_data_index = 0;
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   if (record_timestamp >= (previous_imu_timestamp + 500))
   {
      previous_imu_timestamp = record_timestamp;
      const storage_item_t storage_item = { .timestamp = record_timestamp, .value = imu_data_index, .type = STORAGE_TYPE_IMU };
      imu_data[imu_data_index].length = 1;
      memcpy(imu_data[imu_data_index].data + imu_data[imu_data_index].length, data, data_len);
      imu_data[imu_data_index].length += data_len;
      imu_data[imu_data_index].data[0] = (uint8_t)imu_data[imu_data_index].length;
      if (enqueue_storage_item(&storage_item))
         imu_data_index = (imu_data_index + 1) % MAX_NUM_DATA_ITEMS;
   }
}

void storage_write_radio_abort(uint32_t timestamp, const storage_radio_abort_t *abort)
{
#if DIAGNOSTIC_BUILD
   static uint32_t abort_index = 0;
   abort_data[abort_index] = *abort;
   const storage_item_t storage_item = { .timestamp = timestamp, .value = abort_index, .type = STORAGE_TYPE_RADIO_ABORT };
   if (enqueue_storage_item(&storage_item))
      abort_index = (abort_index + 1) % MAX_NUM_ABORT_ITEMS;
#endif
}

#else

bool storage_flush_and_shutdown(void) { return false; }
void storage_write_battery_level(uint32_t battery_voltage_mV) {}
void storage_write_motion_status(motion_code_t motion_code) {}
void storage_write_charging_status(battery_event_t battery_event) {}
void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len, int32_t timestamp_offset) {}
void storage_write_ble_scan_results(uint8_t *found_devices, uint32_t num_devices) {}
void storage_write_imu_data(const uint8_t *data, uint32_t data_len) {}
void storage_write_radio_abort(uint32_t timestamp, const storage_radio_abort_t *abort) {}
void storage_write_time_anchor(void) {}
void storage_write_diagnostics(void) {}

#endif    // #if !defined(_TEST_NO_STORAGE)

void StorageTask(void *params)
{
   // Create a queue to hold pending storage items
   static storage_item_t item;
   previous_imu_timestamp = 0;

   // Recover the local-to-network time offset from the log instead of waiting for the next ranging round to re-derive it
   app_set_time_offset(0);
#if !defined(_TEST_NO_STORAGE)
   uint32_t anchor_network_ms = 0, anchor_local_ms = 0;
   if (recover_time_anchor(&anchor_network_ms, &anchor_local_ms))
   {
      // The anchor holds network time and local time at the same instant, so the offset is their difference
      if (time_anchor_is_plausible(anchor_network_ms, anchor_local_ms))
      {
         app_set_time_offset((int32_t)((int64_t)anchor_network_ms - (int64_t)anchor_local_ms));
         print("INFO: Recovered ranging time offset from a log anchor: %d ms\n", app_get_time_offset());
      }
      else
         print("ERROR: Recovered log anchor is implausible (network %u ms, local %u ms); keeping a zero offset until a ranging round supplies one\n", anchor_network_ms, anchor_local_ms);
   }
#endif

   // Now that the slow flash work of startup is behind us, ask to be watched
   storage_queue_init();
   system_watchdog_register(WATCHDOG_TASK_STORAGE);

   // Set whether the storage peripheral should be in maintenance mode
   if (params)
      nandlog_end_session();
   else
      nandlog_begin_session();

#if defined(_TEST_NO_STORAGE)

   // Loop forever, waiting until storage events are received
   while (true)
      if (xQueueReceive(storage_queue, &item, portMAX_DELAY) == pdPASS)
         if (item.type == STORAGE_TYPE_SHUTDOWN)
            system_reset(true);

#else

   // Record why the device last restarted once per boot
   const uint16_t reset_reason = system_get_reset_reason();
   nandlog_store_record(STORAGE_TYPE_RESET_REASON, app_get_experiment_time(app_get_time_offset()), &reset_reason, sizeof(reset_reason));

   // Anchor the boot itself, so the log records how stale the recovered offset was and how much real time the reboot consumed
   const uint32_t boot_local_clock = app_get_experiment_time(0);
   const int64_t boot_network_clock = (int64_t)boot_local_clock + app_get_time_offset();
   nandlog_store_record(STORAGE_TYPE_TIME_ANCHOR, (boot_network_clock > 0) ? (uint32_t)boot_network_clock : 0,
                        &boot_local_clock, sizeof(boot_local_clock));

   // Loop forever, waiting until storage events are received or buffered data has waited long enough
   const TickType_t flush_timeout_ticks = pdMS_TO_TICKS(1000 * STORAGE_FLUSH_TIMEOUT_S);
   TickType_t flush_armed_at = 0;
   bool flush_pending = false;

   const TickType_t checkin_ticks = pdMS_TO_TICKS(WATCHDOG_CHECKIN_INTERVAL_MS);
   while (true)
   {
      // Never wait indefinitely to check in with the watchdog timer
      TickType_t wait_ticks = checkin_ticks;
      if (flush_pending)
      {
         const TickType_t elapsed = xTaskGetTickCount() - flush_armed_at;
         const TickType_t remaining = (elapsed < flush_timeout_ticks) ? (flush_timeout_ticks - elapsed) : 0;
         wait_ticks = (remaining < checkin_ticks) ? remaining : checkin_ticks;
      }
      system_watchdog_pet(WATCHDOG_TASK_STORAGE);

      // A shutdown request is sticky, so it survives a full queue that dropped its nudge
      if (shutdown_requested)
      {
         nandlog_flush(true);
         system_reset(true);
      }

      if (xQueueReceive(storage_queue, &item, wait_ticks) == pdPASS)
      {
         switch (item.type)
         {
            case STORAGE_TYPE_SHUTDOWN:
               nandlog_flush(true);
               system_reset(true);
               break;
            case STORAGE_TYPE_VOLTAGE:
               nandlog_store_record(STORAGE_TYPE_VOLTAGE, item.timestamp, &item.value, sizeof(item.value));
               break;
            case STORAGE_TYPE_MOTION:
            {
               const uint8_t in_motion = (item.value != NOT_IN_MOTION) ? 1 : 0;
               nandlog_store_record(STORAGE_TYPE_MOTION, item.timestamp, &in_motion, sizeof(in_motion));
               break;
            }
            case STORAGE_TYPE_CHARGING_EVENT:
            {
               const uint8_t charging_event = (uint8_t)item.value;
               nandlog_store_record(STORAGE_TYPE_CHARGING_EVENT, item.timestamp, &charging_event, sizeof(charging_event));
               break;
            }
            case STORAGE_TYPE_RANGES:
               nandlog_store_record(STORAGE_TYPE_RANGES, item.timestamp, range_data[item.value].data, range_data[item.value].length);
               break;
            case STORAGE_TYPE_IMU:
               nandlog_store_record(STORAGE_TYPE_IMU, item.timestamp, imu_data[item.value].data, imu_data[item.value].length);
               break;
            case STORAGE_TYPE_TIME_ANCHOR:
               nandlog_store_record(STORAGE_TYPE_TIME_ANCHOR, item.timestamp, &item.value, sizeof(item.value));
               break;
            case STORAGE_TYPE_BLE_SCAN:
               nandlog_store_record(STORAGE_TYPE_BLE_SCAN, item.timestamp, ble_data[item.value].data, ble_data[item.value].length);
               break;
#if DIAGNOSTIC_BUILD
            case STORAGE_TYPE_RADIO_ABORT:
               nandlog_store_record(STORAGE_TYPE_RADIO_ABORT, item.timestamp, &abort_data[item.value], sizeof(abort_data[item.value]));
               break;
#endif
            case STORAGE_TYPE_DIAGNOSTICS:
            {
               // Gathered here rather than at the call site so the counters are read as late as possible
               storage_diagnostics_t diagnostics = { 0 };
               watchdog_stats_t watchdog = { 0 };
               system_watchdog_get_stats(&watchdog);
               diagnostics.watchdog_declines = watchdog.declines;
               for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
                  diagnostics.watchdog_late_episodes[task] = watchdog.late_episodes[task];
               const uint32_t suppressed = battery_monitor_get_suppressed_edge_count();
               diagnostics.charger_suppressed_edges = (suppressed > UINT16_MAX) ? UINT16_MAX : (uint16_t)suppressed;
               bluetooth_buffer_stats_t buffers;
               bluetooth_get_buffer_stats(&buffers);
               diagnostics.wsf_alloc_failures = (buffers.failures > UINT16_MAX) ? UINT16_MAX : (uint16_t)buffers.failures;
               diagnostics.wsf_largest_failed_length = buffers.largest_failed_length;
               const uint8_t reported = (buffers.num_pools > STORAGE_DIAGNOSTIC_NUM_POOLS) ? STORAGE_DIAGNOSTIC_NUM_POOLS : buffers.num_pools;
               for (uint8_t pool = 0; pool < reported; ++pool)
               {
                  diagnostics.wsf_pool_high_water[pool] = buffers.high_water[pool];
                  diagnostics.wsf_pool_capacity[pool] = buffers.capacity[pool];
               }
               diagnostics.master_cycle_failures = scheduler_get_master_cycle_failures();
               diagnostics.firmware_revision = firmware_revision_code();
               diagnostics.status_flags = (battery_monitor_tempco_available() ? STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE : 0) |
                                          (battery_monitor_tempco_applied() ? STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED : 0) |
                                          (_FW_DIRTY ? STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED : 0) |
                                          (DIAGNOSTIC_BUILD ? STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD : 0) |
                                          (TEMPCO_ENABLED ? 0 : STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED);
               diagnostics.temperature_c = battery_monitor_get_temperature_c();

               // Radio health, since boot
               ranging_radio_stats_t radio;
               ranging_radio_get_stats(&radio);
               diagnostics.radio_rx_ok = radio.rx_ok;
               diagnostics.radio_rx_failed = radio.rx_failed;
               diagnostics.radio_tx_late = saturate_u16(radio.tx_failed);
               diagnostics.radio_rx_arm_late = saturate_u16(radio.rx_arm_failed);
               diagnostics.radio_isr_over_budget = saturate_u16(radio.isr_over_count);
               diagnostics.radio_isr_warm_max_us = saturate_u16(radio.isr_warm_max_us);
               diagnostics.radio_irq_stuck = saturate_u16(ranging_radio_get_isr_overrun_count());
               diagnostics.radio_wake_max_us = saturate_u16(radio.wake_max_us);
               diagnostics.radio_wake_failures = saturate_u16(radio.wake_failed);

               // Silent losses, stack headroom, and recoveries
               diagnostics.storage_records_dropped = records_dropped;
               for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
                  diagnostics.stack_free_words[task] = saturate_u16(system_get_task_stack_free_words((watchdog_task_t)task));
               const TaskHandle_t timer_service = xTimerGetTimerDaemonTaskHandle();
               diagnostics.stack_free_words[WATCHDOG_NUM_TASKS] = timer_service ? saturate_u16(uxTaskGetStackHighWaterMark(timer_service)) : STORAGE_DIAGNOSTIC_STACK_UNMONITORED;
               const uint32_t ble_resets = bluetooth_get_reset_count();
               diagnostics.ble_resets = (ble_resets > UINT8_MAX) ? UINT8_MAX : (uint8_t)ble_resets;
               diagnostics.nand_bad_blocks = saturate_u16(nandlog_bad_block_count());
               nandlog_store_record(STORAGE_TYPE_DIAGNOSTICS, item.timestamp, &diagnostics, sizeof(diagnostics));
               break;
            }
            default:
               break;
         }

         // Arm the deadline the moment the cache becomes dirty, and disarm it once a write empties it
         if (!nandlog_has_buffered_data())
            flush_pending = false;
         else if (!flush_pending)
         {
            flush_pending = true;
            flush_armed_at = xTaskGetTickCount();
         }
      }
      else if (flush_pending && ((xTaskGetTickCount() - flush_armed_at) >= flush_timeout_ticks))
      {
         // Buffered data has aged out so commit it as a partial page
         nandlog_flush(true);
         flush_pending = nandlog_has_buffered_data();
         flush_armed_at = xTaskGetTickCount();
      }
   }

#endif
}
