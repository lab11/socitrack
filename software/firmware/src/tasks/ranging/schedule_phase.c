// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "ranging_phase.h"
#include "schedule_phase.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint8_t scheduled_slot, device_timeouts[MAX_NUM_RANGING_DEVICES];
static schedule_packet_t schedule_packet;
static scheduler_phase_t current_phase;
static bool is_master_scheduler;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void deschedule_device(uint8_t device_index)
{
   // Search for the specified EUI and move all subsequent devices up in the schedule
   print("INFO: Descheduling device 0x%02X due to inactivity\n", schedule_packet.schedule[device_index]);
   for (int i = device_index + 1; i < MAX_NUM_RANGING_DEVICES; ++i)
   {
      schedule_packet.schedule[i-1] = schedule_packet.schedule[i];
      device_timeouts[i-1] = device_timeouts[i];
   }
   schedule_packet.schedule[MAX_NUM_RANGING_DEVICES-1] = device_timeouts[MAX_NUM_RANGING_DEVICES-1] = 0;
   --schedule_packet.num_devices;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void schedule_phase_initialize(const uint8_t *uid, bool is_master, uint32_t epoch_timestamp)
{
   // Initialize all Schedule Phase parameters
   schedule_packet = (schedule_packet_t){ .header = { .frameCtrl = { 0x41, 0x98 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = SCHEDULE_PACKET, .epoch_time_unix = epoch_timestamp, .num_devices = 1,
      .schedule = { 0 }, .footer = { { 0 } } };
   memset(device_timeouts, 0, sizeof(device_timeouts));
   memcpy(schedule_packet.header.sourceAddr, uid, sizeof(schedule_packet.header.sourceAddr));
   schedule_packet.schedule[0] = uid[0];
   is_master_scheduler = is_master;
   scheduled_slot = 0;
}

bool schedule_phase_begin(void)
{
   // Reset the necessary Schedule Phase parameters
   schedule_packet.header.seqNum = 0;
   current_phase = SCHEDULE_PHASE;

   // Set up the correct antenna for schedule transmission
   ranging_radio_choose_antenna(SCHEDULE_XMIT_ANTENNA);

   // Begin transmission or reception depending on the current role
   if (is_master_scheduler)
   {
      // Increment the epoch timestamp and increment all device timeouts
      ++schedule_packet.epoch_time_unix;
      for (uint8_t i = 1; i < schedule_packet.num_devices; ++i)
         ++device_timeouts[i];

      // Schedule packet transmission
      const uint16_t packet_size = sizeof(schedule_packet_t) - MAX_NUM_RANGING_DEVICES + schedule_packet.num_devices;
      dwt_writetxfctrl(packet_size, 0, 0);
      if ((dwt_writetxdata(packet_size, (uint8_t*)&schedule_packet, 0) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_IMMEDIATE) != DWT_SUCCESS))
      {
         print("ERROR: Failed to transmit schedule with length %u\n", (uint32_t)packet_size);
         return false;
      }
   }
   else
   {
      // Set up packet reception with a timeout
      dwt_setrxtimeout(DW_TIMEOUT_FROM_US(1000000.0));
      if (!ranging_radio_rxenable(DWT_START_RX_IMMEDIATE))
      {
         print("ERROR: Unable to start listening for schedule packets\n");
         return false;
      }
   }
   return true;
}

scheduler_phase_t schedule_phase_tx_complete(void)
{
   // Forward this request to the next phase if not currently in the Schedule Phase
   if (current_phase != SCHEDULE_PHASE)
      return ranging_phase_tx_complete();

   // Retransmit the schedule up to the specified number of times
   if ((++schedule_packet.header.seqNum < SCHEDULE_NUM_MASTER_BROADCASTS) && is_master_scheduler)
   {
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US(SCHEDULE_RESEND_INTERVAL_US));
      if ((dwt_writetxdata(sizeof(schedule_packet.header.seqNum), &schedule_packet.header.seqNum, offsetof(ieee154_header_t, seqNum)) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_TS) != DWT_SUCCESS))
      {
         print("ERROR: Failed to retransmit schedule\n");
         return RANGING_ERROR;
      }
      return SCHEDULE_PHASE;
   }

   // Move to the Ranging Phase of the ranging protocol
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(scheduled_slot, schedule_packet.num_devices, ((uint32_t)(SCHEDULE_NUM_TOTAL_BROADCASTS - schedule_packet.header.seqNum + 1)) * SCHEDULE_RESEND_INTERVAL_US, true);
}

