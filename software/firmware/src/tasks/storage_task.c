// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "imu.h"
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
static int32_t ranging_timestamp_offset;
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
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = 0, .type = STORAGE_TYPE_SHUTDOWN };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_battery_level(uint32_t battery_voltage_mV)
{
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = battery_voltage_mV, .type = STORAGE_TYPE_VOLTAGE };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_motion_status(motion_code_t motion_code)
{
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = (uint32_t)motion_code, .type = STORAGE_TYPE_MOTION };
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len, int32_t timestamp_offset)
{
   static uint32_t range_data_index = 0;
   ranging_timestamp_offset = timestamp_offset;
   const uint32_t rounded_timestamp = 500 * (timestamp / 500);
   const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = range_data_index, .type = STORAGE_TYPE_RANGES };
   memcpy(range_data[range_data_index].data, ranging_data, ranging_data_len);
   range_data[range_data_index].length = ranging_data_len;
   range_data_index = (range_data_index + 1) % MAX_NUM_DATA_ITEMS;
   xQueueSendToBack(storage_queue, &storage_item, 0);
}

void storage_write_ble_scan_results(uint8_t *found_devices, uint32_t num_devices)
{
   static uint32_t ble_data_index = 0;
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = ble_data_index, .type = STORAGE_TYPE_BLE_SCAN };
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
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   if (rounded_timestamp >= (previous_imu_timestamp + 500))
   {
      previous_imu_timestamp = rounded_timestamp;
      const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = imu_data_index, .type = STORAGE_TYPE_IMU };
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
void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len, int32_t timestamp_offset) {}
void storage_write_ble_scan_results(uint8_t *found_devices, uint32_t num_devices) {}
void storage_write_imu_data(const uint8_t *data, uint32_t data_len) {}

#endif    // #if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)

void StorageTask(void *params)
{
   // Create a queue to hold pending storage items
   static storage_item_t item;
   previous_imu_timestamp = 0;
   ranging_timestamp_offset = 0;
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
               storage_store_record(STORAGE_TYPE_MOTION, item.timestamp, &item.value, sizeof(uint8_t));
               break;
            case STORAGE_TYPE_RANGES:
               storage_store_record(STORAGE_TYPE_RANGES, item.timestamp, range_data[item.value].data, range_data[item.value].length);
               break;
            case STORAGE_TYPE_IMU:
               storage_store_record(STORAGE_TYPE_IMU, item.timestamp, imu_data[item.value].data, imu_data[item.value].length);
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
