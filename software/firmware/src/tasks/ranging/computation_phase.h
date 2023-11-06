#ifndef __COMPUTATION_PHASE_HEADER_H__
#define __COMPUTATION_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct
{
   uint8_t device_eui;
   uint32_t poll_tx_times[RANGING_NUM_SEQUENCES_PER_RANGE], poll_rx_times[RANGING_NUM_SEQUENCES_PER_RANGE];
   uint32_t resp_tx_times[RANGING_NUM_SEQUENCES_PER_RANGE], resp_rx_times[RANGING_NUM_SEQUENCES_PER_RANGE];
   uint32_t final_tx_times[RANGING_NUM_SEQUENCES_PER_RANGE], final_rx_times[RANGING_NUM_SEQUENCES_PER_RANGE];
} ranging_device_state_t;

typedef struct
{
   uint8_t num_responses;
   ranging_device_state_t responses[MAX_NUM_RANGING_DEVICES];
} ranging_state_t;


// Public API ----------------------------------------------------------------------------------------------------------

void reset_computation_phase(void);
void add_ranging_times_poll_tx(uint8_t eui, uint8_t sequence_number, uint32_t tx_time);
void add_ranging_times_poll_rx(uint8_t eui, uint8_t sequence_number, uint32_t rx_time);
void add_ranging_times_response_tx(uint8_t eui, uint8_t sequence_number, uint32_t tx_time);
void add_ranging_times_response_rx(uint8_t eui, uint8_t sequence_number, uint32_t rx_time);
void add_ranging_times_final_tx(uint8_t eui, uint8_t sequence_number, uint32_t tx_time);
void add_ranging_times_final_rx(uint8_t eui, uint8_t sequence_number, uint32_t rx_time);
void compute_ranges(uint8_t *ranging_results);
bool responses_received(void);

#endif  // #ifndef __COMPUTATION_PHASE_HEADER_H__
