#ifndef __NANDLOG_HW_TEST_HEADER_H__
#define __NANDLOG_HW_TEST_HEADER_H__

// Shared scaffolding for the on-device tests.
//
// These tests exist because the host simulator cannot answer three kinds of question: what the real part
// does with a command the simulator only pretends to implement, what its on-die ECC does to bytes the log
// wants to read or write, and how long any of it takes. Everything here is about one of those three.
//
// The tests reach past nandlog.h into the chip driver on purpose. They are checking the driver against the
// silicon, not the log against the driver, so they call the same internals the driver calls.

#include <stdbool.h>
#include <stdint.h>
#include "nandlog.h"
#include "nandlog_chip.h"
#include "nandlog_port.h"
#include "logging.h"
#include "system.h"


// Result Tracking -----------------------------------------------------------------------------------------------------

extern uint32_t hw_checks_run, hw_checks_failed;

#define HW_CHECK(cond, ...)  do {                                           \
      ++hw_checks_run;                                                      \
      if (!(cond)) { ++hw_checks_failed; print("  FAIL: "); print(__VA_ARGS__); print("\n"); } \
   } while (0)

#define HW_REPORT(name)      do {                                           \
      print("\n=== %s: %u checks, %u failed [%s] ===\n", (name), hw_checks_run, hw_checks_failed, \
            hw_checks_failed ? "FAILED" : "PASSED");                        \
   } while (0)


// Cycle-Accurate Timing -----------------------------------------------------------------------------------------------

// DWT is gated without a debugger on some parts, so the counter is enabled explicitly and then calibrated
// against a known delay rather than against an assumed core clock. That doubles as a liveness check: a
// calibration of zero means DWT is not counting and every timing figure below would be a fiction
void hw_timing_init(void);
uint32_t hw_cycles_per_us(void);

static inline uint32_t hw_cycle_count(void)
{
   return *(volatile uint32_t*)0xE0001004;   // DWT_CYCCNT
}

// Microseconds between two cycle counts, tolerating the counter wrapping once
uint32_t hw_elapsed_us(uint32_t start_cycles, uint32_t end_cycles);


// Scratch Area --------------------------------------------------------------------------------------------------------

// Tests that program and erase need somewhere to do it that the log will never place a page in. The top of
// the reserve is the only such place: the log region stops below it, and the chip driver only ever writes
// its bad-block marker into the FIRST usable block of the reserve, so the last blocks are unclaimed.
//
// Returns the first page of the n-th scratch block, counting down from the top of the array
uint32_t hw_scratch_page(uint32_t block_index);

#define HW_SCRATCH_BLOCKS                           4

// Fill a buffer with a pattern that is neither constant nor 0xFF, so a read that returns an erased page, a
// stale page, or a half-programmed one all look different from a correct one
void hw_fill_pattern(uint8_t *buffer, uint32_t length, uint32_t seed);
bool hw_check_pattern(const uint8_t *buffer, uint32_t length, uint32_t seed);

#endif  // #ifndef __NANDLOG_HW_TEST_HEADER_H__
