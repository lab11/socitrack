// Host-side tests for the nandlog core, run against the RAM-backed port.
//
// These are not a replacement for the on-device suite: they cannot catch anything about the real part's
// timing, its ECC, or the board. What they do catch is everything above the SPI wire -- and they catch it in
// milliseconds, and they can inject faults that are impractical to stage on hardware.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nandlog.h"
#include "nandlog_chip.h"
#include "nandlog_port_sim.h"

static uint32_t tests_run, tests_failed;

#define CHECK(cond, ...)  do {                                              \
      ++tests_run;                                                          \
      if (!(cond)) { ++tests_failed; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
   } while (0)

static const uint8_t DEVICE_ID[3] = { 0x8D, 0x00, 0x00 };

static uint8_t payload[1024], readback[8192];

static void fresh_log(void)
{
   nandlog_sim_create(DEVICE_ID, sizeof(DEVICE_ID));
   if (!nandlog_init())
   {
      printf("  FATAL: nandlog_init() failed\n");
      exit(1);
   }
   nandlog_begin_session();
   const uint8_t metadata[16] = "sim-metadata";
   nandlog_begin_epoch(metadata, sizeof(metadata));
   nandlog_end_session();
}

static void write_records(uint32_t count, uint32_t first_timestamp)
{
   for (uint32_t i = 0; i < count; ++i)
   {
      memset(payload, (uint8_t)(i | 0x80), sizeof(payload));
      memcpy(payload, &i, sizeof(i));
      nandlog_store_record(7, first_timestamp + (100 * i), payload, sizeof(payload));
   }
   nandlog_flush(true);
}

static uint32_t read_all_pages(void)
{
   uint32_t pages = 0, total = 0;
   nandlog_begin_session();
   nandlog_begin_reading(0, 0);
   nandlog_read_span(&pages, &total);
   uint32_t seen = 0, bytes = 0;
   for (uint32_t i = 0; i < pages; ++i)
   {
      nandlog_page_header_t header;
      const uint32_t length = nandlog_retrieve_next_page(readback, &header);
      if (length)
         { ++seen; bytes += length; }
   }
   nandlog_end_reading();
   nandlog_end_session();
   CHECK(total == bytes, "byte total %u disagrees with what the pages delivered, %u", total, bytes);
   return seen;
}


// Tests ----------------------------------------------------------------------------------------------------------------

static void test_roundtrip(void)
{
   printf("Round trip\n");
   fresh_log();
   write_records(64, 1000);
   const uint32_t pages = read_all_pages();
   CHECK(pages > 0, "no pages came back");
   nandlog_deinit();
}

static void test_a_record_can_exactly_fill_a_page(void)
{
   printf("A record sized to a page fills exactly one page\n");
   const uint32_t overhead = (NANDLOG_RECORD_FRAMING ? NANDLOG_FRAMING_LENGTH_BYTES : 0) + 5;
   static uint8_t big[NANDLOG_MAX_DATA_BYTES_PER_PAGE];
   memset(big, 0x5A, sizeof(big));

   fresh_log();
   const uint32_t capacity = nandlog_data_bytes_per_page();
   nandlog_store_record(7, 1000, big, capacity - overhead);
   nandlog_flush(true);
   CHECK(read_all_pages() == 1, "a record sized to fill a page did not produce exactly one page");
   nandlog_deinit();

   // One byte more than fits has nowhere to go: records are never split, so there is no partial page to find either
   fresh_log();
   nandlog_store_record(7, 1000, big, capacity - overhead + 1);
   nandlog_flush(true);
   CHECK(read_all_pages() == 0, "a record one byte too large for a page was stored anyway");
   nandlog_deinit();
}

