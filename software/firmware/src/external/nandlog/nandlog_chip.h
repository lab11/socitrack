#ifndef __NANDLOG_CHIP_HEADER_H__
#define __NANDLOG_CHIP_HEADER_H__

#include <stdbool.h>
#include <stdint.h>
#include "nandlog_conf.h"


// What the part is, as reported by the driver that knows. Valid at any time, including before
// nandlog_chip_init(), because these are properties of the silicon rather than of its state
typedef struct
{
   uint32_t page_size_bytes;     // bytes in a page's main array, excluding the spare area
   uint32_t spare_size_bytes;    // spare/ECC bytes that follow it
   uint32_t pages_per_block;     // erase granularity, in pages; always a power of two
   uint32_t block_count;         // blocks in the whole array
   uint32_t reserved_blocks;     // blocks at the top of the array the driver keeps for bad-block management
} nandlog_geometry_t;

const nandlog_geometry_t *nandlog_chip_geometry(void);

// Confirm the part is present and answering. Safe to call before nandlog_chip_init()
bool nandlog_chip_probe(void);

// Bring the chip into service: configure its status registers, load the persisted bad-block table or,
// on a first boot, build one from the factory markers and persist it. Call once, after nandlog_chip_probe()
void nandlog_chip_init(void);

// Enter or leave the chip's lowest-power state
void nandlog_chip_low_power(bool sleep);

// Read a whole page. False means the read reported an uncorrectable ECC error and the buffer is untrustworthy
bool nandlog_chip_read_page(uint8_t *buffer, uint32_t page);

// Program a whole page, retrying up to MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL times. False means the chip
// reported a program failure every time. Whether that block is then retired is the caller's decision
bool nandlog_chip_write_page(const uint8_t *data, uint32_t page);

// Erase the block containing 'page'. False means the chip reported an erase failure
bool nandlog_chip_erase_block(uint32_t page);

// Whether the block containing 'page' is known bad
bool nandlog_chip_is_bad_block(uint32_t page);

// Retire the block containing 'page'
void nandlog_chip_mark_bad_block(uint32_t page);

// RECOVERY UTILITY. Discard the persisted bad-block table so it is rebuilt on the next boot. Returns whether
// the table was verified gone afterwards, rather than assuming the erase took
bool nandlog_chip_reset_bad_blocks(void);

#endif  // #ifndef __NANDLOG_CHIP_HEADER_H__
