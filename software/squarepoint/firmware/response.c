#include <stddef.h>
#include <string.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"
#include "ranging.h"
#include "response.h"
#include "scheduler.h"
#include "SEGGER_RTT.h"

// Application state ---------------------------------------------------------------------------------------------------

static response_state_t _state;
static response_packet_t _response_packet;

// Public functions ----------------------------------------------------------------------------------------------------

void initialize_response(uint8_t *src_address)
{
   // Initialize the Response Packet
   _response_packet = (response_packet_t){ .header = { .frameCtrl = { 0x41, 0xC8 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = RANGE_RESPONSE_PACKET, .dw_time_sent = 0, .requests_count = 0, .footer = { { 0 } } };
   memset(_response_packet.requests, 0, sizeof(_response_packet.requests));
   memcpy(_response_packet.header.sourceAddr, src_address, EUI_LEN);
   reset_response_phase();
}

void reset_response_phase(void)
{
   // Reset all response variables
   _state.sending_response = FALSE;
   _response_packet.requests_count = 0;
   _state.current_subsequence_number = NUM_RANGING_BROADCASTS;
   _state.dw_transmit_start_time = 0;
   _state.expected_response_eui = 0;
   memset(_state.num_packets_received_per_antenna, 0, sizeof(_state.num_packets_received_per_antenna));
}

uint8_t prepare_for_next_subsequence(void)
{
   // Ensure that we are ready for reception of the next request
   if (++_state.current_subsequence_number == NUM_RANGING_BROADCASTS)
      ++_response_packet.requests_count;
   return _state.current_subsequence_number;
}

bool handle_incoming_request(uint64_t dw_rx_timestamp, PROTOCOL_EUI_TYPE source_eui, uint8_t subsequence_number, bool new_device)
{
   // Get an index to the next/current slot in the response packet as well as the reception antenna
   uint8_t idx = _response_packet.requests_count;
   uint8_t antenna_index = subsequence_number_to_antenna(FALSE, subsequence_number);

   // Update the response packet based on whether or not this is a request from a new device
   if (new_device)
   {
      // Set all relevant response packet fields for this incoming request
      memset(_response_packet.requests[idx].TOAs, 0, sizeof(_response_packet.requests[idx].TOAs));
      _response_packet.requests[idx].requester_eui = source_eui;
      _response_packet.requests[idx].first_rxd_toa = dw_rx_timestamp;
      _response_packet.requests[idx].first_rxd_idx = subsequence_number;
      _response_packet.requests[idx].TOAs[subsequence_number] = dw_rx_timestamp & 0xFFFF;

      // Keep track of how many packets were received on each antenna
      _state.current_subsequence_number = subsequence_number;
      _state.num_packets_received_per_antenna[antenna_index]++;
      return TRUE;
   }
   else if (_response_packet.requests[idx].requester_eui == source_eui)
   {
      // Set all relevant response packet fields for this incoming request
      _response_packet.requests[idx].last_rxd_toa = dw_rx_timestamp;
      _response_packet.requests[idx].last_rxd_idx = subsequence_number;
      _response_packet.requests[idx].TOAs[subsequence_number] = dw_rx_timestamp & 0xFFFF;

      // Keep track of how many packets were received on each antenna
      _state.current_subsequence_number = subsequence_number;
      _state.num_packets_received_per_antenna[antenna_index]++;
      return TRUE;
   }

   // Return that this packet was not successfully handled
   return FALSE;
}

void ranging_response_txcallback(bool transmission_success)
{
   // Send packet twice to ensure that it is received by all devices
   if (!transmission_success)
      debug_msg("ERROR: Failed to send ranging response packet!\n");
   if (_state.sending_response && dw1000_radio_disable())
   {
      dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);
      _response_packet.dw_time_sent = dw1000_setdelayedtrxtime(_state.dw_transmit_start_time + DW_DELAY_FROM_US(RESPONSE_RESEND_INTERVAL_US)) + dw1000_get_tx_delay(RANGING_RESPONSE_CHANNEL_INDEX, RANGING_RESPONSE_ANTENNA_INDEX);
      dwt_writetodevice(TX_BUFFER_ID, offsetof(response_packet_t, dw_time_sent), sizeof(_response_packet.dw_time_sent), (uint8_t*)&_response_packet.dw_time_sent);
      dwt_starttx(DWT_START_TX_DELAYED);
   }
   _state.sending_response = FALSE;
}

void receive_ranging_response(PROTOCOL_EUI_TYPE this_eui, uint64_t dw_raw_timestamp, const response_packet_t* response)
{
   // Ensure that this is the expected response and we have not already received a response packet from this device)
   if ((memcmp(&_state.expected_response_eui, response->header.sourceAddr, PROTOCOL_EUI_SIZE) != 0) || ranging_response_received(_state.expected_response_eui))
   {
      // Ignore the packet, re-enable listening, and return
      debug_msg("WARNING: Ignoring ranging response received from an unexpected device\n");
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, DO_NOT_CHANGE_FLAG, DO_NOT_CHANGE_FLAG);
      return;
   }

   // Add the response message to the current ranging state
   add_ranging_response(this_eui, dw_raw_timestamp, response);
}

