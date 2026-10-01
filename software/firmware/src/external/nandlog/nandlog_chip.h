#ifndef __NANDLOG_CHIP_HEADER_H__
#define __NANDLOG_CHIP_HEADER_H__

#include <stdbool.h>
#include <stdint.h>
#include "nandlog_conf.h"


// Integrator Configuration Check --------------------------------------------------------------------------------------

#ifndef NANDLOG_MAX_PAGE_SIZE_BYTES
#error "nandlog_conf.h must define NANDLOG_MAX_PAGE_SIZE_BYTES"
#endif
#ifndef NANDLOG_MAX_SPARE_SIZE_BYTES
#error "nandlog_conf.h must define NANDLOG_MAX_SPARE_SIZE_BYTES"
#endif
#ifndef NANDLOG_HAS_HARDWARE
#error "nandlog_conf.h must define NANDLOG_HAS_HARDWARE"
#endif
#ifndef NANDLOG_BLOCK_ERRORS_BEFORE_REMOVAL
#error "nandlog_conf.h must define NANDLOG_BLOCK_ERRORS_BEFORE_REMOVAL"
#endif
#ifndef NANDLOG_PAGE_PLACEMENT_ATTEMPTS
#error "nandlog_conf.h must define NANDLOG_PAGE_PLACEMENT_ATTEMPTS"
#endif
#ifndef NANDLOG_ERASE_AHEAD_BLOCKS
#error "nandlog_conf.h must define NANDLOG_ERASE_AHEAD_BLOCKS"
#endif
#ifndef NANDLOG_MAX_EPOCH_DETAILS_BYTES
#error "nandlog_conf.h must define NANDLOG_MAX_EPOCH_DETAILS_BYTES"
#endif
#ifndef NANDLOG_TIMESTAMP_TOLERANCE_MS
#error "nandlog_conf.h must define NANDLOG_TIMESTAMP_TOLERANCE_MS"
#endif
#ifndef NANDLOG_BUSY_POLL_INTERVAL_US
#error "nandlog_conf.h must define NANDLOG_BUSY_POLL_INTERVAL_US"
#endif
#ifndef NANDLOG_BUSY_TIMEOUT_MS
#error "nandlog_conf.h must define NANDLOG_BUSY_TIMEOUT_MS"
#endif
#ifndef NANDLOG_CHIP_PAGE_COPY
#error "nandlog_conf.h must define NANDLOG_CHIP_PAGE_COPY"
#endif


// Nandlog Chip Type Definitions ---------------------------------------------------------------------------------------

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

// What a part made of a request to relocate a page without the host handling its contents
typedef enum
{
   NANDLOG_COPY_OK,              // the part did it; nothing but addresses crossed the bus
   NANDLOG_COPY_FAILED,          // the part tried and reported a program failure, so the block is suspect
   NANDLOG_COPY_UNSUPPORTED      // the part has no such command; the caller must read and write it itself
} nandlog_copy_result_t;


// Chip-Specific Required Functions ------------------------------------------------------------------------------------

// Get the geometry of the NAND chip
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

// Read 'length' bytes of a page starting at 'offset'. Same return meaning as nandlog_chip_read_page(), which
// is this function over the whole page.
bool nandlog_chip_read_page_region(uint8_t *buffer, uint32_t page, uint32_t offset, uint32_t length);

// Program a whole page, retrying up to NANDLOG_BLOCK_ERRORS_BEFORE_REMOVAL times. False means the chip
// reported a program failure every time. Whether that block is then retired is the caller's decision
bool nandlog_chip_write_page(const uint8_t *data, uint32_t page);

// Erase the block containing 'page'. False means the chip reported an erase failure
bool nandlog_chip_erase_block(uint32_t page);

// Relocate one page to another in the chip rather than through the host. A part with no such command answers
// NANDLOG_COPY_UNSUPPORTED unconditionally and without touching the bus, and the caller falls back to a
// read and a write. Whether the block is then retired stays the caller's decision, exactly as for a write
nandlog_copy_result_t nandlog_chip_copy_page(uint32_t source_page, uint32_t destination_page);

// Whether the block containing 'page' is known bad
bool nandlog_chip_is_bad_block(uint32_t page);

// Retire the block containing 'page'
void nandlog_chip_mark_bad_block(uint32_t page);

// RECOVERY UTILITY. Discard the persisted bad-block table so it is rebuilt on the next boot. Returns whether
// the table was verified gone afterwards, rather than assuming the erase took
bool nandlog_chip_reset_bad_blocks(void);

#endif  // #ifndef __NANDLOG_CHIP_HEADER_H__
