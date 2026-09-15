// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "computation_phase.h"
#include "logging.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "status_phase.h"
#include "subscription_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static status_success_packet_t success_packet;
static uint8_t current_index, scheduled_slot, total_num_slots, transmit_index;
static uint32_t status_phase_start;
static uint16_t present_slots, directly_heard;
static uint8_t pending_subscriber;


// Private Helper Functions --------------------------------------------------------------------------------------------

static uint8_t transmit_tier(uint8_t slot, uint16_t master_heard, uint8_t nearest)
{
   // 0: the master could not hear this device, so it transmits early and feeds those that follow
   // 1: the master could hear it
   // 2: this is the closest device the master could hear and most likely to still be audible
   if (slot && (slot == nearest))
      return 2;
   return ((master_heard >> slot) & 1u) ? 1 : 0;
}

static uint8_t compute_transmit_index(uint8_t my_slot, uint8_t total, uint16_t master_heard, uint8_t nearest)
{
   // A strict total order every device derives identically from the schedule: by tier then by descending slot
   if (!my_slot)
      return total;
   const uint8_t mine = transmit_tier(my_slot, master_heard, nearest);
   uint8_t index = 1;
   for (uint8_t slot = 1; slot < total; ++slot)
   {
      if (slot == my_slot)
         continue;
      const uint8_t tier = transmit_tier(slot, master_heard, nearest);
      if (tier != mine)
      {
         if (tier < mine)
            ++index;
      }
      else if (slot > my_slot)
         ++index;
   }
   return index;
}

static inline uint32_t status_slot_time(uint32_t index)
{
   return status_phase_start + ((index - 1) * RANGE_STATUS_BROADCAST_PERIOD_US);
}

static inline scheduler_phase_t start_tx(const char *error_message)
{
   // Announce everything known so far
   success_packet.heard_slots = present_slots;
   success_packet.pending_subscriber = pending_subscriber;
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(status_slot_time(current_index)));
   if ((dwt_writetxdata(sizeof(status_success_packet_t) - sizeof(ieee154_footer_t), (uint8_t*)&success_packet, 0) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
   {
      print(error_message);
      return RANGE_COMPUTATION_PHASE;
   }
   return RANGE_STATUS_PHASE;
}

static inline scheduler_phase_t start_rx(const char *error_message)
{
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(status_slot_time(current_index) - RECEIVE_EARLY_START_US));
   if (dwt_rxenable(DWT_START_RX_DLY_REF | DWT_IDLE_ON_DLY_ERR) != DWT_SUCCESS)
   {
      print(error_message);
      return RANGE_COMPUTATION_PHASE;
   }
   return RANGE_STATUS_PHASE;
}

static scheduler_phase_t advance_to_next_slot(void)
{
   if (++current_index >= total_num_slots)
      return RANGE_COMPUTATION_PHASE;
   return (current_index == transmit_index) ?
         start_tx("ERROR: Failed to transmit STATUS packet\n") :
         start_rx("ERROR: Unable to re-enable listening for STATUS packets\n");
}


// Public API Functions ------------------------------------------------------------------------------------------------

void status_phase_initialize(const uint8_t *uid)
{
   // Initialize all Status Phase parameters
   success_packet = (status_success_packet_t){ .header = { .msgType = STATUS_SUCCESS_PACKET },
      .src_addr = uid[0], .heard_slots = 0, .pending_subscriber = 0, .footer = { { 0 } } };
}

void status_phase_reset(void)
{
   // Called for a round that never reaches this phase
   present_slots = 0;
   directly_heard = 0;
   pending_subscriber = 0;
}

scheduler_phase_t status_phase_begin(uint8_t status_slot, uint8_t num_slots, uint32_t next_action_time)
{
   // Reset the necessary Status Phase parameters
   current_index = 1;
   scheduled_slot = status_slot;
   status_phase_start = next_action_time;
   total_num_slots = (num_slots > MAX_NUM_RANGING_DEVICES) ? MAX_NUM_RANGING_DEVICES : num_slots;
   transmit_index = compute_transmit_index(scheduled_slot, total_num_slots, schedule_phase_get_master_heard(), schedule_phase_get_master_nearest());

   // Seed with what this device knows first-hand
   present_slots = ranging_phase_get_heard_slots();
   directly_heard = present_slots;
   if (status_slot < MAX_NUM_RANGING_DEVICES)
      present_slots |= (uint16_t)(1u << status_slot);

   // A subscription request this device overheard rides to the master in the packet below
   pending_subscriber = subscription_phase_get_heard_subscriber();

   dwt_writetxfctrl(sizeof(status_success_packet_t), 0, 0);

   // Set up the correct initial antenna and RX timeout duration
   ranging_radio_choose_antenna(0);
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGE_STATUS_TIMEOUT_US));

   // Begin transmission or reception depending on the scheduled time slot
   return (transmit_index == current_index) ?
         start_tx("ERROR: Failed to transmit initial STATUS packet\n") :
         start_rx("ERROR: Unable to start listening for STATUS packets\n");
}

scheduler_phase_t status_phase_tx_complete(void)
{
   return advance_to_next_slot();
}

scheduler_phase_t status_phase_rx_complete(status_success_packet_t* packet)
{
   // Ensure that this packet is of the expected type
   if (packet->header.msgType != STATUS_SUCCESS_PACKET)
   {
      print("ERROR: Received an unexpected message type during STATUS phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }

   // Take on everything the sender knows so that whatever this device transmits next carries it onward
   const uint8_t sender_slot = schedule_phase_get_slot_from_addr(packet->src_addr);
   if (sender_slot < MAX_NUM_RANGING_DEVICES)
   {
      directly_heard |= (uint16_t)(1u << sender_slot);
      present_slots |= (uint16_t)(1u << sender_slot);
   }
   present_slots |= packet->heard_slots;
   if (!pending_subscriber)
      pending_subscriber = packet->pending_subscriber;
   return advance_to_next_slot();
}

scheduler_phase_t status_phase_rx_error(void)
{
   return advance_to_next_slot();
}

uint16_t status_phase_get_present_slots(void)
{
   // The union of every device's view that reached this one
   return present_slots;
}

uint8_t status_phase_get_pending_subscriber(void)
{
   return pending_subscriber;
}

uint16_t status_phase_get_directly_heard(void)
{
   // Only what this device received first-hand this round with nothing relayed folded in
   return directly_heard;
}
