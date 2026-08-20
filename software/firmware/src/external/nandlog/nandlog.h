#ifndef __NANDLOG_HEADER_H__
#define __NANDLOG_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include "nandlog_conf.h"


// On-Flash Page Format Defintions -------------------------------------------------------------------------------------

#define NANDLOG_FORMAT_VERSION                      1
#define NANDLOG_PAGE_MAGIC                          0x31505454   // 'TTP1', little-endian
#define NANDLOG_NO_TIMESTAMP                        0xFFFFFFFF

typedef struct __attribute__ ((__packed__))
{
   uint32_t magic;               // NANDLOG_PAGE_MAGIC
   uint32_t epoch;               // experiment generation; makes stale data from a prior run unmistakable
   uint32_t seq;                 // page index within the epoch, starting at 0
   uint32_t first_timestamp;     // experiment-relative ms of the first record, or NANDLOG_NO_TIMESTAMP
   uint32_t last_timestamp;      // experiment-relative ms of the last record, or NANDLOG_NO_TIMESTAMP
   uint16_t payload_length;      // valid payload bytes
   uint16_t record_count;        // complete records in the payload
   uint32_t payload_crc;         // CRC-32 over payload[0 .. payload_length)
   uint32_t header_crc;          // CRC-32 over this header up to (not including) this field
} nandlog_page_header_t;

#define NANDLOG_PAGE_HEADER_CRC_BYTES               (sizeof(nandlog_page_header_t) - sizeof(uint32_t))
#define NANDLOG_META_MAGIC                          0x314D5454   // 'TTM1', little-endian

typedef struct __attribute__ ((__packed__))
{
   uint32_t magic;               // NANDLOG_META_MAGIC
   uint32_t epoch;               // the experiment generation this page describes
   uint32_t log_start_page;      // physical page holding seq 0 for this epoch
   uint32_t created_timestamp;   // RTC timestamp at creation
   uint16_t details_length;      // bytes of caller-defined metadata following this header
   uint16_t format_version;      // NANDLOG_FORMAT_VERSION
   uint32_t details_crc;         // CRC-32 over the experiment details blob that follows the header
   uint32_t header_crc;          // CRC-32 over this header up to (not including) this field
   uint32_t reserved;
} nandlog_meta_header_t;

#define NANDLOG_MAX_DATA_BYTES_PER_PAGE             (NANDLOG_MAX_PAGE_SIZE_BYTES - sizeof(nandlog_page_header_t))
#define NANDLOG_META_HEADER_CRC_BYTES               (sizeof(nandlog_meta_header_t) - (2 * sizeof(uint32_t)))


// Offload Wire Format -------------------------------------------------------------------------------------------------

#define NANDLOG_STREAM_MAGIC                        0x31535454   // 'TTS1', little-endian

typedef struct __attribute__ ((__packed__))
{
   uint32_t magic;                  // NANDLOG_STREAM_MAGIC
   uint16_t format_version;         // NANDLOG_FORMAT_VERSION
   uint16_t details_length;         // bytes of caller-defined metadata following this header
   uint32_t total_pages;            // pages that will be sent, including any zero-length gaps
   uint32_t total_payload_bytes;    // sum of all payload lengths
} nandlog_stream_header_t;

#define NANDLOG_MAX_RETRANSMIT_PAGES                256

typedef struct __attribute__ ((__packed__))
{
   uint32_t seq;                    // page sequence within the epoch; identifies a gap for retransmission
   uint32_t first_timestamp;        // experiment-relative ms, or NANDLOG_NO_TIMESTAMP
   uint32_t last_timestamp;         // experiment-relative ms, or NANDLOG_NO_TIMESTAMP
   uint16_t payload_length;         // 0 means the device could not read this page
   uint16_t record_count;
   uint32_t payload_crc;            // CRC-32 over the payload bytes that follow
} nandlog_wire_page_t;


// Public API Functions ------------------------------------------------------------------------------------------------

// Confirm a part is present and answering, without bringing the log up or writing anything to it. Intended
// for builds that only test for its presence; nandlog_init() performs the same check for itself
bool nandlog_probe(void);

// Bring the log up: open the port, identify the part, load its bad-block table, then recover the current
// epoch from the metadata ring and locate the write head within it. Safe to call more than once. False means
// the port could not be opened, and no other call should be made
bool nandlog_init(void);

// Release the port. The log can be brought back up with nandlog_init()
void nandlog_deinit(void);

