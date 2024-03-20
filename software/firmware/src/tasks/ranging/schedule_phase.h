#ifndef __SCHEDULE_PHASE_HEADER_H__
#define __SCHEDULE_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t sequence_number;
   uint32_t epoch_time_unix;
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
void schedule_phase_add_device(uint8_t eui);
void schedule_phase_update_device_presence(uint8_t eui);
void schedule_phase_handle_device_timeouts(void);

#endif  // #ifndef __SCHEDULE_PHASE_HEADER_H__
