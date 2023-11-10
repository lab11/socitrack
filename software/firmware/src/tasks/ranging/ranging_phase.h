#ifndef __RANGING_PHASE_HEADER_H__
#define __RANGING_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t sequence_number;
   uint32_t tx_time, rx_time;
   ieee154_footer_t footer;
} ranging_packet_t;


// Public API ----------------------------------------------------------------------------------------------------------

void ranging_phase_initialize(const uint8_t *uid);
scheduler_phase_t ranging_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t start_delay_dwt);
scheduler_phase_t ranging_phase_tx_complete(void);
scheduler_phase_t ranging_phase_rx_complete(ranging_packet_t* packet);
scheduler_phase_t ranging_phase_rx_error(void);
uint32_t ranging_phase_get_duration(void);
bool ranging_phase_was_scheduled(void);

#endif  // #ifndef __RANGING_PHASE_HEADER_H__
