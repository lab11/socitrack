#ifndef __NANDLOG_CONF_HEADER_H__
#define __NANDLOG_CONF_HEADER_H__

// Static Sizing Budget ------------------------------------------------------------------------------------------------

#define NANDLOG_MAX_PAGE_SIZE_BYTES                 4096
#define NANDLOG_MAX_SPARE_SIZE_BYTES                256


// Log Policy ----------------------------------------------------------------------------------------------------------

// Program or erase attempts on one block before it is retired as bad
#define MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL      3

// Blocks kept erased ahead of the write head, so a page write never waits on an erase
#define ERASE_AHEAD_BLOCKS                          2

// Largest caller-defined metadata blob stored alongside the log
#define STORAGE_MAX_METADATA_BYTES                  512

// How long to wait for the chip to clear BUSY after a program or erase before declaring it dead
#define STORAGE_BUSY_POLL_INTERVAL_US               10
#define STORAGE_BUSY_TIMEOUT_MS                     500

#endif  // #ifndef __NANDLOG_CONF_HEADER_H__
