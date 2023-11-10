// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "computation_phase.h"
#include "logging.h"
#include "ranging_phase.h"
#include "status_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static status_success_packet_t success_packet;
static uint8_t current_slot, scheduled_slot, total_num_slots;
static uint32_t transmitted_seq_num, next_action_timestamp;
static uint8_t present_devices[MAX_NUM_RANGING_DEVICES], num_present_devices;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline scheduler_phase_t start_tx(const char *error_message, status_success_packet_t *packet)
{
   transmitted_seq_num = packet->sequence_number;
   dwt_setdelayedtrxtime((uint32_t)((US_TO_DWT(next_action_timestamp) - TX_ANTENNA_DELAY) >> 8) & 0xFFFFFFFE);
   if ((dwt_writetxdata(2 * sizeof(success_packet.sequence_number), &packet->sequence_number, offsetof(status_success_packet_t, sequence_number)) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
   {
      print(error_message);
      return RANGE_COMPUTATION_PHASE;
   }
   return RANGE_STATUS_PHASE;
}

static inline scheduler_phase_t start_rx(const char *error_message)
{
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp - RECEIVE_EARLY_START_US));
   if (dwt_rxenable(DWT_START_RX_DLY_REF) != DWT_SUCCESS)
   {
      print(error_message);
      return RANGE_COMPUTATION_PHASE;
   }
   return RANGE_STATUS_PHASE;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void status_phase_initialize(const uint8_t *uid)
{
   // Initialize all Schedule Phase parameters
   success_packet = (status_success_packet_t){ .header = { .frameCtrl = { 0x41, 0x88 }, .msgType = STATUS_SUCCESS_PACKET,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .sequence_number = 0, .success = 0, .footer = { { 0 } } };
   memcpy(success_packet.header.sourceAddr, uid, sizeof(success_packet.header.sourceAddr));
}

scheduler_phase_t status_phase_begin(uint8_t status_slot, uint8_t num_slots, uint32_t start_delay_dwt)
{
   // Reset the necessary Schedule Phase parameters
   current_slot = 1;
   num_present_devices = 0;
   total_num_slots = num_slots;
   scheduled_slot = status_slot;
   success_packet.sequence_number = 0;
   success_packet.success = responses_received();
   next_action_timestamp = RECEIVE_EARLY_START_US;
   memset(present_devices, 0, sizeof(present_devices));
   dwt_writetxfctrl(sizeof(status_success_packet_t), 0, 0);
   dwt_writetxdata(sizeof(status_success_packet_t) - sizeof(ieee154_footer_t), (uint8_t*)&success_packet, 0);

   // Set up the correct initial start time, antenna, and RX timeout duration
   ranging_radio_choose_antenna(0);
   dwt_setreferencetrxtime(start_delay_dwt + DW_DELAY_FROM_US(1000 - RANGING_BROADCAST_INTERVAL_US));
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGE_STATUS_TIMEOUT_US));

   // Begin transmission or reception depending on the scheduled time slot
   return (scheduled_slot == current_slot) ?
         start_tx("ERROR: Failed to transmit initial STATUS packet\n", &success_packet) :
         start_rx("ERROR: Unable to start listening for STATUS packets\n");
}

scheduler_phase_t status_phase_tx_complete(void)
{
   next_action_timestamp += RANGE_STATUS_BROADCAST_PERIOD_US - (transmitted_seq_num * RANGE_STATUS_RESEND_INTERVAL_US);
   if (++current_slot == scheduled_slot)
      return start_tx("ERROR: Failed to transmit STATUS packet after prior transmission\n", &success_packet);
   else if (current_slot < total_num_slots)
      return start_rx("ERROR: Unable to re-enable listening for STATUS packets after transmission\n");
   return RANGE_COMPUTATION_PHASE;
}

scheduler_phase_t status_phase_rx_complete(status_success_packet_t* packet)
{
   // Ensure that this packet is of the expected type
   if (packet->header.msgType != STATUS_SUCCESS_PACKET)
   {
      print("ERROR: Received an unexpected message type during STATUS phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }

   // Record the presence of the transmitting device
   if (!scheduled_slot)
      present_devices[num_present_devices++] = packet->header.sourceAddr[0];

   // Retransmit the status packet upon reception
   const uint32_t seqNum = packet->sequence_number;
   if (scheduled_slot && (scheduled_slot <= RANGE_STATUS_NUM_TOTAL_BROADCASTS) && (packet->sequence_number < scheduled_slot))
   {
      packet->sequence_number = (scheduled_slot < current_slot) ? scheduled_slot : (scheduled_slot - 1);
      next_action_timestamp += (packet->sequence_number - seqNum) * RANGE_STATUS_RESEND_INTERVAL_US;
      return start_tx("ERROR: Failed to retransmit received STATUS packet\n", packet);
   }
   else if (++current_slot == scheduled_slot)
   {
      next_action_timestamp += RANGE_STATUS_BROADCAST_PERIOD_US - (seqNum * RANGE_STATUS_RESEND_INTERVAL_US);
      return start_tx("ERROR: Failed to transmit STATUS packet\n", &success_packet);
   }
   else if (current_slot < total_num_slots)
   {
      next_action_timestamp += RANGE_STATUS_BROADCAST_PERIOD_US - (seqNum * RANGE_STATUS_RESEND_INTERVAL_US);
      return start_rx("ERROR: Unable to re-enable listening for STATUS packets after reception\n");
   }
   return RANGE_COMPUTATION_PHASE;
}

scheduler_phase_t status_phase_rx_error(void)
{
   // Move to the next expected status packet to receive
   next_action_timestamp += RANGE_STATUS_BROADCAST_PERIOD_US;
   if (++current_slot == scheduled_slot)
      return start_tx("ERROR: Failed to transmit STATUS packet after error\n", &success_packet);
   else if (current_slot < total_num_slots)
      return start_rx("ERROR: Unable to re-enable listening for STATUS packets after error\n");
   return RANGE_COMPUTATION_PHASE;
}

const uint8_t* status_phase_get_detected_devices(uint8_t *num_devices)
{
   *num_devices = num_present_devices;
   return present_devices;
}