// Payload bytes a page can carry on the fitted part: the page size less the header the log writes into it.
// Only known once nandlog_init() has asked the chip its geometry
uint32_t nandlog_data_bytes_per_page(void);

// Stop or resume accepting records. A disabled log discards what it is given rather than buffering it, and
// still serves reads. Nothing is disabled by nandlog_init()
void nandlog_disable(bool disable);

// RECOVERY UTILITY. Discard the persisted bad-block table so it is rebuilt from the factory markers on the
// next boot. What this can achieve depends on the part: one that remaps in hardware cannot forget
void nandlog_reset_bad_block_table(void);

// Begin a new epoch, described by an opaque caller-defined blob of at most NANDLOG_MAX_METADATA_BYTES. The
// log neither reads nor interprets the blob. Everything logged previously stays on the part but stops being
// reachable, since reads only ever cover the current epoch. Must be called inside a maintenance session, and
// returns false if it was not, if the blob is too large, or if no slot in the metadata ring would take it
bool nandlog_store_metadata(const void *blob, uint16_t length);

// Copy back the blob most recently stored, truncated to 'length'. Zero-fills if the log has no valid
// metadata, so a caller that never stored any reads zeros rather than stale bytes
void nandlog_retrieve_metadata(void *blob, uint16_t length);

// Append one record to the page being assembled in RAM. 'record_type' and 'data' are opaque to the log; the
// timestamp is only ever compared, never interpreted, so its epoch and units are the caller's business.
// Committing a full page happens here, so this call occasionally writes to flash. Records are never split
// across pages, and one larger than a page is dropped
void nandlog_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length);

// Commit what is buffered. A page is written automatically as soon as the next record will not fit, so this
// is only needed to force out a partial page -- a timed flush, or shutdown. Pass false to write only if a
// whole page has accumulated
void nandlog_flush(bool write_partial_pages);

// Whether any records are sitting unwritten in RAM
bool nandlog_has_buffered_data(void);

// Hold the part powered across a run of operations, instead of waking and sleeping it around each one.
// Reading and storing metadata both require a session; ordinary logging does not. Sessions do not nest
void nandlog_enter_maintenance_mode(void);
void nandlog_exit_maintenance_mode(void);

// Open a read over the current epoch, bounded by timestamp. Zero for either bound means "from the beginning"
// and "to the end"; both bounds are resolved here, so nothing downstream has to seek again. Has no effect
// outside a maintenance session. While a read is open, writing is refused
void nandlog_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp);

// Close a read, whether or not it ran to completion. Implied by nandlog_exit_maintenance_mode()
void nandlog_end_reading(void);

// Measure the open read: how many pages it will yield, including any that turn out to be unreadable, and how
// many payload bytes they hold. Both come from one pass, so neither can be taken without the other; either
// pointer may be NULL. Reports zero for both if no read is open
void nandlog_read_span(uint32_t *num_pages, uint32_t *num_bytes);

// Take the next page of the open read, returning its payload length. Pass NULL for 'header' to ignore the
// framing metadata. Zero means that page was unreadable, which is not the end of the read; a page whose
// header could not be read comes back with 'seq' set to its position, so the gap is still identifiable
uint32_t nandlog_retrieve_next_page(uint8_t *buffer, nandlog_page_header_t *header);

// Fetch one page of the current epoch by its sequence number, so a host can re-request what it lost.
// Returns zero, with the header describing the gap, if that page cannot be produced
uint32_t nandlog_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, nandlog_page_header_t *header);

// Walk backwards from the write head, newest page first, independently of any open read. Zero means that
// page could not be read, which is not the end of the log; 'end_of_epoch' is the signal to stop
uint32_t nandlog_read_recent_page(uint32_t pages_back, uint8_t *buffer, nandlog_page_header_t *header, bool *end_of_epoch);

// Accumulate the sequence numbers a host reports missing, across as many calls as it takes, then serve them
// back. nandlog_retransmit_add() returns the number held; anything beyond NANDLOG_MAX_RETRANSMIT_PAGES is
// dropped. An index past the end yields a zero-length page rather than nothing, so the host can tell
// "still missing" from "never answered"
void nandlog_retransmit_clear(void);
uint32_t nandlog_retransmit_add(const uint32_t *seqs, uint32_t count);
uint32_t nandlog_retransmit_count(void);
uint32_t nandlog_retransmit_total_bytes(void);
uint32_t nandlog_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, nandlog_page_header_t *header);

#endif  // #ifndef __NANDLOG_HEADER_H__
