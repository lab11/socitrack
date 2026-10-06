#ifndef __LIVE_STATS_FUNCTIONALITY_HEADER_H__
#define __LIVE_STATS_FUNCTIONALITY_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Live Radio Statistics -----------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   uint8_t version;                                        // BLE_RADIO_STATS_VERSION, so a reader can refuse a layout it does not know
   uint8_t role;                                           // schedule_role_t now
   uint8_t schedule_size;                                  // devices in the current schedule
   uint8_t flags;                                          // BLE_RADIO_STATS_FLAG_* bits
   uint16_t test_seconds_left;                             // until the radio test ends, 0 outside one
   uint32_t rounds_scheduled;                              // rounds this badge took part in
   uint32_t rounds_ranged;                                 // of those, rounds that produced at least one range
   uint32_t rx_ok;                                         // ranging slots that produced a decoded packet
   uint32_t rx_failed;                                     // ranging slots that timed out or errored
   uint32_t rx_ok_by_antenna[NUM_XMIT_ANTENNAS];           // rx_ok split by the antenna used
   uint32_t rx_failed_by_antenna[NUM_XMIT_ANTENNAS];       // rx_failed split the same way
   uint16_t tx_late;                                       // delayed transmissions programmed after their slot had passed, saturating
   uint16_t rx_arm_late;                                   // delayed receives that could not be armed in time, saturating
   uint16_t isr_over_budget;                               // radio interrupts that ran past RADIO_ISR_BUDGET_US, saturating
   uint16_t wake_max_us;                                   // worst radio wake-up, saturating
   uint16_t wake_failures;                                 // wake-ups the radio never answered, saturating
} ble_radio_stats_t;

#define BLE_RADIO_STATS_VERSION                     1
#define BLE_RADIO_STATS_FLAG_TEST_RUNNING           0x01   // this boot is a radio test
#define BLE_RADIO_STATS_FLAG_TEST_WAITING           0x02   // a radio test waiting to be re-sent its badge list


// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleLiveStatsRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr);
uint8_t handleLiveStatsWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr);
void updateRangeResults(dmConnId_t connId, const uint8_t *results, uint16_t results_length);
void updateImuData(dmConnId_t connId, const uint8_t *results, uint16_t results_length);
void fillRadioStats(ble_radio_stats_t *stats);

#endif  // #ifndef __LIVE_STATS_FUNCTIONALITY_HEADER_H__