void send_ranging_response(uint32_t dw_timestamp_to_begin)
{
   // Can only send one range response at a time
   if (!_state.sending_response)
   {
      // Set all necessary state and phase variables
      _state.sending_response = TRUE;
      _state.dw_transmit_start_time = dw_timestamp_to_begin;

      // Print out how many request packets were received on each antenna
      debug_msg("INFO: Number of ranging packets received: Antenna 1 = ");
      debug_msg_uint(_state.num_packets_received_per_antenna[0]);
      debug_msg("; Antenna 2 = ");
      debug_msg_uint(_state.num_packets_received_per_antenna[1]);
      debug_msg("; Antenna 3 = ");
      debug_msg_uint(_state.num_packets_received_per_antenna[2]);
      debug_msg("\n");

      // Setup and send a ranging response broadcast
      if (dw1000_radio_disable())
      {
         dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);
         dw1000_update_channel(channel_index_to_channel(RANGING_RESPONSE_CHANNEL_INDEX));
         dw1000_choose_antenna(RANGING_RESPONSE_ANTENNA_INDEX);
         const uint16_t packet_length = sizeof(struct ieee154_header_broadcast) + RESPONSE_PACKET_PAYLOAD_LENGTH + sizeof(struct ieee154_footer) + (_response_packet.requests_count * sizeof(request_info_t));
         dwt_writetxfctrl(packet_length, 0, MSG_TYPE_RANGING);
         _response_packet.dw_time_sent = dw1000_setdelayedtrxtime(dw_timestamp_to_begin) + dw1000_get_tx_delay(RANGING_RESPONSE_CHANNEL_INDEX, RANGING_RESPONSE_ANTENNA_INDEX);
         if ((dwt_starttx_no_auto_fcs(DWT_START_TX_DELAYED) == DWT_SUCCESS) && (dwt_writetxdata_enable_fcs(packet_length, (uint8*)&_response_packet, 0) != DWT_SUCCESS))
            debug_msg("ERROR: Unable to send RESPONSE message. This may occur if more than 8-10 devices are in the current network.\n");
      }
      else
         debug_msg("ERROR: Unable to send RESPONSE message due to a radio error\n");
   }
}

void receive_ranging_responses(PROTOCOL_EUI_TYPE expected_eui)
{
   // Turn on the DW1000 radio to start receiving responses
   _state.sending_response = FALSE;
   _state.expected_response_eui = expected_eui;
   dw1000_rxenable(DWT_START_RX_IMMEDIATE, channel_index_to_channel(RANGING_RESPONSE_CHANNEL_INDEX), RANGING_RESPONSE_ANTENNA_INDEX);
}
