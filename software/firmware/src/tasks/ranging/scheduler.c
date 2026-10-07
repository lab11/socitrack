// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "bluetooth.h"
#include "computation_phase.h"
#include "deca_interface.h"
#include "battery.h"
#include "logging.h"
#include "nandlog.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "scheduler.h"
#include "status_phase.h"
#include "storage_records.h"
#include "subscription_phase.h"
#include "system.h"


// Local Scheduler Definitions -----------------------------------------------------------------------------------------

typedef struct
{
   int32_t correction_us;        // learned on top of RADIO_WAKEUP_SAFETY_DELAY_US, for every wake-up
   int32_t anchor_shift_us;      // extra head start for the next wake-up alone, when the master sent this round late
   bool settled;                 // the last round learned from came within RADIO_WAKEUP_SETTLED_US of its target lead
   uint8_t late_rounds;          // late rounds in a row the wake-ups have been aimed past, at the master's usual timing
} wake_timing_t;

#define WAKE_TIMING_INITIAL { .correction_us = RADIO_WAKEUP_CORRECTION_INITIAL_US, .anchor_shift_us = 0, .settled = false, .late_rounds = 0 }


// Static Global Variables ---------------------------------------------------------------------------------------------

static bool heard_anyone_as_master;
static TaskHandle_t notification_handle;
static am_hal_timer_config_t wakeup_timer_config;
static uint8_t failed_master_cycles, total_master_cycle_failures;
static uint8_t empty_round_timeout, eui[EUI_LEN], read_buffer[128];
static uint8_t ranging_results[MAX_COMPRESSED_RANGE_DATA_LENGTH];
static uint32_t last_round_stimer, search_started_stimer;
static uint32_t rounds_scheduled, rounds_with_ranges;
static experiment_details_t *experiment;
static wake_timing_t wake_timing = WAKE_TIMING_INITIAL;
static int32_t wake_armed_head_start_us = RADIO_WAKEUP_CORRECTION_INITIAL_US;
static bool wake_expectation_valid, wake_measure_pending;
static uint32_t wake_expected_timestamp, wake_listen_dw_hi;
static volatile schedule_role_t current_role = ROLE_IDLE;
static volatile scheduler_phase_t ranging_phase;
static volatile bool is_running;

#if DIAGNOSTIC_BUILD
static volatile bool abort_pending;
static storage_radio_abort_t pending_abort;
static uint32_t pending_abort_timestamp;

// A participant's timed wake-up, from its receiver opening until a schedule copy is decoded
static volatile struct
{
   bool pending, listened, heard, measured;
   uint8_t first_copy, rx_errors, other_frames;
   uint16_t timer_to_task_us, wake_us, first_error_us, timer_latency_us;
   int16_t carrier_offset_cppm, wake_correction_us;
   uint32_t listen_dw_hi, listen_stimer, expected_timestamp, heard_timestamp;
   uint64_t heard_reference;
} catch_state;
static storage_round_start_t round_record, finished_round_record;
static uint32_t round_record_timestamp, finished_round_timestamp, round_task_stimer, round_wake_us;
static bool round_record_pending, finished_round_pending;
static void close_round_record(void);
static volatile uint32_t timer_fired_stimer, timer_latency_ticks;
static volatile uint32_t schedules_heard_total, join_requests_sent, join_requests_heard, listen_errors;
static volatile bool collision_seen;
static volatile uint8_t collision_phase, collision_type, collision_source, isr_trigger, round_flags;
static volatile uint16_t collision_at_us;
static volatile storage_radio_timing_t radio_timing;
static uint32_t session_started_stimer, rounds_ranged, stalls, radio_timing_started_stimer, radio_timing_antenna_changes;
static uint8_t end_reason;
#endif


// Private Helper Functions --------------------------------------------------------------------------------------------

static void idle_until_next_round(void)
{
   // Nothing further happens this round and the next one is a free-running timer away
   ranging_radio_sleep(true);
   ranging_phase = UNSCHEDULED_TIME_PHASE;
#if DIAGNOSTIC_BUILD
   round_flags |= STORAGE_ROUND_START_FLAG_ABANDONED;
   close_round_record();
#endif
}

static void begin_schedule_phase(void)
{
   // Publish the phase with no window in which a radio interrupt could advance it and be overwritten
   AM_CRITICAL_BEGIN
   ranging_phase = schedule_phase_begin();
#if RADIO_INSTRUMENTATION
   ranging_radio_note_phase((uint8_t)ranging_phase);
#endif
   AM_CRITICAL_END
}

#if !defined(_TEST_RANGING_TASK) && !defined(_TEST_NO_STORAGE)
static uint32_t schedule_reference_age_ms(void)
{
   // How long ago the master's timestamp for this round was sampled
   const uint32_t elapsed = am_hal_stimer_counter_get() - schedule_phase_get_reference_stimer();
   return (elapsed < RANGING_MS_TO_STIMER(SCHEDULING_INTERVAL_US / 1000u)) ? ((elapsed * 1000u) / RANGING_STIMER_HZ) : 0u;
}
#endif

static uint32_t round_elapsed_us(void)
{
   // How far into the round this device actually is
   const uint64_t now = (uint64_t)dwt_readsystimestamphi32() << 8;
   return DWT_TO_US((now - schedule_phase_get_reference_time_full()) & 0xFFFFFFFFFFULL);
}

static inline void wake_timing_learn(wake_timing_t *state, int32_t lead_us, int32_t rounds_missed)
{
   // Nudge the next wake-up so the receiver opens RADIO_WAKEUP_TARGET_LEAD_US ahead of the round
   const int32_t error_us = (int32_t)RADIO_WAKEUP_TARGET_LEAD_US - lead_us;
   const bool far_ahead = (rounds_missed == 0) && (error_us < -(int32_t)RADIO_WAKEUP_LATE_MASTER_US);
   state->anchor_shift_us = 0;
   if (far_ahead && (state->late_rounds ? (state->late_rounds < RADIO_WAKEUP_LATE_MASTER_ROUNDS) : state->settled))
   {
      // A settled participant whose receiver suddenly opened far ahead of the schedule means the master sent this round late
      state->anchor_shift_us = (-error_us > (int32_t)RADIO_WAKEUP_LATE_MASTER_MAX_US) ? (int32_t)RADIO_WAKEUP_LATE_MASTER_MAX_US : -error_us;
      ++state->late_rounds;
      return;
   }
   if (state->late_rounds)
   {
      // Still late after RADIO_WAKEUP_LATE_MASTER_ROUNDS, so the master's rounds have moved
      state->late_rounds = 0;
      if (far_ahead)
         return;
   }
   state->settled = (error_us <= (int32_t)RADIO_WAKEUP_SETTLED_US) && (error_us >= -(int32_t)RADIO_WAKEUP_SETTLED_US);
   state->correction_us += state->settled ? (error_us / 4) : (error_us / 2);
   if (state->correction_us > RADIO_WAKEUP_CORRECTION_MAX_US)
      state->correction_us = RADIO_WAKEUP_CORRECTION_MAX_US;
   else if (state->correction_us < RADIO_WAKEUP_CORRECTION_MIN_US)
      state->correction_us = RADIO_WAKEUP_CORRECTION_MIN_US;
}

