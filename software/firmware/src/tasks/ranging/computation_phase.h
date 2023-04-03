#ifndef __COMPUTATION_PHASE_HEADER_H__
#define __COMPUTATION_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef struct
{
   uint8_t device_eui;
   uint32_t round_trip1_times[RANGING_NUM_SEQUENCES];
   uint32_t round_trip2_times[RANGING_NUM_SEQUENCES];
} ranging_device_state_t;

typedef struct
{
   uint8_t num_responses;
   ranging_device_state_t responses[MAX_NUM_RANGING_DEVICES];
} ranging_state_t;


// Public API ----------------------------------------------------------------------------------------------------------

void reset_computation_phase(void);
void add_roundtrip1_time(uint8_t eui, uint8_t sequence_number, uint32_t roundtrip_time);
void add_roundtrip2_time(uint8_t eui, uint8_t sequence_number, uint32_t roundtrip_time);
void compute_ranges(uint8_t *ranging_results);
bool responses_received(void);

#endif  // #ifndef __COMPUTATION_PHASE_HEADER_H__
