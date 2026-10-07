#ifndef __STORAGE_RECORDS_HEADER_H__
#define __STORAGE_RECORDS_HEADER_H__

#include "app_tasks.h"
#include "rtc.h"
#include "system.h"


// Storage Record Types and Definitions --------------------------------------------------------------------------------

typedef enum {
   STORAGE_TYPE_SHUTDOWN = 0,
   STORAGE_TYPE_VOLTAGE,
   STORAGE_TYPE_CHARGING_EVENT,
   STORAGE_TYPE_MOTION,
   STORAGE_TYPE_RANGES,
   STORAGE_TYPE_IMU,
   STORAGE_TYPE_BLE_SCAN,
   STORAGE_TYPE_RESET_REASON,
   STORAGE_TYPE_TIME_ANCHOR,
   STORAGE_TYPE_DIAGNOSTICS,
   STORAGE_TYPE_RADIO_ABORT,
   STORAGE_TYPE_SCHEDULE_CATCH,
   STORAGE_TYPE_ROUND_START,
   STORAGE_TYPE_SESSION_END,
   STORAGE_TYPE_RADIO_TIMING,
   STORAGE_NUM_TYPES,
} storage_data_type_t;

typedef struct __attribute__ ((__packed__))
{
   uint16_t watchdog_declines;                             // pets refused because some task was late
   uint8_t watchdog_late_episodes[WATCHDOG_NUM_TASKS];     // distinct episodes of lateness, per task
   uint16_t charger_suppressed_edges;                      // charger interrupts discarded as chatter
   uint16_t wsf_alloc_failures;                            // BLE buffer allocations that returned NULL
   uint16_t wsf_largest_failed_length;                      // size of the largest request that failed
   uint8_t wsf_pool_high_water[STORAGE_DIAGNOSTIC_NUM_POOLS];   // peak simultaneous allocations per pool
   uint8_t wsf_pool_capacity[STORAGE_DIAGNOSTIC_NUM_POOLS];     // buffers in each pool, so headroom is readable
   uint8_t master_cycle_failures;                          // times this device ran a network as master and heard nothing
   uint32_t firmware_revision;                             // leading eight hex digits of the git commit the firmware was built from
   uint8_t status_flags;                                   // STORAGE_DIAGNOSTIC_FLAG_* bits
   int8_t temperature_c;                                   // chip temperature, or BATTERY_TEMPERATURE_UNKNOWN before the first reading
   uint32_t radio_rx_ok;                                   // ranging slots that produced a decoded packet
   uint32_t radio_rx_failed;                               // ranging slots that timed out or errored -- the receive-sensitivity metric
   uint16_t radio_tx_late;                                 // delayed transmissions programmed after their slot had already passed
   uint16_t radio_rx_arm_late;                             // delayed receives that could not be armed in time, each aborting a round
   uint16_t radio_isr_over_budget;                         // radio interrupts that ran past RADIO_ISR_BUDGET_US
   uint16_t radio_isr_warm_max_us;                         // longest radio interrupt once the first few had run
   uint16_t radio_irq_stuck;                               // times the radio interrupt line stayed asserted and the radio was silenced
   uint16_t radio_wake_max_us;                             // worst radio wake-up, against RADIO_WAKEUP_SAFETY_DELAY_US
   uint16_t radio_wake_failures;                           // wake-ups the radio never answered, so it had to be reset
   uint16_t storage_records_dropped;                       // records discarded because the storage queue was full
   uint16_t stack_free_words[STORAGE_DIAGNOSTIC_NUM_STACKS];   // least free stack ever seen, per watchdog task then the timer service
   uint8_t ble_resets;                                     // Bluetooth controller restarts by the self-check
   uint16_t nand_bad_blocks;                               // retired flash blocks, factory-marked and grown
} storage_diagnostics_t;

#define STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE    0x01   // this chip's trims support TempCo
#define STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED      0x02   // the last temperature reading adjusted the voltage trims
#define STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED   0x04   // built from a tree with uncommitted firmware changes
#define STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD    0x08   // built with DIAGNOSTIC_BUILD: radio aborts are logged and interrupts timed
#define STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED     0x10   // built with TempCo switched off, whatever the chip supports
#define STORAGE_DIAGNOSTIC_STACK_UNMONITORED        0xFFFF // a stack entry for a task that is not running in this mode