static inline int32_t wake_timing_head_start_us(wake_timing_t *state)
{
   // How long before the next round's reference instant to wake beyond RADIO_WAKEUP_SAFETY_DELAY_US
   const int32_t head_start_us = state->correction_us + state->anchor_shift_us;
   state->anchor_shift_us = 0;
   return head_start_us;
}

static bool wake_lead_us(uint64_t reference_full, uint32_t timestamp, uint32_t listen_dw_hi, uint32_t expected_timestamp, int32_t *lead_us, int32_t *rounds_missed)
{
   // How far ahead of the expected round's first schedule copy the receiver opened, from the radio clock
   const int32_t period_ms = (int32_t)(SCHEDULING_INTERVAL_US / 1000u);
   const int32_t ahead_ms = (int32_t)(timestamp - expected_timestamp);
   if (ahead_ms < -(period_ms / 2))
      return false;
   const int32_t rounds = (ahead_ms + (period_ms / 2)) / period_ms;
   int64_t lead_dw = (int64_t)((reference_full - ((uint64_t)listen_dw_hi << 8)) & 0xFFFFFFFFFFULL);
   if (lead_dw >= (int64_t)0x8000000000LL)
      lead_dw -= (int64_t)0x10000000000LL;
   const int64_t lead = ((lead_dw * 10) / 638976) - ((int64_t)rounds * (int64_t)SCHEDULING_INTERVAL_US);
   if ((lead > (int64_t)SCHEDULING_INTERVAL_US) || (lead < -(int64_t)SCHEDULING_INTERVAL_US))
      return false;
   *lead_us = (int32_t)lead;
   *rounds_missed = rounds;
   return true;
}

static void learn_wake_timing(void)
{
   // Learn from how far ahead of the round this wake-up opened the receiver
   if (!wake_measure_pending)
      return;
   wake_measure_pending = false;
   int32_t lead_us = 0, rounds_missed = 0;
   if (!wake_lead_us(schedule_phase_get_reference_time_full(), schedule_phase_get_timestamp(), wake_listen_dw_hi, wake_expected_timestamp, &lead_us, &rounds_missed))
      return;
   if (rounds_missed > 1)
      return;
   wake_timing_learn(&wake_timing, lead_us, rounds_missed);
}

static void log_pending_abort(void)
{
#if DIAGNOSTIC_BUILD && !defined(_TEST_RANGING_TASK) && !defined(_TEST_NO_STORAGE)
   if (!abort_pending)
      return;
   storage_radio_abort_t abort;
   uint32_t timestamp;
   AM_CRITICAL_BEGIN
   abort = pending_abort;
   timestamp = pending_abort_timestamp;
   abort_pending = false;
   AM_CRITICAL_END
   storage_write_radio_abort(timestamp, &abort);
#endif
}

#if DIAGNOSTIC_BUILD

static uint16_t saturate_u16(uint32_t value)
{
   return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
}

static uint32_t stimer_ticks_to_us(uint32_t ticks)
{
   return (uint32_t)(((uint64_t)ticks * 1000000u) / RANGING_STIMER_HZ);
}

static int16_t read_carrier_offset_cppm(void)
{
   // The DW3000 driver's own conversion from the carrier integrator of the frame just received
#if RADIO_XMIT_CHANNEL == 9
   const float hertz_to_ppm = (float)HERTZ_TO_PPM_MULTIPLIER_CHAN_9;
#else
   const float hertz_to_ppm = (float)HERTZ_TO_PPM_MULTIPLIER_CHAN_5;
#endif
   const float cppm = 100.0f * (float)dwt_readcarrierintegrator() * (float)FREQ_OFFSET_MULTIPLIER * hertz_to_ppm;
   return (cppm >= (float)INT16_MAX) ? INT16_MAX : ((cppm <= (float)INT16_MIN) ? INT16_MIN : (int16_t)cppm);
}

static void reset_radio_timing(void)
{
   AM_CRITICAL_BEGIN
   memset((void*)&radio_timing, 0, sizeof(radio_timing));
   radio_timing.slack_min_us = INT16_MAX;
   radio_timing.event_to_isr_min_us = UINT16_MAX;
   AM_CRITICAL_END
}

static void flush_radio_timing(bool session_over)
{
   // Once a minute and whatever is left when the session ends
   const uint32_t now = am_hal_stimer_counter_get();
   if (!session_over && ((now - radio_timing_started_stimer) < RANGING_MS_TO_STIMER(STORAGE_RADIO_TIMING_INTERVAL_MS)))
      return;
   storage_radio_timing_t record;
   AM_CRITICAL_BEGIN
   record = *(const storage_radio_timing_t*)&radio_timing;
   AM_CRITICAL_END
   reset_radio_timing();
   radio_timing_started_stimer = now;
   ranging_radio_stats_t radio;
   ranging_radio_get_stats(&radio);
   const uint32_t changes = radio.antenna_changes - radio_timing_antenna_changes;
   radio_timing_antenna_changes = radio.antenna_changes;
   record.antenna = radio.antenna;
   record.antenna_changes = (changes > UINT8_MAX) ? UINT8_MAX : (uint8_t)changes;
   if (record.arms)
      storage_write_radio_timing(session_over ? app_get_experiment_time(app_get_time_offset()) : schedule_phase_get_timestamp(), &record);
}

