// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "computation_phase.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "status_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t current_phase;
static ranging_device_state_t measurements[MAX_NUM_RANGING_DEVICES];
static uint32_t time_slot, my_slot, num_slots, slots_per_range, schedule_length;
static uint32_t reference_time, round_rx_senders;
static uint8_t current_antenna, round_rx_ok[MAX_NUM_RANGING_DEVICES][NUM_XMIT_ANTENNAS], round_rx_failed[MAX_NUM_RANGING_DEVICES][NUM_XMIT_ANTENNAS];
static bool round_rx_pending;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline void note_rx_outcome(uint32_t slot, bool decoded)
{
   // The first schedule_length slots of each attempt carry polls and responses, the rest carry finals, in the same order
   const uint32_t sender = (slot < schedule_length) ? slot : (slot - schedule_length);
   if ((sender >= MAX_NUM_RANGING_DEVICES) || (current_antenna >= NUM_XMIT_ANTENNAS))
      return;
   uint8_t *count = decoded ? &round_rx_ok[sender][current_antenna] : &round_rx_failed[sender][current_antenna];
   if (*count < UINT8_MAX)
      ++*count;
}

static inline uint32_t ranging_slot_time(uint32_t slot_index)
{
   return RANGING_PHASE_START_US + (slot_index * RANGING_BROADCAST_INTERVAL_US);
}

static inline scheduler_phase_t start_tx(const char *error_message)
{
   // Handle antenna switching functionality
   if ((time_slot % slots_per_range) == 0)
   {
      current_antenna = (current_antenna + 1) % NUM_XMIT_ANTENNAS;
      ranging_radio_choose_antenna(current_antenna);
   }

   // Perform the actual radio transmit
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(ranging_slot_time(time_slot)));
   if (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS)
   {
      ranging_radio_note_tx_failure();
      print_isr(error_message);
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
   dwt_setdelayedtrxtime(DW_DELAY_FROM_US(ranging_slot_time(time_slot) - RECEIVE_EARLY_START_US));
   if (dwt_rxenable(DWT_START_RX_DLY_REF | DWT_IDLE_ON_DLY_ERR) != DWT_SUCCESS)
   {
      ranging_radio_note_rx_arm_failure();
      scheduler_note_rx_arm_failure(RANGING_PHASE, time_slot, schedule_length, ranging_slot_time(time_slot) - RECEIVE_EARLY_START_US);
      print_isr(error_message);
      return RADIO_ERROR;
   }
   scheduler_note_rx_armed(ranging_slot_time(time_slot) - RECEIVE_EARLY_START_US);
   return RANGING_PHASE;
}


// Public Functions ----------------------------------------------------------------------------------------------------

void ranging_phase_initialize(const uint8_t *uid) {}

