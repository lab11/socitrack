#include <string.h>
#include "glossy.h"
#include "host_interface.h"
#include "ranging.h"
#include "SEGGER_RTT.h"

// Application state ---------------------------------------------------------------------------------------------------

// Configure the RF channels to use. This is just a mapping from 0..2 to the actual RF channel numbers the DW1000 uses.
// Note that we must have 3 instead of NUM_RANGING_CHANNELS to allow for variations
static const uint8_t channel_index_to_channel_rf_number[3] = { 1, 3, 5 };
static uint64_t _rx_TOAs[NUM_RANGING_BROADCASTS];
static ranging_state_t _state;
static int _distances_millimeters[NUM_RANGING_BROADCASTS];

// Public functions ----------------------------------------------------------------------------------------------------

uint8_t channel_index_to_channel(uint8_t channel_index)
{
   return channel_index_to_channel_rf_number[channel_index];
}

uint8_t subsequence_number_to_channel_index(uint8_t subsequence_num)
{
   return (((subsequence_num / NUM_RANGING_CHANNELS) / NUM_RANGING_CHANNELS) % NUM_ANTENNAS);
}

uint8_t subsequence_number_to_channel(uint8_t subsequence_num)
{
   return channel_index_to_channel_rf_number[subsequence_number_to_channel_index(subsequence_num)];
}

uint8_t subsequence_number_to_antenna(bool is_requester, uint8_t subsequence_num)
{
   return (is_requester ? (subsequence_num % NUM_RANGING_CHANNELS) : ((subsequence_num / NUM_RANGING_CHANNELS) % NUM_ANTENNAS));
}

void reset_ranging_phase(void)
{
   _state.num_responses = _state.num_ranges = 0;
   memset(_state.responses, 0, sizeof(_state.responses));
   memset(_state.subsequence_send_times, 0, sizeof(_state.subsequence_send_times));
}

bool ranging_response_received(PROTOCOL_EUI_TYPE eui)
{
   // Search for an existing response packet from this EUI
   for (uint8_t i = 0; i < _state.num_responses; ++i)
      if (eui == _state.responses[i].responder_eui)
         return TRUE;
   return FALSE;
}

void add_ranging_request_send_time(uint8_t subsequence_number, uint64_t send_time)
{
   _state.subsequence_send_times[subsequence_number] = send_time;
}

void add_ranging_response(PROTOCOL_EUI_TYPE this_eui, uint64_t dw_raw_timestamp, const response_packet_t* response)
{
   // Check that this response message contains a response to a request from this device
   for (uint8_t i = 0; i < response->requests_count; ++i)
      if (response->requests[i].requester_eui == this_eui)
      {
         // Save the response in the current ranging state
         memcpy(&_state.responses[_state.num_responses].responder_eui, response->header.sourceAddr, PROTOCOL_EUI_SIZE);
         _state.responses[_state.num_responses].first_rxd_idx = response->requests[i].first_rxd_idx;
         _state.responses[_state.num_responses].first_rxd_toa = response->requests[i].first_rxd_toa;
         _state.responses[_state.num_responses].last_rxd_idx = response->requests[i].last_rxd_idx;
         _state.responses[_state.num_responses].last_rxd_toa = response->requests[i].last_rxd_toa;
         memcpy(_state.responses[_state.num_responses].TOAs, response->requests[i].TOAs, sizeof(response->requests[i].TOAs));
         _state.responses[_state.num_responses].response_tx_timestamp = response->dw_time_sent;
         _state.responses[_state.num_responses].response_rx_timestamp = dw_raw_timestamp - dw1000_get_rx_delay(RANGING_RESPONSE_CHANNEL_INDEX, RANGING_RESPONSE_ANTENNA_INDEX);
         ++_state.num_responses;
         break;
      }
}