// One radio receive that could not be armed before its slot, logged only by a DIAGNOSTIC_BUILD
typedef struct __attribute__ ((__packed__))
{
   uint8_t phase;                                          // STORAGE_RADIO_ABORT_PHASE_*
   uint8_t slot;                                           // slot within that phase
   uint8_t schedule_size;                                  // devices in this round's schedule
   int16_t late_us;                                        // how far past the arm deadline the attempt came, saturating
   uint16_t isr_elapsed_us;                                // time already spent in this radio interrupt, or 0xFFFF if unmeasurable
   uint8_t isr_events;                                     // radio events serviced by this interrupt so far
   uint16_t since_temperature_ms;                          // since the last 10 s temperature refresh, saturating, 0xFFFF if none
   uint8_t trigger;                                        // STORAGE_RADIO_ABORT_TRIGGER_*: the radio event this interrupt was handling
   uint16_t isr_entry_us;                                  // when this interrupt started, after the round's reference, saturating, 0xFFFF if unmeasurable
   int16_t event_to_isr_us;                                // from the triggering frame's radio timestamp to the interrupt starting, saturating, or STORAGE_RADIO_ABORT_NO_EVENT_TIME
   uint16_t asleep_us;                                     // how long the processor had slept when this interrupt woke it, 0 if it arrived as the processor was going to sleep, 0xFFFF if it was awake
   uint16_t wake_to_isr_us;                                // from the processor waking to this interrupt starting, saturating, 0xFFFF if it was awake
} storage_radio_abort_t;

#define STORAGE_RADIO_ABORT_PHASE_RANGING           1      // the round is abandoned
#define STORAGE_RADIO_ABORT_PHASE_STATUS            2      // the status exchange ends early and the round is computed from what arrived
#define STORAGE_RADIO_ABORT_UNMEASURED              0xFFFF
#define STORAGE_RADIO_ABORT_TRIGGER_UNKNOWN         0      // not inside a radio interrupt, or before it had handled an event
#define STORAGE_RADIO_ABORT_TRIGGER_TX_DONE         1      // a frame this device sent
#define STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME        2      // a frame this device received
#define STORAGE_RADIO_ABORT_TRIGGER_RX_TIMEOUT      3      // a receive window that closed empty
#define STORAGE_RADIO_ABORT_TRIGGER_RX_ERROR        4      // a frame that could not be decoded
#define STORAGE_RADIO_ABORT_NO_EVENT_TIME           (-32768)    // the event has no radio timestamp, or the interrupt start is unknown

// How a participant's timed wake-up found the next round's schedule, logged only by a DIAGNOSTIC_BUILD
typedef struct __attribute__ ((__packed__))
{
   uint8_t first_copy;                                     // sequence number of the first copy decoded: 0-1 sent by the master, 2-4 relayed
   uint8_t rounds_missed;                                  // rounds that went by before a copy was decoded, or 0xFF if the schedule timestamps went backwards
   int32_t lead_us;                                        // receiver on this long before the expected round's first copy, negative if after it
   uint16_t timer_to_task_us;                              // wake-up timer firing to the ranging task running, saturating
   uint16_t wake_us;                                       // radio wake-up, 0xFFFF if the radio had to be reset
   uint8_t rx_errors;                                      // frames heard but not decodable before the schedule, saturating
   uint8_t other_frames;                                   // decodable frames before the schedule that were not one, saturating
   uint16_t first_error_us;                                // receiver on to the first undecodable frame, saturating, 0xFFFF if none
   int16_t carrier_offset_cppm;                            // carrier offset of the decoded copy in hundredths of a ppm, as the DW3000 reports it
   int16_t wake_correction_us;                             // what this device had learned to add to RADIO_WAKEUP_SAFETY_DELAY_US for this wake-up
   uint16_t timer_latency_us;                              // wake-up timer's compare match to its interrupt running, saturating
} storage_schedule_catch_t;

