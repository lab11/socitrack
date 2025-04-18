// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "schedule_phase.h"
#include "subscription_phase.h"
#include "computation_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint8_t device_timeouts[MAX_NUM_RANGING_DEVICES], valid_devices[MAX_NUM_RANGING_DEVICES];
static uint32_t reference_time, next_action_timestamp;
static uint8_t scheduled_slot, num_valid_devices;
static schedule_packet_t schedule_packet;
static scheduler_phase_t current_phase;
static bool is_master_scheduler;


// Private Helper Functions --------------------------------------------------------------------------------------------

static bool is_valid_device(uint8_t device_uid)
{
#if !defined(_TEST_NO_EXP_DETAILS)
   // Search through the list of active deployment devices for a match
   for (int i = 0; i < num_valid_devices; ++i)
      if (valid_devices[i] == device_uid)
         return true;
   return false;
#else
   return true;
#endif
}

static void deschedule_device(uint8_t device_index)
{
   // Search for the specified EUI and move all subsequent devices up in the schedule
   print("INFO: De-scheduling device 0x%02X due to inactivity\n", schedule_packet.schedule[device_index]);
   for (int i = device_index + 1; i < MAX_NUM_RANGING_DEVICES; ++i)
   {
      schedule_packet.schedule[i-1] = schedule_packet.schedule[i];
      device_timeouts[i-1] = device_timeouts[i];
   }
   schedule_packet.schedule[MAX_NUM_RANGING_DEVICES-1] = device_timeouts[MAX_NUM_RANGING_DEVICES-1] = 0;
   --schedule_packet.num_devices;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void schedule_phase_initialize(const uint8_t *uid, bool is_master)
{
   // Initialize all Schedule Phase parameters
   schedule_packet = (schedule_packet_t){ .header = { .msgType = SCHEDULE_PACKET },
      .src_addr = uid[0], .sequence_number = 0, .epoch_time_unix = 0, .num_devices = 1,
      .schedule = { 0 }, .footer = { { 0 } } };
   memset(device_timeouts, 0, sizeof(device_timeouts));
   schedule_packet.schedule[0] = uid[0];
   is_master_scheduler = is_master;
   scheduled_slot = 0;
}

void schedule_phase_store_experiment_details(experiment_details_t *details)
{
   // Store a list of all valid devices in the current experiment
   num_valid_devices = details->num_devices;
   for (uint8_t i = 0; i < details->num_devices; ++i)
      valid_devices[i] = details->uids[i][0];
   computation_phase_configure_filters(details);
}

scheduler_phase_t schedule_phase_begin(void)
{
   // Reset the necessary Schedule Phase parameters
   schedule_packet.sequence_number = 0;
   current_phase = SCHEDULE_PHASE;
   next_action_timestamp = 0;

   // Set up the correct antenna for schedule transmission
   ranging_radio_choose_antenna(0);

   // Begin transmission or reception depending on the current role
   if (is_master_scheduler)
   {
      // Increment the epoch timestamp and increment all device timeouts
      schedule_packet.epoch_time_unix = app_get_experiment_time(0);
      for (uint8_t i = 1; i < schedule_packet.num_devices; ++i)
         ++device_timeouts[i];

      // Schedule packet transmission
      const uint16_t packet_size = sizeof(schedule_packet_t) - MAX_NUM_RANGING_DEVICES + schedule_packet.num_devices;
      dwt_writetxfctrl(packet_size, 0, 0);
      if ((dwt_writetxdata(packet_size - sizeof(ieee154_footer_t), (uint8_t*)&schedule_packet, 0) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS))
      {
         print("ERROR: Failed to transmit schedule with length %u\n", (uint32_t)packet_size);
         return RADIO_ERROR;
      }
   }
   else
   {
      // Set up packet reception with a timeout
      dwt_setpreambledetecttimeout(0);
      dwt_setrxtimeout(DW_TIMEOUT_FROM_US(1000000.0));
      if (!ranging_radio_rxenable(DWT_START_RX_IMMEDIATE))
      {
         print("ERROR: Unable to start listening for schedule packets\n");
         return RADIO_ERROR;
      }
   }
   return SCHEDULE_PHASE;
}

scheduler_phase_t schedule_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Schedule Phase
   if (current_phase != SCHEDULE_PHASE)
      return subscription_phase_tx_complete();

   // Retransmit the schedule up to the specified number of times
   next_action_timestamp += SCHEDULE_RESEND_INTERVAL_US;
   while ((++schedule_packet.sequence_number < SCHEDULE_NUM_MASTER_BROADCASTS) && is_master_scheduler)
   {
      ranging_radio_choose_antenna(schedule_packet.sequence_number % NUM_XMIT_ANTENNAS);
      if (schedule_packet.sequence_number == 1)
      {
         uint64_t ref_time = (ranging_radio_readtxtimestamp() - TX_ANTENNA_DELAY) & 0xFFFFFFFE00;
         dwt_setreferencetrxtime((uint32_t)(ref_time >> 8));
         reference_time = (uint32_t)ref_time;
      }
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp));
      if ((dwt_writetxdata(sizeof(schedule_packet.sequence_number), &schedule_packet.sequence_number, offsetof(schedule_packet_t, sequence_number)) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
      {
         next_action_timestamp += SCHEDULE_RESEND_INTERVAL_US;
         print("ERROR: Failed to retransmit schedule\n");
      }
      else
         return SCHEDULE_PHASE;
   }

   // Move to the Subscription Phase of the ranging protocol
   current_phase = SUBSCRIPTION_PHASE;
   next_action_timestamp += ((uint32_t)(SCHEDULE_NUM_TOTAL_BROADCASTS - schedule_packet.sequence_number)) * SCHEDULE_RESEND_INTERVAL_US;
   return subscription_phase_begin(scheduled_slot, schedule_packet.num_devices, reference_time, next_action_timestamp);
}

