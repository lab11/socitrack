#include "storage.h"
#include "system.h"
#include "logging.h"


// Forced Block-Crossing Test ------------------------------------------------------------------------------------------
//
// Drives the write head across several block boundaries in a tight loop so that erase_ahead_of_head() is
// exercised many times in seconds. A real two-tag deployment writes a page roughly every 97 s, so it needs
// ~1.75 hours per block crossing -- this covers the same ground in well under a minute.
//
// Every page is tagged with its own index and filled with an index-derived byte pattern, so a lost,
// truncated, reordered, or partially-erased page is detected on read-back rather than passing silently.

#define BLOCK_CROSSING_NUM_PAGES    ((3 * MEMORY_PAGES_PER_BLOCK) + 8)

static uint8_t page_buffer[MEMORY_NUM_DATA_BYTES_PER_PAGE];
static uint8_t verify_buffer[MEMORY_PAGE_SIZE_BYTES];

// Writes one full page tagged with its own index, so the log is self-describing on read-back
static void write_tagged_page(uint32_t index)
{
   memset(page_buffer, (uint8_t)index, sizeof(page_buffer));
   memcpy(page_buffer, "PAGE", 4);
   memcpy(page_buffer + 4, &index, sizeof(index));
   storage_store(page_buffer, sizeof(page_buffer));
   storage_flush(false);
}

// Checks a chunk already read into verify_buffer against its expected index
static bool verify_tagged_page(uint32_t expected_index, uint32_t length)
{
   if (!length)
   {
      print("  ERROR: page %u returned 0 bytes (lost)\n", expected_index);
      return false;
   }
   if (length != MEMORY_NUM_DATA_BYTES_PER_PAGE)
   {
      print("  ERROR: page %u length %u, expected %u\n", expected_index, length, (uint32_t)MEMORY_NUM_DATA_BYTES_PER_PAGE);
      return false;
   }
   uint32_t page_index = 0;
   memcpy(&page_index, verify_buffer + 4, sizeof(page_index));
   if (memcmp(verify_buffer, "PAGE", 4) || (page_index != expected_index))
   {
      print("  ERROR: page %u carries index %u (corrupt or out of order)\n", expected_index, page_index);
      return false;
   }
   for (uint32_t i = 8; i < MEMORY_NUM_DATA_BYTES_PER_PAGE; ++i)
      if (verify_buffer[i] != (uint8_t)expected_index)
      {
         print("  ERROR: page %u body mismatch at offset %u\n", expected_index, i);
         return false;
      }
   return true;
}

// Wipes the log and repositions the write head to a known state. Storing experiment details also calls
// storage_disable() based on RTC validity, and the RTC is typically unset in a bare test, so writes must
// be re-enabled explicitly afterwards. Must be called while in maintenance mode.
static void reset_log_to_known_state(void)
{
   experiment_details_t details = {
      .experiment_start_time = 1, .experiment_end_time = 0xFFFFFFFF,
      .daily_start_time = 0, .daily_end_time = 0, .use_daily_times = 0,
      .num_devices = 1, .uids = {}, .uid_name_mappings = {}, .is_terminated = 0
   };
   storage_store_experiment_details(&details);
}

#ifndef _TEST_STORAGE_REBOOT

static void test_forced_block_crossings(void)
{
   print("\n=== Forced block-crossing test ===\n");
   print("Writing %u pages of %u bytes (%u pages/block, %u boundaries crossed)\n",
         (uint32_t)BLOCK_CROSSING_NUM_PAGES, (uint32_t)MEMORY_NUM_DATA_BYTES_PER_PAGE,
         (uint32_t)MEMORY_PAGES_PER_BLOCK, (uint32_t)(BLOCK_CROSSING_NUM_PAGES / MEMORY_PAGES_PER_BLOCK));

   // Reset the log so page indices start from zero
   storage_enter_maintenance_mode();
   reset_log_to_known_state();
   storage_exit_maintenance_mode();
   storage_disable(false);

   // Write one full page per iteration
   for (uint32_t page = 0; page < BLOCK_CROSSING_NUM_PAGES; ++page)
   {
      write_tagged_page(page);
      if (((page + 1) % MEMORY_PAGES_PER_BLOCK) == 0)
         print("  %u pages written (crossed a block boundary)\n", page + 1);
   }
   print("Write phase complete\n");

   // Read every page back and verify content, ordering, and completeness
   storage_enter_maintenance_mode();
   storage_begin_reading(0, 0);
   const uint32_t num_chunks = storage_retrieve_num_data_chunks(0);
   print("Read-back: device reports %u chunks for %u written pages (expect %u)\n",
         num_chunks, (uint32_t)BLOCK_CROSSING_NUM_PAGES, (uint32_t)BLOCK_CROSSING_NUM_PAGES + 1);

   // The final chunk is the in-RAM cache, which is empty here, so only the first num_chunks-1 are pages
   uint32_t errors = 0, verified = 0;
   for (uint32_t chunk = 0; (chunk + 1) < num_chunks; ++chunk)
   {
      const uint32_t length = storage_retrieve_next_data_chunk(verify_buffer);
      if (verify_tagged_page(chunk, length))
         ++verified;
      else
         ++errors;
   }
   storage_end_reading();
   storage_exit_maintenance_mode();

   const bool passed = !errors && (verified == BLOCK_CROSSING_NUM_PAGES);
   print("=== Block-crossing test %s: %u/%u pages verified, %u errors ===\n\n",
         passed ? "PASSED" : "FAILED", verified, (uint32_t)BLOCK_CROSSING_NUM_PAGES, errors);
}

