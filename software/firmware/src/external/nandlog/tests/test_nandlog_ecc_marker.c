// ON-DEVICE TEST 3 of 3 -- can a bad-block marker be written into the spare area?
//
// WHAT THIS ANSWERS. This test gates nothing that ships today. It answers the one question that decides
// whether nandlog can delete its whole persisted bad-block apparatus -- the 80-block reserve, the marker
// page, the reload path and the "table reports an implausible count, ignore it" branch -- and mark a retired
// block in its own spare area instead, the way Dhara does.
//
// That only works if a marker byte can be PROGRAMMED into the spare area and read back afterwards. On a part
// with on-die ECC that is not obvious: the ECC engine owns part of the spare, and the factory's own markers
// are written at the factory with ECC disabled, which is why they can be read but says nothing about whether
// we can write one.
//
// THIS TEST WRITES TO THE PART. It works only in the top blocks of the reserve, which the log never places
// a page in, and it erases them again at the end. It does NOT touch the log region, the metadata ring, or
// the bad-block marker page. It is still a write test; run it on a development board.
//
// It talks to the chip directly rather than through the driver, because it has to do things no driver
// function exposes -- turn ECC off, program a single byte at a column address -- and because the point is
// to interrogate the silicon, not to exercise the driver.
//
// WHAT TO REPORT BACK: every line from step 1 onwards. There are no pass/fail verdicts for most of it;
// the values ARE the answer.

#include <string.h>
#include "nandlog_hw_test.h"

// Alliance AS5F18G04SND. Change these two blocks for another part
#define COMMAND_READ_STATUS_REGISTER                0x0F
#define COMMAND_WRITE_STATUS_REGISTER               0x1F
#define COMMAND_WRITE_ENABLE                        0x06
#define COMMAND_WRITE_DISABLE                       0x04
#define COMMAND_BLOCK_ERASE                         0xD8
#define COMMAND_PROGRAM_DATA_LOAD                   0x02
#define COMMAND_PROGRAM_LOAD_RANDOM_DATA            0x84
#define COMMAND_PROGRAM_EXECUTE                     0x10
#define COMMAND_PAGE_DATA_READ                      0x13
#define COMMAND_READ                                0x03

#define STATUS_REGISTER_1                           0xA0
#define STATUS_REGISTER_2                           0xB0
#define STATUS_REGISTER_3                           0xC0

#define STATUS_PAGE_FATAL_ERROR                     0b00100000
#define STATUS_WRITE_FAILURE                        0b00001000
#define STATUS_ERASE_FAILURE                        0b00000100
#define STATUS_BUSY                                 0b00000001

#define PROTECT_NONE                                0b00000010
#define CONFIG_NORMAL                               0b00010000
#define ECC_ENABLE_BIT                              0b00010000

#define PAGE_BYTES     (nandlog_chip_geometry()->page_size_bytes)

static uint8_t saved_status_2;
static uint32_t marker_page;

// Whether step 0 found a marker from a previous run. Once it has, the power-cycle question is answered and
// the marker can be cleared -- otherwise it stays on the part for good, and every later survey counts it as
// a factory-bad block
static bool persistence_confirmed;


// A minimal command layer, owned by this test ---------------------------------------------------------------------------

static uint8_t read_register(uint8_t which)
{
   uint8_t value = 0;
   nandlog_port_transfer_read(COMMAND_READ_STATUS_REGISTER, &which, 1, &value, 1);
   return value;
}

static void write_register(uint8_t which, uint8_t value)
{
   nandlog_port_transfer_write(COMMAND_WRITE_STATUS_REGISTER, &which, 1, &value, 1);
}

static bool wait_until_not_busy(void)
{
   for (uint32_t polls = 100000; polls; --polls)
   {
      if (!(read_register(STATUS_REGISTER_3) & STATUS_BUSY))
         return true;
      nandlog_port_delay_us(10);
   }
   print("  *** the part never cleared BUSY ***\n");
   return false;
}