static void trace_session_begin(void)
{
   // Every counter describes one run of the scheduler
   catch_state.pending = catch_state.heard = catch_state.measured = false;
   round_record_pending = finished_round_pending = collision_seen = false;
   schedules_heard_total = join_requests_sent = join_requests_heard = listen_errors = 0;
   rounds_ranged = stalls = 0;
   end_reason = STORAGE_SESSION_END_UNKNOWN;
   session_started_stimer = radio_timing_started_stimer = am_hal_stimer_counter_get();
   reset_radio_timing();
}

static void retire_round_record(void)
{
   // The master's record for the round just finished
   if (!round_record_pending)
      return;
   round_record_pending = false;
   round_record.flags = round_flags;
   finished_round_record = round_record;
   finished_round_timestamp = round_record_timestamp;
   finished_round_pending = true;
}

static void flush_round_record(void)
{
   // Hand a retired record to storage
   if (!finished_round_pending)
      return;
   finished_round_pending = false;
   storage_write_round_start(finished_round_timestamp, &finished_round_record);
}

static void close_round_record(void)
{
   // Write the master's record as soon as its round is over, stamped with that round
   retire_round_record();
   flush_round_record();
}

static void flush_schedule_catch(bool network_lost)
{
   // A timed wake-up is written out once a copy has been decoded, or when the network is gone without one
   if (!catch_state.pending || (!network_lost && !(catch_state.heard && catch_state.measured)))
      return;
   storage_schedule_catch_t record = { .first_copy = STORAGE_SCHEDULE_CATCH_NONE, .rounds_missed = 0, .lead_us = 0,
      .timer_to_task_us = catch_state.timer_to_task_us, .wake_us = catch_state.wake_us, .rx_errors = catch_state.rx_errors,
      .other_frames = catch_state.other_frames, .first_error_us = catch_state.first_error_us, .carrier_offset_cppm = 0,
      .wake_correction_us = catch_state.wake_correction_us, .timer_latency_us = catch_state.timer_latency_us };
   uint32_t timestamp = catch_state.expected_timestamp;
   if (catch_state.heard && catch_state.listened)
   {
      // Measure against the round this wake-up was aimed at
      int32_t lead_us = 0, rounds = 0;
      if (wake_lead_us(catch_state.heard_reference, catch_state.heard_timestamp, catch_state.listen_dw_hi, catch_state.expected_timestamp, &lead_us, &rounds))
      {
         record.rounds_missed = (rounds > 254) ? 254 : (uint8_t)rounds;
         record.lead_us = lead_us;
      }
      else
         record.rounds_missed = STORAGE_SCHEDULE_CATCH_UNKNOWN;
      record.first_copy = catch_state.first_copy;
      record.carrier_offset_cppm = catch_state.carrier_offset_cppm;
      timestamp = catch_state.heard_timestamp;
   }
   else
   {
      // Rounds that went by while still listening
      const uint32_t listened_ms = stimer_ticks_to_us(am_hal_stimer_counter_get() - catch_state.listen_stimer) / 1000u;
      const uint32_t rounds = listened_ms / (SCHEDULING_INTERVAL_US / 1000u);
      record.rounds_missed = (rounds > 254) ? 254 : (uint8_t)rounds;
      timestamp = app_get_experiment_time(app_get_time_offset());
   }
   catch_state.pending = false;
   storage_write_schedule_catch(timestamp, &record);
}

static void trace_round_prepare(uint32_t task_stimer)
{
   // Called between waking the radio and opening the round, so a participant's catch is armed before its receiver opens
   const uint32_t wake = ranging_radio_last_wake_us();
   round_task_stimer = task_stimer;
   round_wake_us = (wake == UINT32_MAX) ? STORAGE_SCHEDULE_CATCH_UNMEASURED : saturate_u16(wake);
   if (current_role == ROLE_MASTER)
   {
      // Cleared before the first copy goes out, since its completion interrupt may set a flag
      retire_round_record();
      round_flags = 0;
   }
   else if (!catch_state.pending && wake_expectation_valid && wake)
   {
      // Only a wake-up from sleep is a timed attempt: one landing on a radio still listening carries on the earlier one
      catch_state.listened = catch_state.heard = catch_state.measured = false;
      catch_state.first_copy = STORAGE_SCHEDULE_CATCH_NONE;
      catch_state.rx_errors = catch_state.other_frames = 0;
      catch_state.first_error_us = STORAGE_SCHEDULE_CATCH_UNMEASURED;
      catch_state.timer_to_task_us = saturate_u16(stimer_ticks_to_us(task_stimer - timer_fired_stimer));
      catch_state.wake_us = round_wake_us;
      catch_state.expected_timestamp = wake_expected_timestamp;
      catch_state.wake_correction_us = (int16_t)((wake_armed_head_start_us > INT16_MAX) ? INT16_MAX : ((wake_armed_head_start_us < INT16_MIN) ? INT16_MIN : wake_armed_head_start_us));
      catch_state.timer_latency_us = saturate_u16(stimer_ticks_to_us(timer_latency_ticks));
      catch_state.listen_stimer = am_hal_stimer_counter_get();
      catch_state.pending = true;
   }
}

static void trace_round_begin(void)
{
   // The receiver or the first schedule copy has just been started
   if (current_role == ROLE_MASTER)
   {
      flush_round_record();
      const uint32_t fired = timer_fired_stimer;
      round_record = (storage_round_start_t){ .timer_to_task_us = saturate_u16(stimer_ticks_to_us(round_task_stimer - fired)),
         .wake_us = round_wake_us, .timer_to_transmit_us = saturate_u16(stimer_ticks_to_us(am_hal_stimer_counter_get() - fired)),
         .flags = 0, .schedule_size = (uint8_t)schedule_phase_get_num_devices(), .devices_ranged = 0,
         .timer_latency_us = saturate_u16(stimer_ticks_to_us(timer_latency_ticks)) };
      round_record_timestamp = schedule_phase_get_timestamp();
      round_record_pending = true;
   }
   else if (catch_state.pending && !catch_state.listened)
   {
      // The same instant the wake-up correction measures from when it is measuring
      catch_state.listen_dw_hi = wake_measure_pending ? wake_listen_dw_hi : dwt_readsystimestamphi32();
      catch_state.listened = true;
   }
}

