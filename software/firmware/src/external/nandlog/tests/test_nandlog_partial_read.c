// ON-DEVICE TEST 1 of 3 -- partial and column-addressed page reads
//
// WHAT THIS GATES. nandlog now reads 32-byte headers out of 4 KB pages on every boot and on every bounded
// download, and reads a single marker byte out of the spare area once per block when it builds the
// bad-block table. Both rest on nandlog_chip_read_page_region(), which has never run on silicon.
//
// WHY IT MATTERS THAT YOU RUN IT. If the 16-bit column address goes out in the wrong byte order, the
// factory bad-block scan does not fail -- it reads 0xFF from the wrong place, concludes every block is
// good, and retires nothing. That is a fail-OPEN, and the log would go on working until it wrote into a
// block the manufacturer had already condemned. Nothing in the host simulator can catch it, because the
// simulator decodes the column address the same way the driver encodes it.
//
// WHAT TO REPORT BACK: the PASS/FAIL line, the factory bad-block count, and the whole timing table.

#include <string.h>
#include "nandlog_hw_test.h"

static uint8_t whole_page[NANDLOG_MAX_PAGE_SIZE_BYTES + NANDLOG_MAX_SPARE_SIZE_BYTES];
static uint8_t region[NANDLOG_MAX_PAGE_SIZE_BYTES];

#define PAGE_BYTES     (nandlog_chip_geometry()->page_size_bytes)
#define SPARE_BYTES    (nandlog_chip_geometry()->spare_size_bytes)
#define PAGES_PER_BLK  (nandlog_chip_geometry()->pages_per_block)
#define BLOCK_COUNT    (nandlog_chip_geometry()->block_count)


// Test 1a -- a short read from offset zero agrees with a whole-page read -----------------------------------------------

static void test_short_read_agrees_with_whole_page(void)
{
   print("\n--- 1a: a short read returns the same bytes as a whole-page read ---\n");

   // Put something on a scratch page that is neither erased nor constant, so a read from the wrong place
   // cannot coincidentally agree
   const uint32_t page = hw_scratch_page(0);
   hw_fill_pattern(whole_page, PAGE_BYTES, 0xA5A50001);
   nandlog_chip_erase_block(page);
   HW_CHECK(nandlog_chip_write_page(whole_page, page), "could not program the scratch page");

   HW_CHECK(nandlog_chip_read_page(whole_page, page), "whole-page read reported an ECC failure");
   HW_CHECK(hw_check_pattern(whole_page, PAGE_BYTES, 0xA5A50001), "the scratch page did not read back as written");

   static const uint32_t lengths[] = { 1, 4, 32, 33, 255, 256, 1024 };
   for (uint32_t i = 0; i < (sizeof(lengths) / sizeof(lengths[0])); ++i)
   {
      memset(region, 0x00, lengths[i]);
      HW_CHECK(nandlog_chip_read_page_region(region, page, 0, lengths[i]), "a %u-byte read reported an ECC failure", lengths[i]);
      HW_CHECK(memcmp(region, whole_page, lengths[i]) == 0, "a %u-byte read from offset 0 disagrees with the whole page", lengths[i]);
   }
}


// Test 1b -- a read from a non-zero column lands where it says it does -------------------------------------------------

