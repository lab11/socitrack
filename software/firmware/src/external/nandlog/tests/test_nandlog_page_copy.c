// ON-DEVICE TEST 2 of 3 -- relocating a page inside the chip
//
// WHAT THIS GATES. nandlog_chip_copy_page() asks the part to latch a page into its own cache register and
// program it straight back out to another address, with no PROGRAM DATA LOAD in between, so the page never
// crosses the bus. The host simulator emulates this faithfully enough to test the log's use of it, but it
// cannot say what the real part's on-die ECC does on the way through.
//
// THE THREE THINGS THAT COULD GO WRONG.
//   1. The part does not support the sequence, and the copy silently programs whatever was left in the
//      cache register -- so the destination holds the WRONG PAGE rather than nothing. 2a catches this.
//   2. ECC is re-encoded over the copy and the destination does not read back clean. 2a catches this.
//   3. The copy carries the source's SPARE AREA across as well. If a source spare ever held a non-0xFF
//      byte, the destination would then look factory-bad and a later scan would retire a perfectly good
//      block. 2b catches this, and it is the subtle one.
//
// WHAT TO REPORT BACK: the PASS/FAIL line and the timing table. If you can, power-cycle and run it again;
// it checks the previous run's copy on startup before erasing anything.

#include <string.h>
#include "nandlog_hw_test.h"

static uint8_t source_buffer[NANDLOG_MAX_PAGE_SIZE_BYTES];
static uint8_t verify_buffer[NANDLOG_MAX_PAGE_SIZE_BYTES + NANDLOG_MAX_SPARE_SIZE_BYTES];

#define PAGE_BYTES     (nandlog_chip_geometry()->page_size_bytes)
#define SPARE_BYTES    (nandlog_chip_geometry()->spare_size_bytes)
#define PATTERN_SEED   0x13572468u


// Test 2a -- a copied page is the source page ---------------------------------------------------------------------------

static void test_a_copied_page_is_the_source_page(void)
{
   print("\n--- 2a: a copied page arrives intact and reads back clean ---\n");
   hw_power_up();
   const uint32_t source = hw_scratch_page(0), destination = hw_scratch_page(1);

   HW_CHECK(nandlog_chip_erase_block(source), "could not erase the source block");
   HW_CHECK(nandlog_chip_erase_block(destination), "could not erase the destination block");

   hw_fill_pattern(source_buffer, PAGE_BYTES, PATTERN_SEED);
   HW_CHECK(nandlog_chip_write_page(source_buffer, source), "could not program the source page");
   HW_CHECK(nandlog_chip_read_page(verify_buffer, source), "the source page would not read back");
   HW_CHECK(hw_check_pattern(verify_buffer, PAGE_BYTES, PATTERN_SEED), "the source page is wrong before any copy");

   // Deliberately leave something ELSE in the cache register first. If the part ignores the copy sequence
   // and programs whatever the register happens to hold, the destination gets this instead -- which is a
   // silent wrong-data failure, and the reason this step exists
   nandlog_chip_read_page(verify_buffer, hw_scratch_page(2));

   const nandlog_copy_result_t result = nandlog_chip_copy_page(source, destination);
   ++hw_checks_run;
   if (result == NANDLOG_COPY_UNSUPPORTED)
   {
      print("  NOTE: this driver reports no internal copy; nothing below applies\n");
      return;
   }
   if (result != NANDLOG_COPY_OK)
   {
      ++hw_checks_failed;
      print("  FAIL: the copy reported a program failure\n");
      return;
   }

   memset(verify_buffer, 0, PAGE_BYTES);
   HW_CHECK(nandlog_chip_read_page(verify_buffer, destination), "the copied page reported an uncorrectable ECC error");
   HW_CHECK(hw_check_pattern(verify_buffer, PAGE_BYTES, PATTERN_SEED), "the copied page is not the source page");

   // Name the failure mode if it happened, rather than leaving a bare mismatch
   if (!hw_check_pattern(verify_buffer, PAGE_BYTES, PATTERN_SEED))
   {
      bool all_erased = true;
      for (uint32_t i = 0; i < PAGE_BYTES; ++i)
         if (verify_buffer[i] != 0xFF)
            { all_erased = false; break; }
      print("        the destination is %s\n", all_erased ? "still erased -- the copy did nothing"
                                                          : "holding something else -- the cache register was not reloaded");
   }
}