static void trace_rx(scheduler_phase_t phase_before, uint32_t heard_before)
{
   // Runs in the radio interrupt after the protocol has handled a decoded frame
   if ((phase_before == SCHEDULE_PHASE) && (current_role != ROLE_MASTER) && (schedules_heard_total == heard_before) &&
       catch_state.pending && !catch_state.heard && (catch_state.other_frames < UINT8_MAX))
      ++catch_state.other_frames;
   if (catch_state.pending && catch_state.heard && !catch_state.measured)
   {
      // Read straight after the copy, before another frame replaces the radio's receive diagnostics
      catch_state.carrier_offset_cppm = read_carrier_offset_cppm();
      catch_state.measured = true;
   }
   if ((ranging_phase == MESSAGE_COLLISION) && !collision_seen)
   {
      collision_phase = (uint8_t)phase_before;
      collision_type = ((ieee154_header_t*)read_buffer)->msgType;
      collision_source = read_buffer[sizeof(ieee154_header_t)];
      collision_at_us = saturate_u16(round_elapsed_us());
      collision_seen = true;
   }
}

static void trace_listen_error(void)
{
   // A frame heard while listening for a schedule that could not be decoded
   if (listen_errors < UINT32_MAX)
      ++listen_errors;
   if (catch_state.pending && !catch_state.heard)
   {
      if (catch_state.rx_errors < UINT8_MAX)
         ++catch_state.rx_errors;
      if (catch_state.first_error_us == STORAGE_SCHEDULE_CATCH_UNMEASURED)
         catch_state.first_error_us = saturate_u16(stimer_ticks_to_us(am_hal_stimer_counter_get() - catch_state.listen_stimer));
   }
}

static void trace_session_end(void)
{
   // Close out anything still open, then say why this run ended
   flush_schedule_catch(true);
   retire_round_record();
   flush_round_record();
   flush_radio_timing(true);
   const storage_session_end_t record = { .reason = end_reason, .role = (uint8_t)current_role,
      .schedule_size = (uint8_t)schedule_phase_get_num_devices(),
      .collision_phase = collision_seen ? collision_phase : 0, .collision_type = collision_seen ? collision_type : 0,
      .collision_source = collision_seen ? collision_source : 0, .collision_at_us = collision_seen ? collision_at_us : 0,
      .session_ms = stimer_ticks_to_us(am_hal_stimer_counter_get() - session_started_stimer) / 1000u,
      .rounds_ranged = saturate_u16(rounds_ranged), .schedules_heard = saturate_u16(schedules_heard_total),
      .join_requests_sent = saturate_u16(join_requests_sent), .join_requests_heard = saturate_u16(join_requests_heard),
      .listen_errors = saturate_u16(listen_errors), .stalls = (stalls > UINT8_MAX) ? UINT8_MAX : (uint8_t)stalls };
   storage_write_session_end(app_get_experiment_time(app_get_time_offset()), &record);
}

#endif  // #if DIAGNOSTIC_BUILD

static void arm_wakeup_timer(uint32_t elapsed_us)
{
   // Wake RADIO_WAKEUP_SAFETY_DELAY_US before the next round's reference instant
   wake_armed_head_start_us = wake_timing_head_start_us(&wake_timing);
   const uint32_t target_us = SCHEDULING_INTERVAL_US - (uint32_t)((int32_t)RADIO_WAKEUP_SAFETY_DELAY_US + wake_armed_head_start_us);
   const uint32_t remaining_us = (elapsed_us < target_us) ? (target_us - elapsed_us) : RADIO_WAKEUP_SAFETY_DELAY_US;
   wakeup_timer_config.ui32Compare0 = (uint32_t)(((uint64_t)RADIO_WAKEUP_TIMER_TICK_RATE_HZ * remaining_us) / 1000000u);
   am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
   am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);
}

static void fix_network_errors(uint8_t num_ranging_results)
{
   // Presence is the union of what every device reachable from here reported
   const uint16_t present = status_phase_get_present_slots();
   const uint32_t num_scheduled = schedule_phase_get_num_devices();
   uint32_t num_devices = 0;
   for (uint32_t slot = 1; slot < num_scheduled; ++slot)
      if (present & (1u << slot))
      {
         schedule_phase_update_device_presence(schedule_phase_get_addr_from_slot((uint8_t)slot));
         ++num_devices;
      }
   schedule_phase_handle_device_timeouts();

   // Nominate the closest device heard this round to transmit last in the next one
   uint8_t nearest_slot = 0;
   int16_t nearest_mm = INT16_MAX;
   for (uint8_t i = 0; i < ranging_results[0]; ++i)
   {
      const uint32_t offset = 1 + ((uint32_t)i * COMPRESSED_RANGE_DATUM_LENGTH);
      int16_t range_mm = 0;
      memcpy(&range_mm, &ranging_results[offset + 1], sizeof(range_mm));
      const uint8_t slot = schedule_phase_get_slot_from_addr(ranging_results[offset]);
      if ((slot != UNSCHEDULED_SLOT) && slot && (range_mm < nearest_mm))
      {
         nearest_mm = range_mm;
         nearest_slot = slot;
      }
   }
   schedule_phase_set_master_nearest(nearest_slot);

   // A request from a device the master cannot hear arrives by way of one that can
   const uint8_t pending_subscriber = status_phase_get_pending_subscriber();
   if (pending_subscriber)
   {
      schedule_phase_add_device(pending_subscriber);
#if DIAGNOSTIC_BUILD
      ++join_requests_heard;
      round_flags |= STORAGE_ROUND_START_FLAG_JOIN_RELAYED;
#endif
   }

   // Check if we are still synchronized with the network
   if (num_devices || num_ranging_results)
      heard_anyone_as_master = true;
   empty_round_timeout = (!num_devices && !num_ranging_results) ? (empty_round_timeout + 1) : 0;
   if (empty_round_timeout >= MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE)
   {
      // A master run that ends having heard nothing from anyone at all is a network nobody could reach
      if (!heard_anyone_as_master && (total_master_cycle_failures < UINT8_MAX))
         ++total_master_cycle_failures;
      failed_master_cycles = heard_anyone_as_master ? 0 :
            ((failed_master_cycles < UINT8_MAX) ? (failed_master_cycles + 1) : failed_master_cycles);
      if (failed_master_cycles == MASTER_INELIGIBLE_AFTER_CYCLES)
         print("WARNING: Formed a network as MASTER %u times without hearing any device...no longer offering to be elected\n", (uint32_t)failed_master_cycles);
      print("WARNING: No network traffic received\n");
#if DIAGNOSTIC_BUILD
      end_reason = STORAGE_SESSION_END_SILENT;
#endif
#ifndef _TEST_RANGING_TASK
      is_running = false;
#else
      empty_round_timeout = 0;
#endif
   }
}