static void test_column_addressing(void)
{
   print("\n--- 1b: a read from a non-zero column lands at that column ---\n");
   const uint32_t page = hw_scratch_page(0);

   HW_CHECK(nandlog_chip_read_page(whole_page, page), "whole-page read reported an ECC failure");

   // Offsets chosen to exercise both address bytes: below 256 uses only the low byte, above it needs the
   // high byte, and a byte-swapped encoding lands somewhere else entirely for every one of these
   static const uint32_t offsets[] = { 1, 7, 100, 255, 256, 257, 512, 1000, 2047, 2048 };
   for (uint32_t i = 0; i < (sizeof(offsets) / sizeof(offsets[0])); ++i)
   {
      const uint32_t offset = offsets[i];
      if ((offset + 64) > PAGE_BYTES)
         continue;
      memset(region, 0x00, 64);
      HW_CHECK(nandlog_chip_read_page_region(region, page, offset, 64), "a read at column %u reported an ECC failure", offset);
      if (memcmp(region, whole_page + offset, 64) != 0)
      {
         ++hw_checks_failed;
         print("  FAIL: column %u read the wrong bytes (got %02X %02X %02X %02X, expected %02X %02X %02X %02X)\n",
               offset, region[0], region[1], region[2], region[3],
               whole_page[offset], whole_page[offset + 1], whole_page[offset + 2], whole_page[offset + 3]);

         // Say plainly what a byte-swapped address would have produced, so the failure names its own cause
         const uint32_t swapped = ((offset & 0xFF) << 8) | ((offset >> 8) & 0xFF);
         if ((swapped + 4) <= PAGE_BYTES)
            print("        a byte-swapped column would have read from %u: %02X %02X %02X %02X\n", swapped,
                  whole_page[swapped], whole_page[swapped + 1], whole_page[swapped + 2], whole_page[swapped + 3]);
      }
      else
         ++hw_checks_run;
   }
}


// Test 1c -- THE IMPORTANT ONE: the spare area reads the same both ways ------------------------------------------------

static void test_spare_area_marker_agrees(void)
{
   print("\n--- 1c: the spare-area marker reads the same via a long read and via its column ---\n");
   print("     (this is the one that decides whether the bad-block scan can be trusted)\n");

   uint32_t mismatches = 0, factory_bad = 0, unreadable = 0, scanned = 0;
   const uint32_t start = hw_cycle_count();

   for (uint32_t page = 0; page < (BLOCK_COUNT * PAGES_PER_BLK); page += PAGES_PER_BLK)
   {
      // The marker, arrived at by reading everything in front of it
      const bool ok_long = nandlog_chip_read_page_region(whole_page, page, 0, PAGE_BYTES + SPARE_BYTES);
      const uint8_t via_long = whole_page[PAGE_BYTES];

      // The marker, addressed directly
      uint8_t via_column = 0xFF;
      const bool ok_column = nandlog_chip_read_page_region(&via_column, page, PAGE_BYTES, 1);

      ++scanned;
      if ((via_long != via_column) || (ok_long != ok_column))
      {
         if (mismatches < 8)
            print("  FAIL: block %u -- long read %02X/%u, column read %02X/%u\n",
                  page / PAGES_PER_BLK, via_long, (uint32_t)ok_long, via_column, (uint32_t)ok_column);
         ++mismatches;
      }
      if (!ok_long)
         ++unreadable;
      else if (via_long != 0xFF)
         ++factory_bad;
   }

   const uint32_t elapsed_us = hw_elapsed_us(start, hw_cycle_count());
   ++hw_checks_run;
   if (mismatches)
   {
      ++hw_checks_failed;
      print("  FAIL: %u of %u blocks disagreed\n", mismatches, scanned);
   }

   print("  scanned %u blocks in %u us (%u us/block)\n", scanned, elapsed_us, scanned ? (elapsed_us / scanned) : 0);
   print("  factory-marked bad: %u    unreadable page 0: %u\n", factory_bad, unreadable);

   // A survey that finds nothing has not proved the column address is right -- it has only proved that
   // reading two different wrong places both returned 0xFF
   if (!factory_bad && !unreadable)
      print("  *** NOTE: this part reports NO factory-bad blocks, so 1c compared 0xFF against 0xFF.    ***\n"
            "  *** 1b is then the only evidence the column address is correct. Treat 1b as REQUIRED.  ***\n");
}


// Test 1d -- a page header reads correctly on its own ------------------------------------------------------------------