scheduler_phase_t ranging_phase_begin(uint8_t scheduled_slot, uint8_t schedule_size, uint32_t ref_time)
{
   // Ensure there are at least two devices to begin ranging
   my_slot = scheduled_slot;
   if (schedule_size > MAX_NUM_RANGING_DEVICES)
      schedule_size = MAX_NUM_RANGING_DEVICES;
   reset_computation_phase(schedule_size);
   memset(&measurements, 0, sizeof(measurements));
   memset(round_rx_ok, 0, sizeof(round_rx_ok));
   memset(round_rx_failed, 0, sizeof(round_rx_failed));
   round_rx_pending = false;
   slots_per_range = (uint32_t)schedule_size * (RANGING_NUM_PACKETS_PER_DEVICE - 1);
   num_slots = slots_per_range * RANGING_NUM_RANGE_ATTEMPTS;
   if ((schedule_size < 2) || (my_slot == UNSCHEDULED_SLOT))
   {
      status_phase_reset();
      return RANGE_COMPUTATION_PHASE;
   }

   // Reset the necessary Ranging Phase parameters
   current_phase = RANGING_PHASE;
   schedule_length = schedule_size;
   static const ieee154_header_t ranging_packet_header = { .msgType = RANGING_PACKET };
   dwt_writetxdata(sizeof(ieee154_header_t), (uint8_t*)&ranging_packet_header, 0);
   current_antenna = NUM_XMIT_ANTENNAS - 1;
   reference_time = ref_time;
   time_slot = 0;
   round_rx_senders = schedule_size;
   round_rx_pending = true;

   // Set up the correct RX timeout durations
   dwt_setpreambledetecttimeout(DW_PREAMBLE_TIMEOUT);
   dwt_setrxtimeout(DW_TIMEOUT_FROM_US(RANGING_TIMEOUT_US));
   dwt_writetxfctrl(sizeof(ieee154_header_t) + sizeof(ieee154_footer_t) + (uint16_t)((schedule_length - 1) * sizeof(((ranging_packet_t*)0)->tx_rx_times[0])), 0, 1);

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
         measurements[i].resp_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(ranging_slot_time(time_slot)) + TX_ANTENNA_DELAY);
      for (uint32_t i = my_slot + 1; i < schedule_length; ++i)
         measurements[i].poll_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(ranging_slot_time(time_slot)) + TX_ANTENNA_DELAY);
   }
   else
      for (uint32_t i = my_slot + 1; i < schedule_length; ++i)
         measurements[i].final_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(ranging_slot_time(time_slot)) + TX_ANTENNA_DELAY);

   // Move to the next time slot operation
   ++time_slot;
   if (time_slot >= num_slots)
   {
      current_phase = RANGE_STATUS_PHASE;
      return status_phase_begin(my_slot, schedule_length, ranging_slot_time(time_slot));
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
      print_isr("ERROR: Received an unexpected message type during RANGING phase...possible network collision\n");
      return MESSAGE_COLLISION;
   }

   // Record the packet reception time in all relevant storage structures
   const div_t slot_results = div(time_slot, slots_per_range);
   register const uint32_t slot = (uint32_t)slot_results.rem, sequence_number = (uint32_t)slot_results.quot;
   note_rx_outcome(slot, true);
   if (slot < my_slot)
   {
      const uint32_t rx_time = ranging_radio_readrxtimestamp_lo() - reference_time;
      measurements[slot].poll_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(ranging_slot_time(time_slot)) + TX_ANTENNA_DELAY);
      measurements[slot].poll_rx_times[sequence_number] = rx_time;
      dwt_writetxdata(sizeof(rx_time), (uint8_t*)&rx_time, offsetof(ranging_packet_t, tx_rx_times[slot]));
   }
   else if (slot < schedule_length)
   {
      const uint32_t rx_time = ranging_radio_readrxtimestamp_lo() - reference_time;
      measurements[slot].resp_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(ranging_slot_time(time_slot)) + TX_ANTENNA_DELAY);
      measurements[slot].poll_rx_times[sequence_number] = packet->tx_rx_times[my_slot];
      measurements[slot].resp_rx_times[sequence_number] = rx_time;
      dwt_writetxdata(sizeof(rx_time), (uint8_t*)&rx_time, offsetof(ranging_packet_t, tx_rx_times[slot - my_slot - 1]));
   }
   else
   {
      register const uint32_t tx_device_slot = slot - schedule_length;
      measurements[tx_device_slot].device_eui = schedule_phase_get_addr_from_slot(tx_device_slot);
      if (my_slot > tx_device_slot)
      {
         const uint32_t rx_time = ranging_radio_readrxtimestamp_lo() - reference_time;
         measurements[tx_device_slot].final_tx_times[sequence_number] = (uint32_t)(US_TO_DWT(ranging_slot_time(time_slot)) + TX_ANTENNA_DELAY);
         measurements[tx_device_slot].final_rx_times[sequence_number] = rx_time;
         measurements[tx_device_slot].resp_rx_times[sequence_number] = packet->tx_rx_times[my_slot - tx_device_slot - 1];
         dwt_writetxdata(sizeof(rx_time), (uint8_t*)&rx_time, offsetof(ranging_packet_t, tx_rx_times[schedule_length - my_slot + tx_device_slot - 1]));
      }
      else
         measurements[tx_device_slot].final_rx_times[sequence_number] = packet->tx_rx_times[schedule_length - tx_device_slot + my_slot - 1];
   }

   // Move to the next time slot operation
   ++time_slot;
   if (time_slot >= num_slots)
   {
      current_phase = RANGE_STATUS_PHASE;
      return status_phase_begin(my_slot, schedule_length, ranging_slot_time(time_slot));
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

   // Record an invalid packet reception time in all relevant storage structures
   const uint32_t to_send = RANGING_INVALID_TIMESTAMP;
   const div_t slot_results = div(time_slot, slots_per_range);
   register const uint32_t slot = (uint32_t)slot_results.rem, sequence_number = (uint32_t)slot_results.quot;
   note_rx_outcome(slot, false);
   if (slot < my_slot)
   {
      measurements[slot].poll_tx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
      measurements[slot].poll_rx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
      dwt_writetxdata(sizeof(to_send), (uint8_t*)&to_send, offsetof(ranging_packet_t, tx_rx_times[slot]));
   }
   else if (slot < schedule_length)
   {
      measurements[slot].resp_tx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
      measurements[slot].poll_rx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
      measurements[slot].resp_rx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
      dwt_writetxdata(sizeof(to_send), (uint8_t*)&to_send, offsetof(ranging_packet_t, tx_rx_times[slot - my_slot - 1]));
   }
   else
   {
      register const uint32_t tx_device_slot = slot - schedule_length;
      measurements[tx_device_slot].final_rx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
      if (my_slot > tx_device_slot)
      {
         measurements[tx_device_slot].final_tx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
         measurements[tx_device_slot].resp_rx_times[sequence_number] = RANGING_INVALID_TIMESTAMP;
         dwt_writetxdata(sizeof(to_send), (uint8_t*)&to_send, offsetof(ranging_packet_t, tx_rx_times[schedule_length - my_slot + tx_device_slot - 1]));
      }
   }

   // Move to the next time slot operation
   ++time_slot;
   if (time_slot >= num_slots)
   {
      current_phase = RANGE_STATUS_PHASE;
      return status_phase_begin(my_slot, schedule_length, ranging_slot_time(time_slot));
   }
   return ((time_slot % schedule_length) == my_slot) ?
          start_tx("ERROR: Unable to transmit next RANGING packet after error\n") :
          start_rx("ERROR: Unable to start listening for RANGING packets after error\n");
}