static void handle_range_computation_phase(void)
{
   // Read the radio clock before the radio goes away then arm the next wake-up from it
   const uint32_t elapsed_us = round_elapsed_us();
   learn_wake_timing();
   ranging_phase_commit_receive_counts(status_phase_get_present_slots());
   ranging_radio_reconsider_antenna();
   ranging_radio_sleep(true);
   if (current_role != ROLE_MASTER)
   {
      arm_wakeup_timer(elapsed_us);
      wake_expected_timestamp = schedule_phase_get_timestamp() + (SCHEDULING_INTERVAL_US / 1000u);
      wake_expectation_valid = true;
   }
#if DIAGNOSTIC_BUILD
   if (ranging_phase_was_scheduled())
      ++rounds_ranged;
#endif

   // Hearing any device at all, in any role, proves this device's UWB path works in both directions
   if (ranging_phase_get_heard_slots())
      failed_master_cycles = 0;

   // Continue based on the current role
   switch (ranging_phase_was_scheduled() ? current_role : ROLE_IDLE)
   {
      case ROLE_MASTER:
      {
         // Carry out the ranging algorithm and fix any detected network errors
         compute_ranges(ranging_results);
#if DIAGNOSTIC_BUILD
         round_record.devices_ranged = ranging_results[0];
         round_flags |= STORAGE_ROUND_START_FLAG_COMPUTED;
#endif
         fix_network_errors(ranging_results[0]);
         const uint32_t data_timestamp = schedule_phase_get_timestamp();
         bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
#ifndef _TEST_NO_STORAGE
         if (ranging_results[0])
            storage_write_ranging_data(data_timestamp, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH), app_get_time_offset());
#endif
#endif
         print_ranges(app_experiment_time_to_rtc_time(data_timestamp), data_timestamp % 1000, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#if DIAGNOSTIC_BUILD
         close_round_record();
#endif
         break;
      }
      case ROLE_PARTICIPANT:
      {
         // Carry out the ranging algorithm and fix any detected network errors
         compute_ranges(ranging_results);
         const uint32_t data_timestamp = schedule_phase_get_timestamp();
         bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
#ifndef _TEST_NO_STORAGE
         if (ranging_results[0])
         {
            // Wind back the local clock to match the master's timestamp for this round
            const int64_t local_at_reference = (int64_t)app_get_experiment_time(0) - (int64_t)schedule_reference_age_ms();
            storage_write_ranging_data(data_timestamp, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH), (int32_t)((int64_t)data_timestamp - local_at_reference));
         }
#endif
#endif
         print_ranges(app_experiment_time_to_rtc_time(data_timestamp), data_timestamp % 1000, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
         break;
      }
      default:
         break;
   }
   if (ranging_phase_was_scheduled())
   {
      ++rounds_scheduled;
      if (ranging_results[0])
         ++rounds_with_ranges;
   }
   ranging_radio_note_network_size((uint8_t)schedule_phase_get_num_devices());
   ranging_phase = UNSCHEDULED_TIME_PHASE;
   last_round_stimer = am_hal_stimer_counter_get();
}


// Interrupt Service Routines and Callbacks ----------------------------------------------------------------------------

void am_timer02_isr(void)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
#if DIAGNOSTIC_BUILD
   timer_fired_stimer = am_hal_stimer_counter_get();
   const uint32_t count = am_hal_timer_read(RADIO_WAKEUP_TIMER_NUMBER), compare = wakeup_timer_config.ui32Compare0;
   timer_latency_ticks = (count >= compare) ? (count - compare) : count;
#endif
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   xTaskNotifyFromISR(notification_handle, RANGING_NEW_ROUND_START, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void tx_callback(const dwt_cb_data_t *txData)
{
   // Allow the scheduling protocol to handle the interrupt
#if DIAGNOSTIC_BUILD
   isr_trigger = STORAGE_RADIO_ABORT_TRIGGER_TX_DONE;
#endif
   ranging_phase = schedule_phase_tx_complete();
#if RADIO_INSTRUMENTATION
   ranging_radio_note_phase((uint8_t)ranging_phase);
#endif

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RADIO_ERROR) || (ranging_phase == RANGE_COMPUTATION_PHASE))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, RANGING_TX_COMPLETE, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

static void rx_callback(const dwt_cb_data_t *rxData)
{
   // Read the received data packet and allow the scheduling protocol to handle it
   dwt_readrxdata(read_buffer, rxData->datalength, 0);
#if DIAGNOSTIC_BUILD
   isr_trigger = STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME;
   const scheduler_phase_t phase_before = ranging_phase;
   const uint32_t heard_before = schedules_heard_total;
#endif
   ranging_phase = schedule_phase_rx_complete((schedule_packet_t*)read_buffer);
#if DIAGNOSTIC_BUILD
   trace_rx(phase_before, heard_before);
#endif
#if RADIO_INSTRUMENTATION
   ranging_radio_note_phase((uint8_t)ranging_phase);
#endif

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RANGE_COMPUTATION_PHASE) || (ranging_phase == MESSAGE_COLLISION) || (ranging_phase == RADIO_ERROR))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, RANGING_RX_COMPLETE, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

static void handle_rx_failure(uint32_t notification_reason)
{
   // Allow the scheduling protocol to handle the interrupt
#if DIAGNOSTIC_BUILD
   isr_trigger = (notification_reason == RANGING_RX_ERROR) ? STORAGE_RADIO_ABORT_TRIGGER_RX_ERROR : STORAGE_RADIO_ABORT_TRIGGER_RX_TIMEOUT;
   if ((notification_reason == RANGING_RX_ERROR) && (ranging_phase == SCHEDULE_PHASE) && (current_role != ROLE_MASTER))
      trace_listen_error();
#endif
   ranging_phase = schedule_phase_rx_error();
#if RADIO_INSTRUMENTATION
   ranging_radio_note_phase((uint8_t)ranging_phase);
#endif

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RANGING_ERROR) || (ranging_phase == RADIO_ERROR) || (ranging_phase == RANGE_COMPUTATION_PHASE))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, notification_reason, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

