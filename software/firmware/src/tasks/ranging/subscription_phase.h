#ifndef __SUBSCRIPTION_PHASE_HEADER_H__
#define __SUBSCRIPTION_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   ieee154_footer_t footer;
} subscription_packet_t;


// Public API ----------------------------------------------------------------------------------------------------------

void subscription_phase_initialize(const uint8_t *uid);
scheduler_phase_t subscription_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t ref_time, uint32_t next_action_time);
scheduler_phase_t subscription_phase_tx_complete(void);
scheduler_phase_t subscription_phase_rx_complete(subscription_packet_t* packet);
scheduler_phase_t subscription_phase_rx_error(void);

#endif  // #ifndef __SUBSCRIPTION_PHASE_HEADER_H__
