// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "subscription_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t current_phase;
static subscription_packet_t subscription_packet;
static uint8_t schedule_index, schedule_length;
static uint32_t reference_time;
static uint8_t heard_subscriber;


// Public API Functions ------------------------------------------------------------------------------------------------

void subscription_phase_initialize(const uint8_t *uid)
{
   // Initialize all Subscription Phase parameters
   subscription_packet = (subscription_packet_t){ .header = { .msgType = SUBSCRIPTION_PACKET }, .src_addr = uid[0], .footer = { { 0 } } };

   // Seed from the device identity rather than the radio clock
   unsigned seed = 0;
   for (uint32_t i = 0; i < EUI_LEN; ++i)
      seed = (seed * 31u) + uid[i];
   srand(seed ? seed : 1u);
}

static bool relay_listen_this_round(uint8_t slot, uint8_t schedule_size)
{
   // One relay listens per round rotating over the non-master slots so exactly one is always covering the window
   const uint32_t peers = (schedule_size > 1u) ? (uint32_t)(schedule_size - 1u) : 1u;
   const uint32_t round = schedule_phase_get_timestamp() / (SCHEDULING_INTERVAL_US / 1000u);
   return ((round + slot) % peers) == 0;
}

scheduler_phase_t subscription_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t ref_time)
{
   // Initialize the Subscription Phase parameters
   current_phase = SUBSCRIPTION_PHASE;
   schedule_index = scheduled_slot;
   schedule_length = schedule_size;
   ranging_radio_choose_antenna(0);
   reference_time = ref_time;
   heard_subscriber = 0;

   // Reset the necessary Subscription Phase parameters
   if (schedule_index == UNSCHEDULED_SLOT)
   {
      dwt_writetxfctrl(sizeof(subscription_packet_t), 0, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(SUBSCRIPTION_PHASE_START_US + (rand() % (SUBSCRIPTION_TIMEOUT_US - 100))));
      if ((dwt_writetxdata(sizeof(subscription_packet_t) - sizeof(ieee154_footer_t), (uint8_t*)&subscription_packet, 0) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
         print_isr("ERROR: Failed to transmit SUBSCRIPTION request packet\n");
      else
         return SUBSCRIPTION_PHASE;
   }
   else if ((schedule_length < MAX_NUM_RANGING_DEVICES) && (!schedule_index || relay_listen_this_round(schedule_index, schedule_length)))
   {
      dwt_setpreambledetecttimeout(0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(SUBSCRIPTION_PHASE_START_US - RECEIVE_EARLY_START_US));
      dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RECEIVE_EARLY_START_US + SUBSCRIPTION_TIMEOUT_US));
      if (dwt_rxenable(DWT_START_RX_DLY_REF | DWT_IDLE_ON_DLY_ERR) != DWT_SUCCESS)
         print_isr("ERROR: Unable to start listening for SUBSCRIPTION packets\n");
      else
         return SUBSCRIPTION_PHASE;
   }

   // Transition to the Ranging Phase at the appropriate future time
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(schedule_index, schedule_length, reference_time);
}

scheduler_phase_t subscription_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Subscription Phase
   if (current_phase != SUBSCRIPTION_PHASE)
      return ranging_phase_tx_complete();
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(schedule_index, schedule_length, reference_time);
}

scheduler_phase_t subscription_phase_rx_complete(subscription_packet_t* packet)
{
   // Ensure that this packet is of the expected type and schedule the requesting device
   if (current_phase != SUBSCRIPTION_PHASE)
      return ranging_phase_rx_complete((ranging_packet_t*)packet);
   else if (packet->header.msgType != SUBSCRIPTION_PACKET)
   {
      print_isr("ERROR: Received an unexpected message type during SUBSCRIPTION phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }
   if (!schedule_index)
      schedule_phase_add_device(packet->src_addr);
   else if (!heard_subscriber)
      heard_subscriber = packet->src_addr;
   return subscription_phase_rx_error();
}

scheduler_phase_t subscription_phase_rx_error(void)
{
   // Forward this request to the next phase if not currently in the Subscription Phase
   if (current_phase != SUBSCRIPTION_PHASE)
      return ranging_phase_rx_error();

   // Attempt to re-enable listening for additional Subscription packets
   const uint32_t window_elapsed_dw = (uint32_t)((uint64_t)dwt_readsystimestamphi32() << 8) - reference_time;
   const int32_t time_elapsed_us = (int32_t)DWT_TO_US(window_elapsed_dw) - (int32_t)SUBSCRIPTION_PHASE_START_US;
   if ((time_elapsed_us >= 0) && (((uint32_t)time_elapsed_us + SUBSCRIPTION_RELISTEN_MARGIN_US) <= SUBSCRIPTION_TIMEOUT_US))
   {
      print_isr("INFO: More time left in the Subscription phase...listening again\n");
      dwt_setrxtimeout(DW_TIMEOUT_FROM_US(SUBSCRIPTION_TIMEOUT_US - (uint32_t)time_elapsed_us));
      if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS)
         print_isr("ERROR: Unable to restart listening for SUBSCRIPTION packets\n");
      else
         return SUBSCRIPTION_PHASE;
   }

   // Move on to the Ranging phase
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(schedule_index, schedule_length, reference_time);
}

uint8_t subscription_phase_get_heard_subscriber(void)
{
   // A subscription request this device overheard and that the master may not have
   return heard_subscriber;
}