static void split_page_address(uint32_t page, uint8_t *out)
{
   out[0] = (uint8_t)(page >> 16);
   out[1] = (uint8_t)(page >> 8);
   out[2] = (uint8_t)page;
}

static uint8_t latch_and_read(uint8_t *out, uint32_t page, uint32_t column, uint32_t length)
{
   // Returns status register 3, so the caller can see the ECC verdict as well as the bytes
   const uint8_t column_address[3] = { (uint8_t)(column >> 8), (uint8_t)column, 0x00 };
   uint8_t page_address[3];
   split_page_address(page, page_address);
   wait_until_not_busy();
   nandlog_port_transfer_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_address, sizeof(page_address));
   wait_until_not_busy();
   const uint8_t status = read_register(STATUS_REGISTER_3);
   nandlog_port_transfer_read(COMMAND_READ, column_address, sizeof(column_address), out, length);
   return status;
}

// Program 'length' bytes at 'column' WITHOUT resetting the rest of the cache register first, which is what
// 0x84 is for. 0x02 would clear the whole register to 0xFF, which for a single spare byte is the same thing
// -- but only because the page is erased, and being explicit costs nothing
static bool program_at_column(uint32_t page, uint32_t column, const uint8_t *data, uint32_t length, bool reset_cache)
{
   const uint8_t column_address[2] = { (uint8_t)(column >> 8), (uint8_t)column };
   uint8_t page_address[3];
   split_page_address(page, page_address);
   wait_until_not_busy();
   nandlog_port_transfer_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
   nandlog_port_transfer_write(reset_cache ? COMMAND_PROGRAM_DATA_LOAD : COMMAND_PROGRAM_LOAD_RANDOM_DATA,
                               column_address, sizeof(column_address), data, length);
   wait_until_not_busy();
   nandlog_port_transfer_write(COMMAND_PROGRAM_EXECUTE, NULL, 0, page_address, sizeof(page_address));
   wait_until_not_busy();
   return !(read_register(STATUS_REGISTER_3) & STATUS_WRITE_FAILURE);
}

static bool erase_block(uint32_t page)
{
   uint8_t page_address[3];
   split_page_address(page & ~(nandlog_chip_geometry()->pages_per_block - 1), page_address);
   wait_until_not_busy();
   nandlog_port_transfer_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
   nandlog_port_transfer_write(COMMAND_BLOCK_ERASE, NULL, 0, page_address, sizeof(page_address));
   wait_until_not_busy();
   return !(read_register(STATUS_REGISTER_3) & STATUS_ERASE_FAILURE);
}

static void set_ecc(bool enabled)
{
   const uint8_t value = enabled ? (uint8_t)(saved_status_2 | ECC_ENABLE_BIT)
                                 : (uint8_t)(saved_status_2 & ~ECC_ENABLE_BIT);
   write_register(STATUS_REGISTER_2, value);
}


// Step 1 -- what the part says about itself -------------------------------------------------------------------------------

static bool step_1_registers(void)
{
   print("\n--- STEP 1: status registers as shipped ---\n");
   nandlog_port_write_enable(true);
   const uint8_t sr1 = read_register(STATUS_REGISTER_1);
   saved_status_2 = read_register(STATUS_REGISTER_2);
   const uint8_t sr3 = read_register(STATUS_REGISTER_3);
   print("  SR1 (protection)   : %02X\n", sr1);
   print("  SR2 (configuration): %02X\n", saved_status_2);
   print("  SR3 (status)       : %02X\n", sr3);
   print("  ECC-E (bit 4 of SR2): %s\n", (saved_status_2 & ECC_ENABLE_BIT) ? "SET -- on-die ECC is on" : "CLEAR");

   ++hw_checks_run;
   if (!(saved_status_2 & ECC_ENABLE_BIT))
   {
      ++hw_checks_failed;
      print("  FAIL: ECC is not enabled, which is not how the driver configures the part.\n"
            "        Everything below assumes the shipping configuration; stop here.\n");
      return false;
   }

   print("\n--- STEP 2: can ECC be turned off and back on? ---\n");
   set_ecc(false);
   const uint8_t with_ecc_off = read_register(STATUS_REGISTER_2);
   print("  SR2 with ECC-E cleared: %02X\n", with_ecc_off);
   ++hw_checks_run;
   const bool can_disable = !(with_ecc_off & ECC_ENABLE_BIT);
   if (!can_disable)
   {
      ++hw_checks_failed;
      print("  FAIL: the part refused to clear ECC-E.\n"
            "        *** THIS IS THE ANSWER: spare-area bad-block marking is NOT possible on this part. ***\n"
            "        *** Keep the persisted bad-block table. Nothing below will run.                    ***\n");
   }
   set_ecc(true);
   const uint8_t restored = read_register(STATUS_REGISTER_2);
   print("  SR2 restored          : %02X %s\n", restored, (restored == saved_status_2) ? "(matches)" : "(DOES NOT MATCH)");
   return can_disable;
}


