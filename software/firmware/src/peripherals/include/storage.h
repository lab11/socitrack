#ifndef __STORAGE_HEADER_H__
#define __STORAGE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"


// Storage Data Types --------------------------------------------------------------------------------------------------

typedef enum {
   STORAGE_TYPE_SHUTDOWN = 0,
   STORAGE_TYPE_VOLTAGE,
   STORAGE_TYPE_CHARGING_EVENT,
   STORAGE_TYPE_MOTION,
   STORAGE_TYPE_RANGES,
   STORAGE_TYPE_IMU,
   STORAGE_TYPE_BLE_SCAN,
   STORAGE_NUM_TYPES,
} storage_data_type_t;


// Chip-Specific Definitions -------------------------------------------------------------------------------------------

#define MEMORY_PAGES_PER_BLOCK                      64
#define MEMORY_BLOCK_SIZE_BYTES                     (MEMORY_PAGE_SIZE_BYTES * MEMORY_PAGES_PER_BLOCK)
#define MEMORY_PAGE_COUNT                           (MEMORY_PAGES_PER_BLOCK * MEMORY_BLOCK_COUNT)
#define MEMORY_PAGE_WITH_ECC_SIZE_BYTES             (MEMORY_PAGE_SIZE_BYTES + MEMORY_ECC_BYTES_PER_PAGE)
#define MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL      3
#define MEMORY_NUM_DATA_BYTES_PER_PAGE              (MEMORY_PAGE_SIZE_BYTES - 4)


// Public API Functions ------------------------------------------------------------------------------------------------

void storage_init(void);
void storage_deinit(void);
void storage_disable(bool disable);
void storage_store_experiment_details(const experiment_details_t *details);
void storage_retrieve_experiment_details(experiment_details_t *details);
void storage_store(const void *data, uint32_t data_length);
void storage_flush(bool write_partial_pages);
void storage_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp);
void storage_end_reading(void);
void storage_enter_maintenance_mode(void);
void storage_exit_maintenance_mode(void);
uint32_t storage_retrieve_num_data_chunks(uint32_t ending_timestamp);
uint32_t storage_retrieve_num_data_bytes(void);
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer);

#endif  // #ifndef __STORAGE_HEADER_H__