#define STORAGE_SCHEDULE_CATCH_NONE                 0xFF   // first_copy when the network was lost before any copy arrived
#define STORAGE_SCHEDULE_CATCH_UNKNOWN              0xFF   // rounds_missed when the schedule timestamps went backwards
#define STORAGE_SCHEDULE_CATCH_UNMEASURED           0xFFFF

// How the master started a round and how it went, logged by a DIAGNOSTIC_BUILD as the round ends
typedef struct __attribute__ ((__packed__))
{
   uint16_t timer_to_task_us;                              // wake-up timer firing to the ranging task running, saturating
   uint16_t wake_us;                                       // radio wake-up, 0 if it was already awake, 0xFFFF if it had to be reset
   uint16_t timer_to_transmit_us;                          // wake-up timer firing to the first copy being sent, saturating
   uint8_t flags;                                          // STORAGE_ROUND_START_FLAG_* bits
   uint8_t schedule_size;                                  // devices in this round's schedule
   uint8_t devices_ranged;                                 // ranges the master computed this round
   uint16_t timer_latency_us;                              // wake-up timer's compare match to its interrupt running, saturating
} storage_round_start_t;

#define STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED 0x01   // the master's second schedule copy could not be armed in time
#define STORAGE_ROUND_START_FLAG_COMPUTED           0x02   // the round reached the computation phase
#define STORAGE_ROUND_START_FLAG_ABANDONED          0x04   // the round ended early on a radio or ranging error
#define STORAGE_ROUND_START_FLAG_JOIN_HEARD         0x08   // the join window heard a request directly
#define STORAGE_ROUND_START_FLAG_JOIN_RELAYED       0x10   // a request arrived relayed through the status exchange

// Why one run of the ranging scheduler ended, logged only by a DIAGNOSTIC_BUILD
typedef struct __attribute__ ((__packed__))
{
   uint8_t reason;                                         // STORAGE_SESSION_END_* code
   uint8_t role;                                           // schedule_role_t when it ended
   uint8_t schedule_size;                                  // devices in the last schedule this device knew
   uint8_t collision_phase;                                // scheduler phase an unexpected frame arrived in, for a collision
   uint8_t collision_type;                                 // that frame's message type byte
   uint8_t collision_source;                               // the byte after its header: the sender, for every type but a ranging packet
   uint16_t collision_at_us;                               // how far into the round it arrived, saturating
   uint32_t session_ms;                                    // how long this run lasted
   uint16_t rounds_ranged;                                 // rounds computed while scheduled, saturating
   uint16_t schedules_heard;                               // schedules decoded, saturating
   uint16_t join_requests_sent;                            // rounds spent unscheduled, each sending a join request, saturating
   uint16_t join_requests_heard;                           // join requests that reached this device as master, saturating
   uint16_t listen_errors;                                 // undecodable frames while listening for a schedule, saturating
   uint8_t stalls;                                         // times no round completed for RANGING_ROUND_STALL_TIMEOUT_MS, saturating
} storage_session_end_t;

#define STORAGE_SESSION_END_UNKNOWN                 0
#define STORAGE_SESSION_END_STOPPED                 1      // the application stopped it, as it does on finding a higher-ID master
#define STORAGE_SESSION_END_SEARCH_TIMEOUT          2      // no round completed within NETWORK_SEARCH_TIME_SECONDS
#define STORAGE_SESSION_END_COLLISION               3      // a frame of an unexpected type arrived mid-round
#define STORAGE_SESSION_END_SILENT                  4      // as master, heard nobody for MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE rounds

// How close a minute of receives came to their deadlines, logged only by a DIAGNOSTIC_BUILD
#define STORAGE_RADIO_TIMING_BANDS                  7      // event_to_isr_counts: below FIRST_US, then STEP_US wide, the last open-ended
#define STORAGE_RADIO_TIMING_FIRST_US               40
#define STORAGE_RADIO_TIMING_STEP_US                5

