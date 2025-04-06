#ifndef __RANGING_PHASE_HEADER_H__
#define __RANGING_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   int16_t tx_rx_times[MAX_NUM_RANGING_DEVICES-1];
   ieee154_footer_t footer;
} ranging_packet_t;

typedef struct
{
   uint8_t device_eui;
   uint32_t poll_tx_times[RANGING_NUM_RANGE_ATTEMPTS], poll_rx_times[RANGING_NUM_RANGE_ATTEMPTS];
   uint32_t resp_tx_times[RANGING_NUM_RANGE_ATTEMPTS], resp_rx_times[RANGING_NUM_RANGE_ATTEMPTS];
   uint32_t final_tx_times[RANGING_NUM_RANGE_ATTEMPTS], final_rx_times[RANGING_NUM_RANGE_ATTEMPTS];
} ranging_device_state_t;


// Public API ----------------------------------------------------------------------------------------------------------

void ranging_phase_initialize(const uint8_t *uid);
scheduler_phase_t ranging_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t ref_time, uint32_t next_action_time);
scheduler_phase_t ranging_phase_tx_complete(void);
scheduler_phase_t ranging_phase_rx_complete(ranging_packet_t* packet);
scheduler_phase_t ranging_phase_rx_error(void);
ranging_device_state_t* ranging_phase_get_measurements(void);
uint32_t ranging_phase_get_duration(void);
bool ranging_phase_was_scheduled(void);
bool responses_received(void);

#endif  // #ifndef __RANGING_PHASE_HEADER_H__
