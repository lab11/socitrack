// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "wsf_types.h"
#include "att_api.h"
#include "live_stats_functionality.h"
#include "live_stats_service.h"
#include "logging.h"
#include "rtc.h"
#include "system.h"


// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleLiveStatsRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr)
{
   print("TotTag BLE: Live Stats Read: connID = %d, handle = %d, operation = %d\n", connId, handle, operation);
   if (handle == BATTERY_LEVEL_HANDLE)
      *(uint16_t*)pAttr->pValue = (uint16_t)battery_monitor_get_level_mV();
   else if (handle == TIMESTAMP_HANDLE)
      *(uint32_t*)pAttr->pValue = rtc_get_timestamp();
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


void updateIMUData(dmConnId_t connId, const uint8_t *results, uint16_t results_length)
{
#ifdef _LIVE_IMU_DATA
   // Update the imu data characteristic
   if (connId != DM_CONN_ID_NONE)
      AttsHandleValueNtf(connId, IMU_DATA_HANDLE, results_length, (uint8_t*)results);
#endif
}
