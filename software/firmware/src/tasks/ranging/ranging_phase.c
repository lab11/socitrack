// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "computation_phase.h"
#include "ranging_phase.h"
#include "status_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t current_phase;
static ranging_packet_t ranging_packet;
static uint8_t responder_slot_index, scheduled_slot, total_num_slots, antenna_index;
static int32_t responder_slots[MAX_NUM_RANGING_DEVICES], current_slot;
static int32_t initiator_slot_first, initiator_slot_last;
static uint32_t num_sub_slots, last_rx_delay_time_us;
static bool last_rx_relative_to_transmit;


// Public functions ----------------------------------------------------------------------------------------------------

void ranging_phase_initialize(const uint8_t *uid)
{
   // Initialize all Ranging Phase parameters
   ranging_packet = (ranging_packet_t){ .header = { .frameCtrl = { 0x41, 0x98 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = RANGING_PACKET, .round_trip_time = 0, .footer = { { 0 } } };
   memcpy(ranging_packet.header.sourceAddr, uid, sizeof(ranging_packet.header.sourceAddr));
   scheduled_slot = 0xFF;
}

scheduler_phase_t ranging_phase_begin(uint8_t ranging_slot, uint8_t num_slots, uint32_t start_delay_us, bool start_relative_to_transmit)
{
   // Ensure there are at least two devices to begin ranging
   reset_computation_phase();
   if (num_slots < 2)
      return RANGE_COMPUTATION_PHASE;

   // Reset the necessary Ranging Phase parameters
   current_phase = RANGING_PHASE;
   responder_slot_index = antenna_index = 0;
   num_sub_slots = (uint32_t)num_slots * (num_slots - 1) / 2;
   scheduled_slot = ranging_slot;
   total_num_slots = num_slots;

   // Compute the initiator time slots for the device
   if ((ranging_slot + 1) < num_slots)
   {
      initiator_slot_first = 0;
      for (uint8_t i = 1; i <= ranging_slot; ++i)
         initiator_slot_first += (num_slots - i);
      initiator_slot_last = initiator_slot_first + (num_slots - ranging_slot - 2);
   }
   else
      initiator_slot_first = initiator_slot_last = -1;

   // Compute the responder time slots for the device
   responder_slots[0] = (int32_t)ranging_slot - 1;
   for (uint8_t i = 1; i < ranging_slot; ++i)
      responder_slots[i] = responder_slots[i-1] + (num_slots - i - 1);
   for (uint8_t i = ranging_slot; i < MAX_NUM_RANGING_DEVICES; ++i)
      responder_slots[i] = -1;

   // Set up the correct initial antenna and RX timeout duration
   ranging_radio_choose_antenna(0);
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGING_TIMEOUT_US));

   // Begin transmission or reception depending on the scheduled time slot
   if (ranging_slot == 0)
   {
      current_slot = 0;
      ranging_packet.header.seqNum = 0;
      dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
      dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(start_delay_us));
      if (dwt_starttx(start_relative_to_transmit ? DWT_START_TX_DLY_TS : DWT_START_TX_DLY_RS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit RANGING REQUEST packet\n");
         return RANGING_ERROR;
      }
   }
   else
   {
      ranging_packet.header.seqNum = 11;
      current_slot = (uint8_t)responder_slots[0];
      last_rx_relative_to_transmit = start_relative_to_transmit;
      last_rx_delay_time_us = start_delay_us + ((uint32_t)(responder_slots[0]) * RANGING_ITERATION_INTERVAL_US) - RECEIVE_EARLY_START_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      if (!ranging_radio_rxenable(start_relative_to_transmit ? DWT_START_RX_DLY_TS : DWT_START_RX_DLY_RS))
      {
         print("ERROR: Unable to start listening for RANGING REQUEST packets\n");
         return RANGING_ERROR;
      }
   }
   return RANGING_PHASE;
}