static void test_image_dump_for_the_parser(void)
{
   printf("Dumping an image for the reference parser\n");
   fresh_log();

   // Mixed sizes, so the dumped image exercises both the one-byte length and the escape to a wide one
   static const uint32_t sizes[] = { 3, 60, 254, 255, 700 };
   for (uint32_t i = 0; i < 120; ++i)
   {
      const uint32_t length = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
      memset(payload, (uint8_t)(i | 0x80), length);
      nandlog_store_record((uint8_t)(1 + (i % 5)), 1000 + (10 * i), payload, length);
   }
   nandlog_flush(true);
   const uint32_t pages = read_all_pages();
   CHECK(pages > 0, "nothing to dump");

   // Rot one page so the parser has a failure to find as well as successes to verify
   for (uint32_t page = 0; page < 4096 * 64; ++page)
   {
      uint32_t magic;
      memcpy(&magic, nandlog_sim_raw_page(page), sizeof(magic));
      if (magic == NANDLOG_PAGE_MAGIC_THIS_BUILD)
      {
         nandlog_sim_corrupt(page + 1, 300, 0xFF);
         break;
      }
   }
   // The metadata ring plus the first blocks of the log region is all this test touched
   CHECK(nandlog_sim_dump(NANDLOG_RECORD_FRAMING ? "nandlog_image_framed.bin" : "nandlog_image.bin", 0, 1024), "could not write the image");
   printf("  wrote %s\n", NANDLOG_RECORD_FRAMING ? "nandlog_image_framed.bin" : "nandlog_image.bin");
   nandlog_deinit();
}

#if NANDLOG_RECORD_FRAMING

static void test_framed_records_walk_back(void)
{
   printf("Framed records can be walked without knowing their types\n");
   fresh_log();

   // Deliberately mixed sizes, including the empty record and sizes either side of a byte boundary
   static const uint32_t sizes[] = { 0, 1, 7, 200, 255, 256, 900, 1000 };
   uint32_t written = 0;
   for (uint32_t round = 0; round < 4; ++round)
      for (uint32_t i = 0; i < (sizeof(sizes) / sizeof(sizes[0])); ++i)
      {
         for (uint32_t b = 0; b < sizes[i]; ++b)
            payload[b] = (uint8_t)(b + i);
         nandlog_store_record((uint8_t)(i + 1), 1000 + (10 * written), payload, sizes[i]);
         ++written;
      }
   nandlog_flush(true);

   // Walk every page the way a reader with no knowledge of the application would
   uint32_t pages = 0, walked = 0;
   nandlog_begin_session();
   nandlog_begin_reading(0, 0);
   nandlog_read_span(&pages, NULL);
   for (uint32_t i = 0; i < pages; ++i)
   {
      nandlog_page_header_t header;
      const uint32_t length = nandlog_retrieve_next_page(readback, &header);
      if (!length)
         continue;
      CHECK(header.magic == NANDLOG_PAGE_MAGIC_FRAMED, "page %u did not announce framing", i);

      // Walked through the library's own iterator rather than by hand
      uint32_t offset = 0, in_page = 0;
      const uint8_t *record = NULL;
      uint32_t record_bytes = 0;
      while (nandlog_framed_next_record(readback, length, &offset, &record, &record_bytes))
      {
         CHECK(record_bytes >= 5, "record %u in page %u is shorter than a header", in_page, i);
         ++in_page;
         ++walked;
      }
      CHECK(offset == length, "page %u had %u bytes left over after walking", i, length - offset);
      CHECK(in_page == header.record_count, "page %u walked %u records, header said %u",
            i, in_page, header.record_count);
   }
   nandlog_end_reading();
   nandlog_end_session();
   CHECK(walked == written, "wrote %u records, walked %u back", written, walked);
   nandlog_deinit();
}

#endif

static void test_metadata_survives(void)
{
   printf("Metadata round trip\n");
   fresh_log();
   uint8_t blob[16];
   nandlog_begin_session();
   nandlog_retrieve_epoch_details(blob, sizeof(blob));
   nandlog_end_session();
   CHECK(memcmp(blob, "sim-metadata", 12) == 0, "metadata came back as '%s'", blob);
   nandlog_deinit();
}

