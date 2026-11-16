#ifndef __SCHEDULE_PHASE_HEADER_H__
#define __SCHEDULE_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t src_addr, sequence_number;
   uint32_t experiment_time_ms;
   uint16_t master_heard;
   uint8_t master_nearest;
   uint8_t num_devices;
   uint8_t schedule[MAX_NUM_RANGING_DEVICES];
   ieee154_footer_t footer;
} schedule_packet_t;


// Public API ----------------------------------------------------------------------------------------------------------

void schedule_phase_initialize(const uint8_t *uid, bool is_master);
void schedule_phase_store_experiment_details(experiment_details_t *details);
scheduler_phase_t schedule_phase_begin(void);
scheduler_phase_t schedule_phase_tx_complete(void);
scheduler_phase_t schedule_phase_rx_complete(schedule_packet_t* schedule);
scheduler_phase_t schedule_phase_rx_error(void);
uint32_t schedule_phase_get_num_devices(void);
uint32_t schedule_phase_get_timestamp(void);
uint32_t schedule_phase_get_reference_stimer(void);
void schedule_phase_add_device(uint8_t eui);
uint8_t schedule_phase_get_addr_from_slot(uint8_t slot);
uint8_t schedule_phase_get_slot_from_addr(uint8_t eui);
uint16_t schedule_phase_get_master_heard(void);
uint8_t schedule_phase_get_master_nearest(void);
void schedule_phase_set_master_nearest(uint8_t slot);
uint64_t schedule_phase_get_reference_time_full(void);
void schedule_phase_update_device_presence(uint8_t eui);
void schedule_phase_handle_device_timeouts(void);

#endif  // #ifndef __SCHEDULE_PHASE_HEADER_H__
