// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "computation_phase.h"
#include "ranging_phase.h"
#include "status_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t current_phase;
static ranging_packet_t ranging_packet;
static uint8_t initiator_index, responder_index, my_index;
static uint8_t initiator_antenna, responder_antenna, schedule_length;
static uint32_t next_action_timestamp, ranging_phase_duration;
static uint64_t reference_time;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline scheduler_phase_t start_tx(const char *error_message)
{
   dwt_setdelayedtrxtime((uint32_t)((US_TO_DWT(next_action_timestamp) - TX_ANTENNA_DELAY) >> 8) & 0xFFFFFFFE);
   if ((dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ieee154_header_t) - sizeof(ieee154_footer_t), &ranging_packet.sequence_number, offsetof(ranging_packet_t, sequence_number)) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
   {
      print(error_message);
      return RADIO_ERROR;
   }
   return RANGING_PHASE;
}

static inline scheduler_phase_t start_rx(const char *error_message)
{
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp - RECEIVE_EARLY_START_US));
   if (dwt_rxenable(DWT_START_RX_DLY_REF) != DWT_SUCCESS)
   {
      print(error_message);
      return RADIO_ERROR;
   }
   return RANGING_PHASE;
}

static scheduler_phase_t initiator_handle_tx(void)
{
   // Listen for an incoming RESPONSE or SUMMARY packet
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   return start_rx("ERROR: Unable to start listening for RANGING packets after INITIATOR TX\n");
}

static scheduler_phase_t initiator_handle_rx(ranging_packet_t *packet)
{
   // Verify the received packet sequence number
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   ranging_packet.sequence_number = packet->sequence_number + 1;
   if ((packet->sequence_number & 0x03) == 1)
   {
      // Update the packet round-trip times and transit the FINAL packet
      add_ranging_times_poll_tx(packet->header.sourceAddr[0], packet->sequence_number >> 2, ranging_packet.tx_time);
      add_ranging_times_poll_rx(packet->header.sourceAddr[0], packet->sequence_number >> 2, packet->rx_time);
      add_ranging_times_response_tx(packet->header.sourceAddr[0], packet->sequence_number >> 2, packet->tx_time);
      ranging_packet.rx_time = (uint32_t)ranging_radio_readrxtimestamp();
      ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;
      add_ranging_times_response_rx(packet->header.sourceAddr[0], packet->sequence_number >> 2, ranging_packet.rx_time);
      add_ranging_times_final_tx(packet->header.sourceAddr[0], packet->sequence_number >> 2, ranging_packet.tx_time);
      return start_tx("ERROR: Failed to transmit RANGING FINAL packet\n");
   }
   else if ((packet->sequence_number & 0x03) == 3)
   {
      // Update the packet round-trip times and the antenna settings
      add_ranging_times_final_rx(packet->header.sourceAddr[0], packet->sequence_number >> 2, packet->rx_time);
      responder_antenna = (responder_antenna + 1) % NUM_RCV_ANTENNAS;
      if (responder_antenna == 0)
      {
         initiator_antenna = (initiator_antenna + 1) % NUM_XMIT_ANTENNAS;
         ranging_radio_choose_antenna(initiator_antenna);
         if (initiator_antenna == 0)
         {
            ranging_packet.sequence_number = 0;
            responder_index = (responder_index + 1) % schedule_length;
            if (responder_index == 0)
            {
               // Move to the Range Status Phase of the ranging protocol
               current_phase = RANGE_STATUS_PHASE;
               uint32_t remaining = schedule_length - my_index - 1;
               next_action_timestamp += ((remaining * (remaining - 1) / 2) * RANGING_US_PER_RANGE);
               return status_phase_begin(my_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(next_action_timestamp - RECEIVE_EARLY_START_US)) >> 8) & 0xFFFFFFFE);
            }
         }
      }

      // Transmit the next POLL packet
      ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;
      return start_tx("ERROR: Failed to transmit RANGING POLL packet\n");
   }
   print("ERROR: Received an unexpected RANGING sequence number for initiator...possible network collision\n");
   return MESSAGE_COLLISION;
}

