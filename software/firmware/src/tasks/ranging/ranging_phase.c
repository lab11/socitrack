// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "computation_phase.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "status_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t current_phase;
static ranging_packet_t ranging_packet;
static ranging_device_state_t measurements[MAX_NUM_RANGING_DEVICES];
static uint32_t time_slot, my_slot, num_slots, slots_per_range, schedule_length;
static uint32_t reference_time, next_action_timestamp, ranging_phase_duration;
static uint16_t tx_payload_length;
static uint8_t current_antenna;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline scheduler_phase_t start_tx(const char *error_message)
{
   // Handle antenna switching functionality
   if ((time_slot % slots_per_range) == 0)
   {
      current_antenna = (current_antenna + 1) % NUM_XMIT_ANTENNAS;
      ranging_radio_choose_antenna(current_antenna);
   }

   // Perform the actual radio transmit
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp));
   dwt_writetxfctrl(sizeof(ieee154_header_t) + sizeof(ieee154_footer_t) + tx_payload_length, 0, 1);
   if ((dwt_writetxdata(tx_payload_length, (uint8_t*)ranging_packet.tx_rx_times, offsetof(ranging_packet_t, tx_rx_times)) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
   {
      print(error_message);
      return RADIO_ERROR;
   }
   return RANGING_PHASE;
}

static inline scheduler_phase_t start_rx(const char *error_message)
{
   // Handle antenna switching functionality
   if ((time_slot % slots_per_range) == 0)
   {
      current_antenna = (current_antenna + 1) % NUM_XMIT_ANTENNAS;
      ranging_radio_choose_antenna(current_antenna);
   }

   // Perform the actual radio receive
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp - RECEIVE_EARLY_START_US));
   if (dwt_rxenable(DWT_START_RX_DLY_REF | DWT_IDLE_ON_DLY_ERR) != DWT_SUCCESS)
   {
      print(error_message);
      return RADIO_ERROR;
   }
   return RANGING_PHASE;
}


// Public Functions ----------------------------------------------------------------------------------------------------

void ranging_phase_initialize(const uint8_t *uid)
{
   // Initialize all Ranging Phase parameters
   ranging_packet = (ranging_packet_t){ .header = { .msgType = RANGING_PACKET },
      .tx_rx_times = { 0 }, .footer = { { 0 } } };
}

scheduler_phase_t ranging_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t ref_time, uint32_t next_action_time)
{
   // Ensure there are at least two devices to begin ranging
   my_slot = scheduled_slot;
   reset_computation_phase(schedule_size);
   memset(&measurements, 0, sizeof(measurements));
   slots_per_range = (uint32_t)schedule_size * (RANGING_NUM_PACKETS_PER_DEVICE - 1);
   num_slots = slots_per_range * RANGING_NUM_RANGE_ATTEMPTS;
   ranging_phase_duration = num_slots * RANGING_BROADCAST_INTERVAL_US;
   if ((schedule_size < 2) || (my_slot == UNSCHEDULED_SLOT))
      return RANGE_COMPUTATION_PHASE;

   // Reset the necessary Ranging Phase parameters
   current_phase = RANGING_PHASE;
   schedule_length = schedule_size;
   next_action_timestamp = next_action_time;
   dwt_writetxdata(sizeof(ranging_packet_t) - sizeof(ieee154_footer_t), (uint8_t*)&ranging_packet, 0);
   tx_payload_length = (uint16_t)((schedule_length - 1) * sizeof(ranging_packet.tx_rx_times[0]));
   current_antenna = NUM_XMIT_ANTENNAS - 1;
   reference_time = ref_time;
   time_slot = 0;

   // Set up the correct RX timeout durations
   dwt_setpreambledetecttimeout(DW_PREAMBLE_TIMEOUT);
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGING_TIMEOUT_US));

   // Begin transmission or reception depending on the scheduled time slot
   return (my_slot == 0) ?
         start_tx("ERROR: Failed to transmit initial RANGING packet\n") :
         start_rx("ERROR: Unable to start listening for RANGING packets\n");
}