static void test_reboot_recovery(void)
{
   printf("Write head recovers across a reboot\n");
   fresh_log();
   write_records(40, 1000);
   const uint32_t before = read_all_pages();

   // A reboot is deinit/init with the array untouched, which is exactly what the simulator keeps
   nandlog_deinit();
   CHECK(nandlog_init(), "re-init after reboot failed");
   const uint32_t after = read_all_pages();
   CHECK(before == after, "%u pages before the reboot, %u after", before, after);

   write_records(40, 100000);
   const uint32_t grown = read_all_pages();
   CHECK(grown > after, "appending after a reboot did not grow the log (%u -> %u)", after, grown);
   nandlog_deinit();
}

static void test_a_factory_marked_block_is_found_in_the_spare_area(void)
{
   printf("A factory bad-block marker is read out of the spare area\n");
   nandlog_sim_create(DEVICE_ID, sizeof(DEVICE_ID));

   // Mark block 12 the way a manufacturer does: a non-0xFF byte at the first byte of the spare area of the
   // block's first page, with the main array left erased. A read that starts at column zero cannot see this
   // without dragging the whole page along behind it, so this is what exercises the column-addressed read
   const uint32_t page_size = nandlog_chip_geometry()->page_size_bytes;
   const uint32_t pages_per_block = nandlog_chip_geometry()->pages_per_block;
   const uint32_t marked_page = 12 * pages_per_block;
   uint8_t *raw = nandlog_sim_raw_page(marked_page);
   CHECK(raw != NULL, "the simulator would not hand back block 12's first page");
   if (raw)
      raw[page_size] = 0x00;

   CHECK(nandlog_init(), "init failed with a factory-marked block present");
   CHECK(nandlog_chip_is_bad_block(marked_page), "the factory-marked block was not retired at first boot");
   CHECK(!nandlog_chip_is_bad_block(marked_page + pages_per_block), "a block with no marker was retired anyway");
   CHECK(nandlog_bad_block_count() == 1, "the retired-block count is %u, expected 1", nandlog_bad_block_count());

   // And it still holds after a reboot, which is the persisted table doing its job
   nandlog_deinit();
   CHECK(nandlog_init(), "re-init failed");
   CHECK(nandlog_chip_is_bad_block(marked_page), "the factory-marked block was forgotten across a reboot");
   CHECK(nandlog_bad_block_count() == 1, "the retired-block count did not survive a reboot (%u)", nandlog_bad_block_count());
   nandlog_deinit();
}

static void test_bad_block_is_skipped(void)
{
   printf("A block that will not erase is retired\n");
   nandlog_sim_create(DEVICE_ID, sizeof(DEVICE_ID));
   nandlog_sim_faults_t faults;
   memset(&faults, 0, sizeof(faults));
   memcpy(faults.device_id, DEVICE_ID, sizeof(DEVICE_ID));
   faults.unerasable_blocks[0] = 9;
   faults.unerasable_blocks[1] = 10;
   faults.num_unerasable_blocks = 2;
   nandlog_sim_set_faults(&faults);

   CHECK(nandlog_init(), "init failed with bad blocks present");
   nandlog_begin_session();
   const uint8_t metadata[16] = "sim-metadata";
   nandlog_begin_epoch(metadata, sizeof(metadata));
   nandlog_end_session();
   write_records(300, 1000);
   const uint32_t pages = read_all_pages();
   CHECK(pages > 0, "no pages readable with two dead blocks");
   CHECK(nandlog_chip_is_bad_block(9 * 64) || nandlog_chip_is_bad_block(10 * 64), "neither dead block was retired");
   CHECK(nandlog_bad_block_count() > 0, "a block was retired at runtime but the count still reads zero");
   nandlog_deinit();
}

