#include "app_tasks.h"
#include "bluetooth.h"
#include "button.h"
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


// BLUETOOTH TEST FUNCTIONALITY ----------------------------------------------------------------------------------------

typedef enum { SCANNING_START = 1, SCANNING_STOP } test_notification_t;
static TaskHandle_t ble_task_handle, test_task_handle;
static uint8_t device_uid[EUI_LEN];

static void button_pressed(void *button_number)
{
   // Notify the Test Task that a button was pressed
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xTaskNotifyFromISR(test_task_handle, ((uint32_t)button_number == PIN_BUTTON_1) ? SCANNING_START : SCANNING_STOP, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void ble_discovery_handler(const uint8_t ble_address[6], uint8_t ranging_role)
{
   print("Discovered %02X:%02X:%02X:%02X:%02X:%02X\n", ble_address[0], ble_address[1], ble_address[2],
         ble_address[3], ble_address[4], ble_address[5]);
   bluetooth_stop_scanning();
   bluetooth_join_ranging_network(ble_address);
}

void TestTask(void *this_uid)
{
   // Initiate interrupt-based button press detection
   uint32_t notification_bits = 0;
   button_press_register_callback(PIN_BUTTON_1, button_pressed);
   button_press_register_callback(PIN_BUTTON_2, button_pressed);

   // Wait until the BLE stack has been fully initialized
   while (!bluetooth_is_initialized())
      vTaskDelay(1);

   // Update the whitelist, and start advertising over BLE
   //uint8_t uid[6] = { 0x01, 0x00, 0x42, 0xe5, 0x98, 0xc0 };
   //bluetooth_add_device_to_whitelist(uid);
   bluetooth_start_advertising();

   // Loop forever waiting for notifications
   uint8_t results[4] = { 1, 5, 0, 0 };
   while (true)
   {
      xTaskNotifyWait(pdFALSE, 0xffffffff, &notification_bits, pdMS_TO_TICKS(1000));
      if ((notification_bits & SCANNING_START) != 0)
      {
         print("STARTING SCANNING\n");
         bluetooth_start_scanning();
      }
      else if ((notification_bits & SCANNING_STOP) != 0)
      {
         print("STOPPING SCANNING\n");
         bluetooth_stop_scanning();
      }
      ++(*((uint16_t*)(&results[2])));
      bluetooth_write_range_results(results, sizeof(results));
   }
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   buttons_init();

   // Fetch the device UID and initialize the Bluetooth hardware
   system_read_UID(device_uid, sizeof(device_uid));
   bluetooth_init(device_uid);
   print("Initialized BLE with address %02X:%02X:%02X:%02X:%02X:%02X\n", device_uid[0], device_uid[1], device_uid[2],
                                                                         device_uid[3], device_uid[4], device_uid[5]);

   // Register handlers for BLE events
   bluetooth_register_discovery_callback(ble_discovery_handler);

   // Create the BLE task and start the task scheduler
   configASSERT1(xTaskCreate(BLETask, "BLETask", 512, NULL, 3, &ble_task_handle));
   configASSERT1(xTaskCreate(TestTask, "TestTask", 128, NULL, 2, &test_task_handle));
   vTaskStartScheduler();

   // Should never reach this point
   return 0;
}