static void rx_timeout_callback(const dwt_cb_data_t *rxData)
{
   // A listening window that expired with nothing in it
   handle_rx_failure(RANGING_RX_TIMEOUT);
}

static void rx_error_callback(const dwt_cb_data_t *rxData)
{
   // A frame that arrived and could not be decoded
   handle_rx_failure(RANGING_RX_ERROR);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void scheduler_init(experiment_details_t *details)
{
   // Store the device EUI and experiment details
   experiment = details;
   if (details)
   {
      schedule_phase_store_experiment_details(details);
      system_read_UID(eui, sizeof(eui));

      // Set the DW3000 callback configuration
      ranging_radio_register_callbacks(tx_callback, rx_callback, rx_timeout_callback, rx_error_callback);
   }
   is_running = false;
}

schedule_role_t scheduler_get_current_role(void)
{
   return current_role;
}

void scheduler_reload_experiment_details(void)
{
   // For a radio test whose device list arrived after the scheduler was initialized
   if (experiment)
      schedule_phase_store_experiment_details(experiment);
}

const experiment_details_t* scheduler_get_experiment_details(void)
{
   static const experiment_details_t none = { 0 };
   return experiment ? experiment : &none;
}

void scheduler_get_round_counts(uint32_t *scheduled, uint32_t *with_ranges)
{
   // Rounds this device took part in since boot and those that produced at least one range
   *scheduled = rounds_scheduled;
   *with_ranges = rounds_with_ranges;
}

void scheduler_run(schedule_role_t role)
{
   // Ensure that the role is a valid ranging role
   if ((role != ROLE_MASTER) && (role != ROLE_PARTICIPANT))
      return;

   // Wake up the DW3000 ranging radio and set it to the correct channel
   ranging_radio_wakeup();
   ranging_radio_choose_channel(RADIO_XMIT_CHANNEL);

   // Initialize all static ranging variables
   notification_handle = xTaskGetCurrentTaskHandle();
   memset(ranging_results, 0, sizeof(ranging_results));
   last_round_stimer = search_started_stimer = am_hal_stimer_counter_get();
   empty_round_timeout = 0;
   heard_anyone_as_master = (role != ROLE_MASTER);
   ranging_phase = UNSCHEDULED_TIME_PHASE;
   wake_expectation_valid = wake_measure_pending = false;
#if DIAGNOSTIC_BUILD
   trace_session_begin();
#endif

   // Initialize the Schedule, Ranging, Status, and Subscription phases
   schedule_phase_initialize(eui, role == ROLE_MASTER);
   ranging_phase_initialize(eui);
   status_phase_initialize(eui);
   subscription_phase_initialize(eui);

   // Initialize the wakeup timer based on the device role
   is_running = true;
   if (role == ROLE_MASTER)
   {
      // Initialize the scheduler timer
      current_role = ROLE_MASTER;
      am_hal_timer_default_config_set(&wakeup_timer_config);
      wakeup_timer_config.eFunction = AM_HAL_TIMER_FN_UPCOUNT;
      wakeup_timer_config.eInputClock = AM_HAL_TIMER_CLOCK_XT;
      wakeup_timer_config.ui32Compare0 = (uint32_t)(((uint64_t)RADIO_WAKEUP_TIMER_TICK_RATE_HZ * SCHEDULING_INTERVAL_US) / 1000000u);
      am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
      am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
      NVIC_SetPriority(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
      NVIC_EnableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
      am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);
      ranging_radio_sleep(true);
   }
   else
   {
      // Initialize the radio wakeup timer
      current_role = ROLE_IDLE;
      am_hal_timer_default_config_set(&wakeup_timer_config);
      wakeup_timer_config.eFunction = AM_HAL_TIMER_FN_UPCOUNT;
      wakeup_timer_config.eInputClock = AM_HAL_TIMER_CLOCK_XT;
      am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
      NVIC_SetPriority(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
      NVIC_EnableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
      print("INFO: Searching for an existing network\n");
      begin_schedule_phase();
   }

   // Notify the application that network connectivity has been established
   app_notify(APP_NOTIFY_NETWORK_CONNECTED);

   // Loop forever waiting for actions to wake us up
   uint32_t pending_actions = 0;
   const TickType_t wait_ticks = pdMS_TO_TICKS(RANGING_ROUND_STALL_TIMEOUT_MS);
   while (is_running)
   {
      system_watchdog_pet(WATCHDOG_TASK_RANGING);
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &pending_actions, wait_ticks) == pdTRUE)
      {
         // Handle any pending actions
         log_pending_abort();
#if DIAGNOSTIC_BUILD
         flush_schedule_catch(false);
         flush_radio_timing(false);
#endif
         if ((pending_actions & RANGING_NEW_ROUND_START))
         {
            // Wake up the radio and wait until all schedule updating tasks have completed
#if DIAGNOSTIC_BUILD
            const uint32_t task_stimer = am_hal_stimer_counter_get();
#endif
            ranging_radio_wakeup();
            const bool woke_from_sleep = (ranging_radio_last_wake_us() != 0);
#if DIAGNOSTIC_BUILD
            trace_round_prepare(task_stimer);
#endif
            begin_schedule_phase();
            if ((current_role != ROLE_MASTER) && woke_from_sleep && wake_expectation_valid)
            {
               // Note when the receiver opened to learn from once the round is caught
               wake_listen_dw_hi = dwt_readsystimestamphi32();
               wake_measure_pending = true;
            }
#if DIAGNOSTIC_BUILD
            trace_round_begin();
#endif
         }
         else if ((pending_actions & RANGING_STOP))
            continue;

         // Carry out logic based on the current reported phase of the ranging protocol
         switch (ranging_phase)
         {
            case RANGE_COMPUTATION_PHASE:
               search_started_stimer = am_hal_stimer_counter_get();
               if (ranging_phase_was_scheduled() && (current_role == ROLE_IDLE))
               {
                  // Notify the application that our network role has changed
                  current_role = ROLE_PARTICIPANT;
                  app_notify(APP_NOTIFY_VERIFY_CONFIGURATION);
               }
               handle_range_computation_phase();
               break;
            case RADIO_ERROR:
               // A round cut short has no presence reports, so only senders this device heard itself count
               ranging_phase_commit_receive_counts(0);
               ranging_radio_reconsider_antenna();
               if (current_role == ROLE_MASTER)
                  idle_until_next_round();
               else
               {
                  // The radio may have been reset along the way, taking the clock a pending wake-up measurement relies on
                  wake_measure_pending = false;
                  begin_schedule_phase();
               }
               break;
            case RANGING_ERROR:
               // A participant that caught this round's schedule still learns from its wake-up before listening for the next round
               if (current_role != ROLE_MASTER)
                  learn_wake_timing();
               ranging_phase_commit_receive_counts(0);
               ranging_radio_reconsider_antenna();
               if (current_role == ROLE_MASTER)
                  idle_until_next_round();
               else if ((am_hal_stimer_counter_get() - search_started_stimer) >= NETWORK_SEARCH_TIMEOUT_STIMER)
               {
                     // Stop the ranging task if no network was detected after a period of time
                     print("WARNING: Timed out searching for an existing network\n");
#if DIAGNOSTIC_BUILD
                     end_reason = STORAGE_SESSION_END_SEARCH_TIMEOUT;
#endif
#ifndef _TEST_RANGING_TASK
                     is_running = false;
#else
                     begin_schedule_phase();
                     search_started_stimer = am_hal_stimer_counter_get();
#endif
               }
               else
                  begin_schedule_phase();
               break;
            case MESSAGE_COLLISION:
               print("WARNING: Stopping ranging due to possible network collision\n");
#if DIAGNOSTIC_BUILD
               end_reason = STORAGE_SESSION_END_COLLISION;
#endif
#ifndef _TEST_RANGING_TASK
               is_running = false;
#else
               begin_schedule_phase();
               search_started_stimer = am_hal_stimer_counter_get();
#endif
               break;
            default:
               break;
         }
      }
      else if ((current_role != ROLE_MASTER) && ((am_hal_stimer_counter_get() - last_round_stimer) > RANGING_ROUND_STALL_STIMER))
      {
         // A participant's rounds are restarted only by its own wake-up timer
         print("WARNING: No ranging round completed in %u ms...restarting the Schedule Phase\n", (uint32_t)RANGING_ROUND_STALL_TIMEOUT_MS);
#if DIAGNOSTIC_BUILD
         ++stalls;
#endif
         last_round_stimer = am_hal_stimer_counter_get();
         ranging_radio_wakeup();
         begin_schedule_phase();
      }
   }

   // Disable all ranging timers and interrupts
   const am_hal_rtc_time_t scheduler_interval = {
      .ui32ReadError = 0, .ui32Weekday = 0, .ui32CenturyBit = RTC_CTRUP_CB_2000, .ui32Year = 0,
      .ui32Month = 0, .ui32DayOfMonth = 0, .ui32Hour = 0, .ui32Minute = 0, .ui32Second = 0, .ui32Hundredths = 0 };
   am_hal_rtc_alarm_set((am_hal_rtc_time_t*)&scheduler_interval, AM_HAL_RTC_ALM_RPT_DIS);
   am_hal_timer_interrupt_disable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   am_hal_rtc_interrupt_disable(AM_HAL_RTC_INT_ALM);
   NVIC_DisableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
   NVIC_DisableIRQ(RTC_IRQn);

   // Put the DW3000 radio into deep sleep mode
   ranging_radio_sleep(true);
#if DIAGNOSTIC_BUILD
   trace_session_end();
#endif

   // Notify the application that network connectivity has been lost
   current_role = ROLE_IDLE;
   app_notify(APP_NOTIFY_NETWORK_LOST);
}

