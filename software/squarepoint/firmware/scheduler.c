#include <stdlib.h>
#include <string.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "glossy.h"
#include "host_interface.h"
#include "led.h"
#include "prng.h"
#include "ranging.h"
#include "request.h"
#include "response.h"
#include "scheduler.h"
#include "SEGGER_RTT.h"
#include "squarepoint.h"
#include "stm32f0xx_gpio.h"
#include "timer.h"

// Application state ---------------------------------------------------------------------------------------------------

static app_config_t _config;
static ranctx _prng_state;
static stm_timer_t *_scheduler_timer = NULL, *_ranging_timer = NULL;
static scheduler_phase_t _scheduler_phase = UNSCHEDULED_TIME_PHASE;
static schedule_packet_t _schedule_packet;
static control_packet_t _control_packet;
static results_packet_t _results_packet;
static uint32_t _slot_start_timestamp, _network_search_time, _network_search_time_end, _num_ranging_packets_received;
static uint8_t _num_successful_ranges, _num_expected_valid_ranges, _next_slot_number, _empty_round_timeout;
static uint8_t _missing_schedule_timeout, _network_search_timeout, _request_phase_start_slot, _response_phase_start_slot;
static uint8_t _shared_bus_phase_slot_start, _schedule_xmit_phase_slot_start_next, _schedule_xmit_phase_slot_start;
static uint8_t _ranging_phase_slot_start, _results_phase_slot_start, _error_correction_phase_slot_start, _failure_count;
static PROTOCOL_EUI_TYPE _expected_valid_ranges[PROTOCOL_MAX_NUM_RESPONDERS + PROTOCOL_MAX_NUM_HYBRIDS];
static bool _is_running = FALSE, _is_configured = FALSE;
static bool _ranging_successful, _previous_ranging_successful, _results_received, _network_synchronized;

// Schedule storage ----------------------------------------------------------------------------------------------------

