// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "imu.h"
#include "logging.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"


// Storage Task and Notification Types ---------------------------------------------------------------------------------

#define MAX_NUM_DATA_ITEMS      (STORAGE_QUEUE_MAX_NUM_ITEMS / 3)

typedef struct storage_item_t { uint32_t timestamp, value; uint8_t type; } storage_item_t;
typedef struct imu_data_t { uint8_t data[MAX_IMU_DATA_LENGTH]; uint32_t length; } imu_data_t;
typedef struct ranging_data_t { uint8_t data[MAX_COMPRESSED_RANGE_DATA_LENGTH]; uint32_t length; } ranging_data_t;
typedef struct ble_data_t { uint8_t data[1 + MAX_NUM_RANGING_DEVICES]; uint32_t length; } ble_data_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint32_t previous_imu_timestamp;
static uint8_t ucQueueStorage[STORAGE_QUEUE_MAX_NUM_ITEMS * sizeof(storage_item_t)];
static StaticQueue_t xQueueBuffer;
static QueueHandle_t storage_queue;

#if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)

static imu_data_t imu_data[MAX_NUM_DATA_ITEMS];
static ranging_data_t range_data[MAX_NUM_DATA_ITEMS];
static ble_data_t ble_data[MAX_NUM_DATA_ITEMS];

#endif  // #if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)


// Public API Functions ------------------------------------------------------------------------------------------------

#if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)

void storage_flush_and_shutdown(void)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = 0, .type = STORAGE_TYPE_SHUTDOWN };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_time_anchor(void)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = rtc_get_timestamp(), .type = STORAGE_TYPE_TIME_ANCHOR };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_battery_level(uint32_t battery_voltage_mV)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = battery_voltage_mV, .type = STORAGE_TYPE_VOLTAGE };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_motion_status(motion_code_t motion_code)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = (uint32_t)motion_code, .type = STORAGE_TYPE_MOTION };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_charging_status(battery_event_t battery_event)
{
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = (uint32_t)battery_event, .type = STORAGE_TYPE_CHARGING_EVENT };
   xQueueSendToBack(storage_queue, &storage_item, 0);
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
   range_data_index = (range_data_index + 1) % MAX_NUM_DATA_ITEMS;
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_ble_scan_results(uint8_t *found_devices, uint32_t num_devices)
{
   static uint32_t ble_data_index = 0;
   const uint32_t record_timestamp = app_get_experiment_time(app_get_time_offset());
   const storage_item_t storage_item = { .timestamp = record_timestamp, .value = ble_data_index, .type = STORAGE_TYPE_BLE_SCAN };
   ble_data[ble_data_index].data[0] = (uint8_t)num_devices;
   memcpy(ble_data[ble_data_index].data + 1, found_devices, num_devices);
   ble_data[ble_data_index].length = 1 + num_devices;
   ble_data_index = (ble_data_index + 1) % MAX_NUM_DATA_ITEMS;
   xQueueSendToBack(storage_queue, &storage_item, 0);
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
      imu_data_index = (imu_data_index + 1) % MAX_NUM_DATA_ITEMS;
      xQueueSendToBack(storage_queue, &storage_item, 0);
   }
}

#else

void storage_flush_and_shutdown(void) {}
void storage_write_battery_level(uint32_t battery_voltage_mV) {}
void storage_write_motion_status(motion_code_t motion_code) {}
void storage_write_charging_status(battery_event_t battery_event) {}
void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len, int32_t timestamp_offset) {}
void storage_write_ble_scan_results(uint8_t *found_devices, uint32_t num_devices) {}
void storage_write_imu_data(const uint8_t *data, uint32_t data_len) {}
void storage_write_time_anchor(void) {}

#endif    // #if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)

