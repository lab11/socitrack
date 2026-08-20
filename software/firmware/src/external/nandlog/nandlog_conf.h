#ifndef __NANDLOG_CONF_HEADER_H__
#define __NANDLOG_CONF_HEADER_H__

// Static Sizing Budget ------------------------------------------------------------------------------------------------

#define NANDLOG_MAX_PAGE_SIZE_BYTES                 4096
#define NANDLOG_MAX_SPARE_SIZE_BYTES                256
#define NANDLOG_MAX_PAGE_WITH_SPARE_SIZE_BYTES      (NANDLOG_MAX_PAGE_SIZE_BYTES + NANDLOG_MAX_SPARE_SIZE_BYTES)


// Log Policy ----------------------------------------------------------------------------------------------------------

#define MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL      3
#define MEMORY_NUM_ERASE_MARGIN_BLOCKS              16
#define ERASE_AHEAD_BLOCKS                          2

// Largest caller-defined metadata blob stored alongside the log
#define STORAGE_MAX_METADATA_BYTES                  512

// How long to wait for the chip to clear BUSY after a program or erase before declaring it dead
#define STORAGE_BUSY_POLL_INTERVAL_US               10
#define STORAGE_BUSY_TIMEOUT_MS                     500
#define STORAGE_BUSY_TIMEOUT_POLLS                  ((1000 * STORAGE_BUSY_TIMEOUT_MS) / STORAGE_BUSY_POLL_INTERVAL_US)

#endif  // #ifndef __NANDLOG_CONF_HEADER_H__