static bool _received_schedule, _received_backup_schedule;
static PROTOCOL_EUI_TYPE _request_schedule[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static PROTOCOL_EUI_TYPE _response_schedule[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static PROTOCOL_EUI_TYPE _hybrid_schedule[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static PROTOCOL_EUI_TYPE _request_order[PROTOCOL_MAX_NUM_REQUESTERS + PROTOCOL_MAX_NUM_HYBRIDS];
static PROTOCOL_EUI_TYPE _response_order[PROTOCOL_MAX_NUM_HYBRIDS + PROTOCOL_MAX_NUM_RESPONDERS];
static uint8_t _requester_device_timeouts[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static uint8_t _responder_device_timeouts[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static uint8_t _hybrid_device_timeouts[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static uint8_t _num_requesters_scheduled, _num_responders_scheduled, _num_hybrids_scheduled, _num_results_scheduled;
static int16_t _scheduled_request_timeslot, _scheduled_response_timeslot, _scheduled_result_timeslot;

// Scratch variables ---------------------------------------------------------------------------------------------------

static uint8_t ids_and_ranges[((PROTOCOL_MAX_NUM_RESPONDERS + PROTOCOL_MAX_NUM_HYBRIDS) * PACKET_SINGLE_RESULT_LENGTH) + 1 + sizeof(_schedule_packet.epoch_time_unix)];
static uint8_t scratch_ranges[((PROTOCOL_MAX_NUM_RESPONDERS + PROTOCOL_MAX_NUM_HYBRIDS) * PACKET_SINGLE_RESULT_LENGTH) + 1];
PROTOCOL_EUI_TYPE requester_eui_array[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
PROTOCOL_EUI_TYPE responder_eui_array[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
PROTOCOL_EUI_TYPE hybrid_eui_array[PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
static uint8_t read_buffer[MSG_MAX_PACKET_LENGTH];

// Private helper functions --------------------------------------------------------------------------------------------

static void tx_callback(const dwt_cb_data_t *txData);
static void rx_callback(const dwt_cb_data_t *rxData);

static void update_led_statuses(void)
{
   // Signal normal round by turning on GREEN if synchronized with other nodes or ORANGE if not synchronized
   led_on(_previous_ranging_successful ? GREEN : ORANGE);
}

static bool reset_dw1000_configuration(void)
{
   // Set all DW1000 settings and turn the radio OFF
   dw1000_spi_slow();
   dwt_setcallbacks(tx_callback, rx_callback, rx_callback, rx_callback);
   if (!dw1000_radio_disable())
      return FALSE;

   // Set this device so that it only receives data and acknowledgment packets
   dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

   // Disable the DW1000 double receive buffer mode, receive timeouts, and set the SPI to fast
   dwt_setdblrxbuffmode(FALSE);
   dwt_setrxtimeout(FALSE);
   dw1000_spi_fast();
   return TRUE;
}

static void print_schedule(void)
{
   debug_msg("REQUESTERS: ");
   for (uint8_t i = 0; i < (_num_requesters_scheduled + _num_hybrids_scheduled); ++i)
   {
      debug_msg_eui(_request_order[i]);
      debug_msg(" ");
   }
   debug_msg("; RESPONDERS: ");
   for (uint8_t i = 0; i < (_num_hybrids_scheduled + _num_responders_scheduled); ++i)
   {
      debug_msg_eui(_response_order[i]);
      debug_msg(" ");
   }
   debug_msg("; SCHEDULER = ");
   debug_msg_eui(_schedule_packet.scheduler_eui);
   debug_msg(" ; BACKUP SCHEDULER = ");
   debug_msg_eui(_schedule_packet.backup_scheduler_eui);
   debug_msg("\n");
}

static void initialize_new_round(void)
{
   // Reset all sub-states and phases
   reset_watchdog();
   glossy_reset_state();
   reset_request_phase();
   reset_response_phase();
   reset_ranging_phase();

   // Reset all global variables to begin a new ranging round
   memset(ids_and_ranges, 0, sizeof(ids_and_ranges));
   memset(scratch_ranges, 0, sizeof(scratch_ranges));
   _num_ranging_packets_received = _num_successful_ranges = _next_slot_number = 0;
   _previous_ranging_successful = _ranging_successful;
   _ranging_successful = _results_received = FALSE;
   _schedule_xmit_phase_slot_start = _schedule_xmit_phase_slot_start_next;
   _ranging_phase_slot_start = _schedule_xmit_phase_slot_start_next + 1;
   _results_phase_slot_start = _ranging_phase_slot_start + 1;
   _error_correction_phase_slot_start = _results_phase_slot_start + _num_results_scheduled;

   // Increment the epoch timestamp
   if (_schedule_packet.epoch_time_unix)
      ++_schedule_packet.epoch_time_unix;
}

static bool switch_backup_scheduler(PROTOCOL_EUI_TYPE current_backup_eui)
{
   // Assign the backup scheduler to be a device which is currently responsive
   for (uint8_t i = 0; i < PROTOCOL_MAX_NUM_DEVICES_PER_TYPE; ++i)
      if (_request_schedule[i] && (_request_schedule[i] != current_backup_eui) && (_request_schedule[i] != _schedule_packet.scheduler_eui) && !(_requester_device_timeouts[i]))
      {
         _schedule_packet.backup_scheduler_eui = _request_schedule[i];
         break;
      }
      else if (_response_schedule[i] && (_response_schedule[i] != current_backup_eui) && (_response_schedule[i] != _schedule_packet.scheduler_eui) && !(_responder_device_timeouts[i]))
      {
         _schedule_packet.backup_scheduler_eui = _response_schedule[i];
         break;
      }
      else if (_hybrid_schedule[i] && (_hybrid_schedule[i] != current_backup_eui) && (_hybrid_schedule[i] != _schedule_packet.scheduler_eui) && !(_hybrid_device_timeouts[i]))
      {
         _schedule_packet.backup_scheduler_eui = _hybrid_schedule[i];
         break;
      }

   // Return whether the backup scheduler was successfully switched
   return (current_backup_eui != _schedule_packet.backup_scheduler_eui);
}

static void compress_schedule(PROTOCOL_EUI_TYPE *array, uint8_t *timeouts, uint8_t array_length)
{
   // Ensure that scheduled devices are placed at the beginning of the schedule buffers
   uint8_t i_full, i_empty = array_length - 1;
   for (i_full = 0; i_full < i_empty; ++i_full)
   {
      // Find first non-full element from the back
      for (; i_full < i_empty; --i_empty)
         if (array[i_empty])
            break;

      if (i_empty == i_full)
         return;

      // Move the back element to the first empty slot
      if (!array[i_full])
      {
         array[i_full] = array[i_empty];
         array[i_empty] = 0;
         timeouts[i_full] = timeouts[i_empty];
         timeouts[i_empty] = 0;
      }
   }
}

static void schedule_device(PROTOCOL_EUI_TYPE *schedule_array, uint8_t *timeout_array, uint8_t *num_scheduled, PROTOCOL_EUI_TYPE eui)
{
   // Search for the first empty schedule slot, ensuring that the device has not already been scheduled
   uint8_t candidate_slot = 0xFF;
   for (uint8_t i = 0; i < PROTOCOL_MAX_NUM_DEVICES_PER_TYPE; ++i)
   {
      // Check if the device has already been scheduled
      if (schedule_array[i] == eui)
      {
         debug_msg("WARNING: Node has already been scheduled\n");
         return;
      }

      // Search for the next empty slot if an empty slot has not already been found
      if ((candidate_slot == 0xFF) && (schedule_array[i] == 0))
         candidate_slot = i;
   }

   // Schedule the device if an available slot was found
   if (candidate_slot < 0xFF)
   {
      // Set the device to be the backup scheduler if none exists
      if (_schedule_packet.scheduler_eui == _schedule_packet.backup_scheduler_eui)
         _schedule_packet.backup_scheduler_eui = eui;
      schedule_array[candidate_slot] = eui;
      timeout_array[candidate_slot] = 0;
      ++(*num_scheduled);
      return;
   }

   // Inform that the device was unable to be scheduled
   debug_msg("WARNING: Unable to schedule node...no more slots available!\n");
}

static void deschedule_device(PROTOCOL_EUI_TYPE *schedule_array, uint8_t *timeout_array, uint8_t *num_scheduled, PROTOCOL_EUI_TYPE eui)
{
   // Search for the specified EUI
   for (uint8_t i = 0; i < PROTOCOL_MAX_NUM_DEVICES_PER_TYPE; ++i)
      if (schedule_array[i] == eui)
      {
         schedule_array[i] = 0;
         timeout_array[i] = 0;
         --(*num_scheduled);
      }

   // Set a new backup scheduler if the existing one was removed
   if ((eui == _schedule_packet.backup_scheduler_eui) && !switch_backup_scheduler(eui))
      _schedule_packet.backup_scheduler_eui = _schedule_packet.scheduler_eui;
}

static void update_schedule(void)
{
   // Only handle schedule updates if we are the master SCHEDULER
   if (_config.scheduler_role == SCHEDULER)
   {
      // Update device timeouts
      for (int i = 0; i < PROTOCOL_MAX_NUM_DEVICES_PER_TYPE; ++i)
      {
         // Update requester timeouts
         if ((_request_schedule[i] > 0) && (_request_schedule[i] != _config.EUI))
         {
            if (_requester_device_timeouts[i]++ == PROTOCOL_DEVICE_TIMEOUT_SECONDS)
            {
               debug_msg("INFO: Device EUI ");
               debug_msg_eui(_request_schedule[i]);
               debug_msg(" has timed out...removing from the schedule\n");
               _request_schedule[i] = 0;
               --_num_requesters_scheduled;
               _requester_device_timeouts[i] = 0;
            }
         }
         else
            _requester_device_timeouts[i] = 0;

         // Update responder timeouts
         if ((_response_schedule[i] > 0) && (_response_schedule[i] != _config.EUI))
         {
            if (_responder_device_timeouts[i]++ == PROTOCOL_DEVICE_TIMEOUT_SECONDS)
            {
               debug_msg("INFO: Device EUI ");
               debug_msg_eui(_response_schedule[i]);
               debug_msg(" has timed out...removing from the schedule\n");
               _response_schedule[i] = 0;
               --_num_responders_scheduled;
               _responder_device_timeouts[i] = 0;
            }
         }
         else
            _responder_device_timeouts[i] = 0;

         // Update hybrid timeouts
         if ((_hybrid_schedule[i] > 0) && (_hybrid_schedule[i] != _config.EUI))
         {
            if (_hybrid_device_timeouts[i]++ == PROTOCOL_DEVICE_TIMEOUT_SECONDS)
            {
               debug_msg("INFO: Device EUI ");
               debug_msg_eui(_hybrid_schedule[i]);
               debug_msg(" has timed out...removing from the schedule\n");
               _hybrid_schedule[i] = 0;
               --_num_hybrids_scheduled;
               _hybrid_device_timeouts[i] = 0;
            }
         }
         else
            _hybrid_device_timeouts[i] = 0;
      }
   }
   else
   {
      if (_config.scheduler_role != BACKUP_SCHEDULER)
         _scheduled_request_timeslot = _scheduled_response_timeslot = -1;
      memset(_requester_device_timeouts, 0, sizeof(_requester_device_timeouts));
      memset(_responder_device_timeouts, 0, sizeof(_responder_device_timeouts));
      memset(_hybrid_device_timeouts, 0, sizeof(_hybrid_device_timeouts));
   }
}

static void reset_device_timeout(PROTOCOL_EUI_TYPE source_eui)
{
   // Reset the device timeout to 0 for the specified EUI
   for (uint8_t i = 0; i < PROTOCOL_MAX_NUM_DEVICES_PER_TYPE; ++i)
   {
      if (_request_schedule[i] == source_eui)
      {
         _requester_device_timeouts[i] = 0;
         return;
      }
      else if (_response_schedule[i] == source_eui)
      {
         _responder_device_timeouts[i] = 0;
         return;
      }
      else if (_hybrid_schedule[i] == source_eui)
      {
         _hybrid_device_timeouts[i] = 0;
         return;
      }
   }
}

static void determine_request_response_order(uint8_t num_requesters, uint8_t num_responders, uint8_t num_hybrids, const PROTOCOL_EUI_TYPE* requesters, const PROTOCOL_EUI_TYPE* responders, const PROTOCOL_EUI_TYPE* hybrids)
{
   // Determine REQUEST ordering
   uint8_t slot = 0;
   _scheduled_request_timeslot = _scheduled_response_timeslot = -1;
   for (uint8_t i = 0; i < num_requesters; ++i, ++slot)
   {
      _request_order[slot] = requesters[i];
      if (requesters[i] == _config.EUI)
         _scheduled_request_timeslot = slot * PROTOCOL_NUM_SLOTS_PER_REQUEST;
   }
   for (uint8_t i = 0; i < num_hybrids; ++i, ++slot)
   {
      _request_order[slot] = hybrids[i];
      if (hybrids[i] == _config.EUI)
         _scheduled_request_timeslot = slot * PROTOCOL_NUM_SLOTS_PER_REQUEST;
   }

   // Determine RESPONSE ORDERING
   slot = 0;
   for (uint8_t i = 0; i < num_hybrids; ++i, ++slot)
   {
      _response_order[slot] = hybrids[num_hybrids - i - 1];
      if (hybrids[num_hybrids - i - 1] == _config.EUI)
         _scheduled_response_timeslot = slot * PROTOCOL_NUM_SLOTS_PER_RESPONSE;
   }
   for (uint8_t i = 0; i < num_responders; ++i, ++slot)
   {
      _response_order[slot] = responders[i];
      if (responders[i] == _config.EUI)
         _scheduled_response_timeslot = slot * PROTOCOL_NUM_SLOTS_PER_RESPONSE;
   }
}

static void determine_expected_valid_ranges(void)
{
   // Determine which devices we can expect to receive a ranging response from
   _num_results_scheduled = _num_requesters_scheduled + _num_hybrids_scheduled;
   _num_expected_valid_ranges = 0;
   if (_scheduled_request_timeslot >= 0)
   {
      for (uint8_t i = 0; i < _num_responders_scheduled; ++i)
         if (_response_schedule[i] && (_response_schedule[i] != _config.EUI))
            _expected_valid_ranges[_num_expected_valid_ranges++] = _response_schedule[i];
      if ((_config.device_role == REQUESTER) || ((_config.device_role == HYBRID) && _config.hybrids_perform_all_rangings))
      {
         for (uint8_t i = 0; i < _num_hybrids_scheduled; ++i)
            if (_hybrid_schedule[i] && (_hybrid_schedule[i] != _config.EUI))
               _expected_valid_ranges[_num_expected_valid_ranges++] = _hybrid_schedule[i];
      }
      else if (_config.device_role == HYBRID)
      {
         for (uint8_t i = 0; i < _num_hybrids_scheduled; ++i)
            if (_hybrid_schedule[_num_hybrids_scheduled - i - 1] && (_hybrid_schedule[_num_hybrids_scheduled - i - 1] != _config.EUI) && (i < _scheduled_response_timeslot))
               _expected_valid_ranges[_num_expected_valid_ranges++] = _hybrid_schedule[_num_hybrids_scheduled - i - 1];
      }
   }
}

static void transmit_schedule(bool is_master_schedule, uint32_t transmit_time_dw)
{
   // Create schedule transmission message
   _schedule_packet.message_type = (is_master_schedule ? SCHEDULE_PACKET : SCHEDULE_SYNC_PACKET);
   compress_schedule(_request_schedule, _requester_device_timeouts, PROTOCOL_MAX_NUM_REQUESTERS);
   compress_schedule(_response_schedule, _responder_device_timeouts, PROTOCOL_MAX_NUM_RESPONDERS);
   compress_schedule(_hybrid_schedule, _hybrid_device_timeouts, PROTOCOL_MAX_NUM_HYBRIDS);
   _schedule_packet.request_schedule_length = _num_requesters_scheduled;
   _schedule_packet.response_schedule_length = _num_responders_scheduled;
   _schedule_packet.hybrid_schedule_length = _num_hybrids_scheduled;
   memset(_schedule_packet.eui_array, 0, sizeof(_schedule_packet.eui_array));
   memcpy(_schedule_packet.eui_array, _request_schedule, _num_requesters_scheduled * PROTOCOL_EUI_SIZE);
   memcpy(_schedule_packet.eui_array + _num_requesters_scheduled, _hybrid_schedule, _num_hybrids_scheduled * PROTOCOL_EUI_SIZE);
   memcpy(_schedule_packet.eui_array + _num_requesters_scheduled + _num_hybrids_scheduled, _response_schedule, _num_responders_scheduled * PROTOCOL_EUI_SIZE);
   uint16_t packet_size = sizeof(struct ieee154_header_broadcast) + sizeof(struct ieee154_footer) + SCHEDULE_PACKET_PAYLOAD_LENGTH + ((_num_requesters_scheduled + _num_responders_scheduled + _num_hybrids_scheduled) * PROTOCOL_EUI_SIZE);

   // Trigger send operation
   if (!glossy_transmit_packet(transmit_time_dw, (uint8_t*)&_schedule_packet, packet_size, TRUE))
   {
      debug_msg("ERROR: Failed to transmit schedule with length ");
      debug_msg_uint(packet_size);
      debug_msg("\n");
      ++_failure_count;
   }
   else
   {
      // Output the schedule to the console
      debug_msg(is_master_schedule ? "INFO: Sent Master Schedule: " : "INFO: Sent Backup Schedule: ");
      print_schedule();
      _failure_count = 0;
      if (!is_master_schedule)
         determine_expected_valid_ranges();

      // Signal schedule distribution by turning LEDs OFF (will turn back on after 200ms)
      if (!is_master_schedule)
         led_off();
   }

   // Make sure the backup EUI is set correctly in case we are currently both SCHEDULER and BACKUP
   if (is_master_schedule)
      _received_schedule = FALSE;
   else
      _received_backup_schedule = FALSE;
   if (!is_master_schedule && (_config.scheduler_role == SCHEDULER) && (_schedule_packet.scheduler_eui == _schedule_packet.backup_scheduler_eui) && ((_num_requesters_scheduled + _num_responders_scheduled + _num_hybrids_scheduled) > 1))
   {
      _received_backup_schedule = TRUE;
      switch_backup_scheduler(0);
   }
}

static void receive_schedule(const schedule_packet_t* schedule, bool is_master_schedule)
{
   if (schedule)
   {
      // Ensure that all schedule lengths received are valid, and signal that a valid schedule has been received
      if ((schedule->request_schedule_length > PROTOCOL_MAX_NUM_REQUESTERS) || (schedule->response_schedule_length > PROTOCOL_MAX_NUM_RESPONDERS) || (schedule->hybrid_schedule_length > PROTOCOL_MAX_NUM_HYBRIDS))
      {
         debug_msg("ERROR: Received a schedule containing an invalid number devices...ignoring!\n");
         return;
      }

      // Update the Unix epoch timestamp as long as it is within a year of the time we currently think it is
      if ((schedule->epoch_time_unix > 1612810414) && ((_schedule_packet.epoch_time_unix < 1612810414) || (abs((int32_t)(schedule->epoch_time_unix - _schedule_packet.epoch_time_unix)) <= SECONDS_PER_YEAR)))
         _schedule_packet.epoch_time_unix = schedule->epoch_time_unix;
      else
      {
         debug_msg("WARNING: Received improbable epoch timestamp: ");
         debug_msg_uint(schedule->epoch_time_unix);
         debug_msg("\n");
      }

      // Copy schedule into local storage
      _num_requesters_scheduled = schedule->request_schedule_length;
      _num_responders_scheduled = schedule->response_schedule_length;
      _num_hybrids_scheduled = schedule->hybrid_schedule_length;
      memset(_request_schedule + schedule->request_schedule_length, 0, (PROTOCOL_MAX_NUM_REQUESTERS - schedule->request_schedule_length) * PROTOCOL_EUI_SIZE);
      memset(_hybrid_schedule + schedule->hybrid_schedule_length, 0, (PROTOCOL_MAX_NUM_HYBRIDS - schedule->hybrid_schedule_length) * PROTOCOL_EUI_SIZE);
      memset(_response_schedule + schedule->response_schedule_length, 0, (PROTOCOL_MAX_NUM_RESPONDERS - schedule->response_schedule_length) * PROTOCOL_EUI_SIZE);
      memcpy(_request_schedule, schedule->eui_array, schedule->request_schedule_length * PROTOCOL_EUI_SIZE);
      memcpy(_hybrid_schedule, schedule->eui_array + schedule->request_schedule_length, schedule->hybrid_schedule_length * PROTOCOL_EUI_SIZE);
      memcpy(_response_schedule, schedule->eui_array + schedule->request_schedule_length + schedule->hybrid_schedule_length, schedule->response_schedule_length * PROTOCOL_EUI_SIZE);

      // Determine when everyone is scheduled to request or respond
      memcpy(requester_eui_array, schedule->eui_array, PROTOCOL_EUI_SIZE * schedule->request_schedule_length);
      memcpy(hybrid_eui_array, schedule->eui_array + schedule->request_schedule_length, PROTOCOL_EUI_SIZE * schedule->hybrid_schedule_length);
      memcpy(responder_eui_array, schedule->eui_array + (schedule->request_schedule_length + schedule->hybrid_schedule_length), PROTOCOL_EUI_SIZE * schedule->response_schedule_length);
      determine_request_response_order(schedule->request_schedule_length, schedule->response_schedule_length, schedule->hybrid_schedule_length, requester_eui_array, responder_eui_array, hybrid_eui_array);

      // Determine the start times of the various scheduled phases
      _request_phase_start_slot = 1;
      _response_phase_start_slot = _request_phase_start_slot + ((_num_requesters_scheduled + _num_hybrids_scheduled) * PROTOCOL_NUM_SLOTS_PER_REQUEST);
      _shared_bus_phase_slot_start = _response_phase_start_slot + ((_num_responders_scheduled + _num_hybrids_scheduled) * PROTOCOL_NUM_SLOTS_PER_RESPONSE);
      _schedule_xmit_phase_slot_start_next = _shared_bus_phase_slot_start + 1;
      if (!is_master_schedule)
      {
         _num_results_scheduled = _num_requesters_scheduled + _num_hybrids_scheduled;
         _schedule_xmit_phase_slot_start = _schedule_xmit_phase_slot_start_next;
         _ranging_phase_slot_start = _schedule_xmit_phase_slot_start_next + 1;
         _results_phase_slot_start = _ranging_phase_slot_start + 1;
         _error_correction_phase_slot_start = _results_phase_slot_start + _num_results_scheduled;
      }

      // Determine if our scheduling role has changed
      _schedule_packet.scheduler_eui = schedule->scheduler_eui;
      _schedule_packet.backup_scheduler_eui = schedule->backup_scheduler_eui;
      _config.scheduler_role = (_schedule_packet.scheduler_eui == _config.EUI) ? SCHEDULER : ((_schedule_packet.backup_scheduler_eui == _config.EUI) ? BACKUP_SCHEDULER : PARTICIPANT);

      // Print the schedule and handle additional sync packet reception tasks
      if (is_master_schedule)
      {
         _received_schedule = TRUE;
         debug_msg("INFO: Received Master Schedule: ");
         print_schedule();
      }
      else
      {
         // Determine the devices for which we are expecting to calculate a range
         _received_backup_schedule = TRUE;
         debug_msg("INFO: Received Backup Schedule: ");
         print_schedule();
         determine_expected_valid_ranges();

         // Signal schedule reception time by turning LEDs OFF (will turn back on after 200ms)
         led_off();
      }
   }
   else
   {
      // Signal that no valid schedule has yet been received
      if (is_master_schedule)
         _received_schedule = FALSE;
      else
         _received_backup_schedule = FALSE;

      // Setup the correct antenna and channel for schedule reception and immediately start listening
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, SCHEDULE_XMIT_CHANNEL, SCHEDULE_XMIT_ANTENNA);
   }
}

static void transmit_results(uint32_t transmit_time_dw)
{
   // Do not transmit if no valid ranges were calculated
   if (!_num_successful_ranges)
      return;

   // Create results transmission message containing only successful results
   _results_packet.results_length = _num_successful_ranges;
   uint16_t packet_size = sizeof(struct ieee154_header_broadcast) + sizeof(struct ieee154_footer) + RESULTS_PACKET_PAYLOAD_LENGTH + (_num_successful_ranges * PACKET_SINGLE_RESULT_LENGTH);
   memcpy(_results_packet.results, ids_and_ranges + 1, _num_successful_ranges * PACKET_SINGLE_RESULT_LENGTH);

   // Trigger send operation
   if (!glossy_transmit_packet(transmit_time_dw, (uint8_t*)&_results_packet, packet_size, TRUE))
   {
      debug_msg("ERROR: Failed to transmit ranging results with length ");
      debug_msg_uint(packet_size);
      debug_msg("\n");
      ++_failure_count;
   }
   else
   {
      debug_msg("INFO: Sent local ranging results to remote devices\n");
      _failure_count = 0;
   }
}

static void receive_results(const results_packet_t* results)
{
   if (results && !_results_received)
   {
      // Ensure that the ranging results array length is valid
      if (results->results_length > (PROTOCOL_MAX_NUM_REQUESTERS + PROTOCOL_MAX_NUM_HYBRIDS))
      {
         debug_msg("ERROR: Received a results packet containing an invalid number devices...ignoring!\n");
         return;
      }

      // Search for a valid ranging result with this device as the destination
      int32_t received_range = 0, current_range = 0;
      uint8_t output_buffer_index = 1 + (scratch_ranges[0] * PACKET_SINGLE_RESULT_LENGTH);
      for (uint8_t i = 0, offset = 0; i < results->results_length; ++i, offset += PACKET_SINGLE_RESULT_LENGTH)
      {
         memcpy(&received_range, results->results + offset + PROTOCOL_EUI_SIZE, sizeof(received_range));
         if ((memcmp(results->results + offset, &_config.EUI, PROTOCOL_EUI_SIZE) == 0) && (received_range >= 0))
         {
            // Update an existing range to the specified device
            bool existing_range_found = FALSE;
            for (uint8_t j = 0; j < scratch_ranges[0]; ++j)
               if (memcmp(scratch_ranges + 1 + (j * PACKET_SINGLE_RESULT_LENGTH), results->header.sourceAddr, PROTOCOL_EUI_SIZE) == 0)
               {
                  // Take the simple average of the two ranges as the final range
                  existing_range_found = TRUE;
                  memcpy(&current_range, scratch_ranges + 1 + PROTOCOL_EUI_SIZE + (j * PACKET_SINGLE_RESULT_LENGTH), sizeof(current_range));
                  if (current_range >= 0)
                  {
                     current_range = (current_range + received_range) / 2;
                     memcpy(scratch_ranges + 1 + PROTOCOL_EUI_SIZE + (j * PACKET_SINGLE_RESULT_LENGTH), &current_range, sizeof(current_range));
                  }
                  else
                     memcpy(scratch_ranges + 1 + PROTOCOL_EUI_SIZE + (j * PACKET_SINGLE_RESULT_LENGTH), &received_range, sizeof(received_range));
               }

            // Add the remote device's range if ours was non-existent
            if (!existing_range_found)
            {
               memcpy(scratch_ranges + output_buffer_index, results->header.sourceAddr, PROTOCOL_EUI_SIZE);
               output_buffer_index += PROTOCOL_EUI_SIZE;
               memcpy(scratch_ranges + output_buffer_index, &received_range, sizeof(received_range));
               output_buffer_index += sizeof(received_range);
               ++scratch_ranges[0];
            }
            break;
         }
      }
   }
   else
   {
      // Setup the correct antenna and channel for results reception and immediately start listening
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, SCHEDULE_XMIT_CHANNEL, SCHEDULE_XMIT_ANTENNA);
      _results_received = FALSE;
   }
}

static void transmit_schedule_requests(uint32_t transmit_time_dw)
{
   // Determine whether we need to request a scheduled time slot
   if (((_scheduled_request_timeslot == -1) && ((_config.device_role == REQUESTER) || (_config.device_role == HYBRID))) || ((_scheduled_response_timeslot == -1) && ((_config.device_role == RESPONDER) || (_config.device_role == HYBRID))))
   {
      // Send a schedule request sometime within the SHARED BUS phase
      debug_msg("INFO: Sending request to be scheduled by the SCHEDULER node\n");
      _control_packet.device_eui = _config.EUI;
      _control_packet.packet_type = (_config.device_role == REQUESTER) ? CONTROL_ADD_REQUESTER : ((_config.device_role == RESPONDER) ? CONTROL_ADD_RESPONDER : CONTROL_ADD_HYBRID);
      if (!glossy_transmit_packet(transmit_time_dw, (uint8_t*)&_control_packet, sizeof(_control_packet), FALSE))
      {
         debug_msg("ERROR: Failed to request schedule using control packet\n");
         ++_failure_count;
      }
      else
         _failure_count = 0;
   }
}

static void receive_schedule_requests(const control_packet_t* control)
{
   if (control)
   {
      // Output the EUI of the received shared bus request
      debug_msg("INFO: Shared bus request received for EUI ");
      debug_msg_eui(control->device_eui);
      debug_msg("\n");

      // Parse the schedule request packet and add or remove the device EUI to the schedule
      switch (control->packet_type)
      {
         case CONTROL_ADD_REQUESTER:
            debug_msg("INFO: Request was to schedule as a REQUESTER\n");
            schedule_device(_request_schedule, _requester_device_timeouts, &_num_requesters_scheduled, control->device_eui);
            break;
         case CONTROL_ADD_RESPONDER:
            debug_msg("INFO: Request was to schedule as a RESPONDER\n");
            schedule_device(_response_schedule, _responder_device_timeouts, &_num_responders_scheduled, control->device_eui);
            break;
         case CONTROL_ADD_HYBRID:
            debug_msg("INFO: Request was to schedule as a HYBRID\n");
            schedule_device(_hybrid_schedule, _hybrid_device_timeouts, &_num_hybrids_scheduled, control->device_eui);
            break;
         case CONTROL_REMOVE_REQUESTER:
            debug_msg("INFO: Request was to deschedule as a REQUESTER\n");
            deschedule_device(_request_schedule, _requester_device_timeouts, &_num_requesters_scheduled, control->device_eui);
            break;
         case CONTROL_REMOVE_RESPONDER:
            debug_msg("INFO: Request was to deschedule as a RESPONDER\n");
            deschedule_device(_response_schedule, _responder_device_timeouts, &_num_responders_scheduled, control->device_eui);
            break;
         case CONTROL_REMOVE_HYBRID:
            debug_msg("INFO: Request was to deschedule as a HYBRID\n");
            deschedule_device(_hybrid_schedule, _hybrid_device_timeouts, &_num_hybrids_scheduled, control->device_eui);
            break;
         default:
            debug_msg("ERROR: Unknown shared bus request received (");
            debug_msg_uint(control->packet_type);
            debug_msg(")\n");
            break;
      }

      // Re-determine when everyone is scheduled to request or respond
      determine_request_response_order(_num_requesters_scheduled, _num_responders_scheduled, _num_hybrids_scheduled, _request_schedule, _response_schedule, _hybrid_schedule);
   }

   // Setup the correct antenna and channel for schedule request reception and immediately start listening
   dw1000_rxenable(DWT_START_RX_IMMEDIATE, SCHEDULE_XMIT_CHANNEL, SCHEDULE_XMIT_ANTENNA);
}

static void tx_callback(const dwt_cb_data_t *txData)
{
   // Handle transmission completion
   switch (_scheduler_phase)
   {
      case BACKUP_SCHEDULE_PHASE:
         if (!glossy_txcallback())
            _scheduler_phase = RANGE_REQUEST_PHASE;
         break;
      case RANGE_REQUEST_PHASE:
         ranging_request_txcallback(txData->status & SYS_STATUS_TXFRS);
         break;
      case RANGE_RESPONSE_PHASE:
         ranging_response_txcallback(txData->status & SYS_STATUS_TXFRS);
         break;
      case SHARED_BUS_PHASE:
         // Re-enable shared bus listening if we are the scheduler or backup and the Glossy flood is complete
         if (!glossy_txcallback() && ((_config.scheduler_role == SCHEDULER) || (_config.scheduler_role == BACKUP_SCHEDULER)))
            dw1000_rxenable(DWT_START_RX_IMMEDIATE, SCHEDULE_XMIT_CHANNEL, SCHEDULE_XMIT_ANTENNA);
         break;
      case MASTER_SCHEDULE_PHASE:
         if (!glossy_txcallback())
            _scheduler_phase = RANGING_PHASE;
         break;
      case RESULTS_PHASE:
         glossy_txcallback();
         break;
      default:
         break;
   }
}

static void rx_callback(const dwt_cb_data_t *rxData)
{
   // Retrieve timestamp, packet length, and packet bytes, and ensure that no errors were encountered
   uint32_t reception_callback_time = timer_value_us(_scheduler_timer) + 298;    // Magic number based on how long it takes to verify received packet
   uint64_t dw_timestamp_raw = dw1000_readrxtimestamp();
   uint32_t dw_timestamp_non_ranging = (uint32_t)((dw_timestamp_raw - dw1000_get_rx_delay(0, 0)) >> 8);
   if (rxData->datalength > MSG_MAX_PACKET_LENGTH)
   {
      debug_msg("ERROR: Received packet which exceeds maximal length (received ");
      debug_msg_uint(rxData->datalength);
      debug_msg(" bytes)!\n");
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, DO_NOT_CHANGE_FLAG, DO_NOT_CHANGE_FLAG);
      return;
   }
   else if (!(rxData->status & SYS_STATUS_RXFCG) || (rxData->status & SYS_STATUS_ALL_RX_ERR) || (rxData->status & SYS_STATUS_ALL_RX_TO) || (rxData->status & SYS_STATUS_RXOVRR))
   {
      if (!(rxData->status & SYS_STATUS_RXPTO) && !(rxData->status & SYS_STATUS_RXSFDTO))
         dwt_rxreset();
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, DO_NOT_CHANGE_FLAG, DO_NOT_CHANGE_FLAG);
      /*debug_msg("DEBUG: Packet reception error, Status Code: ");
      debug_msg_uint(rxData->status);
      debug_msg("\n");*/
      return;
   }
   uint16_t num_transactions = (rxData->datalength / MAX_SPI_TRANSACTION_BYTES) + ((rxData->datalength % MAX_SPI_TRANSACTION_BYTES) ? 1 : 0);
   for (uint16_t i = 0; i < num_transactions; ++i)
      dwt_readrxdata(read_buffer + (i * MAX_SPI_TRANSACTION_BYTES), ((i == (num_transactions - 1)) ? (rxData->datalength - (i * MAX_SPI_TRANSACTION_BYTES)) : MAX_SPI_TRANSACTION_BYTES), i * MAX_SPI_TRANSACTION_BYTES);

   // Fetch the message type and let Glossy flood it if it is a schedule packet
   uint8_t message_type = read_buffer[sizeof(struct ieee154_header_broadcast)];
   if ((message_type == SCHEDULE_PACKET) || (message_type == SCHEDULE_SYNC_PACKET))
      glossy_rxcallback(dw_timestamp_non_ranging, read_buffer, rxData->datalength);

   // Reset the device timeout for a received packet from a device
   PROTOCOL_EUI_TYPE source_eui = 0;
   memcpy(&source_eui, ((struct ieee154_header_broadcast*)read_buffer)->sourceAddr, PROTOCOL_EUI_SIZE);
   if (_config.scheduler_role == SCHEDULER)
      reset_device_timeout(source_eui);

   // Always attempt to synchronize the local clock with the backup scheduler's clock
   if (message_type == SCHEDULE_SYNC_PACKET)
   {
      _next_slot_number = 1;
      _network_synchronized = TRUE;
      _scheduler_phase = BACKUP_SCHEDULE_PHASE;
      schedule_packet_t *sched_packet = (schedule_packet_t*)read_buffer;
      timer_reset(_scheduler_timer, SCHEDULE_XMIT_TIME_OFFSET_US + ((uint32_t)sched_packet->header.seqNum * GLOSSY_FLOOD_INTERVAL_US) + US_DELAY_FROM_PKT_LEN(rxData->datalength) + (timer_value_us(_scheduler_timer) - reception_callback_time));
      receive_schedule((schedule_packet_t*)read_buffer, FALSE);
   }

   // Handle the received packet based on its type
   if (_network_synchronized)
   {
      if ((message_type == SCHEDULE_PACKET) && (_scheduler_phase == MASTER_SCHEDULE_PHASE))
         receive_schedule((schedule_packet_t*)read_buffer, TRUE);
      else if ((message_type == CONTROL_PACKET) && (_scheduler_phase == SHARED_BUS_PHASE))
         receive_schedule_requests((control_packet_t*)read_buffer);
      else if ((message_type == RANGE_REQUEST_PACKET) && (_scheduler_phase == RANGE_REQUEST_PHASE))
      {
         ++_num_ranging_packets_received;
         receive_ranging_request(dw_timestamp_raw, (request_packet_t*)read_buffer);
      }
      else if ((message_type == RANGE_RESPONSE_PACKET) && (_scheduler_phase == RANGE_RESPONSE_PHASE))
      {
         ++_num_ranging_packets_received;
         receive_ranging_response(_config.EUI, dw_timestamp_raw, (response_packet_t*)read_buffer);
      }
      else if ((message_type == RESULTS_PACKET) && (_scheduler_phase == RESULTS_PHASE))
      {
         receive_results((results_packet_t*)read_buffer);
         _results_received = TRUE;
      }
   }
   else     // If network is not synchronized, always keep the receiver enabled searching for a network
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, SCHEDULE_XMIT_CHANNEL, SCHEDULE_XMIT_ANTENNA);
}

static void force_role_as_scheduler(void)
{
   // Set self as both the master and backup scheduler
   _network_synchronized = TRUE;
   _config.scheduler_role = SCHEDULER;
   _schedule_packet.scheduler_eui = _schedule_packet.backup_scheduler_eui = _config.EUI;

   // Add self to the current schedule
   switch (_config.device_role)
   {
      case REQUESTER:
         schedule_device(_request_schedule, _requester_device_timeouts, &_num_requesters_scheduled, _config.EUI);
         _scheduled_request_timeslot = 0;
         _request_order[0] = _config.EUI;
         break;
      case RESPONDER:
         schedule_device(_response_schedule, _responder_device_timeouts, &_num_responders_scheduled, _config.EUI);
         _scheduled_response_timeslot = 0;
         _response_order[0] = _config.EUI;
         break;
      case HYBRID:
         schedule_device(_hybrid_schedule, _hybrid_device_timeouts, &_num_hybrids_scheduled, _config.EUI);
         _scheduled_request_timeslot = _scheduled_response_timeslot = 0;
         _request_order[0] = _response_order[0] = _config.EUI;
         break;
      default:
         break;
   }

   // Determine the start times of the various scheduled phases
   _request_phase_start_slot = 1;
   _response_phase_start_slot = _request_phase_start_slot + ((_num_requesters_scheduled + _num_hybrids_scheduled) * PROTOCOL_NUM_SLOTS_PER_REQUEST);
   _shared_bus_phase_slot_start = _response_phase_start_slot + ((_num_responders_scheduled + _num_hybrids_scheduled) * PROTOCOL_NUM_SLOTS_PER_RESPONSE);
   _schedule_xmit_phase_slot_start_next = _shared_bus_phase_slot_start + 1;
}

static bool search_for_network(void)
{
   // Enable listening for a schedule or synchronization packet
   reset_watchdog();
   if (_network_search_time++ == 0)
   {
      // Reset the entire scheduler state
      debug_msg("WARNING: Not part of an existing network. Searching...\n");
      _ranging_successful = _previous_ranging_successful = FALSE;
      memset(_request_schedule, 0, sizeof(_request_schedule));
      memset(_response_schedule, 0, sizeof(_response_schedule));
      memset(_hybrid_schedule, 0, sizeof(_hybrid_schedule));
      memset(_request_order, 0, sizeof(_request_order));
      memset(_response_order, 0, sizeof(_response_order));
      memset(_requester_device_timeouts, 0, sizeof(_requester_device_timeouts));
      memset(_responder_device_timeouts, 0, sizeof(_responder_device_timeouts));
      memset(_hybrid_device_timeouts, 0, sizeof(_hybrid_device_timeouts));
      _num_requesters_scheduled = _num_responders_scheduled = _num_hybrids_scheduled = 0;
#ifndef BYPASS_HOST_INTERFACE
      _network_search_time_end = MIN_NETWORK_SEARCH_TIME_US / PROTOCOL_SLOT_PERIOD_US;
#else
      _network_search_time_end = (MIN_NETWORK_SEARCH_TIME_US + (ranval(&_prng_state) % (MAX_NETWORK_SEARCH_TIME_US - MIN_NETWORK_SEARCH_TIME_US))) / PROTOCOL_SLOT_PERIOD_US;
#endif

      // Re-initialize the entire DW1000 configuration in case we got here from an unrecoverable radio error
      dw1000_reset_hard(FALSE);
      if (!dw1000_force_wakeup() || (dw1000_init() != DW1000_NO_ERR) || !reset_dw1000_configuration())
      {
         debug_msg("ERROR: Unable to wake up or re-initialize the DW1000 radio!\n");
         module_reset();
      }
      else
         dw1000_rxenable(DWT_START_RX_IMMEDIATE, SCHEDULE_XMIT_CHANNEL, SCHEDULE_XMIT_ANTENNA);
      update_led_statuses();
   }
   else if (_network_search_time == _network_search_time_end)
   {
      // If timed out, stop module until the host informs us that a network is available
#ifndef BYPASS_HOST_INTERFACE
      if (++_network_search_timeout == PROTOCOL_MAX_NUM_NETWORK_SEARCH_ATTEMPTS)
      {
         debug_msg("WARNING: Timed out searching for an existing network\n");
         module_reset();
         return FALSE;
      }
#endif

      // Set current role to SCHEDULER
      debug_msg("WARNING: Could not find an existing network. Taking over as SCHEDULER...\n");
      _network_search_time = _network_search_time_end = 0;
      _scheduler_phase = UNSCHEDULED_TIME_PHASE;
      force_role_as_scheduler();
      return TRUE;
   }

   // No network located
   return FALSE;
}

static void fix_network_errors(void)
{
   // Reset the whole module if too many consecutive RxTx failures were encountered
   if (_failure_count == MAXIMUM_NUM_RXTX_FAILURES)
      module_reset();

   // Set the empty round timeout statistic
   bool empty_round = _num_ranging_packets_received < MIN_PACKETS_FOR_NONEMPTY_ROUND;
   if (empty_round)
      ++_empty_round_timeout;
   else
      _empty_round_timeout = _network_search_timeout = 0;

   // Change roles based on network errors
   switch (_config.scheduler_role)
   {
      case SCHEDULER:
         if (!_received_backup_schedule)
         {
            if (!empty_round)
            {
               if ((++_missing_schedule_timeout == PROTOCOL_MAX_MISSING_SCHEDULE_ROUNDS_BEFORE_STATE_CHANGE) && !switch_backup_scheduler(_schedule_packet.backup_scheduler_eui))
                  _config.scheduler_role = UNKNOWN;
            }
            else if (_empty_round_timeout == PROTOCOL_MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE)
               _config.scheduler_role = UNKNOWN;
         }
         else
            _missing_schedule_timeout = 0;
         break;
      case BACKUP_SCHEDULER:
         if (!_received_schedule)
         {
            if (!empty_round)
            {
               if (++_missing_schedule_timeout == PROTOCOL_MAX_MISSING_SCHEDULE_ROUNDS_BEFORE_STATE_CHANGE)
               {
                  _config.scheduler_role = SCHEDULER;
                  _schedule_packet.scheduler_eui = _schedule_packet.backup_scheduler_eui;
               }
            }
            else if (_empty_round_timeout == PROTOCOL_MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE)
            {
               _config.scheduler_role = PARTICIPANT;
               _network_synchronized = FALSE;
            }
         }
         else
            _missing_schedule_timeout = 0;
         break;
      case PARTICIPANT:
         if (!_received_schedule && !_received_backup_schedule)
            _network_synchronized = FALSE;
         else
            _missing_schedule_timeout = 0;
         break;
      default:
         break;
   }

   // Reset all states if our role has become unknown
   if (_config.scheduler_role == UNKNOWN)
   {
      // Reset all internal states
      glossy_reset_state();
      reset_request_phase();
      reset_response_phase();
      reset_ranging_phase();

      // Stop the module and let Bluetooth rediscover the network
#ifndef BYPASS_HOST_INTERFACE
      debug_msg("WARNING: No network traffic received\n");
      module_reset();
#endif
   }

   // Move to the next scheduler phase
   if (_empty_round_timeout == PROTOCOL_MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE)
      _empty_round_timeout = 0;
   _scheduler_phase = UNSCHEDULED_TIME_PHASE;
}

static void perform_scheduled_slot_task(void)
{
   // Set the correct scheduler phase based on the current schedule and slot number
   _slot_start_timestamp = dwt_readsystimestamphi32();
   if ((_next_slot_number == ((PROTOCOL_SCHEDULING_INTERVAL_US / PROTOCOL_SLOT_PERIOD_US) - 1)) && !dw1000_wakeup() && !dw1000_force_wakeup())
   {
      debug_msg("ERROR: Unable to wake up the DW1000 radio!\n");
      module_reset();
      return;
   }
   else if ((_next_slot_number == 0) || (_next_slot_number == (PROTOCOL_SCHEDULING_INTERVAL_US / PROTOCOL_SLOT_PERIOD_US)))
   {
      initialize_new_round();
      _scheduler_phase = !_network_synchronized ? UNSCHEDULED_TIME_PHASE : BACKUP_SCHEDULE_PHASE;
   }
   else if (_scheduler_phase != UNSCHEDULED_TIME_PHASE)
   {
      if (_next_slot_number == _error_correction_phase_slot_start)
         _scheduler_phase = ERROR_CORRECTION_PHASE;
      else if (_next_slot_number == _results_phase_slot_start)
         _scheduler_phase = RESULTS_PHASE;
      else if (_next_slot_number == _ranging_phase_slot_start)
         _scheduler_phase = RANGING_PHASE;
      else if (_next_slot_number == _schedule_xmit_phase_slot_start)
         _scheduler_phase = MASTER_SCHEDULE_PHASE;
      else if (_next_slot_number == _shared_bus_phase_slot_start)
         _scheduler_phase = SHARED_BUS_PHASE;
      else if (_next_slot_number == _response_phase_start_slot)
         _scheduler_phase = RANGE_RESPONSE_PHASE;
      else if (_next_slot_number == _request_phase_start_slot)
      {
         _scheduled_result_timeslot = (_scheduled_request_timeslot < 0) ? -5 : (_scheduled_request_timeslot / PROTOCOL_NUM_SLOTS_PER_REQUEST);
         _scheduler_phase = RANGE_REQUEST_PHASE;
      }
   }

   // Update the status LEDs 200ms after the beginning of each schedule
   if (++_next_slot_number == 20)
      update_led_statuses();

   // If not synchronized, attempt to determine our correct role in an existing network
   if (!_network_synchronized && !search_for_network())
   {
      // Send a keep-alive message to the host
      if (_next_slot_number == 1)
      {
         ids_and_ranges[0] = 0;
         memset(ids_and_ranges + 1, 0, PROTOCOL_EUI_SIZE);
         memcpy(ids_and_ranges + 1 + PROTOCOL_EUI_SIZE, &_schedule_packet.epoch_time_unix, sizeof(_schedule_packet.epoch_time_unix));
         host_interface_notify_ranges(ids_and_ranges, 1 + PROTOCOL_EUI_SIZE + sizeof(_schedule_packet.epoch_time_unix));
      }
      return;
   }
   _network_search_time = _network_search_time_end = 0;

   // Carry out the correct slot functionality based on device role and current schedule phase
   switch (_scheduler_phase)
   {
      case BACKUP_SCHEDULE_PHASE:
         if ((_config.scheduler_role == BACKUP_SCHEDULER) || ((_config.scheduler_role == SCHEDULER) && (_schedule_packet.scheduler_eui == _schedule_packet.backup_scheduler_eui)))
            transmit_schedule(FALSE, _slot_start_timestamp + DW_DELAY_FROM_US(SCHEDULE_XMIT_TIME_OFFSET_US) - (uint32_t)(dw1000_get_tx_delay(0, 0) >> 8));
         else
            receive_schedule(NULL, FALSE);
         break;
      case RANGE_REQUEST_PHASE:
         if ((_num_requesters_scheduled + _num_hybrids_scheduled + _num_responders_scheduled) <= 1)
            dw1000_radio_disable();
         else if (_config.device_role == RESPONDER)
            receive_ranging_requests(_request_order[(_next_slot_number - _request_phase_start_slot - 1) / PROTOCOL_NUM_SLOTS_PER_REQUEST]);
         else if (((int16_t)(_next_slot_number - _request_phase_start_slot) > _scheduled_request_timeslot) && ((int16_t)(_next_slot_number - _request_phase_start_slot) <= (_scheduled_request_timeslot + PROTOCOL_NUM_SLOTS_PER_REQUEST)))
            send_ranging_request(_slot_start_timestamp + DW_DELAY_FROM_US(RANGING_REQUEST_TIME_OFFSET_US));
         else if ((_config.device_role == HYBRID) && (((int16_t)(_next_slot_number - _request_phase_start_slot) <= _scheduled_request_timeslot) || _config.hybrids_perform_all_rangings))
            receive_ranging_requests(_request_order[(_next_slot_number - _request_phase_start_slot - 1) / PROTOCOL_NUM_SLOTS_PER_REQUEST]);
         else
            dw1000_radio_disable();
         break;
      case RANGE_RESPONSE_PHASE:
         if ((_num_requesters_scheduled + _num_hybrids_scheduled + _num_responders_scheduled) <= 1)
            dw1000_radio_disable();
         else if (_config.device_role == REQUESTER)
            receive_ranging_responses(_response_order[(_next_slot_number - _response_phase_start_slot - 1) / PROTOCOL_NUM_SLOTS_PER_RESPONSE]);
         else if (((int16_t)(_next_slot_number - _response_phase_start_slot) > _scheduled_response_timeslot) && ((int16_t)(_next_slot_number - _response_phase_start_slot) <= (_scheduled_response_timeslot + PROTOCOL_NUM_SLOTS_PER_RESPONSE)))
            send_ranging_response(_slot_start_timestamp + DW_DELAY_FROM_US(RANGING_RESPONSE_TIME_OFFSET_US));
         else if ((_config.device_role == HYBRID) && (((int16_t)(_next_slot_number - _response_phase_start_slot) <= _scheduled_response_timeslot) || _config.hybrids_perform_all_rangings))
            receive_ranging_responses(_response_order[(_next_slot_number - _response_phase_start_slot - 1) / PROTOCOL_NUM_SLOTS_PER_RESPONSE]);
         else
            dw1000_radio_disable();
         break;
      case SHARED_BUS_PHASE:
         if ((_config.scheduler_role == SCHEDULER) || (_config.scheduler_role == BACKUP_SCHEDULER))
            receive_schedule_requests(NULL);
         else
         {
            uint32_t sched_req_delay = (ranval(&_prng_state) % (PROTOCOL_SLOT_PERIOD_US - (2 * RANGING_CONTENTION_PADDING_US))) + RANGING_CONTENTION_PADDING_US;
            transmit_schedule_requests(_slot_start_timestamp + DW_DELAY_FROM_US(sched_req_delay));
         }
         break;
      case MASTER_SCHEDULE_PHASE:
         update_schedule();
         if (_config.scheduler_role == SCHEDULER)
            transmit_schedule(TRUE, _slot_start_timestamp + DW_DELAY_FROM_US(SCHEDULE_XMIT_TIME_OFFSET_US) - (uint32_t)(dw1000_get_tx_delay(0, 0) >> 8));
         else
            receive_schedule(NULL, TRUE);
         break;
      case RANGING_PHASE:
         if (_config.radio_sleep_while_passive && _config.radio_wakeup_from_host)
         {
            uint32_t sleep_time_ms = (((PROTOCOL_SCHEDULING_INTERVAL_US / PROTOCOL_SLOT_PERIOD_US) - 1 - _next_slot_number) * PROTOCOL_SLOT_PERIOD_US) / 1e3;
            host_interface_schedule_wakeup((uint8_t)(sleep_time_ms / 4));
         }
         _num_successful_ranges = perform_ranging(ids_and_ranges, _expected_valid_ranges, _num_expected_valid_ranges);
         _ranging_successful = _num_successful_ranges || (!_config.hybrids_perform_all_rangings && !_scheduled_response_timeslot);
         memcpy(scratch_ranges, ids_and_ranges, 1 + (ids_and_ranges[0] * PACKET_SINGLE_RESULT_LENGTH));
         break;
      case RESULTS_PHASE:
         if ((int16_t)(_next_slot_number - _results_phase_slot_start - 1) == _scheduled_result_timeslot)
            transmit_results(_slot_start_timestamp + DW_DELAY_FROM_US(RANGING_REQUEST_TIME_OFFSET_US) - (uint32_t)(dw1000_get_tx_delay(0, 0) >> 8));
         else
            receive_results(NULL);
         break;
      case ERROR_CORRECTION_PHASE:
         fix_network_errors();
         if (!_scheduled_response_timeslot)
            glossy_reset_clock();
         if (_config.radio_sleep_while_passive)
            dw1000_sleep(FALSE);
         ids_and_ranges[0] = scratch_ranges[0];
         memcpy(ids_and_ranges + 1, &_schedule_packet.scheduler_eui, PROTOCOL_EUI_SIZE);
         memcpy(ids_and_ranges + 1 + PROTOCOL_EUI_SIZE, scratch_ranges + 1, scratch_ranges[0] * PACKET_SINGLE_RESULT_LENGTH);
         memcpy(ids_and_ranges + 1 + PROTOCOL_EUI_SIZE + (ids_and_ranges[0] * PACKET_SINGLE_RESULT_LENGTH), &_schedule_packet.epoch_time_unix, sizeof(_schedule_packet.epoch_time_unix));
         host_interface_notify_ranges(ids_and_ranges, 1 + PROTOCOL_EUI_SIZE + (ids_and_ranges[0] * PACKET_SINGLE_RESULT_LENGTH) + sizeof(_schedule_packet.epoch_time_unix));
         break;
      case UNSCHEDULED_TIME_PHASE:      // Intentional fallthrough
      default:
         break;
   }
}

// Public functions ----------------------------------------------------------------------------------------------------

void scheduler_set_timestamp(uint32_t timestamp)
{
   // Update the Unix epoch timestamp as long as it is within a year of the time we currently think it is
   if ((timestamp > 1612810414) && (timestamp < 2000000000) && ((_schedule_packet.epoch_time_unix < 1612810414) || (abs((int32_t)(timestamp - _schedule_packet.epoch_time_unix)) <= SECONDS_PER_YEAR)))
   {
      _schedule_packet.epoch_time_unix = timestamp;
      debug_msg("INFO: Setting clock to Unix timestamp ");
      debug_msg_uint(timestamp);
      debug_msg("\n");
   }
}

bool scheduler_verify_config(app_config_t *config)
{
   return (config->device_role == _config.device_role) && (config->hybrids_perform_all_rangings == _config.hybrids_perform_all_rangings) &&
         (config->radio_sleep_while_passive == _config.radio_sleep_while_passive) && (config->radio_wakeup_from_host == _config.radio_wakeup_from_host);
}

bool scheduler_configure(app_config_t *config)
{
   // Ensure that the scheduler is currently stopped
   if (_is_running)
      scheduler_stop();
   debug_msg("INFO: Configuring scheduling module...\n");

   // Save the application settings
   _is_configured = FALSE;
   memcpy(&_config, config, sizeof(_config));

   // Initialize the schedule packet
   _schedule_packet = (schedule_packet_t){ .header = { .frameCtrl = { 0x41, 0xC8 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = SCHEDULE_PACKET, .epoch_time_unix = 0, .scheduler_eui = 0, .backup_scheduler_eui = 0,
      .request_schedule_length = 0, .hybrid_schedule_length = 0, .response_schedule_length = 0, .footer = { { 0 } } };
   _schedule_packet.epoch_time_unix = (_config.startup_timestamp > 1612810414) ? _config.startup_timestamp : 0;
   memset(_schedule_packet.eui_array, 0, sizeof(_schedule_packet.eui_array));

   // Initialize the control packet
   _control_packet = (control_packet_t){ .header = _schedule_packet.header, .message_type = CONTROL_PACKET,
      .packet_type = CONTROL_PACKET_UNDEFINED, .device_eui = 0, .footer = { { 0 } } };

   // Initialize the results packet
   _results_packet = (results_packet_t){ .header = _schedule_packet.header, .message_type = RESULTS_PACKET, .results_length = 0, .footer = { { 0 } } };
   memset(_results_packet.results, 0, sizeof(_results_packet.results));

   // Make sure the DW1000 is awake and retrieve its EUI
   if (!dw1000_wakeup() && !dw1000_force_wakeup())
   {
      debug_msg("ERROR: Unable to wake up the DW1000 radio!\n");
      module_reset();
      return FALSE;
   }
   dw1000_read_eui(_schedule_packet.header.sourceAddr);
   memcpy(_control_packet.header.sourceAddr, _schedule_packet.header.sourceAddr, EUI_LEN);
   memcpy(_results_packet.header.sourceAddr, _schedule_packet.header.sourceAddr, EUI_LEN);
   memcpy(&_config.EUI, _schedule_packet.header.sourceAddr, PROTOCOL_EUI_SIZE);
   debug_msg("INFO: Device EUI: ");
   debug_msg_eui_full(_schedule_packet.header.sourceAddr);
   debug_msg("\n");

   // Initialize the random number generator and the Glossy packet flooding protocol
   raninit(&_prng_state, dwt_readsystimestamphi32());
   glossy_init();

   // Output the specified device role and scheduling mode
   switch (_config.device_role)
   {
      case REQUESTER:
         debug_msg("INFO: Device Role: Initialized as REQUESTER\n");
         break;
      case RESPONDER:
         debug_msg("INFO: Device Role: Initialized as RESPONDER\n");
         break;
      case HYBRID:
         debug_msg("INFO: Device Role: Initialized as HYBRID\n");
         break;
      case SUPPORTER:
         debug_msg("INFO: Device Role: Initialized as SUPPORTER\n");
         break;
      default:
         debug_msg("ERROR: Unknown device role...cannot continue!\n");
         return FALSE;
   }
   switch (_config.scheduler_role)
   {
      case SCHEDULER:
         debug_msg("INFO: Scheduling Role: Initialized as SCHEDULER\n");
         break;
      case BACKUP_SCHEDULER:
         debug_msg("INFO: Scheduling Role: Initialized as BACKUP SCHEDULER\n");
         break;
      case PARTICIPANT:
         debug_msg("INFO: Scheduling Role: Initialized as PARTICIPANT\n");
         break;
      default:
         debug_msg("ERROR: Unknown scheduling role...cannot continue!\n");
         return FALSE;
   }

   // Return success if scheduler was configured correctly
   _is_configured = TRUE;
   debug_msg("INFO: Setting clock to Unix timestamp ");
   debug_msg_uint(_config.startup_timestamp);
   debug_msg("\nINFO: Scheduling module successfully configured!\n");
   return TRUE;
}

bool scheduler_start(void)
{
   // Do nothing if the scheduler is already running
   if (!_is_configured)
   {
      debug_msg("ERROR: Scheduling module has not been configured...cannot continue!\n");
      return FALSE;
   }
   else if (_is_running)
      return TRUE;
   debug_msg("INFO: Starting scheduling module...\n");

   // Initialize all static global variables
   _scheduler_phase = UNSCHEDULED_TIME_PHASE;
   _received_schedule = _received_backup_schedule = _network_synchronized = FALSE;
   _ranging_successful = _previous_ranging_successful = _results_received = FALSE;
   memset(ids_and_ranges, 0, sizeof(ids_and_ranges));
   memset(scratch_ranges, 0, sizeof(scratch_ranges));
   memset(_request_schedule, 0, sizeof(_request_schedule));
   memset(_response_schedule, 0, sizeof(_response_schedule));
   memset(_hybrid_schedule, 0, sizeof(_hybrid_schedule));
   memset(_request_order, 0, sizeof(_request_order));
   memset(_response_order, 0, sizeof(_response_order));
   memset(_requester_device_timeouts, 0, sizeof(_requester_device_timeouts));
   memset(_responder_device_timeouts, 0, sizeof(_responder_device_timeouts));
   memset(_hybrid_device_timeouts, 0, sizeof(_hybrid_device_timeouts));
   memset(_expected_valid_ranges, 0, sizeof(_expected_valid_ranges));
   _num_requesters_scheduled = _num_responders_scheduled = _num_hybrids_scheduled = 0;
   _request_phase_start_slot = _response_phase_start_slot = _shared_bus_phase_slot_start = 1;
   _schedule_xmit_phase_slot_start = _schedule_xmit_phase_slot_start_next = 2;
   _ranging_phase_slot_start = 3;
   _results_phase_slot_start = 4;
   _error_correction_phase_slot_start = 5;
   _scheduled_request_timeslot = _scheduled_response_timeslot = _scheduled_result_timeslot = -1;
   _next_slot_number = _missing_schedule_timeout = _empty_round_timeout = _failure_count = 0;
   _slot_start_timestamp = _network_search_time = _network_search_time_end = _network_search_timeout = 0;
   _num_ranging_packets_received = _num_expected_valid_ranges = _num_successful_ranges = 0;

   // Set up this device as the scheduler if requested
   if (_config.scheduler_role == SCHEDULER)
      force_role_as_scheduler();

   // Make sure the DW1000 is awake and reset its configuration
   if (!dw1000_wakeup() && !dw1000_force_wakeup())
   {
      debug_msg("ERROR: Unable to wake up the DW1000 radio!\n");
      module_reset();
      return FALSE;
   }
   if (!reset_dw1000_configuration())
      return FALSE;

   // Initialize the scheduler and ranging timers
   glossy_init();
   _scheduler_timer = timer_init(SCHEDULER_TIMER);
   if (!_scheduler_timer)
   {
      debug_msg("ERROR: Unable to initialize the scheduling timer...cannot continue!\n");
      scheduler_stop();
      return FALSE;
   }
   _ranging_timer = timer_init(RANGING_TIMER);
   if (!_ranging_timer)
   {
      debug_msg("ERROR: Unable to initialize the ranging timer...cannot continue!\n");
      scheduler_stop();
      return FALSE;
   }

   // Initialize the Request and Response phases
   initialize_request(_ranging_timer, _schedule_packet.header.sourceAddr);
   initialize_response(_schedule_packet.header.sourceAddr);

   // Start the scheduling timer
   _is_running = TRUE;
   debug_msg("INFO: Scheduling module successfully started\n");
   timer_start(_scheduler_timer, PROTOCOL_SLOT_PERIOD_US, perform_scheduled_slot_task, TRUE);
   return TRUE;
}

void scheduler_stop(void)
{
   // Disable scheduling and stop the scheduling timers
   debug_msg("INFO: Stopping scheduling module...\n");
   _is_running = FALSE;
   if (_ranging_timer)
   {
      timer_stop(_ranging_timer);
      timer_free(_ranging_timer);
      _ranging_timer = NULL;
   }
   if (_scheduler_timer)
   {
      timer_stop(_scheduler_timer);
      timer_free(_scheduler_timer);
      _scheduler_timer = NULL;
   }

   // Set the LED to BLUE to indicate that we are not currently ranging
   led_on(BLUE);

   // Put the DW1000 radio to sleep and set the scheduler role to UNKNOWN
   dw1000_sleep(TRUE);
   _is_configured = FALSE;
   _config.scheduler_role = UNKNOWN;
   debug_msg("INFO: Scheduling module stopped!\n");
}