static scheduler_phase_t responder_handle_tx(void)
{
   // Verify the received packet sequence number
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   if ((ranging_packet.sequence_number & 0x03) == 3)
   {
      // Update the initiator and responder antenna settings
      responder_antenna = (responder_antenna + 1) % NUM_RCV_ANTENNAS;
      ranging_radio_choose_antenna(responder_antenna);
      if (responder_antenna == 0)
      {
         initiator_antenna = (initiator_antenna + 1) % NUM_XMIT_ANTENNAS;
         if (initiator_antenna == 0)
         {
            // Update the initiator and responder active indices
            responder_index = (responder_index + 1) % schedule_length;
            if (responder_index == 0)
            {
               initiator_index += 1;
               if (initiator_index == (schedule_length - 1))
               {
                  // Move to the Range Status Phase of the ranging protocol
                  current_phase = RANGE_STATUS_PHASE;
                  return status_phase_begin(my_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(next_action_timestamp - RECEIVE_EARLY_START_US)) >> 8) & 0xFFFFFFFE);
               }
               else
               {
                  next_action_timestamp += (uint32_t)(my_index - initiator_index - 1) * RANGING_US_PER_RANGE;
                  responder_index = my_index;
               }
            }
            else
            {
               // Determine if we are the next initiator
               if (my_index == ++initiator_index)
               {
                  // Transmit the next POLL packet at the correct time
                  ranging_packet.sequence_number = 0;
                  next_action_timestamp += (schedule_length - responder_index) * RANGING_US_PER_RANGE;
                  ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;
                  responder_index = my_index + 1;
                  return start_tx("ERROR: Failed to transmit next RANGING POLL packet\n");
               }
               else
               {
                  next_action_timestamp += (uint32_t)(schedule_length - responder_index) * RANGING_US_PER_RANGE;
                  responder_index = my_index;
               }
            }
         }
      }
   }

   // Enter RX mode at the appropriate time
   return start_rx("ERROR: Unable to start listening for RANGING packets after RESPONDER TX\n");
}

static scheduler_phase_t responder_handle_rx(ranging_packet_t *packet)
{
   // Update the ranging packet contents for the upcoming transmission
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   ranging_packet.sequence_number = packet->sequence_number + 1;
   ranging_packet.rx_time = (uint32_t)ranging_radio_readrxtimestamp();
   ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;

   // Update the packet round-trip times
   if ((packet->sequence_number & 0x03) == 0)
   {
      add_ranging_times_poll_tx(packet->header.sourceAddr[0], packet->sequence_number >> 2, packet->tx_time);
      add_ranging_times_poll_rx(packet->header.sourceAddr[0], packet->sequence_number >> 2, ranging_packet.rx_time);
      add_ranging_times_response_tx(packet->header.sourceAddr[0], packet->sequence_number >> 2, ranging_packet.tx_time);
      return start_tx("ERROR: Failed to transmit next RANGING RESPONSE packet\n");
   }
   else if ((packet->sequence_number & 0x03) == 2)
   {
      add_ranging_times_response_rx(packet->header.sourceAddr[0], packet->sequence_number >> 2, packet->rx_time);
      add_ranging_times_final_tx(packet->header.sourceAddr[0], packet->sequence_number >> 2, packet->tx_time);
      add_ranging_times_final_rx(packet->header.sourceAddr[0], packet->sequence_number >> 2, ranging_packet.rx_time);
      return start_tx("ERROR: Failed to transmit next RANGING SUMMARY packet\n");
   }
   print("ERROR: Received an unexpected RANGING sequence number for responder...possible network collision\n");
   return MESSAGE_COLLISION;
}


// Public Functions ----------------------------------------------------------------------------------------------------

void ranging_phase_initialize(const uint8_t *uid)
{
   // Initialize all Ranging Phase parameters
   ranging_packet = (ranging_packet_t){ .header = { .frameCtrl = { 0x41, 0x88 }, .msgType = RANGING_PACKET,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .sequence_number = 0, .tx_time = 0, .rx_time = 0, .footer = { { 0 } } };
   memcpy(ranging_packet.header.sourceAddr, uid, sizeof(ranging_packet.header.sourceAddr));
}

scheduler_phase_t ranging_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t start_delay_dwt)
{
   // Ensure there are at least two devices to begin ranging
   reset_computation_phase();
   my_index = scheduled_slot;
   ranging_phase_duration = ((uint32_t)schedule_size * (schedule_size - 1) / 2) * RANGING_US_PER_RANGE;
   if ((schedule_size < 2) || (my_index == UNSCHEDULED_SLOT))
      return RANGE_COMPUTATION_PHASE;

   // Reset the necessary Ranging Phase parameters
   current_phase = RANGING_PHASE;
   schedule_length = schedule_size;
   initiator_antenna = responder_antenna = 0;
   next_action_timestamp = RECEIVE_EARLY_START_US;
   dwt_writetxfctrl(sizeof(ranging_packet_t), 0, 1);
   dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ieee154_footer_t), (uint8_t*)&ranging_packet, 0);
   initiator_index = 0;
   responder_index = 1;

   // Initialize the Ranging Phase start time for calculating timing offsets
   reference_time = ((uint64_t)start_delay_dwt) << 8;
   dwt_setreferencetrxtime(start_delay_dwt);

   // Set up the correct initial antenna and RX timeout duration
   ranging_radio_choose_antenna(0);
   dwt_setpreambledetecttimeout(DW_PREAMBLE_TIMEOUT);
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGING_TIMEOUT_US));

   // Begin transmission or reception depending on the scheduled time slot
   if (my_index == 0)
   {
      ranging_packet.sequence_number = 0;
      ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;
      return start_tx("ERROR: Failed to transmit initial RANGING POLL packet\n");
   }
   else
   {
      responder_index = my_index;
      ranging_packet.sequence_number = 0x03;
      next_action_timestamp += (uint32_t)(my_index - 1) * RANGING_US_PER_RANGE;
      return start_rx("ERROR: Unable to start listening for RANGING POLL packets\n");
   }
}

