#ifndef __COMPUTATION_PHASE_HEADER_H__
#define __COMPUTATION_PHASE_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Public API ----------------------------------------------------------------------------------------------------------

void computation_phase_configure_filters(experiment_details_t *details);
void computation_phase_reset_range_filter(uint8_t eui);
void reset_computation_phase(uint8_t schedule_length);
void compute_ranges(uint8_t *ranging_results);

#endif  // #ifndef __COMPUTATION_PHASE_HEADER_H__
