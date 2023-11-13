#ifndef __COMPUTATION_PHASE_HEADER_H__
#define __COMPUTATION_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct
{
   uint8_t device_eui;
   uint32_t poll_tx_times[RANGING_NUM_RANGE_ATTEMPTS], poll_rx_times[RANGING_NUM_RANGE_ATTEMPTS];
   uint32_t resp_tx_times[RANGING_NUM_RANGE_ATTEMPTS], resp_rx_times[RANGING_NUM_RANGE_ATTEMPTS];
   uint32_t final_tx_times[RANGING_NUM_RANGE_ATTEMPTS], final_rx_times[RANGING_NUM_RANGE_ATTEMPTS];
} ranging_device_state_t;


// Public API ----------------------------------------------------------------------------------------------------------

void reset_computation_phase(uint8_t schedule_length);
void associate_eui_with_index(uint32_t index, uint8_t eui);
void add_ranging_times_poll_tx(uint32_t index, uint8_t sequence_number, uint32_t tx_time);
void add_ranging_times_poll_rx(uint32_t index, uint8_t sequence_number, uint32_t rx_time);
void add_ranging_times_response_tx(uint32_t index, uint8_t sequence_number, uint32_t tx_time);
void add_ranging_times_response_rx(uint32_t index, uint8_t sequence_number, uint32_t rx_time);
void add_ranging_times_final_tx(uint32_t index, uint8_t sequence_number, uint32_t tx_time);
void add_ranging_times_final_rx(uint32_t index, uint8_t sequence_number, uint32_t rx_time);
void compute_ranges(uint8_t *ranging_results);
bool responses_received(void);

#endif  // #ifndef __COMPUTATION_PHASE_HEADER_H__