void StorageTask(void *params)
{
   // Create a queue to hold pending storage items
   static storage_item_t item;
   previous_imu_timestamp = 0;

   // Recover the local-to-network time offset from the log instead of waiting for the next ranging round to re-derive it
   app_set_time_offset(0);
#if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)
   uint32_t anchor_experiment_ms = 0, anchor_rtc = 0;
   if (storage_recover_time_anchor(&anchor_experiment_ms, &anchor_rtc))
   {
      // Recover the local clock offset from network time
      const uint32_t experiment_start = app_get_experiment_start_time();
      const int64_t rtc_elapsed_ms = (anchor_rtc > experiment_start) ? ((int64_t)(anchor_rtc - experiment_start) * 1000) : 0;
      app_set_time_offset((int32_t)((int64_t)anchor_experiment_ms - rtc_elapsed_ms));
      print("INFO: Recovered ranging time offset from a log anchor: %d ms\n", app_get_time_offset());
   }
#endif
   storage_queue = xQueueCreateStatic(STORAGE_QUEUE_MAX_NUM_ITEMS, sizeof(storage_item_t), ucQueueStorage, &xQueueBuffer);

   // Set whether the storage peripheral should be in maintenance mode
   if (params)
      storage_exit_maintenance_mode();
   else
      storage_enter_maintenance_mode();

#if REVISION_ID == REVISION_APOLLO4_EVB || defined(_TEST_NO_STORAGE)

   // Loop forever, waiting until storage events are received
   while (true)
      if (xQueueReceive(storage_queue, &item, portMAX_DELAY) == pdPASS)
         if (item.type == STORAGE_TYPE_SHUTDOWN)
            system_reset(true);

#else

   // Record why the device last restarted once per boot
   const uint16_t reset_reason = system_get_reset_reason();
   storage_store_record(STORAGE_TYPE_RESET_REASON, app_get_experiment_time(app_get_time_offset()), &reset_reason, sizeof(reset_reason));

   // Anchor the boot time itself
   const uint32_t boot_rtc = rtc_get_timestamp();
   storage_store_record(STORAGE_TYPE_TIME_ANCHOR, app_get_experiment_time(app_get_time_offset()), &boot_rtc, sizeof(boot_rtc));

   // Loop forever, waiting until storage events are received or buffered data has waited long enough
   const TickType_t flush_timeout_ticks = pdMS_TO_TICKS(1000 * STORAGE_FLUSH_TIMEOUT_S);
   TickType_t flush_armed_at = 0;
   bool flush_pending = false;

   while (true)
   {
      // Wait for the remaining lifetime of the oldest buffered data or indefinitely if there is none
      TickType_t wait_ticks = portMAX_DELAY;
      if (flush_pending)
      {
         const TickType_t elapsed = xTaskGetTickCount() - flush_armed_at;
         wait_ticks = (elapsed < flush_timeout_ticks) ? (flush_timeout_ticks - elapsed) : 0;
      }

      if (xQueueReceive(storage_queue, &item, wait_ticks) == pdPASS)
      {
         switch (item.type)
         {
            case STORAGE_TYPE_SHUTDOWN:
               storage_flush(true);
               system_reset(true);
               break;
            case STORAGE_TYPE_VOLTAGE:
               storage_store_record(STORAGE_TYPE_VOLTAGE, item.timestamp, &item.value, sizeof(item.value));
               break;
            case STORAGE_TYPE_MOTION:
            {
               const uint8_t in_motion = (item.value != NOT_IN_MOTION) ? 1 : 0;
               storage_store_record(STORAGE_TYPE_MOTION, item.timestamp, &in_motion, sizeof(in_motion));
               break;
            }
            case STORAGE_TYPE_CHARGING_EVENT:
            {
               const uint8_t charging_event = (uint8_t)item.value;
               storage_store_record(STORAGE_TYPE_CHARGING_EVENT, item.timestamp, &charging_event, sizeof(charging_event));
               break;
            }
            case STORAGE_TYPE_RANGES:
               storage_store_record(STORAGE_TYPE_RANGES, item.timestamp, range_data[item.value].data, range_data[item.value].length);
               break;
            case STORAGE_TYPE_IMU:
               storage_store_record(STORAGE_TYPE_IMU, item.timestamp, imu_data[item.value].data, imu_data[item.value].length);
               break;
            case STORAGE_TYPE_TIME_ANCHOR:
               storage_store_record(STORAGE_TYPE_TIME_ANCHOR, item.timestamp, &item.value, sizeof(item.value));
               break;
            case STORAGE_TYPE_BLE_SCAN:
               storage_store_record(STORAGE_TYPE_BLE_SCAN, item.timestamp, ble_data[item.value].data, ble_data[item.value].length);
               break;
            default:
               break;
         }

         // Arm the deadline the moment the cache becomes dirty, and disarm it once a write empties it
         if (!storage_has_buffered_data())
            flush_pending = false;
         else if (!flush_pending)
         {
            flush_pending = true;
            flush_armed_at = xTaskGetTickCount();
         }
      }
      else
      {
         // Buffered data has aged out so commit it as a partial page
         storage_flush(true);
         flush_pending = storage_has_buffered_data();
         flush_armed_at = xTaskGetTickCount();
      }
   }

#endif
}
