#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

// Algorithm Constants -------------------------------------------------------------------------------------------------

#define NUM_CALIBRATION_DEVICES                                 3
#define NUM_REFINEMENT_ITERATIONS                             250
#define ANTENNA_DISTANCE_MM                                3000.0
#define NUM_CANDIDATE_DELAY_ESTIMATES                       10000
#define INITIAL_DELAY_ESTIMATE_NS                           514.0
#define INITIAL_DELAY_ESTIMATE_DISTRIBUTION_NS                6.0
#define DELAY_PERTURBATION_LIMIT_NS                           0.2
#define SPEED_OF_LIGHT_MM_PER_NS                       299.702547


// Static Global Variables ---------------------------------------------------------------------------------------------

static double perturbation_limit_ns = DELAY_PERTURBATION_LIMIT_NS;
static double candidate_delay_estimates[NUM_CANDIDATE_DELAY_ESTIMATES] = { 0 };
static double euclidean_distance_matrix_measured[NUM_CALIBRATION_DEVICES][NUM_CALIBRATION_DEVICES] = { 0 };
static double euclidean_distance_matrix_estimated[NUM_CALIBRATION_DEVICES][NUM_CALIBRATION_DEVICES] = { 0 };
static const double euclidean_distance_matrix_actual[NUM_CALIBRATION_DEVICES][NUM_CALIBRATION_DEVICES] = {
      { 0.0, ANTENNA_DISTANCE_MM, ANTENNA_DISTANCE_MM },
      { ANTENNA_DISTANCE_MM, 0.0, ANTENNA_DISTANCE_MM },
      { ANTENNA_DISTANCE_MM, ANTENNA_DISTANCE_MM, 0.0 } };


// Helper Functions ----------------------------------------------------------------------------------------------------

static int compare_estimates(const void *a, const void *b)
{
   double d0 = **(double**)a, d1 = **(double**)b;
   return (d0 > d1) ? -1 : ((d0 < d1) ? 1 : 0);
}

static uint64_t calculate_tx_antenna_delay(double aggregate_delay)
{
   // TODO: Convert aggregate delay into DWT units
   return (uint64_t)(0.44 * aggregate_delay);
}

static uint64_t calculate_rx_antenna_delay(double aggregate_delay)
{
   // TODO: Convert aggregate delay into DWT units
   return (uint64_t)(0.56 * aggregate_delay);
}

static void populate_candidate_delay_estimates(int iteration)
{
   // First iteration is used to generate the initial set of candidate delays
   if (iteration == 0)
   {
      // Generate a set of random delays uniformly distributed around the initial delay
      const double initial_estimate = INITIAL_DELAY_ESTIMATE_NS - INITIAL_DELAY_ESTIMATE_DISTRIBUTION_NS;
      const double estimate_offset = (2.0 * INITIAL_DELAY_ESTIMATE_DISTRIBUTION_NS) / NUM_CANDIDATE_DELAY_ESTIMATES;
      for (int i = 0; i < NUM_CANDIDATE_DELAY_ESTIMATES; ++i)
         candidate_delay_estimates[i] = initial_estimate + (estimate_offset * i);
   }
   else
   {
      // Halve the perturbation limit every 20 iterations
      const int quarter_of_num_estimates = (NUM_CANDIDATE_DELAY_ESTIMATES / 4);
      if (iteration == 20)
         perturbation_limit_ns *= 0.5;

      // Randomly perturb the top 25% of candidates 3 times to create a new set of candidates
      for (int i = 1; i < 4; ++i)
         for (int candidate_idx = 0; candidate_idx < quarter_of_num_estimates; ++candidate_idx)
         {
            double random_value = (rand() * 2.0 * perturbation_limit_ns / RAND_MAX) - perturbation_limit_ns;
            candidate_delay_estimates[(i * quarter_of_num_estimates) + candidate_idx] = candidate_delay_estimates[candidate_idx] + random_value;
         }
   }
}

static void evaluate_candidate_delay_estimates(void)
{
   // Determine the "goodness" of each candidate delay estimate
   static double candidate_delay_norms[NUM_CANDIDATE_DELAY_ESTIMATES];
   for (int candidate_idx = 0; candidate_idx < NUM_CANDIDATE_DELAY_ESTIMATES; ++candidate_idx)
   {
      // Fill in the estimated euclidean distance matrix
      const double candidate_delay_ns = candidate_delay_estimates[candidate_idx];
      for (int device1 = 0; device1 < NUM_CALIBRATION_DEVICES; ++device1)
         for (int device2 = 0; device2 < NUM_CALIBRATION_DEVICES; ++device2)
            if (device1 != device2)
               euclidean_distance_matrix_estimated[device1][device2] = (euclidean_distance_matrix_measured[device1][device2] - (2.0 * candidate_delay_ns)) * SPEED_OF_LIGHT_MM_PER_NS;

      // Compute the norm of the difference between the actual and the estimated EDM
      double norm = 0.0;
      for (int device1 = 0; device1 < NUM_CALIBRATION_DEVICES; ++device1)
         for (int device2 = 0; device2 < NUM_CALIBRATION_DEVICES; ++device2)
         {
            const double edm_difference = euclidean_distance_matrix_estimated[device1][device2] - euclidean_distance_matrix_actual[device1][device2];
            norm += (edm_difference * edm_difference);
         }
      candidate_delay_norms[candidate_idx] = sqrt(norm);
   }

   // Sort the array of candidate delay estimates based on the computed norms
   double *sorting_array[NUM_CANDIDATE_DELAY_ESTIMATES];
   for (int i = 0; i < NUM_CANDIDATE_DELAY_ESTIMATES; ++i)
      sorting_array[i] = &candidate_delay_estimates[i];
   qsort(sorting_array, NUM_CANDIDATE_DELAY_ESTIMATES, sizeof(sorting_array[0]), compare_estimates);
   for (int i = 0; i < NUM_CANDIDATE_DELAY_ESTIMATES; ++i)
      if (i != (sorting_array[i] - candidate_delay_estimates))
      {
         size_t temp_candidate_delay_estimate = candidate_delay_estimates[i], j, k = i;
         while (i != (j = (sorting_array[k] - candidate_delay_estimates)))
         {
            candidate_delay_estimates[k] = candidate_delay_estimates[j];
            sorting_array[k] = &candidate_delay_estimates[k];
            k = j;
         }
         candidate_delay_estimates[k] = temp_candidate_delay_estimate;
         sorting_array[k] = &candidate_delay_estimates[k];
     }
}


// Main Calibration Function -------------------------------------------------------------------------------------------

int main(void)
{
   // Collect a large number of time-of-flight measurements
   srand(1);
   // TODO: Fill in euclidean_distance_matrix_measured using ranging algorithm

   // Carry out the antenna delay estimation algorithm some number of times
   for (int i = 0; i < NUM_REFINEMENT_ITERATIONS; ++i)
   {
      populate_candidate_delay_estimates(i);
      evaluate_candidate_delay_estimates();
   }

   // Print out the best calculated TX/RX antenna delays
   printf("TX Antenna Delay: %llu\n", calculate_tx_antenna_delay(candidate_delay_estimates[0]));
   printf("RX Antenna Delay: %llu\n", calculate_rx_antenna_delay(candidate_delay_estimates[0]));
   return 0;
}