#endif  // #ifndef _TEST_STORAGE_REBOOT

// Reboot-Survival Test ------------------------------------------------------------------------------------------------
//
// Probes the one Phase 0 path that can destroy data: storage_init() recovers the write head heuristically,
// then erase_ahead_of() erases blocks past it. If recovery under-reports the head, live data is erased.
//
// Needs no state outside the flash: every page carries its own index, so each boot simply counts what is
// already there, verifies it, and appends another round. Run it repeatedly to cover repeated reboots.
//
//   1. make storage_reboot BOARD_REV=P
//   2. Wait for "HARD-RESET THE DEVICE NOW", then pull power or press reset
//   3. Read the ROUND RESULT line, which reports whether every prior page survived
//   4. Repeat step 2 as many times as desired -- each round appends and re-verifies everything

#ifdef _TEST_STORAGE_REBOOT

#define REBOOT_TEST_PAGES_PER_ROUND    (MEMORY_PAGES_PER_BLOCK + 8)

static void test_reboot_survival(void)
{
   print("\n=== Reboot-survival test ===\n");

   // Count and verify whatever survived previous rounds
   storage_enter_maintenance_mode();
   storage_begin_reading(0, 0);
   const uint32_t num_chunks = storage_retrieve_num_data_chunks(0);
   const uint32_t existing_pages = num_chunks ? (num_chunks - 1) : 0;

   uint32_t verified = 0, errors = 0;
   bool foreign = false;
   for (uint32_t chunk = 0; chunk < existing_pages; ++chunk)
   {
      const uint32_t length = storage_retrieve_next_data_chunk(verify_buffer);

      // Anything that is not this test's data (a previous test, a real deployment) is discarded rather
      // than reported as corruption
      if (!chunk && ((length != MEMORY_NUM_DATA_BYTES_PER_PAGE) || memcmp(verify_buffer, "PAGE", 4)))
      {
         foreign = true;
         break;
      }
      if (verify_tagged_page(chunk, length))
         ++verified;
      else
         ++errors;
   }
   storage_end_reading();

   // Report the result of the previous round, then decide where this round starts
   uint32_t base_index;
   if (foreign || !existing_pages)
   {
      print(foreign ? "Existing log is not from this test; wiping and starting round 1\n"
                    : "Log is empty; starting round 1\n");
      reset_log_to_known_state();
      base_index = 0;
   }
   else
   {
      const bool passed = !errors && (verified == existing_pages);
      print("ROUND RESULT [%s]: %u/%u pages survived the reboot, %u errors\n",
            passed ? "PASSED" : "FAILED", verified, existing_pages, errors);
      base_index = existing_pages;
   }
   storage_exit_maintenance_mode();
   storage_disable(false);

   // Append another round, continuing the index sequence
   for (uint32_t i = 0; i < REBOOT_TEST_PAGES_PER_ROUND; ++i)
      write_tagged_page(base_index + i);
   print("Appended %u pages (total now %u)\n",
         (uint32_t)REBOOT_TEST_PAGES_PER_ROUND, base_index + (uint32_t)REBOOT_TEST_PAGES_PER_ROUND);

   print("\n>>> HARD-RESET THE DEVICE NOW (pull power or press reset) <<<\n");
   print(">>> On reboot, every page above must still verify <<<\n\n");
}

#endif  // #ifdef _TEST_STORAGE_REBOOT

int main(void)
{
   // Set up system hardware
   setup_hardware();
   storage_init();
   system_enable_interrupts(true);

#ifdef _TEST_STORAGE_REBOOT

   // Verify prior rounds survived, append another, then wait for a manual reset
   test_reboot_survival();
   while (true)
      am_hal_delay_us(1000000);

#else

   // Drive the write head across several block boundaries to exercise erase-ahead
   test_forced_block_crossings();

#endif

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
