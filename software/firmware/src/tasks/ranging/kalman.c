// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "kalman.h"


// Private Kalman Filter definitions -----------------------------------------------------------------------------------

#define KALMAN_PROCESS_NOISE_RANGE             0
#define KALMAN_PROCESS_NOISE_DELTA_RANGE       5

#define KALMAN_STATE_NOISE_DELTA_RANGE         5


// Private Helper Functions ------------------------------------------------------------------------------------------------

static void kalman_filter_init(kalman_filter_t *filter, int16_t initial_range_mm, uint16_t initial_range_acc_mm)
{
   filter->initialized = true;
   filter->range_mm = initial_range_mm;
   filter->delta_range_mm_per_sec = 0;
   filter->P[1] = filter->P[2] = 0;
   filter->P[0] = initial_range_acc_mm * initial_range_acc_mm;
   filter->P[3] = KALMAN_STATE_NOISE_DELTA_RANGE * KALMAN_STATE_NOISE_DELTA_RANGE;
   filter->range_cov = KALMAN_PROCESS_NOISE_RANGE * KALMAN_PROCESS_NOISE_RANGE;
   filter->delta_range_cov = KALMAN_PROCESS_NOISE_DELTA_RANGE * KALMAN_PROCESS_NOISE_DELTA_RANGE;
}

static void kalman_filter_predict(kalman_filter_t *filter, float dt)
{
   filter->range_mm += (filter->delta_range_mm_per_sec * dt);
   filter->P[0] += (filter->P[2] * dt);
   filter->P[1] += (filter->P[3] * dt);
   filter->P[0] += (filter->P[1] * dt) + filter->range_cov;
   filter->P[2] += (filter->P[3] * dt);
   filter->P[3] += filter->delta_range_cov;
}

static void kalman_filter_update(kalman_filter_t *filter, int16_t range_mm, uint16_t range_acc_mm)
{
   float y = (int32_t)range_mm - filter->range_mm;
   float S_inv = 1.0f / (filter->P[0] + (range_acc_mm * range_acc_mm));
   float K0 = filter->P[0] * S_inv, K1 = filter->P[2] * S_inv;
   filter->range_mm += (int32_t)(y * K0);
   filter->delta_range_mm_per_sec += (int32_t)(y * K1);
   float newP0 = filter->P[0] * (1.0f - K0);
   float newP1 = filter->P[1] * (1.0f - K0);
   filter->P[2] = filter->P[2] - K1*filter->P[0];
   filter->P[3] = filter->P[3] - K1*filter->P[1];
   filter->P[0] = newP0;
   filter->P[1] = newP1;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void kalman_filter_reset(kalman_filter_t *filter)
{
   filter->initialized = false;
}

int16_t kalman_filter_estimate(kalman_filter_t *filter, float delta_time_seconds, int16_t range_mm, uint16_t range_acc_mm)
{
   // Ensure that the filter has been initialized
   if (!filter->initialized)
      kalman_filter_init(filter, range_mm, range_acc_mm);
   else
   {
      // Update the Kalman Filter state
      kalman_filter_predict(filter, delta_time_seconds);
      kalman_filter_update(filter, range_mm, range_acc_mm);
   }

   // Return the current filtered position estimate
   return (filter->range_mm < 0) ? 0 : (int16_t)filter->range_mm;
}