scheduler_phase_t ranging_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_tx_complete();
   return (initiator_index == my_index) ? initiator_handle_tx() : responder_handle_tx();
}

scheduler_phase_t ranging_phase_rx_complete(ranging_packet_t* packet)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_rx_complete((status_success_packet_t*)packet);
   else if (packet->header.msgType != RANGING_PACKET)
   {
      print("ERROR: Received an unexpected message type during RANGING phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }
   return (initiator_index == my_index) ? initiator_handle_rx(packet) : responder_handle_rx(packet);
}

scheduler_phase_t ranging_phase_rx_error(void)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_rx_error();

   // Handle RX errors differently depending on which sequence number experienced the problem
   if (((ranging_packet.sequence_number & 0x03) == 0) || ((ranging_packet.sequence_number & 0x03) == 2))
   {
      // We are the initiator and didn't receive a response
      uint32_t skip_packets = 3 - (ranging_packet.sequence_number & 0x03);
      next_action_timestamp += (skip_packets * RANGING_BROADCAST_INTERVAL_US);
      ranging_packet.sequence_number += skip_packets + 1;

      // Update the antenna settings
      responder_antenna = (responder_antenna + 1) % NUM_RCV_ANTENNAS;
      if (responder_antenna == 0)
      {
         initiator_antenna = (initiator_antenna + 1) % NUM_XMIT_ANTENNAS;
         ranging_radio_choose_antenna(initiator_antenna);
         if (initiator_antenna == 0)
         {
            responder_index = (responder_index + 1) % schedule_length;
            if (responder_index == 0)
            {
               // Move to the Range Status Phase of the ranging protocol
               current_phase = RANGE_STATUS_PHASE;
               uint32_t remaining = schedule_length - my_index - 1;
               next_action_timestamp += ((remaining * (remaining - 1) / 2) * RANGING_US_PER_RANGE);
               return status_phase_begin(my_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(next_action_timestamp - RECEIVE_EARLY_START_US)) >> 8) & 0xFFFFFFFE);
            }
         }
      }

      // Transmit the next POLL packet
      ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;
      return start_tx("ERROR: Failed to transmit RANGING POLL packet after INITIATOR RX error\n");
   }
   else  // if (((ranging_packet.sequence_number & 0x03) == 1) || ((ranging_packet.sequence_number & 0x03) == 3))
   {
      // We are the responder and didn't receive a response
      uint32_t skip_packets = 1 + (ranging_packet.sequence_number & 0x03);
      next_action_timestamp += (skip_packets * RANGING_BROADCAST_INTERVAL_US);
      ranging_packet.sequence_number += skip_packets;

      // Update the initiator and responder antenna settings
      responder_antenna = (responder_antenna + 1) % NUM_RCV_ANTENNAS;
      ranging_radio_choose_antenna(responder_antenna);
      if (responder_antenna == 0)
      {
         initiator_antenna = (initiator_antenna + 1) % NUM_XMIT_ANTENNAS;
         if (initiator_antenna == 0)
         {
            // Update the initiator and responder active indices
            responder_index = (responder_index + 1) % schedule_length;
            if (responder_index == 0)
            {
               initiator_index += 1;
               if (initiator_index == (schedule_length - 1))
               {
                  // Move to the Range Status Phase of the ranging protocol
                  current_phase = RANGE_STATUS_PHASE;
                  return status_phase_begin(my_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(next_action_timestamp - RECEIVE_EARLY_START_US)) >> 8) & 0xFFFFFFFE);
               }
               else
               {
                  next_action_timestamp += (uint32_t)(my_index - initiator_index - 1) * RANGING_US_PER_RANGE;
                  responder_index = my_index;
               }
            }
            else
            {
               // Determine if we are the next initiator
               if (my_index == ++initiator_index)
               {
                  // Transmit the next POLL packet at the correct time
                  ranging_packet.sequence_number = 0;
                  next_action_timestamp += ((schedule_length - responder_index) * RANGING_US_PER_RANGE);
                  responder_index = my_index + 1;
                  ranging_packet.tx_time = (uint32_t)(reference_time + US_TO_DWT(next_action_timestamp)) & 0xFFFFFE00;
                  return start_tx("ERROR: Failed to transmit next RANGING POLL packet after an RX error\n");
               }
               else
               {
                  next_action_timestamp += (uint32_t)(schedule_length - responder_index) * RANGING_US_PER_RANGE;
                  responder_index = my_index;
               }
            }
         }
      }

      // Attempt to receive the next ranging packet
      return start_rx("ERROR: Unable to start listening for RANGING packets after RESPONDER RX error\n");
   }
}

uint32_t ranging_phase_get_duration(void)
{
   return ranging_phase_duration;
}

bool ranging_phase_was_scheduled(void)
{
   return (my_index != UNSCHEDULED_SLOT);
}
