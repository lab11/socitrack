#include "storage.h"
#include "system.h"
#include "logging.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   storage_init();
   system_enable_interrupts(true);

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
   storage_begin_reading(0, 0);
   uint32_t bytes_read = storage_retrieve_next_data_chunk(read_data);
   while (bytes_read)
   {
      print("Read %u bytes\n", bytes_read);
      bytes_read = storage_retrieve_next_data_chunk(read_data);
   }
   print("Reading complete\n");

   // Test storing a new set of experiment details
   experiment_details_t details = {
      .experiment_start_time = 1, .experiment_end_time = 2,
      .daily_start_time = 3, .daily_end_time = 4,
      .num_devices = 3, .uids = {}, .uid_name_mappings = {},
      .is_terminated = 0
   };
   for (uint8_t i = 0; i < details.num_devices; ++i)
   {
      const char name_index[2] = { '0' + i, 0 };
      uint8_t uid[] = { 0xc0, 0x98, 0x01, 0x02, 0x68, i+1 };
      memcpy(details.uids[i], uid, EUI_LEN);
      strcpy(details.uid_name_mappings[i], "Test Name ");
      strcat(details.uid_name_mappings[i], name_index);
   }
   storage_store_experiment_details(&details);

   // Test retrieving the set of experiment details
   memset(&details, 0, sizeof(details));
   storage_retrieve_experiment_details(&details);
   print("Experiment Details:\n");
   print("Start/End Times: %u, %u\n", details.experiment_start_time, details.experiment_end_time);
   print("Daily Start/End Times: %u, %u\n", details.daily_start_time, details.daily_end_time);
   print("Num Devices: %u\n", (uint32_t)details.num_devices);
   for (uint8_t i = 0; i < details.num_devices; ++i)
      print("UID and Mapping: %02X:%02X:%02X:%02X:%02X:%02X = %s\n", details.uids[i][0], details.uids[i][1], details.uids[i][2], details.uids[i][3], details.uids[i][4], details.uids[i][5], details.uid_name_mappings[i]);

   // Done with test, loop forever
   while (true)
      am_hal_delay_us(1000000);

   // Should never reach this point
   return 0;
}