ranging_device_state_t* ranging_phase_get_measurements(void)
{
   return measurements;
}

bool ranging_phase_was_scheduled(void)
{
   return (my_slot != UNSCHEDULED_SLOT);
}

void ranging_phase_commit_receive_counts(uint16_t present_slots)
{
   // Count this round's receives towards the radio's statistics, leaving out every sender nobody heard all round
   if (!round_rx_pending)
      return;
   round_rx_pending = false;
   for (uint32_t sender = 0; (sender < round_rx_senders) && (sender < MAX_NUM_RANGING_DEVICES); ++sender)
   {
      bool present = ((present_slots >> sender) & 1u) != 0;
      for (uint32_t antenna = 0; antenna < NUM_XMIT_ANTENNAS; ++antenna)
         present = present || (round_rx_ok[sender][antenna] != 0);
      for (uint32_t antenna = 0; antenna < NUM_XMIT_ANTENNAS; ++antenna)
         if (round_rx_ok[sender][antenna] || (present && round_rx_failed[sender][antenna]))
            ranging_radio_note_rx_results((uint8_t)antenna, round_rx_ok[sender][antenna], present ? round_rx_failed[sender][antenna] : 0);
   }
}

uint16_t ranging_phase_get_heard_slots(void)
{
   // One bit per schedule slot this device actually completed a ranging exchange with
   uint16_t heard = 0;
   for (uint32_t i = 0; i < MAX_NUM_RANGING_DEVICES; ++i)
      if (measurements[i].device_eui)
         heard |= (uint16_t)(1u << i);
   return heard;
}
