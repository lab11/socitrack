// Shared scaffolding for the on-device tests. See nandlog_hw_test.h.

#include <string.h>
#include "nandlog_hw_test.h"

uint32_t hw_checks_run, hw_checks_failed;

static uint32_t cycles_per_us;


// Cycle-Accurate Timing -----------------------------------------------------------------------------------------------

void hw_timing_init(void)
{
   volatile uint32_t *demcr = (volatile uint32_t*)0xE000EDFC;
   volatile uint32_t *dwt_control = (volatile uint32_t*)0xE0001000;
   volatile uint32_t *dwt_cyccnt = (volatile uint32_t*)0xE0001004;

   *demcr |= (1UL << 24);          // TRCENA: without this the counter does not run
   *dwt_cyccnt = 0;
   *dwt_control |= 1UL;            // CYCCNTENA

   // Calibrate against a known delay rather than against an assumed core clock, so the figure stays correct
   // across power-mode changes. A calibration of zero means DWT is not counting at all
   const uint32_t start = hw_cycle_count();
   am_hal_delay_us(10000);
   const uint32_t elapsed = hw_cycle_count() - start;
   cycles_per_us = elapsed / 10000;
   if (!cycles_per_us)
      print("*** WARNING: DWT is not counting. Every timing figure below is meaningless. ***\n");
   else
      print("Timing calibrated: %u cycles/us\n", cycles_per_us);
}

uint32_t hw_cycles_per_us(void)
{
   return cycles_per_us;
}

uint32_t hw_elapsed_us(uint32_t start_cycles, uint32_t end_cycles)
{
   const uint32_t elapsed = end_cycles - start_cycles;    // unsigned arithmetic rides one wrap
   return cycles_per_us ? (elapsed / cycles_per_us) : 0;
}


// Scratch Area --------------------------------------------------------------------------------------------------------

uint32_t hw_scratch_page(uint32_t block_index)
{
   // Count down from the top of the array, skipping anything already retired, so a factory-bad block at the
   // very top does not silently turn every program in a test into an expected failure
   const nandlog_geometry_t *geometry = nandlog_chip_geometry();
   uint32_t found = 0;
   for (uint32_t block = geometry->block_count; block-- > (geometry->block_count - geometry->reserved_blocks + 1); )
   {
      const uint32_t page = block * geometry->pages_per_block;
      if (nandlog_chip_is_bad_block(page))
         continue;
      if (found++ == block_index)
         return page;
   }
   print("*** WARNING: fewer than %u good scratch blocks at the top of the reserve ***\n", block_index + 1);
   return (geometry->block_count - 1) * geometry->pages_per_block;
}

void hw_fill_pattern(uint8_t *buffer, uint32_t length, uint32_t seed)
{
   // A simple LCG. Not for randomness -- for a sequence where a page read from the wrong address, or one
   // that stopped part-way, cannot coincidentally look right
   uint32_t state = seed ? seed : 1;
   for (uint32_t i = 0; i < length; ++i)
   {
      state = (1664525u * state) + 1013904223u;
      buffer[i] = (uint8_t)(state >> 24);
   }
}

bool hw_check_pattern(const uint8_t *buffer, uint32_t length, uint32_t seed)
{
   uint32_t state = seed ? seed : 1;
   for (uint32_t i = 0; i < length; ++i)
   {
      state = (1664525u * state) + 1013904223u;
      if (buffer[i] != (uint8_t)(state >> 24))
         return false;
   }
   return true;
}
