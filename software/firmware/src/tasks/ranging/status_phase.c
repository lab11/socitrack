// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "computation_phase.h"
#include "logging.h"
#include "ranging_phase.h"
#include "status_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static status_success_packet_t success_packet;
static uint8_t current_slot, scheduled_slot, total_num_slots;
static uint32_t transmitted_seq_num, last_rx_delay_time_us;
static uint8_t present_devices[MAX_NUM_RANGING_DEVICES], num_present_devices;
static bool last_rx_relative_to_transmit;


// Public API Functions ------------------------------------------------------------------------------------------------

void status_phase_initialize(const uint8_t *uid)
{
   // Initialize all Schedule Phase parameters
   success_packet = (status_success_packet_t){ .header = { .frameCtrl = { 0x41, 0x98 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = STATUS_SUCCESS_PACKET, .success = 0, .footer = { { 0 } } };
   memcpy(success_packet.header.sourceAddr, uid, sizeof(success_packet.header.sourceAddr));
   scheduled_slot = 0xFF;
}

scheduler_phase_t status_phase_begin(uint8_t status_slot, uint8_t num_slots, uint32_t start_delay_us, bool start_relative_to_transmit)
{
   // Reset the necessary Schedule Phase parameters
   current_slot = 1;
   num_present_devices = 0;
   total_num_slots = num_slots;
   scheduled_slot = status_slot;
   success_packet.header.seqNum = 0;
   success_packet.success = responses_received();
   memset(present_devices, 0, sizeof(present_devices));
   dwt_writetxfctrl(sizeof(status_success_packet_t), 0, 0);

   // Set up the correct initial antenna and RX timeout duration
   ranging_radio_choose_antenna(RANGE_STATUS_XMIT_ANTENNA);
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGE_STATUS_TIMEOUT_US));

   // Begin transmission or reception depending on the scheduled time slot
   if (scheduled_slot == current_slot)
   {
      transmitted_seq_num = 0;
      dwt_writetxdata(sizeof(status_success_packet_t), (uint8_t*)&success_packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(start_delay_us));
      if (dwt_starttx(start_relative_to_transmit ? DWT_START_TX_DLY_TS : DWT_START_TX_DLY_RS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit STATUS packet\n");
         return RANGE_COMPUTATION_PHASE;
      }
   }
   else
   {
      last_rx_relative_to_transmit = start_relative_to_transmit;
      last_rx_delay_time_us = start_delay_us - RECEIVE_EARLY_START_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      if (!ranging_radio_rxenable(start_relative_to_transmit ? DWT_START_RX_DLY_TS : DWT_START_RX_DLY_RS))
      {
         print("ERROR: Unable to start listening for STATUS packets\n");
         return RANGE_COMPUTATION_PHASE;
      }
   }
   return RANGE_STATUS_PHASE;
}

scheduler_phase_t status_phase_tx_complete(void)
{
   if (++current_slot == scheduled_slot)
   {
      dwt_writetxdata(sizeof(status_success_packet_t), (uint8_t*)&success_packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(RANGE_STATUS_BROADCAST_PERIOD_US - (transmitted_seq_num * RANGE_STATUS_RESEND_INTERVAL_US)));
      transmitted_seq_num = 0;
      if (dwt_starttx(DWT_START_TX_DLY_TS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit STATUS packet after prior transmission\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }
   else if (current_slot < total_num_slots)
   {
      // Move back into listening mode for additional status responses
      last_rx_relative_to_transmit = true;
      last_rx_delay_time_us = RANGE_STATUS_BROADCAST_PERIOD_US - (transmitted_seq_num * RANGE_STATUS_RESEND_INTERVAL_US) - RECEIVE_EARLY_START_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      transmitted_seq_num = 0;
      if (!ranging_radio_rxenable(DWT_START_RX_DLY_TS))
      {
         print("ERROR: Unable to re-enable listening for STATUS packets\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }

   // Move to the Range Computation Phase of the ranging protocol
   return RANGE_COMPUTATION_PHASE;
}

scheduler_phase_t status_phase_rx_complete(status_success_packet_t* packet)
{
   // Ensure that this packet is of the expected type
   if (packet->message_type != STATUS_SUCCESS_PACKET)
   {
      print("ERROR: Received an unexpected message type during STATUS phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }

   // Record the presence of the transmitting device
   if (!scheduled_slot)
      present_devices[num_present_devices++] = packet->header.sourceAddr[0];

   // Retransmit the status packet upon reception
   const uint32_t seqNum = packet->header.seqNum;
   if (scheduled_slot && (scheduled_slot <= RANGE_STATUS_NUM_TOTAL_BROADCASTS) && (packet->header.seqNum < scheduled_slot))
   {
      packet->header.seqNum = scheduled_slot - 1;
      transmitted_seq_num = packet->header.seqNum;
      dwt_writetxdata(sizeof(status_success_packet_t), (uint8_t*)packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US((transmitted_seq_num - seqNum) * RANGE_STATUS_RESEND_INTERVAL_US));
      if (dwt_starttx(DWT_START_TX_DLY_RS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to retransmit received STATUS packet\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }
   else if (++current_slot == scheduled_slot)
   {
      transmitted_seq_num = 0;
      dwt_writetxdata(sizeof(status_success_packet_t), (uint8_t*)&success_packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(RANGE_STATUS_BROADCAST_PERIOD_US - (seqNum * RANGE_STATUS_RESEND_INTERVAL_US)));
      if (dwt_starttx(DWT_START_TX_DLY_RS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit STATUS packet\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }
   else if (current_slot < total_num_slots)
   {
      // Move back into listening mode for additional status responses
      last_rx_relative_to_transmit = false;
      last_rx_delay_time_us = RANGE_STATUS_BROADCAST_PERIOD_US - (seqNum * RANGE_STATUS_RESEND_INTERVAL_US) - RECEIVE_EARLY_START_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      if (!ranging_radio_rxenable(DWT_START_RX_DLY_RS))
      {
         print("ERROR: Unable to re-enable listening for STATUS packets after reception\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }

   // Move to the Range Computation Phase of the ranging protocol
   return RANGE_COMPUTATION_PHASE;
}

scheduler_phase_t status_phase_rx_error(void)
{
   // Move to the next expected status packet to receive
   if (++current_slot == scheduled_slot)
   {
      transmitted_seq_num = 0;
      dwt_writetxdata(sizeof(status_success_packet_t), (uint8_t*)&success_packet, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us + RECEIVE_EARLY_START_US + RANGE_STATUS_BROADCAST_PERIOD_US));
      if (dwt_starttx(last_rx_relative_to_transmit ? DWT_START_TX_DLY_TS : DWT_START_TX_DLY_RS) != DWT_SUCCESS)
      {
         print("ERROR: Failed to transmit STATUS packet after error\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }
   else if (current_slot < total_num_slots)
   {
      last_rx_delay_time_us += RANGE_STATUS_BROADCAST_PERIOD_US;
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(last_rx_delay_time_us));
      if (!ranging_radio_rxenable(last_rx_relative_to_transmit ? DWT_START_RX_DLY_TS : DWT_START_RX_DLY_RS))
      {
         print("ERROR: Unable to re-enable listening for STATUS packets after error\n");
         return RANGE_COMPUTATION_PHASE;
      }
      return RANGE_STATUS_PHASE;
   }

   // Move to the Range Computation Phase of the ranging protocol
   return RANGE_COMPUTATION_PHASE;
}

const uint8_t* status_phase_get_detected_devices(uint8_t *num_devices)
{
   *num_devices = num_present_devices;
   return present_devices;
}
