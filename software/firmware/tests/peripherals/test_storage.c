#include "storage.h"
#include "storage_records.h"
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

// One record sized to exactly fill a page, flushed immediately, so page index == record index and the
// existing per-page assertions still hold under the record-framed format
#define TEST_RECORD_DATA_BYTES   (MEMORY_NUM_DATA_BYTES_PER_PAGE - 5)

static void write_tagged_page(uint32_t index)
{
   memset(page_buffer, (uint8_t)index, TEST_RECORD_DATA_BYTES);
   memcpy(page_buffer, "PAGE", 4);
   memcpy(page_buffer + 4, &index, sizeof(index));
   storage_store_record(STORAGE_TYPE_IMU, 500 * index, page_buffer, TEST_RECORD_DATA_BYTES);
   storage_flush(true);
}

// Checks a chunk already read into verify_buffer. A chunk is now a framed record:
// [type:1][timestamp:4][data:TEST_RECORD_DATA_BYTES]
static bool verify_tagged_page(uint32_t expected_index, uint32_t length)
{
   if (!length)
   {
      print("  ERROR: page %u returned 0 bytes (lost, or failed CRC)\n", expected_index);
      return false;
   }
   if (length != MEMORY_NUM_DATA_BYTES_PER_PAGE)
   {
      print("  ERROR: page %u length %u, expected %u\n", expected_index, length, (uint32_t)MEMORY_NUM_DATA_BYTES_PER_PAGE);
      return false;
   }
   if (verify_buffer[0] != STORAGE_TYPE_IMU)
   {
      print("  ERROR: page %u record type %u, expected %u\n", expected_index, verify_buffer[0], (uint32_t)STORAGE_TYPE_IMU);
      return false;
   }
   uint32_t timestamp = 0;
   memcpy(&timestamp, verify_buffer + 1, sizeof(timestamp));
   if (timestamp != (500 * expected_index))
   {
      print("  ERROR: page %u timestamp %u, expected %u\n", expected_index, timestamp, 500 * expected_index);
      return false;
   }
   const uint8_t *record = verify_buffer + 5;
   uint32_t page_index = 0;
   memcpy(&page_index, record + 4, sizeof(page_index));
   if (memcmp(record, "PAGE", 4) || (page_index != expected_index))
   {
      print("  ERROR: page %u carries index %u (corrupt or out of order)\n", expected_index, page_index);
      return false;
   }
   for (uint32_t i = 8; i < TEST_RECORD_DATA_BYTES; ++i)
      if (record[i] != (uint8_t)expected_index)
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
   const uint32_t num_chunks = storage_retrieve_num_data_chunks();
   print("Read-back: device reports %u chunks for %u written pages (expect %u)\n",
         num_chunks, (uint32_t)BLOCK_CROSSING_NUM_PAGES, (uint32_t)BLOCK_CROSSING_NUM_PAGES);

   // Every reported chunk is a stored page: an empty in-RAM buffer is no longer announced as one
   uint32_t errors = 0, verified = 0;
   for (uint32_t chunk = 0; chunk < num_chunks; ++chunk)
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


// Partial-Page Flush Test ---------------------------------------------------------------------------------------------
//
// Covers the Phase 2 change to storage_flush(true). Before Phase 2 a partial page was written WITHOUT
// advancing the write head or clearing the cache, which was safe only because the single caller
// (STORAGE_TYPE_SHUTDOWN) immediately reset the device. Now that a timer can fire the same path
// mid-deployment, failing to advance would re-program a NAND page that has already been programmed --
// exactly the corruption class this work exists to eliminate.
//
// The test therefore writes full pages, forces a partial page, then writes more full pages, and verifies
// that everything reads back intact and in order. A head that failed to advance shows up as a short page
// count, a corrupt page, or both.

#define PARTIAL_TEST_FULL_PAGES        3
#define PARTIAL_TEST_PARTIAL_BYTES     100

static void test_partial_page_flush(void)
{
   print("\n=== Partial-page flush test ===\n");

   storage_enter_maintenance_mode();
   reset_log_to_known_state();
   storage_exit_maintenance_mode();
   storage_disable(false);

   uint32_t errors = 0;

   // Full pages before the partial
   for (uint32_t page = 0; page < PARTIAL_TEST_FULL_PAGES; ++page)
      write_tagged_page(page);
   if (storage_has_buffered_data())
   {
      print("  ERROR: cache should be empty after writing whole pages\n");
      ++errors;
   }

   // Force a partial page
   memset(page_buffer, 0xA5, PARTIAL_TEST_PARTIAL_BYTES);
   memcpy(page_buffer, "PART", 4);
   storage_store_record(STORAGE_TYPE_MOTION, 12345, page_buffer, PARTIAL_TEST_PARTIAL_BYTES);
   if (!storage_has_buffered_data())
   {
      print("  ERROR: cache should report buffered data after a store\n");
      ++errors;
   }
   storage_flush(true);
   if (storage_has_buffered_data())
   {
      print("  ERROR: cache should be empty after a partial flush\n");
      ++errors;
   }
   print("  Wrote %u full pages, then a %u-byte partial page\n",
         (uint32_t)PARTIAL_TEST_FULL_PAGES, (uint32_t)PARTIAL_TEST_PARTIAL_BYTES);

   // Full pages after the partial. If the head did not advance, these re-program an already-programmed
   // page rather than landing on a fresh one.
   for (uint32_t page = 0; page < PARTIAL_TEST_FULL_PAGES; ++page)
      write_tagged_page(PARTIAL_TEST_FULL_PAGES + page);

   // Read back: N full pages, one partial, N full pages, then the empty in-RAM cache
   storage_enter_maintenance_mode();
   storage_begin_reading(0, 0);
   const uint32_t num_chunks = storage_retrieve_num_data_chunks();
   const uint32_t expected_chunks = (2 * PARTIAL_TEST_FULL_PAGES) + 1;
   if (num_chunks != expected_chunks)
   {
      print("  ERROR: %u chunks reported, expected %u (a missing page means the head did not advance)\n",
            num_chunks, expected_chunks);
      ++errors;
   }

   for (uint32_t chunk = 0; chunk < num_chunks; ++chunk)
   {
      const uint32_t length = storage_retrieve_next_data_chunk(verify_buffer);
      if (chunk == PARTIAL_TEST_FULL_PAGES)
      {
         // The partial page: verify it kept exactly the bytes that were buffered, no more and no less
         if ((length != (PARTIAL_TEST_PARTIAL_BYTES + 5)) || memcmp(verify_buffer + 5, "PART", 4))
         {
            print("  ERROR: partial page came back as %u bytes, expected %u\n", length, (uint32_t)PARTIAL_TEST_PARTIAL_BYTES + 5);
            ++errors;
         }
         else
            for (uint32_t i = 4; i < PARTIAL_TEST_PARTIAL_BYTES; ++i)
               if (verify_buffer[5 + i] != 0xA5)
               {
                  print("  ERROR: partial page body mismatch at offset %u\n", i);
                  ++errors;
                  break;
               }
      }
      else
      {
         // Full pages keep their original tag index, which skips over the partial page's slot
         const uint32_t expected_index = (chunk < PARTIAL_TEST_FULL_PAGES) ? chunk : (chunk - 1);
         if (!verify_tagged_page(expected_index, length))
            ++errors;
      }
   }
   storage_end_reading();
   storage_exit_maintenance_mode();

   print("=== Partial-page flush test %s: %u errors ===\n\n", errors ? "FAILED" : "PASSED", errors);
}


// Time-Range Seek Test ------------------------------------------------------------------------------------------------
//
// Covers the Phase 4 replacement of the timestamp search. The old implementation scanned payload bytes
// hunting for something that looked like a voltage record; it could not tell a real record from ordinary
// payload that happened to match, so it could select the wrong page and silently drop or duplicate a span
// of the log. It is now a binary search over the per-page header time bounds, which is exact.
//
// Every other test calls storage_begin_reading(0, 0), which skips the seek entirely, so without this the
// new code path would ship unexercised.

#define SEEK_TEST_NUM_PAGES     40
#define SEEK_TEST_SEEK_SECONDS  13    // absolute; the experiment starts at 1, so this is 12000 ms relative

static void test_time_range_seek(void)
{
   print("\n=== Time-range seek test ===\n");

   storage_enter_maintenance_mode();
   reset_log_to_known_state();
   storage_exit_maintenance_mode();
   storage_disable(false);

   // Page N carries exactly one record stamped 500*N ms after the experiment start
   for (uint32_t page = 0; page < SEEK_TEST_NUM_PAGES; ++page)
      write_tagged_page(page);

   // storage_begin_reading() takes the same experiment-relative milliseconds the page headers carry; the
   // conversion from a wall clock now happens in the caller, since only the caller knows the epoch. The
   // seek time is still derived from whole seconds so this test keeps exercising the boundary an
   // application would actually ask for, rather than one that happens to land on a page edge.
   const uint32_t seek_timestamp = SEEK_TEST_SEEK_SECONDS;
   const uint32_t target_relative_ms = 1000 * (seek_timestamp - 1);
   const uint32_t target_page = target_relative_ms / 500;

   storage_enter_maintenance_mode();
   storage_begin_reading(target_relative_ms, 0);
   const uint32_t num_chunks = storage_retrieve_num_data_chunks();
   const uint32_t length = storage_retrieve_next_data_chunk(verify_buffer);

   uint32_t first_index = 0xFFFFFFFF, first_timestamp = 0;
   if (length >= 9)
   {
      memcpy(&first_timestamp, verify_buffer + 1, sizeof(first_timestamp));
      memcpy(&first_index, verify_buffer + 5 + 4, sizeof(first_index));
   }
   storage_end_reading();
   storage_exit_maintenance_mode();

   // Seeking to time T must land on the page holding T, not before it and not past it
   const uint32_t expected_chunks = SEEK_TEST_NUM_PAGES - target_page;
   const bool passed = (first_index == target_page) && (first_timestamp == target_relative_ms) &&
                       (num_chunks == expected_chunks);
   print("  Sought %u ms; first page returned index %u (ts %u ms), expected index %u (ts %u ms)\n",
         target_relative_ms, first_index, first_timestamp, target_page, target_relative_ms);
   print("  Remaining chunks from that point: %u (expected %u)\n", num_chunks, expected_chunks);
   print("=== Time-range seek test %s ===\n\n", passed ? "PASSED" : "FAILED");
}


// Page Retransmission Test --------------------------------------------------------------------------------------------
//
// Covers storage_retrieve_page_by_seq(), the primitive the host will use to ask for pages that arrived
// corrupt or not at all. It binary-searches the epoch by sequence number, so it must return the right page
// regardless of request order, and must return nothing for a sequence that was never written rather than
// the nearest match -- handing back the wrong page would be worse than reporting the gap.

#define RETRANSMIT_TEST_PAGES   40

#define JUMP_TEST_RECORD_BYTES  16
#define RECOVERY_TEST_RECORDS   120

static void test_timestamp_jump(void)
{
   // The ranging offset that converts local time to network time resets on reboot and is only restored by
   // the next ranging round, so records written in between can step BACKWARDS relative to the ones already
   // in the page. A page whose last_timestamp precedes its first cannot be interpreted by the time-range
   // seek at all, so such a record has to start a new page.
   print("\n=== Timestamp jump test ===\n");

   storage_enter_maintenance_mode();
   reset_log_to_known_state();
   storage_exit_maintenance_mode();
   storage_disable(false);

   // Four small records in what would otherwise be a single page, with a backwards step in the middle
   uint8_t record[JUMP_TEST_RECORD_BYTES];
   memset(record, 0xA5, sizeof(record));
   storage_store_record(STORAGE_TYPE_IMU, 10000, record, sizeof(record));
   storage_store_record(STORAGE_TYPE_IMU, 11000, record, sizeof(record));
   storage_store_record(STORAGE_TYPE_IMU,  3000, record, sizeof(record));   // <-- the jump
   storage_store_record(STORAGE_TYPE_IMU,  4000, record, sizeof(record));
   storage_flush(true);

   // A step smaller than STORAGE_TIMESTAMP_TOLERANCE_MS is writer disagreement, not a moved time base, so it
   // must be clamped forward rather than allowed to commit a page. These three become ONE page if the
   // tolerance is applied and TWO if it is not, so the total page count is the assertion
   storage_store_record(STORAGE_TYPE_IMU, 20000, record, sizeof(record));
   storage_store_record(STORAGE_TYPE_IMU, 20000 - (STORAGE_TIMESTAMP_TOLERANCE_MS / 2), record, sizeof(record));
   storage_store_record(STORAGE_TYPE_IMU, 20100, record, sizeof(record));
   storage_flush(true);

   storage_enter_maintenance_mode();
   storage_begin_reading(0, 0);
   const uint32_t num_chunks = storage_retrieve_num_data_chunks();

   uint32_t errors = 0;
   // Pages 0-1 are the deliberate jump. Page 2 is the tolerance check: a THIRD page means the small step
   // was clamped, a fourth would mean it split the page as a real re-basing would
   const uint32_t expected_first[] = { 10000, 3000, 20000 }, expected_last[] = { 11000, 4000, 20100 };
   if (num_chunks != 3)
   {
      print("  ERROR: %u pages, expected 3 (%s)\n", num_chunks,
            (num_chunks == 4) ? "a sub-tolerance step split a page" : "the jump did not split the page");
      ++errors;
   }
   for (uint32_t i = 0; i < num_chunks; ++i)
   {
      storage_page_header_t header;
      const uint32_t length = storage_retrieve_next_page(verify_buffer, &header);
      print("  page %u: first=%u last=%u records=%u bytes=%u\n",
            i, header.first_timestamp, header.last_timestamp, header.record_count, length);
      if (header.first_timestamp > header.last_timestamp)
      {
         print("  ERROR: page %u is inverted (first > last)\n", i);
         ++errors;
      }
      if ((i < 2) && ((header.first_timestamp != expected_first[i]) || (header.last_timestamp != expected_last[i])))
      {
         print("  ERROR: page %u should span %u..%u\n", i, expected_first[i], expected_last[i]);
         ++errors;
      }
      if ((i < 2) && (header.record_count != 2))
      {
         print("  ERROR: page %u holds %u records, expected 2\n", i, header.record_count);
         ++errors;
      }
   }
   storage_end_reading();
   storage_exit_maintenance_mode();

   if (errors)
      print("=== Timestamp jump test FAILED: %u errors ===\n", errors);
   else
      print("=== Timestamp jump test PASSED: every page spans a forward time range ===\n");
}

static void store_range_record(uint32_t timestamp, uint8_t range_mm_div)
{
   // [count:1][uid:1][range:2] -- one peer, which is what a two-device deployment produces
   uint8_t data[1 + COMPRESSED_RANGE_DATUM_LENGTH] = { 1, 0x42, range_mm_div, 0 };
   storage_store_record(STORAGE_TYPE_RANGES, timestamp, data, sizeof(data));
}

static void store_time_anchor(uint32_t network_ms, uint32_t local_ms)
{
   // The payload is the device's own un-offset clock; the record timestamp is the same instant on the
   // network clock. Their difference is the offset, which is what recovery reads back
   storage_store_record(STORAGE_TYPE_TIME_ANCHOR, network_ms, &local_ms, sizeof(local_ms));
}

static void test_time_anchor_recovery(void)
{
   print("\n=== Time anchor recovery test ===\n");

   storage_enter_maintenance_mode();
   reset_log_to_known_state();
   storage_exit_maintenance_mode();
   storage_disable(false);

   uint32_t errors = 0, experiment_ms = 0, rtc = 0;

   // Nothing written yet: there is no network base to recover, and inventing one would be worse than
   // leaving the offset at zero
   if (recover_time_anchor(&experiment_ms, &rtc))
   {
      print("  ERROR: reported an anchor for an empty log\n");
      ++errors;
   }

   // A log holding no anchors must report nothing rather than fall back to some other record. Seeding from
   // an ordinary timestamp is exactly the bug the anchor replaced: it assumes the log ends at the present
   // moment, and up to STORAGE_FLUSH_TIMEOUT_S of records are still in the page cache at any reboot
   uint8_t voltage[4] = { 0x70, 0x10, 0, 0 };
   storage_store_record(STORAGE_TYPE_VOLTAGE, 1000, voltage, sizeof(voltage));
   for (uint32_t i = 0; i < RECOVERY_TEST_RECORDS; ++i)
      store_range_record(2000 + (500 * i), (uint8_t)(i & 0xFF));
   storage_flush(true);
   if (recover_time_anchor(&experiment_ms, &rtc))
   {
      print("  ERROR: recovered an anchor from a log holding none\n");
      ++errors;
   }

   // Now a spread of anchors across several pages, newest last
   uint32_t expected_ms = 0, expected_rtc = 0;
   for (uint32_t i = 0; i < RECOVERY_TEST_RECORDS; ++i)
   {
      expected_ms = 100000 + (500 * i);
      expected_rtc = 1700000000u + i;
      store_time_anchor(expected_ms, expected_rtc);
      store_range_record(expected_ms, (uint8_t)(i & 0xFF));
      if ((i % 20) == 19)
         storage_flush(true);
   }
   storage_flush(true);

   if (!recover_time_anchor(&experiment_ms, &rtc))
   {
      print("  ERROR: found no anchor in a log full of them\n");
      ++errors;
   }
   else if ((experiment_ms != expected_ms) || (rtc != expected_rtc))
   {
      print("  ERROR: recovered (%u ms, rtc %u), expected (%u ms, rtc %u)\n",
            experiment_ms, rtc, expected_ms, expected_rtc);
      ++errors;
   }
   else
      print("  recovered the newest anchor: %u ms paired with rtc %u\n", experiment_ms, rtc);

   // Later non-anchor records must not displace the answer. The whole point is that an anchor stays valid
   // however stale it gets, so records written after it change nothing
   storage_store_record(STORAGE_TYPE_VOLTAGE, expected_ms + 5000, voltage, sizeof(voltage));
   store_range_record(expected_ms + 5500, 0);
   storage_flush(true);
   if (!recover_time_anchor(&experiment_ms, &rtc) ||
       (experiment_ms != expected_ms) || (rtc != expected_rtc))
   {
      print("  ERROR: trailing records changed the answer to (%u, %u)\n", experiment_ms, rtc);
      ++errors;
   }
   else
      print("  trailing records correctly left the anchor at %u ms / rtc %u\n", experiment_ms, rtc);

   // The recovered offset must not depend on WHEN it is read back -- that is the entire property being
   // bought. Two reads separated by more records must agree exactly.
   uint32_t second_ms = 0, second_rtc = 0;
   for (uint32_t i = 0; i < 40; ++i)
      store_range_record(expected_ms + 6000 + (500 * i), (uint8_t)i);
   storage_flush(true);
   if (!recover_time_anchor(&second_ms, &second_rtc) ||
       (second_ms != experiment_ms) || (second_rtc != rtc))
   {
      print("  ERROR: a later read returned (%u, %u) instead of (%u, %u)\n",
            second_ms, second_rtc, experiment_ms, rtc);
      ++errors;
   }
   else
      print("  a later read returned the same anchor, so staleness does not shift the offset\n");

   storage_disable(true);
   if (errors)
      print("=== Time anchor recovery FAILED: %u errors ===\n", errors);
   else
      print("=== Time anchor recovery PASSED ===\n");
}

static void test_page_retransmission(void)
{
   print("\n=== Page retransmission test ===\n");

   storage_enter_maintenance_mode();
   reset_log_to_known_state();
   storage_exit_maintenance_mode();
   storage_disable(false);
   for (uint32_t page = 0; page < RETRANSMIT_TEST_PAGES; ++page)
      write_tagged_page(page);

   storage_enter_maintenance_mode();
   storage_begin_reading(0, 0);
   storage_retrieve_num_data_chunks();        // establishes the readable range

   // Deliberately out of order, including both ends, to prove the search does not depend on locality
   const uint32_t wanted[] = { 37, 0, 19, 5, RETRANSMIT_TEST_PAGES - 1, 1, 20 };
   uint32_t errors = 0;
   for (uint32_t i = 0; i < (sizeof(wanted) / sizeof(wanted[0])); ++i)
   {
      storage_page_header_t header;
      const uint32_t length = storage_retrieve_page_by_seq(wanted[i], verify_buffer, &header);
      if (!length)
      {
         print("  ERROR: seq %u returned nothing\n", wanted[i]);
         ++errors;
      }
      else if (header.seq != wanted[i])
      {
         print("  ERROR: asked for seq %u, got seq %u\n", wanted[i], header.seq);
         ++errors;
      }
      else if (!verify_tagged_page(wanted[i], length))
         ++errors;
   }
   print("  Requested %u pages out of order\n", (uint32_t)(sizeof(wanted) / sizeof(wanted[0])));

   // A sequence number that was never written must yield nothing, not the closest page
   storage_page_header_t header;
   if (storage_retrieve_page_by_seq(RETRANSMIT_TEST_PAGES + 500, verify_buffer, &header))
   {
      print("  ERROR: a sequence number that was never written returned data\n");
      ++errors;
   }

   // Requesting by sequence must not disturb the sequential read in progress
   const uint32_t length = storage_retrieve_next_data_chunk(verify_buffer);
   if (!verify_tagged_page(0, length))
   {
      print("  ERROR: sequential read was disturbed by a retransmission request\n");
      ++errors;
   }

   storage_end_reading();
   storage_exit_maintenance_mode();
   print("=== Page retransmission test %s: %u errors ===\n\n", errors ? "FAILED" : "PASSED", errors);
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
   const uint32_t num_chunks = storage_retrieve_num_data_chunks();
   const uint32_t existing_pages = num_chunks;   // no trailing cache chunk when the buffer is empty

   uint32_t verified = 0, errors = 0;
   bool foreign = false;
   for (uint32_t chunk = 0; chunk < existing_pages; ++chunk)
   {
      const uint32_t length = storage_retrieve_next_data_chunk(verify_buffer);

      // Anything that is not this test's data (a previous test, a real deployment) is discarded rather
      // than reported as corruption. The tag sits at offset 5 because a chunk is a framed record:
      // [type:1][timestamp:4][data...] -- offset 0 holds the record type, never the tag.
      if (!chunk && ((length != MEMORY_NUM_DATA_BYTES_PER_PAGE) ||
                     (verify_buffer[0] != STORAGE_TYPE_IMU) || memcmp(verify_buffer + 5, "PAGE", 4)))
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
      // Shout when discarding data. A wipe-and-continue reports a clean round while verifying nothing,
      // which is indistinguishable from success unless the running page total is watched -- exactly how a
      // stale offset assumption in this test hid itself for three consecutive "passing" rounds.
      if (foreign)
         print("*** WARNING: existing log is NOT from this test -- WIPING. This round verifies NOTHING. ***\n"
               "*** If this repeats every reboot, the test is self-healing and not actually testing.   ***\n");
      else
         print("Log is empty; starting round 1\n");
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

#ifdef _TEST_STORAGE_RESET_BBM

   // One-shot recovery: clear a bad-block table polluted by a firmware fault that retired good blocks.
   // Run this once, then reflash a normal test build.
   print("\n=== Bad-block table reset ===\n");
   storage_enter_maintenance_mode();
   storage_reset_bad_block_table();
   storage_exit_maintenance_mode();
   print("=== Reset complete -- power-cycle, then flash a normal build ===\n");
   while (true)
      am_hal_delay_us(1000000);

#endif

#ifdef _TEST_STORAGE_REBOOT

   // Verify prior rounds survived, append another, then wait for a manual reset
   test_reboot_survival();
   while (true)
      am_hal_delay_us(1000000);

#else

   // Drive the write head across several block boundaries to exercise erase-ahead
   test_forced_block_crossings();

   // Verify that a partial page advances the write head rather than re-programming it
   test_partial_page_flush();

   // Verify that seeking to a timestamp lands on the exact page holding it
   test_time_range_seek();

   // Verify that a specific page can be fetched by sequence number, for retransmission
   test_timestamp_jump();
   test_time_anchor_recovery();
   test_page_retransmission();

#endif

   // Test storing and retrieving a set of experiment details
   print("\n=== Experiment details round-trip test ===\n");
   storage_enter_maintenance_mode();

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
   const experiment_details_t written = details;
   storage_store_experiment_details(&details);

   // Retrieve and compare against what was written
   memset(&details, 0, sizeof(details));
   storage_retrieve_experiment_details(&details);
   storage_exit_maintenance_mode();

   print("Start/End Times: %u, %u\n", details.experiment_start_time, details.experiment_end_time);
   print("Daily Start/End Times: %u, %u\n", details.daily_start_time, details.daily_end_time);
   print("Num Devices: %u\n", (uint32_t)details.num_devices);
   for (uint8_t i = 0; i < details.num_devices; ++i)
      print("UID and Mapping: %02X:%02X:%02X:%02X:%02X:%02X = %s\n", details.uids[i][0], details.uids[i][1], details.uids[i][2], details.uids[i][3], details.uids[i][4], details.uids[i][5], details.uid_name_mappings[i]);

   const bool details_match = (memcmp(&written, &details, sizeof(details)) == 0);
   print("=== Experiment details round-trip %s ===\n\n", details_match ? "PASSED" : "FAILED");

   // Done with test, loop forever
   while (true)
      am_hal_delay_us(1000000);

   // Should never reach this point
   return 0;
}