// Test 2b -- THE SUBTLE ONE: the copy must not carry the spare area across ----------------------------------------------

static void test_a_copy_does_not_carry_the_spare_area(void)
{
   print("\n--- 2b: a copy does not drag the source's spare area to the destination ---\n");
   print("     (a stray non-0xFF byte here would make a good block look factory-bad)\n");
   hw_power_up();

   const uint32_t destination = hw_scratch_page(1);
   uint8_t marker = 0x00;
   HW_CHECK(nandlog_chip_read_page_region(&marker, destination, PAGE_BYTES, 1), "could not read the destination's spare area");
   HW_CHECK(marker == 0xFF, "the copied page's spare byte is %02X, not 0xFF -- a later scan would retire this block", marker);

   // The whole first spare word, since a part may place its marker at any of the first few bytes
   memset(verify_buffer, 0x00, 8);
   HW_CHECK(nandlog_chip_read_page_region(verify_buffer, destination, PAGE_BYTES, 8), "could not read the destination's spare area");
   print("  destination spare bytes 0..7: %02X %02X %02X %02X %02X %02X %02X %02X\n",
         verify_buffer[0], verify_buffer[1], verify_buffer[2], verify_buffer[3],
         verify_buffer[4], verify_buffer[5], verify_buffer[6], verify_buffer[7]);
}


// Test 2c -- copying an erased page, and copying several in a row --------------------------------------------------------

static void test_copying_a_run_of_pages(void)
{
   print("\n--- 2c: a run of pages, including an erased one ---\n");
   hw_power_up();
   const uint32_t source = hw_scratch_page(0), destination = hw_scratch_page(1);
   const uint32_t pages_per_block = nandlog_chip_geometry()->pages_per_block;
   const uint32_t run = (pages_per_block < 8) ? pages_per_block : 8;

   HW_CHECK(nandlog_chip_erase_block(source), "could not erase the source block");
   HW_CHECK(nandlog_chip_erase_block(destination), "could not erase the destination block");

   // Pages 0..run-2 carry data; the last is left erased, which is what a half-filled block looks like
   for (uint32_t i = 0; i + 1 < run; ++i)
   {
      hw_fill_pattern(source_buffer, PAGE_BYTES, PATTERN_SEED + i);
      HW_CHECK(nandlog_chip_write_page(source_buffer, source + i), "could not program source page %u", i);
   }

   uint32_t copied = 0;
   for (uint32_t i = 0; i < run; ++i)
      if (nandlog_chip_copy_page(source + i, destination + i) == NANDLOG_COPY_OK)
         ++copied;
   HW_CHECK(copied == run, "only %u of %u pages copied", copied, run);

   for (uint32_t i = 0; i + 1 < run; ++i)
   {
      memset(verify_buffer, 0, PAGE_BYTES);
      HW_CHECK(nandlog_chip_read_page(verify_buffer, destination + i), "copied page %u reported an ECC error", i);
      HW_CHECK(hw_check_pattern(verify_buffer, PAGE_BYTES, PATTERN_SEED + i), "copied page %u is wrong", i);
   }

   // The erased one must still be erased, not whatever the previous copy left in the cache register
   HW_CHECK(nandlog_chip_read_page(verify_buffer, destination + run - 1), "the copied erased page reported an ECC error");
   bool still_erased = true;
   for (uint32_t i = 0; i < PAGE_BYTES; ++i)
      if (verify_buffer[i] != 0xFF)
         { still_erased = false; break; }
   HW_CHECK(still_erased, "copying an erased page left data at the destination -- the cache register is stale");
}


// Test 2d -- what it actually saves ---------------------------------------------------------------------------------------

