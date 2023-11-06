#ifndef __STATUS_PHASE_HEADER_H__
#define __STATUS_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t sequence_number, success;
   ieee154_footer_t footer;
} status_success_packet_t;


// Public API ----------------------------------------------------------------------------------------------------------

void status_phase_initialize(const uint8_t *uid);
scheduler_phase_t status_phase_begin(uint8_t status_slot, uint8_t num_slots, uint32_t start_delay_dwt);
scheduler_phase_t status_phase_tx_complete(void);
scheduler_phase_t status_phase_rx_complete(status_success_packet_t* packet);
scheduler_phase_t status_phase_rx_error(void);
const uint8_t* status_phase_get_detected_devices(uint8_t *num_devices);

#endif  // #ifndef __STATUS_PHASE_HEADER_H__