void scheduler_note_rx_arm_failure(scheduler_phase_t phase, uint32_t slot, uint32_t schedule_size, uint32_t deadline_us)
{
#if DIAGNOSTIC_BUILD
   // Runs in the radio interrupt the moment a delayed receive is refused, so everything here is as of that instant
   if (abort_pending)
      return;
   const uint32_t now_us = round_elapsed_us();
   const int32_t late_us = (int32_t)now_us - (int32_t)deadline_us;
   uint32_t isr_us = 0, events = 0;
   const bool in_isr = (__get_IPSR() != 0);
   const bool timed = ranging_radio_isr_progress(&isr_us, &events) && in_isr && (isr_us <= now_us);
   const uint32_t since_ms = battery_monitor_ms_since_temperature_sample();

   // When this interrupt started and how long after the event it was handling
   const uint8_t trigger = in_isr ? isr_trigger : STORAGE_RADIO_ABORT_TRIGGER_UNKNOWN;
   uint32_t asleep_us = 0, wake_to_isr_us = 0;
   const bool woke = in_isr && ranging_radio_isr_woke_processor(&asleep_us, &wake_to_isr_us);
   const uint32_t entry_us = now_us - isr_us;
   int32_t event_to_isr_us = STORAGE_RADIO_ABORT_NO_EVENT_TIME;
   if (timed && ((trigger == STORAGE_RADIO_ABORT_TRIGGER_TX_DONE) || (trigger == STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME)))
   {
      const uint64_t event = (trigger == STORAGE_RADIO_ABORT_TRIGGER_TX_DONE) ? ranging_radio_readtxtimestamp() : ranging_radio_readrxtimestamp();
      const int32_t event_us = (int32_t)DWT_TO_US((event - schedule_phase_get_reference_time_full()) & 0xFFFFFFFFFFULL);
      event_to_isr_us = (int32_t)entry_us - event_us;
      event_to_isr_us = (event_to_isr_us > INT16_MAX) ? INT16_MAX : ((event_to_isr_us <= INT16_MIN) ? (INT16_MIN + 1) : event_to_isr_us);
   }
   pending_abort = (storage_radio_abort_t){
      .phase = (phase == RANGING_PHASE) ? STORAGE_RADIO_ABORT_PHASE_RANGING : STORAGE_RADIO_ABORT_PHASE_STATUS,
      .slot = (uint8_t)((slot > UINT8_MAX) ? UINT8_MAX : slot),
      .schedule_size = (uint8_t)schedule_size,
      .late_us = (int16_t)((late_us > INT16_MAX) ? INT16_MAX : ((late_us < INT16_MIN) ? INT16_MIN : late_us)),
      .isr_elapsed_us = (uint16_t)(!timed ? STORAGE_RADIO_ABORT_UNMEASURED : ((isr_us >= STORAGE_RADIO_ABORT_UNMEASURED) ? (STORAGE_RADIO_ABORT_UNMEASURED - 1) : isr_us)),
      .isr_events = (uint8_t)((events > UINT8_MAX) ? UINT8_MAX : events),
      .since_temperature_ms = (uint16_t)((since_ms >= STORAGE_RADIO_ABORT_UNMEASURED) ? STORAGE_RADIO_ABORT_UNMEASURED : since_ms),
      .trigger = trigger,
      .isr_entry_us = (uint16_t)(!timed ? STORAGE_RADIO_ABORT_UNMEASURED : ((entry_us >= STORAGE_RADIO_ABORT_UNMEASURED) ? (STORAGE_RADIO_ABORT_UNMEASURED - 1) : entry_us)),
      .event_to_isr_us = (int16_t)event_to_isr_us,
      .asleep_us = woke ? ((asleep_us >= STORAGE_RADIO_ABORT_UNMEASURED) ? (STORAGE_RADIO_ABORT_UNMEASURED - 1) : (uint16_t)asleep_us) : STORAGE_RADIO_ABORT_UNMEASURED,
      .wake_to_isr_us = woke ? ((wake_to_isr_us >= STORAGE_RADIO_ABORT_UNMEASURED) ? (STORAGE_RADIO_ABORT_UNMEASURED - 1) : (uint16_t)wake_to_isr_us) : STORAGE_RADIO_ABORT_UNMEASURED
   };
   pending_abort_timestamp = schedule_phase_get_timestamp();
   abort_pending = true;
#endif
}

