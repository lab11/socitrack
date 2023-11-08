// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "subscription_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t current_phase;
static subscription_packet_t subscription_packet;
static uint8_t schedule_index, schedule_length;
static uint32_t next_action_timestamp;
static uint64_t reference_time;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline scheduler_phase_t start_tx(const char *error_message)
{
   next_action_timestamp += (rand() % (SUBSCRIPTION_TIMEOUT_US - 200));
   dwt_setdelayedtrxtime((uint32_t)((US_TO_DWT(next_action_timestamp) - TX_ANTENNA_DELAY) >> 8) & 0xFFFFFFFE);
   if (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS)
   {
      print(error_message);
      return RANGING_ERROR;
   }
   return SUBSCRIPTION_PHASE;
}

static inline scheduler_phase_t start_rx(const char *error_message, int mode)
{
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp - RECEIVE_EARLY_START_US));
   if (dwt_rxenable(mode) != DWT_SUCCESS)
   {
      print(error_message);
      return RANGING_ERROR;
   }
   return SUBSCRIPTION_PHASE;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void subscription_phase_initialize(const uint8_t *uid)
{
   // Initialize all Subscription Phase parameters
   subscription_packet = (subscription_packet_t){ .header = { .frameCtrl = { 0x41, 0x88 }, .msgType = SUBSCRIPTION_PACKET,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } }, .footer = { { 0 } } };
   memcpy(subscription_packet.header.sourceAddr, uid, sizeof(subscription_packet.header.sourceAddr));
   srand(dwt_readsystimestamphi32());
}

scheduler_phase_t subscription_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t start_delay_dwt)
{
   // Initialize the Subscription Phase start time for calculating timing offsets
   current_phase = SUBSCRIPTION_PHASE;
   schedule_index = scheduled_slot;
   schedule_length = schedule_size;
   next_action_timestamp = RECEIVE_EARLY_START_US;
   reference_time = ((uint64_t)start_delay_dwt) << 8;
   dwt_setreferencetrxtime(start_delay_dwt);

   // Reset the necessary Subscription Phase parameters
   if (scheduled_slot == UNSCHEDULED_SLOT)
   {
      dwt_writetxfctrl(sizeof(subscription_packet_t), 0, 0);
      dwt_writetxdata(sizeof(subscription_packet_t) - sizeof(ieee154_footer_t), (uint8_t*)&subscription_packet, 0);
      return start_tx("ERROR: Failed to transmit SUBSCRIPTION request packet\n");
   }
   else if (!scheduled_slot)
   {
      // Set up the correct initial antenna and RX timeout duration
      ranging_radio_choose_antenna(0);
      dwt_setpreambledetecttimeout(0);
      dwt_setrxtimeout(DW_TIMEOUT_FROM_US(SUBSCRIPTION_TIMEOUT_US));
      dwt_setdelayedtrxtime(0);
      return start_rx("ERROR: Unable to start listening for SUBSCRIPTION packets\n", DWT_START_RX_DLY_REF);
   }

   // Immediately transition to the Ranging Phase if already scheduled
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(schedule_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(SUBSCRIPTION_BROADCAST_PERIOD_US)) >> 8) & 0xFFFFFFFE);
}

scheduler_phase_t subscription_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Subscription Phase
   if (current_phase != SUBSCRIPTION_PHASE)
      return ranging_phase_tx_complete();
   return RANGING_ERROR;
}

scheduler_phase_t subscription_phase_rx_complete(subscription_packet_t* packet)
{
   // Ensure that this packet is of the expected type
   if (current_phase != SUBSCRIPTION_PHASE)
      return ranging_phase_rx_complete((ranging_packet_t*)packet);
   else if (packet->header.msgType != SUBSCRIPTION_PACKET)
   {
      print("ERROR: Received an unexpected message type during SUBSCRIPTION phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }

   // Attempt to schedule the requesting device then re-enable listening
   current_phase = RANGING_PHASE;
   schedule_phase_add_device(packet->header.sourceAddr[0]);
   return ranging_phase_begin(schedule_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(SUBSCRIPTION_BROADCAST_PERIOD_US)) >> 8) & 0xFFFFFFFE);
}

scheduler_phase_t subscription_phase_rx_error(void)
{
   // Re-enable listening for additional Subscription packets
   uint32_t time_expired_us = DWT_TO_US((uint64_t)(dwt_readsystimestamphi32() - (uint32_t)(reference_time >> 8)) << 8);
   if ((time_expired_us + 400) < SUBSCRIPTION_TIMEOUT_US)
   {
      dwt_setrxtimeout(DW_TIMEOUT_FROM_US(SUBSCRIPTION_TIMEOUT_US - time_expired_us));
      start_rx("ERROR: Unable to restart listening for SUBSCRIPTION packets after error\n", DWT_START_RX_IMMEDIATE);
      return SUBSCRIPTION_PHASE;
   }
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(schedule_index, schedule_length, (uint32_t)((reference_time + US_TO_DWT(SUBSCRIPTION_BROADCAST_PERIOD_US)) >> 8) & 0xFFFFFFFE);
}