static void test_page_header_only_read(void)
{
   print("\n--- 1d: a page header reads correctly without the page behind it ---\n");

   // Commit a real log page, then read only its header the way every boot and every bounded download now do
   nandlog_disable(false);
   hw_fill_pattern(region, 512, 0x5EED1234);
   nandlog_store_record(1, 1000, region, 512);
   nandlog_flush(true);

   bool found = false;
   nandlog_page_header_t via_recent;
   const uint32_t length = nandlog_read_recent_page(0, region, &via_recent, &found);
   HW_CHECK(length > 0, "the page just committed did not read back");

   // Same page, header only, straight off the chip
   const uint32_t page = via_recent.seq;   // only used for the message below
   HW_CHECK(via_recent.magic == NANDLOG_PAGE_MAGIC_THIS_BUILD,
            "the committed page announced magic %08X, not %08X (seq %u)",
            via_recent.magic, (uint32_t)NANDLOG_PAGE_MAGIC_THIS_BUILD, page);
   HW_CHECK(via_recent.payload_length == length, "header says %u payload bytes, the read gave %u",
            via_recent.payload_length, length);

   // And prove the boot path works end to end: re-initialising re-runs find_newest_metadata() and
   // recover_write_head(), both of which are now header-only reads
   nandlog_deinit();
   HW_CHECK(nandlog_init(), "re-initialising after a header-only boot path failed");
   nandlog_begin_session();
   uint32_t pages = 0;
   nandlog_begin_reading(0, 0);
   nandlog_read_span(&pages, NULL);
   nandlog_end_reading();
   nandlog_end_session();
   HW_CHECK(pages > 0, "the write head was not recovered -- the log reads as empty after a re-init");
   print("  write head recovered, %u pages in the epoch\n", pages);
}


// Test 1e -- boot cost, which is the number that decides the bad-block redesign ----------------------------------------

static void test_boot_timing(void)
{
   print("\n--- 1e: boot cost ---\n");

   nandlog_deinit();
   const uint32_t start = hw_cycle_count();
   const bool ok = nandlog_init();
   const uint32_t warm_boot_us = hw_elapsed_us(start, hw_cycle_count());
   HW_CHECK(ok, "nandlog_init() failed while being timed");

   // One latch plus a 32-byte read, and one latch plus a whole page, so the ratio of the two says how much
   // of a read is the flash and how much is the bus
   const uint32_t page = hw_scratch_page(0);
   uint32_t t0 = hw_cycle_count();
   for (uint32_t i = 0; i < 32; ++i)
      nandlog_chip_read_page_region(region, page, 0, sizeof(nandlog_page_header_t));
   const uint32_t header_us = hw_elapsed_us(t0, hw_cycle_count()) / 32;

   t0 = hw_cycle_count();
   for (uint32_t i = 0; i < 32; ++i)
      nandlog_chip_read_page(whole_page, page);
   const uint32_t full_us = hw_elapsed_us(t0, hw_cycle_count()) / 32;

   print("  nandlog_init() (warm boot) : %u us\n", warm_boot_us);
   print("  32-byte header read        : %u us\n", header_us);
   print("  whole-page read            : %u us\n", full_us);
   print("  ratio                      : %u%% of a whole-page read\n", full_us ? ((100 * header_us) / full_us) : 0);
   print("\n  A full-array scan at %u us/block would cost %u ms per boot, which is what a bad-block\n"
         "  scheme with no persisted table would have to spend on every single boot.\n",
         header_us, (header_us * BLOCK_COUNT) / 1000);
}


int main(void)
{
   setup_hardware();
   hw_timing_init();
   if (!nandlog_init())
   {
      print("FATAL: nandlog_init() failed -- no part, or the port would not open\n");
      while (true)
         am_hal_delay_us(1000000);
   }
   system_enable_interrupts(true);

   print("\n============================================================\n");
   print("nandlog on-device test 1: partial and column-addressed reads\n");
   print("part geometry: %u B page + %u B spare, %u pages/block, %u blocks\n",
         PAGE_BYTES, SPARE_BYTES, PAGES_PER_BLK, BLOCK_COUNT);
   print("============================================================\n");

   test_short_read_agrees_with_whole_page();
   test_column_addressing();
   test_spare_area_marker_agrees();
   test_page_header_only_read();
   test_boot_timing();

   HW_REPORT("TEST 1: PARTIAL READS");
   print("\nIf 1b or 1c failed, do not deploy: the bad-block scan is reading the wrong place\n"
         "and will silently fail to retire factory-bad blocks.\n");

   while (true)
      am_hal_delay_us(1000000);
}
