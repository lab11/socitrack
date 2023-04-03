// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "computation_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static ranging_state_t state;
static int distances_millimeters[RANGING_NUM_SEQUENCES];


// Private Helper Functions --------------------------------------------------------------------------------------------

void insert_sorted(int arr[], int new, unsigned end)
{
   unsigned insert_at = 0;
   while ((insert_at < end) && (new >= arr[insert_at]))
      ++insert_at;
   if (insert_at == end)
      arr[insert_at] = new;
   else
      while (insert_at <= end)
      {
         int temp = arr[insert_at];
         arr[insert_at] = new;
         new = temp;
         ++insert_at;
      }
}


// Public API Functions ------------------------------------------------------------------------------------------------

void reset_computation_phase(void)
{
   memset(&state, 0, sizeof(state));
}

void add_roundtrip1_time(uint8_t eui, uint8_t sequence_number, uint32_t roundtrip_time)
{
   bool response_found = false;
   for (uint8_t i = 0; i < state.num_responses; ++i)
      if (state.responses[i].device_eui == eui)
      {
         state.responses[i].round_trip1_times[sequence_number] = roundtrip_time;
         response_found = true;
         break;
      }
   if (!response_found)
   {
      state.responses[state.num_responses].device_eui = eui;
      state.responses[state.num_responses].round_trip1_times[sequence_number] = roundtrip_time;
      ++state.num_responses;
   }
}

void add_roundtrip2_time(uint8_t eui, uint8_t sequence_number, uint32_t roundtrip_time)
{
   bool response_found = false;
   for (uint8_t i = 0; i < state.num_responses; ++i)
      if (state.responses[i].device_eui == eui)
      {
         state.responses[i].round_trip2_times[sequence_number] = roundtrip_time;
         response_found = true;
         break;
      }
   if (!response_found)
   {
      state.responses[state.num_responses].device_eui = eui;
      state.responses[state.num_responses].round_trip2_times[sequence_number] = roundtrip_time;
      ++state.num_responses;
   }
}

void compute_ranges(uint8_t *ranging_results)
{
   // Iterate through all responses to calculate the range from this to that device
   ranging_results[0] = 0;
   uint8_t output_buffer_index = 1;
   for (uint8_t dev_index = 0; dev_index < state.num_responses; ++dev_index)
   {
      // Calculate the device distances using symmetric two-way TOFs
      uint8_t num_valid_distances = 0;
      memset(distances_millimeters, 0, sizeof(distances_millimeters));
      for (uint8_t i = 0; i < RANGING_NUM_SEQUENCES; ++i)
         if (state.responses[dev_index].round_trip1_times[i] && state.responses[dev_index].round_trip2_times[i])
         {
            // Compute the device range from the two-way round-trip times
            const double broadcast_interval_dwt = APP_US_TO_DEVICETIMEU64(RANGING_BROADCAST_INTERVAL_US);
            const double TOF = (((double)state.responses[dev_index].round_trip1_times[i] * state.responses[dev_index].round_trip2_times[i]) - (broadcast_interval_dwt * broadcast_interval_dwt)) /
                  ((double)state.responses[dev_index].round_trip1_times[i] + state.responses[dev_index].round_trip2_times[i] + broadcast_interval_dwt + broadcast_interval_dwt);
            const int distance_millimeters = ranging_radio_time_to_millimeters(TOF);

            // Check that the distance we have at this point is at all reasonable
            if ((distance_millimeters >= MIN_VALID_RANGE_MM) && (distance_millimeters <= MAX_VALID_RANGE_MM))
               insert_sorted(distances_millimeters, distance_millimeters, num_valid_distances++);
            else
               print("WARNING: Disregarding range to EUI %u for subsequence #%u: %d\n", (uint32_t)state.responses[dev_index].device_eui, i, distance_millimeters);
         }

      // Skip this device if too few ranging packets were received
      int16_t range_millimeters = INT16_MAX;
      if (num_valid_distances >= 1)
      {
         // Take the median range as the final range estimate
         uint8_t top = (num_valid_distances / 2), bot = (num_valid_distances % 2) ? (num_valid_distances / 2) : ((num_valid_distances / 2) - 1);
         range_millimeters = (int16_t)((distances_millimeters[bot] + distances_millimeters[top]) / 2);
         if (range_millimeters < 0)
            range_millimeters = 0;
         if (range_millimeters < MAX_VALID_RANGE_MM)
         {
            // Copy valid ranges into the ID/range output buffer
            ranging_results[output_buffer_index++] = state.responses[dev_index].device_eui;
            *((int16_t*)&ranging_results[output_buffer_index]) = range_millimeters;
            output_buffer_index += sizeof(range_millimeters);
            ++ranging_results[0];
         }
      }
   }
}

bool responses_received(void)
{
   return state.num_responses;
}