static void test_relocating_a_block_does_not_move_pages_across_the_bus(void)
{
   printf("Relocating a block %s\n", NANDLOG_CHIP_PAGE_COPY ? "moves no page data across the bus"
                                                            : "reads and writes every page");
   fresh_log();

   // Commit exactly twenty pages into the epoch's first block, then take that block away. The next page
   // write has nowhere to go, so those twenty have to be relocated -- which is the work being measured
   write_records(60, 1000);
   const uint32_t before = read_all_pages();
   CHECK(before == 20, "expected twenty committed pages before the fault, got %u", before);

   nandlog_sim_faults_t faults;
   memset(&faults, 0, sizeof(faults));
   memcpy(faults.device_id, DEVICE_ID, sizeof(DEVICE_ID));
   faults.unwritable_blocks[0] = 9;
   faults.num_unwritable_blocks = 1;
   nandlog_sim_set_faults(&faults);

   nandlog_sim_reset_counters();
   write_records(3, 90000);
   const nandlog_sim_counters_t moved = nandlog_sim_counters();
   const uint32_t traffic = moved.spi_read_bytes + moved.spi_write_bytes;
   const uint32_t relocated_bytes = before * nandlog_chip_geometry()->page_size_bytes;
   const uint32_t after = read_all_pages();

   CHECK(nandlog_chip_is_bad_block(9 * 64), "the block that refused the write was not retired");
   CHECK(after >= before, "relocation lost committed pages (%u -> %u)", before, after);
#if NANDLOG_CHIP_PAGE_COPY
   // The page still has to be written and read back once to verify it, and retiring the block persists a
   // marker page, so the bound is not zero -- but it is far below the pages that moved
   CHECK(traffic < relocated_bytes, "an internal copy still moved %u bytes to relocate %u bytes of pages", traffic, relocated_bytes);
#else
   CHECK(traffic > (2 * relocated_bytes), "the read-and-write path moved only %u bytes to relocate %u bytes of pages, so it cannot have read and written each one", traffic, relocated_bytes);
#endif
   printf("  %u bytes of bus traffic to relocate %u bytes of pages\n", traffic, relocated_bytes);
   nandlog_deinit();
}

static void test_an_earlier_epoch_can_be_listed_and_read(void)
{
   printf("An earlier epoch is listed, selected, and read back\n");
   fresh_log();                        // epoch 1
   write_records(30, 1000);            // ten pages
   const uint32_t epoch_one_pages = read_all_pages();
   CHECK(epoch_one_pages == 10, "expected ten pages in the first epoch, got %u", epoch_one_pages);

   // Begin a second epoch and put a different amount of data in it
   nandlog_begin_session();
   const uint8_t details[16] = "epoch-two";
   CHECK(nandlog_begin_epoch(details, sizeof(details)), "beginning a second epoch failed");
   nandlog_end_session();
   write_records(15, 500000);
   CHECK(read_all_pages() == 5, "expected five pages in the second epoch");

   nandlog_begin_session();

   // Both epochs are still named by the ring, newest first
   CHECK(nandlog_epoch_count() == 2, "the ring should describe two epochs, it describes %u", nandlog_epoch_count());
   nandlog_epoch_info_t newest, older;
   CHECK(nandlog_epoch_info(0, &newest), "epoch 0 was not described");
   CHECK(nandlog_epoch_info(1, &older), "epoch 1 was not described");
   CHECK(newest.is_current, "index 0 is not the current epoch");
   CHECK(!older.is_current, "index 1 claims to be the current epoch");
   CHECK(newest.epoch > older.epoch, "the listing is not newest-first (%u then %u)", newest.epoch, older.epoch);
   CHECK(!nandlog_epoch_info(2, &newest), "a third epoch was described out of nowhere");

   // Select the older one and read it back. Nothing has swept over it yet, so all ten pages are still there
   CHECK(nandlog_select_epoch(older.epoch), "selecting the earlier epoch failed");
   uint32_t pages = 0, bytes = 0, seen = 0;
   nandlog_begin_reading(0, 0);
   nandlog_read_span(&pages, &bytes);
   for (uint32_t i = 0; i < pages; ++i)
   {
      nandlog_page_header_t header;
      if (nandlog_retrieve_next_page(readback, &header))
      {
         ++seen;
         CHECK(header.epoch == older.epoch, "a page of epoch %u turned up while reading epoch %u",
               header.epoch, older.epoch);
      }
   }
   nandlog_end_reading();
   CHECK(seen == epoch_one_pages, "reading the earlier epoch gave %u of its %u pages", seen, epoch_one_pages);

   // Writing is refused while it is selected, so a download cannot be mistaken for somewhere to put data
   const uint32_t before = seen;
   write_records(3, 900000);
   nandlog_select_current_epoch();
   nandlog_end_session();
   CHECK(read_all_pages() == 5, "a record was stored while an earlier epoch was selected");
   (void)before;

   // And selecting an epoch that was never written fails rather than reading something else
   nandlog_begin_session();
   CHECK(!nandlog_select_epoch(9999), "selecting an epoch that does not exist succeeded");
   nandlog_end_session();
   nandlog_deinit();
}

