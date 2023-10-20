#ifndef __COMPUTATION_PHASE_HEADER_H__
#define __COMPUTATION_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct
{
   uint8_t device_eui;
   uint32_t poll_tx_times[RANGING_NUM_SEQUENCES], poll_rx_times[RANGING_NUM_SEQUENCES], resp_tx_times[RANGING_NUM_SEQUENCES];
   uint32_t resp_rx_times[RANGING_NUM_SEQUENCES], final_tx_times[RANGING_NUM_SEQUENCES], final_rx_times[RANGING_NUM_SEQUENCES];
} ranging_device_state_t;

typedef struct
{
   uint8_t num_responses;
   ranging_device_state_t responses[MAX_NUM_RANGING_DEVICES];
} ranging_state_t;


// Public API ----------------------------------------------------------------------------------------------------------

void reset_computation_phase(void);
void add_roundtrip0_times(uint8_t eui, uint8_t sequence_number, uint32_t poll_rx_time);
void add_roundtrip1_times(uint8_t eui, uint8_t sequence_number, uint32_t poll_tx_time, uint32_t resp_rx_time, uint32_t final_tx_time);
void add_roundtrip2_times(uint8_t eui, uint8_t sequence_number, uint32_t resp_tx_time, uint32_t final_rx_time);
void compute_ranges(uint8_t *ranging_results);
bool responses_received(void);

#endif  // #ifndef __COMPUTATION_PHASE_HEADER_H__
