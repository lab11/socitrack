// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "wsf_types.h"
#include "att_api.h"
#include "live_stats_functionality.h"
#include "live_stats_service.h"
#include "logging.h"
#include "radio_test.h"
#include "ranging.h"
#include "rtc.h"
#include "scheduler.h"
#include "system.h"


// Private Helper Functions --------------------------------------------------------------------------------------------

static uint16_t saturate_u16(uint32_t value)
{
   return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
}


// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleLiveStatsRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr)
{
   print("TotTag BLE: Live Stats Read: connID = %d, handle = %d, operation = %d\n", connId, handle, operation);
   if (handle == BATTERY_LEVEL_HANDLE)
      *(uint16_t*)pAttr->pValue = (uint16_t)battery_monitor_get_level_mV();
   else if (handle == TIMESTAMP_HANDLE)
      *(uint32_t*)pAttr->pValue = rtc_get_timestamp();
   else if ((handle == RADIO_STATS_HANDLE) && !offset)
      fillRadioStats((ble_radio_stats_t*)pAttr->pValue);
   return ATT_SUCCESS;
}

uint8_t handleLiveStatsWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr)
{
   print("TotTag BLE: Live Stats Write: connID = %d handle = %d, value = %d\n", connId, handle, *pValue);
   if (handle == FIND_MY_TOTTAG_HANDLE)
      app_activate_find_my_tottag(*(uint32_t*)pValue);
   else if (handle == TIMESTAMP_HANDLE)
      rtc_set_time_from_timestamp(*(uint32_t*)pValue);
#ifdef _REMOTE_MODE_SWITCH_ENABLED
   else if (handle == APP_MODE_SWITCH_HANDLE)
      app_allow_downloads(*(uint8_t*)pValue);
#endif
   return ATT_SUCCESS;
}

void updateRangeResults(dmConnId_t connId, const uint8_t *results, uint16_t results_length)
{
   // Update the BLE ranges characteristic
   if (connId != DM_CONN_ID_NONE)
      AttsHandleValueNtf(connId, RANGES_HANDLE, results_length, (uint8_t*)results);
}

void fillRadioStats(ble_radio_stats_t *stats)
{
   // A snapshot of the counters the ranging radio and scheduler keep anyway
   ranging_radio_stats_t radio;
   ranging_radio_get_stats(&radio);
   uint32_t rounds_scheduled = 0, rounds_ranged = 0;
   scheduler_get_round_counts(&rounds_scheduled, &rounds_ranged);
   stats->version = BLE_RADIO_STATS_VERSION;
   stats->role = (uint8_t)scheduler_get_current_role();
   stats->schedule_size = radio.network_size;
   stats->flags = (radio_test_running() ? BLE_RADIO_STATS_FLAG_TEST_RUNNING : 0) | (radio_test_waiting_for_devices() ? BLE_RADIO_STATS_FLAG_TEST_WAITING : 0);
   stats->test_seconds_left = saturate_u16(radio_test_seconds_left());
   stats->rounds_scheduled = rounds_scheduled;
   stats->rounds_ranged = rounds_ranged;
   stats->rx_ok = radio.rx_ok;
   stats->rx_failed = radio.rx_failed;
   for (uint32_t i = 0; i < NUM_XMIT_ANTENNAS; ++i)
   {
      stats->rx_ok_by_antenna[i] = radio.rx_ok_antenna[i];
      stats->rx_failed_by_antenna[i] = radio.rx_failed_antenna[i];
   }
   stats->tx_late = saturate_u16(radio.tx_failed);
   stats->rx_arm_late = saturate_u16(radio.rx_arm_failed);
   stats->isr_over_budget = saturate_u16(radio.isr_over_count);
   stats->wake_max_us = saturate_u16(radio.wake_max_us);
   stats->wake_failures = saturate_u16(radio.wake_failed);
}

void updateImuData(dmConnId_t connId, const uint8_t *results, uint16_t results_length)
{
   // Update the IMU data characteristic
   if (connId != DM_CONN_ID_NONE)
      AttsHandleValueNtf(connId, IMU_DATA_HANDLE, results_length, (uint8_t*)results);
}
