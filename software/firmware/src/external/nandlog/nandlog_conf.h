#ifndef __NANDLOG_CONF_HEADER_H__
#define __NANDLOG_CONF_HEADER_H__

// Hardware Presence ---------------------------------------------------------------------------------------------------

// Whether a NAND part is fitted in this build. When it is not, the library compiles to stand-ins that accept
// everything and return nothing, so the application above it needs no conditionals of its own. This is the
// one place the host's own build flags are translated into the library's vocabulary
#if defined(_TEST_NO_STORAGE)
#define NANDLOG_HAS_HARDWARE                        0
#else
#define NANDLOG_HAS_HARDWARE                        1
#endif


// Static Sizing Budget ------------------------------------------------------------------------------------------------

// The largest page and spare area the host is prepared to set aside RAM for. Buffers throughout the library
// and the application above it are sized from these, which is what keeps every allocation static. A part
// whose page exceeds the budget is a compile error inside that part's driver, so the only cost of setting
// these larger than the fitted part needs is unused RAM
#define NANDLOG_MAX_PAGE_SIZE_BYTES                 4096
#define NANDLOG_MAX_SPARE_SIZE_BYTES                256


// Log Policy ----------------------------------------------------------------------------------------------------------

// Program or erase attempts on one block before it is retired as bad
#define NANDLOG_BLOCK_ERRORS_BEFORE_REMOVAL         3

// Blocks kept erased ahead of the write head, so a page write never waits on an erase
#define NANDLOG_ERASE_AHEAD_BLOCKS                  2

// Largest caller-defined metadata blob stored alongside the log
#define NANDLOG_MAX_METADATA_BYTES                  512

// A record whose timestamp steps backwards by no more than this is treated as writer disagreement and
// pulled forward, rather than as the time base having moved and the page having to be committed
#define NANDLOG_TIMESTAMP_TOLERANCE_MS              250

// Whether each record carries its own length so that a reader can walk a page without knowing the
// application's record types
#define NANDLOG_RECORD_FRAMING                      0

// How long to wait for the chip to clear BUSY after a program or erase before declaring it dead
#define NANDLOG_BUSY_POLL_INTERVAL_US               10
#define NANDLOG_BUSY_TIMEOUT_MS                     500

#endif  // #ifndef __NANDLOG_CONF_HEADER_H__
