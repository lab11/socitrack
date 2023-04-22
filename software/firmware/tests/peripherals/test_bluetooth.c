#include "app_tasks.h"
#include "bluetooth.h"
#include "logging.h"
#include "system.h"


// EMULATE STORAGE READING FUNCTIONALITY -------------------------------------------------------------------------------

#define MEMORY_PAGE_SIZE_BYTES 2048
#define MEMORY_PAGE_COUNT 32768

static uint16_t reading_page, final_page, cache_index;
static uint8_t cache[2 * MEMORY_PAGE_SIZE_BYTES];
static const uint32_t total_size = 2097158;
static bool is_reading;

static void read_page(uint8_t *buffer)
{
   for (uint16_t i = 0; i < MEMORY_PAGE_SIZE_BYTES; ++i)
      buffer[i] = (uint8_t)((((uint32_t)reading_page * MEMORY_PAGE_SIZE_BYTES) + i) & 0xFF);
}
void storage_begin_reading(void)
{
   reading_page = 0;
   is_reading = true;
   cache_index = total_size % MEMORY_PAGE_SIZE_BYTES;
   final_page = (total_size / MEMORY_PAGE_SIZE_BYTES) + (((total_size % MEMORY_PAGE_SIZE_BYTES) > 0) ? 1 : 0) - 1;
   for (uint16_t i = 0; i < cache_index; ++i)
      cache[i] = (uint8_t)i;
}
void storage_end_reading(void) { is_reading = false; }
uint32_t storage_retrieve_data_length(void) { return total_size; }
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;

   // Determine if a full page of memory is available to read
   uint32_t num_bytes_retrieved = MEMORY_PAGE_SIZE_BYTES;
   if (reading_page == final_page)
   {
      // Return the valid available bytes
      memcpy(buffer, cache, cache_index);
      num_bytes_retrieved = cache_index;
      is_reading = false;
   }
   else
   {
      // Read the next page of memory and update the reading metadata
      read_page(buffer);
      reading_page = (reading_page + 1) % MEMORY_PAGE_COUNT;
   }
   return num_bytes_retrieved;
}


int main(void)
{
   // Set up system hardware
   setup_hardware();
   
   print("please print this!");
   // Fetch the device UID and initialize the Bluetooth hardware
   static uint8_t uid[EUI_LEN];
   system_read_UID(uid, sizeof(uid));
   bluetooth_init(uid);
   //am_hal_delay_us(5000000);

   // Create the BLE task and start the task scheduler
   static TaskHandle_t ble_task_handle;
   configASSERT1(xTaskCreate(BLETask, "BLETask", 512, NULL, 3, &ble_task_handle));
   bluetooth_start_advertising();
   vTaskStartScheduler();

   // Should never reach this point
   return 0;
}
