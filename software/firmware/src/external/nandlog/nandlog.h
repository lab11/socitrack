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

// Confirm the part is present without bringing the log up, for builds that only test for its presence
bool nandlog_probe(void);

// Bring the log up: open the port, identify the part, and recover the write head from the metadata ring
bool nandlog_init(void);
void nandlog_deinit(void);

// Payload bytes in a page on the fitted part, and physical pages the current epoch spans
uint32_t nandlog_data_bytes_per_page(void);
uint32_t nandlog_epoch_page_count(void);

// Whether a read session is currently open
bool nandlog_is_reading(void);
void nandlog_disable(bool disable);
void nandlog_reset_bad_block_table(void);
bool nandlog_store_metadata(const void *blob, uint16_t length);
void nandlog_retrieve_metadata(void *blob, uint16_t length);
void nandlog_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length);
void nandlog_flush(bool write_partial_pages);
bool nandlog_has_buffered_data(void);
void nandlog_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp);
void nandlog_end_reading(void);
void nandlog_enter_maintenance_mode(void);
void nandlog_exit_maintenance_mode(void);
uint32_t nandlog_retrieve_num_data_chunks(void);
uint32_t nandlog_retrieve_num_data_bytes(void);
uint32_t nandlog_retrieve_next_data_chunk(uint8_t *buffer);
uint32_t nandlog_retrieve_next_page(uint8_t *buffer, nandlog_page_header_t *header);
uint32_t nandlog_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, nandlog_page_header_t *header);
uint32_t nandlog_read_recent_page(uint32_t pages_back, uint8_t *buffer, nandlog_page_header_t *header, bool *end_of_epoch);
void nandlog_retransmit_clear(void);
uint32_t nandlog_retransmit_add(const uint32_t *seqs, uint32_t count);
uint32_t nandlog_retransmit_count(void);
uint32_t nandlog_retransmit_total_bytes(void);
uint32_t nandlog_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, nandlog_page_header_t *header);

#endif  // #ifndef __NANDLOG_HEADER_H__
