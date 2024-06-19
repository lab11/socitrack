#ifndef __LIVE_STATS_SERVICE_HEADER_H__
#define __LIVE_STATS_SERVICE_HEADER_H__

// Live Statistics Handle Enumerations ---------------------------------------------------------------------------------

enum
{
   LIVE_STATS_SERVICE_HANDLE = 0x0040,      // Live statistics service
   BATTERY_LEVEL_CHAR_HANDLE,               // Battery level characteristic
   BATTERY_LEVEL_HANDLE,                    // Battery level
   BATTERY_LEVEL_DESC_HANDLE,               // Battery level description
   TIMESTAMP_CHAR_HANDLE,                   // Current timestamp characteristic
   TIMESTAMP_HANDLE,                        // Current timestamp
   TIMESTAMP_DESC_HANDLE,                   // Current timestamp description
   FIND_MY_TOTTAG_CHAR_HANDLE,              // FindMyTottag characteristic
   FIND_MY_TOTTAG_HANDLE,                   // FindMyTottag value
   FIND_MY_TOTTAG_DESC_HANDLE,              // FindMyTottag description
   RANGES_CHAR_HANDLE,                      // Current ranges characteristic
   RANGES_HANDLE,                           // Current ranges
   RANGES_DESC_HANDLE,                      // Current ranges description
   RANGES_CCC_HANDLE,                       // Current ranges CCCD
   IMU_DATA_CHAR_HANDLE,                    // Current imu data characteristic
   IMU_DATA_HANDLE,                         // Current imu data
   IMU_DATA_DESC_HANDLE,                    // Current imu data description
   IMU_DATA_CCC_HANDLE,                     // Current imu data CCCD
   LIVE_STATS_MAX_HANDLE                    // Maximum live statistics handle
};


// Public API ----------------------------------------------------------------------------------------------------------

void liveStatsAddGroup(void);
void liveStatsRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback);

#endif  // #ifndef __LIVE_STATS_SERVICE_HEADER_H__
