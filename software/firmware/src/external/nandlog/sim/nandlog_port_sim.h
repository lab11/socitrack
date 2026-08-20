#ifndef __NANDLOG_PORT_SIM_HEADER_H__
#define __NANDLOG_PORT_SIM_HEADER_H__

// Control surface for the RAM-backed port. Only host tests include this; the log and the chip driver see
// nothing but nandlog_port.h, and neither is aware it is not talking to real hardware.

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

#define NANDLOG_SIM_MAX_PAGE_WITH_SPARE             (8192 + 512)
#define NANDLOG_SIM_MAX_FAULT_BLOCKS                32


// What the emulated part should refuse to do. Blocks are block numbers, not page addresses
typedef struct
{
   uint8_t device_id[3];                                       // what READ_DEVICE_ID reports
   uint32_t pages_per_block;                                   // set from the chip's geometry by create()
   uint32_t unwritable_blocks[NANDLOG_SIM_MAX_FAULT_BLOCKS];   // program always reports failure
   uint32_t num_unwritable_blocks;
   uint32_t unerasable_blocks[NANDLOG_SIM_MAX_FAULT_BLOCKS];   // erase always reports failure
   uint32_t num_unerasable_blocks;
   bool stay_busy;                                             // never clears BUSY, to exercise the timeout
} nandlog_sim_faults_t;

typedef struct
{
   uint32_t spi_reads, spi_writes;
   uint32_t page_reads, page_writes, block_erases;
} nandlog_sim_counters_t;


// Build a part matching the geometry the compiled chip driver reports, so the two can never disagree. Only
// the identity has to be supplied, because it is the one thing geometry does not carry
void nandlog_sim_create(const uint8_t *device_id, uint32_t device_id_length);
void nandlog_sim_destroy(void);

// Fault injection. Passing a zeroed structure clears everything
void nandlog_sim_set_faults(const nandlog_sim_faults_t *faults);

// Cut the n-th subsequent program or erase short, leaving half of it applied, as losing power would. Zero
// disables. nandlog_sim_power_failed() reports whether it has fired yet
void nandlog_sim_power_fail_after(uint32_t operations);
bool nandlog_sim_power_failed(void);

// Rot bits in place. Only clearing is possible without an erase, so 'mask' names bits to drive to zero
void nandlog_sim_corrupt(uint32_t page, uint32_t offset, uint8_t mask);
uint8_t *nandlog_sim_raw_page(uint32_t page);

// nandlog_port_fatal() aborts the process unless a fatal is expected, in which case it longjmps to this
// buffer. A test arms it with  if (setjmp(*nandlog_sim_fatal_jump()) == 0) { ...expect a fault... }
void nandlog_sim_expect_fatal(bool expected);
bool nandlog_sim_fatal_seen(const char **reason);
jmp_buf *nandlog_sim_fatal_jump(void);

// Write a range of the emulated part out byte for byte, so the reference parser can be run against something
// a device would actually have produced. A whole part is gigabytes, so tests dump the region they used
bool nandlog_sim_dump(const char *path, uint32_t first_page, uint32_t num_pages);

nandlog_sim_counters_t nandlog_sim_counters(void);
void nandlog_sim_set_verbose(bool on);

#endif  // #ifndef __NANDLOG_PORT_SIM_HEADER_H__