typedef struct __attribute__ ((__packed__))
{
   uint16_t arms;                                          // receives armed in time straight after a received frame, saturating
   uint16_t after_sleep;                                   // of those, ones whose interrupt had to wake the processor first
   uint16_t during_sleep_entry;                            // of those, ones whose interrupt arrived as the processor was going to sleep
   int16_t slack_min_us;                                   // least time to spare at any of them, 0x7FFF if none
   uint16_t slack_under_25_us;                             // how many had less than 25 us to spare
   uint16_t event_to_isr_min_us;                           // fastest from the frame's radio timestamp to the interrupt starting, 0xFFFF if none
   uint16_t event_to_isr_max_us;                           // slowest
   uint16_t event_to_isr_counts[STORAGE_RADIO_TIMING_BANDS];   // how many fell in each band
   uint16_t wake_to_isr_max_us;                            // longest from the processor waking to the radio interrupt starting
   uint8_t antenna;                                        // antenna in use for single-antenna exchanges when written, from 0
   uint8_t antenna_changes;                                // times that choice moved during the minute, saturating
} storage_radio_timing_t;

#define STORAGE_RADIO_TIMING_INTERVAL_MS            60000
#define STORAGE_MAX_TRACE_BYTES                     sizeof(storage_radio_timing_t)

_Static_assert((sizeof(storage_radio_abort_t) <= STORAGE_MAX_TRACE_BYTES) && (sizeof(storage_schedule_catch_t) <= STORAGE_MAX_TRACE_BYTES) && (sizeof(storage_round_start_t) <= STORAGE_MAX_TRACE_BYTES) && (sizeof(storage_session_end_t) <= STORAGE_MAX_TRACE_BYTES), "every diagnostic trace record must fit the shared trace buffer");

void storage_write_radio_abort(uint32_t timestamp, const storage_radio_abort_t *abort);
void storage_write_schedule_catch(uint32_t timestamp, const storage_schedule_catch_t *catch_record);
void storage_write_round_start(uint32_t timestamp, const storage_round_start_t *round_start);
void storage_write_session_end(uint32_t timestamp, const storage_session_end_t *session_end);
void storage_write_radio_timing(uint32_t timestamp, const storage_radio_timing_t *timing);

#define STORAGE_IMU_RECORD_BYTES                    (1 + 4 + 1 + MAX_IMU_DATA_LENGTH)
#define STORAGE_DIAGNOSTICS_RECORD_BYTES            (1 + 4 + sizeof(storage_diagnostics_t))
#define STORAGE_MAX_RECORD_BYTES                    ((STORAGE_IMU_RECORD_BYTES > STORAGE_DIAGNOSTICS_RECORD_BYTES) ? STORAGE_IMU_RECORD_BYTES : STORAGE_DIAGNOSTICS_RECORD_BYTES)


// Storage Record Manipulation API -------------------------------------------------------------------------------------

static inline void storage_retrieve_experiment_details(experiment_details_t *details)
{
   nandlog_retrieve_epoch_details(details, sizeof(*details));
}

static inline bool storage_store_experiment_details(const experiment_details_t *details)
{
   if (!nandlog_begin_epoch(details, sizeof(*details)))
      return false;

   const uint32_t timestamp = rtc_get_timestamp(), time_of_day = rtc_get_time_of_day();
   const bool valid_experiment = rtc_is_valid() && details->num_devices && !details->is_terminated;
   const bool active_experiment = valid_experiment &&
         (timestamp >= details->experiment_start_time) && (timestamp < details->experiment_end_time) &&
         (!details->use_daily_times ||
            ((details->daily_start_time < details->daily_end_time) &&
               (time_of_day >= details->daily_start_time) && (time_of_day < details->daily_end_time)) ||
            ((details->daily_start_time > details->daily_end_time) &&
               ((time_of_day >= details->daily_start_time) || (time_of_day < details->daily_end_time))));
   nandlog_disable(!active_experiment);
   return true;
}

static inline uint32_t storage_experiment_ms_from_rtc(uint32_t rtc_timestamp)
{
   const uint32_t start = app_get_experiment_start_time();
   return (rtc_timestamp >= start) ? (1000 * (rtc_timestamp - start)) : 0;
}

#if !defined(_TEST_NO_STORAGE)

#define ANCHOR_SEARCH_MAX_PAGES   8