// Step 0 -- did a marker from a previous run survive the power cycle? --------------------------------------------------------
//
// Runs before anything is erased, and it is the whole reason the test asks to be run twice. The marker block
// is deliberately left programmed at the end of a run so the next boot has something to find

static void step_0_check_previous_run(void)
{
   print("\n--- STEP 0: a marker left by a previous run ---\n");
   marker_page = hw_scratch_page(3);
   saved_status_2 = read_register(STATUS_REGISTER_2);

   uint8_t with_ecc = 0xFF, without_ecc = 0xFF;
   const uint8_t status = latch_and_read(&with_ecc, marker_page, PAGE_BYTES, 1);
   set_ecc(false);
   latch_and_read(&without_ecc, marker_page, PAGE_BYTES, 1);
   set_ecc(true);

   print("  spare byte 0, ECC on : %02X   (SR3 %02X, fatal %s)\n", with_ecc, status,
         (status & STATUS_PAGE_FATAL_ERROR) ? "SET" : "clear");
   print("  spare byte 0, ECC off: %02X\n", without_ecc);

   if (without_ecc == 0x00)
   {
      persistence_confirmed = true;
      print("  => A MARKER SURVIVED A POWER CYCLE. This is the result that matters most.\n");
      print("     The marker will be cleared at the end of this run, so it does not sit on the part\n"
            "     for good and turn up in every later bad-block survey.\n");
   }
   else if (without_ecc == 0xFF)
      print("  => nothing there. First run, or the previous run did not reach step 3.\n");
   else
      print("  => something is there, but not the marker this test writes.\n");
}


// Step 3 -- program a marker into the spare area ----------------------------------------------------------------------------

static void step_3_program_the_marker(void)
{
   print("\n--- STEP 3: program a 0x00 marker into the spare area, ECC off ---\n");
   marker_page = hw_scratch_page(3);
   print("  using scratch block %u (page %u), which the log never writes to\n",
         marker_page / nandlog_chip_geometry()->pages_per_block, marker_page);

   write_register(STATUS_REGISTER_1, PROTECT_NONE);
   HW_CHECK(erase_block(marker_page), "the scratch block would not erase");

   uint8_t before = 0;
   latch_and_read(&before, marker_page, PAGE_BYTES, 1);
   print("  spare byte 0 after erase: %02X %s\n", before, (before == 0xFF) ? "" : "(NOT ERASED -- unexpected)");

   set_ecc(false);
   static const uint8_t marker = 0x00;
   const bool programmed = program_at_column(marker_page, PAGE_BYTES, &marker, 1, true);
   HW_CHECK(programmed, "programming the marker byte reported a write failure");

   uint8_t after = 0xFF;
   const uint8_t status = latch_and_read(&after, marker_page, PAGE_BYTES, 1);
   print("  spare byte 0, ECC OFF   : %02X   (SR3 %02X, fatal %s)\n", after, status,
         (status & STATUS_PAGE_FATAL_ERROR) ? "SET" : "clear");
   HW_CHECK(after == 0x00, "the marker did not take: read %02X, expected 00", after);
   set_ecc(true);
}