scheduler_phase_t ranging_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_tx_complete();

   // Record the packet transmit time in all relevant storage structures
   const div_t slot_results = div(time_slot, slots_per_range);
   register const uint32_t slot = (uint32_t)slot_results.rem, sequence_number = (uint32_t)slot_results.quot;
   if (slot < schedule_length)
   {
      for (uint32_t i = 0; i < my_slot; ++i)
         measurements[i].resp_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(next_action_timestamp) + TX_ANTENNA_DELAY);
      for (uint32_t i = my_slot + 1; i < schedule_length; ++i)
         measurements[i].poll_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(next_action_timestamp) + TX_ANTENNA_DELAY);
   }
   else
      for (uint32_t i = my_slot + 1; i < schedule_length; ++i)
         measurements[i].final_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(next_action_timestamp) + TX_ANTENNA_DELAY);

   // Move to the next time slot operation
   ++time_slot;
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   if (time_slot >= num_slots)
   {
      current_phase = RANGE_STATUS_PHASE;
      return status_phase_begin(my_slot, schedule_length, next_action_timestamp);
   }
   return start_rx("ERROR: Unable to start listening for RANGING packets after TX\n");
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

   // Record the packet reception time in all relevant storage structures
   const div_t slot_results = div(time_slot, slots_per_range);
   register const uint32_t slot = (uint32_t)slot_results.rem, sequence_number = (uint32_t)slot_results.quot;
   if (slot < my_slot)
   {
      register const uint32_t storage_index = slot;
      measurements[slot].poll_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(next_action_timestamp) + TX_ANTENNA_DELAY);
      ranging_packet.tx_rx_times[storage_index] = ranging_radio_readrxtimestamp_lo() - reference_time;
      measurements[slot].poll_rx_times[sequence_number] = ranging_packet.tx_rx_times[storage_index];
   }
   else if (slot < schedule_length)
   {
      register const uint32_t storage_index = slot - my_slot - 1;
      measurements[slot].resp_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(next_action_timestamp) + TX_ANTENNA_DELAY);
      measurements[slot].poll_rx_times[sequence_number] = packet->tx_rx_times[my_slot];
      ranging_packet.tx_rx_times[storage_index] = ranging_radio_readrxtimestamp_lo() - reference_time;
      measurements[slot].resp_rx_times[sequence_number] = ranging_packet.tx_rx_times[storage_index];
   }
   else
   {
      register const uint32_t tx_device_slot = slot - schedule_length;
      measurements[tx_device_slot].device_eui = schedule_phase_get_addr_from_slot(tx_device_slot);
      if (my_slot > tx_device_slot)
      {
         register const uint32_t storage_index = schedule_length - my_slot + tx_device_slot - 1;
         ranging_packet.tx_rx_times[storage_index] = ranging_radio_readrxtimestamp_lo() - reference_time;
         measurements[tx_device_slot].final_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(next_action_timestamp) + TX_ANTENNA_DELAY);
         measurements[tx_device_slot].final_rx_times[sequence_number] = ranging_packet.tx_rx_times[storage_index];
         measurements[tx_device_slot].resp_rx_times[sequence_number] = packet->tx_rx_times[my_slot - tx_device_slot - 1];
      }
      else
         measurements[tx_device_slot].final_rx_times[sequence_number] = packet->tx_rx_times[schedule_length - tx_device_slot + my_slot - 1];
   }

   // Move to the next time slot operation
   ++time_slot;
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   if (time_slot >= num_slots)
   {
      current_phase = RANGE_STATUS_PHASE;
      return status_phase_begin(my_slot, schedule_length, next_action_timestamp);
   }
   return ((time_slot % schedule_length) == my_slot) ?
          start_tx("ERROR: Unable to transmit next RANGING packet after RX\n") :
          start_rx("ERROR: Unable to start listening for RANGING packets after RX\n");
}

scheduler_phase_t ranging_phase_rx_error(void)
{
   // Forward this request to the next phase if not currently in the Ranging Phase
   if (current_phase != RANGING_PHASE)
      return status_phase_rx_error();

   // Move to the next time slot operation
   ++time_slot;
   next_action_timestamp += RANGING_BROADCAST_INTERVAL_US;
   if (time_slot >= num_slots)
   {
      current_phase = RANGE_STATUS_PHASE;
      return status_phase_begin(my_slot, schedule_length, next_action_timestamp);
   }
   return ((time_slot % schedule_length) == my_slot) ?
          start_tx("ERROR: Unable to transmit next RANGING packet after error\n") :
          start_rx("ERROR: Unable to start listening for RANGING packets after error\n");
}

ranging_device_state_t* ranging_phase_get_measurements(void)
{
   return measurements;
}

uint32_t ranging_phase_get_duration(void)
{
   return ranging_phase_duration;
}

bool ranging_phase_was_scheduled(void)
{
   return (my_slot != UNSCHEDULED_SLOT);
}

bool responses_received(void)
{
   for (uint32_t i = 0; i < schedule_length; ++i)
      if (measurements[i].device_eui)
         return true;
   return false;
}
