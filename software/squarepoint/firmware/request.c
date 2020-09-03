#include <string.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "delay.h"
#include "dw1000.h"
#include "ranging.h"
#include "request.h"
#include "response.h"
#include "scheduler.h"
#include "SEGGER_RTT.h"

// Application state ---------------------------------------------------------------------------------------------------

static request_state_t _state;
static stm_timer_t* _ranging_timer;
static request_packet_t _request_packet;

// Private helper functions --------------------------------------------------------------------------------------------

static void ranging_request_transmit_task(void)
{
   // Stop timer on the last ranging subsequence
   if (_state.subsequence_num == (NUM_RANGING_BROADCASTS - 1))
   {
      timer_stop(_ranging_timer);
      _state.sending_request = FALSE;
   }

   // Setup the request packet
   const uint16_t packet_length = sizeof(request_packet_t);
   ++_request_packet.header.seqNum;
   _request_packet.subsequence_number = _state.subsequence_num;

   // Setup the correct channel and antenna for the ranging broadcast
   if (dw1000_radio_disable())
   {
      uint8_t channel_index = subsequence_number_to_channel_index(_state.subsequence_num);
      uint8_t antenna_index = subsequence_number_to_antenna(TRUE, _state.subsequence_num);
      dw1000_update_channel(channel_index_to_channel(channel_index));
      dw1000_choose_antenna(antenna_index);
      dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);

      // Setup the DW1000 radio and transmission delay time
      dwt_writetxfctrl(packet_length, 0, MSG_TYPE_RANGING);
      add_ranging_request_send_time(_state.subsequence_num, dw1000_setdelayedtrxtime(_state.dw_transmit_start_time + DW_DELAY_FROM_US(((uint32_t)_state.subsequence_num * RANGING_BROADCASTS_PERIOD_US))) + dw1000_get_tx_delay(channel_index, antenna_index));
      if ((dwt_starttx_no_auto_fcs(DWT_START_TX_DELAYED) != DWT_SUCCESS) || (dwt_writetxdata_enable_fcs(packet_length, (uint8*)&_request_packet, 0) != DWT_SUCCESS))
         debug_msg("ERROR: Delay for Request packet too short...could not be sent!\n");
   }
   ++_state.subsequence_num;
}

static void ranging_request_receive_task(void)
{
   // Prepare for the next subsequence message to be received
   _state.subsequence_num = prepare_for_next_subsequence();
   if (_state.subsequence_num >= NUM_RANGING_BROADCASTS)
   {
      // Signal completion of the current request
      timer_stop(_ranging_timer);
      _state.receiving_request = FALSE;
      _state.subsequence_num = 0;

      // Reset listening settings and re-enable the DW1000 radio
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, subsequence_number_to_channel(0), subsequence_number_to_antenna(FALSE, 0));
   }
   else
   {
      // Update the subsequence listening settings and re-enable the DW1000 radio
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, subsequence_number_to_channel(_state.subsequence_num), subsequence_number_to_antenna(FALSE, _state.subsequence_num));
   }
}

// Public functions ----------------------------------------------------------------------------------------------------

void initialize_request(stm_timer_t *ranging_timer, uint8_t *src_address)
{
   // Initialize the Request Packet
   _ranging_timer = ranging_timer;
   _request_packet = (request_packet_t){ .header = { .frameCtrl = { 0x41, 0xC8 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = RANGE_REQUEST_PACKET, .subsequence_number = 0, .footer = { { 0 } } };
   memcpy(_request_packet.header.sourceAddr, src_address, EUI_LEN);
   reset_request_phase();
}

void reset_request_phase(void)
{
   // Reset all Request state variables
   _request_packet.header.seqNum = 0;
   _state.sending_request = _state.listening_for_requests = _state.receiving_request = FALSE;
   _state.subsequence_num = 0;
   _state.dw_transmit_start_time = 0;
   _state.expected_request_eui = 0;
}

void ranging_request_txcallback(bool transmission_success)
{
   // Stop sending if there was a problem transmitting the previous packet
   if (!transmission_success)
   {
      debug_msg("ERROR: Failed to send ranging request packet!\n");
      timer_stop(_ranging_timer);
      _state.sending_request = FALSE;
   }

   // Reset the subsequence number to transmit upon completion
   if (_state.subsequence_num == NUM_RANGING_BROADCASTS)
      _state.subsequence_num = 0;
}

void receive_ranging_request(uint64_t dw_raw_timestamp, const request_packet_t* request)
{
   // Disallow timer interrupts while processing
   timer_disable_interrupt(_ranging_timer);

   // Determine the EUI of the requesting device
   PROTOCOL_EUI_TYPE source_eui = 0;
   memcpy(&source_eui, request->header.sourceAddr, PROTOCOL_EUI_SIZE);

   // Calculate the packet reception delay based on the current sequence number
   uint8_t channel_index = subsequence_number_to_channel_index(request->subsequence_number);
   uint8_t antenna_index = subsequence_number_to_antenna(FALSE, request->subsequence_number);
   uint64_t dw_timestamp_ranging = dw_raw_timestamp - dw1000_get_rx_delay(channel_index, antenna_index);

   // Handle the incoming range request packet
   if (_state.receiving_request)
      handle_incoming_request(dw_timestamp_ranging, source_eui, request->subsequence_number, FALSE);
   else if (_state.expected_request_eui == source_eui)
   {
      // Allow the RESPONDER portion of this node to parse all incoming requests
      _state.receiving_request = TRUE;
      handle_incoming_request(dw_timestamp_ranging, source_eui, request->subsequence_number, TRUE);

      // Start a timer to iterate through all antenna/channel combinations while listening for subsequent packets
      timer_start(_ranging_timer, RANGING_BROADCASTS_PERIOD_US, ranging_request_receive_task, TRUE);
   }

   // Re-enable timer interrupts
   timer_enable_interrupt(_ranging_timer);
}

void send_ranging_request(uint32_t dw_timestamp_to_begin)
{
   // Can only send one range request at a time
   if (!_state.sending_request && !_state.subsequence_num && (!dw_timestamp_to_begin || (_state.dw_transmit_start_time != dw_timestamp_to_begin)))
   {
      // Set all necessary state and phase variables
      _state.listening_for_requests = FALSE;
      _state.sending_request = TRUE;
      _state.dw_transmit_start_time = dw_timestamp_to_begin;

      // Force enable TX clock, and start a timer to transmit ranging requests
      dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);
      timer_start(_ranging_timer, RANGING_BROADCASTS_PERIOD_US, ranging_request_transmit_task, TRUE);
   }
}

void receive_ranging_requests(PROTOCOL_EUI_TYPE expected_eui)
{
   // Can only receive one range request at a time
   _state.expected_request_eui = expected_eui;
   if (!_state.listening_for_requests)
   {
      // Set all necessary state and phase variables
      _state.listening_for_requests = TRUE;
      _state.sending_request = FALSE;

      // Turn on the DW1000 radio to start receiving requests
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, subsequence_number_to_channel(0), subsequence_number_to_antenna(FALSE, 0));
   }
}
