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

static uint8_t payload[512], readback[8192];

static void fresh_log(void)
{
   nandlog_sim_create(DEVICE_ID, sizeof(DEVICE_ID));
   if (!nandlog_init())
   {
      printf("  FATAL: nandlog_init() failed\n");
      exit(1);
   }
   nandlog_enter_maintenance_mode();
   const uint8_t metadata[16] = "sim-metadata";
   nandlog_store_metadata(metadata, sizeof(metadata));
   nandlog_exit_maintenance_mode();
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
   nandlog_enter_maintenance_mode();
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
   nandlog_exit_maintenance_mode();
   // read_span() verifies payloads as well as headers, so its total is exactly what the pages deliver. A
   // caller sizes a receive buffer from this, and either direction of error strands a transfer
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

static void test_image_dump_for_the_parser(void)
{
   printf("Dumping an image for the reference parser\n");
   fresh_log();
   write_records(96, 1000);
   nandlog_flush(true);
   const uint32_t pages = read_all_pages();
   CHECK(pages > 0, "nothing to dump");

   // Rot one page so the parser has a failure to find as well as successes to verify
   for (uint32_t page = 0; page < 4096 * 64; ++page)
   {
      uint32_t magic;
      memcpy(&magic, nandlog_sim_raw_page(page), sizeof(magic));
      if (magic == NANDLOG_PAGE_MAGIC)
      {
         nandlog_sim_corrupt(page + 1, 300, 0xFF);
         break;
      }
   }
   // The metadata ring plus the first blocks of the log region is all this test touched
   CHECK(nandlog_sim_dump("nandlog_image.bin", 0, 1024), "could not write nandlog_image.bin");
   printf("  wrote nandlog_image.bin\n");
   nandlog_deinit();
}

static void test_metadata_survives(void)
{
   printf("Metadata round trip\n");
   fresh_log();
   uint8_t blob[16];
   nandlog_enter_maintenance_mode();
   nandlog_retrieve_metadata(blob, sizeof(blob));
   nandlog_exit_maintenance_mode();
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
   nandlog_enter_maintenance_mode();
   const uint8_t metadata[16] = "sim-metadata";
   nandlog_store_metadata(metadata, sizeof(metadata));
   nandlog_exit_maintenance_mode();
   write_records(300, 1000);
   const uint32_t pages = read_all_pages();
   CHECK(pages > 0, "no pages readable with two dead blocks");
   CHECK(nandlog_chip_is_bad_block(9 * 64) || nandlog_chip_is_bad_block(10 * 64),
         "neither dead block was retired");
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
      if (magic == NANDLOG_PAGE_MAGIC) { victim = page; break; }
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
   printf("Reading outside a maintenance session yields nothing\n");
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


int main(void)
{
   printf("nandlog host tests\n==================\n");
   test_roundtrip();
   test_metadata_survives();
   test_image_dump_for_the_parser();
   test_reboot_recovery();
   test_bad_block_is_skipped();
   test_torn_page_is_rejected();
   test_corrupt_page_is_rejected();
   test_disabled_log_drops_records();
   test_reads_refuse_outside_a_session();
   test_busy_timeout_is_fatal();

   const nandlog_sim_counters_t counters = nandlog_sim_counters();
   printf("\n%u checks, %u failed\n", tests_run, tests_failed);
   printf("last run: %u page reads, %u page writes, %u block erases\n",
          counters.page_reads, counters.page_writes, counters.block_erases);
   return tests_failed ? 1 : 0;
}