scheduler_phase_t schedule_phase_rx_complete(schedule_packet_t* schedule)
{
   // Forward this request to the next phase if not currently in the Schedule Phase
   if (current_phase != SCHEDULE_PHASE)
   {
      // Determine if a colliding network is present
      bool device_found = false;
      for (uint8_t i = 0; i < schedule_packet.num_devices; ++i)
         if (schedule_packet.schedule[i] == schedule->header.sourceAddr[0])
         {
            device_found = true;
            break;
         }
      return device_found ? ranging_phase_rx_complete((ranging_packet_t*)schedule) : MESSAGE_COLLISION;
   }
   else if (schedule->message_type != SCHEDULE_PACKET)
   {
      // Immediately restart listening for schedule packets
      if (!ranging_radio_rxenable(DWT_START_RX_IMMEDIATE))
      {
         print("ERROR: Unable to restart listening for schedule packets\n");
         return RANGING_ERROR;
      }
      return SCHEDULE_PHASE;
   }

   // Ensure that the received schedule length is valid
   if (schedule->num_devices > MAX_NUM_RANGING_DEVICES)
   {
      print("ERROR: Received a schedule with too many devices included\n");
      return RANGING_ERROR;
   }

   // Unpack the received schedule
   scheduled_slot = 0;
   schedule_packet.epoch_time_unix = schedule->epoch_time_unix;
   schedule_packet.num_devices = schedule->num_devices;
   for (uint8_t i = 0; i < schedule->num_devices; ++i)
   {
      schedule_packet.schedule[i] = schedule->schedule[i];
      if (schedule->schedule[i] == schedule_packet.header.sourceAddr[0])
         scheduled_slot = i;
   }
   for (uint8_t i = schedule->num_devices; i < MAX_NUM_RANGING_DEVICES; ++i)
      schedule_packet.schedule[i] = 0;

   // Ensure that the schedule included a slot for this device
   if (!scheduled_slot)
      return RANGING_ERROR;

   // Retransmit the schedule at the specified time slot
   schedule_packet.header.seqNum = scheduled_slot + SCHEDULE_NUM_MASTER_BROADCASTS - 1;
   if ((schedule->header.seqNum < schedule_packet.header.seqNum) && (schedule_packet.header.seqNum < SCHEDULE_NUM_TOTAL_BROADCASTS))
   {
      const uint16_t packet_size = sizeof(schedule_packet_t) - MAX_NUM_RANGING_DEVICES + schedule_packet.num_devices;
      dwt_writetxfctrl(packet_size, 0, 0);
      dwt_setdelayedtrxtime(DW_DELAY_FROM_US((uint32_t)(schedule_packet.header.seqNum - schedule->header.seqNum) * SCHEDULE_RESEND_INTERVAL_US));
      if ((dwt_writetxdata(packet_size, (uint8_t*)&schedule_packet, 0) != DWT_SUCCESS) || (dwt_starttx(DWT_START_TX_DLY_RS) != DWT_SUCCESS))
      {
         print("ERROR: Failed to retransmit received schedule\n");
         return RANGING_ERROR;
      }
      return SCHEDULE_PHASE;
   }

   // Move to the Ranging Phase of the ranging protocol
   current_phase = RANGING_PHASE;
   return ranging_phase_begin(scheduled_slot, schedule_packet.num_devices, ((uint32_t)(SCHEDULE_NUM_TOTAL_BROADCASTS - schedule->header.seqNum)) * SCHEDULE_RESEND_INTERVAL_US, false);
}

scheduler_phase_t schedule_phase_rx_error(void)
{
   // Forward this request to the next phase if not currently in the Schedule Phase
   if (current_phase != SCHEDULE_PHASE)
      return ranging_phase_rx_error();
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
   // Deschedule any devices that have been absent for a long time
   for (uint8_t i = 1; i < schedule_packet.num_devices; ++i)
      if (device_timeouts[i] > DEVICE_TIMEOUT_SECONDS)
         deschedule_device(i--);
}