static inline uint32_t stored_record_length(const uint8_t *payload, uint32_t offset, uint32_t length)
{
   switch (payload[offset])
   {
      case STORAGE_TYPE_VOLTAGE:
         return 9;
      case STORAGE_TYPE_CHARGING_EVENT:
      case STORAGE_TYPE_MOTION:
         return 6;
      case STORAGE_TYPE_RANGES:
         return ((offset + 6) <= length) ? (6 + (payload[offset + 5] * COMPRESSED_RANGE_DATUM_LENGTH)) : 0;
      case STORAGE_TYPE_IMU:
         return ((offset + 6) <= length) ? (5 + payload[offset + 5]) : 0;   // the length byte counts itself
      case STORAGE_TYPE_BLE_SCAN:
         return ((offset + 6) <= length) ? (6 + payload[offset + 5]) : 0;
      case STORAGE_TYPE_RESET_REASON:
         return 7;
      case STORAGE_TYPE_TIME_ANCHOR:
         return 9;
      case STORAGE_TYPE_DIAGNOSTICS:
         return 5 + sizeof(storage_diagnostics_t);
      case STORAGE_TYPE_RADIO_ABORT:
         return 5 + sizeof(storage_radio_abort_t);
      case STORAGE_TYPE_SCHEDULE_CATCH:
         return 5 + sizeof(storage_schedule_catch_t);
      case STORAGE_TYPE_ROUND_START:
         return 5 + sizeof(storage_round_start_t);
      case STORAGE_TYPE_SESSION_END:
         return 5 + sizeof(storage_session_end_t);
      case STORAGE_TYPE_RADIO_TIMING:
         return 5 + sizeof(storage_radio_timing_t);
      default:
         return 0;
   }
}

static inline bool last_time_anchor_in_page(const uint8_t *payload, uint32_t length, bool framed, uint32_t *experiment_ms, uint32_t *rtc)
{
   // Keep the newest anchor in this page; payloads are record-aligned so a forward walk is exact
   bool found = false;
   uint32_t offset = 0;
   while ((offset + 5) <= length)
   {
      const uint8_t *record = NULL;
      if (framed)
      {
         // The framing is stepped by nandlog which owns the layout
         uint32_t record_bytes = 0;
         if (!nandlog_framed_next_record(payload, length, &offset, &record, &record_bytes))
            break;
      }
      else
      {
         const uint32_t record_length = stored_record_length(payload, offset, length);
         if (!record_length || ((offset + record_length) > length))
            break;
         record = payload + offset;
         offset += record_length;
      }
      if (record[0] == STORAGE_TYPE_TIME_ANCHOR)
      {
         memcpy(experiment_ms, record + 1, sizeof(*experiment_ms));
         memcpy(rtc, record + 5, sizeof(*rtc));
         found = true;
      }
   }
   return found;
}

static inline bool recover_time_anchor(uint32_t *experiment_ms, uint32_t *rtc)
{
   // The newest anchor pairs an experiment timestamp with the device's own clock at the instant it was written
   static uint8_t page_buffer[NANDLOG_MAX_DATA_BYTES_PER_PAGE];
   for (uint32_t back = 0; back < ANCHOR_SEARCH_MAX_PAGES; ++back)
   {
      bool end_of_epoch = false;
      nandlog_page_header_t header = { 0 };
      const uint32_t length = nandlog_read_recent_page(back, page_buffer, &header, &end_of_epoch);
      if (length && last_time_anchor_in_page(page_buffer, length, header.magic == NANDLOG_PAGE_MAGIC_FRAMED, experiment_ms, rtc))
         return true;
      if (end_of_epoch)
         break;
   }
   return false;
}

static inline bool time_anchor_is_plausible(uint32_t anchor_network_ms, uint32_t anchor_local_ms)
{
   // Two sanity checks on a recovered anchor: An anchor from the future cannot exist and the offset itself must be plausible
   const uint32_t now_local_ms = app_get_experiment_time(0);
   if (anchor_local_ms > now_local_ms)
      return false;
   const int64_t offset = (int64_t)anchor_network_ms - (int64_t)anchor_local_ms;
   return (offset <= STORAGE_MAX_PLAUSIBLE_OFFSET_MS) && (offset >= -STORAGE_MAX_PLAUSIBLE_OFFSET_MS);
}

#endif

#endif  // #ifndef __STORAGE_RECORDS_HEADER_H__