static void test_torn_page_is_rejected(void)
{
   printf("A page torn by power loss is rejected, not served\n");
   fresh_log();
   write_records(20, 1000);
   const uint32_t before = read_all_pages();

   // Cut the next page program in half, then reboot into whatever survived
   nandlog_sim_power_fail_after(1);
   write_records(8, 50000);
   CHECK(nandlog_sim_power_failed(), "the simulated power failure never fired");
   nandlog_deinit();
   CHECK(nandlog_init(), "re-init after a torn write failed");

   const uint32_t after = read_all_pages();
   CHECK(after >= before, "a torn page cost previously committed data (%u -> %u)", before, after);
   nandlog_deinit();
}

static void test_corrupt_page_is_rejected(void)
{
   printf("A page whose CRC no longer matches is treated as a gap\n");
   fresh_log();
   write_records(40, 1000);
   const uint32_t before = read_all_pages();

   // Rot a byte in the middle of a committed page's payload; the header still checksums, the payload does not
   uint32_t victim = 0;
   for (uint32_t page = 0; page < 4096 * 64; ++page)
   {
      const uint8_t *raw = nandlog_sim_raw_page(page);
      uint32_t magic;
      memcpy(&magic, raw, sizeof(magic));
      if (magic == NANDLOG_PAGE_MAGIC_THIS_BUILD) { victim = page; break; }
   }
   CHECK(victim != 0, "could not find a committed page to corrupt");
   nandlog_sim_corrupt(victim, 200, 0xFF);

   const uint32_t after = read_all_pages();
   CHECK(after == before - 1, "expected exactly one page to drop out, got %u of %u", after, before);
   nandlog_deinit();
}

static void test_disabled_log_drops_records(void)
{
   printf("A disabled log accepts nothing and still reads\n");
   fresh_log();
   write_records(16, 1000);
   const uint32_t before = read_all_pages();
   nandlog_disable(true);
   write_records(64, 90000);
   CHECK(!nandlog_has_buffered_data(), "a disabled log buffered records");
   const uint32_t after = read_all_pages();
   CHECK(before == after, "a disabled log still wrote (%u -> %u)", before, after);
   nandlog_disable(false);
   nandlog_deinit();
}

static void test_reads_refuse_outside_a_session(void)
{
   printf("Reading outside a session yields nothing\n");
   fresh_log();
   write_records(16, 1000);
   nandlog_begin_reading(0, 0);
   uint32_t pages = 99;
   nandlog_read_span(&pages, NULL);
   CHECK(pages == 0, "a read opened outside a session reported %u pages", pages);
   nandlog_deinit();
}

static void test_busy_timeout_is_fatal(void)
{
   printf("A part that never clears BUSY ends in a fatal fault\n");
   nandlog_sim_create(DEVICE_ID, sizeof(DEVICE_ID));
   CHECK(nandlog_init(), "init failed before the busy fault was armed");

   nandlog_sim_faults_t faults;
   memset(&faults, 0, sizeof(faults));
   memcpy(faults.device_id, DEVICE_ID, sizeof(DEVICE_ID));
   faults.stay_busy = true;
   nandlog_sim_set_faults(&faults);
   nandlog_sim_expect_fatal(true);

   if (setjmp(*nandlog_sim_fatal_jump()) == 0)
   {
      write_records(8, 1000);
      CHECK(false, "a permanently busy part did not produce a fatal fault");
   }
   const char *reason = NULL;
   CHECK(nandlog_sim_fatal_seen(&reason), "no fatal fault was recorded");
   nandlog_sim_expect_fatal(false);
   nandlog_sim_destroy();
}


