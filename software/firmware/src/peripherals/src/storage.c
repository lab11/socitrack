// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "storage.h"


// Lifecycle -----------------------------------------------------------------------------------------------------------

bool storage_init(void)
{
#ifdef _MANUFACTURING_TEST_
   // The manufacturing test only asks whether the part is there; bringing the log up would write to it
   return nandlog_probe();
#else
   return nandlog_init();
#endif
}

void storage_deinit(void) { nandlog_deinit(); }
void storage_disable(bool disable) { nandlog_disable(disable); }
void storage_reset_bad_block_table(void) { nandlog_reset_bad_block_table(); }
uint32_t storage_data_bytes_per_page(void) { return nandlog_data_bytes_per_page(); }


// Writing -------------------------------------------------------------------------------------------------------------

bool storage_store_metadata(const void *blob, uint16_t length) { return nandlog_store_metadata(blob, length); }
void storage_retrieve_metadata(void *blob, uint16_t length) { nandlog_retrieve_metadata(blob, length); }
void storage_flush(bool write_partial_pages) { nandlog_flush(write_partial_pages); }
bool storage_has_buffered_data(void) { return nandlog_has_buffered_data(); }

void storage_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length)
{
   nandlog_store_record(record_type, timestamp, data, data_length);
}


// Reading -------------------------------------------------------------------------------------------------------------

void storage_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp)
{
#ifdef _TEST_IMU_DATA
   // The IMU capture test wants the whole epoch regardless of what it asked for. Passing zero for both bounds
   // is what the log already does when a bound is not supplied, so no special case is needed below it
   (void)starting_timestamp;
   (void)ending_timestamp;
   nandlog_begin_reading(0, 0);
#else
   nandlog_begin_reading(starting_timestamp, ending_timestamp);
#endif
}

uint32_t storage_retrieve_num_data_chunks(void)
{
#ifdef _TEST_IMU_DATA
   // Pages spanned by the epoch, deliberately excluding the partial page still buffered in RAM, and skipping
   // the byte-total pass the normal download needs
   return nandlog_is_reading() ? nandlog_epoch_page_count() : 0;
#else
   return nandlog_retrieve_num_data_chunks();
#endif
}

void storage_end_reading(void) { nandlog_end_reading(); }
void storage_enter_maintenance_mode(void) { nandlog_enter_maintenance_mode(); }
void storage_exit_maintenance_mode(void) { nandlog_exit_maintenance_mode(); }
uint32_t storage_retrieve_num_data_bytes(void) { return nandlog_retrieve_num_data_bytes(); }
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer) { return nandlog_retrieve_next_data_chunk(buffer); }

uint32_t storage_retrieve_next_page(uint8_t *buffer, storage_page_header_t *header)
{
   return nandlog_retrieve_next_page(buffer, header);
}

uint32_t storage_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, storage_page_header_t *header)
{
   return nandlog_retrieve_page_by_seq(seq, buffer, header);
}

uint32_t storage_read_recent_page(uint32_t pages_back, uint8_t *buffer, storage_page_header_t *header, bool *end_of_epoch)
{
   return nandlog_read_recent_page(pages_back, buffer, header, end_of_epoch);
}


// Retransmission ------------------------------------------------------------------------------------------------------

void storage_retransmit_clear(void) { nandlog_retransmit_clear(); }
uint32_t storage_retransmit_add(const uint32_t *seqs, uint32_t count) { return nandlog_retransmit_add(seqs, count); }
uint32_t storage_retransmit_count(void) { return nandlog_retransmit_count(); }
uint32_t storage_retransmit_total_bytes(void) { return nandlog_retransmit_total_bytes(); }

uint32_t storage_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, storage_page_header_t *header)
{
   return nandlog_retrieve_retransmit_page(index, buffer, header);
}