// Step 4 -- is the marker visible with ECC back on? ----------------------------------------------------------------------------

static void step_4_read_with_ecc_on(const char *when)
{
   uint8_t marker = 0xFF;
   const uint8_t spare_status = latch_and_read(&marker, marker_page, PAGE_BYTES, 1);
   uint8_t first_main = 0;
   const uint8_t main_status = latch_and_read(&first_main, marker_page, 0, 1);

   print("  [%s] spare byte 0, ECC ON: %02X   (SR3 %02X, fatal %s)\n", when, marker, spare_status,
         (spare_status & STATUS_PAGE_FATAL_ERROR) ? "SET" : "clear");
   print("  [%s] main array byte 0   : %02X   (SR3 %02X, fatal %s)\n", when, first_main, main_status,
         (main_status & STATUS_PAGE_FATAL_ERROR) ? "SET" : "clear");

   // Either outcome is usable, but they mean different things, so say which one happened
   if (marker == 0x00)
      print("  [%s] => the marker is DIRECTLY VISIBLE with ECC on. The scan can read it as it reads\n"
            "           a factory marker, and the redesign works exactly as drawn.\n", when);
   else if (main_status & STATUS_PAGE_FATAL_ERROR)
      print("  [%s] => the marker is NOT visible, but the page now reports an uncorrectable error.\n"
            "           That is still usable: the scan already treats an unreadable page 0 as a bad block.\n", when);
   else
      print("  [%s] => the marker is NOT visible and the page reads clean.\n"
            "           *** THIS IS A NEGATIVE RESULT: a retired block would be indistinguishable from a\n"
            "           *** good one after a reboot. Spare-area marking cannot work this way on this part.\n", when);
}


// Step 5 -- NOP: can the spare be programmed after the main array has been? -------------------------------------------------

static void step_5_second_partial_program(void)
{
   print("\n--- STEP 5: a second partial program of the same page (the NOP question) ---\n");
   print("     A block is retired because it just REFUSED A PROGRAM, so the marker often has to go into a\n"
         "     page that already has data in it. If the part allows only one partial program per page, it\n"
         "     cannot, and the marker has to live on an untouched page instead.\n");

   const uint32_t nop_page = hw_scratch_page(2);
   HW_CHECK(erase_block(nop_page), "could not erase a block for the NOP test");

   // First partial program: a few bytes of the main array
   static const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
   set_ecc(false);
   const bool first = program_at_column(nop_page, 0, data, sizeof(data), true);
   print("  first partial program (main array) : %s\n", first ? "accepted" : "REFUSED");

   // Second partial program: the marker byte in the spare area of the same page
   static const uint8_t marker = 0x00;
   const bool second = program_at_column(nop_page, PAGE_BYTES, &marker, 1, false);
   print("  second partial program (spare area): %s\n", second ? "accepted" : "REFUSED");

   uint8_t read_back = 0xFF;
   latch_and_read(&read_back, nop_page, PAGE_BYTES, 1);
   print("  spare byte 0 afterwards, ECC OFF   : %02X\n", read_back);
   set_ecc(true);

   if (second && (read_back == 0x00))
      print("  => NOP is at least 2. A marker can be written into a page that already holds data.\n");
   else
      print("  => NOP appears to be 1. The marker must go on a page the block has not used, or the\n"
            "     retired block has to be erased first -- which a failing block may refuse.\n");
}


// Step 6 -- how many blocks does the factory say are bad? --------------------------------------------------------------------