uint8_t perform_ranging(uint8_t *ids_and_ranges, PROTOCOL_EUI_TYPE *expected_devices, uint8_t expected_devices_len)
{
   // Iterate through all responses to calculate the range from this to that device
   uint8_t output_buffer_index = 1, num_successful_rangings = 0;
   for (uint8_t response_index = 0; response_index < _state.num_responses; ++response_index)
   {
      // Get an estimate of clock offset
      ranging_info_t* response = &_state.responses[response_index];
      double approx_clock_offset = (double)(response->last_rxd_toa - response->first_rxd_toa) / (double)(_state.subsequence_send_times[response->last_rxd_idx] - _state.subsequence_send_times[response->first_rxd_idx]);

      // Decompress TOA values from 16 bits to 64 bits by interpolating between the lowest and highest received timestamps
      memset(_rx_TOAs, 0, sizeof(_rx_TOAs));
      _rx_TOAs[response->first_rxd_idx] = response->first_rxd_toa;
      _rx_TOAs[response->last_rxd_idx] = response->last_rxd_toa;
      for (uint8_t i = response->first_rxd_idx + 1; i < response->last_rxd_idx; ++i)
         if (response->TOAs[i])
         {
            // Calculate actual TOAs, making corrections for 16-bit wraparounds
            uint64_t estimated_TOA = response->first_rxd_toa + (uint64_t)(approx_clock_offset * (_state.subsequence_send_times[i] - _state.subsequence_send_times[response->first_rxd_idx]));
            _rx_TOAs[i] = (estimated_TOA & 0xFFFFFFFFFFFF0000ULL) + response->TOAs[i];
            if (_rx_TOAs[i] < (estimated_TOA - 0x7FFF))
               _rx_TOAs[i] += 0x10000;
            else if (_rx_TOAs[i] > (estimated_TOA + 0x7FFF))
               _rx_TOAs[i] -= 0x10000;
         }

      // Calculate the clock offset between this device and the remote device
      double clock_offset = approx_clock_offset;
      uint8_t valid_offset_calculations = 1;
      for (uint8_t i = 0; i < NUM_RANGING_CHANNELS; ++i)
      {
         // Fetch the send and receive times for this repeated packet
         uint64_t initial_send_time = _state.subsequence_send_times[i];
         uint64_t initial_receive_time = _rx_TOAs[i];
         uint64_t final_send_time = _state.subsequence_send_times[NUM_RANGING_BROADCASTS - NUM_RANGING_CHANNELS + i];
         uint64_t final_receive_time = _rx_TOAs[NUM_RANGING_BROADCASTS - NUM_RANGING_CHANNELS + i];

         // If the packet was received twice, include it in calculating the clock offset between the two devices
         if ((initial_receive_time > 0) && (final_receive_time > 0))
         {
            clock_offset += (double)(final_receive_time - initial_receive_time) / (double)(final_send_time - initial_send_time);
            ++valid_offset_calculations;
         }
      }

      // Set the clock offset and synchronize the local DW1000 clock with the first responder's clock
      clock_offset /= valid_offset_calculations;
      if (!response_index)
         glossy_fix_clock((clock_offset - 1.0) * 1e6);



      /*double clock_drift = 0.0;
      for (uint8_t i = 0; i < NUM_RANGING_BROADCASTS; ++i)
         if (_rx_TOAs[i])
            clock_drift += (((double)response->response_rx_timestamp - _state.subsequence_send_times[i] - response->response_tx_timestamp + _rx_TOAs[i]) - clock_drift);
      clock_drift -= ((double)response->response_rx_timestamp - _state.subsequence_send_times[response->first_rxd_idx] - response->response_tx_timestamp + _rx_TOAs[response->first_rxd_idx]);
      clock_drift = (response->last_rxd_idx && (response->last_rxd_idx - response->first_rxd_idx) > 0) ? (clock_drift / (response->last_rxd_idx - response->first_rxd_idx)) : 0.0;*/
      /*debug_msg("DEBUG: Residual: ");
      debug_msg_int((int)clock_drift);
      debug_msg("\n");*/


      /*double last_one = (((response->response_rx_timestamp - _state.subsequence_send_times[response->last_rxd_idx]) * clock_offset) - (response->response_tx_timestamp - _rx_TOAs[response->last_rxd_idx]));
      double last_two = ((response->response_rx_timestamp - _state.subsequence_send_times[response->last_rxd_idx] - response->response_tx_timestamp + _rx_TOAs[response->last_rxd_idx]) + (clock_drift * (NUM_RANGING_BROADCASTS - 1 - response->last_rxd_idx)));
      double diff = last_two - last_one;*/


      // Calculate the device distances using one-way TOFs for each request message sent
      uint8_t num_valid_distances = 0;
      memset(_distances_millimeters, 0, sizeof(_distances_millimeters));
      for (uint8_t i = 0; i < NUM_RANGING_BROADCASTS; ++i)
         if (_rx_TOAs[i])
         {
            /*debug_msg("DEBUG: Residual [");
            debug_msg_uint(i);
            debug_msg("]: ");
            //debug_msg_int((int)(((response->response_rx_timestamp - _state.subsequence_send_times[i]) * clock_offset) - (response->response_tx_timestamp - _rx_TOAs[i])));
            //debug_msg(" - ");
            debug_msg_int((int)((response->response_rx_timestamp - _state.subsequence_send_times[i] - response->response_tx_timestamp + _rx_TOAs[i]) + (clock_drift * (NUM_RANGING_BROADCASTS - 1 - i)) - diff));
            debug_msg(" - ");
            debug_msg_int((int)clock_drift);
            debug_msg("\n");*/

            // Compute the device range from the one-way TOF
            double TOF = 0.5 * (((response->response_rx_timestamp - _state.subsequence_send_times[i]) * clock_offset) - (response->response_tx_timestamp - _rx_TOAs[i]));
            //double TOF = 0.5 * ((response->response_rx_timestamp - _state.subsequence_send_times[i] - response->response_tx_timestamp + _rx_TOAs[i]) + (clock_drift * (NUM_RANGING_BROADCASTS - 1 - i)) - diff);
            int distance_millimeters = dwtime_to_millimeters(TOF);

            // Check that the distance we have at this point is at all reasonable
            if (distance_millimeters >= MIN_VALID_RANGE_MM && distance_millimeters <= MAX_VALID_RANGE_MM)
               insert_sorted(_distances_millimeters, distance_millimeters, num_valid_distances++);
            else
            {
               debug_msg("WARNING: Disregarding range to EUI ");
               debug_msg_eui(response->responder_eui);
               debug_msg(" for subsequence #");
               debug_msg_uint(i);
               debug_msg(": ");
               debug_msg_int(distance_millimeters);
               debug_msg("\n");
            }
         }

      // TODO: DELETE THIS
      /*for (uint8_t i = 0; i < num_valid_distances; ++i)
      {
         debug_msg("DEBUG: Calculated range [");
         debug_msg_uint(i);
         debug_msg("]: ");
         debug_msg_int(_distances_millimeters[i]);
         debug_msg("\n");
      }*/

      // Skip this device if too few ranging packets were received
      int32_t range_millimeters = INT32_MAX;
      if (num_valid_distances >= MIN_VALID_RANGES_PER_DEVICE)
      {
         // Take the median range as the final range estimate
         uint8_t top = (num_valid_distances / 2), bot = (num_valid_distances % 2) ? (num_valid_distances / 2) : ((num_valid_distances / 2) - 1);
         range_millimeters = (_distances_millimeters[bot] + _distances_millimeters[top]) / 2;
         if (range_millimeters < 0)
            range_millimeters = 0;
         if (range_millimeters < MAX_VALID_RANGE_MM)
         {
            // Copy valid ranges into the ID/range output buffer
            memcpy(ids_and_ranges + output_buffer_index, &response->responder_eui, PROTOCOL_EUI_SIZE);
            output_buffer_index += PROTOCOL_EUI_SIZE;
            memcpy(ids_and_ranges + output_buffer_index, &range_millimeters, sizeof(range_millimeters));
            output_buffer_index += sizeof(range_millimeters);
            ++num_successful_rangings;
            ++_state.num_ranges;
         }
      }

      // Remove this device from the list of expected ranging results if it was valid
      if (range_millimeters < MAX_VALID_RANGE_MM)
         for (uint8_t i = 0; i < expected_devices_len; ++i)
            if (expected_devices[i] == response->responder_eui)
            {
               expected_devices[i] = 0;
               break;
            }
   }

   // Insert OUT_OF_RANGE notifications for any missing devices that were expected to have valid results
   for (uint8_t i = 0; i < expected_devices_len; ++i)
      if (expected_devices[i])
      {
         int32_t out_of_range_value = RANGE_ERROR_OUT_OF_RANGE;
         memcpy(ids_and_ranges + output_buffer_index, &expected_devices[i], PROTOCOL_EUI_SIZE);
         output_buffer_index += PROTOCOL_EUI_SIZE;
         memcpy(ids_and_ranges + output_buffer_index, &out_of_range_value, sizeof(out_of_range_value));
         output_buffer_index += sizeof(out_of_range_value);
         ++_state.num_ranges;
      }

   // Update the ranging state buffer
   ids_and_ranges[0] = _state.num_ranges;
   return num_successful_rangings;
}