#if DIAGNOSTIC_BUILD

void scheduler_note_rx_armed(uint32_t deadline_us)
{
   // Runs in the radio interrupt straight after a delayed receive was armed in time. Only a receive armed for the first
   // event of an interrupt, a received frame, is counted, since only then did the interrupt start because of that frame.
   uint32_t isr_us = 0, events = 0;
   if ((__get_IPSR() == 0) || (isr_trigger != STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME) || !ranging_radio_isr_progress(&isr_us, &events) || (events != 1))
      return;
   const uint32_t now_us = round_elapsed_us();
   const uint64_t event = ranging_radio_readrxtimestamp();
   if (isr_us > now_us)
      return;
   const int32_t event_us = (int32_t)DWT_TO_US((event - schedule_phase_get_reference_time_full()) & 0xFFFFFFFFFFULL);
   const int32_t event_to_isr_us = (int32_t)(now_us - isr_us) - event_us;
   const int32_t slack_us = (int32_t)deadline_us - (int32_t)now_us;
   uint32_t asleep_us = 0, wake_to_isr_us = 0;
   const bool woke = ranging_radio_isr_woke_processor(&asleep_us, &wake_to_isr_us);

   if (radio_timing.arms == UINT16_MAX)
      return;
   ++radio_timing.arms;
   if (woke)
   {
      ++radio_timing.after_sleep;
      if (!asleep_us)
         ++radio_timing.during_sleep_entry;
      if (wake_to_isr_us > radio_timing.wake_to_isr_max_us)
         radio_timing.wake_to_isr_max_us = saturate_u16(wake_to_isr_us);
   }
   if (slack_us < radio_timing.slack_min_us)
      radio_timing.slack_min_us = (int16_t)((slack_us < INT16_MIN) ? INT16_MIN : slack_us);
   if (slack_us < 25)
      ++radio_timing.slack_under_25_us;
   const uint16_t e2i = (event_to_isr_us <= 0) ? 0 : saturate_u16((uint32_t)event_to_isr_us);
   if (e2i < radio_timing.event_to_isr_min_us)
      radio_timing.event_to_isr_min_us = e2i;
   if (e2i > radio_timing.event_to_isr_max_us)
      radio_timing.event_to_isr_max_us = e2i;
   const uint32_t band = (e2i < STORAGE_RADIO_TIMING_FIRST_US) ? 0 : (1 + ((e2i - STORAGE_RADIO_TIMING_FIRST_US) / STORAGE_RADIO_TIMING_STEP_US));
   ++radio_timing.event_to_isr_counts[(band < STORAGE_RADIO_TIMING_BANDS) ? band : (STORAGE_RADIO_TIMING_BANDS - 1)];
}

#endif

uint8_t scheduler_get_master_cycle_failures(void)
{
   // Cumulative since boot, unlike the live strike count
   return total_master_cycle_failures;
}

bool scheduler_master_eligible(void)
{
   // False once this device has formed a network as master and heard nothing for MASTER_INELIGIBLE_AFTER_CYCLES
   return failed_master_cycles < MASTER_INELIGIBLE_AFTER_CYCLES;
}

#if DIAGNOSTIC_BUILD

void scheduler_note_event(scheduler_event_t event, uint32_t value)
{
   // Runs in the radio interrupt
   switch (event)
   {
      case SCHEDULER_EVENT_SCHEDULE_HEARD:
         ++schedules_heard_total;
         if (catch_state.pending && !catch_state.heard)
         {
            catch_state.first_copy = (uint8_t)value;
            catch_state.heard_reference = schedule_phase_get_reference_time_full();
            catch_state.heard_timestamp = schedule_phase_get_timestamp();
            catch_state.heard = true;
         }
         break;
      case SCHEDULER_EVENT_SCHEDULE_RESEND_FAILED:
         if ((current_role == ROLE_MASTER) && (value == 1))
            round_flags |= STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED;
         break;
      case SCHEDULER_EVENT_JOIN_REQUEST_SENT:
         ++join_requests_sent;
         break;
      case SCHEDULER_EVENT_JOIN_REQUEST_HEARD:
         ++join_requests_heard;
         round_flags |= STORAGE_ROUND_START_FLAG_JOIN_HEARD;
         break;
      default:
         break;
   }
}

#endif

void scheduler_stop(void)
{
   // Notify the scheduling task that it is time to stop
#if DIAGNOSTIC_BUILD
   if (end_reason == STORAGE_SESSION_END_UNKNOWN)
      end_reason = STORAGE_SESSION_END_STOPPED;
#endif
   is_running = false;
   xTaskNotify(notification_handle, RANGING_STOP, eSetBits);
}
