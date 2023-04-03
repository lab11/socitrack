#include "storage.h"
#include "system.h"
#include "logging.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   storage_init();

   // Write some random stuff to storage
   storage_exit_maintenance_mode();
   uint8_t random_data[MEMORY_PAGE_SIZE_BYTES*2];
   for (uint32_t i = 0; i < 2; ++i)
   {
      memcpy(&random_data[i*MEMORY_PAGE_SIZE_BYTES], "START TEXT", 10);
      for (uint32_t j = 10; j < MEMORY_PAGE_SIZE_BYTES; ++j)
         random_data[(i*MEMORY_PAGE_SIZE_BYTES)+j] = (uint8_t)j;
   }
   storage_store(random_data, MEMORY_PAGE_SIZE_BYTES * 3 / 2);
   storage_flush(false);
   storage_store(random_data + (MEMORY_PAGE_SIZE_BYTES / 2), MEMORY_PAGE_SIZE_BYTES / 2);
   storage_flush(false);

   // Read the stuff back from storage
   uint8_t read_data[MEMORY_PAGE_SIZE_BYTES*2];
   storage_enter_maintenance_mode();
   storage_begin_reading();
   uint32_t stored_length = storage_retrieve_data_length();
   print("Stored length: %u\n", stored_length);
   uint32_t bytes_read = storage_retrieve_next_data_chunk(read_data);
   while (bytes_read)
   {
      print("Read %u bytes\n", bytes_read);
      bytes_read = storage_retrieve_next_data_chunk(read_data);
   }
   print("Reading complete\n");

   // TODO: Test storing a new set of experiment details
   experiment_details_t details = {
      .experiment_start_time = 0, .experiment_end_time = 0,
      .daily_start_time = 0, .daily_end_time = 0,
      .num_devices = 3, .uids = { 0 }, .uid_name_mappings = { 0 }
   };
   for (uint8_t i = 0; i < details.num_devices; ++i)
   {
      uint8_t uid = { 0xc0, 0x98, 0x01, 0x02, 0x68, i+1 };
      memcpy(details.uids[i], uid, EUI_LEN);
      memcpy(details.uid_name_mappings[i], "Test Name", 9);
   }
   storage_store_experiment_details(&details);

   // Test retrieving the set of experiment details
   memset(&details, 0, sizeof(details));
   storage_retrieve_experiment_details(&details);

   // Done with test, loop forever
   while (true)
      am_hal_delay_us(1);

   // Should never reach this point
   return 0;
}
