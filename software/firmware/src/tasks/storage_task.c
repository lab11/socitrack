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
static imu_data_t imu_data[MAX_NUM_DATA_ITEMS];
static ranging_data_t range_data[MAX_NUM_DATA_ITEMS];
static ble_data_t ble_data[MAX_NUM_DATA_ITEMS];
static uint8_t ucQueueStorage[STORAGE_QUEUE_MAX_NUM_ITEMS * sizeof(storage_item_t)];
static StaticQueue_t xQueueBuffer;
static QueueHandle_t storage_queue;

int32_t ranging_timestamp_offset;
// Private Helper Functions --------------------------------------------------------------------------------------------

#if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)

static void store_battery_voltage(uint32_t timestamp, uint32_t battery_voltage_mV)
{
   const uint8_t storage_type = STORAGE_TYPE_VOLTAGE;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(&battery_voltage_mV, sizeof(battery_voltage_mV));
   storage_flush(false);
}

static void store_motion_change(uint32_t timestamp, bool in_motion)
{
   const uint8_t storage_type = STORAGE_TYPE_MOTION;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(&in_motion, sizeof(in_motion));
   storage_flush(false);
}

static void store_ranges(uint32_t timestamp, const uint8_t *range_data, uint32_t range_data_len)
{
   const uint8_t storage_type = STORAGE_TYPE_RANGES;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(range_data, range_data_len);
   storage_flush(false);
}

static void store_ble_scan_data(uint32_t timestamp, const uint8_t *ble_data, uint32_t ble_data_len)
{
   const uint8_t storage_type = STORAGE_TYPE_BLE_SCAN;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(ble_data, ble_data_len);
   storage_flush(false);
}

static void store_imu_data(uint32_t timestamp, const uint8_t *imu_data, uint32_t imu_data_len)
{
   const uint8_t storage_type = STORAGE_TYPE_IMU;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(imu_data, imu_data_len);
   storage_flush(false);
}


// Public API Functions ------------------------------------------------------------------------------------------------

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

void storage_write_motion_status(bool in_motion)
{
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = in_motion, .type = STORAGE_TYPE_MOTION };
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

#ifdef _TEST_IMU_DATA
void storage_write_imu_data(const uint8_t *raw_data, uint32_t raw_data_len)
{
   static uint32_t imu_data_index = 0;
   const bno055_data_type_t data_types[] = { STAT_DATA, LACC_DATA, GYRO_DATA, QUAT_DATA};
   const storage_item_t storage_item = { .timestamp = app_get_experiment_time(ranging_timestamp_offset), .value = imu_data_index, .type = STORAGE_TYPE_IMU };
   imu_data[imu_data_index].length = 0;
   for (uint8_t i = 0; i < sizeof(data_types) / sizeof(data_types[0]); ++i)
   {
      const uint8_t *data;
      uint8_t data_len = imu_pick_data_from_raw(&data, raw_data, data_types[i]);
      memcpy(imu_data[imu_data_index].data + imu_data[imu_data_index].length, data, data_len);
      imu_data[imu_data_index].length += data_len;
   }
   imu_data_index = (imu_data_index + 1) % MAX_NUM_DATA_ITEMS;
   xQueueSendToBack(storage_queue, &storage_item, 0);
}
#else
void storage_write_imu_data(const uint8_t *calib_data, const int16_t *accel_data)
{
   // Ensure that IMU data is not stored more frequently than 2Hz
   static uint32_t imu_data_index = 0;
   const uint32_t rounded_timestamp = 500 * (app_get_experiment_time(ranging_timestamp_offset) / 500);
   if (rounded_timestamp >= (previous_imu_timestamp + 500))
   {
      previous_imu_timestamp = rounded_timestamp;
      const storage_item_t storage_item = { .timestamp = rounded_timestamp, .value = imu_data_index, .type = STORAGE_TYPE_IMU };
      imu_data[imu_data_index].length = 1;
      memcpy(imu_data[imu_data_index].data + imu_data[imu_data_index].length, calib_data, sizeof(uint8_t));
      imu_data[imu_data_index].length += sizeof(uint8_t);
      memcpy(imu_data[imu_data_index].data + imu_data[imu_data_index].length, accel_data, 3 * sizeof(int16_t));
      imu_data[imu_data_index].length += 3 * sizeof(int16_t);
      imu_data[imu_data_index].data[0] = (uint8_t)imu_data[imu_data_index].length;
      imu_data_index = (imu_data_index + 1) % MAX_NUM_DATA_ITEMS;
      xQueueSendToBack(storage_queue, &storage_item, 0);
   }
}
#endif  // #ifdef _TEST_IMU_DATA

#else

void storage_flush_and_shutdown(void) {}
void storage_write_battery_level(uint32_t battery_voltage_mV) {}
void storage_write_motion_status(bool in_motion) {}
void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len, int32_t timestamp_offset) {}

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

   // Loop forever, waiting until storage events are received
   while (true)
      if (xQueueReceive(storage_queue, &item, portMAX_DELAY) == pdPASS)
#if REVISION_ID == REVISION_APOLLO4_EVB || defined(_TEST_NO_STORAGE)
         if (item.type == STORAGE_TYPE_SHUTDOWN)
            system_reset(true);
#else
         switch (item.type)
         {
            case STORAGE_TYPE_SHUTDOWN:
               storage_flush(true);
               system_reset(true);
               break;
            case STORAGE_TYPE_VOLTAGE:
               store_battery_voltage(item.timestamp, item.value);
               break;
            case STORAGE_TYPE_MOTION:
               store_motion_change(item.timestamp, item.value);
               break;
            case STORAGE_TYPE_RANGES:
               store_ranges(item.timestamp, range_data[item.value].data, range_data[item.value].length);
               break;
            case STORAGE_TYPE_IMU:
               store_imu_data(item.timestamp, imu_data[item.value].data, imu_data[item.value].length);
               break;
            case STORAGE_TYPE_BLE_SCAN:
               store_ble_scan_data(item.timestamp, ble_data[item.value].data, ble_data[item.value].length);
               break;
            default:
               break;
         }
#endif
}