static void test_date_limited_read_spans_a_time_discontinuity(void)
{
   // A device that adopts a new network time base steps its clock BACKWARDS, and the log commits a page at
   // that point, so pages either side of the step are not ordered in time. Field offloads showed steps of
   // 2-17 s. A START bound is the case that loses data: a binary search over page headers can settle past the
   // step, and nandlog_begin_reading() then starts at the write head and ships almost nothing.
   //
   // Layout: a first run climbing to HIGH, then the clock steps back and a second run climbs but STOPS below
   // the requested bound. Only the first run's tail qualifies, and it sits BEFORE hundreds of pages that do
   // not -- so nothing that stops at the first non-qualifying page can find it.
   printf("A date-limited read spans a backward time step\n");
   const uint32_t regression_ms = 17210;          // the largest step seen in the field, to the millisecond
   const uint32_t records_per_run = 600;
   fresh_log();
   nandlog_end_session();

   for (uint32_t i = 0; i < records_per_run; ++i)
   {
      memset(payload, (uint8_t)i, sizeof(payload));
      nandlog_store_record(7, 100000 + (10 * i), payload, sizeof(payload));
   }
   const uint32_t high_water = 100000 + (10 * (records_per_run - 1));
   nandlog_flush(true);

   // Second run: stepped back, and deliberately never climbing back up to high_water
   for (uint32_t i = 0; i < records_per_run; ++i)
   {
      memset(payload, (uint8_t)(i ^ 0x5A), sizeof(payload));
      nandlog_store_record(7, (high_water - regression_ms) + (10 * i), payload, sizeof(payload));
   }
   nandlog_flush(true);

   // A bound only the FIRST run ever reached
   const uint32_t target = high_water - (regression_ms / 4);
   nandlog_begin_session();
   nandlog_begin_reading(target, 0);
   uint32_t pages = 0, bytes = 0;
   nandlog_read_span(&pages, &bytes);
   uint32_t delivered = 0, reached = 0;
   for (uint32_t i = 0; i < pages; ++i)
   {
      nandlog_page_header_t header;
      if (nandlog_retrieve_next_page(readback, &header))
      {
         ++delivered;
         if ((header.last_timestamp != NANDLOG_NO_TIMESTAMP) && (header.last_timestamp >= target))
            ++reached;
      }
   }
   nandlog_end_reading();
   nandlog_end_session();

   CHECK(delivered > 0, "a date-limited read starting across a backward time step returned no pages at all");
   CHECK(reached > 0, "no delivered page reached the requested start bound of %u -- the qualifying data sits before the discontinuity and the seek skipped past it", target);
}


int main(void)
{
   printf("nandlog host tests (record framing %s)\n==================================%s\n", NANDLOG_RECORD_FRAMING ? "on" : "off", NANDLOG_RECORD_FRAMING ? "=" : "");
   test_roundtrip();
   test_a_record_can_exactly_fill_a_page();
   test_metadata_survives();
   test_image_dump_for_the_parser();
#if NANDLOG_RECORD_FRAMING
   test_framed_records_walk_back();
#endif
   test_reboot_recovery();
   test_a_factory_marked_block_is_found_in_the_spare_area();
   test_bad_block_is_skipped();
   test_relocating_a_block_does_not_move_pages_across_the_bus();
   test_an_earlier_epoch_can_be_listed_and_read();
   test_torn_page_is_rejected();
   test_corrupt_page_is_rejected();
   test_disabled_log_drops_records();
   test_reads_refuse_outside_a_session();
   test_busy_timeout_is_fatal();
   test_date_limited_read_spans_a_time_discontinuity();

   const nandlog_sim_counters_t counters = nandlog_sim_counters();
   printf("\n%u checks, %u failed\n", tests_run, tests_failed);
   printf("last run: %u page reads, %u page writes, %u block erases\n", counters.page_reads, counters.page_writes, counters.block_erases);
   return tests_failed ? 1 : 0;
}
