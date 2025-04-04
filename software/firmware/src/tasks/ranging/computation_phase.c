// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "computation_phase.h"
#include "ranging_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static int distances_millimeters[RANGING_NUM_RANGE_ATTEMPTS];
static uint8_t num_scheduled_devices;


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

void reset_computation_phase(uint8_t schedule_length)
{
   num_scheduled_devices = schedule_length;
}

void compute_ranges(uint8_t *ranging_results)
{
   // Iterate through all responses to calculate the range from this to that device
   ranging_results[0] = 0;
   uint8_t output_buffer_index = 1;
   const ranging_device_state_t *state = ranging_phase_get_measurements();
   for (uint8_t dev_index = 0; dev_index < num_scheduled_devices; ++dev_index)
   {
      // Calculate the device distances using symmetric two-way TOFs
      uint8_t num_valid_distances = 0;
      memset(distances_millimeters, 0, sizeof(distances_millimeters));
      for (uint8_t i = 0; i < RANGING_NUM_RANGE_ATTEMPTS; ++i)
         if (state[dev_index].device_eui && state[dev_index].poll_rx_times[i] && state[dev_index].resp_rx_times[i] && state[dev_index].final_rx_times[i])
         {
            // Compute the device range from the two-way round-trip times
            const double Ra = state[dev_index].resp_rx_times[i] - state[dev_index].poll_tx_times[i];
            const double Rb = state[dev_index].final_rx_times[i] - state[dev_index].resp_tx_times[i];
            const double Da = state[dev_index].final_tx_times[i] - state[dev_index].resp_rx_times[i];
            const double Db = state[dev_index].resp_tx_times[i] - state[dev_index].poll_rx_times[i];
            const double TOF = ((Ra * Rb) - (Da * Db)) / (Ra + Rb + Da + Db);
            const int distance_millimeters = ranging_radio_time_to_millimeters(TOF);

            // Check that the distance we have at this point is at all reasonable
            if ((distance_millimeters >= MIN_VALID_RANGE_MM) && (distance_millimeters <= MAX_VALID_RANGE_MM))
               insert_sorted(distances_millimeters, distance_millimeters, num_valid_distances++);
            else
               print("WARNING: Disregarding range to EUI 0x%02X for subsequence #%u: %d\n", (uint32_t)state[dev_index].device_eui, i, distance_millimeters);
         }

      // Skip this device if too few ranging packets were received
      int16_t range_millimeters = INT16_MAX;
      if (num_valid_distances >= 1)
      {
         // Take the lowest range as the final range estimate
         range_millimeters = (int16_t)distances_millimeters[0];
         if (range_millimeters < 0)
            range_millimeters = 0;
         if (range_millimeters < MAX_VALID_RANGE_MM)
         {
            // Copy valid ranges into the ID/range output buffer
            ranging_results[output_buffer_index++] = state[dev_index].device_eui;
            *((int16_t*)&ranging_results[output_buffer_index]) = range_millimeters;
            output_buffer_index += sizeof(range_millimeters);
            ++ranging_results[0];
         }
      }
   }
}
