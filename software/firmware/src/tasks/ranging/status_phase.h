#ifndef __STATUS_PHASE_HEADER_H__
#define __STATUS_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t src_addr;
   uint16_t heard_slots;
   uint8_t pending_subscriber;
   ieee154_footer_t footer;
} status_success_packet_t;

_Static_assert(MAX_NUM_RANGING_DEVICES <= 16, "heard_slots carries one bit per schedule slot");


// Public API ----------------------------------------------------------------------------------------------------------

void status_phase_initialize(const uint8_t *uid);
void status_phase_reset(void);
scheduler_phase_t status_phase_begin(uint8_t status_slot, uint8_t num_slots, uint32_t next_action_time);
scheduler_phase_t status_phase_tx_complete(void);
scheduler_phase_t status_phase_rx_complete(status_success_packet_t* packet);
scheduler_phase_t status_phase_rx_error(void);
uint16_t status_phase_get_present_slots(void);
uint16_t status_phase_get_directly_heard(void);
uint8_t status_phase_get_pending_subscriber(void);

#endif  // #ifndef __STATUS_PHASE_HEADER_H__