scheduler_phase_t schedule_phase_rx_complete(schedule_packet_t* schedule)
{
   // Forward this request to the next phase if not currently in the Schedule Phase
   if (current_phase != SCHEDULE_PHASE)
      return subscription_phase_rx_complete((subscription_packet_t*)schedule);
   else if ((schedule->header.msgType != SCHEDULE_PACKET) || !is_valid_device(schedule->src_addr))
   {
      // Immediately restart listening for schedule packets
      if (!ranging_radio_rxenable(DWT_START_RX_IMMEDIATE))
      {
         print("ERROR: Unable to restart listening for schedule packets\n");
         return RADIO_ERROR;
      }
      return SCHEDULE_PHASE;
   }

   // Unpack the received schedule
   scheduled_slot = UNSCHEDULED_SLOT;
   schedule_packet.epoch_time_unix = schedule->epoch_time_unix;
   schedule_packet.num_devices = schedule->num_devices;
   for (uint8_t i = 0; i < schedule->num_devices; ++i)
   {
      schedule_packet.schedule[i] = schedule->schedule[i];
      if (schedule->schedule[i] == schedule_packet.src_addr)
         scheduled_slot = i;
   }
   for (uint8_t i = schedule->num_devices; i < MAX_NUM_RANGING_DEVICES; ++i)
      schedule_packet.schedule[i] = 0;

   // Set up the reference timestamp for scheduling future messages
   uint64_t ref_time = (ranging_radio_readrxtimestamp() + RX_ANTENNA_DELAY - US_TO_DWT((uint32_t)schedule->sequence_number * SCHEDULE_RESEND_INTERVAL_US)) & 0xFFFFFFFE00;
   dwt_setreferencetrxtime((uint32_t)(ref_time >> 8));
   reference_time = (uint32_t)ref_time;

   // Retransmit the schedule at the specified time slot
   schedule_packet.sequence_number = scheduled_slot + SCHEDULE_NUM_MASTER_BROADCASTS - 1;
   if ((scheduled_slot != UNSCHEDULED_SLOT) && (schedule->sequence_number < schedule_packet.sequence_number) && (schedule_packet.sequence_number < SCHEDULE_NUM_TOTAL_BROADCASTS))
   {
      const uint16_t packet_size = sizeof(schedule_packet_t) - MAX_NUM_RANGING_DEVICES + schedule_packet.num_devices;
      next_action_timestamp += (uint32_t)(schedule_packet.sequence_number - schedule->sequence_number) * SCHEDULE_RESEND_INTERVAL_US;
      dwt_writetxfctrl(packet_size, 0, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(next_action_timestamp));
      if ((dwt_writetxdata(packet_size - sizeof(ieee154_footer_t), (uint8_t*)&schedule_packet, 0) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_REF) != DWT_SUCCESS))
      {
         current_phase = SUBSCRIPTION_PHASE;
         print("ERROR: Failed to retransmit received schedule\n");
         next_action_timestamp += ((uint32_t)(SCHEDULE_NUM_TOTAL_BROADCASTS - schedule_packet.sequence_number)) * SCHEDULE_RESEND_INTERVAL_US;
         return subscription_phase_begin(scheduled_slot, schedule_packet.num_devices, reference_time, next_action_timestamp);
      }
      return SCHEDULE_PHASE;
   }

   // Move to the Subscription Phase of the ranging protocol
   current_phase = SUBSCRIPTION_PHASE;
   next_action_timestamp += ((uint32_t)(SCHEDULE_NUM_TOTAL_BROADCASTS - schedule->sequence_number)) * SCHEDULE_RESEND_INTERVAL_US;
   return subscription_phase_begin(scheduled_slot, schedule_packet.num_devices, reference_time, next_action_timestamp);
}

