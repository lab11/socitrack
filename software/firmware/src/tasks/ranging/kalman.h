#ifndef __KALMAN_HEADER_H__
#define __KALMAN_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "scheduler.h"


// Kalman Filter Type Definitions --------------------------------------------------------------------------------------

typedef struct
{
   bool initialized;
   uint8_t eui;
   int32_t range_mm, delta_range_mm_per_sec;
   float P[4], range_cov, delta_range_cov;
} kalman_filter_t;


// Public API ----------------------------------------------------------------------------------------------------------

void kalman_filter_reset(kalman_filter_t *filter);
int16_t kalman_filter_estimate(kalman_filter_t *filter, float delta_time_seconds, int16_t range_mm, uint16_t range_acc_mm);

#endif  // #ifndef __KALMAN_HEADER_H__