static void step_6_factory_survey(void)
{
   print("\n--- STEP 6: factory bad-block survey and full-scan cost ---\n");
   print("     (run before anything is programmed, so the survey counts only the factory's markers)\n");
   const uint32_t blocks = nandlog_chip_geometry()->block_count;
   const uint32_t pages_per_block = nandlog_chip_geometry()->pages_per_block;
   uint32_t bad = 0, unreadable = 0;

   const uint32_t start = hw_cycle_count();
   for (uint32_t block = 0; block < blocks; ++block)
   {
      uint8_t marker = 0xFF;
      const uint8_t status = latch_and_read(&marker, block * pages_per_block, PAGE_BYTES, 1);
      if (status & STATUS_PAGE_FATAL_ERROR)
         ++unreadable;
      else if (marker != 0xFF)
         ++bad;
   }
   const uint32_t elapsed_us = hw_elapsed_us(start, hw_cycle_count());

   print("  %u blocks scanned in %u us (%u us each)\n", blocks, elapsed_us, blocks ? (elapsed_us / blocks) : 0);
   print("  factory-marked bad: %u    page 0 unreadable: %u    total unusable: %u (%u.%02u%% of the array)\n",
         bad, unreadable, bad + unreadable,
         (10000 * (bad + unreadable)) / blocks / 100, (10000 * (bad + unreadable)) / blocks % 100);
   print("\n  *** THIS IS THE DECIDING NUMBER: %u ms per boot is what a scheme with no persisted\n"
         "  *** bad-block table would spend, on every boot, to rebuild it from the spare area.\n", elapsed_us / 1000);
   print("  For comparison, the reserve it would let us reclaim is %u blocks (%u.%02u%% of the array).\n",
         nandlog_chip_geometry()->reserved_blocks,
         (10000 * nandlog_chip_geometry()->reserved_blocks) / blocks / 100,
         (10000 * nandlog_chip_geometry()->reserved_blocks) / blocks % 100);
}


static void clean_up(void)
{
   // Everything except the marker block, which is left programmed on purpose: it is what the next boot's
   // step 0 looks for, and erasing it here would throw away the power-cycle result the test exists to get
   // On the first run the marker is left behind on purpose, because it is what the next boot's step 0
   // looks for. Once step 0 has found one, the question is answered and leaving another would only
   // pollute the array: a programmed spare byte is indistinguishable from a factory bad-block marker
   print("\n--- Cleaning up ---\n");
   set_ecc(true);
   write_register(STATUS_REGISTER_1, PROTECT_NONE);
   for (uint32_t i = 0; i < HW_SCRATCH_BLOCKS; ++i)
      if (persistence_confirmed || (hw_scratch_page(i) != marker_page))
         erase_block(hw_scratch_page(i));
   write_register(STATUS_REGISTER_2, saved_status_2);
   nandlog_port_transfer_write(COMMAND_WRITE_DISABLE, NULL, 0, NULL, 0);
   print("  SR2 restored to %02X; marker block %u %s\n", read_register(STATUS_REGISTER_2),
         marker_page / nandlog_chip_geometry()->pages_per_block,
         persistence_confirmed ? "ERASED -- the array is back as it was" : "left programmed for the next run");
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
   nandlog_begin_session();

   print("\n============================================================\n");
   print("nandlog on-device test 3: spare-area bad-block marking\n");
   print("THIS TEST WRITES TO THE TOP BLOCKS OF THE RESERVE.\n");
   print("============================================================\n");

   step_0_check_previous_run();
   if (step_1_registers())
   {
      // The survey runs first: steps 3 and 5 program markers of their own, and a survey taken
      // afterwards counts them as factory-bad blocks
      step_6_factory_survey();
      step_3_program_the_marker();
      print("\n--- STEP 4: is the marker visible with ECC back on? ---\n");
      step_4_read_with_ecc_on("now");
      step_5_second_partial_program();

      clean_up();
      if (!persistence_confirmed)
      {
         print("\n============================================================\n");
         print("POWER-CYCLE THE BOARD, THEN RE-FLASH AND RUN THIS AGAIN.\n");
         print("Re-flashing is fine: it rewrites the MCU, not the NAND, and\n");
         print("STEP 0 runs before anything on the part is erased.\n");
         print("STEP 0 of that run is the answer.\n");
         print("============================================================\n");
      }
      else
         print("\nNothing further to run: the power-cycle question is answered and the part is clean.\n");
   }

   HW_REPORT("TEST 3: SPARE-AREA MARKING");
   nandlog_end_session();

   while (true)
      am_hal_delay_us(1000000);
}
