#ifndef __STORAGE_HEADER_H__
#define __STORAGE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "nandlog.h"


// On-Flash and Wire Formats -------------------------------------------------------------------------------------------

typedef nandlog_page_header_t                       storage_page_header_t;
typedef nandlog_meta_header_t                       storage_meta_header_t;
typedef nandlog_stream_header_t                     storage_stream_header_t;
typedef nandlog_wire_page_t                         storage_wire_page_t;

#define STORAGE_FORMAT_VERSION                      NANDLOG_FORMAT_VERSION
#define STORAGE_PAGE_MAGIC                          NANDLOG_PAGE_MAGIC
#define STORAGE_META_MAGIC                          NANDLOG_META_MAGIC
#define STORAGE_STREAM_MAGIC                        NANDLOG_STREAM_MAGIC
#define STORAGE_NO_TIMESTAMP                        NANDLOG_NO_TIMESTAMP
#define STORAGE_PAGE_HEADER_CRC_BYTES               NANDLOG_PAGE_HEADER_CRC_BYTES
#define STORAGE_META_HEADER_CRC_BYTES               NANDLOG_META_HEADER_CRC_BYTES
#define STORAGE_MAX_RETRANSMIT_PAGES                NANDLOG_MAX_RETRANSMIT_PAGES
#define STORAGE_MAX_DATA_BYTES_PER_PAGE             NANDLOG_MAX_DATA_BYTES_PER_PAGE
#define STORAGE_MAX_METADATA_BYTES                  NANDLOG_MAX_METADATA_BYTES
#define STORAGE_TIMESTAMP_TOLERANCE_MS              NANDLOG_TIMESTAMP_TOLERANCE_MS


// Public API Functions ------------------------------------------------------------------------------------------------

bool storage_init(void);
void storage_deinit(void);
void storage_disable(bool disable);
void storage_reset_bad_block_table(void);
uint32_t storage_data_bytes_per_page(void);
bool storage_store_metadata(const void *blob, uint16_t length);
void storage_retrieve_metadata(void *blob, uint16_t length);
void storage_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length);
void storage_flush(bool write_partial_pages);
bool storage_has_buffered_data(void);
void storage_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp);
void storage_end_reading(void);
void storage_enter_maintenance_mode(void);
void storage_exit_maintenance_mode(void);
uint32_t storage_retrieve_num_data_chunks(void);
uint32_t storage_retrieve_num_data_bytes(void);
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer);
uint32_t storage_retrieve_next_page(uint8_t *buffer, storage_page_header_t *header);
uint32_t storage_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, storage_page_header_t *header);
uint32_t storage_read_recent_page(uint32_t pages_back, uint8_t *buffer, storage_page_header_t *header, bool *end_of_epoch);
void storage_retransmit_clear(void);
uint32_t storage_retransmit_add(const uint32_t *seqs, uint32_t count);
uint32_t storage_retransmit_count(void);
uint32_t storage_retransmit_total_bytes(void);
uint32_t storage_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, storage_page_header_t *header);

#endif  // #ifndef __STORAGE_HEADER_H__
