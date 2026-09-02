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
} storage_diagnostics_t;

#define STORAGE_IMU_RECORD_BYTES                    (1 + 4 + 1 + MAX_IMU_DATA_LENGTH)
#define STORAGE_DIAGNOSTICS_RECORD_BYTES            (1 + 4 + sizeof(storage_diagnostics_t))
#define STORAGE_MAX_RECORD_BYTES                    ((STORAGE_IMU_RECORD_BYTES > STORAGE_DIAGNOSTICS_RECORD_BYTES) ? STORAGE_IMU_RECORD_BYTES : STORAGE_DIAGNOSTICS_RECORD_BYTES)


// Storage Record Manipulation API -------------------------------------------------------------------------------------

static inline void storage_retrieve_experiment_details(experiment_details_t *details)
{
   nandlog_retrieve_metadata(details, sizeof(*details));
}

static inline bool storage_store_experiment_details(const experiment_details_t *details)
{
   if (!nandlog_store_metadata(details, sizeof(*details)))
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
      default:
         return 0;
   }
}

static inline bool last_time_anchor_in_page(const uint8_t *payload, uint32_t length, uint32_t *experiment_ms, uint32_t *rtc)
{
   // Keep the newest anchor in this page; payloads are record-aligned so a forward walk is exact
   bool found = false;
   uint32_t offset = 0;
   while ((offset + 5) < length)
   {
      const uint32_t record_length = stored_record_length(payload, offset, length);
      if (!record_length || ((offset + record_length) > length))
         break;
      if (payload[offset] == STORAGE_TYPE_TIME_ANCHOR)
      {
         memcpy(experiment_ms, payload + offset + 1, sizeof(*experiment_ms));
         memcpy(rtc, payload + offset + 5, sizeof(*rtc));
         found = true;
      }
      offset += record_length;
   }
   return found;
}

static inline bool recover_time_anchor(uint32_t *experiment_ms, uint32_t *rtc)
{
   // The newest anchor pairs an experiment timestamp with the raw RTC value at the instant it was written
   static uint8_t page_buffer[NANDLOG_MAX_DATA_BYTES_PER_PAGE];
   for (uint32_t back = 0; back < ANCHOR_SEARCH_MAX_PAGES; ++back)
   {
      bool end_of_epoch = false;
      const uint32_t length = nandlog_read_recent_page(back, page_buffer, NULL, &end_of_epoch);
      if (length && last_time_anchor_in_page(page_buffer, length, experiment_ms, rtc))
         return true;
      if (end_of_epoch)
         break;
   }
   return false;
}

#endif

#endif  // #ifndef __STORAGE_RECORDS_HEADER_H__
