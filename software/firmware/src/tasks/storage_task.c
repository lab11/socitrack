// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"


// Storage Task and Notification Types ---------------------------------------------------------------------------------

typedef enum {
   STORAGE_TYPE_SHUTDOWN = 0,
   STORAGE_TYPE_VOLTAGE,
   STORAGE_TYPE_CHARGING_EVENT,
   STORAGE_TYPE_MOTION,
   STORAGE_TYPE_RANGES
} storage_data_type_t;

typedef struct storage_item_t { uint32_t timestamp, value, type; } storage_item_t;
typedef struct ranging_data_t { uint8_t data[MAX_COMPRESSED_RANGE_DATA_LENGTH]; uint32_t length; } ranging_data_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static QueueHandle_t storage_queue;
static ranging_data_t range_data[STORAGE_QUEUE_MAX_NUM_ITEMS];
static uint32_t range_data_index;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void store_battery_voltage(uint32_t timestamp, uint32_t battery_voltage_mV)
{
   const uint8_t storage_type = STORAGE_TYPE_VOLTAGE;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(&battery_voltage_mV, sizeof(battery_voltage_mV));
   storage_flush(false);
}

static void store_charging_event(uint32_t timestamp, uint8_t event_code)
{
   const uint8_t storage_type = STORAGE_TYPE_CHARGING_EVENT;
   storage_store(&storage_type, sizeof(storage_type));
   storage_store(&timestamp, sizeof(timestamp));
   storage_store(&event_code, sizeof(event_code));
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
   storage_store(range_data, range_data_len);
   storage_flush(false);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void storage_flush_and_shutdown(void)
{
   storage_item_t storage_item = { .timestamp = rtc_get_timestamp(), .value = 0, .type = STORAGE_TYPE_SHUTDOWN };
   xQueueSendToBack(storage_queue, &storage_item, portMAX_DELAY);
}

void storage_write_battery_level(uint32_t battery_voltage_mV)
{
   storage_item_t storage_item = { .timestamp = rtc_get_timestamp(), .value = battery_voltage_mV, .type = STORAGE_TYPE_VOLTAGE };
   xQueueSendToBack(storage_queue, &storage_item, portMAX_DELAY);
}

void storage_write_charging_event(battery_event_t battery_event)
{
   storage_item_t storage_item = { .timestamp = rtc_get_timestamp(), .value = battery_event, .type = STORAGE_TYPE_CHARGING_EVENT };
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xQueueSendToBackFromISR(storage_queue, &storage_item, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void storage_write_motion_status(bool in_motion)
{
   storage_item_t storage_item = { .timestamp = rtc_get_timestamp(), .value = in_motion, .type = STORAGE_TYPE_MOTION };
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xQueueSendToBackFromISR(storage_queue, &storage_item, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len)
{
   storage_item_t storage_item = { .timestamp = timestamp, .value = range_data_index, .type = STORAGE_TYPE_RANGES };
   memcpy(range_data[range_data_index].data, ranging_data, ranging_data_len);
   range_data[range_data_index].length = ranging_data_len;
   range_data_index = (range_data_index + 1) % STORAGE_QUEUE_MAX_NUM_ITEMS;
   xQueueSendToBack(storage_queue, &storage_item, portMAX_DELAY);
}

void StorageTask(void *params)
{
   // Create a queue to hold pending storage items
   storage_item_t item;
   range_data_index = 0;
   storage_queue = xQueueCreate(STORAGE_QUEUE_MAX_NUM_ITEMS, sizeof(storage_item_t));

   // Set whether the storage peripheral should be in maintenance mode
   if (params)
      storage_exit_maintenance_mode();
   else
      storage_enter_maintenance_mode();

   // Loop forever, waiting until storage events are received
   while (true)
      if (xQueueReceive(storage_queue, &item, portMAX_DELAY) == pdPASS)
         switch (item.type)
         {
            case STORAGE_TYPE_SHUTDOWN:
               storage_flush(true);
               system_reset();
               break;
            case STORAGE_TYPE_VOLTAGE:
               store_battery_voltage(item.timestamp, item.value);
               break;
            case STORAGE_TYPE_CHARGING_EVENT:
               store_charging_event(item.timestamp, (uint8_t)item.value);
               break;
            case STORAGE_TYPE_MOTION:
               store_motion_change(item.timestamp, item.value);
               break;
            case STORAGE_TYPE_RANGES:
               store_ranges(item.timestamp, range_data[item.value].data, range_data[item.value].length);
               break;
            default:
               break;
         }
}