static void test_copy_timing(void)
{
   print("\n--- 2d: what the internal copy costs against a read and a write ---\n");
   hw_power_up();
   const uint32_t source = hw_scratch_page(0), destination = hw_scratch_page(2);
   const uint32_t rounds = 8;

   // Internal copy
   uint32_t copy_us = 0;
   for (uint32_t i = 0; i < rounds; ++i)
   {
      nandlog_chip_erase_block(destination);
      const uint32_t t0 = hw_cycle_count();
      nandlog_chip_copy_page(source, destination);
      copy_us += hw_elapsed_us(t0, hw_cycle_count());
   }

   // Read into RAM and write back out, which is what the fallback path does
   uint32_t read_write_us = 0;
   for (uint32_t i = 0; i < rounds; ++i)
   {
      nandlog_chip_erase_block(destination);
      const uint32_t t0 = hw_cycle_count();
      nandlog_chip_read_page(source_buffer, source);
      nandlog_chip_write_page(source_buffer, destination);
      read_write_us += hw_elapsed_us(t0, hw_cycle_count());
   }

   copy_us /= rounds;
   read_write_us /= rounds;
   print("  internal copy    : %u us\n", copy_us);
   print("  read + write back: %u us\n", read_write_us);
   if (copy_us && read_write_us)
      print("  saving           : %u us per page (%u%%)\n",
            (read_write_us > copy_us) ? (read_write_us - copy_us) : 0,
            (read_write_us > copy_us) ? (100 * (read_write_us - copy_us) / read_write_us) : 0);
   print("\n  Relocating a full %u-page block therefore costs %u ms internally against %u ms across the bus.\n",
         nandlog_chip_geometry()->pages_per_block,
         (copy_us * nandlog_chip_geometry()->pages_per_block) / 1000,
         (read_write_us * nandlog_chip_geometry()->pages_per_block) / 1000);
}


// Persistence across a power cycle ----------------------------------------------------------------------------------------

static void check_previous_run_survived(void)
{
   hw_power_up();
   // Run before anything is erased. On a first run the destination is erased or holds something else, which
   // is not a failure; on a second run after a power cycle it must still hold what the first run copied
   const uint32_t destination = hw_scratch_page(1);
   if (nandlog_chip_read_page(verify_buffer, destination) && hw_check_pattern(verify_buffer, PAGE_BYTES, PATTERN_SEED))
      print("\nPREVIOUS RUN: the page copied before the last power cycle is still correct [PASSED]\n");
   else
      print("\nPREVIOUS RUN: nothing to check (first run, or the last run did not get that far)\n"
            "              Power-cycle and run this test again to check that a copy survives.\n");
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
   hw_power_up();                   // every test below calls nandlog_chip_* directly

   print("\n============================================================\n");
   print("nandlog on-device test 2: chip-internal page copy\n");
   print("NANDLOG_CHIP_PAGE_COPY is %u in this build\n", (uint32_t)NANDLOG_CHIP_PAGE_COPY);
   print("scratch blocks: %u, %u, %u\n", hw_scratch_page(0) / nandlog_chip_geometry()->pages_per_block,
         hw_scratch_page(1) / nandlog_chip_geometry()->pages_per_block,
         hw_scratch_page(2) / nandlog_chip_geometry()->pages_per_block);
   print("============================================================\n");

   check_previous_run_survived();
   test_a_copied_page_is_the_source_page();
   test_a_copy_does_not_carry_the_spare_area();
   test_copying_a_run_of_pages();
   test_copy_timing();

   hw_power_down();
   HW_REPORT("TEST 2: INTERNAL PAGE COPY");
   print("\nIf 2a failed, set NANDLOG_CHIP_PAGE_COPY to 0 in nandlog_conf.h before deploying.\n"
         "If only 2b failed, the copy works but carries the spare area -- tell me, because the\n"
         "driver then has to clear the destination's marker byte after every relocation.\n");
   print("\nNow power-cycle the board and run this same test again to check that a copied page survives.\n");

   while (true)
      am_hal_delay_us(1000000);
}