scheduler_phase_t schedule_phase_rx_error(void)
{
   // Forward this request to the next phase if not currently in the Schedule Phase
   if (current_phase != SCHEDULE_PHASE)
      return subscription_phase_rx_error();
   return RANGING_ERROR;
}

uint32_t schedule_phase_get_num_devices(void)
{
   // Return the current number of scheduled devices
   return schedule_packet.num_devices;
}

uint32_t schedule_phase_get_timestamp(void)
{
   // Return the current epoch timestamp from the schedule
   return schedule_packet.epoch_time_unix;
}

void schedule_phase_add_device(uint8_t eui)
{
   // Search for the first empty schedule slot
   for (int i = 1; i < MAX_NUM_RANGING_DEVICES; ++i)
   {
      // Ensure that the device has not already been scheduled
      if (schedule_packet.schedule[i] == eui)
      {
         device_timeouts[i] = 0;
         break;
      }
      else if (schedule_packet.schedule[i] == 0)
      {
         device_timeouts[i] = 0;
         schedule_packet.schedule[i] = eui;
         ++schedule_packet.num_devices;
         break;
      }
   }
}

uint8_t schedule_phase_get_addr_from_slot(uint8_t slot)
{
   return schedule_packet.schedule[slot];
}

void schedule_phase_update_device_presence(uint8_t eui)
{
   // Reset the device timeout for the corresponding EUI
   for (uint8_t i = 1; i < schedule_packet.num_devices; ++i)
      if (schedule_packet.schedule[i] == eui)
      {
         device_timeouts[i] = 0;
         break;
      }
}

void schedule_phase_handle_device_timeouts(void)
{
   // De-schedule any devices that have been absent for a long time
   for (uint8_t i = 1; i < schedule_packet.num_devices; ++i)
      if (device_timeouts[i] > DEVICE_TIMEOUT_SECONDS)
         deschedule_device(i--);
      else if (device_timeouts[i] > 3)
         computation_phase_reset_range_filter(schedule_packet.schedule[i]);
}
