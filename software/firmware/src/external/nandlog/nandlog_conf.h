#ifndef __NANDLOG_CONF_HEADER_H__
#define __NANDLOG_CONF_HEADER_H__

// Hardware Presence ---------------------------------------------------------------------------------------------------

// Whether a NAND part is fitted in this build. When it is not, the library compiles to stand-ins that accept
// everything and return nothing, so the application above it needs no conditionals of its own
#if !defined(NANDLOG_HAS_HARDWARE)
#if defined(_TEST_NO_STORAGE)
#define NANDLOG_HAS_HARDWARE                        0
#else
#define NANDLOG_HAS_HARDWARE                        1
#endif
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

// Blocks a single page write may relocate through before the log concludes the part will not hold it. Each
// attempt costs an erase plus a block's worth of page copies, so this is a bound on real time as much as on
// blocks: it has to be large enough to ride out a genuine cluster of worn blocks and small enough that a part
// which has stopped programming altogether is diagnosed in seconds rather than starving the system
#define NANDLOG_PAGE_PLACEMENT_ATTEMPTS             3

// Largest caller-defined metadata blob stored alongside the log
#define NANDLOG_MAX_METADATA_BYTES                  512

// A record whose timestamp steps backwards by no more than this is treated as writer disagreement and
// pulled forward, rather than as the time base having moved and the page having to be committed
#define NANDLOG_TIMESTAMP_TOLERANCE_MS              250

// Whether each record carries its own length so that a reader can walk a page without knowing the
// application's record types
#if !defined(NANDLOG_RECORD_FRAMING)
#define NANDLOG_RECORD_FRAMING                      0
#endif

// How long to wait for the chip to clear BUSY after a program or erase before declaring it dead
#define NANDLOG_BUSY_POLL_INTERVAL_US               10
#define NANDLOG_BUSY_TIMEOUT_MS                     500

#endif  // #ifndef __NANDLOG_CONF_HEADER_H__