scheduler_phase_t ranging_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_tx_complete();

   // Switch to the next antenna when appropriate
   if ((ranging_packet.header.seqNum == 3) || (ranging_packet.header.seqNum == 7))
      ranging_radio_choose_antenna(++antenna_index);

   // Handle the ranging phase based on the sequence number of the packet that was just transmitted
   if (ranging_packet.header.seqNum == 11)
   {
      // Finished current ranging iteration...continue to next time slot
      antenna_index = 0;
      ++responder_slot_index;
      ranging_radio_choose_antenna(0);
      if ((++current_slot <= initiator_slot_first) && (initiator_slot_first < responder_slots[responder_slot_index]))
      {
         ranging_packet.header.seqNum = 0;
         dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(RANGING_BROADCAST_INTERVAL_US + ((initiator_slot_first - current_slot) * RANGING_ITERATION_INTERVAL_US)));
         current_slot = initiator_slot_first;
         if (dwt_starttx(DWT_START_TX_DLY_TS) != DWT_SUCCESS)
         {
            print("ERROR: Failed to transmit initial RANGING REQUEST packet\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
      else if (current_slot <= responder_slots[responder_slot_index])
      {
         last_rx_relative_to_transmit = true;
         last_rx_delay_time_us = RANGING_BROADCAST_INTERVAL_US + ((uint32_t)(responder_slots[responder_slot_index] - current_slot) * RANGING_ITERATION_INTERVAL_US) - RECEIVE_EARLY_START_US;
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
         current_slot = (uint8_t)responder_slots[responder_slot_index];
         if (!ranging_radio_rxenable(DWT_START_RX_DLY_TS))
         {
            print("ERROR: Unable to start listening for incoming RANGING REQUEST packets\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
   }
   else
   {
      // Finished transmitting non-terminal packet...start listening for incoming responses
      last_rx_relative_to_transmit = true;
      last_rx_delay_time_us = RANGING_BROADCAST_INTERVAL_US - RECEIVE_EARLY_START_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      if (!ranging_radio_rxenable(DWT_START_RX_DLY_TS))
      {
         print("ERROR: Unable to start listening for next RANGING REQUEST packets\n");
         return RANGING_ERROR;
      }
      return RANGING_PHASE;
   }

   // Move to the Status Phase of the ranging protocol
   current_phase = RANGE_STATUS_PHASE;
   return status_phase_begin(scheduled_slot, total_num_slots, RANGING_BROADCAST_INTERVAL_US + ((num_sub_slots - current_slot) * RANGING_ITERATION_INTERVAL_US), true);
}

scheduler_phase_t ranging_phase_rx_complete(ranging_packet_t* packet)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_rx_complete((status_success_packet_t*)packet);
   else if (packet->message_type != RANGING_PACKET)
   {
      print("ERROR: Received an unexpected message type during RANGING phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }

   // Switch to the next antenna when appropriate
   if ((packet->header.seqNum == 3) || (packet->header.seqNum == 7))
      ranging_radio_choose_antenna(++antenna_index);

   // Compute the roundtrip transmission time when appropriate
   if ((packet->header.seqNum == 1) || (packet->header.seqNum == 5) || (packet->header.seqNum == 9))
   {
      const uint64_t range_bias_correction = ranging_radio_compute_correction_for_signal_level(ranging_radio_received_signal_level());
      ranging_packet.round_trip_time = (uint32_t)(ranging_radio_readrxtimestamp() - ranging_radio_readtxtimestamp() - range_bias_correction);
      add_roundtrip1_time(packet->header.sourceAddr[0], (packet->header.seqNum - 1) / 4, ranging_packet.round_trip_time);
   }
   else if ((packet->header.seqNum == 2) || (packet->header.seqNum == 6) || (packet->header.seqNum == 10))
   {
      const uint64_t range_bias_correction = ranging_radio_compute_correction_for_signal_level(ranging_radio_received_signal_level());
      ranging_packet.round_trip_time = (uint32_t)(ranging_radio_readrxtimestamp() - ranging_radio_readtxtimestamp() - range_bias_correction);
      add_roundtrip1_time(packet->header.sourceAddr[0], (packet->header.seqNum - 2) / 4, packet->round_trip_time);
      add_roundtrip2_time(packet->header.sourceAddr[0], (packet->header.seqNum - 2) / 4, ranging_packet.round_trip_time);
   }
   else if ((packet->header.seqNum == 3) || (packet->header.seqNum == 7) || (packet->header.seqNum == 11))
      add_roundtrip2_time(packet->header.sourceAddr[0], (packet->header.seqNum - 3) / 4, packet->round_trip_time);

   // Handle the ranging phase based on the sequence number of the packet that was just received
   if (packet->header.seqNum == 11)
   {
      // Finished current ranging iteration...continue to next time slot
      ranging_radio_choose_antenna(0);
      antenna_index = ranging_packet.header.seqNum = 0;
      if (++current_slot <= initiator_slot_last)
      {
         dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(RANGING_BROADCAST_INTERVAL_US));
         if (dwt_starttx(DWT_START_TX_DLY_RS) != DWT_SUCCESS)
         {
            print("ERROR: Failed to transmit next RANGING REQUEST packet\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
   }
   else
   {
      // Received non-terminal packet...send response
      ranging_packet.header.seqNum = packet->header.seqNum + 1;
      if ((packet->header.seqNum == 0) || (packet->header.seqNum == 4) || (packet->header.seqNum == 8) || (packet->header.seqNum == 3) || (packet->header.seqNum == 7))
      {
         dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
      }
      else
      {
         dwt_writetxfctrl(sizeof(ranging_packet_t), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t), (uint8_t*)&ranging_packet, 0);
      }
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(RANGING_BROADCAST_INTERVAL_US));
      if (dwt_starttx(DWT_START_TX_DLY_RS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit RANGING RESPONSE packet with sequence number %u\n", (uint32_t)ranging_packet.header.seqNum);
         return RANGING_ERROR;
      }
      return RANGING_PHASE;
   }

   // Move to the Status Phase of the ranging protocol
   current_phase = RANGE_STATUS_PHASE;
   return status_phase_begin(scheduled_slot, total_num_slots, RANGING_BROADCAST_INTERVAL_US + ((num_sub_slots - current_slot) * RANGING_ITERATION_INTERVAL_US), false);
}

scheduler_phase_t ranging_phase_rx_error(void)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_rx_error();

   // Handle RX errors differently depending on which sequence number experienced the problem
   if ((ranging_packet.header.seqNum == 8) || (ranging_packet.header.seqNum == 10))
   {
      // We are the initiator and did not receive a response on the final antenna...move to next time slot
      antenna_index = 0;
      ranging_radio_choose_antenna(0);
      uint32_t delay_time_us = (uint32_t)(12 - ranging_packet.header.seqNum) * RANGING_BROADCAST_INTERVAL_US;
      if (++current_slot <= initiator_slot_last)
      {
         ranging_packet.header.seqNum = 0;
         dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(delay_time_us));
         if (dwt_starttx(DWT_START_TX_DLY_TS) != DWT_SUCCESS)
         {
            print("ERROR: Failed to transmit RANGING REQUEST packet after error\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
      else
      {
         // Move to the Range Status Phase of the ranging protocol
         current_phase = RANGE_STATUS_PHASE;
         return status_phase_begin(scheduled_slot, total_num_slots, delay_time_us + ((num_sub_slots - current_slot) * RANGING_ITERATION_INTERVAL_US), true);
      }
   }
   else if ((ranging_packet.header.seqNum == 0) || (ranging_packet.header.seqNum == 2) || (ranging_packet.header.seqNum == 4) || (ranging_packet.header.seqNum == 6))
   {
      // We are the initiator and did not receive a response on the non-final antenna, skip to the next antenna
      ranging_radio_choose_antenna(++antenna_index);
      uint8_t next_sequence_num = 4 * ((4 + ranging_packet.header.seqNum) / 4);
      uint32_t delay_time_us = (uint32_t)(next_sequence_num - ranging_packet.header.seqNum) * RANGING_BROADCAST_INTERVAL_US;
      ranging_packet.header.seqNum = next_sequence_num;
      dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
      dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(delay_time_us));
      if (dwt_starttx(DWT_START_TX_DLY_TS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit next RANGING REQUEST packet after error\n");
         return RANGING_ERROR;
      }
      return RANGING_PHASE;
   }
   else if ((ranging_packet.header.seqNum == 7) || (ranging_packet.header.seqNum == 9))
   {
      // We are the responder and did not receive a response on the final antenna...move to next time slot
      antenna_index = 0;
      ++responder_slot_index;
      ranging_radio_choose_antenna(0);
      uint32_t delay_time_us = (uint32_t)(12 - ranging_packet.header.seqNum) * RANGING_BROADCAST_INTERVAL_US;
      if ((++current_slot <= initiator_slot_first) && (initiator_slot_first < responder_slots[responder_slot_index]))
      {
         ranging_packet.header.seqNum = 0;
         dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(delay_time_us + ((initiator_slot_first - current_slot) * RANGING_ITERATION_INTERVAL_US)));
         current_slot = initiator_slot_first;
         if (dwt_starttx(DWT_START_TX_DLY_TS) != DWT_SUCCESS)
         {
            print("ERROR: Failed to transmit initial RANGING REQUEST packet after error\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
      else if (current_slot <= responder_slots[responder_slot_index])
      {
         last_rx_delay_time_us = delay_time_us + ((uint32_t)(responder_slots[responder_slot_index] - current_slot) * RANGING_ITERATION_INTERVAL_US) - RECEIVE_EARLY_START_US;
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
         current_slot = (uint8_t)responder_slots[responder_slot_index];
         if (!ranging_radio_rxenable(DWT_START_RX_DLY_TS))
         {
            print("ERROR: Unable to start listening for incoming RANGING REQUEST packets after error\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
   }
   else if ((antenna_index + 1) == NUM_ANTENNAS)
   {
      // We are the responder and did not receive any responses...move to next time slot
      antenna_index = 0;
      ++responder_slot_index;
      ranging_radio_choose_antenna(0);
      last_rx_delay_time_us += (4 * RANGING_BROADCAST_INTERVAL_US);
      if ((++current_slot <= initiator_slot_first) && (initiator_slot_first < responder_slots[responder_slot_index]))
      {
         ranging_packet.header.seqNum = 0;
         dwt_writetxfctrl(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), 0, 1);
         dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ranging_packet.round_trip_time), (uint8_t*)&ranging_packet, 0);
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us + RECEIVE_EARLY_START_US + ((initiator_slot_first - current_slot) * RANGING_ITERATION_INTERVAL_US)));
         current_slot = initiator_slot_first;
         if (dwt_starttx(last_rx_relative_to_transmit ? DWT_START_TX_DLY_TS : DWT_START_TX_DLY_RS) != DWT_SUCCESS)
         {
            print("ERROR: Failed to transmit RANGING REQUEST packet after error\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
      else if (current_slot <= responder_slots[responder_slot_index])
      {
         last_rx_delay_time_us += (uint32_t)(responder_slots[responder_slot_index] - current_slot) * RANGING_ITERATION_INTERVAL_US;
         dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
         current_slot = (uint8_t)responder_slots[responder_slot_index];
         if (!ranging_radio_rxenable(last_rx_relative_to_transmit ? DWT_START_RX_DLY_TS : DWT_START_RX_DLY_RS))
         {
            print("ERROR: Unable to start listening for new RANGING REQUEST packets after error\n");
            return RANGING_ERROR;
         }
         return RANGING_PHASE;
      }
   }
   else
   {
      // We are the responder and did not receive a response on the non-final antenna, skip to the next antenna
      ranging_radio_choose_antenna(++antenna_index);
      last_rx_delay_time_us += (((ranging_packet.header.seqNum == 11) || (ranging_packet.header.seqNum == 3)) ? 4 : 2) * RANGING_BROADCAST_INTERVAL_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      ranging_packet.header.seqNum = 11;
      if (!ranging_radio_rxenable(last_rx_relative_to_transmit ? DWT_START_RX_DLY_TS : DWT_START_RX_DLY_RS))
      {
         print("ERROR: Failed to start listening for next RANGING packet after error\n");
         return RANGING_ERROR;
      }
      return RANGING_PHASE;
   }

   // Move to the Range Status Phase of the ranging protocol
   current_phase = RANGE_STATUS_PHASE;
   return status_phase_begin(scheduled_slot, total_num_slots, last_rx_delay_time_us + RECEIVE_EARLY_START_US + ((num_sub_slots - current_slot) * RANGING_ITERATION_INTERVAL_US), last_rx_relative_to_transmit);
}

uint32_t ranging_phase_get_time_slices(void)
{
   return num_sub_slots;
}
