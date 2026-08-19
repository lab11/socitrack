#ifndef __NANDLOG_CONF_HEADER_H__
#define __NANDLOG_CONF_HEADER_H__

// Board-specific pinout declarations
#include "pinout.h"

// Chip geometry
#define MEMORY_PAGES_PER_BLOCK                      64
#define MEMORY_BLOCK_SIZE_BYTES                     (MEMORY_PAGE_SIZE_BYTES * MEMORY_PAGES_PER_BLOCK)
#define MEMORY_PAGE_COUNT                           (MEMORY_PAGES_PER_BLOCK * MEMORY_BLOCK_COUNT)
#define MEMORY_PAGE_WITH_ECC_SIZE_BYTES             (MEMORY_PAGE_SIZE_BYTES + MEMORY_ECC_BYTES_PER_PAGE)
#define MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL      3
#define MEMORY_NUM_ERASE_MARGIN_BLOCKS              16
#define ERASE_AHEAD_BLOCKS                          2
#define ERASE_AHEAD_TRIGGER_PAGE                    (MEMORY_PAGES_PER_BLOCK / 2)

// Largest caller-defined metadata blob stored alongside the log
#define STORAGE_MAX_METADATA_BYTES                  512

// How long to wait for the chip to clear BUSY after a program or erase before declaring it dead
#define STORAGE_BUSY_POLL_INTERVAL_US               10
#define STORAGE_BUSY_TIMEOUT_MS                     500
#define STORAGE_BUSY_TIMEOUT_POLLS                  ((1000 * STORAGE_BUSY_TIMEOUT_MS) / STORAGE_BUSY_POLL_INTERVAL_US)

#endif  // #ifndef __NANDLOG_CONF_HEADER_H__
